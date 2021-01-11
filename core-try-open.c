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

#if defined(HAVE_LIB_RT) &&             \
    defined(HAVE_TIMER_CREATE) &&       \
    defined(HAVE_TIMER_DELETE) &&       \
    defined(HAVE_TIMER_GETOVERRUN) &&   \
    defined(HAVE_TIMER_SETTIME)

static void MLOCKED_TEXT stress_timer_handler(int sig)
{
	(void)sig;
}

/*
 *  stress_try_open_wait()
 *	try to do open, use wait() as we only have one
 *	child to wait for and waitpid() is hence not necessary
 */
int stress_try_open_wait(const char *path, const int flags)
{
	pid_t pid;
	int status;

	pid = fork();
	if (pid < 0)
		return TRY_OPEN_FORK_FAIL;
	if (pid == 0) {
		int fd;

		fd = open(path, flags);
		if (fd < 0)
			_exit(TRY_OPEN_FAIL);

		/* Don't close, it gets reaped on _exit */
		_exit(TRY_OPEN_OK);
	}
	if (wait(&status) < 0) {
		int ret;

		ret = kill(pid, SIGKILL);
		(void)ret;
		ret = wait(&status);
		(void)ret;

		return TRY_OPEN_WAIT_FAIL;
	}

	if ((WIFEXITED(status)) && (WEXITSTATUS(status) != 0))
		return WEXITSTATUS(status);

	return TRY_OPEN_EXIT_FAIL;
}

/*
 *  Try to open a fil, return 0 if can open it, non-zero
 *  if it cannot be opened within timeout nanoseconds.
 */
int stress_try_open(
	const stress_args_t *args,
	const char *path,
	const int flags,
	const unsigned long timeout_ns)
{
	pid_t pid;
	int ret, t_ret, status;
	struct stat statbuf;
	struct sigevent sev;
	timer_t timerid;
	struct itimerspec timer;

	/* Don't try to open if file can't be stat'd */
	if (stat(path, &statbuf) < 0)
		return -1;

	/*
	 *  If a handler can't be installed then
	 *  we can't test, so just return 0 and try
	 *  it anyhow.
	 */
	ret = stress_sighandler(args->name, SIGRTMIN, stress_timer_handler, NULL);
	if (ret < 0)
		return 0;

	pid = fork();
	if (pid < 0)
		return TRY_OPEN_FORK_FAIL;
	if (pid == 0) {
		ret = stress_try_open_wait(path, flags);
		_exit(ret);
	}

	/*
	 *  Enable a timer to interrupt log open waits
	 */
	sev.sigev_notify = SIGEV_SIGNAL;
	sev.sigev_signo = SIGRTMIN;
	sev.sigev_value.sival_ptr = &timerid;

	t_ret = timer_create(CLOCK_REALTIME, &sev, &timerid);
	if (!t_ret) {
		timer.it_value.tv_sec = timeout_ns / STRESS_NANOSECOND;
		timer.it_value.tv_nsec = timeout_ns % STRESS_NANOSECOND;
		timer.it_interval.tv_sec = timer.it_value.tv_sec;
		timer.it_interval.tv_nsec = timer.it_value.tv_nsec;
		t_ret = timer_settime(timerid, 0, &timer, NULL);
	}
	ret = waitpid(pid, &status, 0);
	if (ret < 0) {
		/*
		 * EINTR or something else, treat as failed anyhow
		 * and forcibly kill child and re-wait. The grandchild
		 * may be zombified by will get reaped by init
		 */
		ret = kill(pid, SIGKILL);
		(void)ret;
		ret = waitpid(pid, &status, 0);
		(void)ret;

		return TRY_OPEN_WAIT_FAIL;
	}
	if (!t_ret)
		(void)timer_delete(timerid);

	/* Seems like we can open the device successfully */
	if ((WIFEXITED(status)) && (WEXITSTATUS(status) != 0))
		return WEXITSTATUS(status);

	return TRY_OPEN_EXIT_FAIL;
}
#else
int stress_try_open(
	const stress_args_t *args,
	const char *path,
	const int flags,
	const unsigned long timeout_ns)
{
	(void)args;
	(void)path;
	(void)flags;
	(void)timeout_ns;

	return 0;
}
#endif
