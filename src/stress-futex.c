/*
 * Copyright (C) 2013-2021 Canonical, Ltd.
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
 * This code is a complete clean re-write of the stress tool by
 * Colin Ian King <colin.king@canonical.com> and attempts to be
 * backwardly compatible with the stress tool by Amos Waterland
 * <apw@rossby.metr.ou.edu> but has more stress tests and more
 * functionality.
 *
 */
#include "stress-ng.h"

static const stress_help_t help[] = {
	{ NULL,	"futex N",	"start N workers exercising a fast mutex" },
	{ NULL,	"futex-ops N",	"stop after N fast mutex bogo operations" },
	{ NULL,	NULL,		NULL }
};

#if defined(HAVE_LINUX_FUTEX_H) &&	\
    defined(__NR_futex)

#define THRESHOLD	(100000)

/*
 *  stress_futex()
 *	stress system by futex calls. The intention is not to
 * 	efficiently use futex, but to stress the futex system call
 *	by rapidly calling it on wait and wakes
 */
static int stress_futex(const stress_args_t *args)
{
	uint64_t *timeout = &g_shared->futex.timeout[args->instance];
	uint32_t *futex = &g_shared->futex.futex[args->instance];
	pid_t pid;

	stress_set_proc_state(args->name, STRESS_STATE_RUN);
again:
	pid = fork();
	if (pid < 0) {
		if (keep_stressing_flag() &&
		    ((errno == EAGAIN) || (errno == ENOMEM)))
			goto again;
		pr_err("%s: fork failed: errno=%d: (%s)\n",
			args->name, errno, strerror(errno));
	}
	if (pid > 0) {
		int status;

		(void)setpgid(pid, g_pgrp);

		do {
			int ret;

			/*
			 * Break early in case wake gets stuck
			 * (which it shouldn't)
			 */
			if (!keep_stressing_flag())
				break;
			ret = shim_futex_wake(futex, 1);
			if (g_opt_flags & OPT_FLAGS_VERIFY) {
				if (ret < 0) {
					pr_fail("%s: futex_wake failed, errno=%d (%s)\n",
						args->name, errno, strerror(errno));
				}
			}
		} while (keep_stressing(args));

		/* Kill waiter process */
		(void)kill(pid, SIGKILL);
		(void)shim_waitpid(pid, &status, 0);

		pr_dbg("%s: futex timeouts: %" PRIu64 "\n",
			args->name, *timeout);
	} else {
		uint64_t threshold = THRESHOLD;

		(void)setpgid(0, g_pgrp);
		stress_parent_died_alarm();
		(void)sched_settings_apply(true);

		do {
			/* Small timeout to force rapid timer wakeups */
			const struct timespec t = { .tv_sec = 0, .tv_nsec = 5000 };
			int ret;

			/* Break early before potential long wait */
			if (!keep_stressing_flag())
				break;

			ret = shim_futex_wait(futex, 0, &t);

			/* timeout, re-do, stress on stupid fast polling */
			if ((ret < 0) && (errno == ETIMEDOUT)) {
				(*timeout)++;
				if (*timeout > threshold) {
					/*
					 * Backoff for a short while
					 * and start again
					 */
					(void)shim_usleep(250000);
					threshold += THRESHOLD;
				}
			} else {
				if ((ret < 0) && (g_opt_flags & OPT_FLAGS_VERIFY)) {
					pr_fail("%s: futex_wait failed, errno=%d (%s)\n",
						args->name, errno, strerror(errno));
				}
				inc_counter(args);
			}
		} while (keep_stressing(args));
	}
	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);

	return EXIT_SUCCESS;
}

stressor_info_t stress_futex_info = {
	.stressor = stress_futex,
	.class = CLASS_SCHEDULER | CLASS_OS,
	.help = help
};
#else
stressor_info_t stress_futex_info = {
	.stressor = stress_not_implemented,
	.class = CLASS_SCHEDULER | CLASS_OS,
	.help = help
};
#endif
