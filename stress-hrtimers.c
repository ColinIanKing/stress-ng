/*
 * Copyright (C) 2013-2021 Canonical, Ltd.
 * Copyright (C) 2021-2024 Colin Ian King.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 *
 */
#include "stress-ng.h"
#include "core-builtin.h"
#include "core-killpid.h"
#include "core-out-of-memory.h"

#include <sched.h>

static const stress_help_t help[] = {
	{ NULL,	"hrtimers N",	  "start N workers that exercise high resolution timers" },
	{ NULL, "hrtimers-adjust","adjust rate to try and maximum timer rate" },
	{ NULL,	"hrtimers-ops N", "stop after N bogo high-res timer bogo operations" },
	{ NULL,	NULL,		  NULL }
};

static const stress_opt_t opts[] = {
	{ OPT_hrtimers_adjust, "hrtimers-adjust", TYPE_ID_BOOL, 0, 1, NULL },
	END_OPT,
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
static bool OPTIMIZE3 stress_hrtimers_stress_continue(void)
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
static int stress_hrtimer_process(stress_args_t *args)
{
	struct sigaction action;
	struct sigevent sev;
	struct itimerspec timer;
	sigset_t mask;
	int sched;

	time_end = args->time_end;

	(void)sigemptyset(&mask);
	(void)sigaddset(&mask, SIGINT);
	(void)sigprocmask(SIG_SETMASK, &mask, NULL);
	/* If sched is not set, use SCHED_RR as default */
	if (!stress_get_setting("sched", &sched)) {
		VOID_RET(int, stress_set_sched(getpid(), SCHED_RR, UNDEFINED, true));
	}

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

static int stress_hrtimers(stress_args_t *args)
{
	stress_pid_t *s_pids, *s_pids_head = NULL;
	size_t i;
	bool hrtimers_adjust = false;
	double start_time = -1.0, end_time;
	sigset_t mask;
	int rc = EXIT_SUCCESS;

	if (stress_sigchld_set_handler(args) < 0)
		return EXIT_NO_RESOURCE;

	s_pids = stress_s_pids_mmap(PROCS_MAX);
	if (s_pids == MAP_FAILED) {
		pr_inf_skip("%s: failed to mmap %d PIDs, skipping stressor\n", args->name, PROCS_MAX);
		return EXIT_NO_RESOURCE;
	}

	lock = stress_lock_create();
	if (!lock) {
		pr_inf("%s: cannot create lock, skipping stressor\n", args->name);
		rc = EXIT_NO_RESOURCE;
		goto tidy_s_pids;
	}

        (void)stress_get_setting("hrtimers-adjust", &hrtimers_adjust);
	overrun = 0;
	ns_delay = hrtimers_adjust ? 10000 : -1;
	max_ops = args->max_ops / PROCS_MAX;

	for (i = 0; i < PROCS_MAX; i++) {
		stress_sync_start_init(&s_pids[i]);

		if (!stress_continue(args))
			goto reap;

		s_pids[i].pid = fork();
		if (s_pids[i].pid == 0) {
			/* Child */
			s_pids[i].pid = getpid();
			stress_sync_start_wait_s_pid(&s_pids[i]);

			stress_parent_died_alarm();
			stress_set_oom_adjustment(args, true);
			(void)sched_settings_apply(true);
			stress_hrtimer_process(args);
			_exit(EXIT_SUCCESS);
		} else if (s_pids[i].pid > 0) {
			stress_sync_start_s_pid_list_add(&s_pids_head, &s_pids[i]);
		}
	}

	stress_set_proc_state(args->name, STRESS_STATE_SYNC_WAIT);
	stress_sync_start_wait(args);
	stress_sync_start_cont_list(s_pids_head);
	stress_set_proc_state(args->name, STRESS_STATE_RUN);

	(void)sigemptyset(&mask);
	(void)sigaddset(&mask, SIGRTMIN);
	(void)sigprocmask(SIG_BLOCK, &mask, NULL);

	start_time = stress_time_now();
	do {
		shim_usleep(100000);
	} while (stress_continue(args));

reap:
	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);

	stress_kill_and_wait_many(args, s_pids, PROCS_MAX, SIGALRM, true);
	end_time = stress_time_now();

	if (start_time >= 0.0) {
		const double dt = end_time - start_time;

		if (dt > 0.0) {
			const double rate = (double)stress_bogo_get(args) / dt;

			pr_dbg("%s: hrtimer signals at %.3f MHz\n", args->name, rate / 1000000.0);
			stress_metrics_set(args, 0, "hrtimer signals per sec",
				rate, STRESS_METRIC_HARMONIC_MEAN);
		}
	}
	stress_lock_destroy(lock);
tidy_s_pids:
	(void)stress_s_pids_munmap(s_pids, PROCS_MAX);

	return rc;
}

stressor_info_t stress_hrtimers_info = {
	.stressor = stress_hrtimers,
	.class = CLASS_SCHEDULER,
	.opts = opts,
	.verify = VERIFY_ALWAYS,
	.help = help
};
#else
stressor_info_t stress_hrtimers_info = {
	.stressor = stress_unimplemented,
	.class = CLASS_SCHEDULER,
	.opts = opts,
	.verify = VERIFY_ALWAYS,
	.help = help,
	.unimplemented_reason = "built without librt or hrtimer support"
};
#endif
