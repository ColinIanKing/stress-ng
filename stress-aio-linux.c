/*
 * Copyright (C) 2013-2015 Canonical, Ltd.
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
#define _GNU_SOURCE

#include "stress-ng.h"

#if defined(STRESS_AIO_LINUX)

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <aio.h>
#include <fcntl.h>
#include <linux/aio_abi.h>

#define BUFFER_SZ	(4096)

static int opt_aio_linux_requests = DEFAULT_AIO_LINUX_REQUESTS;
static bool set_aio_linux_requests = false;

static inline int sys_io_setup(unsigned nr_events, aio_context_t *ctx_idp)
{
#if defined(__NR_io_setup)
	return syscall(__NR_io_setup, nr_events, ctx_idp);
#else
	errno = ENOSYS;
	return -1;
#endif
}

static inline int sys_io_destroy(aio_context_t ctx_id)
{
#if defined(__NR_io_destroy)
	return syscall(__NR_io_destroy, ctx_id);
#else
	errno = ENOSYS;
	return -1;
#endif
}

static inline int sys_io_submit(aio_context_t ctx_id, long nr, struct iocb **iocbpp)
{
#if defined(__NR_io_submit)
	return syscall(__NR_io_submit, ctx_id, nr, iocbpp);
#else
	errno = ENOSYS;
	return -1;
#endif
}

static inline int sys_io_getevents(aio_context_t ctx_id, long min_nr, long max_nr,
	struct io_event *events, struct timespec *timeout)
{
#if defined(__NR_io_getevents)
	return syscall(__NR_io_getevents, ctx_id, min_nr, max_nr, events, timeout);
#else
	errno = ENOSYS;
	return -1;
#endif
}

void stress_set_aio_linux_requests(const char *optarg)
{
	uint64_t aio_linux_requests;

	set_aio_linux_requests = true;
	aio_linux_requests = get_uint64(optarg);
	check_range("aioabi-requests", aio_linux_requests,
		MIN_AIO_LINUX_REQUESTS, MAX_AIO_LINUX_REQUESTS);
	opt_aio_linux_requests = (int)aio_linux_requests;
}

/*
 *  aio_linux_fill_buffer()
 *	fill buffer with some known pattern
 */
static inline void aio_linux_fill_buffer(
	const int request,
	uint8_t *const buffer,
	const size_t size)
{
	register size_t i;

	for (i = 0; i < size; i++)
		buffer[i] = (uint8_t)(request + i);
}

/*
 *  stress_aio_linux
 *	stress asynchronous I/O using the linux specific aio ABI
 */
int stress_aio_linux(
	uint64_t *const counter,
	const uint32_t instance,
	const uint64_t max_ops,
	const char *name)
{
	int fd, rc = EXIT_FAILURE;
	char filename[PATH_MAX];
	const pid_t pid = getpid();
	aio_context_t ctx = 0;

	if (!set_aio_linux_requests) {
		if (opt_flags & OPT_FLAGS_MAXIMIZE)
			opt_aio_linux_requests = MAX_AIO_REQUESTS;
		if (opt_flags & OPT_FLAGS_MINIMIZE)
			opt_aio_linux_requests = MIN_AIO_REQUESTS;
	}
	if (sys_io_setup(opt_aio_linux_requests, &ctx) < 0) {
		pr_failed_err(name, "io_setup");
		return EXIT_FAILURE;
	}
	if (stress_temp_dir_mk(name, pid, instance) < 0) {
		return EXIT_FAILURE;
	}
	(void)stress_temp_filename(filename, sizeof(filename),
		name, pid, instance, mwc32());

	(void)umask(0077);
	if ((fd = open(filename, O_CREAT | O_RDWR, S_IRUSR | S_IWUSR)) < 0) {
		pr_failed_err(name, "open");
		goto finish;
	}
	(void)unlink(filename);

	do {
		struct iocb cb[opt_aio_linux_requests];
		struct iocb *cbs[opt_aio_linux_requests];
		struct io_event events[opt_aio_linux_requests];
		uint8_t buffers[opt_aio_linux_requests][BUFFER_SZ];
		int ret, i;
		long n;

		for (i = 0; i < opt_aio_linux_requests; i++)
			aio_linux_fill_buffer(i, buffers[i], BUFFER_SZ);

		memset(cb, 0, sizeof(cb));
		for (i = 0; i < opt_aio_linux_requests; i++) {
			cb[i].aio_fildes = fd;
			cb[i].aio_lio_opcode = IOCB_CMD_PWRITE;
			cb[i].aio_buf = (long)buffers[i];
			cb[i].aio_offset = mwc16() * BUFFER_SZ;
			cb[i].aio_nbytes = BUFFER_SZ;
			cbs[i] = &cb[i];
		}
		ret = sys_io_submit(ctx, opt_aio_linux_requests, cbs);
		if (ret < 0) {
			if (errno == EAGAIN)
				continue;
			pr_failed_err(name, "io_submit");
			break;
		}

		n = opt_aio_linux_requests;
		do {
			struct timespec timeout, *timeout_ptr;

			if (clock_gettime(CLOCK_REALTIME, &timeout) < 0) {
				timeout_ptr = NULL;
			} else {
				timeout.tv_nsec += 1000000;
				if (timeout.tv_nsec > 1000000000) {
					timeout.tv_nsec -= 1000000000;
					timeout.tv_sec++;
				}
				timeout_ptr = &timeout;
			}

			ret = sys_io_getevents(ctx, 1, n, events, timeout_ptr);
			if (ret < 0) {
				if ((errno == EINTR) && (opt_do_run))
					continue;
				pr_failed_err(name, "io_getevents");
				break;
			} else {
				n -= ret;
			}
		} while ((n > 0) && opt_do_run);
		(*counter)++;
	} while (opt_do_run && (!max_ops || *counter < max_ops));

	rc = EXIT_SUCCESS;
	(void)close(fd);
finish:
	(void)sys_io_destroy(ctx);
	(void)stress_temp_dir_rm(name, pid, instance);
	return rc;
}

#endif
