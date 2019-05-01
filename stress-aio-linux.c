/*
 * Copyright (C) 2013-2019 Canonical, Ltd.
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

#define BUFFER_SZ		(4096)
#define DEFAULT_AIO_MAX_NR	(65536)

static const help_t help[] = {
	{ NULL,	"aiol N",	   "start N workers that exercise Linux async I/O" },
	{ NULL,	"aiol-ops N",	   "stop after N bogo Linux aio async I/O requests" },
	{ NULL,	"aiol-requests N", "number of Linux aio async I/O requests per worker" },
	{ NULL,	NULL,		   NULL }
};

static int stress_set_aio_linux_requests(const char *opt)
{
	uint64_t aio_linux_requests;

	aio_linux_requests = get_uint32(opt);
	check_range("aiol-requests", aio_linux_requests,
		MIN_AIO_LINUX_REQUESTS, MAX_AIO_LINUX_REQUESTS);
	return set_setting("aiol-requests", TYPE_ID_UINT64, &aio_linux_requests);
}

static const opt_set_func_t opt_set_funcs[] = {
	{ OPT_aiol_requests,	stress_set_aio_linux_requests },
	{ 0,			NULL }
};

#if defined(HAVE_LIB_AIO) &&		\
    defined(HAVE_LIBAIO_H) &&		\
    defined(HAVE_CLOCK_GETTIME) &&	\
    defined(__NR_io_setup) &&		\
    defined(__NR_io_destroy) &&		\
    defined(__NR_io_submit) &&		\
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
static int stress_aiol(const args_t *args)
{
	int fd, ret, rc = EXIT_FAILURE;
	char filename[PATH_MAX];
	char buf[64];
	io_context_t ctx = 0;
	uint64_t aio_linux_requests = DEFAULT_AIO_LINUX_REQUESTS;
	uint8_t *buffer;
	uint64_t aio_max_nr = DEFAULT_AIO_MAX_NR;

	if (!get_setting("aiol-requests", &aio_linux_requests)) {
		if (g_opt_flags & OPT_FLAGS_MAXIMIZE)
			aio_linux_requests = MAX_AIO_REQUESTS;
		if (g_opt_flags & OPT_FLAGS_MINIMIZE)
			aio_linux_requests = MIN_AIO_REQUESTS;
	}
	if ((aio_linux_requests < MIN_AIO_REQUESTS) ||
	    (aio_linux_requests > MAX_AIO_REQUESTS)) {
		pr_err("%s: iol_requests out of range", args->name);
		return EXIT_FAILURE;
	}

	ret = system_read("/proc/sys/fs/aio-max-nr", buf, sizeof(buf));
	if (ret > 0) {
		if (sscanf(buf, "%" SCNu64, &aio_max_nr) != 1) {
			/* Guess max */
			aio_max_nr = DEFAULT_AIO_MAX_NR;
		}
	} else {
		/* Guess max */
		aio_max_nr = DEFAULT_AIO_MAX_NR;
	}

	aio_max_nr /= (args->num_instances == 0) ? 1 : args->num_instances;

	if (aio_max_nr < 1)
		aio_max_nr = 1;
	if (aio_linux_requests > aio_max_nr) {
		aio_linux_requests = aio_max_nr;
		if (args->instance == 0)
			pr_inf("%s: Limiting AIO requests to "
				"%" PRIu64 " per stressor (avoids running out of resources)\n",
				args->name, aio_linux_requests);
	}

	ret = posix_memalign((void **)&buffer, 4096,
		aio_linux_requests * BUFFER_SZ);
	if (ret) {
		pr_inf("%s: Out of memory allocating buffers, errno=%d (%s)",
			args->name, errno, strerror(errno));
		return EXIT_NO_RESOURCE;
	}
	ret = io_setup(aio_linux_requests, &ctx);
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
			rc = EXIT_NO_RESOURCE;
			goto free_buffer;
		} else if (errno == ENOMEM) {
			pr_err("%s: io_setup failed, ran out of "
				"memory, errno=%d (%s)\n",
				args->name, errno, strerror(errno));
			rc = EXIT_NO_RESOURCE;
			goto free_buffer;
		} else if (errno == ENOSYS) {
			pr_err("%s: io_setup failed, no io_setup "
				"system call with this kernel, "
				"errno=%d (%s)\n",
				args->name, errno, strerror(errno));
			rc = EXIT_NO_RESOURCE;
			goto free_buffer;
		} else {
			pr_fail_err("io_setup");
			rc = EXIT_FAILURE;
			goto free_buffer;
		}
	}
	ret = stress_temp_dir_mk_args(args);
	if (ret < 0) {
		rc = exit_status(-ret);
		goto free_buffer;
	}

	(void)stress_temp_filename_args(args,
		filename, sizeof(filename), mwc32());

	if ((fd = open(filename, O_CREAT | O_RDWR | O_DIRECT, S_IRUSR | S_IWUSR)) < 0) {
		rc = exit_status(errno);
		pr_fail_err("open");
		goto finish;
	}
	(void)unlink(filename);

	do {
		struct iocb cb[aio_linux_requests];
		struct iocb *cbs[aio_linux_requests];
		struct io_event events[aio_linux_requests];
		uint8_t *buffers[aio_linux_requests];
		uint8_t *bufptr = buffer;
		uint64_t i;
		long n;

		for (i = 0; i < aio_linux_requests; i++, bufptr += BUFFER_SZ) {
			buffers[i] = bufptr;
			aio_linux_fill_buffer(i, buffers[i], BUFFER_SZ);
		}

		(void)memset(cb, 0, sizeof(cb));
		for (i = 0; i < aio_linux_requests; i++) {
			cb[i].aio_fildes = fd;
			cb[i].aio_lio_opcode = IO_CMD_PWRITE;
			cb[i].u.c.buf = buffers[i];
			cb[i].u.c.offset = mwc16() * BUFFER_SZ;
			cb[i].u.c.nbytes = BUFFER_SZ;
			cbs[i] = &cb[i];
		}
		ret = io_submit(ctx, (long)aio_linux_requests, cbs);
		if (ret < 0) {
			errno = -ret;
			if (errno == EAGAIN)
				continue;
			pr_fail_err("io_submit");
			break;
		}

		n = aio_linux_requests;
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
				if (errno == EINTR) {
					if (g_keep_stressing_flag)
						continue;
					else
						break;
				}
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

free_buffer:
	free(buffer);
	return rc;
}

stressor_info_t stress_aiol_info = {
	.stressor = stress_aiol,
	.class = CLASS_IO | CLASS_INTERRUPT | CLASS_OS,
	.opt_set_funcs = opt_set_funcs,
	.help = help
};
#else
stressor_info_t stress_aiol_info = {
	.stressor = stress_not_implemented,
	.class = CLASS_IO | CLASS_INTERRUPT | CLASS_OS,
	.opt_set_funcs = opt_set_funcs,
	.help = help
};
#endif
