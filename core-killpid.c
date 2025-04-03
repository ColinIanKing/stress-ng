/*
 * Copyright (C) 2021-2025 Colin Ian King
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
#include "core-killpid.h"

/*
 *  stress_kill_pid()
 *	kill a process with SIGKILL. Try and release memory
 *	as soon as possible using process_mrelease for the
 *	Linux case.
 */
int stress_kill_pid(const pid_t pid)
{
#if defined(__linux__) && 		\
    defined(__NR_process_mrelease)
	int pidfd, ret;

	pidfd = shim_pidfd_open(pid, 0);
	ret = shim_kill(pid, SIGKILL);

	if (pidfd >= 0) {
		 const int saved_errno = errno;

		if (ret == 0)
			(void)shim_process_mrelease(pidfd, 0);
		(void)close(pidfd);

		errno = saved_errno;
	}
	return ret;
#else
	return shim_kill(pid, SIGKILL);
#endif
}

/*
 *  stress_kill_pid_wait()
 *	kill and wait for a pid. If status is non-null copy waitpid
 *	status to it.
 */
pid_t stress_kill_pid_wait(const pid_t pid, int *status)
{
	int tmp_status = 0;

	(void)stress_kill_pid(pid);
	return shim_waitpid(pid, status ? status : &tmp_status, 0);
}

/*
 *  stress_kill_sig()
 *	use process memory releasing if using SIGKILL, otherwise
 *	use vanilla kill
 */
int stress_kill_sig(const pid_t pid, const int signum)
{
	if (signum == SIGKILL)
		return stress_kill_pid(pid);
	else
		return shim_kill(pid, signum);
}

/*
 *  stress_wait_until_reaped()
 *	wait until a process has been removed from process table
 */
static int stress_wait_until_reaped(
	stress_args_t *args,
	const pid_t pid,
	const int signum,
	const bool set_stress_force_killed_bogo)
{
	int count = 0;

	for (;;) {
		pid_t ret;
		int wstatus = 0;

		errno = 0;
		ret = waitpid(pid, &wstatus, 0);
		if ((ret >= 0) || (errno != EINTR)) {
			if (WIFEXITED(wstatus))
				return WEXITSTATUS(wstatus);
		}

		if ((shim_kill(pid, 0) < 0) && (errno == ESRCH))
			break;

		count++;
		/*
		 *  Retry if EINTR unless we've have 2 mins
		 *  consecutive EINTRs then give up.
		 */
		if (UNLIKELY(!stress_continue_flag())) {
			(void)stress_kill_sig(pid, signum);
			if (count > 120) {
				if (set_stress_force_killed_bogo)
					stress_force_killed_bogo(args);
				stress_kill_pid(pid);
			}
			/*
			 *  Process seems unkillable, report and bail out
			 */
			if (count > 600) {
				pr_dbg("%s: cannot kill PID %" PRIdMAX " after 10 minutes, giving up\n",
					args->name, (intmax_t)pid);
				stress_process_info(args, pid);
				break;
			}
		}
		(void)shim_sched_yield();
		if (count > 10)
			(void)sleep(1);
	}
	return EXIT_SUCCESS;
}

/*
 *  stress_kill_and_wait()
 */
int stress_kill_and_wait(
	stress_args_t *args,
	const pid_t pid,
	const int signum,
	const bool set_stress_force_killed_bogo)
{
	const pid_t mypid = getpid();

	if (UNLIKELY((pid == 0) || (pid == 1) || (pid == mypid))) {
		pr_inf("%s: warning, attempt to kill PID %" PRIdMAX " ignored\n",
			args->name, (intmax_t)pid);
	}
	/*
	 *  bad pids, won't kill, but return success to avoid
	 *  confusion of a kill that failed.
	 */
	if (UNLIKELY((pid <= 1) || (pid == mypid)))
		return EXIT_SUCCESS;

	stress_kill_sig(pid, signum);
	return stress_wait_until_reaped(args, pid, signum, set_stress_force_killed_bogo);
}

/*
 *  stress_kill_and_wait_many()
 *	kill and wait on an array of pids. Kill first, then reap.
 *	Avoid killing pids < init and oneself to catch any stupid
 *	breakage.
 *
 *	return EXIT_FAILURE if any of the child processes were
 * 	waited for and definitely exited with EXIT_FAILURE.
 */
int stress_kill_and_wait_many(
	stress_args_t *args,
	const stress_pid_t *s_pids,
	const size_t n_pids,
	const int signum,
	const bool set_stress_force_killed_bogo)
{
	size_t i;
	const pid_t mypid = getpid();
	int rc = EXIT_SUCCESS;

	/* Kill first */
	for (i = 0; i < n_pids; i++) {
		if ((s_pids[i].pid > 1) && (s_pids[i].pid != mypid))
			stress_kill_sig(s_pids[i].pid, signum);
	}
	/* Then reap */
	for (i = 0; i < n_pids; i++) {
		if ((s_pids[i].pid > 1) && (s_pids[i].pid != mypid)) {
			int ret;

			ret = stress_kill_and_wait(args, s_pids[i].pid, signum, set_stress_force_killed_bogo);
			if (ret == EXIT_FAILURE)
				rc = ret;
		}
	}
	return rc;
}
