// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2013-2021 Canonical, Ltd.
 * Copyright (C) 2021-2023 Colin Ian King.
 *
 */
#include "stress-ng.h"
#include "core-builtin.h"
#include "core-killpid.h"
#include "core-out-of-memory.h"

static const stress_help_t help[] = {
	{ NULL,	"hrtimers N",	  "start N workers that exercise high resolution timers" },
	{ NULL, "hrtimers-adjust","adjust rate to try and maximum timer rate" },
	{ NULL,	"hrtimers-ops N", "stop after N bogo high-res timer bogo operations" },
	{ NULL,	NULL,		  NULL }
};

static int stress_set_hrtimers_adjust(const char *opt)
{
        return stress_set_setting_true("hrtimers-adjust", opt);
}

static const stress_opt_set_func_t opt_set_funcs[] = {
	{ OPT_hrtimers_adjust,	stress_set_hrtimers_adjust },
	{ 0,			NULL },
};

#if defined(HAVE_LIB_RT) &&		\
    defined(HAVE_TIMER_CREATE) &&	\
    defined(HAVE_TIMER_DELETE) &&	\
    defined(HAVE_TIMER_SETTIME)
static uint64_t timer_counter;
static uint64_t max_ops;
static timer_t timerid;
static double time_end;
static long ns_delay;
static int overrun;
void *lock;

#define PROCS_MAX	(8)

/*
 *  stress_hrtimers_set()
 *	set timer, ensure it is never zero
 */
static void stress_hrtimers_set(struct itimerspec *timer)
{
	timer->it_value.tv_nsec = (ns_delay < 0) ? stress_mwc16() + 1 : ns_delay;
	timer->it_value.tv_sec = 0;
	timer->it_interval.tv_sec = timer->it_value.tv_sec;
	timer->it_interval.tv_nsec = timer->it_value.tv_nsec;
}

/*
 *  stress_hrtimers_stress_continue(args)
 *      returns true if we can keep on running a stressor
 */
static bool HOT OPTIMIZE3 stress_hrtimers_stress_continue(void)
{
	return (LIKELY(stress_continue_flag()) &&
		LIKELY(!max_ops || ((timer_counter) < max_ops)));
}

/*
 *  stress_hrtimers_handler()
 *	catch timer signal and cancel if no more runs flagged
 */
static void MLOCKED_TEXT OPTIMIZE3 stress_hrtimers_handler(int sig)
{
	struct itimerspec timer;
	sigset_t mask;

	(void)sig;

	timer_counter++;
	if (UNLIKELY(!stress_hrtimers_stress_continue()))
		goto cancel;

	if (UNLIKELY((timer_counter & 4095) == 0)) {
		if (ns_delay >= 0) {
			const long ns_adjust = ns_delay >> 2;

			if (timer_getoverrun(timerid)) {
				ns_delay += ns_adjust;
			} else {
				ns_delay -= ns_adjust;
			}
		}
		stress_hrtimers_set(&timer);
		(void)timer_settime(timerid, 0, &timer, NULL);

		/* check periodically for timeout */
		if ((timer_counter & 65535) == 0) {
			if ((sigpending(&mask) == 0) && (sigismember(&mask, SIGINT)))
				goto cancel;
			if (stress_time_now() > time_end)
				goto cancel;
		}
	}
	return;

cancel:
	stress_continue_set_flag(false);
	/* Cancel timer if we detect no more runs */
	(void)shim_memset(&timer, 0, sizeof(timer));
	(void)timer_settime(timerid, 0, &timer, NULL);
	(void)shim_kill(getpid(), SIGALRM);
}


/*
 *  stress_hrtimer_process
 *	stress timer child process
 */
static int stress_hrtimer_process(const stress_args_t *args)
{
	struct sigaction action;
	struct sigevent sev;
	struct itimerspec timer;
	sigset_t mask;

	time_end = args->time_end;

	(void)sigemptyset(&mask);
	(void)sigaddset(&mask, SIGINT);
	(void)sigprocmask(SIG_SETMASK, &mask, NULL);

	VOID_RET(int, stress_set_sched(getpid(), SCHED_RR, UNDEFINED, true));

	(void)shim_memset(&action, 0, sizeof action);
	action.sa_handler = stress_hrtimers_handler;
	(void)sigemptyset(&action.sa_mask);

	if (sigaction(SIGRTMIN, &action, NULL) < 0)
		return EXIT_FAILURE;

	(void)shim_memset(&sev, 0, sizeof(sev));
	sev.sigev_notify = SIGEV_SIGNAL;
	sev.sigev_signo = SIGRTMIN;
	sev.sigev_value.sival_ptr = &timerid;
	if (timer_create(CLOCK_REALTIME, &sev, &timerid) < 0) {
		if ((errno == EAGAIN) || (errno == ENOMEM)) {
			pr_inf_skip("%s: timer_create, errno=%d (%s), skipping stessor\n",
				args->name, errno, strerror(errno));
			return EXIT_NO_RESOURCE;
		}
		pr_fail("%s: timer_create failed, errno=%d (%s)\n",
			args->name, errno, strerror(errno));
		return EXIT_FAILURE;
	}

	stress_hrtimers_set(&timer);
	if (timer_settime(timerid, 0, &timer, NULL) < 0) {
		pr_fail("%s: timer_settime failed, errno=%d (%s)\n",
			args->name, errno, strerror(errno));
		return EXIT_FAILURE;
	}

	do {
		shim_usleep(100000);
	} while (stress_hrtimers_stress_continue());

	stress_bogo_add_lock(args, lock, timer_counter);

	if (timer_delete(timerid) < 0) {
		pr_fail("%s: timer_delete failed, errno=%d (%s)\n",
			args->name, errno, strerror(errno));
		return EXIT_FAILURE;
	}
	return EXIT_SUCCESS;
}

static int stress_hrtimers(const stress_args_t *args)
{
	pid_t pids[PROCS_MAX];
	size_t i;
        bool hrtimers_adjust = false;
	double start_time = -1.0, end_time;
	sigset_t mask;

	if (stress_sigchld_set_handler(args) < 0)
		return EXIT_NO_RESOURCE;

	lock = stress_lock_create();
	if (!lock) {
		pr_inf("%s: cannot create lock, skipping stressor\n", args->name);
		return EXIT_NO_RESOURCE;
	}

        (void)stress_get_setting("hrtimers-adjust", &hrtimers_adjust);
	overrun = 0;
	ns_delay = hrtimers_adjust ? 10000 : -1;
	max_ops = args->max_ops / PROCS_MAX;

	(void)shim_memset(pids, 0, sizeof(pids));
	stress_set_proc_state(args->name, STRESS_STATE_RUN);

	for (i = 0; i < PROCS_MAX; i++) {
		if (!stress_continue(args))
			goto reap;

		pids[i] = fork();
		if (pids[i] == 0) {
			/* Child */
			stress_parent_died_alarm();
			stress_set_oom_adjustment(args, true);
			(void)sched_settings_apply(true);
			stress_hrtimer_process(args);
			_exit(EXIT_SUCCESS);
		}
	}

	(void)sigemptyset(&mask);
        (void)sigaddset(&mask, SIGRTMIN);
	(void)sigprocmask(SIG_BLOCK, &mask, NULL);

	start_time = stress_time_now();
	do {
		shim_usleep(100000);
	} while (stress_continue(args));

reap:
	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);

	stress_kill_and_wait_many(args, pids, PROCS_MAX, SIGALRM, true);
	end_time = stress_time_now();

	if (start_time >= 0.0) {
		const double dt = end_time - start_time;

		if (dt > 0.0) {
			const double rate = (double)stress_bogo_get(args) / dt;

			pr_dbg("%s: hrtimer signals at %.3f MHz\n", args->name, rate / 1000000.0);
			stress_metrics_set(args, 0, "hrtimer signals per sec", rate);
		}
	}
	stress_lock_destroy(lock);

	return EXIT_SUCCESS;
}

stressor_info_t stress_hrtimers_info = {
	.stressor = stress_hrtimers,
	.class = CLASS_SCHEDULER,
	.opt_set_funcs = opt_set_funcs,
	.verify = VERIFY_ALWAYS,
	.help = help
};
#else
stressor_info_t stress_hrtimers_info = {
	.stressor = stress_unimplemented,
	.class = CLASS_SCHEDULER,
	.opt_set_funcs = opt_set_funcs,
	.verify = VERIFY_ALWAYS,
	.help = help,
	.unimplemented_reason = "built without librt or hrtimer support"
};
#endif
