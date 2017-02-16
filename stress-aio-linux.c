/*
 * Copyright (C) 2013-2017 Canonical, Ltd.
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

#if defined(__linux__) &&	\
    defined(HAVE_LIB_AIO) &&	\
    defined(__NR_io_setup) &&	\
    defined(__NR_io_destroy) &&	\
    defined(__NR_io_submit) &&	\
    defined(__NR_io_getevents)

#include <libaio.h>

#endif

#define BUFFER_SZ	(4096)

static uint32_t opt_aio_linux_requests = DEFAULT_AIO_LINUX_REQUESTS;
static bool set_aio_linux_requests = false;

void stress_set_aio_linux_requests(const char *optarg)
{
	uint32_t aio_linux_requests;

	set_aio_linux_requests = true;
	aio_linux_requests = get_uint32(optarg);
	check_range("aiol-requests", aio_linux_requests,
		MIN_AIO_LINUX_REQUESTS, MAX_AIO_LINUX_REQUESTS);
	opt_aio_linux_requests = aio_linux_requests;
}

#if defined(__linux__) &&	\
    defined(HAVE_LIB_AIO) &&	\
    defined(__NR_io_setup) &&	\
    defined(__NR_io_destroy) &&	\
    defined(__NR_io_submit) &&	\
    defined(__NR_io_getevents)

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
 *  stress_aiol
 *	stress asynchronous I/O using the linux specific aio ABI
 */
int stress_aiol(const args_t *args)
{
	int fd, ret, rc = EXIT_FAILURE;
	char filename[PATH_MAX];
	io_context_t ctx = 0;

	if (!set_aio_linux_requests) {
		if (g_opt_flags & OPT_FLAGS_MAXIMIZE)
			opt_aio_linux_requests = MAX_AIO_REQUESTS;
		if (g_opt_flags & OPT_FLAGS_MINIMIZE)
			opt_aio_linux_requests = MIN_AIO_REQUESTS;
	}
	if ((opt_aio_linux_requests < MIN_AIO_REQUESTS) ||
	    (opt_aio_linux_requests > MAX_AIO_REQUESTS)) {
		pr_err("%s: iol_requests out of range", args->name);
		return EXIT_FAILURE;
	}
	ret = io_setup(opt_aio_linux_requests, &ctx);
	if (ret < 0) {
		/*
		 *  The libaio interface returns -errno in the
		 *  return value, so set errno accordingly
		 */
		errno = -ret;
		if ((errno == EAGAIN) || (errno == EACCES)) {
			pr_err("%s: io_setup failed, ran out of "
				"available events, consider increasing "
				"/proc/sys/fs/aio-max-nr, errno=%d (%s)\n",
				args->name, errno, strerror(errno));
			return EXIT_NO_RESOURCE;
		} else if (errno == ENOMEM) {
			pr_err("%s: io_setup failed, ran out of "
				"memory, errno=%d (%s)\n",
				args->name, errno, strerror(errno));
			return EXIT_NO_RESOURCE;
		} else if (errno == ENOSYS) {
			pr_err("%s: io_setup failed, no io_setup "
				"system call with this kernel, "
				"errno=%d (%s)\n",
				args->name, errno, strerror(errno));
			return EXIT_NO_RESOURCE;
		} else {
			pr_fail_err("io_setup");
			return EXIT_FAILURE;
		}
	}
	ret = stress_temp_dir_mk_args(args);
	if (ret < 0)
		return exit_status(-ret);

	(void)stress_temp_filename_args(args,
		filename, sizeof(filename), mwc32());

	(void)umask(0077);
	if ((fd = open(filename, O_CREAT | O_RDWR, S_IRUSR | S_IWUSR)) < 0) {
		rc = exit_status(errno);
		pr_fail_err("open");
		goto finish;
	}
	(void)unlink(filename);

	do {
		struct iocb cb[opt_aio_linux_requests];
		struct iocb *cbs[opt_aio_linux_requests];
		struct io_event events[opt_aio_linux_requests];
		uint8_t buffers[opt_aio_linux_requests][BUFFER_SZ];
		uint32_t i;
		long n;

		for (i = 0; i < opt_aio_linux_requests; i++)
			aio_linux_fill_buffer(i, buffers[i], BUFFER_SZ);

		memset(cb, 0, sizeof(cb));
		for (i = 0; i < opt_aio_linux_requests; i++) {
			cb[i].aio_fildes = fd;
			cb[i].aio_lio_opcode = IO_CMD_PWRITE;
			cb[i].u.c.buf = buffers[i];
			cb[i].u.c.offset = mwc16() * BUFFER_SZ;
			cb[i].u.c.nbytes = BUFFER_SZ;
			cbs[i] = &cb[i];
		}
		ret = io_submit(ctx, (long)opt_aio_linux_requests, cbs);
		if (ret < 0) {
			errno = -ret;
			if (errno == EAGAIN)
				continue;
			pr_fail_err("io_submit");
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

			ret = io_getevents(ctx, 1, n, events, timeout_ptr);
			if (ret < 0) {
				errno = -ret;
				if ((errno == EINTR) && (g_keep_stressing_flag))
					continue;
				pr_fail_err("io_getevents");
				break;
			} else {
				n -= ret;
			}
		} while ((n > 0) && g_keep_stressing_flag);
		inc_counter(args);
	} while (keep_stressing());

	rc = EXIT_SUCCESS;
	(void)close(fd);
finish:
	(void)io_destroy(ctx);
	(void)stress_temp_dir_rm_args(args);
	return rc;
}
#else
int stress_aiol(const args_t *args)
{
	return stress_not_implemented(args);
}
#endif
