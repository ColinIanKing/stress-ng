/*
 * Copyright (C) 2013-2021 Canonical, Ltd.
 * Copyright (C) 2021-2022 Colin Ian King.
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
 *  stress_kill_handle_sigusr1()
 *	handle SIGUSR1
 */
static void stress_kill_handle_sigusr1(int sig)
{
	(void)sig;
}

/*
 *  stress on sched_kill()
 *	stress system by rapid kills
 */
static int stress_kill(const stress_args_t *args)
{
	uint64_t udelay = 5000;
	pid_t pid;
	const pid_t ppid = getpid();
	int ret;

	if (stress_sighandler(args->name, SIGUSR1, SIG_IGN, NULL) < 0)
		return EXIT_FAILURE;

	pid = fork();
	if (pid == 0) {
		ret = stress_sighandler(args->name, SIGUSR1, stress_kill_handle_sigusr1, NULL);
		(void)ret;

		while (keep_stressing(args)) {
			if (kill(ppid, 0) < 0)
				break;
			pause();
		}
		_exit(0);
	}

	stress_set_proc_state(args->name, STRESS_STATE_RUN);

	do {
		pid_t bad_pid;

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

		ret = kill(args->pid, SIGUSR1);
		if ((ret < 0) && (g_opt_flags & OPT_FLAGS_VERIFY))
			pr_fail("%s: kill PID %d with SIGUSR1 failed, errno=%d (%s)\n",
				args->name, (int)args->pid, errno, strerror(errno));

		/* Zero signal can be used to see if process exists */
		ret = kill(args->pid, 0);
		if ((ret < 0) && (g_opt_flags & OPT_FLAGS_VERIFY))
			pr_fail("%s: kill PID %d with signal 0 failed, errno=%d (%s)\n",
				args->name, (int)args->pid, errno, strerror(errno));

		/*
		 * Zero signal can be used to see if process exists,
		 * -1 pid means signal sent to every process caller has
		 * permission to send to
		 */
		ret = kill(-1, 0);
		if ((ret < 0) && (g_opt_flags & OPT_FLAGS_VERIFY))
			pr_fail("%s: kill PID -1 with signal 0 failed, errno=%d (%s)\n",
				args->name, errno, strerror(errno));

		/*
		 * Exercise the kernel by sending illegal signal numbers,
		 * should return -EINVAL
		 */
		ret = kill(args->pid, -1);
		(void)ret;
		ret = kill(args->pid, INT_MIN);
		(void)ret;
		ret = kill(0, INT_MIN);
		(void)ret;

		/*
		 * Exercise the kernel by sending illegal pid INT_MIN,
		 * should return -ESRCH but not sure if that is portable
		 */
		ret = kill(INT_MIN, 0);
		(void)ret;

		/*
		 * Send child process some signals to keep it busy
		 */
		if (pid > 1) {
			ret = kill(pid, 0);
			(void)ret;
#if defined(SIGSTOP) && 	\
    defined(SIGCONT)
			ret = kill(pid, SIGSTOP);
			(void)ret;
			ret = kill(pid, SIGCONT);
			(void)ret;
#endif
			ret = kill(pid, SIGUSR1);
			(void)ret;
		}

		bad_pid = stress_get_unused_pid_racy(false);
		ret = kill(bad_pid, 0);
		(void)ret;

		inc_counter(args);
	} while (keep_stressing(args));

	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);

	if (pid != -1) {
		int status;

		ret = kill(pid, SIGKILL);
		(void)ret;
		ret = waitpid(pid, &status, 0);
		(void)ret;
	}

	return EXIT_SUCCESS;
}

stressor_info_t stress_kill_info = {
	.stressor = stress_kill,
	.class = CLASS_INTERRUPT | CLASS_SCHEDULER | CLASS_OS,
	.verify = VERIFY_OPTIONAL,
	.help = help
};
