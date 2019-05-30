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
	{ "P N", "poll N",	"start N workers exercising zero timeout polling" },
	{ NULL,	"poll-ops N",	"stop after N poll bogo operations" },
	{ NULL,	NULL,		NULL }
};

#if defined(HAVE_POLL_H)

#define MAX_PIPES	(5)
#define POLL_BUF	(4)

/*
 *  pipe_read()
 *	read a pipe with some verification and checking
 */
static int pipe_read(const args_t *args, const int fd, const int n)
{
	while (g_keep_stressing_flag) {
		ssize_t ret;
		char buf[POLL_BUF];

		ret = read(fd, buf, sizeof(buf));
		if (g_opt_flags & OPT_FLAGS_VERIFY) {
			if (ret < 0) {
				if ((errno == EAGAIN) || (errno == EINTR))
					continue;
				pr_fail("%s: pipe read error detected\n", args->name);
				return ret;
			}
			if (ret > 0) {
				ssize_t i;

				for (i = 0; i < ret; i++) {
					if (buf[i] != '0' + n) {
						pr_fail("%s: pipe read error, "
							"expecting different data on "
							"pipe\n", args->name);
						return ret;
					}
				}
			}
		}
		return ret;
	}
	return -1;
}

/*
 *  stress_poll()
 *	stress system by rapid polling system calls
 */
static int stress_poll(const args_t *args)
{
	int pipefds[MAX_PIPES][2];
	int i;
	pid_t pid;
	int rc = EXIT_SUCCESS;

	for (i = 0; i < MAX_PIPES; i++) {
		if (pipe(pipefds[i]) < 0) {
			pr_fail_dbg("pipe");
			while (--i >= 0) {
				(void)close(pipefds[i][0]);
				(void)close(pipefds[i][1]);
			}
			return EXIT_FAILURE;
		}
	}

again:
	pid = fork();
	if (pid < 0) {
		if (g_keep_stressing_flag && (errno == EAGAIN))
			goto again;
		pr_fail_dbg("fork");
		rc = EXIT_FAILURE;
		goto tidy;
	}
	else if (pid == 0) {
		/* Child writer */

		(void)setpgid(0, g_pgrp);
		stress_parent_died_alarm();

		for (i = 0; i < MAX_PIPES; i++)
			(void)close(pipefds[i][0]);

		do {
			char buf[POLL_BUF];
			ssize_t ret;

			/* Write on a randomly chosen pipe */
			i = (mwc32() >> 8) % MAX_PIPES;
			(void)memset(buf, '0' + i, sizeof(buf));
			ret = write(pipefds[i][1], buf, sizeof(buf));
			if (ret < (ssize_t)sizeof(buf)) {
				if ((errno == EAGAIN) || (errno == EINTR))
					continue;
				pr_fail_dbg("write");
				goto abort;
			}
		 } while (keep_stressing());
abort:
		for (i = 0; i < MAX_PIPES; i++)
			(void)close(pipefds[i][1]);
		exit(EXIT_SUCCESS);
	} else {
		/* Parent read */

		int maxfd = 0, status;
		struct pollfd fds[MAX_PIPES];
		fd_set rfds;
#if defined(HAVE_PPOLL) || defined(HAVE_PSELECT)
		struct timespec ts;
		sigset_t sigmask;
#endif

		(void)setpgid(pid, g_pgrp);

		FD_ZERO(&rfds);
		for (i = 0; i < MAX_PIPES; i++) {
			fds[i].fd = pipefds[i][0];
			fds[i].events = POLLIN;
			fds[i].revents = 0;

			FD_SET(pipefds[i][0], &rfds);
			if (pipefds[i][0] > maxfd)
				maxfd = pipefds[i][0];
		}

		do {
			struct timeval tv;
			int ret;

			if (!keep_stressing())
				break;

			/* stress out poll */
			ret = poll(fds, MAX_PIPES, 1);
			if ((g_opt_flags & OPT_FLAGS_VERIFY) &&
			    (ret < 0) && (errno != EINTR)) {
				pr_fail_err("poll");
			}
			if (ret > 0) {
				for (i = 0; i < MAX_PIPES; i++) {
					if (fds[i].revents == POLLIN) {
						if (pipe_read(args, fds[i].fd, i) < 0)
							break;
					}
				}
				inc_counter(args);
			}

			if (!keep_stressing())
				break;

#if defined(HAVE_PPOLL)
			/* stress out ppoll */

			ts.tv_sec = 0;
			ts.tv_nsec = 20000000;

			(void)sigemptyset(&sigmask);
			(void)sigaddset(&sigmask, SIGPIPE);

			ret = ppoll(fds, MAX_PIPES, &ts, &sigmask);
			if ((g_opt_flags & OPT_FLAGS_VERIFY) &&
			    (ret < 0) && (errno != EINTR)) {
				pr_fail_err("ppoll");
			}
			if (ret > 0) {
				for (i = 0; i < MAX_PIPES; i++) {
					if (fds[i].revents == POLLIN) {
						if (pipe_read(args, fds[i].fd, i) < 0)
							break;
					}
				}
				inc_counter(args);
			}
			if (!keep_stressing())
				break;
#endif

			/* stress out select */
			tv.tv_sec = 0;
			tv.tv_usec = 20000;
			ret = select(maxfd + 1, &rfds, NULL, NULL, &tv);
			if ((g_opt_flags & OPT_FLAGS_VERIFY) &&
			    (ret < 0) && (errno != EINTR)) {
				pr_fail_err("select");
			}
			if (ret > 0) {
				for (i = 0; i < MAX_PIPES; i++) {
					if (FD_ISSET(pipefds[i][0], &rfds)) {
						if (pipe_read(args, pipefds[i][0], i) < 0)
							break;
					}
					FD_SET(pipefds[i][0], &rfds);
				}
				inc_counter(args);
			}
			if (!keep_stressing())
				break;
#if defined(HAVE_PSELECT)
			/* stress out pselect */
			ts.tv_sec = 0;
			ts.tv_nsec = 20000000;

			(void)sigemptyset(&sigmask);
			(void)sigaddset(&sigmask, SIGPIPE);

			ret = pselect(maxfd + 1, &rfds, NULL, NULL, &ts, &sigmask);
			if ((g_opt_flags & OPT_FLAGS_VERIFY) &&
			    (ret < 0) && (errno != EINTR)) {
				pr_fail_err("pselect");
			}
			if (ret > 0) {
				for (i = 0; i < MAX_PIPES; i++) {
					if (FD_ISSET(pipefds[i][0], &rfds)) {
						if (pipe_read(args, pipefds[i][0], i) < 0)
							break;
					}
					FD_SET(pipefds[i][0], &rfds);
				}
				inc_counter(args);
			}
#endif
			/*
			 * stress zero sleep, this is like
			 * a select zero timeout
			 */
			(void)sleep(0);
		} while (keep_stressing());

		(void)kill(pid, SIGKILL);
		(void)shim_waitpid(pid, &status, 0);
	}

tidy:
	for (i = 0; i < MAX_PIPES; i++) {
		(void)close(pipefds[i][0]);
		(void)close(pipefds[i][1]);
	}

	return rc;
}

stressor_info_t stress_poll_info = {
	.stressor = stress_poll,
	.class = CLASS_SCHEDULER | CLASS_OS,
	.help = help
};
#else
stressor_info_t stress_poll_info = {
	.stressor = stress_not_implemented,
	.class = CLASS_SCHEDULER | CLASS_OS,
	.help = help
};
#endif
