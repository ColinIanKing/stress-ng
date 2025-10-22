/*
 * Copyright (C) 2013-2021 Canonical, Ltd.
 * Copyright (C) 2022-2025 Colin Ian King.
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

static const stress_help_t help[] = {
	{ NULL,	"sigabrt N",	 "start N workers generating SIGABRT signals" },
	{ NULL,	"sigabrt-ops N", "stop after N bogo SIGABRT operations" },
	{ NULL,	NULL,		 NULL }
};

typedef struct {
	volatile bool handler_enabled;	/* True if using a SIGABRT handler */
	volatile bool signalled;	/* True if handler handled SIGABRT */
	volatile double count;
	volatile double t_start;
	volatile double latency;
} stress_sigabrt_info_t;

static stress_sigabrt_info_t *sigabrt_info;

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
static int stress_sigabrt(stress_args_t *args)
{
	double rate;
	int rc = EXIT_SUCCESS;

	if (stress_sighandler(args->name, SIGABRT, stress_sigabrt_handler, NULL) < 0)
		return EXIT_NO_RESOURCE;

	sigabrt_info = (stress_sigabrt_info_t *)mmap(NULL, sizeof(*sigabrt_info),
				PROT_READ | PROT_WRITE,
				MAP_SHARED | MAP_ANONYMOUS,
				-1, 0);
	if (sigabrt_info == MAP_FAILED) {
		pr_inf_skip("%s: failed to mmap %zu byte sigabort information%s, "
			"errno=%d (%s), skipping stressor\n",
			args->name, sizeof(*sigabrt_info),
			stress_get_memfree_str(), errno, strerror(errno));
		return EXIT_NO_RESOURCE;
	}
	stress_set_vma_anon_name((void *)sigabrt_info, sizeof(*sigabrt_info), "state");
	sigabrt_info->count = 0.0;

	stress_set_proc_state(args->name, STRESS_STATE_SYNC_WAIT);
	stress_sync_start_wait(args);
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
			if (UNLIKELY(!stress_continue(args)))
				goto finish;
			pr_fail("%s: fork failed, errno=%d (%s)\n",
				args->name, errno, strerror(errno));
			return EXIT_FAILURE;
		} else if (pid == 0) {
			stress_set_proc_state(args->name, STRESS_STATE_RUN);

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
			pid_t ret;
			int  status;

rewait:
			ret = shim_waitpid(pid, &status, 0);
			if (ret < 0) {
				if (errno == EINTR) {
					goto rewait;
				}
				pr_fail("%s: waitpid() on PID %" PRIdMAX " failed, %d (%s)\n",
					args->name, (intmax_t)pid, errno, strerror(errno));
				rc = EXIT_FAILURE;
			} else {
				if (WIFSIGNALED(status) &&
				    (WTERMSIG(status) == SIGABRT)) {
					if (sigabrt_info->handler_enabled) {
						if (sigabrt_info->signalled == false) {
							pr_fail("%s SIGABRT signal handler did not get called\n",
								args->name);
							rc = EXIT_FAILURE;
						}
					} else {
						if (sigabrt_info->signalled == true) {
							pr_fail("%s SIGABRT signal handler was unexpectedly called\n",
								args->name);
							rc = EXIT_FAILURE;
						}
					}
					stress_bogo_inc(args);
				} else if (WIFEXITED(status)) {
					pr_fail("%s: child did not abort as expected\n",
						args->name);
					rc = EXIT_FAILURE;
				}
			}
		}
	} while ((rc == EXIT_SUCCESS) && stress_continue(args));
finish:
	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);

	rate = (sigabrt_info->count > 0.0) ? sigabrt_info->latency / sigabrt_info->count : 0.0;
	stress_metrics_set(args, 0, "nanosec SIGABRT latency",
		rate * STRESS_DBL_NANOSECOND, STRESS_METRIC_HARMONIC_MEAN);

	(void)munmap((void *)sigabrt_info, sizeof(*sigabrt_info));

	return rc;
}

const stressor_info_t stress_sigabrt_info = {
	.stressor = stress_sigabrt,
	.classifier = CLASS_SIGNAL | CLASS_OS,
	.verify = VERIFY_ALWAYS,
	.help = help
};
