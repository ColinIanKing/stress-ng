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
#include "core-affinity.h"
#include "core-builtin.h"

#include <time.h>

#if defined(HAVE_LINUX_FUTEX_H)
#include <linux/futex.h>
#endif

static const stress_help_t help[] = {
	{ NULL,	"futex N",	"start N workers exercising a fast mutex" },
	{ NULL,	"futex-ops N",	"stop after N fast mutex bogo operations" },
	{ NULL,	NULL,		NULL }
};

#if defined(HAVE_LINUX_FUTEX_H) &&	\
    defined(__NR_futex)

#define THRESHOLD	(100000)

/*
 *  stress_futex_wait()
 *     exercise futex_wait and every 16th time futex_waitv
 */
static int stress_futex_wait(uint32_t *futex, const int val, const long int nsec)
{
	struct timespec t;

#if defined(FUTEX_32) &&		\
    defined(CLOCK_MONOTONIC)
	static int try_futex_waitv = true;
	static int try = 0;

	if (try_futex_waitv && (try++ > 16)) {
		struct shim_futex_waitv w;

		(void)shim_memset(&w, 0, sizeof(w));

		w.val = (uint64_t)val;
		w.uaddr = (uintptr_t)futex;
		w.flags = FUTEX_32;

		try = 0;
		if (clock_gettime(CLOCK_MONOTONIC, &t) == 0) {
			int ret;

			t.tv_nsec += nsec;
			if (t.tv_nsec > 1000000000) {
				t.tv_sec++;
				t.tv_nsec -= 1000000000;
			}

			ret = shim_futex_waitv(&w, 1, 0, &t, CLOCK_MONOTONIC);
			if ((ret < 0) || (ret == ENOSYS)) {
				try_futex_waitv = false;
			} else {
				return ret;
			}
		}
	}
#else
	/* UNEXPECTED */
#endif
	t.tv_sec = 0;
	t.tv_nsec = (long int)nsec;

	return shim_futex_wait(futex, val, &t);
}

/*
 *  stress_futex()
 *	stress system by futex calls. The intention is not to
 * 	efficiently use futex, but to stress the futex system call
 *	by rapidly calling it on wait and wakes
 */
static int stress_futex(stress_args_t *args)
{
	uint64_t *timeout = &g_shared->futex.timeout[args->instance];
	uint32_t *futex = &g_shared->futex.futex[args->instance];
	pid_t pid;
	int parent_cpu, rc = EXIT_SUCCESS;

	stress_set_proc_state(args->name, STRESS_STATE_SYNC_WAIT);
	stress_sync_start_wait(args);
	stress_set_proc_state(args->name, STRESS_STATE_RUN);
again:
	parent_cpu = stress_get_cpu();
	pid = fork();
	if (pid < 0) {
		if (stress_redo_fork(args, errno))
			goto again;
		if (UNLIKELY(!stress_continue(args)))
			goto finish;
		pr_err("%s: fork failed, errno=%d: (%s)\n",
			args->name, errno, strerror(errno));
	}
	if (pid > 0) {
		int status;

		stress_set_proc_state(args->name, STRESS_STATE_RUN);

		do {
			int ret;

			/*
			 *  Break early in case wake gets stuck
			 *  (which it shouldn't)
			 */
			if (UNLIKELY(!stress_continue_flag()))
				break;
			ret = shim_futex_wake(futex, 1);
			if (g_opt_flags & OPT_FLAGS_VERIFY) {
				if (ret < 0) {
					pr_fail("%s: futex_wake failed, errno=%d (%s)\n",
						args->name, errno, strerror(errno));
					rc = EXIT_FAILURE;
				}
			}
		} while (stress_continue(args));

		/* send alarm to waiter process */
		(void)shim_kill(pid, SIGALRM);
		(void)shim_waitpid(pid, &status, 0);

		pr_dbg("%s: futex timeouts: %" PRIu64 "\n",
			args->name, *timeout);
	} else {
		uint64_t threshold = THRESHOLD;

		(void)stress_change_cpu(args, parent_cpu);
		stress_parent_died_alarm();
		(void)sched_settings_apply(true);

		do {
			/* Small timeout to force rapid timer wakeups */
			int ret;

			/* Break early before potential long wait */
			if (UNLIKELY(!stress_continue_flag()))
				break;

			ret = stress_futex_wait(futex, 0, 5000);

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
					if (errno != EINTR) {
						pr_fail("%s: futex_wait failed, errno=%d (%s)\n",
							args->name, errno, strerror(errno));
						rc = EXIT_FAILURE;
					}
				}
				stress_bogo_inc(args);
			}
		} while (stress_continue(args));
		_exit(rc);
	}
finish:
	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);

	return rc;
}

const stressor_info_t stress_futex_info = {
	.stressor = stress_futex,
	.classifier = CLASS_SCHEDULER | CLASS_OS | CLASS_IPC,
	.verify = VERIFY_OPTIONAL,
	.help = help
};
#else
const stressor_info_t stress_futex_info = {
	.stressor = stress_unimplemented,
	.classifier = CLASS_SCHEDULER | CLASS_OS | CLASS_IPC,
	.verify = VERIFY_OPTIONAL,
	.help = help,
	.unimplemented_reason = "built without linux/futex.h or futex() system call"
};
#endif
