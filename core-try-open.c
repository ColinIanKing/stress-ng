// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2013-2021 Canonical, Ltd.
 * Copyright (C) 2022-2023 Colin Ian King.
 *
 */
#include "stress-ng.h"
#include "core-builtin.h"

/*
 *  stress_try_kill()
 *	hammer away and try to kill a process
 */
static void stress_try_kill(
	const stress_args_t *args,
	const pid_t pid,
	const char *path)
{
	int i;

	for (i = 1; stress_continue(args) && (i <= 20); i++) {
		int status;

		VOID_RET(int, shim_kill(pid, SIGKILL));
		VOID_RET(int, waitpid(pid, &status, WNOHANG));
		if ((shim_kill(pid, 0) < 0) && (errno == ESRCH))
			return;
		(void)shim_usleep(10000 * i);
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
	int ret, status = 0;
	struct stat statbuf;
	const int retries = 20;
	const unsigned long sleep_ns = timeout_ns / retries;
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

		(void)alarm(1);
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
		if ((shim_kill(pid, 0) < 0) && (errno == ESRCH))
			goto done;

		/* Sleep and retry */
		(void)shim_nanosleep_uint64(sleep_ns);
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
	ret = stress_sighandler(name, SIGRTMIN, stress_sighandler_nop, NULL);
	if (ret < 0)
		return open(path, flags);

	/*
	 *  Enable a timer to interrupt log open waits
	 */
	(void)shim_memset(&sev, 0, sizeof(sev));
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
