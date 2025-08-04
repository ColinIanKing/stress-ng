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
#include "core-out-of-memory.h"
#include "core-put.h"

#if defined(HAVE_SYS_TREE_H)
#include <sys/tree.h>
#endif

#if defined(HAVE_SYS_QUEUE_H)
#include <sys/queue.h>
#endif

#if defined(HAVE_BSD_SYS_TREE_H)
#include <bsd/sys/tree.h>
#endif

#if defined(HAVE_LIB_BSD) &&	\
    !defined(__APPLE__)
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
#endif

#define MMAP_RANDOM_MIN_MAPPINGS	(1)
#define MMAP_RANDOM_MAX_MAPPINGS	(1024 * 1024)
#define MMAP_RANDOM_DEFAULT_MMAPPINGS	(1024)

#define MAX_PAGES_PER_MAPPING		(8)

static const stress_help_t help[] = {
	{ NULL,	"mmaprandom N",	 	 "start N workers stressing random memory mapping operations" },
	{ NULL,	"mmaprandom-ops N",	 "stop after N mmaprandom bogo operations" },
	{ NULL, "mmaprandom-mappings N", "maximum number of mappings to be made" },
	{ NULL,	NULL,		 	 NULL }
};

static const stress_opt_t opts[] = {
        { OPT_mmaprandom_mappings, "mmaprandom_mappings", TYPE_ID_SIZE_T, MMAP_RANDOM_MIN_MAPPINGS, MMAP_RANDOM_MAX_MAPPINGS, NULL },
	END_OPT,
};

#if defined(HAVE_RB_TREE)

typedef struct mr_node {
	void *mmap_addr;	/* mapping start addr */
	size_t mmap_size;	/* mapping size in bytes */
	int mmap_prot;		/* mapping protecton */
	int mmap_flags;		/* mapping flags */
	off_t mmap_offset;	/* file based mmap offset into file */
	int mmap_fd;		/* file_fd or mem_fd that was mmap'd to */
	bool used;		/* true = mapping is used */
	RB_ENTRY(mr_node) rb;	/* rb tree node */
} mr_node_t;

typedef struct {
	stress_args_t *args;	/* stress-ng arguments */
	mr_node_t *mras;	/* array of all mr_nodes */
	size_t n_mras;		/* size of mras array */
	size_t page_size;	/* page size in bytes */
	int file_fd;		/* file mmap file descriptor */
	int mem_fd;		/* memfd mmap file descriptor */
	uint8_t *page;		/* page mapping for writes */
	double *count;		/* array of usage counters for each mr_func_t */
} mr_ctxt_t;

typedef void (*mr_func_t)(mr_ctxt_t *ctxt, const int idx);

typedef struct {
	const mr_func_t func;	/* memory operation function */
	const char *name;	/* human readable name of operation */
} mr_funcs_t;

/*
 *  mra_page_cmp()
 *	tree compare to sort by mapping address, for allocated mappings
 */
static int OPTIMIZE3 mra_page_cmp(mr_node_t *mra1, mr_node_t *mra2)
{
        if (mra1->mmap_addr == mra2->mmap_addr)
                return 0;
        if (mra1->mmap_addr > mra2->mmap_addr)
                return 1;
        else
                return -1;
}

/*
 * mra_node_cmp
 *	treee compare to sort by node addresses, for free'd mras
 */
static int OPTIMIZE3 mra_node_cmp(mr_node_t *mra1, mr_node_t *mra2)
{
        if (mra1 == mra2)
                return 0;
        if (mra1 > mra2)
                return 1;
        else
                return -1;
}

/* Used nodes are ordered by mmap addr */
static RB_HEAD(sm_used_node_tree, mr_node) sm_used_node_tree_root;
RB_PROTOTYPE(sm_used_node_tree, mr_node, rb, mra_page_cmp);
RB_GENERATE(sm_used_node_tree, mr_node, rb, mra_page_cmp);
static size_t sm_used_nodes;

/* Free nodes are ordered by the node's own addr */
static RB_HEAD(sm_free_node_tree, mr_node) sm_free_node_tree_root;
RB_PROTOTYPE(sm_free_node_tree, mr_node, rb, mra_node_cmp);
RB_GENERATE(sm_free_node_tree, mr_node, rb, mra_node_cmp);
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
#if defined(MAP_HUGETLB) && defined(MAP_HUGE_SHIFT) && 0
	MAP_HUGETLB | (21 << MAP_HUGE_SHIFT),
	MAP_HUGETLB | (30 << MAP_HUGE_SHIFT),
#endif
};

/*
 * madvise options
 */
#if defined(HAVE_MADVISE)
static const int madvise_options[] = {
	0,
#if defined(MADV_NORMAL)
	MADV_NORMAL,
#endif
#if defined(MADV_RANDOM)
	MADV_RANDOM,
#endif
#if defined(MADV_SEQUENTIAL)
	MADV_SEQUENTIAL,
#endif
#if defined(MADV_WILLNEED)
	MADV_WILLNEED,
#endif
#if defined(MADV_DONTNEED)
	MADV_DONTNEED,
#endif
#if defined(MADV_REMOVE)
	MADV_REMOVE,
#endif
#if defined(MADV_DONTFORK)
	MADV_DONTFORK,
#endif
#if defined(MADV_DOFORK)
	MADV_DOFORK,
#endif
#if defined(MADV_MERGEABLE)
	MADV_MERGEABLE,
#endif
#if defined(MADV_UNMERGEABLE)
	MADV_UNMERGEABLE,
#endif
#if defined(MADV_SOFT_OFFLINE)
	MADV_SOFT_OFFLINE,
#endif
#if defined(MADV_HUGEPAGE)
	MADV_HUGEPAGE,
#endif
#if defined(MADV_NOHUGEPAGE)
	MADV_NOHUGEPAGE,
#endif
#if defined(MADV_DONTDUMP)
	MADV_DONTDUMP,
#endif
#if defined(MADV_DODUMP)
	MADV_DODUMP,
#endif
#if defined(MADV_FREE)
	MADV_FREE,
#endif
#if defined(MADV_HWPOISON)
	MADV_HWPOISON,
#endif
#if defined(MADV_WIPEONFORK)
	MADV_WIPEONFORK,
#endif
#if defined(MADV_KEEPONFORK)
	MADV_KEEPONFORK,
#endif
#if defined(MADV_INHERIT_ZERO)
	MADV_INHERIT_ZERO,
#endif
#if defined(MADV_COLD)
	MADV_COLD,
#endif
#if defined(MADV_PAGEOUT)
	MADV_PAGEOUT,
#endif
#if defined(MADV_POPULATE_READ)
	MADV_POPULATE_READ,
#endif
#if defined(MADV_POPULATE_WRITE)
	MADV_POPULATE_WRITE,
#endif
#if defined(MADV_DONTNEED_LOCKED)
	MADV_DONTNEED_LOCKED,
#endif
/* Linux 6.0 */
#if defined(MADV_COLLAPSE)
	MADV_COLLAPSE,
#endif
/* FreeBSD */
#if defined(MADV_AUTOSYNC)
	MADV_AUTOSYNC,
#endif
/* FreeBSD and DragonFlyBSD */
#if defined(MADV_CORE)
	MADV_CORE,
#endif
/* FreeBSD */
#if defined(MADV_PROTECT)
	MADV_PROTECT,
#endif
/* Linux 5.14 */
#if defined(MADV_POPULATE_READ)
	MADV_POPULATE_READ,
#endif
/* Linux 5.14 */
#if defined(MADV_POPULATE_WRITE)
	MADV_POPULATE_WRITE,
#endif
/* Linux 6.12 */
#if defined(MADV_GUARD_INSTALL) &&	\
    defined(MADV_NORMAL)
	MADV_GUARD_INSTALL,
#endif
#if defined(MADV_GUARD_REMOVE)
	MADV_GUARD_REMOVE,
#endif
/* OpenBSD */
#if defined(MADV_SPACEAVAIL)
	MADV_SPACEAVAIL,
#endif
/* OS X */
#if defined(MADV_ZERO_WIRED_PAGES)
	MADV_ZERO_WIRED_PAGES,
#endif
/* Solaris */
#if defined(MADV_ACCESS_DEFAULT)
	MADV_ACCESS_DEFAULT,
#endif
/* Solaris */
#if defined(MADV_ACCESS_LWP)
	MADV_ACCESS_LWP,
#endif
/* Solaris */
#if defined(MADV_ACCESS_MANY)
	MADV_ACCESS_MANY,
#endif
/* DragonFlyBSD */
#if defined(MADV_INVAL)
	MADV_INVAL,
#endif
/* DragonFlyBSD */
#if defined(MADV_NOCORE)
	MADV_NOCORE,
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
 *  stress_mmaprandom_mmap_anon()
 *  	perform anonymous mmap
 */
static void stress_mmaprandom_mmap_anon(mr_ctxt_t *ctxt, const int idx)
{
	const size_t page_size = ctxt->page_size;
	size_t pages = stress_mwc8modn(MAX_PAGES_PER_MAPPING) + 1;
	size_t size = page_size *pages, i, j;
	uint8_t *addr;
	int prot_flag, mmap_flag, extra_flags = 0;
	mr_node_t *mra;

	prot_flag = prot_flags[stress_mwc8modn(SIZEOF_ARRAY(prot_flags))];
	mmap_flag = mmap_anon_flags[stress_mwc8modn(SIZEOF_ARRAY(mmap_anon_flags))];

	mra = RB_MIN(sm_free_node_tree, &sm_free_node_tree_root);
	if (!mra)
		return;

	for (i = 0; i < SIZEOF_ARRAY(mmap_extra_flags); i++)
		extra_flags |= mmap_extra_flags[stress_mwc8modn(SIZEOF_ARRAY(mmap_extra_flags))];

	j = stress_mwc8modn(SIZEOF_ARRAY(mmap_extra_flags));
	for (;;) {
		int old_flags;

		addr = (uint8_t *)mmap(NULL, size, prot_flag, mmap_flag | extra_flags, -1, 0);
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

	RB_REMOVE(sm_free_node_tree, &sm_free_node_tree_root, mra);
	sm_free_nodes--;
	mra->mmap_addr = addr;
	mra->mmap_size = size;
	mra->mmap_prot = prot_flag;
	mra->mmap_flags = mmap_flag | extra_flags;
	mra->mmap_offset = 0;
	mra->mmap_fd = -1;
	mra->used = true;
	RB_INSERT(sm_used_node_tree, &sm_used_node_tree_root, mra);
	sm_used_nodes++;
}

static int stress_mmaprandom_mmap_file_write(mr_ctxt_t *ctxt, const int fd, const off_t offset, const size_t pages)
{
	size_t i;

	if (fd < 0)
		return -1;

	if (lseek(fd, offset, SEEK_SET) == (off_t) -1)
		return -1;
	for (i = 0; i < pages; i++) {
		stress_uint8rnd4(ctxt->page, ctxt->page_size);
		if (write(fd, ctxt->page, ctxt->page_size) != (ssize_t)ctxt->page_size)
			return -1;
	}
	return 0;
}

/*
 *  stress_mmaprandom_mmap_file()
 *  	perform file based mmap
 */
static void stress_mmaprandom_mmap_file(mr_ctxt_t *ctxt, const int idx)
{
	const size_t page_size = ctxt->page_size;
	size_t i, j;
	const size_t pages = stress_mwc8modn(MAX_PAGES_PER_MAPPING) + 1;
	const off_t offset = stress_mwc8modn(MAX_PAGES_PER_MAPPING) * page_size;
	const size_t size = page_size * pages;
	uint8_t *addr;
	int prot_flag, mmap_flag, extra_flags = 0;
	mr_node_t *mra;
	int fd;

	mmap_flag = mmap_file_flags[stress_mwc8modn(SIZEOF_ARRAY(mmap_file_flags))];

	if (ctxt->mem_fd < 0)
		fd = ctxt->file_fd;
	else
		fd = stress_mwc1() ? ctxt->file_fd : ctxt->mem_fd;

	mra = RB_MIN(sm_free_node_tree, &sm_free_node_tree_root);
	if (!mra)
		return;

	if (stress_mmaprandom_mmap_file_write(ctxt, fd, offset, pages) < 0)
		return;

	for (i = 0; i < SIZEOF_ARRAY(mmap_extra_flags); i++)
		extra_flags |= mmap_extra_flags[stress_mwc8modn(SIZEOF_ARRAY(mmap_extra_flags))];

	prot_flag = prot_flags[stress_mwc8modn(SIZEOF_ARRAY(prot_flags))];
	j = stress_mwc8modn(SIZEOF_ARRAY(mmap_extra_flags));

	for (;;) {
		int old_flags;

		addr = (uint8_t *)mmap(NULL, size, prot_flag, mmap_flag | extra_flags, fd, offset);
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

	RB_REMOVE(sm_free_node_tree, &sm_free_node_tree_root, mra);
	sm_free_nodes--;
	mra->mmap_addr = addr;
	mra->mmap_size = size;
	mra->mmap_prot = prot_flag;
	mra->mmap_flags = mmap_flag | extra_flags;
	mra->mmap_fd = fd;
	mra->mmap_offset = offset;
	mra->used = true;
	RB_INSERT(sm_used_node_tree, &sm_used_node_tree_root, mra);
	sm_used_nodes++;
}

/*
 *  stress_mmaprandom_get_random_used()
 *	get an randomly selected used mra, suboptimial linear scan,
 *	needs improving.
 */
static mr_node_t *stress_mmaprandom_get_random_used(mr_ctxt_t *ctxt)
{
	size_t i, n;

	n = stress_mwc32modn((uint32_t)ctxt->n_mras);

	for (i = 0; i < ctxt->n_mras; i++) {
		mr_node_t *mra = &ctxt->mras[n];

		if (mra->used)
			return mra;
		n++;
		if (n >= ctxt->n_mras)
			n = 0;
	}
	return NULL;
}

/*
 *  stress_mmaprandom_get_random_size()
 *	get an randomly selected used mra
 */
static size_t stress_mmaprandom_get_random_size(const size_t mmap_size, const size_t page_size)
{
	size_t n = mmap_size / page_size;

	return page_size * (1 + stress_mwc8modn(n));
}

/*
 *  stress_mmaprandom_unmmap()
 *	unmap pages
 */
static void stress_mmaprandom_unmmap(mr_ctxt_t *ctxt, const int idx)
{
	mr_node_t *mra = stress_mmaprandom_get_random_used(ctxt);

	if (mra) {
		if (munmap((void *)mra->mmap_addr, mra->mmap_size) == 0) {
			ctxt->count[idx] += 1.0;

			mra->mmap_addr = NULL;
			mra->mmap_size = 0;
			mra->mmap_prot = 0;
			mra->mmap_flags = 0;
			mra->mmap_offset = 0;
			mra->mmap_fd = -1;
			mra->used = false;
			RB_REMOVE(sm_used_node_tree, &sm_used_node_tree_root, mra);
			sm_used_nodes--;
			RB_INSERT(sm_free_node_tree, &sm_free_node_tree_root, mra);
			sm_free_nodes++;
		}
	}
}

/*
 *  stress_mmaprandom_unmmap_lo_hi_addr()
 *	unmap lowest or highest mapped address
 */
static void stress_mmaprandom_unmmap_lo_hi_addr(mr_ctxt_t *ctxt, const int idx)
{
	mr_node_t *mra = stress_mwc1() ?
		RB_MIN(sm_used_node_tree, &sm_used_node_tree_root) :
		RB_MAX(sm_used_node_tree, &sm_used_node_tree_root);

	(void)ctxt;

	if (mra) {
		if (munmap((void *)mra->mmap_addr, mra->mmap_size) == 0) {
			ctxt->count[idx] += 1.0;

			mra->mmap_addr = NULL;
			mra->mmap_size = 0;
			mra->mmap_prot = 0;
			mra->mmap_flags = 0;
			mra->mmap_offset = 0;
			mra->mmap_fd = -1;
			mra->used = false;
			RB_REMOVE(sm_used_node_tree, &sm_used_node_tree_root, mra);
			sm_used_nodes--;
			RB_INSERT(sm_free_node_tree, &sm_free_node_tree_root, mra);
			sm_free_nodes++;
		}
	}
}

/*
 *  stress_mmaprandom_read()
 *	read from mapping
 */
static void stress_mmaprandom_read(mr_ctxt_t *ctxt, const int idx)
{
	mr_node_t *mra = stress_mmaprandom_get_random_used(ctxt);

	if (mra && mra->mmap_prot & PROT_READ) {
		uint64_t *volatile ptr = mra->mmap_addr;
		const uint64_t *end = (uint64_t *)((intptr_t)ptr + mra->mmap_size);

		while (ptr < end) {
			(void )*ptr;
			ptr++;
		}
		ctxt->count[idx] += 1.0;
	}
}

/*
 *  stress_mmaprandom_read()
 *	write to a mapping
 */
static void stress_mmaprandom_write(mr_ctxt_t *ctxt, const int idx)
{
	mr_node_t *mra = stress_mmaprandom_get_random_used(ctxt);

	if (mra && mra->mmap_prot & PROT_WRITE) {
		uint64_t *volatile ptr = mra->mmap_addr;
		const uint64_t *end = (uint64_t *)((intptr_t)ptr + mra->mmap_size);
		uint64_t v = stress_mwc64();

		while (ptr < end) {
			*ptr = v;
			ptr++;
		}
		ctxt->count[idx] += 1.0;
	}
}

/*
 *  stress_mmaprandom_cache_flush()
 *	cache flush a mapping
 */
static void stress_mmaprandom_cache_flush(mr_ctxt_t *ctxt, const int idx)
{
	mr_node_t *mra = stress_mmaprandom_get_random_used(ctxt);

	if (mra && mra->mmap_prot & PROT_WRITE) {
		stress_cpu_data_cache_flush(mra->mmap_addr, mra->mmap_size);
		ctxt->count[idx] += 1.0;
	}
}

#if defined(HAVE_MREMAP)
/*
 *  stress_mmaprandom_mremap
 *  	memory remap a mapping
 */
static void stress_mmaprandom_mremap(mr_ctxt_t *ctxt, const int idx)
{
	mr_node_t *mra = stress_mmaprandom_get_random_used(ctxt);

	if (mra) {
		size_t pages = stress_mwc8modn(MAX_PAGES_PER_MAPPING) + 1;
		size_t new_size = ctxt->page_size * pages;
		void *new_addr;

		if (new_size > mra->mmap_size) {
			/*
			 * Cannot expand anonymous mappings as there no
			 * backing to expand into
			 */
			if (mra->mmap_flags & MAP_ANONYMOUS)
				return;

			/*
			 * File mapped? Then ensure we have backing
			 * written to the file
			 */
			if (mra->mmap_fd != -1)
				if (stress_mmaprandom_mmap_file_write(ctxt, mra->mmap_fd, mra->mmap_offset, pages) < 0)
					return;
		}

		new_addr = mremap(mra->mmap_addr, mra->mmap_size, new_size, MREMAP_MAYMOVE);
		if (new_addr != MAP_FAILED) {
			ctxt->count[idx] += 1.0;

			mra->mmap_addr = new_addr;
			mra->mmap_size = new_size;
		}
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
	mr_node_t *mra = stress_mmaprandom_get_random_used(ctxt);

	if (mra) {
		const int advice = madvise_options[stress_mwc8modn(SIZEOF_ARRAY(madvise_options))];

		if (madvise(mra->mmap_addr, mra->mmap_size, advice) == 0)
			ctxt->count[idx] += 1.0;
	}
}
#endif

#if defined(HAVE_POSIX_MADVISE)
/*
 *  stress_mmaprandom_posix_madvise()
 *	posix_madvise a mapping
 */
static void stress_mmaprandom_posix_madvise(mr_ctxt_t *ctxt, const int idx)
{
	mr_node_t *mra = stress_mmaprandom_get_random_used(ctxt);

	if (mra) {
		const int advice = posix_madvise_options[stress_mwc8modn(SIZEOF_ARRAY(posix_madvise_options))];

		if (posix_madvise(mra->mmap_addr, mra->mmap_size, advice) == 0)
			ctxt->count[idx] += 1.0;
	}
}
#endif

#if defined(HAVE_MINCORE)
/*
 *  stress_mmaprandom_mincore()
 *	check memory resident pages via mincore
 */
static void stress_mmaprandom_mincore(mr_ctxt_t *ctxt, const int idx)
{
	mr_node_t *mra = stress_mmaprandom_get_random_used(ctxt);
	unsigned char vec[MAX_PAGES_PER_MAPPING];

	if (mra) {
		const size_t max_size = MAX_PAGES_PER_MAPPING * ctxt->page_size;
		size_t size = mra->mmap_size > max_size ? max_size : mra->mmap_size;

		if (mincore(mra->mmap_addr, size, vec) == 0)
			ctxt->count[idx] += 1.0;
	}
}
#endif

#if defined(HAVE_MSYNC)
/*
 *  stress_mmaprandom_mincore()
 *	msync a mapping
 */
static void stress_mmaprandom_msync(mr_ctxt_t *ctxt, const int idx)
{
	mr_node_t *mra = stress_mmaprandom_get_random_used(ctxt);

	if (mra) {
		size_t size = stress_mmaprandom_get_random_size(mra->mmap_size, ctxt->page_size);
		int flags = msync_flags[stress_mwc8modn(SIZEOF_ARRAY(msync_flags))];

		if (msync(mra->mmap_addr, size, flags) == 0)
			ctxt->count[idx] += 1.0;
	}
}
#endif

#if defined(HAVE_MLOCK)
/*
 *  stress_mmaprandom_mlock()
 *	memory lock a mapping
 */
static void stress_mmaprandom_mlock(mr_ctxt_t *ctxt, const int idx)
{
	mr_node_t *mra = stress_mmaprandom_get_random_used(ctxt);

	if (mra) {
		if (mlock(mra->mmap_addr, mra->mmap_size) == 0)
			ctxt->count[idx] += 1.0;
	}
}
#endif

#if defined(HAVE_MUNLOCK)
/*
 *  stress_mmaprandom_munlock()
 *	memory unlock a mapping
 */
static void stress_mmaprandom_munlock(mr_ctxt_t *ctxt, const int idx)
{
	mr_node_t *mra = stress_mmaprandom_get_random_used(ctxt);

	if (mra) {
		if (munlock(mra->mmap_addr, mra->mmap_size) == 0)
			ctxt->count[idx] += 1.0;
	}
}
#endif

#if defined(HAVE_MPROTECT)
/*
 *  stress_mmaprandom_mprotect
 * 	memory protect a mapping
 */
static void stress_mmaprandom_mprotect(mr_ctxt_t *ctxt, const int idx)
{
	mr_node_t *mra = stress_mmaprandom_get_random_used(ctxt);

	if (mra) {
		const int prot_flag = prot_flags[stress_mwc8modn(SIZEOF_ARRAY(prot_flags))];

		if (mprotect(mra->mmap_addr, mra->mmap_size, prot_flag) == 0) {
			mra->mmap_prot = prot_flag;
			ctxt->count[idx] += 1.0;
		}
	}
}
#endif

static void stress_mmaprandom_unmap_first_page(mr_ctxt_t *ctxt, const int idx)
{
	mr_node_t *mra = stress_mmaprandom_get_random_used(ctxt);
	const size_t page_size = ctxt->page_size;

	if (mra && (mra->mmap_size >= (2 * page_size))) {
		uint8_t *ptr = mra->mmap_addr;

		if (munmap(ptr, page_size) < 0)
			return;
		RB_REMOVE(sm_used_node_tree, &sm_used_node_tree_root, mra);

		ptr += page_size;
		mra->mmap_addr = ptr;
		mra->mmap_size -= page_size;
		mra->mmap_offset += page_size;

		RB_INSERT(sm_used_node_tree, &sm_used_node_tree_root, mra);

		ctxt->count[idx] += 1.0;
	}
}

static void stress_mmaprandom_unmap_last_page(mr_ctxt_t *ctxt, const int idx)
{
	mr_node_t *mra = stress_mmaprandom_get_random_used(ctxt);
	const size_t page_size = ctxt->page_size;

	if (mra && (mra->mmap_size >= (2 * page_size))) {
		uint8_t *ptr = mra->mmap_addr;

		ptr += (mra->mmap_size - page_size);
		if (munmap(ptr, page_size) < 0)
			return;
		mra->mmap_size -= page_size;
		ctxt->count[idx] += 1.0;
	}
}

/*
 *  stress_mmaprandom_split()
 *	break a mapping into two mappings, unmap a page between them
 *
 */
static void stress_mmaprandom_split(mr_ctxt_t *ctxt, const int idx)
{
	mr_node_t *mra = stress_mmaprandom_get_random_used(ctxt);
	const size_t page_size = ctxt->page_size;

	if (mra && (mra->mmap_size >= (3 * page_size))) {
		uint8_t *ptr = mra->mmap_addr;
		mr_node_t *new_mra;

		new_mra = RB_MIN(sm_free_node_tree, &sm_free_node_tree_root);
		if (!new_mra)
			return;

		ptr += page_size;
		if (munmap((void *)ptr, page_size) < 0) {
			pr_inf("failed\n");
			return;
		}

		ptr += page_size;

		RB_REMOVE(sm_free_node_tree, &sm_free_node_tree_root, new_mra);
		sm_free_nodes--;
		new_mra->mmap_addr = (void *)ptr;
		new_mra->mmap_size = mra->mmap_size - (2 * page_size);
		new_mra->mmap_prot = mra->mmap_prot;
		new_mra->mmap_flags = mra->mmap_flags;
		new_mra->mmap_offset = mra->mmap_offset + (2 * page_size);
		new_mra->mmap_fd = mra->mmap_fd;
		new_mra->used = true;
		RB_INSERT(sm_used_node_tree, &sm_used_node_tree_root, new_mra);
		sm_used_nodes++;

		mra->mmap_size = page_size;
		ctxt->count[idx] += 1.0;
	}
}

/*
 *  stress_mmaprandom_join()
 *	join to matching mmap'd regions together if they are
 *	next to each other, free's up a used mra.
 */
static void stress_mmaprandom_join(mr_ctxt_t *ctxt, const int idx)
{
	mr_node_t *mra, *next;
	const size_t page_size = ctxt->page_size;
	const size_t max_size = page_size * MAX_PAGES_PER_MAPPING;

	for (mra = RB_MIN(sm_used_node_tree, &sm_used_node_tree_root); mra; mra = next) {
		uint8_t *ptr = mra->mmap_addr;
		mr_node_t find_mra, *found_mra;

                next = RB_NEXT(sm_used_node_tree, &sm_used_node_tree_root, mra);

		/* mappings right next to each other */
		find_mra.mmap_addr = (void *)(ptr + mra->mmap_size);

		found_mra = RB_FIND(sm_used_node_tree, &sm_used_node_tree_root, &find_mra);
		if (found_mra &&
		    (found_mra->mmap_fd == mra->mmap_fd) &&
		    (found_mra->mmap_prot == mra->mmap_prot) &&
		    (found_mra->mmap_flags == mra->mmap_flags) &&
		    (found_mra->mmap_size + mra->mmap_size <= max_size)) {
			mra->mmap_size += found_mra->mmap_size;

			RB_REMOVE(sm_used_node_tree, &sm_used_node_tree_root, found_mra);
			sm_used_nodes--;
			found_mra->used = false;
			RB_INSERT(sm_free_node_tree, &sm_free_node_tree_root, found_mra);
			sm_free_nodes++;

			ctxt->count[idx] += 1.0;
			return;
		}
#if defined(MAP_FIXED)
		/* anonymous mappings with a page hole between each other */
		find_mra.mmap_addr = (void *)(ptr + mra->mmap_size + page_size);

		found_mra = RB_FIND(sm_used_node_tree, &sm_used_node_tree_root, &find_mra);
		if (found_mra &&
		    (found_mra->mmap_fd == -1) && (mra->mmap_fd == -1) &&
		    (found_mra->mmap_prot == mra->mmap_prot) &&
		    (found_mra->mmap_flags == mra->mmap_flags) &&
		    (found_mra->mmap_size + mra->mmap_size <= max_size)) {
			void *addr = ptr + mra->mmap_size;

			addr = mmap(addr, page_size, mra->mmap_prot, MAP_FIXED | mra->mmap_flags, -1, 0);
			if (addr != MAP_FAILED) {
				mra->mmap_size += found_mra->mmap_size + page_size;

				RB_REMOVE(sm_used_node_tree, &sm_used_node_tree_root, found_mra);
				sm_used_nodes--;
				found_mra->used = false;
				RB_INSERT(sm_free_node_tree, &sm_free_node_tree_root, found_mra);
				sm_free_nodes++;

				ctxt->count[idx] += 1.0;
				return;
			}
		}
#endif
	}
}

static const mr_funcs_t mr_funcs[] = {
	{ stress_mmaprandom_mmap_anon,		"mmap anon" },
	{ stress_mmaprandom_mmap_file,		"mmap file" },
	{ stress_mmaprandom_unmmap,		"munmap" },
	{ stress_mmaprandom_unmmap_lo_hi_addr,	"munmap lo/hi addr" },
	{ stress_mmaprandom_read,		"mem read" },
	{ stress_mmaprandom_write,		"mem write" },
	{ stress_mmaprandom_cache_flush,	"cache flush" },
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
	{ stress_mmaprandom_join,		"mmap joining" },
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
	mr_node_t *mra, *next;

	VOID_RET(int, stress_sighandler(args->name, SIGSEGV, stress_mmaprandom_sig_handler, NULL));
	VOID_RET(int, stress_sighandler(args->name, SIGBUS, stress_mmaprandom_sig_handler, NULL));

	do {
		size_t i = stress_mwc8modn(SIZEOF_ARRAY(mr_funcs));

		mr_funcs[i].func(ctxt, i);
		stress_bogo_inc(args);
	} while (stress_continue(args));

	for (mra = RB_MIN(sm_used_node_tree, &sm_used_node_tree_root); mra; mra = next) {
                next = RB_NEXT(sm_used_node_tree, &sm_used_node_tree_root, mra);
		(void)munmap(mra->mmap_addr, mra->mmap_size);
                RB_REMOVE(sm_used_node_tree, &sm_used_node_tree_root, mra);
        }

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
	size_t count_size = SIZEOF_ARRAY(mr_funcs) * sizeof(*ctxt->count);
	int ret;
	char filename[PATH_MAX];

	ctxt = (mr_ctxt_t *)mmap(NULL, sizeof(*ctxt), PROT_READ | PROT_WRITE,
		MAP_SHARED | MAP_ANONYMOUS, -1, 0);
	if (ctxt == MAP_FAILED) {
		pr_inf_skip("%s: skipping stressor, cannot mmap context buffer, errno=%d (%s)\n",
			args->name, errno, strerror(errno));
		return EXIT_NO_RESOURCE;
	}
	stress_set_vma_anon_name(ctxt, sizeof(*ctxt), "context");

	ctxt->page = mmap(NULL, args->page_size, PROT_READ | PROT_WRITE,
		MAP_SHARED | MAP_ANONYMOUS, -1, 0);
	if (ctxt->page == MAP_FAILED) {
		pr_inf_skip("%s: skipping stressor, cannot mmap page buffer, errno=%d (%s)\n",
			args->name, errno, strerror(errno));
		(void)munmap((void *)ctxt, sizeof(*ctxt));
		return EXIT_NO_RESOURCE;
	}

	ctxt->count = (double *)mmap(NULL, count_size, PROT_READ | PROT_WRITE,
		MAP_SHARED | MAP_ANONYMOUS, -1, 0);
	if (ctxt->count == MAP_FAILED) {
		pr_inf_skip("%s: skipping stressor, cannot mmap metrics, errno=%d (%s)\n",
			args->name, errno, strerror(errno));
		(void)munmap((void *)ctxt->page, args->page_size);
		(void)munmap((void *)ctxt, sizeof(*ctxt));
		return EXIT_NO_RESOURCE;
	}

	ret = stress_temp_dir_mk_args(args);
        if (ret < 0)
		return stress_exit_status((int)-ret);
	(void)stress_temp_filename_args(args,
		filename, sizeof(filename), stress_mwc32());
	ctxt->file_fd = open(filename, O_CREAT | O_RDWR, S_IRUSR | S_IWUSR);
	if (ctxt->file_fd < 0) {
		pr_inf_skip("%s: skipping stressor, cannot create file '%s', errno=%d (%s)\n",
			args->name, filename, errno, strerror(errno));
		(void)stress_temp_dir_rm_args(args);
		(void)munmap((void *)ctxt->count, count_size);
		(void)munmap((void *)ctxt->page, args->page_size);
		(void)munmap((void *)ctxt, sizeof(*ctxt));
		return EXIT_NO_RESOURCE;
	}
	(void)shim_unlink(filename);

	(void)snprintf(filename, sizeof(filename), "mmaprandom-%" PRIdMAX "-%" PRIu32,
		(intmax_t)args->pid, args->instance);
	ctxt->mem_fd = shim_memfd_create(filename, 0);

	ctxt->page_size = args->page_size;
	ctxt->n_mras = MMAP_RANDOM_DEFAULT_MMAPPINGS;

	(void)stress_get_setting("mmaprandom_mappings", &ctxt->n_mras);

	ctxt->mras = (mr_node_t *)calloc(ctxt->n_mras, sizeof(*ctxt->mras));
	if (ctxt->mras == NULL) {
		pr_inf_skip("%s: skipping stressor, failed to allocate %zu mmap page structures\n",
			args->name, ctxt->n_mras);
		if (ctxt->mem_fd != -1)
			(void)close(ctxt->mem_fd);
		(void)close(ctxt->file_fd);
		(void)stress_temp_dir_rm_args(args);
		(void)munmap((void *)ctxt->count, count_size);
		(void)munmap((void *)ctxt->page, args->page_size);
		(void)munmap((void *)ctxt, sizeof(*ctxt));
		return EXIT_NO_RESOURCE;
	}

	for (i = 0; i < ctxt->n_mras; i++) {
		ctxt->mras[i].used = false;
		RB_INSERT(sm_free_node_tree, &sm_free_node_tree_root, &ctxt->mras[i]);
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

	free(ctxt->mras);
	(void)close(ctxt->file_fd);
	if (ctxt->mem_fd != -1)
		(void)close(ctxt->mem_fd);
	(void)stress_temp_dir_rm_args(args);
	(void)munmap((void *)ctxt->count, count_size);
	(void)munmap((void *)ctxt->page, args->page_size);
	(void)munmap((void *)ctxt, sizeof(*ctxt));

	return EXIT_SUCCESS;
}

const stressor_info_t stress_mmaprandom_info = {
	.stressor = stress_mmaprandom,
	.classifier = CLASS_VM | CLASS_OS,
	.verify = VERIFY_ALWAYS,
	.opts = opts,
	.help = help
};

#else
const stressor_info_t stress_mmaprandom_info = {
	.stressor = stress_unimplemented,
	.classifier = CLASS_VM | CLASS_OS,
	.verify = VERIFY_ALWAYS,
	.opts = opts,
        .help = help,
	.unimplemented_reason = "not inmplemented, requires BSD red_black tree support"
};
#endif
