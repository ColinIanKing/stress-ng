/*
 * Copyright (C) 2025      Colin Ian King.
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
#include "core-attribute.h"
#include "core-builtin.h"
#include "core-cpu-cache.h"
#include "core-madvise.h"
#include "core-memory.h"
#include "core-numa.h"
#include "core-out-of-memory.h"
#include "core-target-clones.h"

#if defined(HAVE_SYS_TREE_H)
#include <sys/tree.h>
#endif

#if defined(HAVE_SYS_QUEUE_H)
#include <sys/queue.h>
#endif

#if defined(HAVE_BSD_SYS_TREE_H)
#include <bsd/sys/tree.h>
#endif

#if defined(HAVE_SYS_UIO_H)
#include <sys/uio.h>
#endif

/* BSD red-black tree */
#if defined(RB_HEAD) &&		\
    defined(RB_PROTOTYPE) &&	\
    defined(RB_GENERATE) &&	\
    defined(RB_ENTRY) &&	\
    defined(RB_INIT) &&		\
    defined(RB_FIND) &&		\
    defined(RB_INSERT) &&	\
    defined(RB_MIN) &&		\
    defined(RB_NEXT) &&		\
    defined(RB_REMOVE) &&	\
    !defined(__CYGWIN__)
#define HAVE_RB_TREE
#endif

#define MMAP_RANDOM_MIN_MAPPINGS	(1)
#define MMAP_RANDOM_MAX_MAPPINGS	(1024 * 1024)
#define MMAP_RANDOM_DEFAULT_MMAPPINGS	(1024)

#define MAX_PAGES_PER_MAPPING		(8)

#define MWC_RND_ELEMENT(array)		array[stress_mwc8modn(SIZEOF_ARRAY(array))]

static const stress_help_t help[] = {
	{ NULL,	"mmaprandom N",	 	 "start N workers stressing random memory mapping operations" },
	{ NULL,	"mmaprandom-ops N",	 "stop after N mmaprandom bogo operations" },
	{ NULL, "mmaprandom-mappings N", "maximum number of mappings to be made" },
	{ NULL,	"mmaprandom-numa",	 "move processes to randomly chosen NUMA nodes" },
	{ NULL,	NULL,		 	 NULL }
};

static const stress_opt_t opts[] = {
	{ OPT_mmaprandom_mappings, "mmaprandom-mappings", TYPE_ID_SIZE_T, MMAP_RANDOM_MIN_MAPPINGS, MMAP_RANDOM_MAX_MAPPINGS, NULL },
	{ OPT_mmaprandom_numa,     "mmaprandom-numa",     TYPE_ID_BOOL, 0, 1, NULL },
	END_OPT,
};

#if defined(HAVE_RB_TREE)

/*
 *  mr_node_t memory random node keeps track of per memory mapping
 *  information
 */
typedef struct mr_node {
	RB_ENTRY(mr_node) rb;	/* rb tree node */
	void *mmap_addr;	/* mapping start addr */
	size_t mmap_size;	/* mapping size in bytes */
	size_t mmap_page_size;	/* page size (maybe a hugepage) */
	int mmap_prot;		/* mapping protecton */
	int mmap_flags;		/* mapping flags */
	off_t mmap_offset;	/* file based mmap offset into file */
	int mmap_fd;		/* file_fd or mem_fd that was mmap'd to */
	bool used;		/* true = mapping is used */
} mr_node_t;

/*
 *  mr_ctxt_t keeps trace of general mapping context
 */
typedef struct {
	stress_args_t *args;	/* stress-ng arguments */
	mr_node_t *mr_nodes;	/* array of all mr_nodes */
	size_t n_mr_nodes;	/* size of mr_nodes array */
	size_t page_size;	/* page size in bytes */
	int file_fd;		/* file mmap file descriptor */
	int mem_fd;		/* memfd mmap file descriptor */
	uint8_t *page;		/* page mapping for writes */
	double *count;		/* array of usage counters for each mr_func_t */
	bool oom_avoid;		/* low memory avoid flag */
	bool numa;		/* move to random NUMA nodes */
	int pidfd;		/* process' pid file descriptor */
#if defined(HAVE_LINUX_MEMPOLICY_H)
	stress_numa_mask_t *numa_mask;	/* NUMA mask */
	stress_numa_mask_t *numa_nodes;	/* NUMA nodes available */
#endif
} mr_ctxt_t;

typedef void (*mr_func_t)(mr_ctxt_t *ctxt, const int idx);

typedef struct {
	const mr_func_t func;	/* memory operation function */
	const char *name;	/* human readable name of operation */
} mr_funcs_t;

/*
 *  mr_node_page_cmp()
 *	tree compare to sort by mapping address, for allocated mappings
 */
static int OPTIMIZE3 mr_node_page_cmp(mr_node_t *mr_node1, mr_node_t *mr_node2)
{
	if (mr_node1->mmap_addr > mr_node2->mmap_addr)
		return 1;
	else if (mr_node1->mmap_addr < mr_node2->mmap_addr)
		return -1;
	return 0;
}

/*
 * mr_node_node_cmp
 *	tree compare to sort by node addresses, for free'd mr_nodes
 */
static int OPTIMIZE3 mr_node_node_cmp(mr_node_t *mr_node1, mr_node_t *mr_node2)
{
	if (mr_node1 > mr_node2)
		return 1;
	else if (mr_node1 < mr_node2)
		return -1;
	return 0;
}

/* Used nodes are ordered by mmap addr */
static RB_HEAD(sm_used_node_tree, mr_node) sm_used_node_tree_root;
RB_PROTOTYPE(sm_used_node_tree, mr_node, rb, mr_node_page_cmp);
RB_GENERATE(sm_used_node_tree, mr_node, rb, mr_node_page_cmp);
static size_t sm_used_nodes;

/* Free nodes are ordered by the node's own addr */
static RB_HEAD(sm_free_node_tree, mr_node) sm_free_node_tree_root;
RB_PROTOTYPE(sm_free_node_tree, mr_node, rb, mr_node_node_cmp);
RB_GENERATE(sm_free_node_tree, mr_node, rb, mr_node_node_cmp);
static size_t sm_free_nodes;

/*
 *  stress_mmaprandom_sig_handler()
 *	signal handler to immediately terminates
 */
static void NORETURN MLOCKED_TEXT stress_mmaprandom_sig_handler(int num)
{
	(void)num;

	_exit(0);
}

/*
 *  mmap protection flags
 */
static const int prot_flags[] = {
	PROT_NONE,
	PROT_READ,
	PROT_WRITE,
	PROT_EXEC,
	PROT_READ | PROT_WRITE,
	PROT_READ | PROT_EXEC,
	PROT_WRITE | PROT_EXEC,
	PROT_READ | PROT_WRITE | PROT_EXEC,
};

/*
 *  mmap anonymous mapping flags
 */
static const int mmap_anon_flags[] = {
	MAP_SHARED | MAP_ANONYMOUS,
	MAP_PRIVATE | MAP_ANONYMOUS,
};

/*
 *  mmap file mapping flags
 */
static const int mmap_file_flags[] = {
	MAP_SHARED,
	MAP_PRIVATE,
};

/*
 *  extra mmap flags
 */
static const int mmap_extra_flags[] = {
#if defined(MMAP_32BIT)
	MAP_32BIT,
#endif
#if defined(MAP_LOCKED)
	MAP_LOCKED,
#endif
#if defined(MAP_NONBLOCK)
	MAP_NONBLOCK,
#endif
#if defined(MAP_NORESERVE)
	MAP_NORESERVE,
#endif
#if defined(MAP_POPULATE)
	MAP_POPULATE,
#endif
#if defined(MAP_STACK)
	MAP_STACK,
#endif
#if defined(MAP_SYNC)
	MAP_SYNC,
#endif
#if defined(MAP_UNINITIALIZED)
	MAP_UNINITIALIZED,
#endif
#if defined(MAP_HUGETLB) && defined(MAP_HUGE_SHIFT)
	MAP_HUGETLB | (21 << MAP_HUGE_SHIFT),
#endif
#if defined(MAP_DROPPABLE)
	MAP_DROPPABLE,
#endif
};

#if defined(HAVE_MADVISE)
static const int madvise_unmap_options[] = {
	0,
#if defined(MADV_DONTNEED)
	MADV_DONTNEED,
#endif
#if defined(MADV_SOFT_OFFLINE)
	MADV_SOFT_OFFLINE,
#endif
#if defined(MADV_FREE)
	MADV_FREE,
#endif
#if defined(MADV_COLD)
	MADV_COLD,
#endif
};
#endif

/*
 * POSIX madvise options
 */
#if defined(HAVE_POSIX_MADVISE)
static const int posix_madvise_options[] = {
	0,
#if defined(POSIX_MADV_NORMAL)
	POSIX_MADV_NORMAL,
#endif
#if defined(POSIX_MADV_NORMAL)
	POSIX_MADV_SEQUENTIAL,
#endif
#if defined(POSIX_MADV_RANDOM)
	POSIX_MADV_RANDOM,
#endif
#if defined(POSIX_MADV_WILLNEED)
	POSIX_MADV_WILLNEED,
#endif
#if defined(POSIX_MADV_DONTNEED)
	POSIX_MADV_DONTNEED,
#endif
};
#endif

/*
 *  msync flags
 */
#if defined(HAVE_MSYNC)
static const int msync_flags[] = {
#if defined(MS_ASYNC)
	MS_ASYNC,
#endif
#if defined(MS_SYNC)
	MS_SYNC,
#endif
#if defined(MS_ASYNC) && 	\
    defined(MS_INVALIDATE)
	MS_ASYNC | MS_INVALIDATE,
#endif
#if defined(MS_SYNC) &&		\
    defined(MS_INVALIDATE)
	MS_SYNC | MS_INVALIDATE,
#endif
};
#endif

/*
 *  stress_mmaprandom_twiddle_rw_hint()
 *	attempt twiddle randomly selected file rw hint flags on/off
 */
static void stress_mmaprandom_twiddle_rw_hint(const int fd)
{
#if defined(F_SET_RW_HINT)
	static const uint64_t file_rw_hints[] = {
#if defined(RWH_WRITE_LIFE_NOT_SET)
		RWH_WRITE_LIFE_NOT_SET,
#endif
#if defined(RWH_WRITE_LIFE_NONE)
		RWH_WRITE_LIFE_NONE,
#endif
#if defined(RWH_WRITE_LIFE_SHORT)
		RWH_WRITE_LIFE_SHORT,
#endif
#if defined(RWH_WRITE_LIFE_MEDIUM)
		RWH_WRITE_LIFE_MEDIUM,
#endif
#if defined(RWH_WRITE_LIFE_LONG)
		RWH_WRITE_LIFE_LONG,
#endif
#if defined(RWH_WRITE_LIFE_EXTREME)
		RWH_WRITE_LIFE_EXTREME,
#endif
	};
	uint64_t rnd_rw_hint;

	if (SIZEOF_ARRAY(file_rw_hints) == 0)
		return;

	rnd_rw_hint = MWC_RND_ELEMENT(file_rw_hints);
	VOID_RET(int, fcntl(fd, F_SET_RW_HINT, &rnd_rw_hint));
#else
	(void)fd;
#endif
}

/*
 *  stress_mmaprandom_twiddle_file_flags()
 *	attempt twiddle randomly selected file flags on/off
 */
static void stress_mmaprandom_twiddle_file_flags(const int fd)
{
#if defined(F_SETFL) &&	\
    defined(F_GETFL)
	static const int file_flags[] = {
#if defined(O_ASYNC)
		O_ASYNC,
#endif
#if defined(O_DIRECT)
		O_DIRECT,
#endif
#if defined(O_NOATIME)
		O_NOATIME,
#endif
#if defined(O_NONBLOCK)
		O_NONBLOCK,
#endif
	};
	int flags, rnd_flag;

	if (SIZEOF_ARRAY(file_flags) == 0)
		return;

	rnd_flag = MWC_RND_ELEMENT(file_flags);

	flags = fcntl(fd, F_GETFL, NULL);
	if (flags < 0)
		return;

	flags ^= rnd_flag;
	VOID_RET(int, fcntl(fd, F_SETFL, flags));
#else
	(void)fd;
#endif
}

/*
 *  stress_mmaprandom_zap_mr_node()
 *	reset mr_node
 */
static inline void stress_mmaprandom_zap_mr_node(mr_node_t *mr_node)
{
	mr_node->mmap_addr = NULL;
	mr_node->mmap_size = 0;
	mr_node->mmap_page_size = 0;
	mr_node->mmap_prot = 0;
	mr_node->mmap_flags = 0;
	mr_node->mmap_offset = 0;
	mr_node->mmap_fd = -1;
	mr_node->used = false;
}

#if defined(MAP_FIXED_NOREPLACE)
/*
 *  32 bit address space address masks
 */
static const uint32_t masks_32bit[] = {
	0x000fffffUL,
	0x001fffffUL,
	0x003fffffUL,
	0x007fffffUL,
	0x00ffffffUL,
	0x01ffffffUL,
	0x03ffffffUL,
	0x07ffffffUL,
	0x0fffffffUL,
};

/*
 *  64 bit address space addess masks
 */
static const uint64_t masks_64bit[] = {
	0x00000000007fffffULL,
	0x0000000000ffffffULL,
	0x0000000001ffffffULL,
	0x0000000003ffffffULL,
	0x0000000007ffffffULL,
	0x000000000fffffffULL,
	0x000000001fffffffULL,
	0x000000003fffffffULL,
	0x000000007fffffffULL,
	0x00000000ffffffffULL,
	0x00000001ffffffffULL,
	0x00000003ffffffffULL,
	0x00000007ffffffffULL,
	0x0000000fffffffffULL,
	0x0000001fffffffffULL,
	0x0000003fffffffffULL,
	0x0000007fffffffffULL,
	0x000000ffffffffffULL,
	0x000001ffffffffffULL,
	0x000002ffffffffffULL,
	0x000003ffffffffffULL,
};

/*
 *  stress_mmaprandom_fixed_addr()
 *	generate a random mmap hint address
 */
static inline void * stress_mmaprandom_fixed_addr(const size_t page_size)
{
	void *addr;

	if (sizeof(void *) > 4) {
		uint64_t mask_addr = MWC_RND_ELEMENT(masks_64bit);
		uint64_t fixed_addr = stress_mwc64() & mask_addr & ~(uint64_t)(page_size - 1);

		addr = (void *)(uintptr_t)fixed_addr;
	} else {
		uint32_t mask_addr = MWC_RND_ELEMENT(masks_32bit);
		uint32_t fixed_addr = stress_mwc32() & mask_addr & ~(uint32_t)(page_size - 1);

		addr = (void *)(uintptr_t)fixed_addr;
	}
	return addr;
}
#endif

/*
 *  stress_mmaprandom_mmap()
 *	perform mmap, if MAP_FIXED_NOREPLACE is available then 50%
 *	of the time try to perform a randomly chosen mmap on a fixed
 *	address to try and spread the address space about
 */
static void * stress_mmaprandom_mmap(
	void *hint,
	size_t length,
	int prot,
	int flags,
	int fd,
	off_t offset,
	size_t page_size)
{
#if defined(MAP_FIXED_NOREPLACE)
	if (stress_mwc1()) {
		void *fixed_addr = stress_mmaprandom_fixed_addr(page_size);
		void *addr;

		addr = mmap(fixed_addr, length, prot,
				MAP_FIXED_NOREPLACE | flags, fd, offset);
		if (addr != MAP_FAILED)
			return addr;
	}
#endif
	(void)page_size;
	return mmap(hint, length, prot, flags, fd, offset);
}

/*
 *  stress_mmaprandom_madvise_pages()
 *	apply madvise to a region, either over the entire
 *	region in one go or in random page chunks
 */
static int stress_mmaprandom_madvise_pages(
	void *addr,
	size_t length,
	int advice,
	size_t page_size)
{
	uint8_t *ptr = (uint8_t *)addr;
	uint8_t *ptr_end = ptr + length;

#if defined(MADV_HWPOISON)
	/* We really don't want to do this */
        if (advice == MADV_HWPOISON)
		return 0;
#endif

	if (stress_mwc1()) {
#if defined(MADV_NORMAL)
		if (shim_madvise(addr, length, advice) < 0)
			return shim_madvise(addr, length, MADV_NORMAL);

#else
		return shim_madvise(addr, length, advice);
#endif
	}

	while (ptr < ptr_end) {
		if (stress_mwc1()) {
#if defined(MADV_NORMAL)
			if (shim_madvise(ptr, page_size, advice) < 0)
				(void)shim_madvise(ptr, page_size, MADV_NORMAL);
#else
			(void)shim_madvise(ptr, page_size, advice);
#endif
			ptr += page_size;
		} else {
#if defined(MADV_NORMAL)
			(void)shim_madvise(ptr, page_size, MADV_NORMAL);
#endif
			ptr += page_size;
		}

	}
	return 0;
}

/*
 *  stress_mmaprandom_munmap()
 *	unmap a region, apply random unmapping friendly madvise
 */
static int stress_mmaprandom_munmap(
	void *addr,
	size_t length,
	size_t page_size)
{
#if defined(MADV_FREE)
	const int advise = MWC_RND_ELEMENT(madvise_unmap_options);

	(void)stress_mmaprandom_madvise_pages(addr, length, advise, page_size);
#endif
	return stress_munmap_force(addr, length);
}

/*
 *  stress_mmaprandom_mmap_anon()
 *  	perform anonymous mmap
 */
static void OPTIMIZE3 stress_mmaprandom_mmap_anon(mr_ctxt_t *ctxt, const int idx)
{
	size_t page_size = ctxt->page_size;
	size_t pages = stress_mwc8modn(MAX_PAGES_PER_MAPPING) + 1;
	size_t size = page_size * pages, i, j;
	uint8_t *addr;
	int prot_flag, mmap_flag, extra_flags = 0;
	mr_node_t *mr_node;
	char name[80];

	prot_flag = MWC_RND_ELEMENT(prot_flags);
	mmap_flag = MWC_RND_ELEMENT(mmap_anon_flags);

	mr_node = RB_MIN(sm_free_node_tree, &sm_free_node_tree_root);
	if (!mr_node)
		return;

	for (i = 0; i < SIZEOF_ARRAY(mmap_extra_flags); i++) {
		int new_flags = MWC_RND_ELEMENT(mmap_extra_flags);

#if defined(MAP_HUGETLB)
		if (new_flags & MAP_HUGETLB) {
			static int count = 0;

			/* periodically allow a huge page */
			if (UNLIKELY(count++ > 32)) {
				count = 0;
				page_size = 1U << ((new_flags >> MAP_HUGE_SHIFT) & 0x3f);
				size = page_size;
			} else {
				continue;
			}
		}
#endif
		extra_flags |= new_flags;
	}

	j = stress_mwc8modn(SIZEOF_ARRAY(mmap_extra_flags));
	for (;;) {
		int old_flags;

		if (ctxt->oom_avoid && stress_low_memory(size * 2)) {
			addr = MAP_FAILED;
			break;
		}

		addr = (uint8_t *)stress_mmaprandom_mmap(NULL, size, prot_flag,
				mmap_flag | extra_flags, -1, 0, page_size);
		if (addr != MAP_FAILED) {
			ctxt->count[idx] += 1.0;
			break;
		}
		if (!extra_flags)
			break;
		do {
			old_flags = extra_flags;
			extra_flags &= ~mmap_extra_flags[j];
			j++;
			if (UNLIKELY(j >= SIZEOF_ARRAY(mmap_extra_flags)))
				j = 0;
		} while (extra_flags && (old_flags == extra_flags));
	}

	if (UNLIKELY(addr == MAP_FAILED))
		return;

	snprintf(name, sizeof(name), "mmaprandom-anon-%p", addr);
	stress_set_vma_anon_name(addr, size, name);

	RB_REMOVE(sm_free_node_tree, &sm_free_node_tree_root, mr_node);
	sm_free_nodes--;
	mr_node->mmap_addr = addr;
	mr_node->mmap_size = size;
	mr_node->mmap_page_size = page_size;
	mr_node->mmap_prot = prot_flag;
	mr_node->mmap_flags = mmap_flag | extra_flags;
	mr_node->mmap_offset = 0;
	mr_node->mmap_fd = -1;
	mr_node->used = true;
	RB_INSERT(sm_used_node_tree, &sm_used_node_tree_root, mr_node);
	sm_used_nodes++;
}

static int stress_mmaprandom_fallocate(
	mr_ctxt_t *ctxt,
	const int fd,
	const off_t offset,
	const size_t pages)
{
	size_t i;
	const size_t page_size = ctxt->page_size;

	if (fd < 0)
		return -1;

	if (stress_mwc1() || (pages > 1)) {
		/* multiple pages, to one fallocate over the entire allocation */
		if (LIKELY(shim_fallocate(fd, 0, offset, pages * page_size) == 0))
			return 0;
	} else {
		if (page_size == 4096) {
			/* 1 4K page, 1 byte, will get expanded automatically on page map write */
			if (shim_fallocate(fd, 0, offset, 1) == 0)
				return 0;
		} else {
			/* 1 x non-4K page, do whole page */
			if (shim_fallocate(fd, 0, offset, page_size) == 0)
				return 0;
		}
	}

	/* fall back to writes */
	if (UNLIKELY(lseek(fd, offset, SEEK_SET) == (off_t) -1))
		return -1;
	(void)shim_memset(ctxt->page, 0, page_size);
	for (i = 0; i < pages; i++) {
		if (UNLIKELY(write(fd, ctxt->page, page_size) != (ssize_t)page_size))
			return -1;
	}
	return 0;
}

/*
 *  stress_mmaprandom_mmap_file()
 *  	perform file based mmap
 */
static void OPTIMIZE3 stress_mmaprandom_mmap_file(mr_ctxt_t *ctxt, const int idx)
{
	const size_t page_size = ctxt->page_size;
	size_t i, j;
	const size_t pages = stress_mwc8modn(MAX_PAGES_PER_MAPPING) + 1;
	const off_t offset = stress_mwc8modn(MAX_PAGES_PER_MAPPING) * page_size;
	const size_t size = page_size * pages;
	uint8_t *addr;
	int prot_flag, mmap_flag, extra_flags = 0;
	mr_node_t *mr_node;
	int fd;

	mmap_flag = MWC_RND_ELEMENT(mmap_file_flags);

	if (ctxt->mem_fd < 0)
		fd = ctxt->file_fd;
	else
		fd = stress_mwc1() ? ctxt->file_fd : ctxt->mem_fd;

	mr_node = RB_MIN(sm_free_node_tree, &sm_free_node_tree_root);
	if (!mr_node)
		return;

	if (UNLIKELY(stress_mmaprandom_fallocate(ctxt, fd, offset, pages) < 0))
		return;

	for (i = 0; i < SIZEOF_ARRAY(mmap_extra_flags); i++)
		extra_flags |= MWC_RND_ELEMENT(mmap_extra_flags);

	prot_flag = MWC_RND_ELEMENT(prot_flags);
	j = stress_mwc8modn(SIZEOF_ARRAY(mmap_extra_flags));

	for (;;) {
		int old_flags;

		if (ctxt->oom_avoid && stress_low_memory(size * 2)) {
			addr = MAP_FAILED;
			break;
		}

		stress_mmaprandom_twiddle_file_flags(fd);
		stress_mmaprandom_twiddle_rw_hint(fd);
		addr = (uint8_t *)stress_mmaprandom_mmap(NULL, size, prot_flag,
				mmap_flag | extra_flags, fd, offset, page_size);
		if (addr != MAP_FAILED) {
			ctxt->count[idx] += 1.0;
			break;
		}
		if (!extra_flags)
			break;
		do {
			old_flags = extra_flags;
			extra_flags &= ~mmap_extra_flags[j];
			j++;
			if (j >= SIZEOF_ARRAY(mmap_extra_flags))
				j = 0;
		} while (extra_flags && (old_flags == extra_flags));
	}

	if (UNLIKELY(addr == MAP_FAILED))
		return;

	RB_REMOVE(sm_free_node_tree, &sm_free_node_tree_root, mr_node);
	sm_free_nodes--;
	mr_node->mmap_addr = addr;
	mr_node->mmap_size = size;
	mr_node->mmap_page_size = page_size;
	mr_node->mmap_prot = prot_flag;
	mr_node->mmap_flags = mmap_flag | extra_flags;
	mr_node->mmap_fd = fd;
	mr_node->mmap_offset = offset;
	mr_node->used = true;
	RB_INSERT(sm_used_node_tree, &sm_used_node_tree_root, mr_node);
	sm_used_nodes++;
}

/*
 *  stress_mmaprandom_get_random_used()
 *	get an randomly selected used mr_node, suboptimial linear scan,
 *	needs improving.
 */
static OPTIMIZE3 mr_node_t *stress_mmaprandom_get_random_used(mr_ctxt_t *ctxt)
{
	const size_t n = stress_mwc32modn((uint32_t)ctxt->n_mr_nodes);
	register mr_node_t *mr_node = &ctxt->mr_nodes[n];
	register mr_node_t *mr_node_end = &ctxt->mr_nodes[ctxt->n_mr_nodes];
	register size_t i;

	for (i = 0; LIKELY(i < ctxt->n_mr_nodes); i++) {
		if (UNLIKELY(mr_node->used))
			return mr_node;
		mr_node++;
		if (UNLIKELY(mr_node >= mr_node_end))
			mr_node = ctxt->mr_nodes;
	}
	return NULL;
}

/*
 *  stress_mmaprandom_get_random_size()
 *	get an randomly selected used mr_node
 */
static size_t stress_mmaprandom_get_random_size(
	const size_t mmap_size,
	const size_t page_size)
{
	size_t n = mmap_size / page_size;

	return page_size * (1 + stress_mwc8modn(n));
}

/*
 *  stress_mmaprandom_unmmap()
 *	unmap pages
 */
static void OPTIMIZE3 stress_mmaprandom_unmmap(mr_ctxt_t *ctxt, const int idx)
{
	mr_node_t *mr_node = stress_mmaprandom_get_random_used(ctxt);

	if (!mr_node)
		return;

	if (stress_mwc1()) {
		/* unmap entire mapping in one go */

		if (LIKELY(stress_mmaprandom_munmap((void *)mr_node->mmap_addr,
				mr_node->mmap_size, mr_node->mmap_page_size) == 0)) {
			ctxt->count[idx] += 1.0;
			stress_mmaprandom_zap_mr_node(mr_node);
			RB_REMOVE(sm_used_node_tree, &sm_used_node_tree_root, mr_node);
			sm_used_nodes--;
			RB_INSERT(sm_free_node_tree, &sm_free_node_tree_root, mr_node);
			sm_free_nodes++;
		}
	} else {
		/* unmap mapping in pages from start to end */

		uint8_t *ptr = (uint8_t *)mr_node->mmap_addr;
		uint8_t *ptr_end = ptr + mr_node->mmap_size;
		ssize_t page_size = mr_node->mmap_page_size;
		bool failed = false;

		while (ptr < ptr_end) {
			if (UNLIKELY(stress_munmap_force((void *)ptr, page_size) < 0))
				failed = true;
			ptr += page_size;
		}
		/*
		 * force entire mapping to be unmapped if page by
		 * page unmaps failed
		 */
		if (UNLIKELY(failed))
			(void)stress_mmaprandom_munmap(mr_node->mmap_addr, mr_node->mmap_size, page_size);

		ctxt->count[idx] += 1.0;
		stress_mmaprandom_zap_mr_node(mr_node);
		RB_REMOVE(sm_used_node_tree, &sm_used_node_tree_root, mr_node);
		sm_used_nodes--;
		RB_INSERT(sm_free_node_tree, &sm_free_node_tree_root, mr_node);
		sm_free_nodes++;
	}
}

/*
 *  stress_mmaprandom_unmmap_lo_hi_addr()
 *	unmap lowest or highest mapped address
 */
static void OPTIMIZE3 stress_mmaprandom_unmmap_lo_hi_addr(mr_ctxt_t *ctxt, const int idx)
{
	mr_node_t *mr_node = stress_mwc1() ?
		RB_MIN(sm_used_node_tree, &sm_used_node_tree_root) :
		RB_MAX(sm_used_node_tree, &sm_used_node_tree_root);

	(void)ctxt;

	if (!mr_node)
		return;

	if (LIKELY(stress_mmaprandom_munmap((void *)mr_node->mmap_addr,
			mr_node->mmap_size, mr_node->mmap_page_size) == 0)) {
		ctxt->count[idx] += 1.0;

		stress_mmaprandom_zap_mr_node(mr_node);
		RB_REMOVE(sm_used_node_tree, &sm_used_node_tree_root, mr_node);
		sm_used_nodes--;
		RB_INSERT(sm_free_node_tree, &sm_free_node_tree_root, mr_node);
		sm_free_nodes++;
	}
}

/*
 *  stress_mmaprandom_read()
 *	read from mapping
 */
static void OPTIMIZE3 stress_mmaprandom_read(mr_ctxt_t *ctxt, const int idx)
{
	mr_node_t *mr_node = stress_mmaprandom_get_random_used(ctxt);

	if (!mr_node)
		return;

#if defined(MAP_NORESERVE)
	if ((mr_node->mmap_prot & PROT_READ) && !(mr_node->mmap_flags & MAP_NORESERVE)) {
#else
	if (mr_node->mmap_prot & PROT_READ) {
#endif
		uint64_t *volatile ptr = mr_node->mmap_addr;
		const uint64_t *end = (uint64_t *)((intptr_t)ptr + mr_node->mmap_size);

		while (ptr < end) {
			(void)ptr[0];
			(void)ptr[1];
			(void)ptr[2];
			(void)ptr[3];
			(void)ptr[4];
			(void)ptr[5];
			(void)ptr[6];
			(void)ptr[7];
			ptr += 8;
		}
		ctxt->count[idx] += 1.0;
	}
}

/*
 *  stress_mmaprandom_read()
 *	write to a mapping
 */
static void OPTIMIZE3 stress_mmaprandom_write(mr_ctxt_t *ctxt, const int idx)
{
	mr_node_t *mr_node = stress_mmaprandom_get_random_used(ctxt);

	if (!mr_node)
		return;

#if defined(MAP_NORESERVE)
	if ((mr_node->mmap_prot & PROT_WRITE) && !(mr_node->mmap_flags & MAP_NORESERVE)) {
#else
	if (mr_node->mmap_prot & PROT_WRITE) {
#endif
#if defined(HAVE_ATOMIC_STORE) &&	\
    defined(__ATOMIC_ACQUIRE)
		if (mr_node->mmap_prot & PROT_READ)
			__atomic_add_fetch((uint8_t *)mr_node->mmap_addr, 1, __ATOMIC_SEQ_CST);
#endif
		shim_memset(mr_node->mmap_addr, stress_mwc8(), mr_node->mmap_size);
		ctxt->count[idx] += 1.0;
	}
}

/*
 *  stress_mmaprandom_cache_flush()
 *	cache flush a mapping
 */
static void stress_mmaprandom_cache_flush(mr_ctxt_t *ctxt, const int idx)
{
	mr_node_t *mr_node = stress_mmaprandom_get_random_used(ctxt);

	if (!mr_node)
		return;

#if defined(MAP_NORESERVE)
	if ((mr_node->mmap_prot & PROT_WRITE) && !(mr_node->mmap_flags & MAP_NORESERVE)) {
#else
	if (mr_node->mmap_prot & PROT_WRITE) {
#endif
		stress_cpu_data_cache_flush(mr_node->mmap_addr, mr_node->mmap_size);
		ctxt->count[idx] += 1.0;
	}
}

#if defined(HAVE_BUILTIN_PREFETCH)
/*
 *  stress_mmaprandom_cache_prefetch()
 *	cache prefetch a mapping
 */
static void OPTIMIZE3 stress_mmaprandom_cache_prefetch(mr_ctxt_t *ctxt, const int idx)
{
	mr_node_t *mr_node = stress_mmaprandom_get_random_used(ctxt);

	if (!mr_node)
		return;

#if defined(MAP_NORESERVE)
	if ((mr_node->mmap_prot & PROT_READ) && !(mr_node->mmap_flags & MAP_NORESERVE)) {
#else
	if (mr_node->mmap_prot & PROT_READ) {
#endif
		uint8_t *ptr = (uint8_t *)mr_node->mmap_addr;
		uint8_t *ptr_end = ptr + mr_node->mmap_size;

		while (ptr < ptr_end) {
			shim_builtin_prefetch((void *)(ptr + 0x00), 0, 1);
			shim_builtin_prefetch((void *)(ptr + 0x40), 0, 1);
			shim_builtin_prefetch((void *)(ptr + 0x80), 0, 1);
			shim_builtin_prefetch((void *)(ptr + 0xc0), 0, 1);
			ptr += 256;
		}
		ctxt->count[idx] += 1.0;
	}
}
#endif

#if defined(HAVE_MREMAP)
/*
 *  stress_mmaprandom_mremap
 *  	memory remap a mapping
 */
static void stress_mmaprandom_mremap(mr_ctxt_t *ctxt, const int idx)
{
	mr_node_t *mr_node = stress_mmaprandom_get_random_used(ctxt);
	size_t pages, new_size;
	void *new_addr;

	if (!mr_node)
		return;

	pages = stress_mwc8modn(MAX_PAGES_PER_MAPPING) + 1;
	new_size = mr_node->mmap_page_size * pages;

	if (new_size > mr_node->mmap_size) {
		/*
		 * Cannot expand anonymous mappings as there no
		 * backing to expand into
		 */
		if (mr_node->mmap_flags & MAP_ANONYMOUS)
			return;

		/*
		 * File mapped? Then ensure we have backing
		 * written to the file
		 */
		if (mr_node->mmap_fd != -1)
			if (stress_mmaprandom_fallocate(ctxt, mr_node->mmap_fd, mr_node->mmap_offset, pages) < 0)
				return;
	}

	new_addr = mremap(mr_node->mmap_addr, mr_node->mmap_size, new_size, MREMAP_MAYMOVE);
	if (new_addr != MAP_FAILED) {
		ctxt->count[idx] += 1.0;

		mr_node->mmap_addr = new_addr;
		mr_node->mmap_size = new_size;
	}
}
#endif

#if defined(HAVE_MADVISE)
/*
 *  stress_mmaprandom_madvise()
 *	madvise a mapping
 */
static void stress_mmaprandom_madvise(mr_ctxt_t *ctxt, const int idx)
{
	mr_node_t *mr_node = stress_mmaprandom_get_random_used(ctxt);
	int advice;

	if (!mr_node)
		return;

	advice = madvise_options[stress_mwc8modn(madvise_options_elements)];
#if defined(MADV_HWPOISON)
	/* We really don't want to do this */
        if (advice == MADV_HWPOISON)
		return;
#endif
	if (LIKELY(madvise(mr_node->mmap_addr, mr_node->mmap_size, advice) == 0))
		ctxt->count[idx] += 1.0;
}
#endif

#if defined(HAVE_POSIX_MADVISE)
/*
 *  stress_mmaprandom_posix_madvise()
 *	posix_madvise a mapping
 */
static void stress_mmaprandom_posix_madvise(mr_ctxt_t *ctxt, const int idx)
{
	mr_node_t *mr_node = stress_mmaprandom_get_random_used(ctxt);
	int advice;

	if (!mr_node)
		return;

	advice = MWC_RND_ELEMENT(posix_madvise_options);
	if (LIKELY(posix_madvise(mr_node->mmap_addr, mr_node->mmap_size, advice) == 0))
		ctxt->count[idx] += 1.0;
}
#endif

#if defined(HAVE_MINCORE)
/*
 *  stress_mmaprandom_mincore()
 *	check memory resident pages via mincore
 */
static void stress_mmaprandom_mincore(mr_ctxt_t *ctxt, const int idx)
{
	mr_node_t *mr_node = stress_mmaprandom_get_random_used(ctxt);
	unsigned char vec[MAX_PAGES_PER_MAPPING];
	size_t max_size, size;

	if (!mr_node)
		return;

	/* max size must be based on smallest system page size */
	max_size = MAX_PAGES_PER_MAPPING * ctxt->page_size;
	size = mr_node->mmap_size > max_size ? max_size : mr_node->mmap_size;
	if (LIKELY(shim_mincore(mr_node->mmap_addr, size, vec) == 0))
		ctxt->count[idx] += 1.0;
}
#endif

#if defined(HAVE_MSYNC)
/*
 *  stress_mmaprandom_msync()
 *	msync a mapping
 */
static void stress_mmaprandom_msync(mr_ctxt_t *ctxt, const int idx)
{
	mr_node_t *mr_node = stress_mmaprandom_get_random_used(ctxt);
	size_t size;
	int flags;

	if (!mr_node)
		return;

	if (mr_node->mmap_prot == PROT_NONE)
		return;

	size = stress_mmaprandom_get_random_size(mr_node->mmap_size, mr_node->mmap_page_size);
	flags = MWC_RND_ELEMENT(msync_flags);
	if (msync(mr_node->mmap_addr, size, flags) == 0)
		ctxt->count[idx] += 1.0;
}
#endif

#if defined(HAVE_MLOCK)
/*
 *  stress_mmaprandom_mlock()
 *	memory lock a mapping
 */
static void stress_mmaprandom_mlock(mr_ctxt_t *ctxt, const int idx)
{
	mr_node_t *mr_node = stress_mmaprandom_get_random_used(ctxt);

	if (!mr_node)
		return;

	if (mlock(mr_node->mmap_addr, mr_node->mmap_size) == 0)
		ctxt->count[idx] += 1.0;
}
#endif

#if defined(HAVE_MUNLOCK)
/*
 *  stress_mmaprandom_munlock()
 *	memory unlock a mapping
 */
static void stress_mmaprandom_munlock(mr_ctxt_t *ctxt, const int idx)
{
	mr_node_t *mr_node = stress_mmaprandom_get_random_used(ctxt);

	if (!mr_node)
		return;

	if (munlock(mr_node->mmap_addr, mr_node->mmap_size) == 0)
		ctxt->count[idx] += 1.0;
}
#endif

#if defined(HAVE_MPROTECT)
/*
 *  stress_mmaprandom_mprotect
 * 	memory protect a mapping
 */
static void stress_mmaprandom_mprotect(mr_ctxt_t *ctxt, const int idx)
{
	mr_node_t *mr_node = stress_mmaprandom_get_random_used(ctxt);
	int prot_flag;

	if (!mr_node)
		return;

	prot_flag = MWC_RND_ELEMENT(prot_flags);
	if (LIKELY(mprotect(mr_node->mmap_addr, mr_node->mmap_size, prot_flag) == 0)) {
		mr_node->mmap_prot = prot_flag;
		ctxt->count[idx] += 1.0;
	}
}
#endif

/*
 *  stress_mmaprandom_unmap_first_page()
 *	unmap first page of a multi-page mapping
 */
static void OPTIMIZE3 stress_mmaprandom_unmap_first_page(mr_ctxt_t *ctxt, const int idx)
{
	mr_node_t *mr_node = stress_mmaprandom_get_random_used(ctxt);
	size_t page_size;

	if (!mr_node)
		return;

	page_size = mr_node->mmap_page_size;
	if (mr_node->mmap_size >= (2 * page_size)) {
		uint8_t *ptr = mr_node->mmap_addr;

		if (UNLIKELY(stress_mmaprandom_munmap(ptr, page_size, page_size) < 0))
			return;
		RB_REMOVE(sm_used_node_tree, &sm_used_node_tree_root, mr_node);

		ptr += page_size;
		mr_node->mmap_addr = ptr;
		mr_node->mmap_size -= page_size;
		mr_node->mmap_offset += page_size;

		RB_INSERT(sm_used_node_tree, &sm_used_node_tree_root, mr_node);

		ctxt->count[idx] += 1.0;
	}
}

/*
 *  stress_mmaprandom_unmap_last_page()
 *	unmap last page of a multi-page mapping
 */
static void stress_mmaprandom_unmap_last_page(mr_ctxt_t *ctxt, const int idx)
{
	mr_node_t *mr_node = stress_mmaprandom_get_random_used(ctxt);
	size_t page_size;

	if (!mr_node)
		return;

	page_size = mr_node->mmap_page_size;
	if (mr_node->mmap_size >= (2 * page_size)) {
		uint8_t *ptr = mr_node->mmap_addr;

		ptr += (mr_node->mmap_size - page_size);
		if (UNLIKELY(stress_mmaprandom_munmap(ptr, page_size, page_size) < 0))
			return;
		mr_node->mmap_size -= page_size;
		ctxt->count[idx] += 1.0;
	}
}

/*
 *  stress_mmaprandom_split()
 *	break a mapping into two adjacent mappings
 *
 */
static void OPTIMIZE3 stress_mmaprandom_split(mr_ctxt_t *ctxt, const int idx)
{
	mr_node_t *mr_node = stress_mmaprandom_get_random_used(ctxt);
	size_t page_size;

	if (!mr_node)
		return;

	page_size = mr_node->mmap_page_size;
	if (mr_node->mmap_size >= (2 * page_size)) {
		uint8_t *ptr = (uint8_t *)mr_node->mmap_addr;
		mr_node_t *new_mr_node;
#if defined(HAVE_MPROTECT)
		int prot_flag;
#endif

		new_mr_node = RB_MIN(sm_free_node_tree, &sm_free_node_tree_root);
		if (!new_mr_node)
			return;

		ptr += page_size;

		RB_REMOVE(sm_free_node_tree, &sm_free_node_tree_root, new_mr_node);
		sm_free_nodes--;
		new_mr_node->mmap_addr = (void *)ptr;
		new_mr_node->mmap_size = mr_node->mmap_size - (page_size);
		new_mr_node->mmap_page_size = mr_node->mmap_page_size;
#if defined(HAVE_MPROTECT)
		/* Switch to new mmap protection flags */
		prot_flag = MWC_RND_ELEMENT(prot_flags);
		if (mprotect(new_mr_node->mmap_addr, new_mr_node->mmap_size, prot_flag) == 0)
			new_mr_node->mmap_prot = prot_flag;
#else
		/* Keep same protection as original */
		new_mr_node->mmap_prot = mr_node->mmap_prot;
#endif
		new_mr_node->mmap_flags = mr_node->mmap_flags;
		new_mr_node->mmap_offset = mr_node->mmap_offset + page_size;
		new_mr_node->mmap_fd = mr_node->mmap_fd;
		new_mr_node->used = true;
		RB_INSERT(sm_used_node_tree, &sm_used_node_tree_root, new_mr_node);
		sm_used_nodes++;

		mr_node->mmap_size = page_size;
		ctxt->count[idx] += 1.0;
	}
}

/*
 *  stress_mmaprandom_split_hole()
 *	break a mapping into two mappings, unmap a page between them
 *
 */
static void OPTIMIZE3 stress_mmaprandom_split_hole(mr_ctxt_t *ctxt, const int idx)
{
	mr_node_t *mr_node = stress_mmaprandom_get_random_used(ctxt);
	size_t page_size;

	if (!mr_node)
		return;

	page_size = mr_node->mmap_page_size;
	if (mr_node->mmap_size >= (3 * page_size)) {
		uint8_t *ptr = mr_node->mmap_addr;
		mr_node_t *new_mr_node;

		new_mr_node = RB_MIN(sm_free_node_tree, &sm_free_node_tree_root);
		if (!new_mr_node)
			return;

		ptr += page_size;
		if (stress_mmaprandom_munmap((void *)ptr, page_size, page_size) < 0)
			return;

		ptr += page_size;

		RB_REMOVE(sm_free_node_tree, &sm_free_node_tree_root, new_mr_node);
		sm_free_nodes--;
		new_mr_node->mmap_addr = (void *)ptr;
		new_mr_node->mmap_size = mr_node->mmap_size - (2 * page_size);
		new_mr_node->mmap_page_size = mr_node->mmap_page_size;
		new_mr_node->mmap_prot = mr_node->mmap_prot;
		new_mr_node->mmap_flags = mr_node->mmap_flags;
		new_mr_node->mmap_offset = mr_node->mmap_offset + (2 * page_size);
		new_mr_node->mmap_fd = mr_node->mmap_fd;
		new_mr_node->used = true;
		RB_INSERT(sm_used_node_tree, &sm_used_node_tree_root, new_mr_node);
		sm_used_nodes++;

		mr_node->mmap_size = page_size;
		ctxt->count[idx] += 1.0;
	}
}

/*
 *  stress_mmaprandom_fork()
 *	fork to duplicate mappings every ~1 second
 */
static void stress_mmaprandom_fork(mr_ctxt_t *ctxt, const int idx)
{
	pid_t pid;
	static double next_time = 0.0;
	double now;

	now = stress_time_now();
	if (now < next_time)
		return;

	if (ctxt->oom_avoid) {
		size_t total, resident, shared;

		if (stress_get_pid_memory_usage(getpid(), &total, &resident, &shared) < 0) {
			/* Can't get memory, random guess at 128MB */
			total = 128 * MB;
		}
		if (stress_low_memory(total))
			return;
	}

	next_time = now + 1.0;

	pid = fork();
	if (pid < 0) {
		return;
	} if (pid == 0) {
		/* Either unmap mappings in child or let _exit(2) do it */
		if (stress_mwc1()) {
			mr_node_t *mr_node;

			RB_FOREACH(mr_node, sm_used_node_tree, &sm_used_node_tree_root) {
				if (stress_mwc1()) {
#if defined(MAP_NORESERVE)
					if ((mr_node->mmap_prot & PROT_WRITE) && !(mr_node->mmap_flags & MAP_NORESERVE)) {
#else
					if (mr_node->mmap_prot & PROT_WRITE) {
#endif
#if defined(HAVE_ATOMIC_STORE) &&	\
    defined(__ATOMIC_ACQUIRE)
						if (mr_node->mmap_prot & PROT_READ)
							__atomic_add_fetch((uint8_t *)mr_node->mmap_addr, 1, __ATOMIC_SEQ_CST);
#endif
						shim_memset(mr_node->mmap_addr, stress_mwc8(), mr_node->mmap_size);
					}
				}
				(void)stress_munmap_force(mr_node->mmap_addr, mr_node->mmap_size);
			}
		}
		_exit(0);
	} else {
		int status;

		(void)waitpid(pid, &status, 0);
		ctxt->count[idx] += 1.0;
	}
}

/*
 *  stress_mmaprandom_join()
 *	join to matching mmap'd regions together if they are
 *	next to each other, free's up a used mr_node.
 */
static void OPTIMIZE3 stress_mmaprandom_join(mr_ctxt_t *ctxt, const int idx)
{
	size_t i;

	for (i = 0; i < ((ctxt->n_mr_nodes >> 8) + 1); i++) {
		mr_node_t *mr_node = stress_mmaprandom_get_random_used(ctxt);
		mr_node_t find_mr_node, *found_mr_node;
		uint8_t *ptr;
		size_t page_size, max_size;

		if (!mr_node)
			continue;

		ptr = mr_node->mmap_addr;
		page_size = mr_node->mmap_page_size;
		max_size = page_size * MAX_PAGES_PER_MAPPING;

		/* mappings right next to each other */
		find_mr_node.mmap_addr = (void *)(ptr + mr_node->mmap_size);

		found_mr_node = RB_FIND(sm_used_node_tree, &sm_used_node_tree_root, &find_mr_node);
		if (found_mr_node &&
		    (found_mr_node->mmap_fd == mr_node->mmap_fd) &&
		    (found_mr_node->mmap_prot == mr_node->mmap_prot) &&
		    (found_mr_node->mmap_flags == mr_node->mmap_flags) &&
		    (found_mr_node->mmap_page_size == mr_node->mmap_page_size) &&
		    (found_mr_node->mmap_size + mr_node->mmap_size <= max_size)) {
			mr_node->mmap_size += found_mr_node->mmap_size;

			RB_REMOVE(sm_used_node_tree, &sm_used_node_tree_root, found_mr_node);
			sm_used_nodes--;
			found_mr_node->used = false;
			RB_INSERT(sm_free_node_tree, &sm_free_node_tree_root, found_mr_node);
			sm_free_nodes++;

			ctxt->count[idx] += 1.0;
			return;
		}
	}
}

#if defined(HAVE_LINUX_MEMPOLICY_H)
/*
 *  stress_mmaprandom_numa_move()
 *	move pages to different NUMA nodes
 */
static void stress_mmaprandom_numa_move(mr_ctxt_t *ctxt, const int idx)
{
	mr_node_t *mr_node;

	if (!ctxt->numa)
		return;

	mr_node = stress_mmaprandom_get_random_used(ctxt);
	if (!mr_node)
		return;

	stress_numa_randomize_pages(ctxt->args, ctxt->numa_nodes,
				    ctxt->numa_mask, mr_node->mmap_addr,
				    mr_node->mmap_size, mr_node->mmap_page_size);
	ctxt->count[idx] += 1.0;
}
#endif

#if defined(__NR_process_madvise) &&	\
    defined(HAVE_SYSCALL) &&		\
    defined(HAVE_SYS_UIO_H)
/*
 *  stress_mmaprandom_process_madvise()
 *	madvise a mmap'd region using process_madvise
 */
static void stress_mmaprandom_process_madvise(mr_ctxt_t *ctxt, const int idx)
{
	static const int proc_advice[] = {
#if defined(MADV_COLD)
		MADV_COLD,
#endif
#if defined(MADV_COLLAPSE)
		MADV_COLLAPSE,
#endif
#if defined(MADV_PAGEOUT)
		MADV_PAGEOUT,
#endif
#if defined(MADV_WILLNEED)
		MADV_WILLNEED,
#endif
		0,
	};
	const int advice = MWC_RND_ELEMENT(proc_advice);
	mr_node_t *mr_node = stress_mmaprandom_get_random_used(ctxt);
	struct iovec iov[1];

	if (!mr_node)
		return;
	if (ctxt->pidfd == -1)
		return;

	iov[0].iov_base = mr_node->mmap_addr;
	iov[0].iov_len = mr_node->mmap_size;

	if (shim_process_madvise(ctxt->pidfd, iov, 1, advice, 0) != -1)
		ctxt->count[idx] += 1.0;
}
#endif

static const mr_funcs_t mr_funcs[] = {
	{ stress_mmaprandom_mmap_anon,		"mmap anon" },
	{ stress_mmaprandom_mmap_file,		"mmap file" },
	{ stress_mmaprandom_unmmap,		"munmap" },
	{ stress_mmaprandom_unmmap_lo_hi_addr,	"munmap lo/hi addr" },
	{ stress_mmaprandom_read,		"mem read" },
	{ stress_mmaprandom_write,		"mem write" },
	{ stress_mmaprandom_cache_flush,	"cache flush" },
#if defined(HAVE_BUILTIN_PREFETCH)
	{ stress_mmaprandom_cache_prefetch,	"cache prefetch" },
#endif
#if defined(HAVE_MREMAP)
	{ stress_mmaprandom_mremap,		"mremap" },
#endif
#if defined(HAVE_MADVISE)
	{ stress_mmaprandom_madvise,		"madvise" },
#endif
#if defined(HAVE_POSIX_MADVISE)
	{ stress_mmaprandom_posix_madvise,	"posix_madvise" },
#endif
#if defined(HAVE_MINCORE)
	{ stress_mmaprandom_mincore,		"mincore" },
#endif
#if defined(HAVE_MSYNC)
	{ stress_mmaprandom_msync,		"msync" },
#endif
#if defined(HAVE_MLOCK)
	{ stress_mmaprandom_mlock,		"mlock" },
#endif
#if defined(HAVE_MUNLOCK)
	{ stress_mmaprandom_munlock,		"munlock" },
#endif
#if defined(HAVE_MPROTECT)
	{ stress_mmaprandom_mprotect,		"mprotect" },
#endif
	{ stress_mmaprandom_unmap_first_page,	"munmap first page" },
	{ stress_mmaprandom_unmap_last_page,	"munmap last page" },
	{ stress_mmaprandom_split,		"map splitting" },
	{ stress_mmaprandom_split_hole,		"map hole splitting" },
	{ stress_mmaprandom_join,		"mmap joining" },
	{ stress_mmaprandom_fork,		"fork" },
#if defined(HAVE_LINUX_MEMPOLICY_H)
	{ stress_mmaprandom_numa_move,		"NUMA mapping move" },
#endif
#if defined(__NR_process_madvise) &&	\
    defined(HAVE_SYSCALL) &&		\
    defined(HAVE_SYS_UIO_H)
	{ stress_mmaprandom_process_madvise,	"process madvise" },
#endif
};

/*
 *  stress_mmaprandom_child()
 *	child process that attempts to unmap a lot of the
 *	pages mapped into stress-ng without killing itself with
 *	a bus error or segmentation fault.
 */
static int stress_mmaprandom_child(stress_args_t *args, void *context)
{
	mr_ctxt_t *ctxt = (mr_ctxt_t *)context;
	int rc = EXIT_SUCCESS;
	mr_node_t *mr_node;

	VOID_RET(int, stress_sighandler(args->name, SIGSEGV, stress_mmaprandom_sig_handler, NULL));
	VOID_RET(int, stress_sighandler(args->name, SIGBUS, stress_mmaprandom_sig_handler, NULL));

	ctxt->pidfd = shim_pidfd_open(getpid(), 0);

	do {
		const size_t i = stress_mwc8modn(SIZEOF_ARRAY(mr_funcs));

		mr_funcs[i].func(ctxt, i);
		stress_bogo_inc(args);
	} while (stress_continue(args));

	RB_FOREACH(mr_node, sm_used_node_tree, &sm_used_node_tree_root) {
		(void)stress_munmap_force(mr_node->mmap_addr, mr_node->mmap_size);
	}

	if (ctxt->pidfd != -1)
		(void)close(ctxt->pidfd);
	return rc;
}

/*
 *  stress_mmaprandom()
 *	stress munmap
 */
static int stress_mmaprandom(stress_args_t *args)
{
	mr_ctxt_t *ctxt;
	double t, duration;
	size_t i;
	const size_t count_size = SIZEOF_ARRAY(mr_funcs) * sizeof(*ctxt->count);
	size_t mr_nodes_size;
	int ret;
	char filename[PATH_MAX];
	int rc = EXIT_SUCCESS;

	ctxt = (mr_ctxt_t *)mmap(NULL, sizeof(*ctxt), PROT_READ | PROT_WRITE,
		MAP_SHARED | MAP_ANONYMOUS, -1, 0);
	if (ctxt == MAP_FAILED) {
		pr_inf_skip("%s: skipping stressor, cannot mmap context buffer, errno=%d (%s)\n",
			args->name, errno, strerror(errno));
		return EXIT_NO_RESOURCE;
	}
	stress_set_vma_anon_name(ctxt, sizeof(*ctxt), "context");

	ctxt->oom_avoid = !!(g_opt_flags & OPT_FLAGS_OOM_AVOID);
	ctxt->n_mr_nodes = MMAP_RANDOM_DEFAULT_MMAPPINGS;
	ctxt->numa = false;

	(void)stress_get_setting("mmaprandom-mappings", &ctxt->n_mr_nodes);
	(void)stress_get_setting("mmaprandom-numa", &ctxt->numa);

	if (ctxt->numa) {
#if defined(HAVE_LINUX_MEMPOLICY_H)
		stress_numa_mask_and_node_alloc(args, &ctxt->numa_nodes, &ctxt->numa_mask, "--mmaprandom-numa", &ctxt->numa);
#else
		if (args->instance == 0)
			pr_inf("%s: --mmaprandom-numa selected but not supported by this system, disabling option\n",
				args->name);
		ctxt->numa = false;
#endif
	}

	ctxt->page = mmap(NULL, args->page_size, PROT_READ | PROT_WRITE,
		MAP_SHARED | MAP_ANONYMOUS, -1, 0);
	if (ctxt->page == MAP_FAILED) {
		pr_inf_skip("%s: skipping stressor, cannot mmap page buffer, errno=%d (%s)\n",
			args->name, errno, strerror(errno));
		rc = EXIT_NO_RESOURCE;
		goto unmap_ctxt;
	}
	stress_set_vma_anon_name(ctxt->page, args->page_size, "io-page");

	ctxt->count = (double *)mmap(NULL, count_size, PROT_READ | PROT_WRITE,
		MAP_SHARED | MAP_ANONYMOUS, -1, 0);
	if (ctxt->count == MAP_FAILED) {
		pr_inf_skip("%s: skipping stressor, cannot mmap metrics, errno=%d (%s)\n",
			args->name, errno, strerror(errno));
		rc = EXIT_NO_RESOURCE;
		goto unmap_ctxt_page;
	}
	stress_set_vma_anon_name(ctxt->count, count_size, "counters");

	ret = stress_temp_dir_mk_args(args);
	if (ret < 0) {
		rc = stress_exit_status((int)-ret);
		goto tidy_dir;
	}
	(void)stress_temp_filename_args(args,
		filename, sizeof(filename), stress_mwc32());
#if defined(O_NOATIME)
	ctxt->file_fd = open(filename, O_CREAT | O_RDWR | O_NOATIME, S_IRUSR | S_IWUSR);
#else
	ctxt->file_fd = open(filename, O_CREAT | O_RDWR, S_IRUSR | S_IWUSR);
#endif
	if (ctxt->file_fd < 0) {
		pr_inf_skip("%s: skipping stressor, cannot create file '%s', errno=%d (%s)\n",
			args->name, filename, errno, strerror(errno));
		rc = EXIT_NO_RESOURCE;
		goto tidy_dir;
	}
	(void)shim_unlink(filename);

	(void)snprintf(filename, sizeof(filename), "mmaprandom-%" PRIdMAX "-%" PRIu32,
		(intmax_t)args->pid, args->instance);
	ctxt->mem_fd = shim_memfd_create(filename, 0);
	ctxt->page_size = args->page_size;

	mr_nodes_size = ctxt->n_mr_nodes * sizeof(*ctxt->mr_nodes);
	ctxt->mr_nodes = (mr_node_t *)mmap(NULL, mr_nodes_size, PROT_READ | PROT_WRITE,
		MAP_SHARED | MAP_ANONYMOUS, -1, 0);
	if (ctxt->mr_nodes == MAP_FAILED) {
		pr_inf_skip("%s: skipping stressor, cannot mmap %zu page structures\n",
			args->name, ctxt->n_mr_nodes);
		rc = EXIT_NO_RESOURCE;
		goto tidy_fds;
	}
	stress_set_vma_anon_name(ctxt->mr_nodes, mr_nodes_size, "page-structs");

	for (i = 0; i < ctxt->n_mr_nodes; i++) {
		ctxt->mr_nodes[i].used = false;
		RB_INSERT(sm_free_node_tree, &sm_free_node_tree_root, &ctxt->mr_nodes[i]);
		sm_free_nodes++;
	}
	for (i = 0; i < SIZEOF_ARRAY(mr_funcs); i++) {
		ctxt->count[i] = 0.0;
	}

	ctxt->args = args;

	stress_set_proc_state(args->name, STRESS_STATE_SYNC_WAIT);
	stress_sync_start_wait(args);
	stress_set_proc_state(args->name, STRESS_STATE_RUN);

	t = stress_time_now();
	while (stress_continue(args)) {
		uint32_t w, z;

		VOID_RET(int, stress_oomable_child(args, (void *)ctxt, stress_mmaprandom_child, STRESS_OOMABLE_QUIET));

		/* Ensure child never restarts from same seed */
		stress_mwc_get_seed(&w, &z);
		stress_mwc_set_seed(++w, --z);
	}
	duration = stress_time_now() - t;
	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);

	for (i = 0; i < SIZEOF_ARRAY(mr_funcs); i++) {
		char buf[64];
		const double rate = duration > 0.0 ? ctxt->count[i] / duration: 0.0;

		snprintf(buf, sizeof(buf), "%s ops/sec", mr_funcs[i].name);
		stress_metrics_set(args, i, buf, rate, STRESS_METRIC_HARMONIC_MEAN);
	}

	(void)munmap((void *)ctxt->mr_nodes, mr_nodes_size);
tidy_fds:
	(void)close(ctxt->file_fd);
	if (ctxt->mem_fd != -1)
		(void)close(ctxt->mem_fd);
	(void)munmap((void *)ctxt->count, count_size);
tidy_dir:
	(void)stress_temp_dir_rm_args(args);
unmap_ctxt_page:
	(void)munmap((void *)ctxt->page, args->page_size);
unmap_ctxt:
#if defined(HAVE_LINUX_MEMPOLICY_H)
	if (ctxt->numa_mask)
		stress_numa_mask_free(ctxt->numa_mask);
	if (ctxt->numa_nodes)
		stress_numa_mask_free(ctxt->numa_nodes);
#endif
	(void)munmap((void *)ctxt, sizeof(*ctxt));

	return rc;
}

const stressor_info_t stress_mmaprandom_info = {
	.stressor = stress_mmaprandom,
	.classifier = CLASS_VM | CLASS_OS,
	.verify = VERIFY_NONE,
	.opts = opts,
	.help = help
};

#else
const stressor_info_t stress_mmaprandom_info = {
	.stressor = stress_unimplemented,
	.classifier = CLASS_VM | CLASS_OS,
	.verify = VERIFY_NONE,
	.opts = opts,
	.help = help,
	.unimplemented_reason = "not implemented, requires BSD red_black tree support"
};
#endif
