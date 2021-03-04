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

static const stress_help_t help[] = {
	{ "P N", "poll N",	"start N workers exercising zero timeout polling" },
	{ NULL,	"poll-ops N",	"stop after N poll bogo operations" },
	{ NULL, "poll-fds N",	"use N file descriptors" },
	{ NULL,	NULL,		NULL }
};

static int stress_set_poll_fds(const char *opt)
{
	size_t max_fds;

        max_fds = stress_get_uint32(opt);
        stress_check_range("poll-fds", max_fds,
                1, 8192);
        return stress_set_setting("poll-fds", TYPE_ID_SIZE_T, &max_fds);
}

static const stress_opt_set_func_t opt_set_funcs[] = {
	{ OPT_poll_fds,		stress_set_poll_fds },
	{ 0,			NULL },
};

#if defined(HAVE_POLL_H)

#define MAX_PIPES	(5)
#define POLL_BUF	(4)

typedef struct {
	int fd[2];
} pipe_fds_t;

/*
 *  pipe_read()
 *	read a pipe with some verification and checking
 */
static int pipe_read(const stress_args_t *args, const int fd, const int n)
{
	while (keep_stressing_flag()) {
		ssize_t ret;
		char buf[POLL_BUF];

		ret = read(fd, buf, sizeof(buf));
		if (g_opt_flags & OPT_FLAGS_VERIFY) {
			if (ret < 0) {
				if ((errno == EAGAIN) || (errno == EINTR))
					continue;
				pr_fail("%s: pipe read error detected, errno=%d (%s)\n",
					args->name, errno, strerror(errno));
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
static int stress_poll(const stress_args_t *args)
{
	pid_t pid;
	int rc = EXIT_SUCCESS;
	size_t i, max_fds = MAX_PIPES;
	pipe_fds_t *pipe_fds;
	struct pollfd *poll_fds;

	(void)stress_get_setting("poll-fds", &max_fds);

	pipe_fds = calloc((size_t)max_fds, sizeof(*pipe_fds));
	if (!pipe_fds) {
		pr_inf("%s: out of memory allocating %zd pipe file descriptors\n",
			args->name, max_fds);
		return EXIT_NO_RESOURCE;
	}
	poll_fds = calloc((size_t)max_fds, sizeof(*poll_fds));
	if (!poll_fds) {
		pr_inf("%s: out of memory allocating %zd poll file descriptors\n",
			args->name, max_fds);
		free(pipe_fds);
		return EXIT_NO_RESOURCE;
	}

	for (i = 0; i < max_fds; i++) {
		if (pipe(pipe_fds[i].fd) < 0) {
			size_t j;

			pr_fail("%s: pipe failed, errno=%d (%s)\n",
				args->name, errno, strerror(errno));

			for (j = 0; j < i; j++) {
				(void)close(pipe_fds[i].fd[0]);
				(void)close(pipe_fds[i].fd[1]);
			}
			free(poll_fds);
			free(pipe_fds);
			return EXIT_NO_RESOURCE;
		}
	}

	stress_set_proc_state(args->name, STRESS_STATE_RUN);
again:
	pid = fork();
	if (pid < 0) {
		if (keep_stressing_flag() && (errno == EAGAIN))
			goto again;
		pr_fail("%s: fork failed, errno=%d (%s)\n",
			args->name, errno, strerror(errno));
		rc = EXIT_FAILURE;
		goto tidy;
	} else if (pid == 0) {
		/* Child writer */

		(void)setpgid(0, g_pgrp);
		stress_parent_died_alarm();
		(void)sched_settings_apply(true);

		for (i = 0; i < max_fds; i++)
			(void)close(pipe_fds[i].fd[0]);

		do {
			char buf[POLL_BUF];
			ssize_t ret;

			/* Write on a randomly chosen pipe */
			i = (stress_mwc32() >> 8) % max_fds;
			(void)memset(buf, '0' + i, sizeof(buf));
			ret = write(pipe_fds[i].fd[1], buf, sizeof(buf));
			if (ret < (ssize_t)sizeof(buf)) {
				if ((errno == EAGAIN) || (errno == EINTR))
					continue;
				pr_fail("%s: write failed, errno=%d (%s)\n",
					args->name, errno, strerror(errno));
				goto abort;
			}
		 } while (keep_stressing(args));
abort:
		for (i = 0; i < max_fds; i++)
			(void)close(pipe_fds[i].fd[1]);
		exit(EXIT_SUCCESS);
	} else {
		/* Parent read */

		int maxfd = 0, status;
		fd_set rfds;
#if defined(HAVE_PPOLL) ||	\
    defined(HAVE_PSELECT)
		struct timespec ts;
		sigset_t sigmask;
#endif

		(void)setpgid(pid, g_pgrp);

		for (i = 0; i < max_fds; i++) {
			poll_fds[i].fd = pipe_fds[i].fd[0];
			poll_fds[i].events = POLLIN;
			poll_fds[i].revents = 0;
		}

		do {
			struct timeval tv;
			int ret;

			if (!keep_stressing(args))
				break;

			/* stress out poll */
			ret = poll(poll_fds, max_fds, 1);
			if ((g_opt_flags & OPT_FLAGS_VERIFY) &&
			    (ret < 0) && (errno != EINTR)) {
				pr_fail("%s: poll failed, errno=%d (%s)\n",
					args->name, errno, strerror(errno));
			}
			if (ret > 0) {
				for (i = 0; i < max_fds; i++) {
					if (poll_fds[i].revents == POLLIN) {
						if (pipe_read(args, poll_fds[i].fd, i) < 0)
							break;
					}
				}
				inc_counter(args);
			}

			if (!keep_stressing(args))
				break;

#if defined(HAVE_PPOLL)
			/* stress out ppoll */

			ts.tv_sec = 0;
			ts.tv_nsec = 20000000;

			(void)sigemptyset(&sigmask);
			(void)sigaddset(&sigmask, SIGPIPE);

			ret = ppoll(poll_fds, max_fds, &ts, &sigmask);
			if ((g_opt_flags & OPT_FLAGS_VERIFY) &&
			    (ret < 0) && (errno != EINTR)) {
				pr_fail("%s: ppoll failed, errno=%d (%s)\n",
					args->name, errno, strerror(errno));
			}
			if (ret > 0) {
				for (i = 0; i < max_fds; i++) {
					if (poll_fds[i].revents == POLLIN) {
						if (pipe_read(args, poll_fds[i].fd, i) < 0)
							break;
					}
				}
				inc_counter(args);
			}
			if (!keep_stressing(args))
				break;

			/* Exercise illegal poll timeout */
			ts.tv_sec = 0;
			ts.tv_nsec = 1999999999;
			ret = ppoll(poll_fds, max_fds, &ts, &sigmask);
			(void)ret;
			if (!keep_stressing(args))
				break;

#if defined(RLIMIT_NOFILE)
			/*
			 *  Exercise ppoll with more fds than rlimit soft
			 *  limit. This should fail with EINVAL on Linux.
			 */
			{
				struct rlimit old_rlim, new_rlim;

				if (getrlimit(RLIMIT_NOFILE, &old_rlim) == 0) {
					new_rlim.rlim_cur = max_fds - 1;
					new_rlim.rlim_max = old_rlim.rlim_max;

					if (setrlimit(RLIMIT_NOFILE, &new_rlim) == 0) {
						ts.tv_sec = 0;
						ts.tv_nsec = 0;
						ret = ppoll(poll_fds, max_fds, &ts, &sigmask);
						(void)ret;

						(void)setrlimit(RLIMIT_NOFILE, &old_rlim);
						if (!keep_stressing(args))
							break;
					}
				}
			}
#endif

#endif
			/* stress out select */
			FD_ZERO(&rfds);
			for (i = 0; i < max_fds; i++) {
				if (pipe_fds[i].fd[0] < FD_SETSIZE) {
					FD_SET(pipe_fds[i].fd[0], &rfds);
					if (pipe_fds[i].fd[0] > maxfd)
						maxfd = pipe_fds[i].fd[0];
				}
			}
			tv.tv_sec = 0;
			tv.tv_usec = 20000;
			ret = select(maxfd + 1, &rfds, NULL, NULL, &tv);
			if ((g_opt_flags & OPT_FLAGS_VERIFY) &&
			    (ret < 0) && (errno != EINTR)) {
				pr_fail("%s: select failed, errno=%d (%s)\n",
					args->name, errno, strerror(errno));
			}
			if (ret > 0) {
				for (i = 0; i < max_fds; i++) {
					if ((pipe_fds[i].fd[0] < FD_SETSIZE) &&
					     FD_ISSET(pipe_fds[i].fd[0], &rfds)) {
						if (pipe_read(args, pipe_fds[i].fd[0], i) < 0)
							break;
					}
				}
				inc_counter(args);
			}
			if (!keep_stressing(args))
				break;
#if defined(HAVE_PSELECT)
			/* stress out pselect */
			ts.tv_sec = 0;
			ts.tv_nsec = 20000000;

			(void)sigemptyset(&sigmask);
			(void)sigaddset(&sigmask, SIGPIPE);

			FD_ZERO(&rfds);
			for (i = 0; i < max_fds; i++) {
				if (pipe_fds[i].fd[0] < FD_SETSIZE) {
					FD_SET(pipe_fds[i].fd[0], &rfds);
					if (pipe_fds[i].fd[0] > maxfd)
						maxfd = pipe_fds[i].fd[0];
				}
			}
			ret = pselect(maxfd + 1, &rfds, NULL, NULL, &ts, &sigmask);
			if ((g_opt_flags & OPT_FLAGS_VERIFY) &&
			    (ret < 0) && (errno != EINTR)) {
				pr_fail("%s: pselect failed, errno=%d (%s)\n",
					args->name, errno, strerror(errno));
			}
			if (ret > 0) {
				for (i = 0; i < max_fds; i++) {
					if ((pipe_fds[i].fd[0] < FD_SETSIZE) &&
					    (FD_ISSET(pipe_fds[i].fd[0], &rfds))) {
						if (pipe_read(args, pipe_fds[i].fd[0], i) < 0)
							break;
					}
				}
				inc_counter(args);
			}
#endif
			/*
			 * stress zero sleep, this is like
			 * a select zero timeout
			 */
			(void)sleep(0);
		} while (keep_stressing(args));

		(void)kill(pid, SIGKILL);
		(void)shim_waitpid(pid, &status, 0);
	}

tidy:
	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);

	for (i = 0; i < max_fds; i++) {
		(void)close(pipe_fds[i].fd[0]);
		(void)close(pipe_fds[i].fd[1]);
	}

	free(poll_fds);
	free(pipe_fds);

	return rc;
}


stressor_info_t stress_poll_info = {
	.stressor = stress_poll,
	.class = CLASS_SCHEDULER | CLASS_OS,
	.opt_set_funcs = opt_set_funcs,
	.help = help
};
#else
stressor_info_t stress_poll_info = {
	.stressor = stress_not_implemented,
	.class = CLASS_SCHEDULER | CLASS_OS,
	.opt_set_funcs = opt_set_funcs,
	.help = help
};
#endif
