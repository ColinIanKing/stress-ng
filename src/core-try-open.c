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

static void stress_try_kill(
	const stress_args_t *args,
	const pid_t pid,
	const char *path)
{
	int i;

	for (i = 0; i < 10; i++) {
		int ret, status;

		ret = kill(pid, SIGKILL);
		(void)ret;
		ret = waitpid(pid, &status, WNOHANG);
		(void)ret;
		if ((kill(pid, 0) < 0) && (errno == ESRCH))
			return;
		shim_usleep(100000);
	}
	pr_dbg("%s: can't kill pid %" PRIdMAX " opening %s\n",
		args->name, (intmax_t)pid, path);
}

/*
 *  Try to open a file, return 0 if can open it, non-zero
 *  if it cannot be opened within timeout nanoseconds.
 */
int stress_try_open(
	const stress_args_t *args,
	const char *path,
	const int flags,
	const unsigned long timeout_ns)
{
	pid_t pid;
	int ret, status;
	struct stat statbuf;
	const int retries = 20;
	unsigned long sleep_ns = timeout_ns / retries;
	int i;

	(void)args;

	/* Don't try to open if file can't be stat'd */
	if (stat(path, &statbuf) < 0)
		return -1;

	pid = fork();
	if (pid < 0)
		return STRESS_TRY_OPEN_FORK_FAIL;
	if (pid == 0) {
		int fd;

		alarm(1);
		fd = open(path, flags);
		if (fd < 0) {
			/* blocked or out of memory, don't give up */
			if ((errno == EBUSY) ||
			    (errno == ENOMEM))
				_exit(STRESS_TRY_AGAIN);
			_exit(STRESS_TRY_OPEN_FAIL);
		}
		_exit(STRESS_TRY_OPEN_OK);
	}

	for (i = 0; i < retries; i++) {
		/*
		 *  Child may block on open forever if the driver
		 *  is broken, so use WNOHANG wait to poll rather
		 *  than wait forever on a locked up process
		 */
		ret = waitpid(pid, &status, WNOHANG);
		if (ret < 0) {
			/*
			 * EINTR or something else, treat as failed anyhow
			 * and forcibly kill child and re-wait. The child
			 * may be zombified by will get reaped by init
			 */
			stress_try_kill(args, pid, path);

			return STRESS_TRY_OPEN_WAIT_FAIL;
		}
		/* Has pid gone? */
		if ((kill(pid, 0) < 0) && (errno == ESRCH))
			goto done;

		/* Sleep and retry */
		shim_nanosleep_uint64(sleep_ns);
	}

	/* Give up, force kill */
	stress_try_kill(args, pid, path);
done:
	/* Seems like we can open the device successfully */
	if (WIFEXITED(status))
		return WEXITSTATUS(status);

	return STRESS_TRY_OPEN_EXIT_FAIL;
}

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
 *  Try to open a file, return 0 if can open it, non-zero
 *  if it cannot be opened within timeout nanoseconds.
 */
int stress_open_timeout(
	const char *name,
	const char *path,
	const int flags,
	const unsigned long timeout_ns)
{
	int ret, t_ret, tmp;
	struct sigevent sev;
	timer_t timerid;
	struct itimerspec timer;

	/*
	 *  If a handler can't be installed then
	 *  we can't test, so just return 0 and try
	 *  it anyhow.
	 */
	ret = stress_sighandler(name, SIGRTMIN, stress_timer_handler, NULL);
	if (ret < 0)
		return open(path, flags);

	/*
	 *  Enable a timer to interrupt log open waits
	 */
	(void)memset(&sev, 0, sizeof(sev));
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
	ret = open(path, flags);
	tmp = errno;
	if (!t_ret)
		(void)timer_delete(timerid);

	errno = tmp;
	return ret;
}
#else
int stress_open_timeout(
	const char *name,
	const char *path,
	const int flags,
	const unsigned long timeout_ns)
{
	(void)name;
	(void)timeout_ns;

	return open(path, flags);
}
#endif
