/*
 * Copyright (C) 2013-2020 Canonical, Ltd.
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

static const stress_help_t help[] = {
	{ NULL,	"io-uring N",		"start N workers that issue io-uring I/O requests" },
	{ NULL,	"io-uring-ops N",	"stop after N bogo io-uring I/O requests" },
	{ NULL,	NULL,		NULL }
};

#if defined(HAVE_LINUX_IO_URING_H) &&	\
    defined(__NR_io_uring_enter) &&	\
    defined(__NR_io_uring_setup) &&	\
    defined(IORING_OFF_SQ_RING) &&	\
    defined(IORING_OFF_CQ_RING) &&	\
    defined(IORING_OFF_SQES) &&		\
    defined(HAVE_POSIX_MEMALIGN)

/*
 *  io uring file info
 */
typedef struct {
	int fd;			/* file descriptor */
	struct iovec *iovecs;	/* iovecs array 1 per block to submit */
	off_t file_size;	/* size of the file (bytes) */
	size_t blocks;		/* number of blocks to action */
	size_t block_size;	/* per block size */
} stress_io_uring_file_t;

/*
 * io uring submission queue info
 */
typedef struct {
	unsigned *head;
	unsigned *tail;
	unsigned *ring_mask;
	unsigned *ring_entries;
	unsigned *flags;
	unsigned *array;
} stress_uring_io_sq_ring_t;

/*
 * io uring completion queue info
 */
typedef struct {
	unsigned *head;
	unsigned *tail;
	unsigned *ring_mask;
	unsigned *ring_entries;
	struct io_uring_cqe *cqes;
} stress_uring_io_cq_ring_t;

/*
 *  io uring submission info
 */
typedef struct {
	stress_uring_io_sq_ring_t sq_ring;
	stress_uring_io_cq_ring_t cq_ring;
	struct io_uring_sqe *sqes;
	int io_uring_fd;
} stress_io_uring_submit_t;

/*
 *  shim_io_uring_setup
 *	wrapper for io_uring_setup()
 */
static int shim_io_uring_setup(unsigned entries, struct io_uring_params *p)
{
	return (int)syscall(__NR_io_uring_setup, entries, p);
}

/*
 *  shim_io_uring_enter
 *	wrapper for o_uring_enter()
 */
static int shim_io_uring_enter(
	int fd,
	unsigned int to_submit,
	unsigned int min_complete,
	unsigned int flags)
{
	return (int)syscall(__NR_io_uring_enter, fd, to_submit,
		min_complete, flags, NULL, 0);
}

/*
 *  stress_io_uring_free_iovecs()
 *	free uring file iovecs
 */
static void stress_io_uring_free_iovecs(stress_io_uring_file_t *io_uring_file)
{
	size_t i;

	for (i = 0; i < io_uring_file->blocks; i++)
		free(io_uring_file->iovecs[i].iov_base);

	free(io_uring_file->iovecs);
	io_uring_file->iovecs = NULL;
}

/*
 *  stress_setup_io_uring()
 *	setup the io uring
 */
static int stress_setup_io_uring(
	const stress_args_t *args,
	stress_io_uring_submit_t *submit)
{
	stress_uring_io_sq_ring_t *sring = &submit->sq_ring;
	stress_uring_io_cq_ring_t *cring = &submit->cq_ring;
	int sring_size, cring_size;
	struct io_uring_params p;
	void *sq_ptr, *cq_ptr;

	(void)memset(&p, 0, sizeof(p));
	submit->io_uring_fd = shim_io_uring_setup(1, &p);
	if (submit->io_uring_fd < 0) {
		pr_err("%s: io_uring_setup failed: errno=%d (%s)\n",
			args->name, errno, strerror(errno));
		return EXIT_FAILURE;
	}
	sring_size = p.sq_off.array + p.sq_entries * sizeof(unsigned);
	cring_size = p.cq_off.cqes + p.cq_entries * sizeof(struct io_uring_cqe);
	if (p.features & IORING_FEAT_SINGLE_MMAP) {
		if (cring_size > sring_size)
			sring_size = cring_size;
		cring_size = sring_size;
	}

	sq_ptr = mmap(NULL, sring_size, PROT_READ | PROT_WRITE,
		MAP_SHARED | MAP_POPULATE,
		submit->io_uring_fd, IORING_OFF_SQ_RING);
	if (sq_ptr == MAP_FAILED) {
		pr_inf("%s: could not mmap submission queue buffer, errno=%d (%s)\n",
			args->name, errno, strerror(errno));
		return EXIT_NO_RESOURCE;
	}

	if (p.features & IORING_FEAT_SINGLE_MMAP) {
		cq_ptr = sq_ptr;
	} else {
		cq_ptr = mmap(NULL, cring_size, PROT_READ | PROT_WRITE,
				MAP_SHARED | MAP_POPULATE,
				submit->io_uring_fd, IORING_OFF_CQ_RING);
		if (cq_ptr == MAP_FAILED) {
			pr_inf("%s: could not mmap completion queue buffer, errno=%d (%s)\n",
				args->name, errno, strerror(errno));
			(void)munmap(sq_ptr, cring_size);
			return EXIT_NO_RESOURCE;
		}
	}

	sring->head = sq_ptr + p.sq_off.head;
	sring->tail = sq_ptr + p.sq_off.tail;
	sring->ring_mask = sq_ptr + p.sq_off.ring_mask;
	sring->ring_entries = sq_ptr + p.sq_off.ring_entries;
	sring->flags = sq_ptr + p.sq_off.flags;
	sring->array = sq_ptr + p.sq_off.array;

	submit->sqes = mmap(NULL, p.sq_entries * sizeof(struct io_uring_sqe),
			PROT_READ | PROT_WRITE, MAP_SHARED | MAP_POPULATE,
			submit->io_uring_fd, IORING_OFF_SQES);
	if (submit->sqes == MAP_FAILED) {
		pr_inf("%s: count not mmap submission queue buffer, errno=%d (%s)\n",
			args->name, errno, strerror(errno));
		(void)munmap(cq_ptr, cring_size);
		(void)munmap(sq_ptr, sring_size);
		return EXIT_NO_RESOURCE;
	}

	cring->head = cq_ptr + p.cq_off.head;
	cring->tail = cq_ptr + p.cq_off.tail;
	cring->ring_mask = cq_ptr + p.cq_off.ring_mask;
	cring->ring_entries = cq_ptr + p.cq_off.ring_entries;
	cring->cqes = cq_ptr + p.cq_off.cqes;

	return EXIT_SUCCESS;
}

/*
 *  stress_io_uring_iovec_submit()
 *	perform a iovec submit over io_uring
 */
static int stress_io_uring_iovec_submit(
	const stress_args_t *args,
	stress_io_uring_submit_t *submit,
	stress_io_uring_file_t *io_uring_file,
	const int opcode)
{
	stress_uring_io_sq_ring_t *sring = &submit->sq_ring;
	unsigned index = 0, tail = 0, next_tail = 0;
	struct io_uring_sqe *sqe;
	int ret;

	next_tail = tail = *sring->tail;
	next_tail++;

	shim_mb();

	index = tail & *submit->sq_ring.ring_mask;
	sqe = &submit->sqes[index];
	sqe->fd = io_uring_file->fd;
	sqe->flags = 0;
	sqe->opcode = opcode;
	sqe->addr = (unsigned long)io_uring_file->iovecs;
	sqe->len = io_uring_file->blocks;
	sqe->off = 0;
	sqe->user_data = (unsigned long long)io_uring_file;
	sring->array[index] = index;
	tail = next_tail;

	if (*sring->tail != tail) {
		*sring->tail = tail;
		shim_mb();
	}

	ret = shim_io_uring_enter(submit->io_uring_fd, 1,1, IORING_ENTER_GETEVENTS);
	if (ret < 0) {
		pr_fail("%s: io_uring_enter failed, errno=%d (%s)\n",
			args->name, errno, strerror(errno));
		stress_io_uring_free_iovecs(io_uring_file);
		return EXIT_FAILURE;
	}
	return EXIT_SUCCESS;
}

/*
 *  stress_io_uring_iovec_complete()
 *	handle pending iovec I/Os to complete
 */
static int stress_io_uring_iovec_complete(
	const stress_args_t *args,
	stress_io_uring_submit_t *submit)
{
	stress_uring_io_cq_ring_t *cring = &submit->cq_ring;
	struct io_uring_cqe *cqe;
	unsigned head = *cring->head;
	int ret = EXIT_SUCCESS;

	while (keep_stressing()) {
		shim_mb();

		/* Empty? */
		if (head == *cring->tail)
			break;

		cqe = &cring->cqes[head & *submit->cq_ring.ring_mask];
		if (cqe->res < 0) {
			const int err = abs(cqe->res);

			pr_err("%s: completion uring io error: %d (%s)\n",
				args->name, err, strerror(err));
			ret = EXIT_FAILURE;
		}
		head++;
	}

	*cring->head = head;
	shim_mb();

	return ret;
}

/*
 *  stress_io_uring
 *	stress asynchronous I/O
 */
static int stress_io_uring(const stress_args_t *args)
{
	int ret, rc = EXIT_FAILURE;
	char filename[PATH_MAX];
	stress_io_uring_file_t io_uring_file;
	size_t i;
	const size_t blocks = 1024;
	const size_t block_size = 512;
	off_t file_size = (off_t)blocks * block_size;
	stress_io_uring_submit_t submit;

	(void)memset(&submit, 0, sizeof(submit));
	(void)memset(&io_uring_file, 0, sizeof(io_uring_file));

	io_uring_file.file_size = file_size;
	io_uring_file.blocks = blocks;
	io_uring_file.block_size = block_size;
	io_uring_file.iovecs = calloc(blocks, sizeof(*io_uring_file.iovecs));

	for (i = 0; (i < blocks) && (file_size > 0); i++) {
		const size_t iov_len = (file_size > (off_t)block_size) ? (size_t)block_size : (size_t)file_size;

		io_uring_file.iovecs[i].iov_len = iov_len;
		if (posix_memalign(&io_uring_file.iovecs[i].iov_base, block_size, block_size)) {
			pr_inf("%s: cannot allocate iovecs\n", args->name);
			stress_io_uring_free_iovecs(&io_uring_file);
			return EXIT_NO_RESOURCE;
		}
		file_size -= iov_len;
	}

	ret = stress_temp_dir_mk_args(args);
	if (ret < 0)
		return exit_status(-ret);

	(void)stress_temp_filename_args(args,
		filename, sizeof(filename), stress_mwc32());

	rc = stress_setup_io_uring(args, &submit);
	if (rc != EXIT_SUCCESS)
		goto clean;

	if ((io_uring_file.fd = open(filename, O_CREAT | O_RDWR, S_IRUSR | S_IWUSR)) < 0) {
		rc = exit_status(errno);
		pr_fail("%s: open on %s failed, errno=%d (%s)\n",
			args->name, filename, errno, strerror(errno));
		goto clean;
	}
	(void)unlink(filename);

	rc = EXIT_SUCCESS;
	do {
		rc = stress_io_uring_iovec_submit(args, &submit, &io_uring_file, IORING_OP_WRITEV);
		if (rc != EXIT_SUCCESS)
			break;
		rc = stress_io_uring_iovec_complete(args, &submit);
		if (rc != EXIT_SUCCESS)
			break;

		rc = stress_io_uring_iovec_submit(args, &submit, &io_uring_file, IORING_OP_READV);
		if (rc != EXIT_SUCCESS)
			break;
		rc = stress_io_uring_iovec_complete(args, &submit);
		if (rc != EXIT_SUCCESS)
			break;

		inc_counter(args);
	} while (keep_stressing());

	(void)close(io_uring_file.fd);
clean:
	(void)stress_temp_dir_rm_args(args);
	return rc;
}

stressor_info_t stress_io_uring_info = {
	.stressor = stress_io_uring,
	.class = CLASS_IO | CLASS_OS,
	.help = help
};
#else
stressor_info_t stress_io_uring_info = {
	.stressor = stress_not_implemented,
	.class = CLASS_IO | CLASS_OS,
	.help = help
};
#endif
