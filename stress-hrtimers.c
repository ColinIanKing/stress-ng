/*
 * Copyright (C) 2013-2021 Canonical, Ltd.
 * Copyright (C) 2021-2025 Colin Ian King.
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
#include <time.h>

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
stress_args_t *s_args;
static timer_t timerid;
static double time_end;
static long int ns_delay;
static int overrun;
void *lock;

#define PROCS_MAX	(8)

/*
 *  stress_hrtimers_set()
 *	set timer, ensure it is never zero
 */
static void stress_hrtimers_set(struct itimerspec *timer)
{
	if (ns_delay > 999999999)
		ns_delay = 999999999;
	timer->it_value.tv_sec = 0;
	timer->it_value.tv_nsec = (ns_delay < 0) ? stress_mwc16() + 1 : ns_delay;
	timer->it_interval.tv_sec = timer->it_value.tv_sec;
	timer->it_interval.tv_nsec = timer->it_value.tv_nsec;
}

/*
 *  stress_hrtimers_handler()
 *	catch timer signal and cancel if no more runs flagged
 */
static void MLOCKED_TEXT OPTIMIZE3 stress_hrtimers_handler(int sig)
{
	struct itimerspec timer;
	sigset_t mask;
	register uint64_t bogo_counter;

	(void)sig;

	VOID_RET(bool, stress_bogo_inc_lock(s_args, lock, 1));
	if (UNLIKELY(!stress_continue(s_args)))
		goto cancel;
	bogo_counter = stress_bogo_get(s_args);

	/* check periodically for timeout */
	if ((bogo_counter & 65535) == 0) {
		if ((sigpending(&mask) == 0) && (sigismember(&mask, SIGINT)))
			goto cancel;
		if (stress_time_now() > time_end)
			goto cancel;
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
		if ((errno == EAGAIN) || (errno == ENOMEM) || (errno == ENOTSUP)) {
			pr_inf_skip("%s: timer_create, errno=%d (%s), skipping stressor\n",
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

	for (;;) {
		(void)shim_usleep(10000);
		if (!stress_continue(args))
			break;
		if (ns_delay >= 0) {
			const long int ns_adjust = ns_delay >> 2;

			ns_delay += timer_getoverrun(timerid) ? ns_adjust : -ns_adjust;
			(void)shim_memset(&timer, 0, sizeof(timer));
			(void)timer_settime(timerid, 0, &timer, NULL);
			stress_hrtimers_set(&timer);
			(void)timer_settime(timerid, 0, &timer, NULL);
		}
	}

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

	s_args = args;

	if (stress_sigchld_set_handler(args) < 0)
		return EXIT_NO_RESOURCE;

	s_pids = stress_sync_s_pids_mmap(PROCS_MAX);
	if (s_pids == MAP_FAILED) {
		pr_inf_skip("%s: failed to mmap %d PIDs%s, skipping stressor\n",
			args->name, PROCS_MAX, stress_get_memfree_str());
		return EXIT_NO_RESOURCE;
	}

	lock = stress_lock_create("counter");
	if (!lock) {
		pr_inf("%s: cannot create lock, skipping stressor\n", args->name);
		rc = EXIT_NO_RESOURCE;
		goto tidy_s_pids;
	}

        (void)stress_get_setting("hrtimers-adjust", &hrtimers_adjust);
	overrun = 0;
	ns_delay = hrtimers_adjust ? 1000 : -1;

	for (i = 0; i < PROCS_MAX; i++) {
		stress_sync_start_init(&s_pids[i]);

		if (UNLIKELY(!stress_continue(args)))
			goto reap;

		s_pids[i].pid = fork();
		if (s_pids[i].pid == 0) {
			/* Child */
			stress_set_proc_state(args->name, STRESS_STATE_SYNC_WAIT);
			s_pids[i].pid = getpid();
			stress_sync_start_wait_s_pid(&s_pids[i]);
			stress_set_proc_state(args->name, STRESS_STATE_RUN);

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
		(void)shim_usleep(100000);
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
	(void)stress_sync_s_pids_munmap(s_pids, PROCS_MAX);

	return rc;
}

const stressor_info_t stress_hrtimers_info = {
	.stressor = stress_hrtimers,
	.classifier = CLASS_SCHEDULER,
	.opts = opts,
	.verify = VERIFY_ALWAYS,
	.help = help
};
#else
const stressor_info_t stress_hrtimers_info = {
	.stressor = stress_unimplemented,
	.classifier = CLASS_SCHEDULER,
	.opts = opts,
	.verify = VERIFY_ALWAYS,
	.help = help,
	.unimplemented_reason = "built without librt or hrtimer support"
};
#endif
