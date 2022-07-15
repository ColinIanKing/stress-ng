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

static const stress_help_t help[] = {
	{ NULL,	"oom-pipe N",	  "start N workers exercising large pipes" },
	{ NULL,	"oom-pipe-ops N", "stop after N oom-pipe bogo operations" },
	{ NULL,	NULL,		  NULL }
};

#if defined(F_SETPIPE_SZ) &&	\
    defined(O_NONBLOCK) &&	\
    defined(F_SETFL)

typedef struct {
	size_t max_fd;
	size_t max_pipe_size;
} stress_oom_pipe_context_t;

/*
 *  pipe_empty()
 *	read data from read end of pipe
 */
static void pipe_empty(const int fd, const size_t max, const size_t page_size)
{
	size_t i;

	for (i = 0; i < max; i += page_size) {
		ssize_t ret;
		char buffer[page_size];

		ret = read(fd, buffer, sizeof(buffer));
		if (ret < 0)
			return;
	}
}

/*
 *  pipe_fill()
 *	write data to fill write end of pipe
 */
static void pipe_fill(
	const int fd,
	const size_t max,
	const size_t page_size,
	char *buffer)
{
	size_t i;
	static uint32_t val = 0;
	uint32_t *u32ptr = (uint32_t *)buffer;

	for (i = 0; i < max; i += page_size) {
		ssize_t ret;

		*u32ptr = val++;
		ret = write(fd, buffer, page_size);
		if (ret < (ssize_t)page_size)
			return;
	}
}

static int stress_oom_pipe_child(const stress_args_t *args, void *ctxt)
{
	stress_oom_pipe_context_t *context = (stress_oom_pipe_context_t *)ctxt;
	const size_t max_pipes = context->max_fd / 2;
	const size_t page_size = args->page_size;

	size_t i;
	int fds[max_pipes * 2], *fd, pipes_open = 0;
	const bool aggressive = (g_opt_flags & OPT_FLAGS_AGGRESSIVE);
	char *buffer;

	buffer = malloc(page_size);
	if (!buffer) {
		pr_err("%s: cannot allocate pipe write buffer\n", args->name);
		return EXIT_NO_RESOURCE;
	}
	stress_uint8rnd4((uint8_t *)buffer, page_size);

	/* Explicitly drop capabilities, makes it more OOM-able */
	VOID_RET(int, stress_drop_capabilities(args->name));

	for (i = 0; i < max_pipes * 2; i++)
		fds[i] = -1;

	for (i = 0; keep_stressing(args) && (i < max_pipes); i++) {
		int *pfd = fds + (2 * i);
		if (pipe(pfd) < 0) {
			pfd[0] = -1;
			pfd[1] = -1;
			break;
		} else {
			if (fcntl(pfd[0], F_SETFL, O_NONBLOCK) < 0) {
				pr_fail("%s: fcntl F_SET_FL O_NONBLOCK failed, errno=%d (%s)\n",
					args->name, errno, strerror(errno));
				goto clean;
			}
			if (fcntl(pfd[1], F_SETFL, O_NONBLOCK) < 0) {
				pr_fail("%s: fcntl F_SET_FL O_NONBLOCK failed, errno=%d (%s)\n",
					args->name, errno, strerror(errno));
				goto clean;
			}
			pipes_open++;
		}
	}

	if (!pipes_open) {
		pr_dbg("%s: failed to open any pipes, aborted\n",
			args->name);
		free(buffer);
		return EXIT_NO_RESOURCE;
	}

	do {
		/* Set to maximum size */
		for (i = 0, fd = fds; keep_stressing(args) && (i < max_pipes); i++, fd += 2) {
			size_t max_size = context->max_pipe_size;

			if ((fd[0] < 0) || (fd[1] < 0))
				continue;
			if (fcntl(fd[0], F_SETPIPE_SZ, max_size) < 0)
				max_size = page_size;
			if (fcntl(fd[1], F_SETPIPE_SZ, max_size) < 0)
				max_size = page_size;
			pipe_fill(fd[1], max_size, page_size, buffer);
			if (!aggressive)
				pipe_empty(fd[0], max_size, page_size);
		}
		/* Set to invalid size */
		for (i = 0, fd = fds; keep_stressing(args) && (i < max_pipes); i++, fd += 2) {
			if ((fd[0] < 0) || (fd[1] < 0))
				continue;
			(void)fcntl(fd[0], F_SETPIPE_SZ, -1);
			(void)fcntl(fd[1], F_SETPIPE_SZ, -1);
		}

		/* Set to minimum size */
		for (i = 0, fd = fds; keep_stressing(args) && (i < max_pipes); i++, fd += 2) {
			if ((fd[0] < 0) || (fd[1] < 0))
				continue;
			(void)fcntl(fd[0], F_SETPIPE_SZ, page_size);
			(void)fcntl(fd[1], F_SETPIPE_SZ, page_size);
			pipe_fill(fd[1], context->max_pipe_size, page_size, buffer);
			if (!aggressive)
				pipe_empty(fd[0], page_size, page_size);
		}
		inc_counter(args);
	} while (keep_stressing(args));

	/* And close the pipes */
clean:
	for (i = 0, fd = fds; i < max_pipes * 2; i++, fd++) {
		if (*fd >= 0)
			(void)close(*fd);
	}
	free(buffer);
	return EXIT_SUCCESS;
}


/*
 *  stress_oom_pipe
 *	stress pipe memory allocation
 */
static int stress_oom_pipe(const stress_args_t *args)
{
	const size_t page_size = args->page_size;
	int rc;

	stress_oom_pipe_context_t context;

	context.max_fd = stress_get_file_limit();
	context.max_pipe_size = stress_probe_max_pipe_size();

	if (context.max_pipe_size < page_size)
		context.max_pipe_size = page_size;
	context.max_pipe_size &= ~(page_size - 1);

	stress_set_proc_state(args->name, STRESS_STATE_RUN);

	rc = stress_oomable_child(args, &context, stress_oom_pipe_child, STRESS_OOMABLE_DROP_CAP);

	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);

	return rc;
}

stressor_info_t stress_oom_pipe_info = {
	.stressor = stress_oom_pipe,
	.class = CLASS_MEMORY | CLASS_OS | CLASS_PATHOLOGICAL,
	.verify = VERIFY_ALWAYS,
	.help = help
};
#else
stressor_info_t stress_oom_pipe_info = {
	.stressor = stress_not_implemented,
	.class = CLASS_MEMORY | CLASS_OS | CLASS_PATHOLOGICAL,
	.help = help
};
#endif
