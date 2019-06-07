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

static const help_t help[] = {
	{ NULL,	"aio N",	"start N workers that issue async I/O requests" },
	{ NULL,	"aio-ops N",	"stop after N bogo async I/O requests" },
	{ NULL, "aio-requests N", "number of async I/O requests per worker" },
	{ NULL,	NULL,		NULL }
};

#if defined(HAVE_LIB_RT) && \
    defined(HAVE_AIO_H) && \
    NEED_GLIBC(2,1,0)

#define BUFFER_SZ	(16)

/* per request async I/O data */
typedef struct {
	int		request;		/* Request slot */
	int		status;			/* AIO error status */
	struct aiocb	aiocb;			/* AIO cb */
	uint8_t		buffer[BUFFER_SZ];	/* Associated read/write buffer */
	volatile uint64_t count;		/* Signal handled count */
} io_req_t;

static volatile bool do_accounting = true;
#endif

static int stress_set_aio_requests(const char *opt)
{
	uint64_t aio_requests;

	aio_requests = get_uint64(opt);
	check_range("aio-requests", aio_requests,
		MIN_AIO_REQUESTS, MAX_AIO_REQUESTS);
	return set_setting("aio-requests", TYPE_ID_UINT64, &aio_requests);
}

static const opt_set_func_t opt_set_funcs[] = {
	{ OPT_aio_requests,	stress_set_aio_requests },
	{ 0,			NULL },
};

#if defined(HAVE_LIB_RT) && \
    defined(HAVE_AIO_H) && \
    NEED_GLIBC(2,1,0)
/*
 *  aio_fill_buffer()
 *	fill buffer with some known pattern
 */
static inline void aio_fill_buffer(
	const int request,
	uint8_t *const buffer,
	const size_t size)
{
	register size_t i;

	for (i = 0; i < size; i++)
		buffer[i] = (uint8_t)(request + i);
}

/*
 *  aio_signal_handler()
 *	handle an async I/O signal
 */
static void MLOCKED_TEXT aio_signal_handler(int sig, siginfo_t *si, void *ucontext)
{
	io_req_t *io_req = (io_req_t *)si->si_value.sival_ptr;

	(void)sig;
	(void)si;
	(void)ucontext;

	if (do_accounting && io_req)
		io_req->count++;
}

/*
 *  aio_issue_cancel()
 *	cancel an in-progress async I/O request
 */
static void aio_issue_cancel(const char *name, io_req_t *io_req)
{
	int ret;

	if (io_req->status != EINPROGRESS)
		return;

	ret = aio_cancel(io_req->aiocb.aio_fildes,
		&io_req->aiocb);
	switch (ret) {
	case AIO_CANCELED:
	case AIO_ALLDONE:
		break;
	case AIO_NOTCANCELED:
		pr_dbg("%s: async I/O request %d not cancelled\n",
			name, io_req->request);
		break;
	default:
		pr_err("%s: %d error: %d %s\n",
			name, io_req->request,
			errno, strerror(errno));
	}
}

/*
 *  issue_aio_request()
 *	construct an AIO request and action it
 */
static int issue_aio_request(
	const char *name,
	const int fd,
	const off_t offset,
	io_req_t *const io_req,
	const int request,
	int (*aio_func)(struct aiocb *aiocbp) )
{
	while (g_keep_stressing_flag) {
		int ret;

		io_req->request = request;
		io_req->status = EINPROGRESS;
		io_req->aiocb.aio_fildes = fd;
		io_req->aiocb.aio_buf = io_req->buffer;
		io_req->aiocb.aio_nbytes = BUFFER_SZ;
		io_req->aiocb.aio_reqprio = 0;
		io_req->aiocb.aio_offset = offset;
		io_req->aiocb.aio_sigevent.sigev_notify = SIGEV_SIGNAL;
		io_req->aiocb.aio_sigevent.sigev_signo = SIGUSR1;
		io_req->aiocb.aio_sigevent.sigev_value.sival_ptr = io_req;

		ret = aio_func(&io_req->aiocb);
		if (ret < 0) {
			if ((errno == EAGAIN) || (errno == EINTR))
				continue;
			pr_err("%s: failed to issue aio request: %d (%s)\n",
				name, errno, strerror(errno));
		}
		return ret;
	}
	/* Given up */
	return 1;
}

/*
 *  stress_aio
 *	stress asynchronous I/O
 */
static int stress_aio(const args_t *args)
{
	int ret, fd, rc = EXIT_FAILURE;
	io_req_t *io_reqs;
	struct sigaction sa, sa_old;
	char filename[PATH_MAX];
	uint64_t total = 0, i, opt_aio_requests = DEFAULT_AIO_REQUESTS;

	if (!get_setting("aio-requests", &opt_aio_requests)) {
		if (g_opt_flags & OPT_FLAGS_MAXIMIZE)
			opt_aio_requests = MAX_AIO_REQUESTS;
		if (g_opt_flags & OPT_FLAGS_MINIMIZE)
			opt_aio_requests = MIN_AIO_REQUESTS;
	}

	if ((io_reqs = calloc(opt_aio_requests, sizeof(*io_reqs))) == NULL) {
		pr_err("%s: cannot allocate io request structures\n", args->name);
		return EXIT_NO_RESOURCE;
	}

	ret = stress_temp_dir_mk_args(args);
	if (ret < 0) {
		free(io_reqs);
		return exit_status(-ret);
	}

	(void)stress_temp_filename_args(args,
		filename, sizeof(filename), mwc32());

	if ((fd = open(filename, O_CREAT | O_RDWR, S_IRUSR | S_IWUSR)) < 0) {
		rc = exit_status(errno);
		pr_fail_err("open");
		goto finish;
	}
	(void)unlink(filename);

	(void)sigemptyset(&sa.sa_mask);
	sa.sa_flags = SA_RESTART | SA_SIGINFO;
	sa.sa_sigaction = aio_signal_handler;
	if (sigaction(SIGUSR1, &sa, &sa_old) < 0)
		pr_fail_err("sigaction");

	/* Kick off requests */
	for (i = 0; i < opt_aio_requests; i++) {
		aio_fill_buffer(i, io_reqs[i].buffer, BUFFER_SZ);
		ret = issue_aio_request(args->name, fd, (off_t)i * BUFFER_SZ,
			&io_reqs[i], i, aio_write);
		if (ret < 0)
			goto cancel;
		if (ret > 0) {
			rc = EXIT_SUCCESS;
			goto cancel;
		}
	}

	do {
		(void)shim_usleep(250000); /* wait until a signal occurs */

		for (i = 0; keep_stressing() && (i < opt_aio_requests); i++) {
			if (io_reqs[i].status != EINPROGRESS)
				continue;

			io_reqs[i].status = aio_error(&io_reqs[i].aiocb);
			switch (io_reqs[i].status) {
			case ECANCELED:
			case 0:
				/* Succeeded or cancelled, so redo another */
				inc_counter(args);
				if (issue_aio_request(args->name, fd,
					(off_t)i * BUFFER_SZ, &io_reqs[i], i,
					mwc1() ? aio_read : aio_write) < 0)
					goto cancel;
				break;
			case EINPROGRESS:
				break;
			default:
				/* Something went wrong */
				pr_fail_errno("aio_error",
					io_reqs[i].status);
				goto cancel;
			}
		}
	} while (keep_stressing());

	rc = EXIT_SUCCESS;

cancel:
	/* Stop accounting */
	do_accounting = false;
	/* Cancel pending AIO requests */
	for (i = 0; i < opt_aio_requests; i++) {
		aio_issue_cancel(args->name, &io_reqs[i]);
		total += io_reqs[i].count;
	}
	(void)close(fd);
finish:
	free(io_reqs);

	pr_dbg("%s: total of %" PRIu64 " async I/O signals "
		"caught (instance %d)\n",
		args->name, total, args->instance);
	(void)stress_temp_dir_rm_args(args);
	return rc;
}

stressor_info_t stress_aio_info = {
	.stressor = stress_aio,
	.class = CLASS_IO | CLASS_INTERRUPT | CLASS_OS,
	.opt_set_funcs = opt_set_funcs,
	.help = help
};
#else
stressor_info_t stress_aio_info = {
	.stressor = stress_not_implemented,
	.class = CLASS_IO | CLASS_INTERRUPT | CLASS_OS,
	.opt_set_funcs = opt_set_funcs,
	.help = help
};
#endif
