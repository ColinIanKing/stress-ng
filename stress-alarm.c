// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2013-2021 Canonical, Ltd.
 * Copyright (C) 2022-2023 Colin Ian King.
 *
 */
#include "stress-ng.h"

/* Sleep tests */
#define STRESS_SLEEP_INTMAX	(1U << 0)
#define STRESS_SLEEP_ZERO	(1U << 1)
#define STRESS_SLEEP_RANDOM	(1U << 2)
#define STRESS_SLEEP_MASK	(STRESS_SLEEP_INTMAX | STRESS_SLEEP_ZERO | STRESS_SLEEP_RANDOM)

#define STRESS_ALARM_INTMAX	(1U << 3)
#define STRESS_ALARM_ZERO	(1U << 4)
#define STRESS_ALARM_RANDOM	(1U << 5)
#define STRESS_ALARM_MASK	(STRESS_ALARM_INTMAX | STRESS_ALARM_ZERO | STRESS_ALARM_RANDOM)


static const stress_help_t help[] = {
	{ NULL,	"alarm N",	"start N workers exercising alarm timers" },
	{ NULL,	"alarm-ops N",	"stop after N alarm bogo operations" },
	{ NULL, NULL,		NULL }
};

static void stress_alarm_sigusr1_handler(int sig)
{
	(void)sig;

	_exit(0);
}

static void stress_alarm_stress_bogo_inc(const stress_args_t *args)
{
	sigset_t set;

	sigemptyset(&set);
	sigaddset(&set, SIGUSR1);
	if (sigprocmask(SIG_BLOCK, &set, NULL) == 0) {
		stress_bogo_inc(args);
		(void)sigprocmask(SIG_UNBLOCK, &set, NULL);
	}
}

/*
 *  stress_alarm
 *	stress alarm()
 */
static int stress_alarm(const stress_args_t *args)
{
	pid_t pid;

	if (stress_sighandler(args->name, SIGALRM, stress_sighandler_nop, NULL) < 0)
		return EXIT_FAILURE;

	stress_set_proc_state(args->name, STRESS_STATE_RUN);
again:
	pid = fork();
	if (pid < 0) {
		if (stress_redo_fork(args, errno))
			goto again;
		if (!stress_continue(args))
			return EXIT_SUCCESS;
		pr_fail("%s: fork failed, errno=%d (%s)\n",
			args->name, errno, strerror(errno));
		return EXIT_FAILURE;
	} else if (pid == 0) {
		int err_mask = 0;

		if (stress_sighandler(args->name, SIGUSR1, stress_alarm_sigusr1_handler, NULL) < 0)
			_exit(EXIT_FAILURE);

		do {
			unsigned int secs_sleep;
			unsigned int secs_left;

			/* Long duration interrupted sleep */
			(void)alarm(0);	/* Cancel pending alarms */
			secs_left = alarm((unsigned int)INT_MAX);
			if (secs_left != 0)
				err_mask |= STRESS_ALARM_INTMAX;
			secs_left = alarm((unsigned int)INT_MAX);
			if (secs_left == 0)
				err_mask |= STRESS_ALARM_INTMAX;

			secs_left = sleep((unsigned int)INT_MAX);
			if (secs_left == 0)
				err_mask |= STRESS_SLEEP_INTMAX;
			stress_alarm_stress_bogo_inc(args);
			if (!stress_continue(args))
				break;

			/* zeros second interrupted sleep */
			(void)alarm(0);	/* Cancel pending alarms */
			secs_left = alarm(0);
			if (secs_left != 0)
				err_mask |= STRESS_ALARM_ZERO;

			secs_left = sleep(0);
			if (secs_left != 0)
				err_mask |= STRESS_SLEEP_ZERO;
			stress_alarm_stress_bogo_inc(args);
			if (!stress_continue(args))
				break;

			/* random duration interrupted sleep */
			secs_sleep = stress_mwc32() + 100;
			(void)alarm(0); /* Cancel pending alarms */
			secs_left = alarm(secs_left);
			if (secs_left != 0)
				err_mask |= STRESS_ALARM_RANDOM;
			secs_left = sleep(secs_left);
			if (secs_left > secs_sleep)
				err_mask |= STRESS_SLEEP_RANDOM;
			stress_alarm_stress_bogo_inc(args);
		} while (stress_continue(args));
		_exit(err_mask);
	} else {
		int status;
		const bool verify = !!(g_opt_flags & OPT_FLAGS_VERIFY);

		while (stress_continue(args) &&
		       (stress_time_now() < args->time_end)) {
			const uint64_t delay_ns = 1000 + stress_mwc32modn(10000);

			(void)shim_kill(pid, SIGALRM);
			shim_nanosleep_uint64(delay_ns);
			shim_sched_yield();
			(void)shim_kill(pid, SIGALRM);
			shim_sched_yield();
		}

		(void)shim_kill(pid, SIGUSR1);
		(void)waitpid(pid, &status, 0);

		if (verify && WIFEXITED(status)) {
			const unsigned int err_mask = (unsigned int)WEXITSTATUS(status);

			if (err_mask & STRESS_SLEEP_MASK) {
				pr_fail("%s: failed on tests: %s%s%s%s%s\n",
					args->name,
					(err_mask & STRESS_SLEEP_INTMAX) ? "sleep(INT_MAX)" : "",
					(err_mask & STRESS_SLEEP_INTMAX) &&
						(err_mask & (STRESS_SLEEP_ZERO | STRESS_SLEEP_RANDOM)) ? ", " : "",
					(err_mask & STRESS_SLEEP_ZERO) ? "sleep(0)": "",
					(err_mask & STRESS_SLEEP_ZERO) &&
						(err_mask & STRESS_SLEEP_RANDOM) ? ", " : "",
					(err_mask & STRESS_SLEEP_RANDOM) ? "sleep($RANDOM)": "");
			}

			if (err_mask & STRESS_ALARM_MASK) {
				pr_fail("%s: failed on tests: %s%s%s%s%s\n",
					args->name,
					(err_mask & STRESS_ALARM_INTMAX) ? "alarm(INT_MAX)" : "",
					(err_mask & STRESS_ALARM_INTMAX) &&
						(err_mask & (STRESS_ALARM_ZERO | STRESS_ALARM_RANDOM)) ? ", " : "",
					(err_mask & STRESS_ALARM_ZERO) ? "alarm(0)": "",
					(err_mask & STRESS_ALARM_ZERO) &&
						(err_mask & STRESS_ALARM_RANDOM) ? ", " : "",
					(err_mask & STRESS_ALARM_RANDOM) ? "alarm($RANDOM)": "");
			}
		}
	}
	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);

	return EXIT_SUCCESS;
}

stressor_info_t stress_alarm_info = {
	.stressor = stress_alarm,
	.class = CLASS_INTERRUPT | CLASS_OS,
	.verify = VERIFY_OPTIONAL,
	.help = help
};
