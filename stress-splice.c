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
#include "core-builtin.h"
#include "core-madvise.h"
#include "core-mmap.h"

#define MIN_SPLICE_BYTES	(1 * KB)
#define MAX_SPLICE_BYTES	(64 * MB)
#define DEFAULT_SPLICE_BYTES	(64 * KB)

#define SPLICE_BUFFER_LEN	(65536)

static const stress_help_t help[] = {
	{ NULL,	"splice N",	  "start N workers reading/writing using splice" },
	{ NULL,	"splice-bytes N", "number of bytes to transfer per splice call" },
	{ NULL,	"splice-ops N",	  "stop after N bogo splice operations" },
	{ NULL,	NULL,		  NULL }
};

static const stress_opt_t opts[] = {
	{ OPT_splice_bytes, "splice-bytes", TYPE_ID_SIZE_T_BYTES_VM, MIN_SPLICE_BYTES, MAX_MEM_LIMIT, NULL },
	END_OPT,
};

#if defined(HAVE_SPLICE)

/*
 *  stress_splice_pipe_size()
 *	set random splice flags
 */
static unsigned int stress_splice_flag(void)
{
	unsigned int flag = 0;

#if defined(SPLICE_F_MOVE)
	flag |= (stress_mwc1() ? SPLICE_F_MOVE : 0);
#endif
#if defined(SPLICE_F_MORE)
	flag |= (stress_mwc1() ? SPLICE_F_MORE : 0);
#endif
	return flag;
}

/*
 *  stress_splice_pipe_size()
 *	attempt to se pipe size of multiples of the splice buffer length
 */
static int stress_splice_pipe_size(const int fd)
{
#if defined(F_SETPIPE_SZ)
	const size_t pipe_size = (1 + (stress_mwc8() & 3)) * SPLICE_BUFFER_LEN;

	return fcntl(fd, F_SETPIPE_SZ, pipe_size);
#else
	(void)fd;

	return 0;
#endif
}

/*
 *  stress_splice_write()
 *	write buffer to fd
 */
static inline int stress_splice_write(
	const int fd,
	const char *buffer,
	const ssize_t buffer_len,
	ssize_t size)
{
	ssize_t ret = 0;

	while (size > 0) {
		const size_t n = (size_t)(size > buffer_len ? buffer_len : size);

		ret = write(fd, buffer, n);
		if (UNLIKELY(ret < 0))
			break;
		size -= n;

	}
	return (int)ret;
}

/*
 *  stress_splice_non_block_write_4K()
 *	get some data into a pipe to prime it for a read
 *	with stress_splice_looped_pipe()
 */
static bool stress_splice_non_block_write_4K(const int fd)
{
	char buffer[4096] ALIGN64;
	int flags;

	flags = fcntl(fd, F_GETFL, 0);
	if (UNLIKELY(flags < 0))
		return false;
	if (UNLIKELY(fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0))
		return false;
	(void)shim_memset(buffer, 0xa5, sizeof(buffer));
        if (write(fd, buffer, sizeof(buffer)) < 0)
		return false;
        if (fcntl(fd, F_SETFL, flags) < 0)
		return false;
	return true;
}

/*
 *  stress_splice_looped_pipe()
 *	splice data into one pipe, out of another and back into
 *	the first pipe
 */
static void stress_splice_looped_pipe(
	const int fds3[2],
	const int fds4[2],
	bool *use_splice_loop)
{
	ssize_t ret;

	if (UNLIKELY(!*use_splice_loop))
		return;

	ret = splice(fds3[0], 0, fds4[1], 0, 4096, stress_splice_flag());
	if (UNLIKELY(ret < 0)) {
		*use_splice_loop = false;
		return;
	}
	ret = splice(fds4[0], 0, fds3[1], 0, 4096, stress_splice_flag());
	if (UNLIKELY(ret < 0)) {
		*use_splice_loop = false;
		return;
	}
}

/*
 *  stress_splice
 *	stress copying of /dev/zero to /dev/null
 */
static int stress_splice(stress_args_t *args)
{
	int fd_in, fd_out, fds1[2], fds2[2], fds3[2], fds4[2];
	size_t splice_bytes, splice_bytes_total = DEFAULT_SPLICE_BYTES;
	int rc = EXIT_FAILURE;
	int metrics_count = 0;
	bool use_splice = true;
	bool use_splice_loop;
	char *buffer;
	ssize_t buffer_len;
	double duration = 0.0, bytes = 0.0, rate;

	if (!stress_get_setting("splice-bytes", &splice_bytes_total)) {
		if (g_opt_flags & OPT_FLAGS_MAXIMIZE)
			splice_bytes_total = MAX_SPLICE_BYTES;
		if (g_opt_flags & OPT_FLAGS_MINIMIZE)
			splice_bytes_total = MIN_SPLICE_BYTES;
	}
	splice_bytes = splice_bytes_total / args->instances;
	if (splice_bytes < MIN_SPLICE_BYTES)
		splice_bytes = MIN_SPLICE_BYTES;
	if (stress_instance_zero(args))
		stress_usage_bytes(args, splice_bytes, splice_bytes * args->instances);

	buffer_len = (ssize_t)(splice_bytes > SPLICE_BUFFER_LEN ?
				SPLICE_BUFFER_LEN : splice_bytes);
	buffer = stress_mmap_populate(NULL, (size_t)buffer_len,
			PROT_READ | PROT_WRITE,
			MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
	if (buffer == MAP_FAILED) {
		pr_inf("%s: failed to mmap %zu byte write buffer%s, errno=%d (%s)\n",
			args->name, (size_t)buffer_len,
			stress_get_memfree_str(), errno, strerror(errno));
		goto close_done;
	}
	stress_set_vma_anon_name(buffer, buffer_len, "write-buffer");
	(void)stress_madvise_mergeable(buffer, buffer_len);

	if ((fd_in = open("/dev/zero", O_RDONLY)) < 0) {
		pr_fail("%s: open /dev/zero failed, errno=%d (%s)\n",
			args->name, errno, strerror(errno));
		goto close_unmap;
	}

	/*
	 *  /dev/zero -> pipe splice -> pipe splice -> /dev/null
	 */
	if (pipe(fds1) < 0) {
		pr_fail("%s: pipe failed, errno=%d (%s)\n",
			args->name, errno, strerror(errno));
		goto close_fd_in;
	}

	if (pipe(fds2) < 0) {
		pr_fail("%s: pipe failed, errno=%d (%s)\n",
			args->name, errno, strerror(errno));
		goto close_fds1;
	}

	if (pipe(fds3) < 0) {
		pr_fail("%s: pipe failed, errno=%d (%s)\n",
			args->name, errno, strerror(errno));
		goto close_fds2;
	}

	if (pipe(fds4) < 0) {
		pr_fail("%s: pipe failed, errno=%d (%s)\n",
			args->name, errno, strerror(errno));
		goto close_fds3;
	}

	if ((fd_out = open("/dev/null", O_WRONLY)) < 0) {
		pr_fail("%s: open /dev/null failed, errno=%d (%s)\n",
			args->name, errno, strerror(errno));
		goto close_fds4;
	}

	/*
	 *  We may as well exercise setting pipe size at start/end
	 *  of pipe for more kernel exercising
 	 */
	(void)stress_splice_pipe_size(fds1[0]);
	(void)stress_splice_pipe_size(fds1[1]);
	(void)stress_splice_pipe_size(fds2[0]);
	(void)stress_splice_pipe_size(fds2[1]);
	(void)stress_splice_pipe_size(fds3[0]);
	(void)stress_splice_pipe_size(fds3[1]);
	(void)stress_splice_pipe_size(fds4[0]);
	(void)stress_splice_pipe_size(fds4[1]);

	/*
	 *  place data in fds3 for splice loop pipes
	 */
	use_splice_loop = stress_splice_non_block_write_4K(fds3[1]);

	stress_set_proc_state(args->name, STRESS_STATE_SYNC_WAIT);
	stress_sync_start_wait(args);
	stress_set_proc_state(args->name, STRESS_STATE_RUN);

	do {
		ssize_t ret;
		loff_t off_in, off_out;

		/*
		 *  Linux 5.9 dropped the ability to splice from /dev/zero to
		 *  a pipe, so fall back to writing to the pipe to at least
		 *  get some data into the pipe for subsequent splicing in
		 *  the pipeline.
		 */
		if (use_splice) {
			ret = splice(fd_in, NULL, fds1[1], NULL,
				splice_bytes, stress_splice_flag());
			if (UNLIKELY(ret < 0)) {
				if (errno == EINVAL) {
					if (stress_instance_zero(args)) {
						pr_inf("%s: using direct write to pipe and not splicing "
							"from /dev/zero as this is not supported in "
							"this kernel\n", args->name);
					}
					use_splice = false;
					continue;
				}
				break;
			}
		} else {
			ret = stress_splice_write(fds1[1], buffer,
						  buffer_len,
						  (ssize_t)splice_bytes);
			if (UNLIKELY(ret < 0))
				break;
		}

		if (LIKELY(metrics_count++ < 1000)) {
			/* fast non-metric path */
			ret = splice(fds1[0], NULL, fds2[1], NULL,
				splice_bytes, stress_splice_flag());
			if (UNLIKELY(ret < 0))
				break;

			ret = splice(fds2[0], NULL, fd_out, NULL,
				splice_bytes, stress_splice_flag());
			if (UNLIKELY(ret < 0))
				break;
		} else {
			/* slower metrics path */
			double t;

			metrics_count = 0;
			t = stress_time_now();
			ret = splice(fds1[0], NULL, fds2[1], NULL,
				splice_bytes, stress_splice_flag());
			if (UNLIKELY(ret < 0))
				break;
			duration += stress_time_now() - t;
			bytes += (double)ret;

			t = stress_time_now();
			ret = splice(fds2[0], NULL, fd_out, NULL,
				splice_bytes, stress_splice_flag());
			if (UNLIKELY(ret < 0))
				break;
			duration += stress_time_now() - t;
			bytes += (double)ret;
		}

		/* Exercise -ESPIPE errors */
		off_in = 1;
		off_out = 1;
		VOID_RET(ssize_t, splice(fds1[0], &off_in, fds1[1], &off_out,
			4096, stress_splice_flag()));

		off_out = 1;
		VOID_RET(ssize_t, splice(fd_in, NULL, fds1[1], &off_out,
			splice_bytes, stress_splice_flag()));

		off_in = 1;
		VOID_RET(ssize_t, splice(fds1[0], &off_in, fd_out, NULL,
			splice_bytes, stress_splice_flag()));

		/* Exercise no-op splice of zero size */
		VOID_RET(ssize_t, splice(fd_in, NULL, fds1[1], NULL,
			0, stress_splice_flag()));

		/* Exercise invalid splice flags */
		VOID_RET(ssize_t, splice(fd_in, NULL, fds1[1], NULL,
			1, ~0U));

		/* Exercise splicing to oneself */
		off_in = 0;
		off_out = 0;
		VOID_RET(ssize_t, splice(fds1[1], &off_in, fds1[1], &off_out,
			4096, stress_splice_flag()));

		/* Exercise splice loop from one pipe to another and back */
		stress_splice_looped_pipe(fds3, fds4, &use_splice_loop);
		stress_splice_looped_pipe(fds3, fds4, &use_splice_loop);

		stress_bogo_inc(args);
	} while (stress_continue(args));

	rc = EXIT_SUCCESS;

	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);

	rate = (duration > 0.0) ? bytes / duration : 0.0;
	stress_metrics_set(args, 0, "MB per sec splice rate",
		rate / (double)MB, STRESS_METRIC_HARMONIC_MEAN);

	(void)close(fd_out);
close_fds4:
	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);
	(void)close(fds4[0]);
	(void)close(fds4[1]);
close_fds3:
	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);
	(void)close(fds3[0]);
	(void)close(fds3[1]);
close_fds2:
	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);
	(void)close(fds2[0]);
	(void)close(fds2[1]);
close_fds1:
	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);
	(void)close(fds1[0]);
	(void)close(fds1[1]);
close_fd_in:
	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);
	(void)close(fd_in);
close_unmap:
	(void)munmap((void *)buffer, (size_t)buffer_len);
close_done:
	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);

	return rc;
}

const stressor_info_t stress_splice_info = {
	.stressor = stress_splice,
	.classifier = CLASS_PIPE_IO | CLASS_OS,
	.opts = opts,
	.help = help
};
#else
const stressor_info_t stress_splice_info = {
	.stressor = stress_unimplemented,
	.classifier = CLASS_PIPE_IO | CLASS_OS,
	.opts = opts,
	.help = help,
	.unimplemented_reason = "built without splice() system call"
};
#endif
