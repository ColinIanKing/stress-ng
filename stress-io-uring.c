// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2013-2021 Canonical, Ltd.
 * Copyright (C) 2022-2023 Colin Ian King.
 *
 */
#include "stress-ng.h"
#include "core-builtin.h"
#include "io-uring.h"

#if defined(HAVE_LINUX_IO_URING_H)
#include <linux/io_uring.h>
#endif
#if defined(HAVE_SYS_XATTR_H)
#include <sys/xattr.h>
#undef HAVE_ATTR_XATTR_H
#elif defined(HAVE_ATTR_XATTR_H)
#include <attr/xattr.h>
#endif

#if !defined(O_DSYNC)
#define O_DSYNC		(0)
#endif

static const stress_help_t help[] = {
	{ NULL,	"io-uring N",		"start N workers that issue io-uring I/O requests" },
	{ NULL,	"io-uring-ops N",	"stop after N bogo io-uring I/O requests" },
	{ NULL,	NULL,			NULL }
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
     defined(HAVE_IORING_OP_SETXATTR) || \
     defined(HAVE_IORING_OP_GETXATTR) || \
     defined(HAVE_IORING_OP_SYNC_FILE_RANGE))

/*
 *  io uring file info
 */
typedef struct {
	int fd;			/* file descriptor */
	int fd_at;		/* file path descriptor */
	int fd_dup;		/* file descriptor to dup */
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
	size_t sqes_entries;
} stress_io_uring_submit_t;

typedef struct {
	size_t  index;
	uint8_t opcode;
	bool	supported;
} stress_io_uring_user_data_t;

typedef void (*stress_io_uring_setup)(const stress_io_uring_file_t *io_uring_file, struct io_uring_sqe *sqe, const void *extra);

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
static inline int shim_io_uring_setup(unsigned entries, struct io_uring_params *p)
{
	return (int)syscall(__NR_io_uring_setup, entries, p);
}

/*
 *  shim_io_uring_enter
 *	wrapper for io_uring_enter()
 */
static inline int shim_io_uring_enter(
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

	(void)shim_memset(&p, 0, sizeof(p));
	/*
	 *  16 is plenty, with too many we end up with lots of cache
	 *  misses, with too few we end up with ring filling. This
	 *  seems to be a good fit with the set of requests being
	 *  issue by this stressor
	 */
	submit->io_uring_fd = shim_io_uring_setup(16, &p);
	if (submit->io_uring_fd < 0) {
		if (errno == ENOSYS) {
			pr_inf_skip("%s: io-uring not supported by the kernel, skipping stressor\n",
				args->name);
			return EXIT_NOT_IMPLEMENTED;
		}
		if (errno == ENOMEM) {
			pr_inf_skip("%s: io-uring setup failed, out of memory, skipping stressor\n",
				args->name);
			return EXIT_NO_RESOURCE;
		}
		pr_fail("%s: io-uring setup failed, errno=%d (%s)\n",
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
		pr_inf_skip("%s: could not mmap submission queue buffer, "
			"errno=%d (%s), skipping stressor\n",
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
			pr_inf_skip("%s: could not mmap completion queue buffer, "
				"errno=%d (%s), skipping stressor\n",
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

	submit->sqes_entries = p.sq_entries;
	submit->sqes_size = p.sq_entries * sizeof(struct io_uring_sqe);
	submit->sqes_mmap = mmap(NULL, submit->sqes_size,
			PROT_READ | PROT_WRITE, MAP_SHARED | MAP_POPULATE,
			submit->io_uring_fd, IORING_OFF_SQES);
	if (submit->sqes_mmap == MAP_FAILED) {
		pr_inf_skip("%s: count not mmap submission queue buffer, "
			"errno=%d (%s), skipping stressor\n",
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
	stress_io_uring_submit_t *submit)
{
	stress_uring_io_cq_ring_t *cring = &submit->cq_ring;
	struct io_uring_cqe *cqe;
	unsigned head = *cring->head;
	int ret = EXIT_SUCCESS;

	for (;;) {
		stress_asm_mb();

		/* Empty? */
		if (head == *cring->tail)
			break;

		cqe = &cring->cqes[head & *submit->cq_ring.ring_mask];
		stress_io_uring_user_data_t *const user_data =
			(stress_io_uring_user_data_t *)(uintptr_t)cqe->user_data;
		if (cqe->res < 0) {
			int err;

			err = abs(cqe->res);
			/* Silently ignore some completion errors */
			if ((err == EOPNOTSUPP) || (err == ENOTDIR)) {
				user_data->supported = false;
			} else  {
				switch (err) {
				/* Silently ignore some errors */
				case ENOSPC:
				case EFBIG:
					goto next_head;
				case ENOENT:
					if (user_data->opcode == IORING_OP_ASYNC_CANCEL)
						goto next_head;
					break;
				case EINVAL:
					if (user_data->opcode == IORING_OP_FALLOCATE)
						goto next_head;
					break;
#if defined(HAVE_IORING_OP_GETXATTR) &&	\
    defined(XATTR_CREATE)
				case ENODATA:
					if (user_data->opcode == IORING_OP_GETXATTR)
						goto next_head;
					break;
#endif
#if defined(HAVE_IORING_OP_SETXATTR) &&	\
    defined(XATTR_CREATE)
				case EEXIST:
					if (user_data->opcode == IORING_OP_SETXATTR)
						goto next_head;
					break;
#endif
				default:
					break;
				}
				pr_fail("%s: completion opcode 0x%2.2x (%s), error=%d (%s)\n",
					args->name, user_data->opcode,
					stress_io_uring_opcode_name(user_data->opcode),
					err, strerror(err));
				ret = EXIT_FAILURE;
			}
		}
next_head:
		head++;
	}

	*cring->head = head;
	stress_asm_mb();

	return ret;
}

/*
 *  stress_io_uring_submit()
 *	submit an io-uring opcode
 */
static int stress_io_uring_submit(
	const stress_args_t *args,
	const stress_io_uring_setup setup_func,
	const stress_io_uring_file_t *io_uring_file,
	stress_io_uring_submit_t *submit,
	stress_io_uring_user_data_t *user_data,
	const void *extra_data)
{
	stress_uring_io_sq_ring_t *sring = &submit->sq_ring;
	unsigned index = 0, tail = 0, next_tail = 0;
	struct io_uring_sqe *sqe;
	int ret;

	next_tail = tail = *sring->tail;
	next_tail++;
	stress_asm_mb();
	index = tail & *submit->sq_ring.ring_mask;
	sqe = &submit->sqes_mmap[index];
	(void)shim_memset(sqe, 0, sizeof(*sqe));

	setup_func(io_uring_file, sqe, extra_data);
	/* Save opcode for later completion error reporting */
	sqe->user_data = (uint64_t)(uintptr_t)user_data;

	sring->array[index] = index;
	tail = next_tail;
	if (*sring->tail != tail) {
		stress_asm_mb();
		*sring->tail = tail;
		stress_asm_mb();
	} else {
		return EXIT_FAILURE;
	}

retry:
	ret = shim_io_uring_enter(submit->io_uring_fd, 1,
		1, IORING_ENTER_GETEVENTS);
	if (ret < 0) {
		if (errno == EBUSY){
			stress_io_uring_complete(args, submit);
			goto retry;
		}
		/* Silently ignore ENOSPC or cancel opcode failures */
		if ((errno == ENOSPC) || (sqe->opcode == IORING_OP_ASYNC_CANCEL))
			return EXIT_SUCCESS;
		pr_fail("%s: io_uring_enter failed, opcode=%d (%s), errno=%d (%s)\n",
			args->name, sqe->opcode,
			stress_io_uring_opcode_name(sqe->opcode),
			errno, strerror(errno));
		if (errno == EOPNOTSUPP)
			user_data->supported = false;
		return EXIT_FAILURE;
	}
	stress_bogo_inc(args);
	return EXIT_SUCCESS;
}

#if defined(HAVE_IORING_OP_ASYNC_CANCEL)
/*
 *  stress_io_uring_cancel_setup()
 *	setup cancel submit over io_uring
 */
static void stress_io_uring_async_cancel_setup(
	const stress_io_uring_file_t *io_uring_file,
	struct io_uring_sqe *sqe,
	const void *extra_info)
{
	(void)io_uring_file;

	const struct io_uring_sqe *sqe_to_cancel =
		(const struct io_uring_sqe *)extra_info;

	sqe->fd = sqe_to_cancel->fd;
	sqe->flags = 2;
	sqe->opcode = IORING_OP_ASYNC_CANCEL;
	sqe->addr = sqe_to_cancel->addr;
	sqe->off = 0;
	sqe->len = 0;
	sqe->splice_fd_in = 0;
}

/*
 *  stress_io_uring_cancel_rdwr()
 *	try to cancel pending read/writes
 */
static void stress_io_uring_cancel_rdwr(
	const stress_args_t *args,
	const stress_io_uring_file_t *io_uring_file,
	stress_io_uring_submit_t *submit)
{
	size_t i;
	stress_io_uring_user_data_t user_data;

	user_data.supported = true;
	user_data.index = -1;
	user_data.opcode = IORING_OP_ASYNC_CANCEL;

	for (i = 0; i < submit->sqes_entries; i++) {
		struct io_uring_sqe *sqe_to_cancel = &submit->sqes_mmap[i];

		if (!sqe_to_cancel->addr)
			continue;

		switch (sqe_to_cancel->opcode) {
		case IORING_OP_READ:
		case IORING_OP_READV:
		case IORING_OP_WRITE:
		case IORING_OP_WRITEV:
			(void)stress_io_uring_submit(args, stress_io_uring_async_cancel_setup,
				io_uring_file, submit, &user_data, (void *)sqe_to_cancel);
			break;
		default:
			break;
		}
	}
	(void)stress_io_uring_complete(args, submit);
}
#endif

#if defined(HAVE_IORING_OP_READV)
/*
 *  stress_io_uring_readv_setup()
 *	setup readv submit over io_uring
 */
static void stress_io_uring_readv_setup(
	const stress_io_uring_file_t *io_uring_file,
	struct io_uring_sqe *sqe,
	const void *extra_info)
{
	(void)extra_info;

	sqe->fd = io_uring_file->fd;
	sqe->flags = 0;
	sqe->opcode = IORING_OP_READV;
	sqe->addr = (uintptr_t)io_uring_file->iovecs;
	sqe->len = io_uring_file->blocks;
	sqe->off = (uint64_t)stress_mwc8() * io_uring_file->blocks;
}
#endif

#if defined(HAVE_IORING_OP_WRITEV)
/*
 *  stress_io_uring_writev_setup()
 *	setup writev submit over io_uring
 */
static void stress_io_uring_writev_setup(
	const stress_io_uring_file_t *io_uring_file,
	struct io_uring_sqe *sqe,
	const void *extra_info)
{
	(void)extra_info;

	sqe->fd = io_uring_file->fd;
	sqe->flags = 0;
	sqe->opcode = IORING_OP_WRITEV;
	sqe->addr = (uintptr_t)io_uring_file->iovecs;
	sqe->len = io_uring_file->blocks;
	sqe->off = (uint64_t)stress_mwc8() * io_uring_file->blocks;
}
#endif

#if defined(HAVE_IORING_OP_READ)
/*
 *  stress_io_uring_read_setup()
 *	setup read submit over io_uring
 */
static void stress_io_uring_read_setup(
	const stress_io_uring_file_t *io_uring_file,
	struct io_uring_sqe *sqe,
	const void *extra_info)
{
	(void)extra_info;

	sqe->fd = io_uring_file->fd;
	sqe->flags = 0;
	sqe->opcode = IORING_OP_READ;
	sqe->addr = (uintptr_t)io_uring_file->iovecs[0].iov_base;
	sqe->len = io_uring_file->iovecs[0].iov_len;
	sqe->off = (uint64_t)stress_mwc8() * io_uring_file->blocks;
}
#endif

#if defined(HAVE_IORING_OP_WRITE)
/*
 *  stress_io_uring_write_setup()
 *	setup write submit over io_uring
 */
static void stress_io_uring_write_setup(
	const stress_io_uring_file_t *io_uring_file,
	struct io_uring_sqe *sqe,
	const void *extra_info)
{
	(void)extra_info;

	sqe->fd = io_uring_file->fd;
	sqe->flags = 0;
	sqe->opcode = IORING_OP_WRITE;
	sqe->addr = (uintptr_t)io_uring_file->iovecs[0].iov_base;
	sqe->len = io_uring_file->iovecs[0].iov_len;
	sqe->off = (uint64_t)stress_mwc8() * io_uring_file->blocks;
}
#endif

#if defined(HAVE_IORING_OP_FSYNC)
/*
 *  stress_io_uring_fsync_setup()
 *	setup fsync submit over io_uring
 */
static void stress_io_uring_fsync_setup(
	const stress_io_uring_file_t *io_uring_file,
	struct io_uring_sqe *sqe,
	const void *extra_info)
{
	(void)extra_info;

	sqe->fd = io_uring_file->fd;
	sqe->opcode = IORING_OP_FSYNC;
	sqe->len = 0;
	sqe->off = 0;
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
	const stress_io_uring_file_t *io_uring_file,
	struct io_uring_sqe *sqe,
	const void *extra_info)
{
	(void)io_uring_file;
	(void)extra_info;

	sqe->opcode = IORING_OP_NOP;
}
#endif

#if defined(HAVE_IORING_OP_FALLOCATE)
/*
 *  stress_io_uring_fallocate_setup()
 *	setup fallocate submit over io_uring
 */
static void stress_io_uring_fallocate_setup(
	const stress_io_uring_file_t *io_uring_file,
	struct io_uring_sqe *sqe,
	const void *extra_info)
{
	(void)extra_info;

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
	const stress_io_uring_file_t *io_uring_file,
	struct io_uring_sqe *sqe,
	const void *extra_info)
{
	(void)extra_info;

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
	const stress_io_uring_file_t *io_uring_file,
	struct io_uring_sqe *sqe,
	const void *extra_info)
{
	(void)io_uring_file;
	(void)extra_info;

	/* don't worry about bad fd if dup fails */
	sqe->fd = dup(io_uring_file->fd_dup);
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
	const stress_io_uring_file_t *io_uring_file,
	struct io_uring_sqe *sqe,
	const void *extra_info)
{
	(void)extra_info;

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
	const stress_io_uring_file_t *io_uring_file,
	struct io_uring_sqe *sqe,
	const void *extra_info)
{
	(void)io_uring_file;
	(void)sqe;
	(void)extra_info;

#if defined(STATX_SIZE)
	if (io_uring_file->fd_at >= 0) {
		static shim_statx_t statxbuf;

		sqe->opcode = IORING_OP_STATX;
		sqe->fd = io_uring_file->fd_at;
		sqe->addr = (uintptr_t)"";
		sqe->addr2 = (uintptr_t)&statxbuf;
		sqe->statx_flags = AT_EMPTY_PATH;
		sqe->ioprio = 0;
		sqe->buf_index = 0;
		sqe->flags = 0;
		sqe->len = STATX_SIZE;
	}
#endif
}
#endif

#if defined(HAVE_IORING_OP_SYNC_FILE_RANGE)
/*
 *  stress_io_uring_sync_file_range_setup()
 *	setup sync_file_range submit over io_uring
 */
static void stress_io_uring_sync_file_range_setup(
	const stress_io_uring_file_t *io_uring_file,
	struct io_uring_sqe *sqe,
	const void *extra_info)
{
	(void)extra_info;

	sqe->fd = io_uring_file->fd;
	sqe->off = stress_mwc16() & ~511UL;
	sqe->len = stress_mwc32() & ~511UL;
	sqe->flags = 0;
	sqe->addr = 0;
	sqe->ioprio = 0;
	sqe->buf_index = 0;
}
#endif

#if defined(HAVE_IORING_OP_SETXATTR) &&	\
    defined(XATTR_CREATE)
/*
 *  stress_io_uring_setxattr_setup()
 *	setup setxattr submit over io_uring
 */
static void stress_io_uring_setxattr_setup(
	const stress_io_uring_file_t *io_uring_file,
	struct io_uring_sqe *sqe,
	const void *extra_info)
{
	(void)io_uring_file;
	(void)extra_info;

	static char attr_value[] = "ioring-xattr-data";

	sqe->opcode = IORING_OP_SETXATTR;
	sqe->fd = 0;
	sqe->off = (uintptr_t)attr_value;
	sqe->len = sizeof(attr_value);
	sqe->flags = 0;
	sqe->addr = (uintptr_t)"user.var_test";
	sqe->ioprio = 0;
	sqe->rw_flags = 0;
	sqe->buf_index = 0;
	sqe->addr3 = (uintptr_t)io_uring_file->filename;
        sqe->xattr_flags = XATTR_CREATE;
}
#endif


#if defined(HAVE_IORING_OP_GETXATTR) &&	\
    defined(XATTR_CREATE)
/*
 *  stress_io_uring_getxattr_setup()
 *	setup getxattr submit over io_uring
 */
static void stress_io_uring_getxattr_setup(
	const stress_io_uring_file_t *io_uring_file,
	struct io_uring_sqe *sqe,
	const void *extra_info)
{
	(void)io_uring_file;
	(void)extra_info;

	static char attr_value[128];

	sqe->opcode = IORING_OP_GETXATTR;
	sqe->fd = 0;
	sqe->off = (uintptr_t)attr_value;
	sqe->len = sizeof(attr_value);
	sqe->flags = 0;
	sqe->addr = (uintptr_t)"user.var_test";
	sqe->ioprio = 0;
	sqe->rw_flags = 0;
	sqe->buf_index = 0;
	sqe->addr3 = (uintptr_t)io_uring_file->filename;
        sqe->xattr_flags = 0;
}
#endif


/*
 *  We have some duplications here because we want to perform more than
 *  one of these io-urion ops per round before we do a completion so we
 *  can add more activity onto the ring for a bit more activity/stress.
 */
static const stress_io_uring_setup_info_t stress_io_uring_setups[] = {
#if defined(HAVE_IORING_OP_READV)
	{ IORING_OP_READV,	"IORING_OP_READV", 	stress_io_uring_readv_setup },
	{ IORING_OP_READV,	"IORING_OP_READV", 	stress_io_uring_readv_setup },
#endif
#if defined(HAVE_IORING_OP_WRITEV)
	{ IORING_OP_WRITEV,	"IORING_OP_WRITEV",	stress_io_uring_writev_setup },
#endif
#if defined(HAVE_IORING_OP_READ)
	{ IORING_OP_READ,	"IORING_OP_READ",	stress_io_uring_read_setup },
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
	{ IORING_OP_NOP,	"IORING_OP_NOP",	stress_io_uring_nop_setup },
	{ IORING_OP_NOP,	"IORING_OP_NOP",	stress_io_uring_nop_setup },
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
	{ IORING_OP_STATX,	"IORING_OP_STATX",	stress_io_uring_statx_setup },
#endif
#if defined(HAVE_IORING_OP_SYNC_FILE_RANGE)
	{ IORING_OP_SYNC_FILE_RANGE, "IORING_OP_SYNC_FILE_RANGE", stress_io_uring_sync_file_range_setup },
#endif
#if defined(HAVE_IORING_OP_SETXATTR) &&	\
    defined(XATTR_CREATE)
	{ IORING_OP_SETXATTR,	"IORING_OP_SETXATTR",	stress_io_uring_setxattr_setup },
#endif
#if defined(HAVE_IORING_OP_GETXATTR) && \
    defined(XATTR_CREATE)
	{ IORING_OP_GETXATTR,	"IORING_OP_GETXATTR",	stress_io_uring_getxattr_setup },
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
static int stress_io_uring_child(const stress_args_t *args, void *context)
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
	stress_io_uring_user_data_t user_data[SIZEOF_ARRAY(stress_io_uring_setups)];

	(void)context;

	(void)shim_memset(&submit, 0, sizeof(submit));
	(void)shim_memset(&io_uring_file, 0, sizeof(io_uring_file));

	io_uring_file.fd_dup = fileno(stdin);
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
		pr_inf_skip("%s: cannot mmap iovecs, errno=%d (%s), "
				"skipping stressor\n", args->name,
				errno, strerror(errno));
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
			pr_inf_skip("%s: cannot mmap allocate iovec iov_base, errno=%d (%s), "
				"skipping stressor\n", args->name,
				errno, strerror(errno));
			stress_io_uring_unmap_iovecs(&io_uring_file);
			return EXIT_NO_RESOURCE;
		}
		(void)shim_memset(io_uring_file.iovecs[i].iov_base, stress_mwc8(), block_size);
		file_size -= iov_length;
	}

	ret = stress_temp_dir_mk_args(args);
	if (ret < 0) {
		stress_io_uring_unmap_iovecs(&io_uring_file);
		return stress_exit_status(-ret);
	}

	(void)stress_temp_filename_args(args,
		filename, sizeof(filename), stress_mwc32());

	io_uring_file.filename = filename;

	rc = stress_setup_io_uring(args, &submit);
	if (rc != EXIT_SUCCESS)
		goto clean;

	if ((io_uring_file.fd = open(filename, O_CREAT | O_RDWR | O_DSYNC, S_IRUSR | S_IWUSR)) < 0) {
		rc = stress_exit_status(errno);
		pr_fail("%s: open on %s failed, errno=%d (%s)\n",
			args->name, filename, errno, strerror(errno));
		goto clean;
	}
#if defined(O_PATH)
	io_uring_file.fd_at = open(filename, O_PATH);
#else
	io_uring_file.fd_at = -1;
#endif
	stress_file_rw_hint_short(io_uring_file.fd);

	stress_set_proc_state(args->name, STRESS_STATE_RUN);

	/*
	 *  Assume all opcodes are supported
	 */
	for (j = 0; j < SIZEOF_ARRAY(stress_io_uring_setups); j++) {
		user_data[j].supported = true;
		user_data[j].index = j;
		user_data[j].opcode = stress_io_uring_setups[j].opcode;
	}

	rc = EXIT_SUCCESS;
	i = 0;
	do {
		for (j = 0; (j < SIZEOF_ARRAY(stress_io_uring_setups)) && stress_continue_flag(); j++) {
			if (user_data[j].supported) {
				rc = stress_io_uring_submit(args,
					stress_io_uring_setups[j].setup_func,
					&io_uring_file, &submit, &user_data[j], NULL);
				if ((rc != EXIT_SUCCESS) || !stress_continue(args))
					break;
			}
		}
		stress_io_uring_complete(args, &submit);

		if (i++ >= 4096) {
			i = 0;
			(void)stress_read_fdinfo(self, submit.io_uring_fd);
		}
	} while (stress_continue(args));

	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);
	pr_dbg("%s: submits completed, closing uring and unlinking file\n", args->name);
#if defined(HAVE_IORING_OP_ASYNC_CANCEL)
	stress_io_uring_cancel_rdwr(args, &io_uring_file, &submit);
#endif
	(void)close(io_uring_file.fd);
clean:
	if (io_uring_file.fd_at >= 0)
		(void)close(io_uring_file.fd_at);
	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);
	stress_close_io_uring(&submit);
	stress_io_uring_unmap_iovecs(&io_uring_file);
	(void)shim_unlink(filename);
	(void)stress_temp_dir_rm_args(args);
	return rc;
}

/*
 *  stress_io_uring
 *      stress asynchronous I/O
 */
static int stress_io_uring(const stress_args_t *args)
{
	return stress_oomable_child(args, NULL, stress_io_uring_child, STRESS_OOMABLE_NORMAL);
}

stressor_info_t stress_io_uring_info = {
	.stressor = stress_io_uring,
	.class = CLASS_IO | CLASS_OS,
	.verify = VERIFY_ALWAYS,
	.help = help
};
#else
stressor_info_t stress_io_uring_info = {
	.stressor = stress_unimplemented,
	.class = CLASS_IO | CLASS_OS,
	.verify = VERIFY_ALWAYS,
	.help = help,
	.unimplemented_reason = "built without linux/io_uring.h or syscall() support"
};
#endif
