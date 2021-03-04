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

#define BUFFER_SZ		(4096)
#define DEFAULT_AIO_MAX_NR	(65536)

static const stress_help_t help[] = {
	{ NULL,	"aiol N",	   "start N workers that exercise Linux async I/O" },
	{ NULL,	"aiol-ops N",	   "stop after N bogo Linux aio async I/O requests" },
	{ NULL,	"aiol-requests N", "number of Linux aio async I/O requests per worker" },
	{ NULL,	NULL,		   NULL }
};

static int stress_set_aio_linux_requests(const char *opt)
{
	size_t aio_linux_requests;

	aio_linux_requests = stress_get_uint32(opt);
	stress_check_range("aiol-requests", aio_linux_requests,
		MIN_AIO_LINUX_REQUESTS, MAX_AIO_LINUX_REQUESTS);
	return stress_set_setting("aiol-requests", TYPE_ID_SIZE_T, &aio_linux_requests);
}

static const stress_opt_set_func_t opt_set_funcs[] = {
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

#if defined(__NR_io_cancel)
static int shim_io_cancel(
	io_context_t ctx_id,
	struct iocb *iocb,
	struct io_event *result)
{
	return syscall(__NR_io_cancel, ctx_id, iocb, result);
}
#endif

static int shim_io_setup(unsigned nr_events, io_context_t *ctx_idp)
{
	return syscall(__NR_io_setup, nr_events, ctx_idp);
}

static int shim_io_destroy(io_context_t ctx_id)
{
	return syscall(__NR_io_destroy, ctx_id);
}

static int shim_io_submit(io_context_t ctx_id, long nr, struct iocb **iocbpp)
{
	return syscall(__NR_io_submit, ctx_id, nr, iocbpp);
}

static int shim_io_getevents(
	io_context_t ctx_id,
	long min_nr,
	long nr,
	struct io_event *events,
	struct timespec *timeout)
{
	return syscall(__NR_io_getevents, ctx_id, min_nr, nr, events, timeout);
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
 *  aio_linux_check_buffer()
 *	check buffer contains some known pattern
 */
static inline bool aio_linux_check_buffer(
	const int request,
	uint8_t *const buffer,
	const size_t size)
{
	register size_t i;

	for (i = 0; i < size; i++) {
		if (buffer[i] != (uint8_t)(request + i))
			return false;
	}

	return true;
}

/*
 *  stress_aiol_submit()
 *	submit async I/O requests
 */
static int stress_aiol_submit(
	const stress_args_t *args,
	const io_context_t ctx,
	struct iocb *cbs[],
	const size_t n,
	const bool ignore_einval)
{
	do {
		int ret;

		ret = shim_io_submit(ctx, (long)n, cbs);
		if (ret >= 0) {
			break;
		} else {
			if ((errno == EINVAL) && ignore_einval)
				return 0;
			if (errno != EAGAIN) {
				pr_fail("%s: io_submit failed, errno=%d (%s)\n",
					args->name, errno, strerror(errno));
				return ret;
			}
		}
	} while (keep_stressing(args));

	return 0;
}

/*
 *  stress_aiol_wait()
 *	wait for async I/O requests to complete
 */
static int stress_aiol_wait(
	const stress_args_t *args,
	const io_context_t ctx,
	struct io_event events[],
	size_t n)
{
	size_t i = 0;

	do {
		struct timespec timeout, *timeout_ptr;
		int ret;

		if (clock_gettime(CLOCK_REALTIME, &timeout) < 0) {
			timeout_ptr = NULL;
		} else {
			timeout.tv_nsec += 1000000;
			if (timeout.tv_nsec > STRESS_NANOSECOND) {
				timeout.tv_nsec -= STRESS_NANOSECOND;
				timeout.tv_sec++;
			}
			timeout_ptr = &timeout;
		}

		ret = shim_io_getevents(ctx, 1, n - i, events, timeout_ptr);
		if (ret < 0) {
			if (errno == EINTR) {
				if (keep_stressing_flag()) {
					continue;
				} else {
					return i;
				}
			}
			pr_fail("%s: io_getevents failed, errno=%d (%s)\n",
				args->name, errno, strerror(errno));
			return -1;
		} else {
			i += ret;
		}
	} while ((i < n) && keep_stressing_flag());

	return i;
}

/*
 *  stress_aiol_alloc()
 *	allocate various arrays and handle free'ing
 *	and error reporting on out of memory errors.
 */
static int stress_aiol_alloc(
	const stress_args_t *args,
	const size_t n,
	uint8_t **buffer,
	struct iocb **cb,
	struct io_event **events,
	struct iocb ***cbs,
	int **fds)
{
	int ret;

	ret = posix_memalign((void **)buffer, 4096, n * BUFFER_SZ);
	if (ret)
		goto err_msg;
	*cb = calloc(n, sizeof(**cb));
	if (!*cb)
		goto free_buffer;
	*events = calloc(n, sizeof(**events));
	if (!*events)
		goto free_cb;
	*cbs = calloc(n, sizeof(***cbs));
	if (!*cbs)
		goto free_events;
	*fds = calloc(n, sizeof(**fds));
	if (!*fds)
		goto free_cbs;
	return 0;

free_cbs:
	free(*cbs);
free_events:
	free(*events);
free_cb:
	free(*cb);
free_buffer:
	free(*buffer);
err_msg:
	pr_inf("%s: out of memory allocating memory, errno=%d (%s)\n",
		args->name, errno, strerror(errno));

	*buffer = NULL;
	*cb = NULL;
	*events = NULL;
	*cbs = NULL;

	return -1;
}

/*
 *  stress_aiol_free()
 *	free allocated memory
 */
static void stress_aiol_free(
	uint8_t *buffer,
	struct iocb *cb,
	struct io_event *events,
	struct iocb **cbs,
	int *fds)
{
	free(buffer);
	free(cb);
	free(events);
	free(cbs);
	free(fds);
}

/*
 *  stress_aiol
 *	stress asynchronous I/O using the linux specific aio ABI
 */
static int stress_aiol(const stress_args_t *args)
{
	int ret, rc = EXIT_FAILURE;
	char filename[PATH_MAX];
	char buf[1];
	io_context_t ctx = 0;
	size_t aio_linux_requests = DEFAULT_AIO_LINUX_REQUESTS;
	uint8_t *buffer;
	struct iocb *cb;
	struct io_event *events;
	struct iocb **cbs;
	int *fds;
	size_t aio_max_nr = DEFAULT_AIO_MAX_NR;
	uint16_t j;
	size_t i;
	int warnings = 0;

	if (!stress_get_setting("aiol-requests", &aio_linux_requests)) {
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
		if (sscanf(buf, "%zu", &aio_max_nr) != 1) {
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
				"%zu per stressor (avoids running out of resources)\n",
				args->name, aio_linux_requests);
	}

	if (stress_aiol_alloc(args, aio_linux_requests, &buffer, &cb, &events, &cbs, &fds)) {
		stress_aiol_free(buffer, cb, events, cbs, fds);
		return EXIT_NO_RESOURCE;
	}

	/*
	 * Exercise invalid io_setup syscall
	 * on invalid(zero) nr_events
	 */
	ret = shim_io_setup(0, &ctx);
	if (ret >= 0)
		(void)shim_io_destroy(ctx);

	ret = shim_io_setup(aio_linux_requests, &ctx);
	if (ret < 0) {
		/*
		 *  The libaio interface returns -errno in the
		 *  return value, so set errno accordingly
		 */
		if ((errno == EAGAIN) || (errno == EACCES)) {
			pr_err("%s: io_setup failed, ran out of "
				"available events, consider increasing "
				"/proc/sys/fs/aio-max-nr, errno=%d (%s)\n",
				args->name, errno, strerror(errno));
			rc = EXIT_NO_RESOURCE;
			goto free_memory;
		} else if (errno == ENOMEM) {
			pr_err("%s: io_setup failed, ran out of "
				"memory, errno=%d (%s)\n",
				args->name, errno, strerror(errno));
			rc = EXIT_NO_RESOURCE;
			goto free_memory;
		} else if (errno == ENOSYS) {
			pr_err("%s: io_setup failed, no io_setup "
				"system call with this kernel, "
				"errno=%d (%s)\n",
				args->name, errno, strerror(errno));
			rc = EXIT_NO_RESOURCE;
			goto free_memory;
		} else {
			pr_fail("%s: io_setup failed, errno=%d (%s)\n",
				args->name, errno, strerror(errno));
			rc = EXIT_FAILURE;
			goto free_memory;
		}
	}
	ret = stress_temp_dir_mk_args(args);
	if (ret < 0) {
		rc = exit_status(-ret);
		goto free_memory;
	}

	(void)stress_temp_filename_args(args,
		filename, sizeof(filename), stress_mwc32());

	fds[0] = open(filename, O_CREAT | O_RDWR | O_DIRECT, S_IRUSR | S_IWUSR);
	if (fds[0] < 0) {
		rc = exit_status(errno);
		pr_fail("%s: open %s failed, errno=%d (%s)\n",
			args->name, filename, errno, strerror(errno));
		(void)unlink(filename);
		goto finish;
	}

	/*
	 *  Make aio work harder by using lots of different fds on the
	 *  same file. If we can't open a file (e.g. out of file descriptors)
	 *  then use the same fd as fd[0]
	 */
	for (i = 1; i < aio_linux_requests; i++) {
		fds[i] = open(filename, O_RDWR | O_DIRECT, S_IRUSR | S_IWUSR);
		if (fds[i] < 0)
			fds[i] = fds[0];
	}
	(void)unlink(filename);

	stress_set_proc_state(args->name, STRESS_STATE_RUN);

	j = 0;
	do {
		uint8_t *bufptr;
		int n;
		off_t offset = stress_mwc16() * BUFFER_SZ;

		/*
		 *  async writes
		 */
		(void)memset(cb, 0, aio_linux_requests * sizeof(*cb));
		for (bufptr = buffer, i = 0; i < aio_linux_requests; i++, bufptr += BUFFER_SZ) {
			aio_linux_fill_buffer(j + (((intptr_t)bufptr) >> 12), bufptr, BUFFER_SZ);

			cb[i].aio_fildes = fds[i];
			cb[i].aio_lio_opcode = IO_CMD_PWRITE;
			cb[i].u.c.buf = bufptr;
			cb[i].u.c.offset = offset + (i * BUFFER_SZ);
			cb[i].u.c.nbytes = BUFFER_SZ;
			cbs[i] = &cb[i];
		}
		if (stress_aiol_submit(args, ctx, cbs, aio_linux_requests, false) < 0)
			break;
		if (stress_aiol_wait(args, ctx, events, aio_linux_requests) < 0)
			break;
		inc_counter(args);
		if (!keep_stressing(args))
			break;

		/*
		 *  async reads
		 */
		(void)memset(cb, 0, aio_linux_requests * sizeof(*cb));
		for (bufptr = buffer, i = 0; i < aio_linux_requests; i++, bufptr += BUFFER_SZ) {
			(void)memset(bufptr, 0, BUFFER_SZ);

			cb[i].aio_fildes = fds[i];
			cb[i].aio_lio_opcode = IO_CMD_PREAD;
			cb[i].u.c.buf = bufptr;
			cb[i].u.c.offset = offset + (i * BUFFER_SZ);
			cb[i].u.c.nbytes = BUFFER_SZ;
			cbs[i] = &cb[i];
		}

		if (stress_aiol_submit(args, ctx, cbs, aio_linux_requests, false) < 0)
			break;

		n = stress_aiol_wait(args, ctx, events, aio_linux_requests);
		if (n < 0)
			break;

		for (i = 0; i < (size_t)n; i++) {
			struct iocb *obj = events[i].obj;

			if (!obj)
				continue;

			bufptr = obj->u.c.buf;
			if (aio_linux_check_buffer(j + (((intptr_t)bufptr) >> 12), bufptr, BUFFER_SZ) != true) {
				if (warnings++ < 5) {
					pr_inf("%s: unexpected data mismatch in buffer %zd (maybe a wait timeout issue)\n",
						args->name, i);
					break;
				}
			}
		}

#if defined(__NR_io_cancel)
		{
			static int cancel;
			struct io_event event;

			cancel++;
			if (cancel >= 127) {
				ret = shim_io_cancel(ctx, &cb[0], &event);
				(void)ret;
				cancel = 0;
			}
		}
#endif
		inc_counter(args);
		if (!keep_stressing(args))
			break;

		/*
		 *  Exercise aio_poll with illegal settings
		 */
		(void)memset(cb, 0, aio_linux_requests * sizeof(*cb));
		for (i = 0; i < aio_linux_requests; i++) {
			cb[i].aio_fildes = fds[i];
			cb[i].aio_lio_opcode = IO_CMD_POLL;
			cb[i].u.c.buf = (void *)POLLIN;
			cb[i].u.c.offset = ~0;	/* invalid */
			cb[i].u.c.nbytes = ~0;	/* invalid */
			cbs[i] = &cb[i];
		}
		if (stress_aiol_submit(args, ctx, cbs, aio_linux_requests, true) < 0)
			break;
		if (errno == 0)
			(void)stress_aiol_wait(args, ctx, events, aio_linux_requests);
		inc_counter(args);
		if (!keep_stressing(args))
			break;
#if 0
		/*
		 *  Async fdsync and fsync every 256 iterations, older kernels don't
		 *  support these, so don't fail if EINVAL is returned.
		 */
		if (j++ >= 256) {
			j = 0;

			(void)memset(cb, 0, aio_linux_requests * sizeof(*cb));
			for (bufptr = buffer, i = 0; i < aio_linux_requests; i++, bufptr += BUFFER_SZ) {
				aio_linux_fill_buffer(i, bufptr, BUFFER_SZ);

				cb[i].aio_fildes = fds[i];
				cb[i].aio_lio_opcode = (i & 1) ? IO_CMD_FDSYNC : IO_CMD_FSYNC;
				cbs[i] = &cb[i];
			}
			if (stress_aiol_submit(args, ctx, cbs, aio_linux_requests, true) < 0)
				break;
			if (errno == 0)
				(void)stress_aiol_wait(args, ctx, events, aio_linux_requests);
			
		}
#endif
		inc_counter(args);
	} while (keep_stressing(args));

	rc = EXIT_SUCCESS;

	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);
	(void)close(fds[0]);
	for (i = 1; i < aio_linux_requests; i++) {
		if (fds[i] != fds[0])
			(void)close(fds[i]);
	}
finish:
	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);
	(void)shim_io_destroy(ctx);
	(void)stress_temp_dir_rm_args(args);

free_memory:
	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);
	stress_aiol_free(buffer, cb, events, cbs, fds);
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
