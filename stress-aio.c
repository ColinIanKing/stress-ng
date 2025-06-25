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

#if defined(HAVE_AIO_H)
#include <aio.h>
#endif

#define MIN_AIO_REQUESTS	(1)
#define MAX_AIO_REQUESTS	(4096)
#define DEFAULT_AIO_REQUESTS	(16)
#define BUFFER_SZ		(16)

static const stress_help_t help[] = {
	{ NULL,	"aio N",	  "start N workers that issue async I/O requests" },
	{ NULL,	"aio-ops N",	  "stop after N bogo async I/O requests" },
	{ NULL, "aio-requests N", "number of async I/O requests per worker" },
	{ NULL,	NULL,		NULL }
};

#if defined(HAVE_LIB_RT) &&	\
    defined(HAVE_AIO_H)  &&	\
    defined(HAVE_AIO_CANCEL) && \
    defined(HAVE_AIO_READ) &&	\
    defined(HAVE_AIO_WRITE)

/* per request async I/O data */
typedef struct {
	int		request;		/* Request slot */
	int		status;			/* AIO error status */
	struct aiocb	aiocb;			/* AIO cb */
	uint8_t		buffer[BUFFER_SZ];	/* Associated read/write buffer */
	volatile uint64_t count;		/* Signal handled count */
} stress_io_req_t;

static volatile bool do_accounting = true;
#endif

static const stress_opt_t opts[] = {
	{ OPT_aio_requests, "aio-requests", TYPE_ID_UINT32, MIN_AIO_REQUESTS, MAX_AIO_REQUESTS, NULL },
	END_OPT,
};

#if defined(HAVE_LIB_RT) &&	\
    defined(HAVE_AIO_H) &&	\
    defined(HAVE_AIO_CANCEL) && \
    defined(HAVE_AIO_READ) &&	\
    defined(HAVE_AIO_WRITE)
/*
 *  aio_fill_buffer()
 *	fill buffer with some known pattern
 */
static void aio_fill_buffer(
	const uint8_t pattern,
	uint8_t *const buffer,
	const size_t size)
{
	register size_t i;
	register uint8_t pat = pattern;

	for (i = 0; i < size; i++, pat++)
		buffer[i] = pat;
}

/*
 *  aio_signal_handler()
 *	handle an async I/O signal
 */
static void MLOCKED_TEXT aio_signal_handler(int sig, siginfo_t *si, void *ucontext)
{
	stress_io_req_t *io_req = (stress_io_req_t *)si->si_value.sival_ptr;

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
static void aio_issue_cancel(const char *name, stress_io_req_t *io_req)
{
	int retries = 0;

	for (;;) {
		int ret;

		ret = aio_error(&io_req->aiocb);
		if (ret != EINPROGRESS)
			return;

		ret = aio_cancel(io_req->aiocb.aio_fildes,
			&io_req->aiocb);
		switch (ret) {
		case AIO_CANCELED:
		case AIO_ALLDONE:
			return;
		case AIO_NOTCANCELED:
			if (retries++ > 25) {
				/* Give up */
				if ((errno != 0) && (errno != EINTR)) {
					pr_inf("%s aio request %d could not be cancelled: error=%d (%s)\n",
						name, io_req->request,
						errno, strerror(errno));
				}
			}
			/* Wait a bit and retry */
			(void)shim_usleep_interruptible(250000);
			break;
		default:
			pr_fail("%s: %d aio_error(), errno=%d %s\n",
				name, io_req->request,
				errno, strerror(errno));
			break;
		}
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
	stress_io_req_t *const io_req,
	const uint32_t request,
	int (*aio_func)(struct aiocb *aiocbp))
{
	while (stress_continue_flag()) {
		int ret;

		io_req->request = (int)request;
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
			if ((errno == EAGAIN) ||
			    (errno == EINTR) ||
			    (errno == EBUSY))
				continue;
			pr_fail("%s: failed to issue aio request, errno=%d (%s)\n",
				name, errno, strerror(errno));
		}
		return ret;
	}
	/* Given up */
	return 1;
}

#if defined(HAVE_AIO_FSYNC) &&	\
    defined(O_SYNC) &&		\
    defined(O_DSYNC)
/*
 *  issue_aio_sync_request()
 *	construct an AIO sync request and action it
 */
static int issue_aio_sync_request(
	const char *name,
	const int fd,
	stress_io_req_t *const io_req)
{
	while (stress_continue_flag()) {
		int ret;
		const int op = stress_mwc1() ? O_SYNC : O_DSYNC;

		io_req->request = 0;
		io_req->status = EINPROGRESS;
		io_req->aiocb.aio_fildes = fd;
		io_req->aiocb.aio_buf = 0;
		io_req->aiocb.aio_nbytes = 0;
		io_req->aiocb.aio_reqprio = 0;
		io_req->aiocb.aio_offset = 0;
		io_req->aiocb.aio_sigevent.sigev_notify = SIGEV_SIGNAL;
		io_req->aiocb.aio_sigevent.sigev_signo = SIGUSR1;
		io_req->aiocb.aio_sigevent.sigev_value.sival_ptr = io_req;

		ret = aio_fsync(op, &io_req->aiocb);
		if (ret < 0) {
			if ((errno == EAGAIN) || (errno == EINTR))
				continue;
			pr_fail("%s: failed to issue aio request, errno=%d (%s)\n",
				name, errno, strerror(errno));
		}
		return ret;
	}
	/* Given up */
	return 1;
}
#endif

/*
 *  stress_aio
 *	stress asynchronous I/O
 */
static int stress_aio(stress_args_t *args)
{
	int ret, fd, rc = EXIT_FAILURE;
	stress_io_req_t *io_reqs;
	struct sigaction sa, sa_old;
	char filename[PATH_MAX];
	uint32_t total = 0, i, opt_aio_requests = DEFAULT_AIO_REQUESTS;
	double t1 = 0.0, t2 = 0.0, dt, rate;
	const char *fs_type;

	if (!stress_get_setting("aio-requests", &opt_aio_requests)) {
		if (g_opt_flags & OPT_FLAGS_MAXIMIZE)
			opt_aio_requests = MAX_AIO_REQUESTS;
		if (g_opt_flags & OPT_FLAGS_MINIMIZE)
			opt_aio_requests = MIN_AIO_REQUESTS;
	}

	if ((io_reqs = (stress_io_req_t *)calloc(opt_aio_requests, sizeof(*io_reqs))) == NULL) {
		pr_inf_skip("%s: cannot allocate %" PRIu32 " io request "
			    "structures, skipping stressor\n", args->name, opt_aio_requests);
		return EXIT_NO_RESOURCE;
	}

	ret = stress_temp_dir_mk_args(args);
	if (ret < 0) {
		ret = stress_exit_status(-ret);
		free(io_reqs);

		return ret;
	}

	(void)stress_temp_filename_args(args,
		filename, sizeof(filename), stress_mwc32());

	if ((fd = open(filename, O_CREAT | O_RDWR, S_IRUSR | S_IWUSR)) < 0) {
		rc = stress_exit_status(errno);
		pr_fail("%s: open on %s failed, errno=%d (%s)\n",
			args->name, filename, errno, strerror(errno));
		goto finish;
	}
	fs_type = stress_get_fs_type(filename);
	(void)shim_unlink(filename);

	stress_file_rw_hint_short(fd);

	(void)sigemptyset(&sa.sa_mask);
	sa.sa_flags = SA_RESTART | SA_SIGINFO;
	sa.sa_sigaction = aio_signal_handler;
	if (sigaction(SIGUSR1, &sa, &sa_old) < 0)
		pr_fail("%s: sigaction on SIGUSR1 failed, errno=%d (%s)\n",
			args->name, errno, strerror(errno));

	/* Kick off requests */
	for (i = 0; i < opt_aio_requests; i++) {
		aio_fill_buffer((uint8_t)i, io_reqs[i].buffer, BUFFER_SZ);
		ret = issue_aio_request(args->name, fd, (off_t)i * BUFFER_SZ,
			&io_reqs[i], i, aio_write);
		if (ret < 0)
			goto cancel;
		if (ret > 0) {
			rc = EXIT_SUCCESS;
			goto cancel;
		}
	}

	stress_set_proc_state(args->name, STRESS_STATE_SYNC_WAIT);
	stress_sync_start_wait(args);
	stress_set_proc_state(args->name, STRESS_STATE_RUN);

	t1 = stress_time_now();
	do {
		(void)shim_usleep_interruptible(250000); /* wait until a signal occurs */

		for (i = 0; LIKELY(stress_continue(args) && (i < opt_aio_requests)); i++) {
			if (io_reqs[i].status != EINPROGRESS)
				continue;

			io_reqs[i].status = aio_error(&io_reqs[i].aiocb);
			switch (io_reqs[i].status) {
			case ECANCELED:
			case 0:
				/* Succeeded or cancelled, so redo another */
				stress_bogo_inc(args);
#if defined(HAVE_AIO_FSYNC) &&	\
    defined(O_SYNC) &&		\
    defined(O_DSYNC)
				if (i != (opt_aio_requests - 1)) {
					ret = issue_aio_request(args->name, fd,
						(off_t)i * BUFFER_SZ,
						&io_reqs[i], i,
						stress_mwc1() ? aio_read : aio_write);
				} else {
					ret = issue_aio_sync_request(args->name,
						fd, &io_reqs[i]);
				}
#else
				ret = issue_aio_request(args->name, fd,
					(off_t)i * BUFFER_SZ, &io_reqs[i], i,
					stress_mwc1() ? aio_read : aio_write);
#endif
				if (ret < 0)
					goto cancel;

				break;
			case EINPROGRESS:
				break;
			case ENOSPC:
				/* Silently ignore ENOSPC write failures */
				break;
			default:
				/* Something went wrong */
				pr_fail("%s: aio_error, io_reqs[%" PRIu32 "].status = %d (%s)%s\n",
					args->name, i,
					io_reqs[i].status,
					strerror(io_reqs[i].status), fs_type);

				goto cancel;
			}
		}
	} while (stress_continue(args));
	t2 = stress_time_now();

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
	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);
	free(io_reqs);

	dt = t2 - t1;
	rate = (dt > 0.0) ? (double)total / dt : 0.0;
	stress_metrics_set(args, 0, "async I/O signals per sec",
			rate, STRESS_METRIC_HARMONIC_MEAN);
	stress_metrics_set(args, 1, "async I/O signals",
			(double)total, STRESS_METRIC_TOTAL);
	(void)stress_temp_dir_rm_args(args);
	return rc;
}

const stressor_info_t stress_aio_info = {
	.stressor = stress_aio,
	.classifier = CLASS_IO | CLASS_INTERRUPT | CLASS_OS,
	.opts = opts,
	.verify = VERIFY_ALWAYS,
	.help = help
};
#else
const stressor_info_t stress_aio_info = {
	.stressor = stress_unimplemented,
	.classifier = CLASS_IO | CLASS_INTERRUPT | CLASS_OS,
	.opts = opts,
	.verify = VERIFY_ALWAYS,
	.help = help,
	.unimplemented_reason = "built without aio.h"
};
#endif
