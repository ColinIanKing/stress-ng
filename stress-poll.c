/*
 * Copyright (C) 2013-2021 Canonical, Ltd.
 * Copyright (C) 2022-2025 Colin Ian King.
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
#include "core-affinity.h"
#include "core-killpid.h"

#define MIN_POLL_FDS	(1)
#define MAX_POLL_FDS	(8192)	/* Must be never larger than 65535 */

#if defined(HAVE_SYS_SELECT_H)
#include <sys/select.h>
#endif

#if defined(HAVE_POLL_H)
#include <poll.h>
#endif

static const stress_help_t help[] = {
	{ "P N", "poll N",	    "start N workers exercising zero timeout polling" },
	{ NULL, "poll-fds N",	    "use N file descriptors" },
	{ NULL,	"poll-ops N",	    "stop after N poll bogo operations" },
	{ NULL, "poll-random-us N", "use random poll delays of 0..N-1 microseconds" },
	{ NULL,	NULL,		NULL }
};

static const stress_opt_t opts[] = {
	{ OPT_poll_fds,       "poll-fds",       TYPE_ID_SIZE_T, MIN_POLL_FDS, MAX_POLL_FDS, NULL },
	{ OPT_poll_random_us, "poll-random-us", TYPE_ID_UINT32, 0, 1000000, NULL },
	END_OPT,
};

#if defined(HAVE_POLL_H) &&	\
    defined(HAVE_POLL)

#define MAX_PIPES	(5)

typedef struct {
	int fd[2];
} pipe_fds_t;

/*
 *  pipe_read()
 *	read a pipe with some verification and checking
 */
static ssize_t OPTIMIZE3 pipe_read(stress_args_t *args, const int fd, const size_t n)
{
	const bool verify = !!(g_opt_flags & OPT_FLAGS_VERIFY);

	while (stress_continue_flag()) {
		ssize_t ret;
		uint16_t buf ALIGN64 = ~0;

		ret = read(fd, &buf, sizeof(buf));
		if (UNLIKELY(verify)) {
			if (LIKELY(ret > 0)) {
				if (UNLIKELY(buf != (uint16_t)n)) {
					pr_fail("%s: pipe read error, "
						"expecting different data on "
						"pipe\n", args->name);
					return ret;
				}
			} else if (UNLIKELY(ret < 0)) {
				if ((errno == EAGAIN) || (errno == EINTR))
					continue;
				pr_fail("%s: pipe read error detected, errno=%d (%s)\n",
					args->name, errno, strerror(errno));
				return ret;
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
static int OPTIMIZE3 stress_poll(stress_args_t *args)
{
	pid_t pid;
	int rc = EXIT_SUCCESS, parent_cpu;
	register size_t i;
	size_t max_fds = MAX_PIPES, max_rnd_fds;
	pipe_fds_t *pipe_fds;
	struct pollfd *poll_fds;
	int *rnd_fds_index;
	uint32_t poll_random_us = 0;

	if (!stress_get_setting("poll-fds", &max_fds)) {
		if (g_opt_flags & OPT_FLAGS_MAXIMIZE)
			max_fds = MAX_POLL_FDS;
		if (g_opt_flags & OPT_FLAGS_MINIMIZE)
			max_fds = MIN_POLL_FDS;
	}
	(void)stress_get_setting("poll-random-us", &poll_random_us);

	pipe_fds = (pipe_fds_t *)calloc(max_fds, sizeof(*pipe_fds));
	if (!pipe_fds) {
		pr_inf_skip("%s: out of memory allocating %zd pipe file descriptors%s, "
			"skipping stressor\n",
			args->name, max_fds, stress_get_memfree_str());
		return EXIT_NO_RESOURCE;
	}
	poll_fds = (struct pollfd *)calloc(max_fds, sizeof(*poll_fds));
	if (!poll_fds) {
		pr_inf("%s: out of memory allocating %zd poll file descriptors%s, "
			"skipping stressor\n",
			args->name, max_fds, stress_get_memfree_str());
		free(pipe_fds);
		return EXIT_NO_RESOURCE;
	}

	max_rnd_fds = max_fds;
	if (max_rnd_fds < MAX_POLL_FDS)
		max_rnd_fds = MAX_POLL_FDS - (MAX_POLL_FDS % max_fds);

	rnd_fds_index = (int *)calloc(max_rnd_fds, sizeof(*rnd_fds_index));
	if (!rnd_fds_index) {
		pr_inf("%s: out of memory allocating %zd randomized poll indices%s, "
			"skipping stressor\n",
			args->name, max_fds, stress_get_memfree_str());
		free(poll_fds);
		free(pipe_fds);
		return EXIT_NO_RESOURCE;
	}

	/* randomize: shuffle rnd_fds_index */
	for (i = 0; i < max_rnd_fds; i++) {
		rnd_fds_index[i] = i % max_fds;
	}
	for (i = 0; i < max_rnd_fds; i++) {
		register size_t tmp, j = stress_mwc32modn(max_rnd_fds);

		tmp = rnd_fds_index[i];
		rnd_fds_index[i] = rnd_fds_index[j];
		rnd_fds_index[j] = tmp;
	}

	for (i = 0; i < max_fds; i++) {
		if (pipe(pipe_fds[i].fd) < 0) {
			size_t j;

			pr_fail("%s: pipe failed, errno=%d (%s)\n",
				args->name, errno, strerror(errno));

			for (j = 0; j < i; j++) {
				(void)close(pipe_fds[j].fd[0]);
				(void)close(pipe_fds[j].fd[1]);
			}
			free(rnd_fds_index);
			free(poll_fds);
			free(pipe_fds);
			return EXIT_NO_RESOURCE;
		}
	}

	stress_set_proc_state(args->name, STRESS_STATE_SYNC_WAIT);
	stress_sync_start_wait(args);
	stress_set_proc_state(args->name, STRESS_STATE_RUN);
again:
	parent_cpu = stress_get_cpu();
	pid = fork();
	if (pid < 0) {
		if (stress_redo_fork(args, errno))
			goto again;
		if (UNLIKELY(!stress_continue(args)))
			goto tidy;
		pr_fail("%s: fork failed, errno=%d (%s)\n",
			args->name, errno, strerror(errno));
		rc = EXIT_FAILURE;
		goto tidy;
	} else if (pid == 0) {
		/* Child writer */

		stress_set_proc_state(args->name, STRESS_STATE_RUN);
		(void)stress_change_cpu(args, parent_cpu);
		stress_parent_died_alarm();
		(void)sched_settings_apply(true);

		for (i = 0; i < max_fds; i++)
			(void)close(pipe_fds[i].fd[0]);

		i = 0;
		do {
			register size_t j = rnd_fds_index[i];
			uint16_t buf ALIGN64 = (uint16_t)j;
			register ssize_t ret;

			i++;
			if (UNLIKELY(i >= max_rnd_fds))
				i = 0;

			/* Write on a randomly chosen pipe */
			ret = write(pipe_fds[j].fd[1], &buf, sizeof(buf));
			if (UNLIKELY(ret < (ssize_t)sizeof(buf))) {
				if ((errno == EAGAIN) || (errno == EINTR))
					continue;
				pr_fail("%s: write failed, errno=%d (%s)\n",
					args->name, errno, strerror(errno));
				goto abort;
			}
		 } while (stress_continue(args));
abort:
		for (i = 0; i < max_fds; i++)
			(void)close(pipe_fds[i].fd[1]);
		exit(EXIT_SUCCESS);
	} else {
		/* Parent read */

#if defined(HAVE_SYS_SELECT_H) &&	\
    (defined(HAVE_SELECT) ||		\
     defined(HAVE_PSELECT))
		int maxfd = 0;
		fd_set rfds;
#endif
#if defined(HAVE_PPOLL) ||	\
    defined(HAVE_PSELECT)
		struct timespec ts;
		sigset_t sigmask;
#endif

		for (i = 0; i < max_fds; i++) {
			poll_fds[i].fd = pipe_fds[i].fd[0];
			poll_fds[i].events = POLLIN;
			poll_fds[i].revents = 0;
		}

		/*
		 * Exercise POLLNVAL revents being set on fds[1] by
		 * setting the fd to a highly probable invalid fd
		 * number. This should not make poll fail and should
		 * set poll_fds[1].revents to POLLNVAL
		 */
		if (max_fds > 2)
			poll_fds[1].fd = 10000;

		do {
#if defined(HAVE_SYS_SELECT_H) &&	\
    defined(HAVE_SELECT)
			struct timeval tv;
#endif
			int ret;

			if (UNLIKELY(!stress_continue(args)))
				break;

			/* stress out poll */
			ret = poll(poll_fds, max_fds, 1);
			if (UNLIKELY((g_opt_flags & OPT_FLAGS_VERIFY) &&
				     (ret < 0) && (errno != EINTR))) {
				pr_fail("%s: poll failed, errno=%d (%s)\n",
					args->name, errno, strerror(errno));
			}
			if (LIKELY(ret > 0)) {
				for (i = 0; i < max_fds; i++) {
					if (poll_fds[i].revents == POLLIN) {
						if (UNLIKELY(pipe_read(args, poll_fds[i].fd, i) < 0))
							break;
					}
				}
				stress_bogo_inc(args);
			}

			if (UNLIKELY(!stress_continue(args)))
				break;

#if defined(HAVE_PPOLL)
			/* stress out ppoll */

			ts.tv_sec = 0;
			ts.tv_nsec = poll_random_us ? (long)stress_mwc32modn(poll_random_us * 1000) : 20000000L;

			(void)sigemptyset(&sigmask);
			(void)sigaddset(&sigmask, SIGPIPE);

			ret = shim_ppoll(poll_fds, max_fds, &ts, &sigmask);
			if (UNLIKELY((g_opt_flags & OPT_FLAGS_VERIFY) &&
				     (ret < 0) && (errno != EINTR))) {
				pr_fail("%s: ppoll failed, errno=%d (%s)\n",
					args->name, errno, strerror(errno));
			}
			if (LIKELY(ret > 0)) {
				for (i = 0; i < max_fds; i++) {
					if (poll_fds[i].revents == POLLIN) {
						if (UNLIKELY(pipe_read(args, poll_fds[i].fd, i) < 0))
							break;
					}
				}
				stress_bogo_inc(args);
			}
			if (UNLIKELY(!stress_continue(args)))
				break;

			/* Exercise illegal poll timeout */
			ts.tv_sec = 0;
			ts.tv_nsec = 1999999999;
			VOID_RET(int, shim_ppoll(poll_fds, max_fds, &ts, &sigmask));
			if (UNLIKELY(!stress_continue(args)))
				break;

#if defined(RLIMIT_NOFILE)
			/*
			 *  Exercise ppoll with more fds than rlimit soft
			 *  limit. This should fail with EINVAL on Linux.
			 */
			{
				struct rlimit old_rlim, new_rlim;

				if (LIKELY(getrlimit(RLIMIT_NOFILE, &old_rlim) == 0)) {
					new_rlim.rlim_cur = max_fds - 1;
					new_rlim.rlim_max = old_rlim.rlim_max;

					if (LIKELY(setrlimit(RLIMIT_NOFILE, &new_rlim) == 0)) {
						ts.tv_sec = 0;
						ts.tv_nsec = 0;
						VOID_RET(int, shim_ppoll(poll_fds, max_fds, &ts, &sigmask));

						(void)setrlimit(RLIMIT_NOFILE, &old_rlim);
						if (UNLIKELY(!stress_continue(args)))
							break;
					}
				}
			}
#endif

#endif

#if defined(HAVE_SYS_SELECT_H) &&	\
    defined(HAVE_SELECT)
			/* stress out select */
			FD_ZERO(&rfds);
			for (i = 0; i < max_fds; i++) {
				if (pipe_fds[i].fd[0] < FD_SETSIZE) {
					FD_SET(pipe_fds[i].fd[0], &rfds);
					if (LIKELY(pipe_fds[i].fd[0] > maxfd))
						maxfd = pipe_fds[i].fd[0];
				}
			}
			tv.tv_sec = 0;
			tv.tv_usec = poll_random_us ? (long)stress_mwc32modn(poll_random_us) : 20000L;
			ret = select(maxfd + 1, &rfds, NULL, NULL, &tv);
			if (UNLIKELY((g_opt_flags & OPT_FLAGS_VERIFY) &&
				     (ret < 0) && (errno != EINTR))) {
				pr_fail("%s: select failed, errno=%d (%s)\n",
					args->name, errno, strerror(errno));
			}
			if (LIKELY(ret > 0)) {
				for (i = 0; i < max_fds; i++) {
					if ((pipe_fds[i].fd[0] < FD_SETSIZE) &&
					     FD_ISSET(pipe_fds[i].fd[0], &rfds)) {
						if (UNLIKELY(pipe_read(args, pipe_fds[i].fd[0], i) < 0))
							break;
					}
				}
				stress_bogo_inc(args);
			}
			if (UNLIKELY(!stress_continue(args)))
				break;
#endif

#if defined(HAVE_PSELECT)
			/* stress out pselect */
			ts.tv_sec = 0;
			ts.tv_nsec = poll_random_us ? (long)stress_mwc32modn(poll_random_us * 1000) : 20000000L;

			(void)sigemptyset(&sigmask);
			(void)sigaddset(&sigmask, SIGPIPE);

			FD_ZERO(&rfds);
			for (i = 0; i < max_fds; i++) {
				if (pipe_fds[i].fd[0] < FD_SETSIZE) {
					FD_SET(pipe_fds[i].fd[0], &rfds);
					if (LIKELY(pipe_fds[i].fd[0] > maxfd))
						maxfd = pipe_fds[i].fd[0];
				}
			}
			ret = pselect(maxfd + 1, &rfds, NULL, NULL, &ts, &sigmask);
			if (UNLIKELY((g_opt_flags & OPT_FLAGS_VERIFY) &&
				     (ret < 0) && (errno != EINTR))) {
				pr_fail("%s: pselect failed, errno=%d (%s)\n",
					args->name, errno, strerror(errno));
			}
			if (LIKELY(ret > 0)) {
				for (i = 0; i < max_fds; i++) {
					if ((pipe_fds[i].fd[0] < FD_SETSIZE) &&
					    (FD_ISSET(pipe_fds[i].fd[0], &rfds))) {
						if (UNLIKELY(pipe_read(args, pipe_fds[i].fd[0], i) < 0))
							break;
					}
				}
				stress_bogo_inc(args);
			}
#endif
			/*
			 * stress zero sleep, this is like
			 * a select zero timeout
			 */
			(void)sleep(0);
		} while (stress_continue(args));

		(void)stress_kill_pid_wait(pid, NULL);
	}

tidy:
	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);

	for (i = 0; i < max_fds; i++) {
		(void)close(pipe_fds[i].fd[0]);
		(void)close(pipe_fds[i].fd[1]);
	}

	free(rnd_fds_index);
	free(poll_fds);
	free(pipe_fds);

	return rc;
}


const stressor_info_t stress_poll_info = {
	.stressor = stress_poll,
	.classifier = CLASS_SCHEDULER | CLASS_OS,
	.opts = opts,
	.verify = VERIFY_OPTIONAL,
	.help = help
};
#else
const stressor_info_t stress_poll_info = {
	.stressor = stress_unimplemented,
	.classifier = CLASS_SCHEDULER | CLASS_OS,
	.opts = opts,
	.verify = VERIFY_OPTIONAL,
	.help = help,
	.unimplemented_reason = "built without poll.h"
};
#endif
