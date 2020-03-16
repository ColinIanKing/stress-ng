/*
 * Copyright (C) 2013-2020 Canonical, Ltd.
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
	int fd, ret;
	int rc = -1;

	/*
	 *  If a handler can't be installed then
	 *  we can't test, so just return 0 and try
	 *  it anyhow.
	 */
	ret = stress_sighandler(args->name, SIGRTMIN, stress_timer_handler, NULL);
	if (ret < 0)
		return 0;

	pid = fork();
	if (pid < 0) {
		return -1;
	} else if (pid == 0) {
		errno = 0;
		fd = open(path, flags);
		if (fd < 0)
			_exit(EXIT_FAILURE);
		_exit(EXIT_SUCCESS);
	} else {
		int status, t_ret;

		struct sigevent sev;
		timer_t timerid;
		struct itimerspec timer;

		sev.sigev_notify = SIGEV_SIGNAL;
		sev.sigev_signo = SIGRTMIN;
		sev.sigev_value.sival_ptr = &timerid;

		t_ret = timer_create(CLOCK_REALTIME, &sev, &timerid);
		if (!t_ret) {
			timer.it_value.tv_sec = timeout_ns / 1000000000;
			timer.it_value.tv_nsec = timeout_ns % 1000000000;
			timer.it_interval.tv_sec = timer.it_value.tv_sec;
			timer.it_interval.tv_nsec = timer.it_value.tv_nsec;
			t_ret = timer_settime(timerid, 0, &timer, NULL);
		}
		ret = waitpid(pid, &status, 0);
		if (ret < 0) {
			/*
			 * EINTR or something else, treat as failed anyhow
			 * and forcibly kill child and re-wait
			 */
			kill(pid, SIGKILL);
			ret = waitpid(pid, &status, 0);
			(void)ret;
			return -1;
		}
		if (!t_ret)
			(void)timer_delete(timerid);

		/* Seems like we can open the device successfully */
		if ((WIFEXITED(status)) && (WEXITSTATUS(status) == 0))
			rc = 0;
	}
	return rc;
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
