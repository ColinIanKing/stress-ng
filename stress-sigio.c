/*
 * Copyright (C) 2013-2019 Canonical, Ltd.
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

static const help_t help[] = {
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
static int rd_fd;
static const args_t *sigio_args;
static pid_t pid;
static double time_end;

/*
 *  stress_sigio_handler()
 *      SIGIO handler
 */
static void MLOCKED_TEXT stress_sigio_handler(int signum)
{
	static char buffer[BUFFER_SIZE];
	const args_t *args = sigio_args;

        (void)signum;

	if (!keep_stressing() || (time_now() > time_end)) {
		if (pid > 0)
			(void)kill(pid, SIGKILL);

		(void)shim_sched_yield();
		return;
	}

	if (rd_fd > 0) {
		int ret;

		got_err = 0;
		ret = read(rd_fd, buffer, sizeof(buffer));
		if ((ret < 0) && (errno != EAGAIN))
			got_err = errno;
		else if (sigio_args)
			inc_counter(sigio_args);
		(void)shim_sched_yield();
	}
}

/*
 *  stress_sigio
 *	stress reading of /dev/zero using SIGIO
 */
static int stress_sigio(const args_t *args)
{
	int ret, rc = EXIT_FAILURE, fds[2], status;

	rd_fd = -1;
	sigio_args = args;

	time_end = time_now() + (double)g_opt_timeout;

	if (stress_sighandler(args->name, SIGIO, stress_sigio_handler, NULL) < 0)
		return rc;

	if (pipe(fds) < 0) {
		pr_fail_err("pipe");
		return rc;
	}
	rd_fd = fds[0];

#if !defined(__minix__)
	ret = fcntl(fds[0], F_SETOWN, getpid());
	if (ret < 0) {
		if (errno != EINVAL) {
			pr_fail_err("fcntl F_SETOWN");
			goto err;
		}
	}
#endif
	ret = fcntl(fds[0], F_GETFL);
	if (ret < 0) {
		pr_fail_err("fcntl F_GETFL");
		goto err;
	}
	ret = fcntl(fds[0], F_SETFL, ret | O_ASYNC | O_NONBLOCK);
	if (ret < 0) {
		pr_fail_err("fcntl F_SETFL");
		goto err;
	}

	pid = fork();
	if (pid < 0) {
		pr_fail_err("fork");
		goto err;
	} else if (pid == 0) {
		/* Child */

		char buffer[BUFFER_SIZE >> 4];

		(void)setpgid(0, g_pgrp);
                stress_parent_died_alarm();

                /* Make sure this is killable by OOM killer */
                set_oom_adjustment(args->name, true);

		(void)close(fds[0]);

		(void)memset(buffer, 0, sizeof buffer);

		while (keep_stressing()) {
			ssize_t n;

			n = write(fds[1], buffer, sizeof buffer);
			if (n < 0)
				break;
			(void)shim_sched_yield();
		}
		(void)close(fds[1]);
		_exit(1);
	}

	/* Parent */
	do {
		struct timeval timeout;

		timeout.tv_sec = 1;
		timeout.tv_usec = 0000;

		(void)select(0, NULL, NULL, NULL, &timeout);
		if (got_err) {
			pr_inf("%s: read error, errno=%d (%s)\n",
				args->name, got_err, strerror(got_err));
			break;
		}
	} while (keep_stressing());

	if (pid > 0) {
		(void)kill(pid, SIGKILL);
		(void)shim_waitpid(pid, &status, 0);
	}

	rc = EXIT_SUCCESS;

err:
	(void)close(fds[0]);
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
