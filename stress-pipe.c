/*
 * Copyright (C) 2013-2021 Canonical, Ltd.
 * Copyright (C) 2022-2026 Colin Ian King.
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
#include "core-builtin.h"
#include "core-filesystem.h"
#include "core-mmap.h"
#include "core-signal.h"

#include <sys/ioctl.h>

#define MIN_PIPE_DATA_SIZE	(8)
#define MIN_PIPE_PROCS		(1)
#define MAX_PIPE_PROCS		(64)

typedef struct stress_pipe_write {
	double duration;
	uint64_t bytes;
} stress_pipe_write_t;

static const stress_help_t help[] = {
	{ "p N", "pipe N",		"start N workers exercising pipe I/O" },
	{ NULL,	"pipe-data-size N",	"set pipe size of each pipe write to N bytes" },
	{ NULL,	"pipe-ops N",		"stop after N pipe I/O bogo operations" },
#if defined(F_SETPIPE_SZ)
	{ NULL,	"pipe-size N",		"set pipe size to N bytes" },
#endif
	{ NULL, "pipe-vmsplice",	"use vmsplice for pipe data transfer" },
	{ NULL,	NULL,			NULL }
};

#if !defined(PIPE_BUF)
#define PIPE_BUF	(4096)
#endif

static size_t pipe_get_size(const int fd)
{
#if defined(F_GETPIPE_SZ)
	ssize_t sz;

	if (UNLIKELY((sz = fcntl(fd, F_GETPIPE_SZ)) < 0))
		return PIPE_BUF;
	return sz;
#else
	(void)fd;
	return PIPE_BUF;
#endif
}

#if defined(F_SETPIPE_SZ)
/*
 *  pipe_change_size()
 *	see if we can change the pipe size
 */
static void pipe_change_size(
	stress_args_t *args,
	const int fd,
	const size_t pipe_size)
{
#if defined(F_GETPIPE_SZ)
	ssize_t sz;
#endif
	if (UNLIKELY(!pipe_size))
		return;

#if !(defined(HAVE_PIPE2) &&	\
    defined(O_DIRECT))
	if (UNLIKELY(pipe_size < args->page_size))
		return;
#endif
	if (UNLIKELY(fcntl(fd, F_SETPIPE_SZ, pipe_size) < 0)) {
		pr_err("%s: cannot set pipe size, keeping "
			"default pipe size, errno=%d (%s)\n",
			args->name, errno, strerror(errno));
	}
#if defined(F_GETPIPE_SZ)
	/* Sanity check size */
	if (UNLIKELY((sz = fcntl(fd, F_GETPIPE_SZ)) < 0)) {
		pr_err("%s: cannot get pipe size, errno=%d (%s)\n",
			args->name, errno, strerror(errno));
	} else {
		if (UNLIKELY((size_t)sz != pipe_size)) {
			pr_err("%s: cannot set desired pipe size, "
				"pipe %zu, defaulting to size %zd\n",
				args->name, pipe_size, sz);
		}
	}
#endif
}
#endif

/*
 *  stress_pipe_read_generic()
 *	generic pipe read, no verify
 */
static int stress_pipe_read_generic(
	stress_args_t *args,
	const int fd,
	char *buf,
	const size_t pipe_data_size)
{
	int rc = 0;
#if defined(FIONREAD)
	register int i = 0;
#endif
	while (stress_continue_flag()) {
		register ssize_t n;

		n = read(fd, buf, pipe_data_size);
		if (UNLIKELY(n <= 0)) {
			if (n == 0)
				break;
			if ((errno == EAGAIN) || (errno == EINTR))
				continue;
			if (errno == EPIPE)
				break;
			if (errno) {
				pr_fail("%s: read failed, errno=%d (%s)\n",
					args->name, errno, strerror(errno));
				rc = -1;
				break;
			}
			rc = -1;
			pr_fail("%s: zero bytes read\n", args->name);
			break;
		}

#if defined(FIONREAD)
		/* Occasionally exercise FIONREAD on read end */
		if (UNLIKELY((i++ & 0x1ff) == 0)) {
			int readbytes;

			VOID_RET(int, ioctl(fd, FIONREAD, &readbytes));
		}
#endif
	}
	return rc;
}

/*
 *  stress_pipe_read_generic_verify()
 *	generic pipe read, with verify
 */
static int stress_pipe_read_generic_verify(
	stress_args_t *args,
	const int fd,
	char *buf,
	const size_t pipe_data_size,
	uint32_t val)
{
	int rc = 0;
#if defined(FIONREAD)
	register int i = 0;
#endif
	register const uint32_t *const buf32 = (uint32_t *)shim_assume_aligned(buf, 4);

	while (stress_continue_flag()) {
		register ssize_t n;

		n = read(fd, buf, pipe_data_size);
		if (UNLIKELY(n <= 0)) {
			if (n == 0)
				break;
			if ((errno == EAGAIN) || (errno == EINTR))
				continue;
			if (errno == EPIPE)
				break;
			if (errno) {
				pr_fail("%s: read failed, errno=%d (%s)\n",
					args->name, errno, strerror(errno));
				rc = -1;
				break;
			}
			rc = -1;
			pr_fail("%s: zero bytes read\n", args->name);
			break;
		}

#if defined(FIONREAD)
		/* Occasionally exercise FIONREAD on read end */
		if (UNLIKELY((i++ & 0x1ff) == 0)) {
			int readbytes;

			VOID_RET(int, ioctl(fd, FIONREAD, &readbytes));
		}
#endif
		if (UNLIKELY(*buf32 != val)) {
			pr_fail("%s: pipe read error detected, "
				"failed to read expected data\n", args->name);
			rc = -1;
			break;
		}
		val++;
	}
	return rc;
}

#if defined(HAVE_VMSPLICE)
/*
 *  stress_pipe_read_splice()
 *	pipe read using vmsplice
 */
static int stress_pipe_read_splice(
	stress_args_t *args,
	const int fd,
	char *buf,
	const size_t pipe_data_size,
	const size_t buf_size)
{
	int rc = 0;
#if defined(FIONREAD)
	register int i = 0;
#endif
	struct iovec iov;
	size_t offset = 0;
	const size_t nbufs = buf_size / pipe_data_size;
	const size_t offset_end = nbufs * pipe_data_size;

	iov.iov_len = pipe_data_size;

	while (stress_continue_flag()) {
		register ssize_t n;

		iov.iov_base = buf + offset;
		offset += pipe_data_size;
		if (UNLIKELY(offset >= offset_end))
			offset = 0;
		n = vmsplice(fd, &iov, 1, 0);
		if (UNLIKELY(n <= 0)) {
			if (n == 0)
				break;
			if ((errno == EAGAIN) || (errno == EINTR))
				continue;
			if (errno == EPIPE)
				break;
			if (errno) {
				pr_fail("%s: read failed, errno=%d (%s)\n",
					args->name, errno, strerror(errno));
				rc = -1;
				break;
			}
			rc = -1;
			pr_fail("%s: zero bytes read\n", args->name);
			break;
		}

#if defined(FIONREAD)
		/* Occasionally exercise FIONREAD on read end */
		if (UNLIKELY((i++ & 0x1ff) == 0)) {
			int readbytes;

			VOID_RET(int, ioctl(fd, FIONREAD, &readbytes));
		}
#endif
	}
	return rc;
}

/*
 *  stress_pipe_read_splice_verify()
 *	pipe read using vmsplice with verify
 */
static int stress_pipe_read_splice_verify(
	stress_args_t *args,
	const int fd,
	char *buf,
	const size_t pipe_data_size,
	const size_t buf_size,
	uint32_t val)
{
	int rc = 0;
#if defined(FIONREAD)
	register int i = 0;
#endif
	uint32_t *buf32;
	struct iovec iov;
	size_t offset = 0;
	const size_t nbufs = buf_size / pipe_data_size;
	const size_t offset_end = nbufs * pipe_data_size;

	buf = (char *)shim_assume_aligned(buf, 4);

	iov.iov_len = pipe_data_size;

	while (stress_continue_flag()) {
		register ssize_t n;

		iov.iov_base = buf + offset;
		buf32 = (uint32_t *)shim_assume_aligned(iov.iov_base, 4);
		offset += pipe_data_size;
		if (UNLIKELY(offset >= offset_end))
			offset = 0;
		n = vmsplice(fd, &iov, 1, 0);
		if (UNLIKELY(n <= 0)) {
			if (n == 0)
				break;
			if ((errno == EAGAIN) || (errno == EINTR))
				continue;
			if (errno == EPIPE)
				break;
			if (errno) {
				pr_fail("%s: read failed, errno=%d (%s)\n",
					args->name, errno, strerror(errno));
				rc = -1;
				break;
			}
			rc = -1;
			pr_fail("%s: zero bytes read\n", args->name);
			break;
		}

#if defined(FIONREAD)
		/* Occasionally exercise FIONREAD on read end */
		if (UNLIKELY((i++ & 0x1ff) == 0)) {
			int readbytes;

			VOID_RET(int, ioctl(fd, FIONREAD, &readbytes));
		}
#endif
		if (UNLIKELY(*buf32 != val)) {
			pr_fail("%s: pipe read error detected, "
				"failed to read expected data\n", args->name);
			rc = -1;
			break;
		}
		val++;
	}
	return rc;
}
#endif

/*
 *  stress_pipe_write_generic()
 *	generic pipe write, no verify
 */
static int stress_pipe_write_generic(
	stress_args_t *args,
	const int fd,
	char *buf,
	const size_t pipe_data_size,
	uint64_t *pbytes64)
{
	int rc = 0;
	register uint64_t bytes = 0;

	do {
		register ssize_t ret;

		ret = write(fd, buf, pipe_data_size);
		if (UNLIKELY(ret <= 0)) {
			if ((errno == EAGAIN) || (errno == EINTR))
				continue;
			if (errno == EPIPE)
				break;
			if (errno) {
				pr_fail("%s: write failed, errno=%d (%s)\n",
					args->name, errno, strerror(errno));
				rc = -1;
				break;
			}
			continue;
		}
		stress_bogo_inc(args);
		bytes += ret;
	} while (stress_continue(args));

	*pbytes64 = bytes;
	return rc;
}

/*
 *  stress_pipe_write_generic_verify()
 *	generic pipe write with verify data
 */
static int stress_pipe_write_generic_verify(
	stress_args_t *args,
	const int fd,
	char *buf,
	const size_t pipe_data_size,
	uint64_t *pbytes64,
	uint32_t val)
{
	int rc = 0;
	register uint32_t *const buf32 = (uint32_t *)shim_assume_aligned(buf, 4);
	register uint64_t bytes = 0;

	do {
		register ssize_t ret;

		*buf32 = val++;
		ret = write(fd, buf, pipe_data_size);
		if (UNLIKELY(ret <= 0)) {
			if ((errno == EAGAIN) || (errno == EINTR))
				continue;
			if (errno == EPIPE)
				break;
			if (errno) {
				pr_fail("%s: write failed, errno=%d (%s)\n",
					args->name, errno, strerror(errno));
				rc = -1;
				break;
			}
			continue;
		}
		stress_bogo_inc(args);
		bytes += ret;
	} while (stress_continue(args));

	*pbytes64 = bytes;
	return rc;
}

#if defined(HAVE_VMSPLICE)
/*
 *  stress_pipe_write()
 *	vmsplice pipe write, no verify
 */
static int stress_pipe_write_splice(
	stress_args_t *args,
	const int fd,
	char *buf,
	const size_t pipe_data_size,
	const size_t buf_size,
	uint64_t *pbytes64)
{
	int rc = 0;
	register uint64_t bytes = 0;
	struct iovec iov;
	size_t offset = 0;
	const size_t nbufs = buf_size / pipe_data_size;
	const size_t offset_end = nbufs * pipe_data_size;

	iov.iov_len = pipe_data_size;

	do {
		register ssize_t ret;

		iov.iov_base = buf + offset;
		offset += pipe_data_size;
		if (UNLIKELY(offset >= offset_end))
			offset = 0;
		ret = vmsplice(fd, &iov, 1, 0);
		if (UNLIKELY(ret <= 0)) {
			if ((errno == EAGAIN) || (errno == EINTR))
				continue;
			if (errno == EPIPE)
				break;
			if (errno) {
				pr_fail("%s: write failed, errno=%d (%s)\n",
					args->name, errno, strerror(errno));
				rc = -1;
				break;
			}
			continue;
		} else {
			stress_bogo_inc(args);
			bytes += ret;
		}
	} while (stress_continue(args));

	*pbytes64 = bytes;
	return rc;
}

/*
 *  stress_pipe_write_splice_verify()
 *	vmsplice pipe write with verify data
 */
static int stress_pipe_write_splice_verify(
	stress_args_t *args,
	const int fd,
	char *buf,
	const size_t pipe_data_size,
	const size_t buf_size,
	uint64_t *pbytes64,
	uint32_t val)
{
	int rc = 0;
	register uint64_t bytes = 0;
	struct iovec iov;
	size_t offset = 0;
	const size_t nbufs = buf_size / pipe_data_size;
	const size_t offset_end = nbufs * pipe_data_size;

	buf = (char *)shim_assume_aligned(buf, 4);
	iov.iov_len = pipe_data_size;

	do {
		register ssize_t ret;
		uint32_t *buf32;

		iov.iov_base = buf + offset;
		buf32 = (uint32_t *)iov.iov_base;
		*buf32 = val++;
		offset += pipe_data_size;
		if (UNLIKELY(offset >= offset_end))
			offset = 0;
		ret = vmsplice(fd, &iov, 1, 0);
		if (UNLIKELY(ret <= 0)) {
			if ((errno == EAGAIN) || (errno == EINTR))
				continue;
			if (errno == EPIPE)
				break;
			if (errno) {
				pr_fail("%s: write failed, errno=%d (%s)\n",
					args->name, errno, strerror(errno));
				rc = -1;
				break;
			}
			continue;
		}
		stress_bogo_inc(args);
		bytes += pipe_data_size;
	} while (stress_continue(args));

	*pbytes64 = bytes;
	return rc;
}
#endif

/*
 *  stress_pipe
 *	stress by heavy pipe I/O
 */
static int stress_pipe(stress_args_t *args)
{
	pid_t wr_pids[MAX_PIPE_PROCS];
	pid_t rd_pids[MAX_PIPE_PROCS];

	stress_pipe_write_t *pipe_writes;
	int pipefds[2];
	int parent_cpu;
	int rc = EXIT_SUCCESS;
	int status;
	const size_t page_size = args->page_size;
	size_t pipe_data_size = 4096;
	size_t pipe_wr_size, pipe_rd_size, buf_wr_size, buf_rd_size;
	size_t pipe_readers = MIN_PIPE_PROCS;
	size_t pipe_writers = MIN_PIPE_PROCS;
	const size_t pipe_writes_size = MAX_PIPE_PROCS * sizeof(*pipe_writes);
	size_t i;
	char *buf_rd, *buf_wr;
	const uint32_t val = stress_mwc32();
	double total_duration;
	double total_bytes;
	double rate;
	bool verify = !!(g_opt_flags & OPT_FLAGS_VERIFY);
	bool pipe_vmsplice = false;

	(void)memset(wr_pids, 0, sizeof(wr_pids));
	(void)memset(rd_pids, 0, sizeof(rd_pids));

	(void)stress_setting_get("pipe-vmsplice", &pipe_vmsplice);
	if (!stress_setting_get("pipe-data-size", &pipe_data_size)) {
		if (g_opt_flags & OPT_FLAGS_MAXIMIZE)
			pipe_data_size = stress_memory_page_size_get();
		if (g_opt_flags & OPT_FLAGS_MINIMIZE)
			pipe_data_size = MIN_PIPE_DATA_SIZE;
	}
	if (!stress_setting_get("pipe-readers", &pipe_readers)) {
		if (g_opt_flags & OPT_FLAGS_MAXIMIZE)
			pipe_readers = MAX_PIPE_PROCS;
		if (g_opt_flags & OPT_FLAGS_MINIMIZE)
			pipe_readers = MIN_PIPE_PROCS;
	}
	if (!stress_setting_get("pipe-writers", &pipe_writers)) {
		if (g_opt_flags & OPT_FLAGS_MAXIMIZE)
			pipe_writers = MAX_PIPE_PROCS;
		if (g_opt_flags & OPT_FLAGS_MINIMIZE)
			pipe_writers = MIN_PIPE_PROCS;
	}

	if (verify && ((pipe_readers > 1) || (pipe_writers > 1))) {
		verify = false;
		if (stress_instance_zero(args))
			pr_inf("%s: more than 1 reader and/or 1 writer, verify disabled\n", args->name);
	}

	pipe_writes = stress_mmap_populate(NULL, pipe_writes_size, PROT_READ | PROT_WRITE,
				MAP_SHARED | MAP_ANONYMOUS, -1, 0);
	if (pipe_writes == MAP_FAILED) {
		pr_inf_skip("%s: failed to mmap %zu bytes, skipping stressor\n",
			args->name, pipe_writes_size);
		rc = EXIT_FAILURE;
		goto finish;
	}
	for (i = 0; i < MAX_PIPE_PROCS; i++) {
		pipe_writes[i].duration = 0.0;
		pipe_writes[i].bytes = 0ULL;
	}

	if (stress_signal_stop_stressing(args->name, SIGPIPE) < 0) {
		rc = EXIT_FAILURE;
		goto unmap_pipe_writes;
	}

	(void)shim_memset(pipefds, 0, sizeof(pipefds));
#if defined(HAVE_PIPE2) &&	\
    defined(O_DIRECT)
	if (pipe2(pipefds, O_DIRECT) < 0) {
		/*
		 *  Failed, fall back to standard pipe
		 */
		if (pipe(pipefds) < 0) {
			pr_fail("%s: pipe failed, errno=%d (%s)\n",
				args->name, errno, strerror(errno));
			rc = EXIT_FAILURE;
			goto unmap_pipe_writes;
		}
	}
#else
	if (pipe(pipefds) < 0) {
		pr_fail("%s: pipe failed, errno=%d (%s)\n",
			args->name, errno, strerror(errno));
		rc = EXIT_FAILURE;
		goto unmap_pipe_writes;
	}
	if (pipe_vmsplice) {
		pipe_vmsplice = false;
		if (stress_instance_zero(args))
			pr_inf("%s: no pipe packet mode without O_DIRECT, disabled pipe vmsplicing\n", args->name);
	}
#endif

#if defined(F_SETPIPE_SZ)
	{
		size_t pipe_size = 0;

		if (!stress_setting_get("pipe-size", &pipe_size)) {
			if (g_opt_flags & OPT_FLAGS_MAXIMIZE)
				pipe_size = stress_fs_max_pipe_size_get();
			if (g_opt_flags & OPT_FLAGS_MINIMIZE)
				pipe_size = stress_memory_page_size_get();
		}
		if (pipe_size > 0) {
			pipe_change_size(args, pipefds[0], pipe_size);
			pipe_change_size(args, pipefds[1], pipe_size);
		}
	}
#else
	UNEXPECTED
#endif
	pipe_rd_size = pipe_get_size(pipefds[0]);
	pipe_wr_size = pipe_get_size(pipefds[1]);

	if (stress_instance_zero(args)) {
		pr_dbg("%s: readers %zu, writers %zu, pipe read/write size %zuK\n",
			args->name, pipe_readers, pipe_writers, pipe_data_size >> 10);
	}

	/* round to nearest whole page */
	buf_rd_size = ((pipe_rd_size + page_size - 1) & ~(page_size - 1)) * 2;
	buf_rd = (char *)stress_mmap_populate(NULL, buf_rd_size,
				PROT_READ | PROT_WRITE,
				MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
	if (buf_rd == MAP_FAILED) {
		pr_inf_skip("%s: failed to mmap %zu byte read buffer%s, "
			"errno=%d (%s), skipping stressor\n",
			args->name, buf_rd_size,
			stress_memory_free_get(), errno, strerror(errno));
		(void)close(pipefds[0]);
		(void)close(pipefds[1]);
		return EXIT_NO_RESOURCE;
	}
	stress_memory_anon_name_set(buf_rd, buf_rd_size, "read-buffer");

	/* round to nearest whole page */
	buf_wr_size = ((pipe_wr_size + page_size - 1) & ~(page_size - 1)) * 2;
	buf_wr = (char *)stress_mmap_populate(NULL, buf_wr_size,
				PROT_READ | PROT_WRITE,
				MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
	if (buf_wr == MAP_FAILED) {
		pr_inf_skip("%s: failed to mmap %zu byte write buffer%s, "
			"errno=%d (%s), skipping stressor\n",
			args->name, buf_wr_size,
			stress_memory_free_get(), errno, strerror(errno));
		(void)munmap((void *)buf_rd, buf_rd_size);
		(void)close(pipefds[0]);
		(void)close(pipefds[1]);
		return EXIT_NO_RESOURCE;
	}
	stress_memory_anon_name_set(buf_wr, buf_wr_size, "write-buffer");
	stress_rndbuf(buf_wr, buf_wr_size);

	stress_proc_state_set(args->name, STRESS_STATE_SYNC_WAIT);
	stress_sync_start_wait(args);
	stress_proc_state_set(args->name, STRESS_STATE_RUN);
	parent_cpu = stress_cpu_get();

	for (i = 0; i < pipe_readers; i++) {
fork_rd_again:
		rd_pids[i] = fork();
		if (rd_pids[i] < 0) {
			if (stress_redo_fork(args, errno))
				goto fork_rd_again;
			(void)close(pipefds[0]);
			(void)close(pipefds[1]);
			(void)munmap((void *)buf_wr, buf_wr_size);
			(void)munmap((void *)buf_rd, buf_rd_size);
			if (UNLIKELY(!stress_continue(args)))
				goto finish;
			pr_fail("%s: fork failed, errno=%d (%s)\n",
				args->name, errno, strerror(errno));
			rc = EXIT_FAILURE;
			goto finish;
		} else if (rd_pids[i] == 0) {
			int ret;

			stress_proc_state_set(args->name, STRESS_STATE_RUN);
			stress_make_it_fail_set();
			stress_parent_died_alarm();
			(void)stress_sched_settings_apply(true);
			(void)stress_affinity_change_cpu(args, parent_cpu);

			(void)close(pipefds[1]);
#if defined(HAVE_VMSPLICE)
			if (pipe_vmsplice) {
				ret = verify ?
					stress_pipe_read_splice_verify(args, pipefds[0], buf_rd, pipe_data_size, buf_rd_size, val) :
					stress_pipe_read_splice(args, pipefds[0], buf_rd, pipe_data_size, buf_rd_size);

			} else {
#endif
				ret = verify ?
					stress_pipe_read_generic_verify(args, pipefds[0], buf_rd, pipe_data_size, val) :
					stress_pipe_read_generic(args, pipefds[0], buf_rd, pipe_data_size);
#if defined(HAVE_VMSPLICE)
			}
#endif
			(void)close(pipefds[0]);
			(void)munmap((void *)buf_wr, buf_wr_size);
			(void)munmap((void *)buf_rd, buf_rd_size);
			_exit((ret < 0) ? EXIT_FAILURE : EXIT_SUCCESS);
		}
	}

	for (i = 0; i < pipe_writers; i++) {
fork_wr_again:
		wr_pids[i] = fork();
		if (wr_pids[i] < 0) {
			if (stress_redo_fork(args, errno))
				goto fork_wr_again;
			(void)close(pipefds[0]);
			(void)close(pipefds[1]);
			(void)munmap((void *)buf_wr, buf_wr_size);
			(void)munmap((void *)buf_rd, buf_rd_size);
			if (UNLIKELY(!stress_continue(args)))
				goto finish;
			pr_fail("%s: fork failed, errno=%d (%s)\n",
				args->name, errno, strerror(errno));
			rc = EXIT_FAILURE;
			goto finish;
		} else if (wr_pids[i] == 0) {
			double t;
			int ret;
			uint64_t bytes = 0ULL;

			(void)close(pipefds[0]);
			t = stress_time_now();
#if defined(HAVE_VMSPLICE)
			if (pipe_vmsplice) {
				ret = verify ?
					stress_pipe_write_splice_verify(args, pipefds[1], buf_wr, pipe_data_size, buf_wr_size, &bytes, val) :
					stress_pipe_write_splice(args, pipefds[1], buf_wr, pipe_data_size, buf_wr_size, &bytes);
			} else {
#endif
				ret = verify ?
					stress_pipe_write_generic_verify(args, pipefds[1], buf_wr, pipe_data_size, &bytes, val) :
					stress_pipe_write_generic(args, pipefds[1], buf_wr, pipe_data_size, &bytes);
#if defined(HAVE_VMSPLICE)
			}
#endif
			(void)close(pipefds[0]);
			(void)munmap((void *)buf_wr, buf_wr_size);
			(void)munmap((void *)buf_rd, buf_rd_size);

			pipe_writes[i].duration = stress_time_now() - t;
			pipe_writes[i].bytes = bytes;

			_exit((ret < 0) ? EXIT_FAILURE : EXIT_SUCCESS);
		}

	}

	do {
		(void)sleep(1);
	} while (stress_continue(args));

	for (i = 0; i < pipe_readers; i++) {
		if (rd_pids[i] > 1)
			(void)shim_kill(rd_pids[i], SIGPIPE);
	}
	for (i = 0; i < pipe_writers; i++) {
		if (wr_pids[i] > 1)
			(void)shim_kill(wr_pids[i], SIGPIPE);
	}
	for (i = 0; i < pipe_readers; i++) {
		if (rd_pids[i] > 1) {
			(void)shim_waitpid(rd_pids[i], &status, 0);
			if (WIFEXITED(status))
				if (WEXITSTATUS(status) != EXIT_SUCCESS)
					rc = WEXITSTATUS(status);
		}
	}
	for (i = 0; i < pipe_readers; i++) {
		if (wr_pids[i] > 1) {
			(void)shim_waitpid(wr_pids[i], &status, 0);
			if (WIFEXITED(status))
				if (WEXITSTATUS(status) != EXIT_SUCCESS)
					rc = WEXITSTATUS(status);
		}
	}

	total_duration = 0.0;
	total_bytes = 0.0;

	for (i = 0; i < pipe_writers; i++) {
		total_duration += pipe_writes[i].duration;
		total_bytes += (double)pipe_writes[i].bytes;
	}

	rate = (total_duration > 0.0) ? (total_bytes / total_duration) / (double)MB : 0.0;
	stress_metrics_set(args, "MB per sec pipe write rate", rate, STRESS_METRIC_HARMONIC_MEAN);

	(void)munmap((void *)buf_wr, buf_wr_size);
	(void)munmap((void *)buf_rd, buf_rd_size);

unmap_pipe_writes:
	(void)munmap((void *)pipe_writes, pipe_writes_size);

finish:
	stress_proc_state_set(args->name, STRESS_STATE_DEINIT);

	return rc;
}

#if defined(F_SETPIPE_SZ)
/*
 *   stress_pipe_size
 *   	parse --pipe-size, check for variable min/max sizes
 */
static void stress_pipe_size(const char *opt_name, const char *opt_arg, stress_type_id_t *type_id, void *value)
{
	const size_t min_pipe_size = stress_memory_page_size_get();
	const size_t max_pipe_size = stress_fs_max_pipe_size_get();
	const size_t pipe_size = (size_t)stress_get_uint64_byte_memory(opt_arg, 1);

	stress_check_range(opt_name, (uint64_t)pipe_size, min_pipe_size, max_pipe_size);
        *type_id = TYPE_ID_SIZE_T_BYTES_VM;
	*(size_t *)value = (size_t)pipe_size;
}
#endif

/*
 *   stress_pipe_data_size
 *   	parse --pipe-data-size, check for variable min/max sizes
 */
static void stress_pipe_data_size(const char *opt_name, const char *opt_arg, stress_type_id_t *type_id, void *value)
{
	const size_t min_pipe_size = MIN_PIPE_DATA_SIZE;
	const size_t max_pipe_size = stress_memory_page_size_get();
	const size_t pipe_size = (size_t)stress_get_uint64_byte_memory(opt_arg, 1);

	stress_check_range(opt_name, (uint64_t)pipe_size, min_pipe_size, max_pipe_size);
        *type_id = TYPE_ID_SIZE_T_BYTES_VM;
	*(size_t *)value = (size_t)pipe_size;
}

static const stress_opt_t opts[] = {
	{ OPT_pipe_data_size, "pipe-data-size", TYPE_ID_CALLBACK, 0, 0, stress_pipe_data_size },
	{ OPT_pipe_readers,   "pipe-readers",   TYPE_ID_SIZE_T, MIN_PIPE_PROCS, MAX_PIPE_PROCS, NULL },
#if defined(F_SETPIPE_SZ)
	{ OPT_pipe_size,      "pipe-size",      TYPE_ID_CALLBACK, 0, 0, stress_pipe_size },
#endif
	{ OPT_pipe_vmsplice,  "pipe-vmsplice",  TYPE_ID_BOOL, 0, 1, NULL },
	{ OPT_pipe_writers,   "pipe-writers",   TYPE_ID_SIZE_T, MIN_PIPE_PROCS, MAX_PIPE_PROCS, NULL },
	END_OPT,
};

static const stress_exercises_t exercises[] = {
	STRESS_EX_SYSCALL("fcntl"),
	STRESS_EX_SYSCALL("pipe"),
	STRESS_EX_SYSCALL("read"),
	STRESS_EX_SYSCALL("write"),
	STRESS_EX_END,
};

const stressor_info_t stress_pipe_info = {
	.stressor = stress_pipe,
	.classifier = CLASS_PIPE_IO | CLASS_MEMORY | CLASS_OS | CLASS_IPC,
	.opts = opts,
	.verify = VERIFY_OPTIONAL,
	.help = help,
	.exercises = exercises,
};
