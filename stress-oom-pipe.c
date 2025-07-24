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
#include "core-capabilities.h"
#include "core-mmap.h"
#include "core-out-of-memory.h"

static const stress_help_t help[] = {
	{ NULL,	"oom-pipe N",	  "start N workers exercising large pipes" },
	{ NULL,	"oom-pipe-ops N", "stop after N oom-pipe bogo operations" },
	{ NULL,	NULL,		  NULL }
};

#if defined(F_SETPIPE_SZ) &&	\
    defined(O_NONBLOCK) &&	\
    defined(F_SETFL)

typedef struct {
	size_t max_fd;		/* Maximum allowed open file descriptors */
	size_t max_pipe_size;	/* Maximum pipe buffer size */
	void *rd_buffer;	/* Read buffer */
	void *wr_buffer;	/* Write buffer */
	int *fds;		/* File descriptors */
} stress_oom_pipe_context_t;

/*
 *  pipe_empty()
 *	read data from read end of pipe
 */
static void pipe_empty(
	const int fd,
	const size_t max,
	const size_t page_size,
	uint32_t *rd_buffer)
{
	size_t i;

	for (i = 0; i < max; i += page_size) {
		ssize_t ret;

		ret = read(fd, (void *)rd_buffer, page_size);
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
	uint32_t *wr_buffer)
{
	size_t i;
	static uint32_t val = 0;

	for (i = 0; i < max; i += page_size) {
		ssize_t ret;

		*wr_buffer = val++;
		ret = write(fd, (void *)wr_buffer, page_size);
		if (ret < (ssize_t)page_size)
			return;
	}
}

static int stress_oom_pipe_child(stress_args_t *args, void *ctxt)
{
	stress_oom_pipe_context_t *context = (stress_oom_pipe_context_t *)ctxt;
	const size_t max_pipes = context->max_fd / 2;
	const size_t page_size = args->page_size;

	size_t i;
	int *fds = context->fds, *fd, pipes_open = 0;
	const bool aggressive = (g_opt_flags & OPT_FLAGS_AGGRESSIVE);
	uint32_t *rd_buffer = context->rd_buffer;
	uint32_t *wr_buffer = context->wr_buffer;

	stress_uint8rnd4((uint8_t *)wr_buffer, page_size);

	/* Explicitly drop capabilities, makes it more OOM-able */
	VOID_RET(int, stress_drop_capabilities(args->name));

	for (i = 0; i < max_pipes * 2; i++)
		fds[i] = -1;

	for (i = 0; LIKELY(stress_continue(args) && (i < max_pipes)); i++) {
		int *pfd = fds + (2 * i);

		if ((g_opt_flags & OPT_FLAGS_OOM_AVOID) && stress_low_memory(page_size))
			break;

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
		return EXIT_NO_RESOURCE;
	}

	do {
		/* Set to maximum size */
		for (i = 0, fd = fds; LIKELY(stress_continue(args) && (i < max_pipes)); i++, fd += 2) {
			size_t max_size = context->max_pipe_size;

			if ((fd[0] < 0) || (fd[1] < 0))
				continue;
			if ((g_opt_flags & OPT_FLAGS_OOM_AVOID) && stress_low_memory(max_size * 2))
				break;
			if (fcntl(fd[0], F_SETPIPE_SZ, max_size) < 0)
				max_size = page_size;
			if (fcntl(fd[1], F_SETPIPE_SZ, max_size) < 0)
				max_size = page_size;
			pipe_fill(fd[1], max_size, page_size, wr_buffer);
			if (!aggressive)
				pipe_empty(fd[0], max_size, page_size, rd_buffer);
		}
		/* Set to invalid size */
		for (i = 0, fd = fds; LIKELY(stress_continue(args) && (i < max_pipes)); i++, fd += 2) {
			if ((fd[0] < 0) || (fd[1] < 0))
				continue;
			(void)fcntl(fd[0], F_SETPIPE_SZ, -1);
			(void)fcntl(fd[1], F_SETPIPE_SZ, -1);
		}

		/* Set to minimum size */
		for (i = 0, fd = fds; LIKELY(stress_continue(args) && (i < max_pipes)); i++, fd += 2) {
			if ((fd[0] < 0) || (fd[1] < 0))
				continue;
			(void)fcntl(fd[0], F_SETPIPE_SZ, page_size);
			(void)fcntl(fd[1], F_SETPIPE_SZ, page_size);
			pipe_fill(fd[1], context->max_pipe_size, page_size, wr_buffer);
			if (!aggressive)
				pipe_empty(fd[0], page_size, page_size, rd_buffer);
		}
		stress_bogo_inc(args);
	} while (stress_continue(args));

	/* And close the pipes */
clean:
	for (i = 0, fd = fds; i < max_pipes * 2; i++, fd++) {
		if (*fd >= 0)
			(void)close(*fd);
	}
	return EXIT_SUCCESS;
}

/*
 *  stress_oom_pipe
 *	stress pipe memory allocation
 */
static int stress_oom_pipe(stress_args_t *args)
{
	const size_t page_size = args->page_size;
	const size_t buffer_size = page_size << 1;
	int rc;
	stress_oom_pipe_context_t context;
	void *buffer;

	buffer = stress_mmap_populate(NULL, buffer_size, PROT_READ | PROT_WRITE,
			MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
	if (buffer == MAP_FAILED) {
		pr_inf_skip("%s: failed to mmap %zu byte pipe write buffer%s, "
			"errno=%d (%s), skipping stressor\n",
			args->name, buffer_size, stress_get_memfree_str(),
			errno, strerror(errno));
		return EXIT_NO_RESOURCE;
	}
	stress_set_vma_anon_name(buffer, buffer_size, "rw-pipe-buffer");
	context.wr_buffer = (uint32_t *)buffer;
	context.rd_buffer = (uint32_t *)((uintptr_t)buffer + page_size);

	context.max_fd = stress_get_file_limit();
	context.max_pipe_size = stress_probe_max_pipe_size();

	context.fds = (int *)calloc(context.max_fd, sizeof(*context.fds));
	if (!context.fds) {
		/* Shrink down */
		context.max_fd = 1024 * 1024;
		context.fds = (int *)calloc(context.max_fd, sizeof(*context.fds));
		if (!context.fds) {
			pr_inf_skip("%s: cannot allocate %zu file descriptors%s, skipping stressor\n",
				args->name, context.max_fd,
				stress_get_memfree_str());
			(void)munmap(buffer, buffer_size);
			return EXIT_NO_RESOURCE;
		}
	}

	if (context.max_pipe_size < page_size)
		context.max_pipe_size = page_size;
	context.max_pipe_size &= ~(page_size - 1);

	stress_set_proc_state(args->name, STRESS_STATE_SYNC_WAIT);
	stress_sync_start_wait(args);
	stress_set_proc_state(args->name, STRESS_STATE_RUN);

	rc = stress_oomable_child(args, &context, stress_oom_pipe_child, STRESS_OOMABLE_DROP_CAP);

	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);

	free(context.fds);
	(void)munmap(buffer, buffer_size);

	return rc;
}

const stressor_info_t stress_oom_pipe_info = {
	.stressor = stress_oom_pipe,
	.classifier = CLASS_MEMORY | CLASS_OS | CLASS_PATHOLOGICAL,
	.verify = VERIFY_ALWAYS,
	.help = help
};
#else
const stressor_info_t stress_oom_pipe_info = {
	.stressor = stress_unimplemented,
	.classifier = CLASS_MEMORY | CLASS_OS | CLASS_PATHOLOGICAL,
	.verify = VERIFY_ALWAYS,
	.help = help,
	.unimplemented_reason = "built without F_SETFL, F_SETPIPE_SZ or O_NONBLOCK fcntl() commands"
};
#endif
