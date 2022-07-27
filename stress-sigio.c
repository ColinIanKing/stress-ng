/*
 * Copyright (C) 2013-2021 Canonical, Ltd.
 * Copyright (C)      2022 Colin Ian King.
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

#if defined(HAVE_SYS_SELECT_H)
#include <sys/select.h>
#else
UNEXPECTED
#endif

static const stress_help_t help[] = {
	{ NULL,	"sigio N",	"start N workers that exercise SIGIO signals" },
	{ NULL,	"sigio-ops N",	"stop after N bogo sigio signals" },
	{ NULL,	NULL,		NULL }
};

#if defined(O_ASYNC) &&	\
    defined(O_NONBLOCK) && \
    defined(F_SETOWN) && \
    defined(F_GETFL) && \
    defined(F_SETFL)

#define BUFFER_SIZE	(4096)

static volatile int got_err;
static volatile uint64_t async_sigs;
static int rd_fd;
static const stress_args_t *sigio_args;
static pid_t pid;
static double time_end;

/*
 *  stress_sigio_handler()
 *      SIGIO handler
 */
static void MLOCKED_TEXT stress_sigio_handler(int signum)
{
	static char buffer[BUFFER_SIZE];

	(void)signum;

	async_sigs++;

	if (rd_fd > 0) {
		/*
		 *  Data is ready, so drain as much as possible
		 */
		while (keep_stressing_flag() &&  (stress_time_now() < time_end)) {
			ssize_t ret;

			got_err = 0;
			errno = 0;
			ret = read(rd_fd, buffer, sizeof(buffer));
			if (ret < 0) {
				if (errno != EAGAIN)
					got_err = errno;
				break;
			}

			if (sigio_args)
				inc_counter(sigio_args);
		}
	}
}

/*
 *  stress_sigio
 *	stress reading of /dev/zero using SIGIO
 */
static int stress_sigio(const stress_args_t *args)
{
	int ret, rc = EXIT_FAILURE, fds[2], status, flags = -1;

	rd_fd = -1;
	sigio_args = args;
	double t_start, t_delta;
	pid = -1;

	time_end = stress_time_now() + (double)g_opt_timeout;
	if (pipe(fds) < 0) {
		pr_err("%s: pipe failed, errno=%d (%s)\n",
			args->name, errno, strerror(errno));
		return rc;
	}
	rd_fd = fds[0];

#if defined(F_SETPIPE_SZ)
	(void)fcntl(fds[0], F_SETPIPE_SZ, BUFFER_SIZE * 2);
	(void)fcntl(fds[1], F_SETPIPE_SZ, BUFFER_SIZE * 2);
#endif

#if !defined(__minix__)
	ret = fcntl(fds[0], F_SETOWN, getpid());
	if (ret < 0) {
		if (errno != EINVAL) {
			pr_err("%s: fcntl F_SETOWN failed, errno=%d (%s)\n",
				args->name, errno, strerror(errno));
			goto err;
		}
	}
#endif
	flags = fcntl(fds[0], F_GETFL);
	if (flags < 0) {
		pr_err("%s: fcntl F_GETFL failed, errno=%d (%s)\n",
			args->name, errno, strerror(errno));
		goto err;
	}

	async_sigs = 0;

again:
	pid = fork();
	if (pid < 0) {
		if (stress_redo_fork(errno))
			goto again;
		if (!keep_stressing(args))
			goto finish;
		pr_err("%s: fork failed, errno=%d (%s)\n",
			args->name, errno, strerror(errno));
		goto err;
	} else if (pid == 0) {
		/* Child */
		char buffer[BUFFER_SIZE];

		stress_parent_died_alarm();
		(void)sched_settings_apply(true);

		/* Make sure this is killable by OOM killer */
		stress_set_oom_adjustment(args->name, true);

		(void)close(fds[0]);
		(void)memset(buffer, 0, sizeof buffer);

		while (keep_stressing(args)) {
			ssize_t n;

			n = write(fds[1], buffer, sizeof buffer);
			if (n < 0)
				break;
		}
		(void)close(fds[1]);
		_exit(1);
	}
	/* Parent */
	(void)close(fds[1]);
	fds[1] = -1;

	stress_set_proc_state(args->name, STRESS_STATE_RUN);

	if (stress_sighandler(args->name, SIGIO, stress_sigio_handler, NULL) < 0)
		goto reap;

	ret = fcntl(fds[0], F_SETFL, flags | O_ASYNC | O_NONBLOCK);
	if (ret < 0) {
		pr_err("%s: fcntl F_SETFL failed, errno=%d (%s)\n",
			args->name, errno, strerror(errno));
		goto reap;
	}
	t_start = stress_time_now();
	do {
		struct timeval timeout;

		timeout.tv_sec = 1;
		timeout.tv_usec = 0;

		(void)select(0, NULL, NULL, NULL, &timeout);
		if (got_err) {
			if (got_err != EINTR)
				pr_inf("%s: read error, errno=%d (%s)\n",
					args->name, got_err, strerror(got_err));
			break;
		}
	} while (keep_stressing(args));

	t_delta = stress_time_now() - t_start;

	if (t_delta > 0.0) 
		pr_inf("%s: %.2f SIGIO signals/sec\n",
			args->name, (double)async_sigs / t_delta);

finish:
	/*  And ignore IO signals from now on */
	VOID_RET(int, stress_sighandler(args->name, SIGIO, SIG_IGN, NULL));

	rc = EXIT_SUCCESS;
reap:
	if (pid > 0) {
		(void)kill(pid, SIGKILL);
		(void)shim_waitpid(pid, &status, 0);
	}

err:
	if (flags != -1) {
		VOID_RET(int, fcntl(fds[0], F_SETFL, flags & ~(O_ASYNC | O_NONBLOCK)));
	}

	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);

	if (fds[0] != -1)
		(void)close(fds[0]);
	if (fds[1] != -1)
		(void)close(fds[1]);

	return rc;
}

stressor_info_t stress_sigio_info = {
	.stressor = stress_sigio,
	.class = CLASS_INTERRUPT | CLASS_OS,
	.help = help
};
#else
stressor_info_t stress_sigio_info = {
	.stressor = stress_not_implemented,
	.class = CLASS_INTERRUPT | CLASS_OS,
	.help = help
};
#endif
