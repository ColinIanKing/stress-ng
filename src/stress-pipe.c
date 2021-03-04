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

#define PIPE_STOP	"PS!"

static const stress_help_t help[] = {
	{ "p N", "pipe N",		"start N workers exercising pipe I/O" },
	{ NULL,	"pipe-ops N",		"stop after N pipe I/O bogo operations" },
	{ NULL,	"pipe-data-size N",	"set pipe size of each pipe write to N bytes" },
#if defined(F_SETPIPE_SZ)
	{ NULL,	"pipe-size N",		"set pipe size to N bytes" },
#endif
	{ NULL,	NULL,			NULL }
};

#if defined(F_SETPIPE_SZ)
/*
 *  stress_set_pipe_size()
 *	set pipe size in bytes
 */
static int stress_set_pipe_size(const char *opt)
{
	size_t pipe_size;

	pipe_size = (size_t)stress_get_uint64_byte(opt);
	stress_check_range_bytes("pipe-size", pipe_size, 4, 1024 * 1024);
	return stress_set_setting("pipe-size", TYPE_ID_SIZE_T, &pipe_size);
}

#endif

/*
 *  stress_set_pipe_size()
 *	set pipe data write size in bytes
 */
static int stress_set_pipe_data_size(const char *opt)
{
	size_t pipe_data_size;

	pipe_data_size = (size_t)stress_get_uint64_byte(opt);
	stress_check_range_bytes("pipe-data-size", pipe_data_size,
		4, stress_get_pagesize());
	return stress_set_setting("pipe-data-size,", TYPE_ID_SIZE_T, &pipe_data_size);
}

/*
 *  pipe_memset()
 *	set pipe data to be incrementing chars from val upwards
 */
static inline void pipe_memset(char *buf, char val, const size_t sz)
{
	size_t i;

	for (i = 0; i < sz; i++)
		*buf++ = val++;
}

/*
 *  pipe_memchk()
 *	check pipe data contains incrementing chars from val upwards
 */
static inline int pipe_memchk(char *buf, char val, const size_t sz)
{
	size_t i;

	for (i = 0; i < sz; i++)
		if (*buf++ != val++)
			return 1;
	return 0;
}

#if defined(F_SETPIPE_SZ)
/*
 *  pipe_change_size()
 *	see if we can change the pipe size
 */
static void pipe_change_size(
	const stress_args_t *args,
	const int fd,
	const size_t pipe_size)
{
#if defined(F_GETPIPE_SZ)
	ssize_t sz;
#endif
	if (!pipe_size)
		return;

#if !(defined(HAVE_PIPE2) && defined(O_DIRECT))
	if (pipe_size < args->page_size)
		return;
#endif
	if (fcntl(fd, F_SETPIPE_SZ, pipe_size) < 0) {
		pr_err("%s: cannot set pipe size, keeping "
			"default pipe size, errno=%d (%s)\n",
			args->name, errno, strerror(errno));
	}
#if defined(F_GETPIPE_SZ)
	/* Sanity check size */
	if ((sz = fcntl(fd, F_GETPIPE_SZ)) < 0) {
		pr_err("%s: cannot get pipe size, errno=%d (%s)\n",
			args->name, errno, strerror(errno));
	} else {
		if ((size_t)sz != pipe_size) {
			pr_err("%s: cannot set desired pipe size, "
				"pipe size=%zd, errno=%d (%s)\n",
				args->name, sz, errno, strerror(errno));
		}
	}
#endif
}
#endif


/*
 *  stress_pipe
 *	stress by heavy pipe I/O
 */
static int stress_pipe(const stress_args_t *args)
{
	pid_t pid;
	int pipefds[2];
	size_t pipe_data_size = 512;
	char *buf;

	(void)stress_get_setting("pipe-data-size", &pipe_data_size);

	buf = calloc(pipe_data_size, sizeof(*buf));
	if (!buf) {
		pr_err("%s: failed to allocate buffer\n", args->name);
		return EXIT_NO_RESOURCE;
	}

	(void)memset(pipefds, 0, sizeof(pipefds));

#if defined(HAVE_PIPE2) &&	\
    defined(O_DIRECT)
	if (pipe2(pipefds, O_DIRECT) < 0) {
		/*
		 *  Failed, fall back to standard pipe
		 */
		if (pipe(pipefds) < 0) {
			pr_fail("%s: pipe failed, errno=%d (%s)\n",
				args->name, errno, strerror(errno));
			free(buf);
			return EXIT_FAILURE;
		}
	}
#else
	if (pipe(pipefds) < 0) {
		pr_fail("%s: pipe failed, errno=%d (%s)\n",
			args->name, errno, strerror(errno));
		free(buf);
		return EXIT_FAILURE;
	}
#endif

#if defined(F_SETPIPE_SZ)
	{
		size_t pipe_size = 0;

		(void)stress_get_setting("pipe-size", &pipe_size);
		pipe_change_size(args, pipefds[0], pipe_size);
		pipe_change_size(args, pipefds[1], pipe_size);
	}
#endif

	stress_set_proc_state(args->name, STRESS_STATE_RUN);
again:
	pid = fork();
	if (pid < 0) {
		if (keep_stressing_flag() && (errno == EAGAIN))
			goto again;
		(void)close(pipefds[0]);
		(void)close(pipefds[1]);
		free(buf);
		pr_fail("%s: fork failed, errno=%d (%s)\n",
			args->name, errno, strerror(errno));
		return EXIT_FAILURE;
	} else if (pid == 0) {
		int val = 0;
#if defined(FIONREAD)
		int i = 0;
#endif

		(void)setpgid(0, g_pgrp);
		stress_parent_died_alarm();
		(void)sched_settings_apply(true);

		(void)close(pipefds[1]);
		while (keep_stressing_flag()) {
			ssize_t n;

			n = read(pipefds[0], buf, pipe_data_size);
			if (n <= 0) {
				if ((errno == EAGAIN) || (errno == EINTR))
					continue;
				if (errno) {
					pr_fail("%s: read failed, errno=%d (%s)\n",
						args->name, errno, strerror(errno));
					break;
				}
				pr_fail("%s: zero bytes read\n", args->name);
				break;
			}

#if defined(FIONREAD)
			/* Occasionally exercise FIONREAD on read end */
			if ((i++ & 0x1ff) == 0) {
				int ret, bytes;

				ret = ioctl(pipefds[0], FIONREAD, &bytes);
				(void)ret;
			}
#endif
			if (!strncmp(buf, PIPE_STOP, 3))
				break;
			if ((g_opt_flags & OPT_FLAGS_VERIFY) &&
			    pipe_memchk(buf, val++, (size_t)n)) {
				pr_fail("%s: pipe read error detected, "
					"failed to read expected data\n", args->name);
			}
		}
		(void)close(pipefds[0]);
		free(buf);
		_exit(EXIT_SUCCESS);
	} else {
		int val = 0, status;

		/* Parent */
		(void)setpgid(pid, g_pgrp);
		(void)close(pipefds[0]);

		do {
			ssize_t ret;

			pipe_memset(buf, val++, pipe_data_size);
			ret = write(pipefds[1], buf, pipe_data_size);
			if (ret <= 0) {
				if ((errno == EAGAIN) || (errno == EINTR))
					continue;
				if (errno) {
					pr_fail("%s: write failed, errno=%d (%s)\n",
						args->name, errno, strerror(errno));
					break;
				}
				continue;
			}
			inc_counter(args);
		} while (keep_stressing(args));

		(void)memset(buf, 0, pipe_data_size);
		(void)memcpy(buf, PIPE_STOP, sizeof(PIPE_STOP));
		if (write(pipefds[1], buf, pipe_data_size) <= 0) {
			if (errno != EPIPE)
				pr_fail("%s: termination write failed, errno=%d (%s)\n",
					args->name, errno, strerror(errno));
		}
		(void)kill(pid, SIGKILL);
		(void)shim_waitpid(pid, &status, 0);
		(void)close(pipefds[1]);
		free(buf);
	}
	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);

	return EXIT_SUCCESS;
}

static const stress_opt_set_func_t opt_set_funcs[] = {
#if defined(F_SETPIPE_SZ)
	{ OPT_pipe_size,	stress_set_pipe_size },
#endif
	{ OPT_pipe_data_size,	stress_set_pipe_data_size },
	{ 0,			NULL }
};

stressor_info_t stress_pipe_info = {
	.stressor = stress_pipe,
	.class = CLASS_PIPE_IO | CLASS_MEMORY | CLASS_OS,
	.opt_set_funcs = opt_set_funcs,
	.help = help
};
