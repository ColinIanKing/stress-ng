/*
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
#include "core-builtin.h"
#include "core-mmap.h"

#if defined(HAVE_POLL_H)
#include <poll.h>
#endif

#define STRESS_RING_PIPE_NUM_MIN	(4)
#define STRESS_RING_PIPE_NUM_MAX	(256*1024)

#define STRESS_RING_PIPE_SIZE_MIN	(1)
#define STRESS_RING_PIPE_SIZE_MAX	(4096)

typedef struct {
	int fds[2];
} pipe_fds_t;

static const stress_help_t help[] = {
	{ NULL, "ring-pipe N",		"start N workers exercising a ring of pipes" },
	{ NULL,	"ring-pipe-num N",	"number of pipes to use" },
	{ NULL,	"ring-pipe-ops N",	"stop after N ring pipe I/O bogo operations" },
	{ NULL,	"ring-pipe-size N",	"size of data to be written and read in bytes" },
	{ NULL, "ring-pipe-splice",	"use splice instead of read+write" },
	{ NULL,	NULL,			NULL }
};

static const stress_opt_t opts[] = {
	{ OPT_ring_pipe_num,	"ring-pipe-num",    TYPE_ID_SIZE_T, STRESS_RING_PIPE_NUM_MIN, STRESS_RING_PIPE_NUM_MAX, NULL },
	{ OPT_ring_pipe_size,	"ring-pipe-size",   TYPE_ID_SIZE_T_BYTES_VM, STRESS_RING_PIPE_SIZE_MIN, STRESS_RING_PIPE_SIZE_MAX, NULL },
	{ OPT_ring_pipe_splice,	"ring-pipe-splice", TYPE_ID_BOOL, 0, 1, NULL },
	END_OPT,
};

#if defined(HAVE_POLL_H) &&	\
    defined(HAVE_POLL)
static int stress_pipe_non_block(stress_args_t *args, const int fd)
{
	int flags, ret;

	flags = fcntl(fd, F_GETFL);
	flags |= O_NONBLOCK;
	ret = fcntl(fd, F_SETFL, flags);
	if (UNLIKELY(ret < 0)) {
		pr_inf("%s: cannot set O_NONBLOCK on pipe fd %d\n",
			args->name, fd);
		return -1;
	}
	return 0;
}

/*
 *  stress_pipe_read()
 *	read data from a pipe, return bytes read, -1 for failure
 */
static ssize_t stress_pipe_read(
	stress_args_t *args,
	const int fd,
	char *buf,
	const size_t buf_len)
{
	ssize_t sret;

	sret = read(fd, buf, buf_len);
	if (UNLIKELY(sret < 0)) {
		pr_inf("%s: failed to read from pipe fd %d, errno=%d (%s)\n",
			args->name, fd, errno, strerror(errno));
		return -1;
	}
	return sret;
}

/*
 *  stress_pipe_write
 *	write data to a pipe, return bytes written, -1 for failure
 */
static ssize_t stress_pipe_write(
	stress_args_t *args,
	const int fd,
	char *buf,
	const size_t buf_len)
{
	ssize_t sret;

	sret = write(fd, buf, buf_len);
	if (UNLIKELY(sret < 0)) {
		pr_inf("%s: failed to write to pipe fd %d, errno=%d (%s)\n",
			args->name, fd, errno, strerror(errno));
		return -1;
	}
	return sret;
}

/*
 *  stress_ring_pipe
 *	stress by heavy pipe I/O in a ring of pipes
 */
static int stress_ring_pipe(stress_args_t *args)
{
	double duration = 0.0, bytes = 0.0, rate;
	size_t i, n_pipes, ring_pipe_num = 256, ring_pipe_size = 4096;
	bool ring_pipe_splice = false;
	char *buf;
	pipe_fds_t *pipe_fds;
	int ret, max_fd;
	int rc = EXIT_NO_RESOURCE;
	ssize_t sret;
	struct pollfd *poll_fds;

	(void)stress_get_setting("ring-pipe-num", &ring_pipe_num);
	(void)stress_get_setting("ring-pipe-size", &ring_pipe_size);
	(void)stress_get_setting("ring-pipe-splice", &ring_pipe_splice);

	buf = stress_mmap_populate(NULL, (size_t)STRESS_RING_PIPE_SIZE_MAX,
		PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
	if (buf == MAP_FAILED) {
		pr_inf_skip("%s: failed to mmap %d size buffer%s, "
			"errno=%d (%s), skipping stresor\n",
			args->name, STRESS_RING_PIPE_SIZE_MAX,
			stress_get_memfree_str(), errno, strerror(errno));
		goto err_ret;
	}
	stress_set_vma_anon_name(buf, STRESS_RING_PIPE_SIZE_MAX, "ring-pipe-buffer");

	pipe_fds = (pipe_fds_t *)calloc(ring_pipe_num, sizeof(*pipe_fds));
	if (!pipe_fds) {
		pr_inf_skip("%s: failed to allocate %zu pipe file descriptors%s, "
			"skipping stresor\n", args->name, ring_pipe_num,
			stress_get_memfree_str());
		goto err_unmap_buf;
	}
	poll_fds = (struct pollfd *)calloc(ring_pipe_num, sizeof(*poll_fds));
	if (!poll_fds) {
		pr_inf_skip("%s: cannot allocate %zu poll descriptors%s, "
			"skipping stresor\n", args->name, ring_pipe_num,
			stress_get_memfree_str());
		goto err_free_pipe_fds;
	}

	for (max_fd = 0, n_pipes = 0; n_pipes < ring_pipe_num; n_pipes++) {
		ret = pipe(pipe_fds[n_pipes].fds);
		if (ret < 0)
			break;

		poll_fds[n_pipes].fd = pipe_fds[n_pipes].fds[0];
		poll_fds[n_pipes].events = POLLIN;
		poll_fds[n_pipes].revents = 0;

		if (max_fd < pipe_fds[n_pipes].fds[0])
			max_fd = pipe_fds[n_pipes].fds[0];
		if (max_fd < pipe_fds[n_pipes].fds[1])
			max_fd = pipe_fds[n_pipes].fds[1];
		stress_pipe_non_block(args, pipe_fds[n_pipes].fds[0]);
		stress_pipe_non_block(args, pipe_fds[n_pipes].fds[1]);

	}

	if (n_pipes == 0) {
		pr_inf_skip("%s: not enough pipes were created, "
			"skipping stressor\n", args->name);
		goto err_close_pipes;
	} else if (n_pipes < ring_pipe_num) {
		pr_inf("%s: limiting to %zd pipes due to file descriptor limit\n",
			args->name, n_pipes);
	}

#if !defined(HAVE_SPLICE)
	if (stress_instance_zero(args) && ring_pipe_splice) {
		pr_inf("%s: note: falling back to using read + writes as "
			"splice is not available\n", args->name);
		ring_pipe_splice = false;
	}
#endif

	if (stress_instance_zero(args)) {
		pr_inf("%s: using %zd pipes with %zd byte data, %s\n",
			args->name, n_pipes, ring_pipe_size,
			ring_pipe_splice ? "using splice" : "using read+write");
	}

	stress_set_proc_state(args->name, STRESS_STATE_SYNC_WAIT);
	stress_sync_start_wait(args);
	stress_set_proc_state(args->name, STRESS_STATE_RUN);

	(void)shim_memset(buf, 0xa5, STRESS_RING_PIPE_SIZE_MAX);

	if (stress_pipe_write(args, pipe_fds[0].fds[1], buf, ring_pipe_size) < 0)
		goto err_deinit;
	if (stress_pipe_write(args, pipe_fds[n_pipes >> 1].fds[1], buf, ring_pipe_size) < 0)
		goto err_deinit;

	do {
		ret = poll(poll_fds, n_pipes, 100);
		if (UNLIKELY(ret == 0)) {
			pr_inf("%s: unexpected poll timeout\n", args->name);
			break;
		} else {
			for (i = 0; LIKELY(stress_continue(args) && (i < n_pipes)); i++) {
				if (poll_fds[i].revents & POLLIN) {
					double t;
					register size_t j = (i + 1);

					j = (j >= n_pipes) ? 0 : j;
#if defined(HAVE_SPLICE)
					if (ring_pipe_splice) {
#if defined(SPLICE_F_MOVE)
						int flag = SPLICE_F_MOVE;
#else
						int flag = 0;
#endif
						t = stress_time_now();
						sret = splice(pipe_fds[i].fds[0], 0, pipe_fds[j].fds[1], 0,
							      ring_pipe_size, flag);
						if (UNLIKELY(sret < 0)) {
							pr_inf("%s: splice failed, errno=%d (%s)\n",
								args->name, errno, strerror(errno));
							goto finish;
						}
						duration += stress_time_now() - t;
						stress_bogo_inc(args);
						bytes += (double)sret;
						continue;
					}
#endif
					t = stress_time_now();
					sret = stress_pipe_read(args, pipe_fds[i].fds[0], buf, ring_pipe_size);
					if (UNLIKELY(sret < 0))
						goto finish;
					sret = stress_pipe_write(args, pipe_fds[j].fds[1], buf, sret);
					if (UNLIKELY(sret < 0))
						goto finish;
					duration += stress_time_now() - t;
					stress_bogo_inc(args);
					bytes += (double)sret;
				}
			}
		}
	} while (stress_continue(args));
	rc = EXIT_SUCCESS;

finish:
	rate = (duration > 0.0) ? (double)stress_bogo_get(args) / duration : 0.0;
	stress_metrics_set(args, 0, "pipe read+write calls per sec",
		rate, STRESS_METRIC_HARMONIC_MEAN);
	rate = (duration > 0.0) ? (double)bytes / duration : 0.0;
	stress_metrics_set(args, 1, "MB per sec data pipe write",
		rate / (double)MB, STRESS_METRIC_HARMONIC_MEAN);

err_deinit:
	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);
err_close_pipes:
	for (i = 0; i < n_pipes; i++) {
		(void)close(pipe_fds[i].fds[0]);
		(void)close(pipe_fds[i].fds[1]);
	}
	free(poll_fds);
err_free_pipe_fds:
	free(pipe_fds);
err_unmap_buf:
	(void)munmap((void *)buf, STRESS_RING_PIPE_SIZE_MAX);
err_ret:
	return rc;
}

const stressor_info_t stress_ring_pipe_info = {
	.stressor = stress_ring_pipe,
	.classifier = CLASS_PIPE_IO | CLASS_OS,
	.opts = opts,
	.verify = VERIFY_NONE,
	.help = help
};

#else

const stressor_info_t stress_ring_pipe_info = {
	.stressor = stress_unimplemented,
	.classifier = CLASS_PIPE_IO | CLASS_OS,
	.opts = opts,
	.verify = VERIFY_NONE,
	.help = help,
	.unimplemented_reason = "built without poll.h or poll() support"
};

#endif
