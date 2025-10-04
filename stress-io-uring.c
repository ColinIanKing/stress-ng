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
#include "core-builtin.h"
#include "core-mmap.h"
#include "core-out-of-memory.h"
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

#define MIN_IO_URING_ENTRIES	(1)
#define MAX_IO_URING_ENTRIES	(16384)

static const stress_help_t help[] = {
	{ NULL,	"io-uring N",		"start N workers that issue io-uring I/O requests" },
	{ NULL, "io-uring-entries N",	"specify number if io-uring ring entries" },
	{ NULL,	"io-uring-ops N",	"stop after N bogo io-uring I/O requests" },
	{ NULL,	"io-uring-rand",	"enable randomized io-uring I/O request ordering" },
	{ NULL,	NULL,			NULL }
};

static const stress_opt_t opts[] = {
	{ OPT_io_uring_entries, "io-uring-entries", TYPE_ID_UINT32, MIN_IO_URING_ENTRIES, MAX_IO_URING_ENTRIES, NULL },
	{ OPT_io_uring_rand,    "io-uring-rand",    TYPE_ID_BOOL,   0, 1, NULL },
	END_OPT,
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
     defined(HAVE_IORING_OP_SYNC_FILE_RANGE) ||	\
     defined(HAVE_IORING_OP_FTRUNCATE))

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

static bool io_uring_rand;

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
	stress_args_t *args,
	const uint32_t io_uring_entries,
	stress_io_uring_submit_t *submit)
{
	stress_uring_io_sq_ring_t *sring = &submit->sq_ring;
	stress_uring_io_cq_ring_t *cring = &submit->cq_ring;
	struct io_uring_params p;

	(void)shim_memset(&p, 0, sizeof(p));
#if defined(IORING_SETUP_COOP_TASKRUN) && 	\
    defined(IORING_SETUP_DEFER_TASKRUN) &&	\
    defined(IORING_SETUP_SINGLE_ISSUER)
	p.flags = IORING_SETUP_COOP_TASKRUN | IORING_SETUP_DEFER_TASKRUN | IORING_SETUP_SINGLE_ISSUER;
#endif

	/*
	 *  16 is plenty, with too many we end up with lots of cache
	 *  misses, with too few we end up with ring filling. This
	 *  seems to be a good fit with the set of requests being
	 *  issue by this stressor
	 */
	submit->io_uring_fd = shim_io_uring_setup(io_uring_entries, &p);
	if (submit->io_uring_fd < 0) {
		switch (errno) {
		case EPERM:
			pr_inf_skip("%s: io-uring not permitted, skipping stressor\n", args->name);
			return EXIT_NOT_IMPLEMENTED;
		case ENOSYS:
			pr_inf_skip("%s: io-uring not supported by the kernel, skipping stressor\n", args->name);
			return EXIT_NOT_IMPLEMENTED;
		case ENOMEM:
			pr_inf_skip("%s: io-uring setup failed, out of memory, skipping stressor\n", args->name);
			return EXIT_NO_RESOURCE;
		case EINVAL:
			pr_inf_skip("%s: io-uring failed, EINVAL, possibly %"
				PRIu32 " io-uring-entries too large, "
				"skipping stressor\n",
				args->name, io_uring_entries);
			return EXIT_NO_RESOURCE;
		default:
			break;
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

	submit->sq_mmap = stress_mmap_populate(NULL, submit->sq_size,
		PROT_READ | PROT_WRITE, MAP_SHARED ,
		submit->io_uring_fd, IORING_OFF_SQ_RING);
	if (submit->sq_mmap == MAP_FAILED) {
		pr_inf_skip("%s: could not mmap submission queue buffer%s, "
			"errno=%d (%s), skipping stressor\n",
			args->name, stress_get_memfree_str(),
			errno, strerror(errno));
		return EXIT_NO_RESOURCE;
	}

	if (p.features & IORING_FEAT_SINGLE_MMAP) {
		submit->cq_mmap = submit->sq_mmap;
	} else {
		submit->cq_mmap = stress_mmap_populate(NULL, submit->cq_size,
				PROT_READ | PROT_WRITE,
				MAP_SHARED, submit->io_uring_fd, IORING_OFF_CQ_RING);
		if (submit->cq_mmap == MAP_FAILED) {
			pr_inf_skip("%s: could not mmap completion queue buffer%s, "
				"errno=%d (%s), skipping stressor\n",
				args->name, stress_get_memfree_str(),
				errno, strerror(errno));
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
	submit->sqes_mmap = stress_mmap_populate(NULL, submit->sqes_size,
			PROT_READ | PROT_WRITE, MAP_SHARED,
			submit->io_uring_fd, IORING_OFF_SQES);
	if (submit->sqes_mmap == MAP_FAILED) {
		pr_inf_skip("%s: count not mmap submission queue buffer%s, "
			"errno=%d (%s), skipping stressor\n",
			args->name, stress_get_memfree_str(),
			errno, strerror(errno));
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
		(void)munmap((void *)submit->sqes_mmap, submit->sqes_size);
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
	stress_args_t *args,
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
				case EINVAL:
					goto next_head;
				case ENOENT:
#if defined(HAVE_IORING_OP_ASYNC_CANCEL)
					if (user_data->opcode == IORING_OP_ASYNC_CANCEL)
						goto next_head;
#endif
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
	stress_args_t *args,
	const stress_io_uring_setup setup_func,
	const stress_io_uring_file_t *io_uring_file,
	stress_io_uring_submit_t *submit,
	stress_io_uring_user_data_t *user_data,
	const void *extra_data)
{
	stress_uring_io_sq_ring_t *sring = &submit->sq_ring;
	unsigned idx = 0, tail = 0, next_tail = 0;
	struct io_uring_sqe *sqe;
	int ret;

	next_tail = tail = *sring->tail;
	next_tail++;
	stress_asm_mb();
	idx = tail & *submit->sq_ring.ring_mask;
	sqe = &submit->sqes_mmap[idx];
	(void)shim_memset(sqe, 0, sizeof(*sqe));

	setup_func(io_uring_file, sqe, extra_data);
	/* Save opcode for later completion error reporting */
	sqe->user_data = (uint64_t)(uintptr_t)user_data;

	sring->array[idx] = idx;
	tail = next_tail;
	if (*sring->tail != tail) {
		stress_asm_mb();
		*sring->tail = tail;
		stress_asm_mb();
	} else {
		return EXIT_FAILURE;
	}

retry:
	if (UNLIKELY(!stress_continue(args)))
		return EXIT_NO_RESOURCE;
	ret = shim_io_uring_enter(submit->io_uring_fd, 1,
		1, IORING_ENTER_GETEVENTS);
	if (UNLIKELY(ret < 0)) {
		if (errno == EBUSY) {
			stress_io_uring_complete(args, submit);
			goto retry;
		}
		/* Silently ignore ENOSPC or cancel opcode failures */
#if defined(HAVE_IORING_OP_ASYNC_CANCEL)
		if ((errno == ENOSPC) || (sqe->opcode == IORING_OP_ASYNC_CANCEL))
			return EXIT_SUCCESS;
#else
		if (errno == ENOSPC)
			return EXIT_SUCCESS;
#endif
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
static void OPTIMIZE3 stress_io_uring_async_cancel_setup(
	const stress_io_uring_file_t *io_uring_file,
	struct io_uring_sqe *sqe,
	const void *extra_info)
{
	(void)io_uring_file;

	const struct io_uring_sqe *sqe_to_cancel =
		(const struct io_uring_sqe *)extra_info;

	(void)shim_memset(sqe, 0, sizeof(*sqe));
	sqe->fd = sqe_to_cancel->fd;
	sqe->flags = 2;
	sqe->opcode = IORING_OP_ASYNC_CANCEL;
	sqe->addr = sqe_to_cancel->addr;
}

/*
 *  stress_io_uring_cancel_rdwr()
 *	try to cancel pending read/writes
 */
static void stress_io_uring_cancel_rdwr(
	stress_args_t *args,
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
static void OPTIMIZE3 stress_io_uring_readv_setup(
	const stress_io_uring_file_t *io_uring_file,
	struct io_uring_sqe *sqe,
	const void *extra_info)
{
	(void)extra_info;

	(void)shim_memset(sqe, 0, sizeof(*sqe));
	sqe->fd = io_uring_file->fd;
	sqe->opcode = IORING_OP_READV;
	sqe->addr = (uintptr_t)io_uring_file->iovecs;
	sqe->len = io_uring_file->blocks;
	sqe->off = (uint64_t)io_uring_rand ?
			(stress_mwc8() * io_uring_file->blocks) : 0;
}
#endif

#if defined(HAVE_IORING_OP_WRITEV)
/*
 *  stress_io_uring_writev_setup()
 *	setup writev submit over io_uring
 */
static void OPTIMIZE3 stress_io_uring_writev_setup(
	const stress_io_uring_file_t *io_uring_file,
	struct io_uring_sqe *sqe,
	const void *extra_info)
{
	(void)extra_info;

	(void)shim_memset(sqe, 0, sizeof(*sqe));
	sqe->fd = io_uring_file->fd;
	sqe->opcode = IORING_OP_WRITEV;
	sqe->addr = (uintptr_t)io_uring_file->iovecs;
	sqe->len = io_uring_file->blocks;
	sqe->off = (uint64_t)io_uring_rand ?
			(stress_mwc8() * io_uring_file->blocks) : 0;
}
#endif

#if defined(HAVE_IORING_OP_READ)
/*
 *  stress_io_uring_read_setup()
 *	setup read submit over io_uring
 */
static void OPTIMIZE3 stress_io_uring_read_setup(
	const stress_io_uring_file_t *io_uring_file,
	struct io_uring_sqe *sqe,
	const void *extra_info)
{
	(void)extra_info;

	(void)shim_memset(sqe, 0, sizeof(*sqe));
	sqe->fd = io_uring_file->fd;
	sqe->opcode = IORING_OP_READ;
	sqe->addr = (uintptr_t)io_uring_file->iovecs[0].iov_base;
	sqe->len = io_uring_file->iovecs[0].iov_len;
	sqe->off = (uint64_t)io_uring_rand ?
			(stress_mwc8() * io_uring_file->blocks) : 0;
}
#endif

#if defined(HAVE_IORING_OP_WRITE)
/*
 *  stress_io_uring_write_setup()
 *	setup write submit over io_uring
 */
static void OPTIMIZE3 stress_io_uring_write_setup(
	const stress_io_uring_file_t *io_uring_file,
	struct io_uring_sqe *sqe,
	const void *extra_info)
{
	(void)extra_info;

	(void)shim_memset(sqe, 0, sizeof(*sqe));
	sqe->fd = io_uring_file->fd;
	sqe->opcode = IORING_OP_WRITE;
	sqe->addr = (uintptr_t)io_uring_file->iovecs[0].iov_base;
	sqe->len = io_uring_file->iovecs[0].iov_len;
	sqe->off = (uint64_t)io_uring_rand ?
			(stress_mwc8() * io_uring_file->blocks) : 0;
}
#endif

#if defined(HAVE_IORING_OP_FSYNC)
/*
 *  stress_io_uring_fsync_setup()
 *	setup fsync submit over io_uring
 */
static void OPTIMIZE3 stress_io_uring_fsync_setup(
	const stress_io_uring_file_t *io_uring_file,
	struct io_uring_sqe *sqe,
	const void *extra_info)
{
	(void)extra_info;

	(void)shim_memset(sqe, 0, sizeof(*sqe));
	sqe->fd = io_uring_file->fd;
	sqe->opcode = IORING_OP_FSYNC;
}
#endif

#if defined(HAVE_IORING_OP_NOP)
/*
 *  stress_io_uring_nop_setup()
 *	setup nop submit over io_uring
 */
static void OPTIMIZE3 stress_io_uring_nop_setup(
	const stress_io_uring_file_t *io_uring_file,
	struct io_uring_sqe *sqe,
	const void *extra_info)
{
	(void)io_uring_file;
	(void)extra_info;

	(void)shim_memset(sqe, 0, sizeof(*sqe));
	sqe->opcode = IORING_OP_NOP;
}
#endif

#if defined(HAVE_IORING_OP_FALLOCATE)
/*
 *  stress_io_uring_fallocate_setup()
 *	setup fallocate submit over io_uring
 */
static void OPTIMIZE3 stress_io_uring_fallocate_setup(
	const stress_io_uring_file_t *io_uring_file,
	struct io_uring_sqe *sqe,
	const void *extra_info)
{
	(void)extra_info;

	(void)shim_memset(sqe, 0, sizeof(*sqe));
	sqe->fd = io_uring_file->fd;
	sqe->opcode = IORING_OP_FALLOCATE;
	sqe->addr = stress_mwc16();	/* length */
}
#endif

#if defined(HAVE_IORING_OP_FADVISE)
/*
 *  stress_io_uring_fadvise_setup()
 *	setup fadvise submit over io_uring
 */
static void OPTIMIZE3 stress_io_uring_fadvise_setup(
	const stress_io_uring_file_t *io_uring_file,
	struct io_uring_sqe *sqe,
	const void *extra_info)
{
	(void)extra_info;

	(void)shim_memset(sqe, 0, sizeof(*sqe));
	sqe->fd = io_uring_file->fd;
	sqe->opcode = IORING_OP_FADVISE;
	sqe->len = io_uring_rand ?  stress_mwc16(): 1024;
#if defined(POSIX_FADV_NORMAL)
	sqe->fadvise_advice = POSIX_FADV_NORMAL;
#endif
}
#endif

#if defined(HAVE_IORING_OP_CLOSE)
/*
 *  stress_io_uring_close_setup()
 *	setup close submit over io_uring
 */
static void OPTIMIZE3 stress_io_uring_close_setup(
	const stress_io_uring_file_t *io_uring_file,
	struct io_uring_sqe *sqe,
	const void *extra_info)
{
	(void)io_uring_file;
	(void)extra_info;

	(void)shim_memset(sqe, 0, sizeof(*sqe));
	/* don't worry about bad fd if dup fails */
	sqe->fd = dup(io_uring_file->fd_dup);
	sqe->opcode = IORING_OP_CLOSE;
}
#endif

#if defined(HAVE_IORING_OP_MADVISE)
/*
 *  stress_io_uring_madvise_setup ()
 *	setup madvise submit over io_uring
 */
static void OPTIMIZE3 stress_io_uring_madvise_setup(
	const stress_io_uring_file_t *io_uring_file,
	struct io_uring_sqe *sqe,
	const void *extra_info)
{
	(void)extra_info;

	(void)shim_memset(sqe, 0, sizeof(*sqe));
	sqe->fd = io_uring_file->fd;
	sqe->opcode = IORING_OP_MADVISE;
	sqe->addr = (uintptr_t)io_uring_file->iovecs[0].iov_base;
	sqe->len = 4096;
#if defined(MADV_NORMAL)
	sqe->fadvise_advice = MADV_NORMAL;
#endif
}
#endif

#if defined(HAVE_IORING_OP_STATX)
/*
 *  stress_io_uring_statx_setup()
 *	setup statx submit over io_uring
 */
static void OPTIMIZE3 stress_io_uring_statx_setup(
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

		(void)shim_memset(sqe, 0, sizeof(*sqe));
		sqe->opcode = IORING_OP_STATX;
		sqe->fd = io_uring_file->fd_at;
		sqe->addr = (uintptr_t)"";
		sqe->addr2 = (uintptr_t)&statxbuf;
		sqe->statx_flags = AT_EMPTY_PATH;
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
static void OPTIMIZE3 stress_io_uring_sync_file_range_setup(
	const stress_io_uring_file_t *io_uring_file,
	struct io_uring_sqe *sqe,
	const void *extra_info)
{
	(void)extra_info;

	(void)shim_memset(sqe, 0, sizeof(*sqe));
	sqe->fd = io_uring_file->fd;
	sqe->off = stress_mwc16() & ~511UL;
	sqe->len = stress_mwc32() & ~511UL;
}
#endif

#if defined(HAVE_IORING_OP_SETXATTR) &&	\
    defined(HAVE_IO_URING_SQE_ADDR3) &&	\
    defined(XATTR_CREATE)
/*
 *  stress_io_uring_setxattr_setup()
 *	setup setxattr submit over io_uring
 */
static void OPTIMIZE3 stress_io_uring_setxattr_setup(
	const stress_io_uring_file_t *io_uring_file,
	struct io_uring_sqe *sqe,
	const void *extra_info)
{
	(void)io_uring_file;
	(void)extra_info;

	static char attr_value[] = "ioring-xattr-data";

	(void)shim_memset(sqe, 0, sizeof(*sqe));
	sqe->opcode = IORING_OP_SETXATTR;
	sqe->off = (uintptr_t)attr_value;
	sqe->len = sizeof(attr_value);
	sqe->addr = (uintptr_t)"user.var_test";
	sqe->addr3 = (uintptr_t)io_uring_file->filename;
        sqe->xattr_flags = XATTR_CREATE;
}
#endif


#if defined(HAVE_IORING_OP_GETXATTR) &&	\
    defined(HAVE_IO_URING_SQE_ADDR3) &&	\
    defined(XATTR_CREATE)
/*
 *  stress_io_uring_getxattr_setup()
 *	setup getxattr submit over io_uring
 */
static void OPTIMIZE3 stress_io_uring_getxattr_setup(
	const stress_io_uring_file_t *io_uring_file,
	struct io_uring_sqe *sqe,
	const void *extra_info)
{
	(void)io_uring_file;
	(void)extra_info;

	static char attr_value[128];

	(void)shim_memset(sqe, 0, sizeof(*sqe));
	sqe->opcode = IORING_OP_GETXATTR;
	sqe->off = (uintptr_t)attr_value;
	sqe->len = sizeof(attr_value);
	sqe->addr = (uintptr_t)"user.var_test";
	sqe->addr3 = (uintptr_t)io_uring_file->filename;
}
#endif


#if defined(HAVE_IORING_OP_FTRUNCATE)
/*
 *  stress_io_uring_ftruncate_setup ()
 *	setup ftruncate submit over io_uring
 */
static void OPTIMIZE3 stress_io_uring_ftruncate_setup(
	const stress_io_uring_file_t *io_uring_file,
	struct io_uring_sqe *sqe,
	const void *extra_info)
{
	(void)io_uring_file;
	(void)extra_info;

	(void)shim_memset(sqe, 0, sizeof(*sqe));
	/* don't worry about bad fd if dup fails */
	sqe->fd = io_uring_file->fd;
	sqe->opcode = IORING_OP_FTRUNCATE;
	sqe->off = stress_mwc16() & ~511UL;
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
#if defined(HAVE_IORING_OP_SETXATTR) &&	\
    defined(HAVE_IO_URING_SQE_ADDR3) &&	\
    defined(XATTR_CREATE)
	{ IORING_OP_SETXATTR,	"IORING_OP_SETXATTR",	stress_io_uring_setxattr_setup },
#endif
#if defined(HAVE_IORING_OP_GETXATTR) && \
    defined(HAVE_IO_URING_SQE_ADDR3) &&	\
    defined(XATTR_CREATE)
	{ IORING_OP_GETXATTR,	"IORING_OP_GETXATTR",	stress_io_uring_getxattr_setup },
#endif
#if defined(HAVE_IORING_OP_FTRUNCATE)
	{ IORING_OP_FTRUNCATE,	"IORING_OP_FTRUNCATE",	stress_io_uring_ftruncate_setup },
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
static int stress_io_uring_child(stress_args_t *args, void *context)
{
	int ret, rc;
	char filename[PATH_MAX];
	stress_io_uring_file_t io_uring_file;
	size_t i, j;
	const size_t blocks = 4;
	const size_t block_size = 512;
	off_t file_size = (off_t)blocks * block_size;
	stress_io_uring_submit_t submit;
	const pid_t self = getpid();
	uint32_t io_uring_entries;
	stress_io_uring_user_data_t user_data[SIZEOF_ARRAY(stress_io_uring_setups)];
	const int32_t cpus = stress_get_processors_online();
	int flags;

	(void)context;

	/* Minor tweaking based on empirical testing */
	if (cpus > 128)
		io_uring_entries = 22;
	else if (cpus > 32)
		io_uring_entries = 20;
	else if (cpus > 16)
		io_uring_entries = 18;
	else
		io_uring_entries = 14;

	io_uring_rand = false;

	if (!stress_get_setting("io-uring-entries", &io_uring_entries)) {
		if (g_opt_flags & OPT_FLAGS_MAXIMIZE)
			io_uring_entries = MAX_IO_URING_ENTRIES;
		if (g_opt_flags & OPT_FLAGS_MINIMIZE)
			io_uring_entries = MIN_IO_URING_ENTRIES;
	}
	(void)stress_get_setting("io-uring-rand", &io_uring_rand);

	(void)shim_memset(&submit, 0, sizeof(submit));
	(void)shim_memset(&io_uring_file, 0, sizeof(io_uring_file));

	io_uring_file.fd_dup = fileno(stdin);
	io_uring_file.file_size = file_size;
	io_uring_file.blocks = blocks;
	io_uring_file.block_size = block_size;
	io_uring_file.iovecs_sz = blocks * sizeof(*io_uring_file.iovecs);
	io_uring_file.iovecs =
		stress_mmap_populate(NULL, io_uring_file.iovecs_sz,
			PROT_READ | PROT_WRITE,
			MAP_SHARED | MAP_ANONYMOUS, -1, 0);
	if (io_uring_file.iovecs == MAP_FAILED) {
		io_uring_file.iovecs = NULL;
		pr_inf_skip("%s: cannot mmap iovecs, errno=%d (%s), "
				"skipping stressor\n", args->name,
				errno, strerror(errno));
		return EXIT_NO_RESOURCE;
	}
	stress_set_vma_anon_name(io_uring_file.iovecs, io_uring_file.iovecs_sz, "iovecs");

	for (i = 0; (i < blocks) && (file_size > 0); i++) {
		const size_t iov_length = (file_size > (off_t)block_size) ? (size_t)block_size : (size_t)file_size;

		io_uring_file.iovecs[i].iov_len = iov_length;
		io_uring_file.iovecs[i].iov_base =
			stress_mmap_populate(NULL, block_size, PROT_READ | PROT_WRITE,
				MAP_SHARED | MAP_ANONYMOUS, -1, 0);
		if (io_uring_file.iovecs[i].iov_base == MAP_FAILED) {
			io_uring_file.iovecs[i].iov_base = NULL;
			pr_inf_skip("%s: cannot mmap allocate iovec iov_base%s, errno=%d (%s), "
				"skipping stressor\n", args->name,
				stress_get_memfree_str(),
				errno, strerror(errno));
			stress_io_uring_unmap_iovecs(&io_uring_file);
			return EXIT_NO_RESOURCE;
		}
		stress_set_vma_anon_name(io_uring_file.iovecs[i].iov_base, block_size, "iovec-buffer");
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

	rc = stress_setup_io_uring(args, io_uring_entries, &submit);
	if (rc != EXIT_SUCCESS)
		goto clean;

	flags = O_CREAT | O_RDWR | O_TRUNC;
	stress_file_rw_hint_short(io_uring_file.fd);

	stress_set_proc_state(args->name, STRESS_STATE_SYNC_WAIT);
	stress_sync_start_wait(args);
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
		if ((io_uring_file.fd = open(filename, flags, S_IRUSR | S_IWUSR)) < 0) {
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
		for (j = 0; j < SIZEOF_ARRAY(stress_io_uring_setups); j++) {
			size_t idx = io_uring_rand ? (size_t)stress_mwc8modn((uint8_t)SIZEOF_ARRAY(stress_io_uring_setups)) : j;

			if (UNLIKELY(!stress_continue(args)))
				break;
			if (user_data[idx].supported) {
				rc = stress_io_uring_submit(args,
					stress_io_uring_setups[idx].setup_func,
					&io_uring_file, &submit, &user_data[idx], NULL);
				if (rc != EXIT_SUCCESS)
					break;
			}
			if (stress_io_uring_complete(args, &submit) < 0)
				break;
		}
		if (i++ >= 4096) {
			i = 0;
			if (LIKELY(stress_continue(args)))
				(void)stress_read_fdinfo(self, submit.io_uring_fd);
		}
		(void)close(io_uring_file.fd);
		if (io_uring_file.fd_at >= 0)
			(void)close(io_uring_file.fd_at);
	} while (stress_continue(args));

	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);
#if defined(HAVE_IORING_OP_ASYNC_CANCEL)
	stress_io_uring_cancel_rdwr(args, &io_uring_file, &submit);
#endif
clean:
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
static int stress_io_uring(stress_args_t *args)
{
	return stress_oomable_child(args, NULL, stress_io_uring_child, STRESS_OOMABLE_NORMAL);
}

const stressor_info_t stress_io_uring_info = {
	.stressor = stress_io_uring,
	.classifier = CLASS_IO | CLASS_OS,
	.opts = opts,
	.verify = VERIFY_ALWAYS,
	.help = help
};
#else
const stressor_info_t stress_io_uring_info = {
	.stressor = stress_unimplemented,
	.classifier = CLASS_IO | CLASS_OS,
	.opts = opts,
	.verify = VERIFY_ALWAYS,
	.help = help,
	.unimplemented_reason = "built without linux/io_uring.h or syscall() support"
};
#endif
