/*
 * Copyright (C) 2013-2021 Canonical, Ltd.
 * Copyright (C)      2022 Colin Ian King.
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
#include "io-uring.h"

#if defined(HAVE_LINUX_IO_URING_H)
#include <linux/io_uring.h>
#endif

#if !defined(O_DSYNC)
#define O_DSYNC		(0)
#endif

static const stress_help_t help[] = {
	{ NULL,	"io-uring N",		"start N workers that issue io-uring I/O requests" },
	{ NULL,	"io-uring-ops N",	"stop after N bogo io-uring I/O requests" },
	{ NULL,	NULL,		NULL }
};

#if defined(HAVE_LINUX_IO_URING_H) &&	\
    defined(HAVE_SYSCALL) &&		\
    defined(__NR_io_uring_enter) &&	\
    defined(__NR_io_uring_setup) &&	\
    defined(IORING_OFF_SQ_RING) &&	\
    defined(IORING_OFF_CQ_RING) &&	\
    defined(IORING_OFF_SQES) &&		\
    defined(HAVE_POSIX_MEMALIGN) &&	\
    (defined(HAVE_IORING_OP_WRITEV) ||	\
     defined(HAVE_IORING_OP_READV) ||	\
     defined(HAVE_IORING_OP_WRITE) || 	\
     defined(HAVE_IORING_OP_READ) || 	\
     defined(HAVE_IORING_OP_FSYNC) ||	\
     defined(HAVE_IORING_OP_NOP) ||	\
     defined(HAVE_IORING_OP_FALLOCATE) || \
     defined(HAVE_IORING_OP_FADVISE) ||	\
     defined(HAVE_IORING_OP_CLOSE) ||	\
     defined(HAVE_IORING_OP_MADVISE) ||	\
     defined(HAVE_IORING_OP_STATX) || 	\
     defined(HAVE_IORING_OP_GETXATTR) || \
     defined(HAVE_IORING_OP_SYNC_FILE_RANGE))



/*
 *  io uring file info
 */
typedef struct {
	int fd;			/* file descriptor */
	char *filename;		/* filename */
	struct iovec *iovecs;	/* iovecs array 1 per block to submit */
	size_t iovecs_sz;	/* size of iovecs allocation */
	off_t file_size;	/* size of the file (bytes) */
	uint32_t blocks;	/* number of blocks to action */
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
	struct io_uring_sqe *sqes_mmap;
	void *sq_mmap;
	void *cq_mmap;
	int io_uring_fd;
	size_t sq_size;
	size_t cq_size;
	size_t sqes_size;
} stress_io_uring_submit_t;

typedef void (*stress_io_uring_setup)(stress_io_uring_file_t *io_uring_file, struct io_uring_sqe *sqe);

/*
 *  opcode to human readable name lookup
 */
typedef struct {
	const uint8_t opcode;			/* opcode */
	const char *name;			/* stringified opcode name */
	const stress_io_uring_setup setup_func;	/* setup function */
} stress_io_uring_setup_info_t;

static const char *stress_io_uring_opcode_name(const uint8_t opcode);

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
 *	wrapper for io_uring_enter()
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
 *  stress_io_uring_unmap_iovecs()
 *	free uring file iovecs
 */
static void stress_io_uring_unmap_iovecs(stress_io_uring_file_t *io_uring_file)
{
	if (io_uring_file->iovecs) {
		size_t i;

		for (i = 0; i < io_uring_file->blocks; i++) {
			if (io_uring_file->iovecs[i].iov_base) {
				(void)munmap((void *)io_uring_file->iovecs[i].iov_base, io_uring_file->block_size);
				io_uring_file->iovecs[i].iov_base = NULL;
			}
		}
		(void)munmap((void *)io_uring_file->iovecs, io_uring_file->iovecs_sz);
	}
	io_uring_file->iovecs = NULL;
}

/*
 *  Avoid GCCism of void * pointer arithmetic by casting to
 *  uint8_t *, doing the offset and then casting back to void *
 */
#define VOID_ADDR_OFFSET(addr, offset)	\
	((void *)(((uint8_t *)addr) + offset))

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
	struct io_uring_params p;

	(void)memset(&p, 0, sizeof(p));
	submit->io_uring_fd = shim_io_uring_setup(256, &p);
	if (submit->io_uring_fd < 0) {
		if (errno == ENOSYS) {
			pr_inf_skip("%s: io-uring not supported by the kernel, skipping stressor\n",
				args->name);
			return EXIT_NOT_IMPLEMENTED;
		}
		pr_fail("%s: io_uring_setup failed, errno=%d (%s)\n",
			args->name, errno, strerror(errno));
		return EXIT_FAILURE;
	}
	submit->sq_size = p.sq_off.array + p.sq_entries * sizeof(unsigned);
	submit->cq_size = p.cq_off.cqes + p.cq_entries * sizeof(struct io_uring_cqe);
	if (p.features & IORING_FEAT_SINGLE_MMAP) {
		if (submit->cq_size > submit->sq_size)
			submit->sq_size = submit->cq_size;
		submit->cq_size = submit->sq_size;
	}

	submit->sq_mmap = mmap(NULL, submit->sq_size, PROT_READ | PROT_WRITE,
		MAP_SHARED | MAP_POPULATE,
		submit->io_uring_fd, IORING_OFF_SQ_RING);
	if (submit->sq_mmap == MAP_FAILED) {
		pr_inf("%s: could not mmap submission queue buffer, errno=%d (%s)\n",
			args->name, errno, strerror(errno));
		return EXIT_NO_RESOURCE;
	}

	if (p.features & IORING_FEAT_SINGLE_MMAP) {
		submit->cq_mmap = submit->sq_mmap;
	} else {
		submit->cq_mmap = mmap(NULL, submit->cq_size, PROT_READ | PROT_WRITE,
				MAP_SHARED | MAP_POPULATE,
				submit->io_uring_fd, IORING_OFF_CQ_RING);
		if (submit->cq_mmap == MAP_FAILED) {
			pr_inf("%s: could not mmap completion queue buffer, errno=%d (%s)\n",
				args->name, errno, strerror(errno));
			(void)munmap(submit->sq_mmap, submit->cq_size);
			return EXIT_NO_RESOURCE;
		}
	}

	sring->head = VOID_ADDR_OFFSET(submit->sq_mmap, p.sq_off.head);
	sring->tail = VOID_ADDR_OFFSET(submit->sq_mmap, p.sq_off.tail);
	sring->ring_mask = VOID_ADDR_OFFSET(submit->sq_mmap, p.sq_off.ring_mask);
	sring->ring_entries = VOID_ADDR_OFFSET(submit->sq_mmap, p.sq_off.ring_entries);
	sring->flags = VOID_ADDR_OFFSET(submit->sq_mmap, p.sq_off.flags);
	sring->array = VOID_ADDR_OFFSET(submit->sq_mmap, p.sq_off.array);

	submit->sqes_size = p.sq_entries * sizeof(struct io_uring_sqe);
	submit->sqes_mmap = mmap(NULL, submit->sqes_size,
			PROT_READ | PROT_WRITE, MAP_SHARED | MAP_POPULATE,
			submit->io_uring_fd, IORING_OFF_SQES);
	if (submit->sqes_mmap == MAP_FAILED) {
		pr_inf("%s: count not mmap submission queue buffer, errno=%d (%s)\n",
			args->name, errno, strerror(errno));
		if (submit->cq_mmap != submit->sq_mmap)
			(void)munmap(submit->cq_mmap, submit->cq_size);
		(void)munmap(submit->sq_mmap, submit->sq_size);
		return EXIT_NO_RESOURCE;
	}

	cring->head = VOID_ADDR_OFFSET(submit->cq_mmap, p.cq_off.head);
	cring->tail = VOID_ADDR_OFFSET(submit->cq_mmap, p.cq_off.tail);
	cring->ring_mask = VOID_ADDR_OFFSET(submit->cq_mmap, p.cq_off.ring_mask);
	cring->ring_entries = VOID_ADDR_OFFSET(submit->cq_mmap, p.cq_off.ring_entries);
	cring->cqes = VOID_ADDR_OFFSET(submit->cq_mmap, p.cq_off.cqes);

	return EXIT_SUCCESS;
}

/*
 *  stress_close_io_uring()
 *	close and cleanup behind us
 */
static void stress_close_io_uring(stress_io_uring_submit_t *submit)
{
	if (submit->io_uring_fd >= 0) {
		(void)close(submit->io_uring_fd);
		submit->io_uring_fd = -1;
	}

	if (submit->sqes_mmap) {
		(void)munmap(submit->sqes_mmap, submit->sqes_size);
		submit->sqes_mmap = NULL;
	}

	if (submit->cq_mmap && (submit->cq_mmap != submit->sq_mmap)) {
		(void)munmap(submit->cq_mmap, submit->cq_size);
		submit->cq_mmap = NULL;
	}

	if (submit->sq_mmap) {
		(void)munmap(submit->sq_mmap, submit->sq_size);
		submit->sq_mmap = NULL;
	}
}

/*
 *  stress_io_uring_complete()
 *	handle pending I/Os to complete
 */
static inline int stress_io_uring_complete(
	const stress_args_t *args,
	stress_io_uring_submit_t *submit,
	const uint8_t opcode,
	bool *supported)
{
	stress_uring_io_cq_ring_t *cring = &submit->cq_ring;
	struct io_uring_cqe *cqe;
	unsigned head = *cring->head;
	int ret = EXIT_SUCCESS;

	for (;;) {
		shim_mb();

		/* Empty? */
		if (head == *cring->tail)
			break;

		cqe = &cring->cqes[head & *submit->cq_ring.ring_mask];
		if ((cqe->res < 0) && (opcode != IORING_OP_FALLOCATE)) {
			const int err = abs(cqe->res);

			/* Silently ignore EOPNOTSUPP completion errors */
			if (errno == EOPNOTSUPP) {
				*supported = false;
			} else  {
				pr_fail("%s: completion opcode=%d (%s), error=%d (%s)\n",
					args->name, opcode,
					stress_io_uring_opcode_name(opcode),
					err, strerror(err));
				ret = EXIT_FAILURE;
			}
		}
		head++;
	}

	*cring->head = head;
	shim_mb();
	if (ret == EXIT_SUCCESS)
		inc_counter(args);

	return ret;
}

/*
 *  stress_io_uring_submit()
 *	submit an io-uring opcode
 */
static int stress_io_uring_submit(
	const stress_args_t *args,
	stress_io_uring_setup setup_func,
	stress_io_uring_file_t *io_uring_file,
	stress_io_uring_submit_t *submit,
	bool *supported)
{
	stress_uring_io_sq_ring_t *sring = &submit->sq_ring;
	unsigned index = 0, tail = 0, next_tail = 0;
	struct io_uring_sqe *sqe;
	int ret;
	uint8_t opcode;

	next_tail = tail = *sring->tail;
	next_tail++;
	shim_mb();
	index = tail & *submit->sq_ring.ring_mask;
	sqe = &submit->sqes_mmap[index];
	(void)memset(sqe, 0, sizeof(*sqe));

	setup_func(io_uring_file, sqe);
	opcode = sqe->opcode;

	sring->array[index] = index;
	tail = next_tail;
	if (*sring->tail != tail) {
		shim_mb();
		*sring->tail = tail;
		shim_mb();
	}

printf("%d %p\n", index, next_tail);

	ret = shim_io_uring_enter(submit->io_uring_fd, 1,
		1, IORING_ENTER_GETEVENTS);
	if (ret < 0) {
		/* Silently ignore ENOSPC failures */
		if (errno == ENOSPC)	
			return EXIT_SUCCESS;
		pr_fail("%s: io_uring_enter failed, opcode=%d (%s), errno=%d (%s)\n",
			args->name, opcode,
			stress_io_uring_opcode_name(opcode),
			errno, strerror(errno));
		if (errno == EOPNOTSUPP)
			*supported = false;
		return EXIT_FAILURE;
	}

	return stress_io_uring_complete(args, submit, opcode, supported);
}

#if defined(HAVE_IORING_OP_READV)
/*
 *  stress_io_uring_readv_setup()
 *	setup readv submit over io_uring
 */
static void stress_io_uring_readv_setup(
	stress_io_uring_file_t *io_uring_file,
	struct io_uring_sqe *sqe)
{
	sqe->fd = io_uring_file->fd;
	sqe->flags = 0;
	sqe->opcode = IORING_OP_READV;
	sqe->addr = (uintptr_t)io_uring_file->iovecs;
	sqe->len = io_uring_file->blocks;
	sqe->off = (uint64_t)stress_mwc8() * io_uring_file->blocks;
	sqe->user_data = (uintptr_t)io_uring_file;
}
#endif

#if defined(HAVE_IORING_OP_WRITEV)
/*
 *  stress_io_uring_writev_setup()
 *	setup writev submit over io_uring
 */
static void stress_io_uring_writev_setup(
	stress_io_uring_file_t *io_uring_file,
	struct io_uring_sqe *sqe)
{
	sqe->fd = io_uring_file->fd;
	sqe->flags = 0;
	sqe->opcode = IORING_OP_WRITEV;
	sqe->addr = (uintptr_t)io_uring_file->iovecs;
	sqe->len = io_uring_file->blocks;
	sqe->off = (uint64_t)stress_mwc8() * io_uring_file->blocks;
	sqe->user_data = (uintptr_t)io_uring_file;
}
#endif

#if defined(HAVE_IORING_OP_READ)
/*
 *  stress_io_uring_read_setup()
 *	setup read submit over io_uring
 */
static void stress_io_uring_read_setup(
	stress_io_uring_file_t *io_uring_file,
	struct io_uring_sqe *sqe)
{
	sqe->fd = io_uring_file->fd;
	sqe->flags = 0;
	sqe->opcode = IORING_OP_READ;
	sqe->addr = (uintptr_t)io_uring_file->iovecs[0].iov_base;
	sqe->len = io_uring_file->iovecs[0].iov_len;
	sqe->off = (uint64_t)stress_mwc8() * io_uring_file->blocks;
	sqe->user_data = (uintptr_t)io_uring_file;
}
#endif

#if defined(HAVE_IORING_OP_WRITE)
/*
 *  stress_io_uring_write_setup()
 *	setup write submit over io_uring
 */
static void stress_io_uring_write_setup(
	stress_io_uring_file_t *io_uring_file,
	struct io_uring_sqe *sqe)
{
	sqe->fd = io_uring_file->fd;
	sqe->flags = 0;
	sqe->opcode = IORING_OP_WRITE;
	sqe->addr = (uintptr_t)io_uring_file->iovecs[0].iov_base;
	sqe->len = io_uring_file->iovecs[0].iov_len;
	sqe->off = (uint64_t)stress_mwc8() * io_uring_file->blocks;
	sqe->user_data = (uintptr_t)io_uring_file;
}
#endif

#if defined(HAVE_IORING_OP_FSYNC)
/*
 *  stress_io_uring_fsync_setup()
 *	setup fsync submit over io_uring
 */
static void stress_io_uring_fsync_setup(
	stress_io_uring_file_t *io_uring_file,
	struct io_uring_sqe *sqe)
{
	sqe->fd = io_uring_file->fd;
	sqe->opcode = IORING_OP_FSYNC;
	sqe->len = 0;
	sqe->off = 0;
	sqe->user_data = (uintptr_t)io_uring_file;
	sqe->ioprio = 0;
	sqe->buf_index = 0;
	sqe->rw_flags = 0;
}
#endif

#if defined(HAVE_IORING_OP_NOP)
/*
 *  stress_io_uring_nop_setup()
 *	setup nop submit over io_uring
 */
static void stress_io_uring_nop_setup(
	stress_io_uring_file_t *io_uring_file,
	struct io_uring_sqe *sqe)
{
	(void)io_uring_file;

	sqe->opcode = IORING_OP_NOP;
}
#endif

#if defined(HAVE_IORING_OP_FALLOCATE)
/*
 *  stress_io_uring_fallocate_setup()
 *	setup fallocate submit over io_uring
 */
static void stress_io_uring_fallocate_setup(
	stress_io_uring_file_t *io_uring_file,
	struct io_uring_sqe *sqe)
{
	sqe->fd = io_uring_file->fd;
	sqe->opcode = IORING_OP_FALLOCATE;
	sqe->off = 0;			/* offset */
	sqe->addr = stress_mwc16();	/* length */
	sqe->len = 0;			/* mode */
	sqe->ioprio = 0;
	sqe->buf_index = 0;
	sqe->rw_flags = 0;
}
#endif

#if defined(HAVE_IORING_OP_FADVISE)
/*
 *  stress_io_uring_fadvise_setup ()
 *	setup fadvise submit over io_uring
 */
static void stress_io_uring_fadvise_setup(
	stress_io_uring_file_t *io_uring_file,
	struct io_uring_sqe *sqe)
{
	sqe->fd = io_uring_file->fd;
	sqe->opcode = IORING_OP_FADVISE;
	sqe->off = 0;			/* offset */
	sqe->len = stress_mwc16();	/* length */
#if defined(POSIX_FADV_NORMAL)
	sqe->fadvise_advice = POSIX_FADV_NORMAL;
#else
	sqe->fadvise_advice = 0;
#endif
	sqe->ioprio = 0;
	sqe->buf_index = 0;
	sqe->addr = 0;
}
#endif

#if defined(HAVE_IORING_OP_CLOSE)
/*
 *  stress_io_uring_close_setup ()
 *	setup close submit over io_uring
 */
static void stress_io_uring_close_setup(
	stress_io_uring_file_t *io_uring_file,
	struct io_uring_sqe *sqe)
{
	(void)io_uring_file;

	/* don't worry about bad fd if dup fails */
	sqe->fd = dup(fileno(stdin));
	sqe->opcode = IORING_OP_CLOSE;
	sqe->ioprio = 0;
	sqe->off = 0;
	sqe->addr = 0;
	sqe->len = 0;
	sqe->rw_flags = 0;
	sqe->buf_index = 0;
}
#endif

#if defined(HAVE_IORING_OP_MADVISE)
/*
 *  stress_io_uring_madvise_setup ()
 *	setup madvise submit over io_uring
 */
static void stress_io_uring_madvise_setup(
	stress_io_uring_file_t *io_uring_file,
	struct io_uring_sqe *sqe)
{
	sqe->fd = io_uring_file->fd;
	sqe->opcode = IORING_OP_MADVISE;
	sqe->addr = (uintptr_t)io_uring_file->iovecs[0].iov_base;
	sqe->len = 4096;
#if defined(MADV_NORMAL)
	sqe->fadvise_advice = MADV_NORMAL;
#else
	sqe->fadvise_advice = 0;
#endif
	sqe->ioprio = 0;
	sqe->buf_index = 0;
	sqe->off = 0;
}
#endif

#if defined(HAVE_IORING_OP_STATX)
/*
 *  stress_io_uring_statx_setup()
 *	setup statx submit over io_uring
 */
static void stress_io_uring_statx_setup(
	stress_io_uring_file_t *io_uring_file,
	struct io_uring_sqe *sqe)
{
	shim_statx_t statxbuf;

	sqe->opcode = IORING_OP_STATX;
	sqe->fd = io_uring_file->fd;
	sqe->addr = (uintptr_t)io_uring_file->filename;
	sqe->addr2 = (uintptr_t)&statxbuf;
	sqe->statx_flags = AT_EMPTY_PATH;
	sqe->ioprio = 0;
	sqe->buf_index = 0;
	sqe->flags = 0;
}
#endif

#if defined(HAVE_IORING_OP_SYNC_FILE_RANGE)
/*
 *  stress_io_uring_sync_file_range_setup()
 *	setup sync_file_range submit over io_uring
 */
static void stress_io_uring_sync_file_range_setup(
	stress_io_uring_file_t *io_uring_file,
	struct io_uring_sqe *sqe)
{
	sqe->opcode = IORING_OP_SYNC_FILE_RANGE;
	sqe->fd = io_uring_file->fd;
	sqe->off = stress_mwc16() & ~511UL;
	sqe->len = stress_mwc32() & ~511UL;
	sqe->flags = 0;
	sqe->addr = 0;
	sqe->ioprio = 0;
	sqe->buf_index = 0;
}
#endif

#if defined(HAVE_IORING_OP_GETXATTR)
/*
 *  stress_io_uring_getxattr_setup()
 *	setup getxattr submit over io_uring
 */
static void stress_io_uring_getxattr_setup(
	stress_io_uring_file_t *io_uring_file,
	struct io_uring_sqe *sqe)
{
	char value[1024];

	sqe->opcode = IORING_OP_STATX;
	sqe->fd = io_uring_file->fd;
	sqe->addr = (uintptr_t)io_uring_file->filename;
	sqe->addr2 = (uintptr_t)value;
	seq->len = sizeof(value);
	sqe->xattr_flags = 0;
	sqe->ioprio = 0;
	sqe->buf_index = 0;
	sqe->flags = 0;
}
#endif


static const stress_io_uring_setup_info_t stress_io_uring_setups[] = {
#if defined(HAVE_IORING_OP_READV)
	{ IORING_OP_READV,	"IORING_OP_READV", 	stress_io_uring_readv_setup },
#endif
#if defined(HAVE_IORING_OP_WRITEV)
	{ IORING_OP_WRITEV,	"IORING_OP_WRITEV",	stress_io_uring_writev_setup },
#endif
#if defined(HAVE_IORING_OP_READ)
	{ IORING_OP_READ,	"IORING_OP_READ",	stress_io_uring_read_setup },
#endif
#if defined(HAVE_IORING_OP_WRITE)
	{ IORING_OP_WRITE,	"IORING_OP_WRITE",	stress_io_uring_write_setup },
#endif
#if defined(HAVE_IORING_OP_FSYNC)
	{ IORING_OP_FSYNC,	"IORING_OP_FSYNC",	stress_io_uring_fsync_setup },
#endif
#if defined(HAVE_IORING_OP_NOP)
	{ IORING_OP_NOP,	"IORING_OP_NOP",	stress_io_uring_nop_setup },
#endif
#if defined(HAVE_IORING_OP_FALLOCATE)
	{ IORING_OP_FALLOCATE,	"IORING_OP_FALLOCATE",	stress_io_uring_fallocate_setup },
#endif
#if defined(HAVE_IORING_OP_FADVISE)
	{ IORING_OP_FADVISE,	"IORING_OP_FADVISE",	stress_io_uring_fadvise_setup },
#endif
#if defined(HAVE_IORING_OP_CLOSE)
	{ IORING_OP_CLOSE,	"IORING_OP_CLOSE",	stress_io_uring_close_setup },
#endif
#if defined(HAVE_IORING_OP_MADVISE)
	{ IORING_OP_MADVISE,	"IORING_OP_MADVISE",	stress_io_uring_madvise_setup },
#endif
#if defined(HAVE_IORING_OP_STATX)
	{ IORING_OP_STATX,	"IORING_OP_STATX",	stress_io_uring_statx_setup },
#endif
#if defined(HAVE_IORING_OP_SYNC_FILE_RANGE)
	{ IORING_OP_SYNC_FILE_RANGE, "IORING_OP_SYNC_FILE_RANGE", stress_io_uring_sync_file_range_setup },
#endif
#if defined(HAVE_IORING_OP_GETXATTR)
	{ IORING_OP_GETXATTR, "IORING_OP_GETXATTR",	stress_io_uring_getxattr_setup },
#endif
};

/*
 *  stress_io_uring_opcode_name()
 *	lookup opcode -> name
 */
static const char *stress_io_uring_opcode_name(const uint8_t opcode)
{
	size_t i;

	for (i = 0; i < SIZEOF_ARRAY(stress_io_uring_setups); i++) {
		if (stress_io_uring_setups[i].opcode == opcode)
			return stress_io_uring_setups[i].name;
	}
	return "unknown";
}


/*
 *  stress_io_uring
 *	stress asynchronous I/O
 */
static int stress_io_uring(const stress_args_t *args)
{
	int ret, rc;
	char filename[PATH_MAX];
	stress_io_uring_file_t io_uring_file;
	size_t i, j;
	const size_t blocks = 1024;
	const size_t block_size = 512;
	off_t file_size = (off_t)blocks * block_size;
	stress_io_uring_submit_t submit;
	const pid_t self = getpid();
	bool supported[SIZEOF_ARRAY(stress_io_uring_setups)];

	(void)memset(&submit, 0, sizeof(submit));
	(void)memset(&io_uring_file, 0, sizeof(io_uring_file));

	io_uring_file.file_size = file_size;
	io_uring_file.blocks = blocks;
	io_uring_file.block_size = block_size;
	io_uring_file.iovecs_sz = blocks * sizeof(*io_uring_file.iovecs);
	io_uring_file.iovecs =
		mmap(NULL, io_uring_file.iovecs_sz,
			PROT_READ | PROT_WRITE,
			MAP_SHARED | MAP_POPULATE | MAP_ANONYMOUS, -1, 0);
	if (io_uring_file.iovecs == MAP_FAILED) {
		io_uring_file.iovecs = NULL;
		pr_inf("%s: cannot allocate iovecs\n", args->name);
		return EXIT_NO_RESOURCE;
	}

	for (i = 0; (i < blocks) && (file_size > 0); i++) {
		const size_t iov_length = (file_size > (off_t)block_size) ? (size_t)block_size : (size_t)file_size;

		io_uring_file.iovecs[i].iov_len = iov_length;
		io_uring_file.iovecs[i].iov_base =
			mmap(NULL, block_size, PROT_READ | PROT_WRITE,
				MAP_SHARED | MAP_POPULATE | MAP_ANONYMOUS, -1, 0);
		if (io_uring_file.iovecs[i].iov_base == MAP_FAILED) {
			io_uring_file.iovecs[i].iov_base = NULL;
			pr_inf("%s: cannot allocate iovec iov_base\n", args->name);
			stress_io_uring_unmap_iovecs(&io_uring_file);
			return EXIT_NO_RESOURCE;
		}
		(void)memset(io_uring_file.iovecs[i].iov_base, stress_mwc8(), block_size);
		file_size -= iov_length;
	}

	ret = stress_temp_dir_mk_args(args);
	if (ret < 0) {
		stress_io_uring_unmap_iovecs(&io_uring_file);
		return exit_status(-ret);
	}

	(void)stress_temp_filename_args(args,
		filename, sizeof(filename), stress_mwc32());

	io_uring_file.filename = filename;

	rc = stress_setup_io_uring(args, &submit);
	if (rc != EXIT_SUCCESS)
		goto clean;

	if ((io_uring_file.fd = open(filename, O_CREAT | O_RDWR | O_DSYNC, S_IRUSR | S_IWUSR)) < 0) {
		rc = exit_status(errno);
		pr_fail("%s: open on %s failed, errno=%d (%s)\n",
			args->name, filename, errno, strerror(errno));
		goto clean;
	}
	(void)shim_unlink(filename);

	stress_set_proc_state(args->name, STRESS_STATE_RUN);

	/*
	 *  Assume all opcodes are supported
	 */
	for (j = 0; j < SIZEOF_ARRAY(stress_io_uring_setups); j++) {
		supported[i] = true;
	}

	rc = EXIT_SUCCESS;
	i = 0;
	do {
		for (j = 0; j < SIZEOF_ARRAY(stress_io_uring_setups); j++) {
			if (supported[j]) {
				rc = stress_io_uring_submit(args,
					stress_io_uring_setups[j].setup_func,
					&io_uring_file, &submit, &supported[j]);
				if ((rc != EXIT_SUCCESS) || !keep_stressing(args))
					break;
			}
		}

		if (i++ > 1024) {
			i = 0;
			(void)stress_read_fdinfo(self, submit.io_uring_fd);
		}
	} while (keep_stressing(args));

	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);
	(void)close(io_uring_file.fd);
clean:
	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);
	stress_close_io_uring(&submit);
	stress_io_uring_unmap_iovecs(&io_uring_file);
	(void)stress_temp_dir_rm_args(args);
	return rc;
}

stressor_info_t stress_io_uring_info = {
	.stressor = stress_io_uring,
	.class = CLASS_IO | CLASS_OS,
	.verify = VERIFY_ALWAYS,
	.help = help
};
#else
stressor_info_t stress_io_uring_info = {
	.stressor = stress_not_implemented,
	.class = CLASS_IO | CLASS_OS,
	.help = help
};
#endif
