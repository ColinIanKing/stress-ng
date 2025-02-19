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
#include "core-numa.h"
#include "core-out-of-memory.h"

static const stress_help_t help[] = {
	{ NULL,	"mmaptorture N",	"start N workers torturing page mappings" },
	{ NULL, "mmaptorture-bytes N",	"size of file backed region to be memory mapped" },
	{ NULL,	"mmaptorture-ops N",	"stop after N mmaptorture bogo operations" },
	{ NULL,	NULL,			NULL }
};

typedef struct {
	uint8_t *addr;
	size_t	size;
	off_t	offset;
} mmap_info_t;

#define MMAP_MAPPINGS_MAX	(128)
#define MMAP_SIZE_MAP		(4)	/* in pages */

#define PAGE_WR_FLAG		(0x01)
#define PAGE_RD_FLAG		(0x02)

#define MIN_MMAPTORTURE_BYTES		(16 * MB)
#define MAX_MMAPTORTURE_BYTES   	(MAX_MEM_LIMIT)
#define DEFAULT_MMAPTORTURE_BYTES	(1024 * 4096)

static sigjmp_buf jmp_env;

static int mmap_fd;
static const char *name = "mmaptorture";
static uint8_t *mmap_data;
static size_t mmap_bytes = DEFAULT_MMAPTORTURE_BYTES;
static bool mmap_bytes_adjusted = false;

#if defined(HAVE_MADVISE)
static const int madvise_options[] = {
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
};
#endif

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
#if defined(MAP_HUGETLB)
	/* MAP_HUGETLB | (21 << MAP_HUGE_SHIFT), */
#endif
	0,
};

static void stress_mmaptorture_init(const uint32_t num_instances)
{
	char path[PATH_MAX];
	const pid_t pid = getpid();
	const size_t page_size = stress_get_page_size();

	(void)num_instances;

	mmap_bytes = DEFAULT_MMAPTORTURE_BYTES;
	stress_get_setting("mmaptorture-bytes", &mmap_bytes);

	mmap_bytes = (num_instances < 1) ? mmap_bytes : mmap_bytes / num_instances;
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

	VOID_RET(int, ftruncate(mmap_fd, mmap_bytes));
	mmap_data = mmap(NULL, mmap_bytes, PROT_READ | PROT_WRITE, MAP_SHARED, mmap_fd, 0);
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

	siglongjmp(jmp_env, 1);	/* Ugly, bounce back */
}

static void stress_mmaptorture_msync(uint8_t *addr, const size_t length, const size_t page_size)
{
#if defined(HAVE_MSYNC)
	size_t i;

	for (i = 0; i < length; i += page_size) {
		if (stress_mwc1()) {
			const int flag = (stress_mwc1() ? MS_SYNC : MS_ASYNC) |
					 (stress_mwc1() ? 0 : MS_INVALIDATE);

			(void)msync((void *)(addr + i), length, flag);
		}
	}
#else
	(void)addr;
	(void)length;
	(void)page_size;
#endif
}

static int stress_mmaptorture_child(stress_args_t *args, void *context)
{
	const size_t page_size = args->page_size;
	const size_t page_mask = ~(page_size - 1);
	NOCLOBBER mmap_info_t *mappings;
#if defined(HAVE_LINUX_MEMPOLICY_H)
	NOCLOBBER stress_numa_mask_t *numa_mask = NULL;
#endif
	size_t i;
	(void)context;

	if (sigsetjmp(jmp_env, 1)) {
		pr_inf_skip("%s: premature SIGSEGV caught, skipping stressor\n",
			args->name);
		return EXIT_NO_RESOURCE;
        }
	if (stress_sighandler(args->name, SIGBUS, stress_mmaptorture_sighandler, NULL) < 0)
		return EXIT_NO_RESOURCE;
	if (stress_sighandler(args->name, SIGSEGV, stress_mmaptorture_sighandler, NULL) < 0)
		return EXIT_NO_RESOURCE;

	mappings = (mmap_info_t *)calloc((size_t)MMAP_MAPPINGS_MAX, sizeof(*mappings));
	if (UNLIKELY(!mappings)) {
		pr_fail("%s: calloc failed, out of memory\n", args->name);
		return EXIT_NO_RESOURCE;
	}

#if defined(HAVE_LINUX_MEMPOLICY_H)
	if (stress_numa_nodes() > 0)
		numa_mask = stress_numa_mask_alloc();
#endif
	for (i = 0; i < MMAP_MAPPINGS_MAX; i++) {
		mappings[i].addr = MAP_FAILED;
		mappings[i].size = 0;
	}

	do {
		unsigned char vec[MMAP_SIZE_MAP];
		NOCLOBBER uint8_t *ptr;
		NOCLOBBER size_t n, mmap_size;
		NOCLOBBER pid_t pid = -1;
		off_t offset;

		if (sigsetjmp(jmp_env, 1))
			goto mappings_unmap;

		VOID_RET(int, ftruncate(mmap_fd, 0));

		offset = stress_mwc64modn((uint64_t)mmap_bytes) & page_mask;
		if (lseek(mmap_fd, offset, SEEK_SET) == offset) {
			char data[page_size];

			shim_memset(data, stress_mwc8(), sizeof(data));

			if (write(mmap_fd, data, sizeof(data)) == (ssize_t)sizeof(data)) {
				volatile uint8_t *vptr = (volatile uint8_t *)(mmap_data + offset);

				(*vptr)++;
				stress_mmaptorture_msync(mmap_data, mmap_bytes, page_size);
			}
		}
#if defined(HAVE_REMAP_FILE_PAGES) &&   \
    defined(MAP_NONBLOCK) &&		\
    !defined(STRESS_ARCH_SPARC)
		(void)remap_file_pages(mmap_data, mmap_bytes, PROT_NONE, 0, MAP_SHARED | MAP_NONBLOCK);
		(void)mprotect(mmap_data, mmap_bytes, PROT_READ | PROT_WRITE);
#endif

		for (n = 0; n < MMAP_MAPPINGS_MAX; n++) {
			int flag = 0;
#if defined(HAVE_MADVISE)
			const int madvise_option = madvise_options[stress_mwc8modn(SIZEOF_ARRAY(madvise_options))];
#endif
#if defined(HAVE_MPROTECT)
			const int mprotect_flag = mprotect_flags[stress_mwc8modn(SIZEOF_ARRAY(mprotect_flags))];
#else
			const int mprotect_flag = ~0;
#endif
			int mmap_flag = mmap_flags[stress_mwc8modn(SIZEOF_ARRAY(mmap_flags))] |
				        mmap_flags[stress_mwc8modn(SIZEOF_ARRAY(mmap_flags))];

			if (UNLIKELY(!stress_continue(args)))
				break;

			mmap_size = page_size * (1 + stress_mwc8modn(MMAP_SIZE_MAP));
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
				ptr = (uint8_t *)mmap(NULL, mmap_size, PROT_READ | PROT_WRITE,
					MAP_SHARED | mmap_flag, mmap_fd, offset);
				if (UNLIKELY(ptr == MAP_FAILED)) {
					ptr = (uint8_t *)mmap(NULL, mmap_size, PROT_READ | PROT_WRITE,
						MAP_SHARED, mmap_fd, offset);
					if (UNLIKELY(ptr == MAP_FAILED))  {
						mappings[n].addr = MAP_FAILED;
						mappings[n].size = 0;
						mappings[n].offset = 0;
						continue;
					}
				}
			} else {
				ptr = (uint8_t *)mmap(NULL, mmap_size, PROT_READ | PROT_WRITE,
					MAP_PRIVATE | MAP_ANONYMOUS | mmap_flag, -1, 0);
				if (UNLIKELY(ptr == MAP_FAILED)) {
					ptr = (uint8_t *)mmap(NULL, mmap_size, PROT_READ | PROT_WRITE,
						MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
					if (UNLIKELY(ptr == MAP_FAILED))  {
						mappings[n].addr = MAP_FAILED;
						mappings[n].size = 0;
						mappings[n].offset = 0;
						continue;
					}
				}
			}
			mappings[n].addr = ptr;
			mappings[n].size = mmap_size;
			mappings[n].offset = offset;

			if (stress_mwc1()) {
				for (i = 0; i < mmap_size; i += 64)
					shim_builtin_prefetch((void *)(ptr + i));
			}
#if defined(HAVE_LINUX_MEMPOLICY_H)
			if (numa_mask && stress_mwc1()) {
				stress_numa_randomize_pages(numa_mask, (void *)ptr, page_size, mmap_size);

#if defined(HAVE_MSYNC) &&	\
    defined(MS_SYNC) &&		\
    defined(MS_ASYNC)
				stress_mmaptorture_msync(ptr, mmap_size, page_size);
#endif
			}
#endif
#if defined(HAVE_MADVISE)
			(void)madvise((void *)ptr, mmap_size, madvise_option);
#endif
			(void)shim_mincore((void *)ptr, mmap_size, vec);
			for (i = 0; i < mmap_size; i += page_size) {
				if (stress_mwc1())
					(void)shim_mlock((void *)(ptr + i), page_size);
				if ((flag & PAGE_WR_FLAG) && (mprotect_flag & PROT_WRITE))
					*(volatile uint8_t *)(ptr + i) = stress_mwc64();
				if ((flag & PAGE_RD_FLAG) && (mprotect_flag & PROT_READ))
					*(volatile uint8_t *)(ptr + i);
			}

#if defined(HAVE_MPROTECT)
			if (stress_mwc1())
				(void)mprotect((void *)ptr, mmap_size, mprotect_flag);
#endif
#if defined(HAVE_LINUX_MEMPOLICY_H)
			if (stress_mwc1() && (numa_mask))
				stress_numa_randomize_pages(numa_mask, (void *)ptr, page_size, mmap_size);
#endif
			for (i = 0; i < mmap_size; i += page_size) {
				if (stress_mwc1())
					(void)shim_munlock((void *)(ptr + i), page_size);
#if defined(HAVE_MADVISE) &&	\
    defined(MADV_PAGEOUT)
				if (stress_mwc1())
					(void)madvise((void *)(ptr + i), page_size, MADV_PAGEOUT);
#endif
				stress_mmaptorture_msync(ptr + i, page_size, page_size);
#if defined(HAVE_MADVISE) &&	\
    defined(MADV_FREE)
				if (stress_mwc1())
					(void)madvise((void *)(ptr + i), page_size, MADV_FREE);
#endif

			}

			if (stress_mwc1())
				(void)shim_mincore((void *)ptr, mmap_size, vec);

			if (stress_mwc1()) {
				int ret;

				ret = stress_munmap_retry_enomem((void *)ptr, mmap_size);
				if (ret == 0) {
#if defined(MAP_FIXED)
					if (stress_mwc1()) {
						mappings[n].addr = mmap(mappings[n].addr, page_size, 
								PROT_READ | PROT_WRITE, MAP_FIXED | MAP_SHARED, mmap_fd, mappings[n].offset);
						mappings[n].size = page_size;
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
				/* Pass 1, free random pages */
				for (i = 0; i < n; i++) {
					(void)madvise((void *)ptr, mmap_size, MADV_DONTNEED);

					if (stress_mwc1()) {
						ptr = mappings[i].addr;
						mmap_size = mappings[i].size;
						if ((ptr != MAP_FAILED) && (mmap_size > 0)) {
							(void)stress_munmap_retry_enomem((void *)(ptr + i), page_size);
							mappings[i].addr = MAP_FAILED;
							mappings[i].size = 0;
						}
					}
				}
				/* Pass 2, free unfreed pages */
				for (i = 0; i < n; i++) {
					ptr = mappings[i].addr;
					mmap_size = mappings[i].size;
					if ((ptr != MAP_FAILED) && (mmap_size > 0)) {
						(void)stress_munmap_retry_enomem((void *)(ptr + i), page_size);
						mappings[i].addr = MAP_FAILED;
						mappings[i].size = 0;
					}
				}
				_exit(0);
			}
		}

mappings_unmap:
		for (i = 0; i < n; i++) {
			ptr = mappings[i].addr;
			mmap_size = mappings[i].size;

			if ((ptr != MAP_FAILED) && (mmap_size > 0)) {
#if defined(HAVE_MADVISE) &&	\
    defined(MADV_REMOVE)
				size_t j;
#endif

#if defined(HAVE_MREMAP)
				if (mmap_size > page_size) {
					uint8_t *newptr;

					newptr = (uint8_t *)mremap(ptr, mmap_size, mmap_size - page_size, MREMAP_MAYMOVE);
					if (newptr != MAP_FAILED) {
						ptr = newptr;
						mmap_size -= page_size;
					}
				}
#endif

#if defined(HAVE_MADVISE) &&	\
    defined(MADV_NORMAL)
				(void)madvise((void *)ptr, mmap_size, MADV_NORMAL);
#endif
#if defined(HAVE_MPROTECT)
				(void)mprotect((void *)ptr, mmap_size, PROT_READ | PROT_WRITE);
#endif
				(void)shim_munlock((void *)ptr, mmap_size);
#if defined(HAVE_MADVISE) &&	\
    defined(MADV_DONTNEED)
				if (stress_mwc1())
					(void)madvise((void *)ptr, mmap_size, MADV_DONTNEED);
#endif
#if defined(HAVE_MADVISE) &&	\
    defined(MADV_REMOVE)
				for (j = 0; j < mmap_size; j += page_size) {
					if (stress_mwc1())
						(void)madvise((void *)(ptr + j), mmap_size, MADV_REMOVE);
					(void)stress_munmap_retry_enomem((void *)(ptr + j), page_size);
				}
#endif
				(void)shim_mincore((void *)ptr, mmap_size, vec);
			}
			mappings[i].addr = MAP_FAILED;
			mappings[i].size = 0;
		}
		if (pid > 0)
			(void)stress_kill_and_wait(args, pid, SIGKILL, false);
	} while (stress_continue(args));

	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);

#if defined(HAVE_LINUX_MEMPOLICY_H)
	if (numa_mask)
		stress_numa_mask_free(numa_mask);
#endif
	free(mappings);
	return EXIT_SUCCESS;
}

/*
 *  stress_mmaptorture()
 *	stress mmap with many pages being mapped
 */
static int stress_mmaptorture(stress_args_t *args)
{
	if (args->instance == 0) {
		char str1[64], str2[64];

		stress_uint64_to_str(str1, sizeof(str1), (uint64_t)mmap_bytes);
		stress_uint64_to_str(str2, sizeof(str2), (uint64_t)mmap_bytes * args->num_instances);

		pr_inf("%s: using %smmap'd size %s per stressor (total %s)\n", args->name,
			mmap_bytes_adjusted ? "adjusted " : "", str1, str2);
	}
	stress_set_proc_state(args->name, STRESS_STATE_SYNC_WAIT);
	stress_sync_start_wait(args);
	stress_set_proc_state(args->name, STRESS_STATE_RUN);

	return stress_oomable_child(args, NULL, stress_mmaptorture_child, STRESS_OOMABLE_NORMAL);
}

static const stress_opt_t opts[] = {
        { OPT_mmaptorture_bytes, "mmaptorture-bytes",  TYPE_ID_SIZE_T_BYTES_VM, MIN_MMAPTORTURE_BYTES, MAX_MMAPTORTURE_BYTES, NULL },
};

const stressor_info_t stress_mmaptorture_info = {
	.stressor = stress_mmaptorture,
	.class = CLASS_VM | CLASS_OS,
	.verify = VERIFY_NONE,
	.init = stress_mmaptorture_init,
	.deinit = stress_mmaptorture_deinit,
	.opts = opts,
	.help = help
};
