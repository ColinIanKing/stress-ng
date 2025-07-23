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

static const stress_help_t help[] = {
	{ NULL,	"kill N",	"start N workers killing with SIGUSR1" },
	{ NULL,	"kill-ops N",	"stop after N kill bogo operations" },
	{ NULL,	NULL,		NULL }
};

/*
 *  stress on sched_kill()
 *	stress system by rapid kills
 */
static int stress_kill(stress_args_t *args)
{
	uint64_t udelay = 5000;
	pid_t pid;
	const pid_t ppid = getpid();
	int ret;
	double duration = 0.0, count = 0.0, rate;

	if (stress_sighandler(args->name, SIGUSR1, SIG_IGN, NULL) < 0)
		return EXIT_FAILURE;

	pid = fork();
	if (pid == 0) {
		stress_set_proc_state(args->name, STRESS_STATE_RUN);

		VOID_RET(int, stress_sighandler(args->name, SIGUSR1, stress_sighandler_nop, NULL));

		while (stress_continue(args)) {
			if (kill(ppid, 0) < 0)
				break;
			(void)shim_pause();
		}
		stress_set_proc_state(args->name, STRESS_STATE_WAIT);
		_exit(0);
	}

	stress_set_proc_state(args->name, STRESS_STATE_SYNC_WAIT);
	stress_sync_start_wait(args);
	stress_set_proc_state(args->name, STRESS_STATE_RUN);

	do {
		pid_t bad_pid;
		double t;

		/*
		 *  With many kill stressors we get into a state
		 *  where they all hammer on kill system calls and
		 *  this stops the parent from getting scheduling
		 *  time to spawn off the rest of the kill stressors
		 *  causing some lag in getting all the stressors
		 *  running. Ease this pressure off to being with
		 *  with some small sleeps that shrink to zero over
		 *  time. The alternative was to re-nice all the
		 *  processes, but even then one gets the child
		 *  stressors all contending and causing a bottle
		 *  neck.  Any simpler and/or better solutions would
		 *  be appreciated!
		 */
		if (udelay >= 1000) {
			(void)shim_usleep(udelay);
			udelay -= 500;
		}

		t = stress_time_now();
		ret = kill(args->pid, SIGUSR1);
		if (LIKELY(ret == 0)) {
			const int saved_errno = errno;

			duration += stress_time_now() - t;
			count += 1.0;
			errno = saved_errno;
		} else if (UNLIKELY((ret < 0) && (g_opt_flags & OPT_FLAGS_VERIFY))) {
			pr_fail("%s: kill PID %" PRIdMAX " with SIGUSR1 failed, errno=%d (%s)\n",
				args->name, (intmax_t)args->pid, errno, strerror(errno));
		}

		/* Zero signal can be used to see if process exists */
		t = stress_time_now();
		ret = kill(args->pid, 0);
		if (LIKELY(ret == 0)) {
			const int saved_errno = errno;

			duration += stress_time_now() - t;
			count += 1.0;
			errno = saved_errno;
		} else if (UNLIKELY((ret < 0) && (g_opt_flags & OPT_FLAGS_VERIFY))) {
			pr_fail("%s: kill PID %" PRIdMAX " with signal 0 failed, errno=%d (%s)\n",
				args->name, (intmax_t)args->pid, errno, strerror(errno));
		}

		/*
		 * Zero signal can be used to see if process exists,
		 * -1 pid means signal sent to every process caller has
		 * permission to send to
		 */
		t = stress_time_now();
		ret = kill(-1, 0);
		if (LIKELY(ret == 0)) {
			const int saved_errno = errno;

			duration += stress_time_now() - t;
			count += 1.0;
			errno = saved_errno;
		} else if (UNLIKELY((ret < 0) && (g_opt_flags & OPT_FLAGS_VERIFY))) {
			pr_fail("%s: kill PID -1 with signal 0 failed, errno=%d (%s)\n",
				args->name, errno, strerror(errno));
		}

		/*
		 * Exercise the kernel by sending illegal signal numbers,
		 * should return -EINVAL
		 */
		VOID_RET(int, kill(args->pid, -1));
		VOID_RET(int, kill(args->pid, INT_MIN));
		VOID_RET(int, kill(0, INT_MIN));

		/*
		 * Exercise the kernel by sending illegal pid INT_MIN,
		 * should return -ESRCH but not sure if that is portable
		 */
		VOID_RET(int, kill(INT_MIN, 0));

		/*
		 * Send child process some signals to keep it busy
		 */
		if (LIKELY(pid > 1)) {
			VOID_RET(int, kill(pid, 0));
#if defined(SIGSTOP) && 	\
    defined(SIGCONT)
			VOID_RET(int, kill(pid, SIGSTOP));
			VOID_RET(int, kill(pid, SIGCONT));
#endif
			VOID_RET(int, kill(pid, SIGUSR1));
		}

		bad_pid = stress_get_unused_pid_racy(false);
		VOID_RET(int, kill(bad_pid, 0));

		stress_bogo_inc(args);
	} while (stress_continue(args));

	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);

	if (pid != -1) {
		int status;

		VOID_RET(int, kill(pid, SIGKILL));
		VOID_RET(pid_t, waitpid(pid, &status, 0));
	}

	rate = (duration > 0.0) ? count / duration : 0.0;
	stress_metrics_set(args, 0, "kill calls per sec",
		rate, STRESS_METRIC_HARMONIC_MEAN);

	return EXIT_SUCCESS;
}

const stressor_info_t stress_kill_info = {
	.stressor = stress_kill,
	.classifier = CLASS_INTERRUPT | CLASS_SCHEDULER | CLASS_OS,
	.verify = VERIFY_OPTIONAL,
	.help = help
};
