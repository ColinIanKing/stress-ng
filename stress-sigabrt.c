// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2013-2021 Canonical, Ltd.
 * Copyright (C) 2022-2023 Colin Ian King.
 *
 */
#include "stress-ng.h"

static const stress_help_t help[] = {
	{ NULL,	"sigabrt N",	 "start N workers generating segmentation faults" },
	{ NULL,	"sigabrt-ops N", "stop after N bogo segmentation faults" },
	{ NULL,	NULL,		 NULL }
};

typedef struct {
	volatile bool handler_enabled;	/* True if using a SIGABRT handler */
	volatile bool signalled;	/* True if handler handled SIGABRT */
	volatile double count;
	volatile double t_start;
	volatile double latency;
} stress_sigabrt_info_t;

static volatile stress_sigabrt_info_t *sigabrt_info;

static void MLOCKED_TEXT stress_sigabrt_handler(int num)
{
	(void)num;

	if (sigabrt_info) { /* Should always be not null */
		double latency = stress_time_now() - sigabrt_info->t_start;

		sigabrt_info->signalled = true;
		if (latency > 0.0) {
			sigabrt_info->latency += latency;
			sigabrt_info->count += 1.0;
		}
	}
}

/*
 *  stress_sigabrt
 *	stress by generating segmentation faults by
 *	writing to a read only page
 */
static int stress_sigabrt(const stress_args_t *args)
{
	void *sigabrt_mapping;
	double rate;

	if (stress_sighandler(args->name, SIGABRT, stress_sigabrt_handler, NULL) < 0)
		return EXIT_NO_RESOURCE;

	sigabrt_mapping = mmap(NULL, args->page_size,
				PROT_READ | PROT_WRITE,
				MAP_SHARED | MAP_ANONYMOUS,
				-1, 0);
	if (sigabrt_mapping == MAP_FAILED) {
		pr_inf_skip("%s: failed to mmap shared page, "
			"errno=%d (%s), skipping stressor\n",
			args->name, errno, strerror(errno));
		return EXIT_NO_RESOURCE;
	}
	sigabrt_info = (stress_sigabrt_info_t *)sigabrt_mapping;
	sigabrt_info->count = 0.0;

	stress_set_proc_state(args->name, STRESS_STATE_RUN);

	do {
		pid_t pid;

		(void)stress_mwc32();

		sigabrt_info->signalled = false;
		sigabrt_info->handler_enabled = stress_mwc1();

again:
		pid = fork();
		if (pid < 0) {
			if (stress_redo_fork(args, errno))
				goto again;
			if (!stress_continue(args))
				goto finish;
			pr_fail("%s: fork failed: %d (%s)\n",
				args->name, errno, strerror(errno));
			return EXIT_FAILURE;
		} else if (pid == 0) {
			/* Randomly select death by abort or SIGABRT */
			if (sigabrt_info->handler_enabled) {
				VOID_RET(int, stress_sighandler(args->name, SIGABRT, stress_sigabrt_handler, NULL));

				/*
				 * Aborting with a handler will call the handler, the handler will
				 * then be disabled and a second SIGABRT will occur causing the
				 * abort.
				 */
				sigabrt_info->t_start = stress_time_now();
				abort();
				/* Should never get here */
			} else {
				(void)stress_sighandler_default(SIGABRT);

				/* Raising SIGABRT without an handler will abort */
				sigabrt_info->t_start = stress_time_now();
				shim_raise(SIGABRT);
			}

			_exit(EXIT_FAILURE);
		} else {
			int ret, status;

rewait:
			ret = shim_waitpid(pid, &status, 0);
			if (ret < 0) {
				if (errno == EINTR) {
					goto rewait;
				}
				pr_fail("%s: waitpid failed: %d (%s)\n",
					args->name, errno, strerror(errno));
			} else {
				if (WIFSIGNALED(status) &&
				    (WTERMSIG(status) == SIGABRT)) {
					if (sigabrt_info->handler_enabled) {
						if (sigabrt_info->signalled == false) {
							pr_fail("%s SIGABORT signal handler did not get called\n",
								args->name);
						}
					} else {
						if (sigabrt_info->signalled == true) {
							pr_fail("%s SIGABORT signal handler was unexpectedly called\n",
								args->name);
						}
					}
					stress_bogo_inc(args);
				} else if (WIFEXITED(status)) {
					pr_fail("%s: child did not abort as expected\n",
						args->name);
				}
			}
		}
	} while (stress_continue(args));
finish:
	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);

	rate = (sigabrt_info->count > 0.0) ? sigabrt_info->latency / sigabrt_info->count : 0.0;
	stress_metrics_set(args, 0, "nanosec SIGABRT latency", rate * STRESS_DBL_NANOSECOND);

	(void)munmap((void *)sigabrt_mapping, args->page_size);

	return EXIT_SUCCESS;
}

stressor_info_t stress_sigabrt_info = {
	.stressor = stress_sigabrt,
	.class = CLASS_INTERRUPT | CLASS_OS,
	.verify = VERIFY_ALWAYS,
	.help = help
};
