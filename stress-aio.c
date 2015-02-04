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

#if defined (__linux__)

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <aio.h>
#include <fcntl.h>

#include "stress-ng.h"

#define BUFFER_SZ	(16)

static uint64_t opt_aio_requests = DEFAULT_AIO_REQUESTS;

/* per request async I/O data */
typedef struct {
	int		request;		/* Request slot */
	int		status;			/* AIO error status */
	struct aiocb	aiocb;			/* AIO cb */
	uint8_t		buffer[BUFFER_SZ];	/* Associated read/write buffer */
	volatile uint64_t count;		/* Signal handled count */
} io_req_t;

void stress_set_aio_requests(const char *optarg)
{
	opt_aio_requests = get_uint64(optarg);
	check_range("aio-requests", opt_aio_requests,
		MIN_AIO_REQUESTS, MAX_AIO_REQUESTS);
}

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
		buffer[i] = request + i;
}

/*
 *  aio_signal_handler()
 *	handle an async I/O signal
 */
static void aio_signal_handler(int sig, siginfo_t *si, void *ucontext)
{
	io_req_t *io_req = (io_req_t *)si->si_value.sival_ptr;

	(void)sig;
	(void)si;
	(void)ucontext;

	if (io_req)
		io_req->count++;
}

/*
 *  aio_issue_cancel()
 *	cancel an in-progress async I/O request
 */
static void aio_issue_cancel(io_req_t *io_req)
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
		pr_dbg(stderr, "async I/O request %d not cancelled\n",
			io_req->request);
		break;
	default:
		pr_err(stderr, "%d error: %d %s\n",
			io_req->request,
			errno, strerror(errno));
	}
}

/*
 *  issue_aio_request()
 *	construct an AIO request and action it
 */
static int issue_aio_request(
	const int fd,
	const off_t offset,
	io_req_t *const io_req,
	const int request,
	int (*aio_func)(struct aiocb *aiocbp) )
{
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
		pr_err(stderr, "failed to issue aio request: %d (%s)\n",
			errno, strerror(errno));
	}
	return ret;
}

/*
 *  stress_aio
 *	stress asynchronous I/O
 */
int stress_aio(
	uint64_t *const counter,
	const uint32_t instance,
	const uint64_t max_ops,
	const char *name)
{
	int fd, rc = EXIT_FAILURE;
	io_req_t *io_reqs;
	struct sigaction sa;
	uint64_t i, total = 0;
	char filename[PATH_MAX];
	const pid_t pid = getpid();

	if ((io_reqs = calloc((size_t)opt_aio_requests, sizeof(io_req_t))) == NULL) {
		pr_err(stderr, "%s: cannot allocate io request structures\n", name);
		return EXIT_FAILURE;
	}

	if (stress_temp_dir_mk(name, pid, instance) < 0) {
		free(io_reqs);
		return EXIT_FAILURE;
	}
	(void)stress_temp_filename(filename, sizeof(filename),
		name, pid, instance, mwc());

	(void)umask(0077);
	if ((fd = open(filename, O_CREAT | O_RDWR, S_IRUSR | S_IWUSR)) < 0) {
		pr_failed_err(name, "open");
		goto finish;
	}
	(void)unlink(filename);

	sigemptyset(&sa.sa_mask);
	sa.sa_flags = SA_RESTART | SA_SIGINFO;
	sa.sa_sigaction = aio_signal_handler;
	if (sigaction(SIGUSR1, &sa, NULL) < 0) {
		pr_failed_err(name, "sigaction");
	}

	/* Kick off requests */
	for (i = 0; i < opt_aio_requests; i++) {
		aio_fill_buffer(i, io_reqs[i].buffer, BUFFER_SZ);
		if (issue_aio_request(fd, i * BUFFER_SZ, &io_reqs[i], i, aio_write) < 0)
			goto cancel;
	}

	do {
		usleep(250000); /* wait until a signal occurs */

		for (i = 0; i < opt_aio_requests; i++) {
			if (io_reqs[i].status != EINPROGRESS)
				continue;

			io_reqs[i].status = aio_error(&io_reqs[i].aiocb);
			switch (io_reqs[i].status) {
			case ECANCELED:
			case 0:
				/* Succeeded or cancelled, so redo another */
				(*counter)++;
				if (issue_aio_request(fd, i * BUFFER_SZ, &io_reqs[i], i,
					(mwc() & 0x20) ? aio_read : aio_write) < 0)
					goto cancel;
				break;
			case EINPROGRESS:
				break;
			default:
				/* Something went wrong */
				pr_failed_err(name, "aio_error");
				goto cancel;
			}
		}
	} while (opt_do_run && (!max_ops || *counter < max_ops));

	rc = EXIT_SUCCESS;

cancel:
	for (i = 0; i < opt_aio_requests; i++) {
		aio_issue_cancel(&io_reqs[i]);
		total += io_reqs[i].count;
	}
	(void)close(fd);
finish:
	pr_dbg(stderr, "total of %" PRIu64 " async I/O signals caught (instance %d)\n",
		total, instance);
	(void)stress_temp_dir_rm(name, pid, instance);
	free(io_reqs);
	return rc;
}

#endif
