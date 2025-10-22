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
#include "core-builtin.h"
#include "core-cpu-cache.h"
#include "core-killpid.h"
#include "core-mmap.h"
#include "core-numa.h"
#include "core-out-of-memory.h"

#define MMAP_MAPPINGS_MAX		(128)
#define MMAP_SIZE_MAP			(512)	/* in pages */

#define PAGE_WR_FLAG			(0x01)
#define PAGE_RD_FLAG			(0x02)

#define MIN_MMAPTORTURE_BYTES		(16 * MB)
#define MAX_MMAPTORTURE_BYTES   	(MAX_MEM_LIMIT)
#define DEFAULT_MMAPTORTURE_BYTES	(256 * MB)

#define MIN_MMAPTORTURE_MSYNC		(0)
#define MAX_MMAPTORTURE_MSYNC		(100)
#define DEFAULT_MMAPTORTURE_MSYNC	(10)


static const stress_help_t help[] = {
	{ NULL,	"mmaptorture N",	"start N workers torturing page mappings" },
	{ NULL, "mmaptorture-bytes N",	"size of file backed region to be memory mapped" },
	{ NULL, "mmaptorture-msync N",	"percentage of pages to be msync'd (default 10%)" },
	{ NULL,	"mmaptorture-ops N",	"stop after N mmaptorture bogo operations" },
	{ NULL,	NULL,			NULL }
};

static const stress_opt_t opts[] = {
        { OPT_mmaptorture_bytes, "mmaptorture-bytes", TYPE_ID_SIZE_T_BYTES_VM, MIN_MMAPTORTURE_BYTES, MAX_MMAPTORTURE_BYTES, NULL },
	{ OPT_mmaptorture_msync, "mmaptorture-msync", TYPE_ID_UINT32, MIN_MMAPTORTURE_MSYNC, MAX_MMAPTORTURE_MSYNC, NULL },
	END_OPT
};

#if defined(HAVE_SIGLONGJMP)

typedef struct {
	uint8_t *addr;
	size_t	size;
	off_t	offset;
} mmap_info_t;

typedef struct {
	uint64_t	mmap_pages;
	uint64_t	sync_pages;
	uint64_t	lock_pages;
	uint64_t	mprotect_pages;
	uint64_t	madvise_pages;
	uint64_t	remapped_pages;
	uint64_t	sigbus_traps;
	uint64_t	sigsegv_traps;
	uint64_t	mmap_retries;
} mmap_stats_t;

static sigjmp_buf jmp_env;
static int mmap_fd;
static const char *name = "mmaptorture";
static uint8_t *mmap_data;
static size_t mmap_bytes = DEFAULT_MMAPTORTURE_BYTES;
static bool mmap_bytes_adjusted = false;
static mmap_stats_t *mmap_stats;

static const int madvise_options[] = {
#if defined(SHIM_MADV_NORMAL)
	SHIM_MADV_NORMAL,
#endif
#if defined(SHIM_MADV_RANDOM)
	SHIM_MADV_RANDOM,
#endif
#if defined(SHIM_MADV_SEQUENTIAL)
	SHIM_MADV_SEQUENTIAL,
#endif
#if defined(SHIM_MADV_WILLNEED)
	SHIM_MADV_WILLNEED,
#endif
#if defined(SHIM_MADV_DONTNEED)
	SHIM_MADV_DONTNEED,
#endif
#if defined(SHIM_MADV_REMOVE)
	SHIM_MADV_REMOVE,
#endif
#if defined(SHIM_MADV_DONTFORK)
	SHIM_MADV_DONTFORK,
#endif
#if defined(SHIM_MADV_DOFORK)
	SHIM_MADV_DOFORK,
#endif
#if defined(SHIM_MADV_MERGEABLE)
	SHIM_MADV_MERGEABLE,
#endif
#if defined(SHIM_MADV_UNMERGEABLE)
	SHIM_MADV_UNMERGEABLE,
#endif
#if defined(SHIM_MADV_SOFT_OFFLINE)
	SHIM_MADV_SOFT_OFFLINE,
#endif
#if defined(SHIM_MADV_HUGEPAGE)
	SHIM_MADV_HUGEPAGE,
#endif
#if defined(SHIM_MADV_NOHUGEPAGE)
	SHIM_MADV_NOHUGEPAGE,
#endif
#if defined(SHIM_MADV_DONTDUMP)
	SHIM_MADV_DONTDUMP,
#endif
#if defined(SHIM_MADV_DODUMP)
	SHIM_MADV_DODUMP,
#endif
#if defined(SHIM_MADV_FREE)
	SHIM_MADV_FREE,
#endif
#if defined(SHIM_MADV_WIPEONFORK)
	SHIM_MADV_WIPEONFORK,
#endif
#if defined(SHIM_MADV_KEEPONFORK)
	SHIM_MADV_KEEPONFORK,
#endif
#if defined(SHIM_MADV_INHERIT_ZERO)
	SHIM_MADV_INHERIT_ZERO,
#endif
#if defined(SHIM_MADV_COLD)
	SHIM_MADV_COLD,
#endif
#if defined(SHIM_MADV_PAGEOUT)
	SHIM_MADV_PAGEOUT,
#endif
#if defined(SHIM_MADV_POPULATE_READ)
	SHIM_MADV_POPULATE_READ,
#endif
#if defined(SHIM_MADV_POPULATE_WRITE)
	SHIM_MADV_POPULATE_WRITE,
#endif
#if defined(SHIM_MADV_DONTNEED_LOCKED)
	SHIM_MADV_DONTNEED_LOCKED,
#endif
/* Linux 6.0 */
#if defined(SHIM_MADV_COLLAPSE)
	SHIM_MADV_COLLAPSE,
#endif
/* FreeBSD */
#if defined(SHIM_MADV_AUTOSYNC)
	SHIM_MADV_AUTOSYNC,
#endif
/* FreeBSD and DragonFlyBSD */
#if defined(SHIM_MADV_CORE)
	SHIM_MADV_CORE,
#endif
/* FreeBSD */
#if defined(SHIM_MADV_PROTECT)
	SHIM_MADV_PROTECT,
#endif
/* Linux 5.14 */
#if defined(SHIM_MADV_POPULATE_READ)
	SHIM_MADV_POPULATE_READ,
#endif
/* Linux 5.14 */
#if defined(SHIM_MADV_POPULATE_WRITE)
	SHIM_MADV_POPULATE_WRITE,
#endif
/* OpenBSD */
#if defined(SHIM_MADV_SPACEAVAIL)
	SHIM_MADV_SPACEAVAIL,
#endif
/* OS X */
#if defined(SHIM_MADV_ZERO_WIRED_PAGES)
	SHIM_MADV_ZERO_WIRED_PAGES,
#endif
/* Solaris */
#if defined(SHIM_MADV_ACCESS_DEFAULT)
	SHIM_MADV_ACCESS_DEFAULT,
#endif
/* Solaris */
#if defined(SHIM_MADV_ACCESS_LWP)
	SHIM_MADV_ACCESS_LWP,
#endif
/* Solaris */
#if defined(SHIM_MADV_ACCESS_MANY)
	SHIM_MADV_ACCESS_MANY,
#endif
#if !defined(MADV_NORMAL) || \
    (MADV_NORMAL != 0)
	/* ensure we always have at least one item in array */
	0,
#endif
};

#if defined(HAVE_MPROTECT)
static const int mprotect_flags[] = {
#if defined(PROT_READ)
	PROT_READ,
#endif
#if defined(PROT_WRITE)
	PROT_WRITE,
#endif
#if defined(PROT_READ) &&	\
    defined(PROT_WRITE)
	PROT_READ | PROT_WRITE,
#endif
#if defined(PROT_NONE)
	PROT_NONE,
#endif
};
#endif

static const int mmap_flags[] = {
#if defined(MAP_32BIT)
	MAP_32BIT,
#endif
#if defined(MAP_LOCKED)
	MAP_LOCKED,
#endif
#if defined(MAP_STACK)
	MAP_STACK,
#endif
#if defined(MAP_SHARED_VALIDATE)
	MAP_SHARED_VALIDATE,
#endif
#if defined(MAP_POPULATE)
	MAP_POPULATE,
#endif
#if defined(MAP_NORESERVE)
	MAP_NORESERVE,
#endif
#if defined(MAP_NONBLOCK) &&	\
    defined(MAP_POPULATE)
	MAP_NONBLOCK | MAP_POPULATE,
#endif
#if defined(MAP_SYNC)
	MAP_SYNC,
#endif
#if defined(MAP_UNINITIALIZED)
	MAP_UNINITIALIZED,
#endif
#if defined(MAP_HUGETLB)
	/* MAP_HUGETLB | (21 << MAP_HUGE_SHIFT), */
#endif
	0,
};

#if defined(HAVE_MLOCKALL) &&	\
    defined(MCL_CURRENT) && 	\
    defined(MCL_FUTURE)
static const int mlockall_flags[] = {
#if defined(MCL_CURRENT)
	MCL_CURRENT,
#endif
#if defined(MCL_FUTURE)
	MCL_FUTURE,
#endif
#if defined(MCL_CURRENT) && 	\
    defined(MCL_FUTURE)
	MCL_CURRENT | MCL_FUTURE,
#endif
};
#endif

static void stress_mmaptorture_init(const uint32_t instances)
{
	char path[PATH_MAX];
	const pid_t pid = getpid();
	const size_t page_size = stress_get_page_size();

	mmap_bytes = DEFAULT_MMAPTORTURE_BYTES;
	if (!stress_get_setting("mmaptorture-bytes", &mmap_bytes)) {
		if (g_opt_flags & OPT_FLAGS_MAXIMIZE)
			mmap_bytes = MAX_32;
		if (g_opt_flags & OPT_FLAGS_MINIMIZE)
			mmap_bytes = MIN_MMAPTORTURE_BYTES;
	}

	mmap_bytes = (instances < 1) ? mmap_bytes : mmap_bytes / instances;
	mmap_bytes &= ~(page_size - 1);
	if (mmap_bytes < page_size * MMAP_SIZE_MAP * 2) {
		mmap_bytes = (page_size * MMAP_SIZE_MAP * 2);
		mmap_bytes_adjusted = true;
	}

	if (stress_temp_dir_mk(name, pid, 0) < 0) {
		mmap_fd = -1;
		return;
	}
	stress_temp_filename(path, sizeof(path), name, pid, 0, stress_mwc32());
	mmap_fd = open(path, O_CREAT | O_RDWR, S_IRUSR | S_IWUSR | S_IXUSR);
	if (mmap_fd < 0) {
		mmap_fd = -1;
		(void)stress_temp_dir_rm(path, pid, 0);
		return;
	}
	(void)unlink(path);

	VOID_RET(int, ftruncate(mmap_fd, (off_t)mmap_bytes));
	mmap_data = (uint8_t *)mmap(NULL, mmap_bytes, PROT_READ | PROT_WRITE, MAP_SHARED, mmap_fd, 0);
}

static void stress_mmaptorture_deinit(void)
{
	if (mmap_fd == -1)
		return;
	if (mmap_data != MAP_FAILED)
		(void)munmap((void *)mmap_data, mmap_bytes);
	(void)stress_temp_dir_rm(name, getpid(), 0);
}

static void NORETURN MLOCKED_TEXT stress_mmaptorture_sighandler(int signum)
{
	(void)signum;

	switch (signum) {
	case SIGBUS:
		if (mmap_stats)
			mmap_stats->sigbus_traps++;
		break;
	case SIGSEGV:
		if (mmap_stats)
			mmap_stats->sigbus_traps++;
		break;
	default:
		break;
	}
	siglongjmp(jmp_env, 1);	/* Ugly, bounce back */
	stress_no_return();
}

static void stress_mmaptorture_msync(
	uint8_t *addr,
	const size_t length,
	const size_t page_size,
	const uint32_t mmaptorture_msync)
{
#if defined(HAVE_MSYNC)
	size_t i;
	uint32_t percent;

	if (mmaptorture_msync > 100)
		percent = 10000000 * 100;
	else
		percent = 10000000 * mmaptorture_msync;


	for (i = 0; i < length; i += page_size) {
		if (stress_mwc32modn(1000000000) < percent) {
			const int flag = (stress_mwc1() ? MS_SYNC : MS_ASYNC) |
					 (stress_mwc1() ? 0 : MS_INVALIDATE);

			(void)msync((void *)(addr + i), page_size, flag);
			mmap_stats->sync_pages++;
		}
	}
#else
	(void)addr;
	(void)length;
	(void)page_size;
	(void)mmaptorture_msync;
#endif
}

static void stress_mmaptorture_vm_name(
	uint8_t *ptr,
	const size_t size,
	const size_t page_size)
{
	char vma_name[32];
	size_t i, j;
	static const char hex[] = "0123456789ABCDEF";

	for (i = 0, j = 0; i < size; i += page_size, j++) {
		(void)stress_rndstr(vma_name, sizeof(vma_name));
		vma_name[0] = hex[(j >> 4) & 0xf];
		vma_name[1] = hex[j & 0xf];

		stress_set_vma_anon_name(ptr + i, page_size, vma_name);
	}
}

static int stress_mmaptorture_child(stress_args_t *args, void *context)
{
	const size_t page_size = args->page_size;
	const size_t page_mask = ~(page_size - 1);
#if defined(HAVE_LIB_RT) &&	\
    defined(HAVE_SHM_OPEN) &&	\
    defined(HAVE_SHM_UNLINK)
	const pid_t mypid = getpid();
#endif
	char *data;
	NOCLOBBER uint32_t mmaptorture_msync = DEFAULT_MMAPTORTURE_MSYNC;
	NOCLOBBER mmap_info_t *mappings;
	NOCLOBBER off_t mmap_fd_offset = 0;
#if defined(HAVE_LINUX_MEMPOLICY_H)
	NOCLOBBER stress_numa_mask_t *numa_mask = NULL;
	NOCLOBBER stress_numa_mask_t *numa_nodes = NULL;
#endif
	size_t i;
	(void)context;

	if (!stress_get_setting("mmaptorture-msync", &mmaptorture_msync)) {
		if (g_opt_flags & OPT_FLAGS_MAXIMIZE)
			mmaptorture_msync = MAX_MMAPTORTURE_MSYNC;
		if (g_opt_flags & OPT_FLAGS_MINIMIZE)
			mmaptorture_msync = 1;
	}

	if (sigsetjmp(jmp_env, 1)) {
		pr_inf_skip("%s: premature SIGSEGV caught, skipping stressor\n",
			args->name);
		return EXIT_NO_RESOURCE;
        }
	if (stress_sighandler(args->name, SIGBUS, stress_mmaptorture_sighandler, NULL) < 0)
		return EXIT_NO_RESOURCE;
	if (stress_sighandler(args->name, SIGSEGV, stress_mmaptorture_sighandler, NULL) < 0)
		return EXIT_NO_RESOURCE;

	data = malloc(page_size);
	if (UNLIKELY(!data)) {
		pr_fail("%s: malloc of %zu bytes failed%s, out of memory\n",
			args->name, page_size, stress_get_memfree_str());
		return EXIT_NO_RESOURCE;
	}

	mappings = (mmap_info_t *)calloc((size_t)MMAP_MAPPINGS_MAX, sizeof(*mappings));
	if (UNLIKELY(!mappings)) {
		pr_fail("%s: calloc of %zu bytes failed%s, out of memory\n",
			args->name, (size_t)MMAP_MAPPINGS_MAX * sizeof(*mappings),
			stress_get_memfree_str());
		free(data);
		return EXIT_NO_RESOURCE;
	}

#if defined(HAVE_LINUX_MEMPOLICY_H)
	if (stress_numa_nodes() > 0) {
		numa_mask = stress_numa_mask_alloc();
		if (numa_mask) {
			numa_nodes = stress_numa_mask_alloc();
			if (!numa_nodes) {
				stress_numa_mask_free(numa_mask);
				numa_mask = NULL;
			}
		}
	}
#endif
	for (i = 0; i < MMAP_MAPPINGS_MAX; i++) {
		mappings[i].addr = MAP_FAILED;
		mappings[i].size = 0;
		mappings[i].offset = 0;
	}

	do {
		unsigned char vec[MMAP_SIZE_MAP];
		NOCLOBBER uint8_t *ptr;
		NOCLOBBER size_t n, mmap_size;
		NOCLOBBER pid_t pid = -1;
		NOCLOBBER uint64_t total_bytes = 0;
		off_t offset;

		if (sigsetjmp(jmp_env, 1))
			goto mappings_unmap;

		VOID_RET(int, ftruncate(mmap_fd, (off_t)stress_mwc64modn((uint64_t)mmap_bytes)));
		VOID_RET(int, ftruncate(mmap_fd, (off_t)mmap_bytes));

		offset = stress_mwc64modn((uint64_t)mmap_bytes) & page_mask;
		if (lseek(mmap_fd, offset, SEEK_SET) == offset) {
			(void)shim_memset(data, stress_mwc8(), page_size);
			if (write(mmap_fd, data, page_size) == (ssize_t)page_size) {
				volatile uint8_t *vptr = (volatile uint8_t *)(mmap_data + offset);

				(*vptr)++;
				stress_mmaptorture_msync(mmap_data, mmap_bytes, page_size, mmaptorture_msync);
			}
		}
#if defined(HAVE_REMAP_FILE_PAGES) &&   \
    defined(MAP_NONBLOCK) &&		\
    !defined(STRESS_ARCH_SPARC)
		if (remap_file_pages(mmap_data, mmap_bytes, PROT_NONE, 0, MAP_SHARED | MAP_NONBLOCK) == 0)
			mmap_stats->remapped_pages += mmap_bytes / page_size;
		if (mprotect(mmap_data, mmap_bytes, PROT_READ | PROT_WRITE) == 0)
			mmap_stats->mprotect_pages += mmap_bytes / page_size;
#endif
		for (n = 0; n < MMAP_MAPPINGS_MAX; n++) {
			mappings[n].addr = MAP_FAILED;
			mappings[n].size = 0;
			mappings[n].offset = 0;
		}

		for (n = 0; n < MMAP_MAPPINGS_MAX; n++) {
			int flag = 0, mmap_flag;

			const int madvise_option = madvise_options[stress_mwc8modn(SIZEOF_ARRAY(madvise_options))];
#if defined(HAVE_MPROTECT)
			const int mprotect_flag = mprotect_flags[stress_mwc8modn(SIZEOF_ARRAY(mprotect_flags))];
#else
			const int mprotect_flag = ~0;
#endif

retry:
			if (UNLIKELY(!stress_continue(args)))
				break;

			/* Don't exceed mmap limit */
			if (total_bytes >= mmap_bytes)
				break;

			mmap_flag = mmap_flags[stress_mwc8modn(SIZEOF_ARRAY(mmap_flags))] |
				    mmap_flags[stress_mwc8modn(SIZEOF_ARRAY(mmap_flags))];

			mmap_size = page_size * (1 + stress_mwc16modn(MMAP_SIZE_MAP));
			offset = stress_mwc64modn((uint64_t)mmap_bytes) & page_mask;
#if defined(HAVE_FALLOCATE)
#if defined(FALLOC_FL_PUNCH_HOLE) &&	\
    defined(FALLOC_FL_KEEP_SIZE)
			if (stress_mwc1()) {
				(void)shim_fallocate(mmap_fd, 0, offset, mmap_size);
				flag = PAGE_WR_FLAG | PAGE_RD_FLAG;
			} else {
				(void)shim_fallocate(mmap_fd, FALLOC_FL_PUNCH_HOLE | FALLOC_FL_KEEP_SIZE, offset, mmap_size);
			}
#else
			(void)shim_fallocate(mmap_fd, 0, offset, mmap_size);
			flag = PAGE_WR_FLAG | PAGE_RD_FLAG;
#endif
#endif
			if (stress_mwc1()) {
				/* file based mmap */
				ptr = (uint8_t *)mmap(NULL, mmap_size, PROT_READ | PROT_WRITE,
							MAP_SHARED | mmap_flag, mmap_fd, offset);
				if (ptr != MAP_FAILED)
					goto mapped_ok;
				ptr = (uint8_t *)mmap(NULL, mmap_size, PROT_READ | PROT_WRITE,
							MAP_SHARED, mmap_fd, offset);
				if (ptr != MAP_FAILED)
					goto mapped_ok;
				mmap_stats->mmap_retries++;
				goto retry;
			}
#if defined(HAVE_LIB_RT) &&	\
    defined(HAVE_SHM_OPEN) &&	\
    defined(HAVE_SHM_UNLINK)
			if (stress_mwc1()) {
				/* anonymous shm mapping */
				int shm_fd;
				char shm_name[128];

				(void)snprintf(shm_name, sizeof(shm_name), "%s-%" PRIdMAX "-%zu",
						args->name, (intmax_t)mypid, n);
				shm_fd = shm_open(shm_name, O_CREAT | O_RDWR, S_IRUSR | S_IWUSR);
				if (shm_fd < 0)
					goto retry;
				ptr = (uint8_t *)mmap(NULL, mmap_size, PROT_READ | PROT_WRITE,
							MAP_SHARED | mmap_flag, shm_fd, offset);
				if (ptr != MAP_FAILED) {
					(void)shm_unlink(shm_name);
					(void)close(shm_fd);
					goto mapped_ok;
				}
				ptr = (uint8_t *)mmap(NULL, mmap_size, PROT_READ | PROT_WRITE,
							MAP_SHARED, shm_fd, offset);
				if (ptr != MAP_FAILED) {
					(void)shm_unlink(shm_name);
					(void)close(shm_fd);
					goto mapped_ok;
				}
				mmap_stats->mmap_retries++;
				goto retry;
			}
#endif
			/* anonymous mmap mapping */
			ptr = (uint8_t *)mmap(NULL, mmap_size, PROT_READ | PROT_WRITE,
						MAP_SHARED | MAP_ANONYMOUS | mmap_flag, -1, 0);
			if (LIKELY(ptr != MAP_FAILED))
				goto mapped_ok;
			ptr = (uint8_t *)mmap(NULL, mmap_size, PROT_READ | PROT_WRITE,
						MAP_SHARED | MAP_ANONYMOUS, -1, 0);
			if (LIKELY(ptr != MAP_FAILED))
				goto mapped_ok;
			mmap_stats->mmap_retries++;
			goto retry;

mapped_ok:
			total_bytes += mmap_size;
			mmap_stats->mmap_pages += mmap_size / page_size;
			mappings[n].addr = ptr;
			mappings[n].size = mmap_size;
			mappings[n].offset = offset;
			stress_mmaptorture_vm_name((void *)ptr, mmap_size, page_size);

			if (stress_mwc1()) {
				for (i = 0; i < mmap_size; i += 64)
					shim_builtin_prefetch((void *)(ptr + i));
			}
#if defined(HAVE_LINUX_MEMPOLICY_H)
			if (numa_mask && numa_nodes && stress_mwc1())
				stress_numa_randomize_pages(args, numa_nodes, numa_mask, (void *)ptr, mmap_size, page_size);

#if defined(HAVE_MSYNC) &&	\
    defined(MS_SYNC) &&		\
    defined(MS_ASYNC)
			stress_mmaptorture_msync(ptr, mmap_size, page_size, mmaptorture_msync);
#endif
#endif
			if (shim_madvise((void *)ptr, mmap_size, madvise_option) == 0)
				mmap_stats->madvise_pages += mmap_size / page_size;
			(void)shim_mincore((void *)ptr, mmap_size, vec);
			for (i = 0; i < mmap_size; i += page_size) {
				if (stress_mwc1()) {
					if (shim_mlock((void *)(ptr + i), page_size) == 0)
						mmap_stats->lock_pages++;
				}
				if ((flag & PAGE_WR_FLAG) && (mprotect_flag & PROT_WRITE))
					*(volatile uint8_t *)(ptr + i) = stress_mwc64();
				if ((flag & PAGE_RD_FLAG) && (mprotect_flag & PROT_READ))
					*(volatile uint8_t *)(ptr + i);
			}
#if defined(MAP_FIXED_NOREPLACE)
			{
				void *tmp;

				/* mmap onto an existing virt addr, should fail */
				tmp = mmap((void *)ptr, mmap_size, PROT_READ | PROT_WRITE,
					MAP_SHARED | MAP_FIXED_NOREPLACE, mmap_fd, offset);
				if (tmp != MAP_FAILED) {
					mmap_stats->mmap_pages += mmap_size / page_size;
					(void)munmap(tmp, mmap_size);
				}
			}
#endif

#if defined(HAVE_MPROTECT)
			if (stress_mwc1())
				if (mprotect((void *)ptr, mmap_size, mprotect_flag) == 0)
					mmap_stats->mprotect_pages += mmap_size / page_size;
#endif
#if defined(HAVE_LINUX_MEMPOLICY_H)
			if (numa_mask && numa_nodes && stress_mwc1())
				stress_numa_randomize_pages(args, numa_nodes, numa_mask, (void *)ptr, page_size, mmap_size);
#endif
			for (i = 0; i < mmap_size; i += page_size) {
				if (stress_mwc1())
					(void)shim_munlock((void *)(ptr + i), page_size);
#if defined(SHIM_MADV_PAGEOUT)
				if (stress_mwc1()) {
					if (shim_madvise((void *)(ptr + i), page_size, SHIM_MADV_PAGEOUT) == 0)
						mmap_stats->madvise_pages++;
				}
#endif
				stress_mmaptorture_msync(ptr + i, page_size, page_size, mmaptorture_msync);
#if defined(SHIM_MADV_FREE)
				if (stress_mwc1()) {
					if (shim_madvise((void *)(ptr + i), page_size, SHIM_MADV_FREE) == 0)
						mmap_stats->madvise_pages++;
				}
#endif
			}

			if (stress_mwc1())
				(void)shim_mincore((void *)ptr, mmap_size, vec);

			if (stress_mwc1()) {
				int ret;

				ret = stress_munmap_force((void *)ptr, mmap_size);
				if (ret == 0) {
#if defined(MAP_FIXED)
					if (stress_mwc1()) {
						mappings[n].addr = (uint8_t *)mmap((void *)mappings[n].addr, page_size,
								PROT_READ | PROT_WRITE, MAP_FIXED | MAP_SHARED,
								mmap_fd, mappings[n].offset);
						if (UNLIKELY(mappings[n].addr == MAP_FAILED)) {
							mappings[n].size = 0;
						} else {
							stress_mmaptorture_vm_name((void *)mappings[n].addr, page_size, page_size);
							mmap_stats->mmap_pages++;
						}
					} else {
						mappings[n].addr = MAP_FAILED;
						mappings[n].size = 0;
					}
#endif
				} else {
					mappings[n].addr = MAP_FAILED;
					mappings[n].size = 0;
				}
			}
			stress_bogo_inc(args);
		}

		if (stress_mwc1()) {
			pid = fork();
			if (pid == 0) {
				stress_set_proc_state(args->name, STRESS_STATE_RUN);

#if defined(HAVE_MLOCKALL) &&	\
    defined(MCL_CURRENT) && 	\
    defined(MCL_FUTURE)
				{
					const size_t idx = stress_mwc8modn(SIZEOF_ARRAY(mlockall_flags));

					(void)shim_mlockall(mlockall_flags[idx]);
				}
#endif
				/* Pass 1, free random pages */
				for (i = 0; i < n; i++) {
					ptr = mappings[i].addr;
					mmap_size = mappings[i].size;

#if defined(SHIM_MADV_DONTNEED)
					if (shim_madvise((void *)ptr, mmap_size, SHIM_MADV_DONTNEED) == 0)
						mmap_stats->madvise_pages += mmap_size / page_size;
#endif

					if (stress_mwc1()) {
						if ((ptr != MAP_FAILED) && (mmap_size > 0)) {
							(void)stress_munmap_force((void *)(ptr + i), page_size);
							mappings[i].addr = MAP_FAILED;
							mappings[i].size = 0;
						}
					}
				}

				for (i = 0; i < n; i++) {
					ptr = mappings[i].addr;
					mmap_size = mappings[i].size;

					if ((ptr != MAP_FAILED) && (mmap_size > 0)) {
						(void)shim_mseal(ptr, mmap_size, 0);
						break;
					}
				}

				/* Pass 2, free unfreed pages */
				for (i = 0; i < n; i++) {
					ptr = mappings[i].addr;
					mmap_size = mappings[i].size;

					if ((ptr != MAP_FAILED) && (mmap_size > 0)) {
						(void)stress_munmap_force((void *)ptr, mmap_size);
						mappings[i].addr = MAP_FAILED;
						mappings[i].size = 0;
					}
				}
#if defined(HAVE_MUNLOCKALL)
				shim_munlockall();
#endif
				_exit(0);
			}
		}

mappings_unmap:
		for (i = 0; i < n; i++) {
			ptr = mappings[i].addr;
			mmap_size = mappings[i].size;

			if ((ptr != MAP_FAILED) && (mmap_size > 0)) {
#if defined(HAVE_MREMAP)
				if (mmap_size > page_size) {
					uint8_t *newptr;
					size_t new_size = mmap_size - page_size;

					newptr = (uint8_t *)mremap(ptr, mmap_size, new_size, MREMAP_MAYMOVE);
					if (newptr != MAP_FAILED) {
						ptr = newptr;
						mmap_size -= page_size;
						mmap_stats->remapped_pages += mmap_size / new_size;
					}
				}
#endif

#if defined(SHIM_MADV_NORMAL)
				if (shim_madvise((void *)ptr, mmap_size, MADV_NORMAL) == 0)
					mmap_stats->madvise_pages += mmap_size / page_size;
#endif
#if defined(HAVE_MPROTECT)
				if (mprotect((void *)ptr, mmap_size, PROT_READ | PROT_WRITE) == 0)
					mmap_stats->mprotect_pages += mmap_size / page_size;
#endif
				(void)shim_munlock((void *)ptr, mmap_size);
#if defined(SHIM_MADV_DONTNEED)
				if (stress_mwc1()) {
					if (shim_madvise((void *)ptr, mmap_size, MADV_DONTNEED) == 0)
						mmap_stats->madvise_pages += mmap_size / page_size;
				}
#endif
#if defined(SHIM_MADV_REMOVE)
				{
					size_t j;

					for (j = 0; j < mmap_size; j += page_size) {
						if (stress_mwc1()) {
							if (shim_madvise((void *)(ptr + j), page_size, MADV_REMOVE) == 0)
								mmap_stats->madvise_pages += 1;
#if defined(MADV_RANDOM)
							else if (shim_madvise((void *)(ptr + j), 0, MADV_RANDOM) == 0)
								mmap_stats->madvise_pages += 1;
#endif
						}
						(void)stress_munmap_force((void *)(ptr + j), page_size);
					}
				}
#endif
				(void)shim_mincore((void *)ptr, mmap_size, vec);
			}
			mappings[i].addr = MAP_FAILED;
			mappings[i].size = 0;
		}

		if (stress_mwc1()) {
			if (shim_fallocate(mmap_fd, 0, mmap_fd_offset, page_size) == 0)
				(void)shim_memcpy(data, mmap_data + mmap_fd_offset, page_size);
		} else {
#if defined(HAVE_PWRITE)
			(void)shim_memset(data, stress_mwc8(), page_size);
			if (pwrite(mmap_fd, data, page_size, mmap_fd_offset) == (ssize_t)page_size)
				(void)shim_memcpy(data, mmap_data + mmap_fd_offset, page_size);
#else
			(void)shim_memset(data, stress_mwc8(), page_size);
			if (lseek(mmap_fd, mmap_fd_offset, SEEK_SET) == mmap_fd_offset) {
				if (write(mmap_fd, data, page_size) == (ssize_t)page_size)
					(void)shim_memcpy(data, mmap_data + mmap_fd_offset, page_size);
			}
#endif
		}
		mmap_fd_offset += page_size;
		if (mmap_fd_offset >= (off_t)mmap_bytes)
			mmap_bytes = 0;

		if (pid > 0)
			(void)stress_kill_and_wait(args, pid, SIGKILL, false);
	} while (stress_continue(args));

	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);

#if defined(HAVE_LINUX_MEMPOLICY_H)
	if (numa_mask)
		stress_numa_mask_free(numa_mask);
	if (numa_nodes)
		stress_numa_mask_free(numa_nodes);
#endif
	free(mappings);
	free(data);

	return EXIT_SUCCESS;
}

/*
 *  stress_mmaptorture()
 *	stress mmap with many pages being mapped
 */
static int stress_mmaptorture(stress_args_t *args)
{
	int ret;
	double t_start, duration, rate;

	mmap_stats = (mmap_stats_t *)stress_mmap_populate(NULL, sizeof(*mmap_stats),
					PROT_READ | PROT_WRITE,
					MAP_SHARED | MAP_ANONYMOUS, -1, 0);
	if (mmap_stats == MAP_FAILED) {
		pr_inf_skip("%s: cannot mmap %zu bytes stats shared page%s, "
			"errno=%d (%s), skipping stressor\n", args->name,
			sizeof(*mmap_stats), stress_get_memfree_str(),
			errno, strerror(errno));
		return EXIT_NO_RESOURCE;
	}

	if (stress_instance_zero(args))
		stress_usage_bytes(args, mmap_bytes, mmap_bytes * args->instances);

	stress_set_proc_state(args->name, STRESS_STATE_SYNC_WAIT);
	stress_sync_start_wait(args);
	stress_set_proc_state(args->name, STRESS_STATE_RUN);

	t_start = stress_time_now();
	ret = stress_oomable_child(args, NULL, stress_mmaptorture_child, STRESS_OOMABLE_NORMAL);
	duration = stress_time_now() - t_start;

	rate = (duration > 0.0) ? (double)mmap_stats->mmap_pages / duration : 0.0;
	stress_metrics_set(args, 0, "pages mapped pec sec", rate, STRESS_METRIC_HARMONIC_MEAN);
	rate = (duration > 0.0) ? (double)mmap_stats->sync_pages / duration : 0.0;
	stress_metrics_set(args, 1, "pages synced pec sec", rate, STRESS_METRIC_HARMONIC_MEAN);
	rate = (duration > 0.0) ? (double)mmap_stats->lock_pages / duration : 0.0;
	stress_metrics_set(args, 2, "pages locked pec sec", rate, STRESS_METRIC_HARMONIC_MEAN);
	rate = (duration > 0.0) ? (double)mmap_stats->mprotect_pages / duration : 0.0;
	stress_metrics_set(args, 3, "pages mprotected pec sec", rate, STRESS_METRIC_HARMONIC_MEAN);
	rate = (duration > 0.0) ? (double)mmap_stats->madvise_pages / duration : 0.0;
	stress_metrics_set(args, 4, "pages madvised pec sec", rate, STRESS_METRIC_HARMONIC_MEAN);
	rate = (duration > 0.0) ? (double)mmap_stats->remapped_pages / duration : 0.0;
	stress_metrics_set(args, 5, "pages remapped pec sec", rate, STRESS_METRIC_HARMONIC_MEAN);
	rate = (duration > 0.0) ? (double)mmap_stats->mmap_retries / duration : 0.0;
	stress_metrics_set(args, 6, "mmap retries pec sec", rate, STRESS_METRIC_HARMONIC_MEAN);
	rate = (duration > 0.0) ? (double)mmap_stats->sigbus_traps / duration : 0.0;
	stress_metrics_set(args, 7, "intentional SIGBUS signals sec", rate, STRESS_METRIC_HARMONIC_MEAN);
	rate = (duration > 0.0) ? (double)mmap_stats->sigsegv_traps / duration : 0.0;
	stress_metrics_set(args, 8, "intentional SIGSEGV signals sec", rate, STRESS_METRIC_HARMONIC_MEAN);

	(void)munmap((void *)mmap_stats, sizeof(*mmap_stats));

	return ret;
}

const stressor_info_t stress_mmaptorture_info = {
	.stressor = stress_mmaptorture,
	.classifier = CLASS_VM | CLASS_OS,
	.verify = VERIFY_NONE,
	.init = stress_mmaptorture_init,
	.deinit = stress_mmaptorture_deinit,
	.opts = opts,
	.help = help
};

#else

const stressor_info_t stress_mmaptorture_info = {
	.stressor = stress_unimplemented,
	.classifier = CLASS_VM | CLASS_OS,
	.verify = VERIFY_NONE,
	.opts = opts,
	.help = help,
	.unimplemented_reason = "built without siglongjmp support",
};

#endif
