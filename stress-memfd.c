/*
 * Copyright (C) 2013-2021 Canonical, Ltd.
 * Copyright (C) 2021-2025 Colin Ian King.
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
#include "core-cpu.h"
#include "core-madvise.h"
#include "core-numa.h"
#include "core-out-of-memory.h"
#include "core-target-clones.h"

#if defined(HAVE_LINUX_MEMFD_H)
#include <linux/memfd.h>
#endif

#define MIN_MEMFD_BYTES		(2 * MB)
#define DEFAULT_MEMFD_BYTES	(256 * MB)

#define MIN_MEMFD_FDS		(8)
#define MAX_MEMFD_FDS		(4096)
#define DEFAULT_MEMFD_FDS	(256)

#define MEMFD_STRIDE		(8)	/* 16 * sizeof(uint64_t) = 64 bytes */

static const stress_help_t help[] = {
	{ NULL,	"memfd N",	 "start N workers allocating memory with memfd_create" },
	{ NULL,	"memfd-bytes N", "allocate N bytes for each stress iteration" },
	{ NULL,	"memfd-fds N",	 "number of memory fds to open per stressors" },
	{ NULL,	"memfd-madvise", "add random madvise hints to memfd mapped pages" },
	{ NULL,	"memfd-mlock",	 "attempt to mlock pages into memory" },
	{ NULL,	"memfd-numa",	 "bind memory mappings to randomly selected NUMA nodes" },
	{ NULL,	"memfd-ops N",	 "stop after N memfd bogo operations" },
	{ NULL,	"memfd-zap-pte", "enable zap pte bug check (slow)" },
	{ NULL,	NULL,		 NULL }
};

static const stress_opt_t opts[] = {
	{ OPT_memfd_bytes,   "memfd-bytes",   TYPE_ID_SIZE_T_BYTES_VM, MIN_MEMFD_BYTES, MAX_MEM_LIMIT, NULL },
	{ OPT_memfd_fds,     "memfd-fds",     TYPE_ID_INT32, MIN_MEMFD_FDS, MAX_MEMFD_FDS, NULL },
	{ OPT_memfd_madvise, "memfd-madvise", TYPE_ID_BOOL, 0, 1, NULL },
	{ OPT_memfd_mlock,   "memfd-mlock",   TYPE_ID_BOOL, 0, 1, NULL },
	{ OPT_memfd_numa,    "memfd-numa",    TYPE_ID_BOOL, 0, 1, NULL },
	{ OPT_memfd_zap_pte, "memfd-zap-pte", TYPE_ID_BOOL, 0, 1, NULL },
	END_OPT
};

#if defined(HAVE_MEMFD_CREATE)

static const unsigned int flags[] = {
	0,
#if defined(MFD_CLOEXEC)
	MFD_CLOEXEC,
#endif
#if defined(MFD_ALLOW_SEALING)
	MFD_ALLOW_SEALING,
#endif
#if defined(MFD_HUGETLB) &&	\
    defined(MFD_HUGE_2MB)
	MFD_HUGETLB | MFD_HUGE_2MB,
#endif
#if defined(MFD_HUGETLB) &&	\
    defined(MFD_HUGE_1GB)
	MFD_HUGETLB | MFD_HUGE_1GB,
#endif
#if defined(MFD_NOEXEC_SEAL)
	MFD_NOEXEC_SEAL,
#endif
#if defined(MFD_EXEC)
	MFD_EXEC,
#endif
};

/*
 *  uint64_ptr_offset()
 *	add offset to 64 bit ptr
 */
static inline const uint64_t *uint64_ptr_offset(const uint64_t *ptr, const size_t offset)
{
	return (uint64_t *)((uintptr_t)ptr + offset);
}

/*
 *  stress_memfd_fill_pages()
 *	fill pages with random uin64_t values
 */
static void stress_memfd_fill_pages_generic(const uint64_t val, void *ptr, const size_t size)
{
	register uint64_t *u64ptr = (uint64_t *)ptr;
	register const uint64_t *u64end = uint64_ptr_offset(ptr, size);
	register uint64_t v = val;

	while (u64ptr < u64end) {
		*u64ptr = v;
		u64ptr += MEMFD_STRIDE;
		*u64ptr = v;
		u64ptr += MEMFD_STRIDE;
		*u64ptr = v;
		u64ptr += MEMFD_STRIDE;
		*u64ptr = v;
		u64ptr += MEMFD_STRIDE;
		*u64ptr = v;
		u64ptr += MEMFD_STRIDE;
		*u64ptr = v;
		u64ptr += MEMFD_STRIDE;
		*u64ptr = v;
		u64ptr += MEMFD_STRIDE;
		*u64ptr = v;
		u64ptr += MEMFD_STRIDE;
		v++;
	}
}

#if defined(HAVE_MADVISE) &&	\
    defined(MADV_PAGEOUT)
/*
 *  stress_memfd_check()
 *	check if buffer buf contains uint64_t values val, return true if OK, false if not
 */
static inline bool stress_memfd_check(
	const uint64_t val,
	const uint64_t *ptr,
	const size_t size,
	const uint64_t inc)
{
	register const uint64_t *u64ptr = ptr, *u64_end = uint64_ptr_offset(ptr, size);
	register uint64_t v = val;
	bool passed = true;

	while (u64ptr < u64_end) {
		passed &= (*u64ptr == v);
		u64ptr += MEMFD_STRIDE;
		passed &= (*u64ptr == v);
		u64ptr += MEMFD_STRIDE;
		passed &= (*u64ptr == v);
		u64ptr += MEMFD_STRIDE;
		passed &= (*u64ptr == v);
		u64ptr += MEMFD_STRIDE;
		passed &= (*u64ptr == v);
		u64ptr += MEMFD_STRIDE;
		passed &= (*u64ptr == v);
		u64ptr += MEMFD_STRIDE;
		passed &= (*u64ptr == v);
		u64ptr += MEMFD_STRIDE;
		passed &= (*u64ptr == v);
		u64ptr += MEMFD_STRIDE;
		v += inc;
	}
	return passed;
}
#endif

/*
 *  Create allocations using memfd_create, ftruncate and mmap
 */
static int stress_memfd_child(stress_args_t *args, void *context)
{
	int *fds, rc = EXIT_SUCCESS;
	register int fd;
	void **maps;
	int32_t i;
	const size_t page_size = args->page_size;
	const size_t min_size = 2 * page_size;
	size_t size, flag_index = 0;
	size_t memfd_bytes = DEFAULT_MEMFD_BYTES;
	int32_t memfd_fds = DEFAULT_MEMFD_FDS;
	double duration = 0.0, count = 0.0, rate;
	bool memfd_madvise = false;
	bool memfd_mlock = false;
	bool memfd_numa = false;
	bool memfd_zap_pte = false;
	char filename_rndstr[64], filename_unusual[64], filename_pid[64];
#if defined(HAVE_LINUX_MEMPOLICY_H)
	stress_numa_mask_t *numa_mask = NULL;
	stress_numa_mask_t *numa_nodes = NULL;
#endif

	stress_catch_sigill();

	(void)context;

	(void)stress_get_setting("memfd-madvise", &memfd_madvise);
	(void)stress_get_setting("memfd-mlock", &memfd_mlock);
	(void)stress_get_setting("memfd-numa", &memfd_numa);
	(void)stress_get_setting("memfd-zap-pte", &memfd_zap_pte);

#if !defined(HAVE_MADVISE)
	if (memfd_madvise) {
		if (stress_instance_zero(args)) {
			pr_inf("%s: disabling --memfd-madvise, madvise() "
				"not supported\n", args->name);
		}
		memfd_madvise = false;
	}
#endif

#if !defined(HAVE_MADVISE) ||	\
    !defined(MADV_PAGEOUT)
	if (memfd_zap_pte) {
		if (stress_instance_zero(args)) {
			pr_inf("%s: disabling --memfd-zap-pte, madvise() "
				"with MADV_PAGEOUT not supported\n", args->name);
		}
		memfd_zap_pte = false;
	}
#endif

	if (!stress_get_setting("memfd-bytes", &memfd_bytes)) {
		if (g_opt_flags & OPT_FLAGS_MAXIMIZE)
			memfd_bytes = MAX_32;
		if (g_opt_flags & OPT_FLAGS_MINIMIZE)
			memfd_bytes = MIN_MEMFD_BYTES;
	}
	memfd_bytes /= args->instances;
	if (memfd_bytes < MIN_MEMFD_BYTES)
		memfd_bytes = MIN_MEMFD_BYTES;

	(void)stress_get_setting("memfd-fds", &memfd_fds);

	size = memfd_bytes / memfd_fds;
	if (size < min_size)
		size = min_size;

	fds = (int *)calloc(memfd_fds, sizeof(*fds));
	if (!fds) {
		pr_inf("%s: cannot allocate fds buffer, errno=%d (%s)\n",
			args->name, errno, strerror(errno));
		return EXIT_NO_RESOURCE;
	}
	maps = (void **)calloc(memfd_fds, sizeof(*maps));
	if (!maps) {
		pr_inf("%s: cannot allocate maps buffer, errno=%d (%s)\n",
			args->name, errno, strerror(errno));
		free(fds);
		return EXIT_NO_RESOURCE;
	}

	if (memfd_numa) {
#if defined(HAVE_LINUX_MEMPOLICY_H)
		stress_numa_mask_and_node_alloc(args, &numa_nodes, &numa_mask, "--memfd-numa", &memfd_numa);
#else
		if (stress_instance_zero(args))
			pr_inf("%s: --memfd-numa selected but not supported by this system, disabling option\n",
				args->name);
		memfd_numa = false;
#endif
	}

	stress_rndstr(filename_rndstr, sizeof(filename_rndstr));
	(void)snprintf(filename_unusual, sizeof(filename_unusual),
		"memfd-%c[H%c%c:?*~", 27, 7, 255);
	(void)snprintf(filename_pid, sizeof(filename_pid),
		"memfd-%" PRIdMAX "-%" PRIu64, (intmax_t)args->pid, stress_mwc64());

	stress_set_proc_state(args->name, STRESS_STATE_SYNC_WAIT);
	stress_sync_start_wait(args);
	stress_set_proc_state(args->name, STRESS_STATE_RUN);

	do {
		char filename[64];
		double t;
		int min_fd = INT_MAX;
		int max_fd = INT_MIN;

		for (i = 0; i < memfd_fds; i++) {
			fds[i] = -1;
			maps[i] = MAP_FAILED;
		}

		for (i = 0; i < memfd_fds; i++) {
			/* Low memory avoidance, re-start */
			if ((g_opt_flags & OPT_FLAGS_OOM_AVOID) && stress_low_memory(size))
				break;

			(void)snprintf(filename, sizeof(filename),
				"memfd-%" PRIdMAX "-%" PRId32,
				(intmax_t)args->pid, i);

			t = stress_time_now();
			fd = shim_memfd_create(filename, 0);
			if (fd >= 0) {
				duration += stress_time_now() - t;
				count += 1.0;
				if (min_fd > fd)
					min_fd = fd;
				if (max_fd < fd)
					max_fd = fd;
				fds[i] = fd;
			} else {
				switch (errno) {
				case EMFILE:
				case ENFILE:
					break;
				case ENOMEM:
					goto memfd_unmap;
				case ENOSYS:
				case EFAULT:
				default:
					pr_fail("%s: memfd_create failed, errno=%d (%s)\n",
						args->name, errno, strerror(errno));
					stress_continue_set_flag(false);
					rc = EXIT_FAILURE;
					goto memfd_unmap;
				}
			}
			if (UNLIKELY(!stress_continue_flag()))
				goto memfd_unmap;
		}

		for (i = 0; i < memfd_fds; i++) {
			ssize_t ret;

#if defined(FALLOC_FL_PUNCH_HOLE) &&	\
    defined(FALLOC_FL_KEEP_SIZE)
			off_t whence;
#endif

			if (fds[i] < 0)
				continue;

			if (UNLIKELY(!stress_continue_flag()))
				break;

			/* Allocate space */
			ret = ftruncate(fds[i], (off_t)size);
			if (UNLIKELY(ret < 0)) {
				switch (errno) {
				case EINTR:
					break;
				default:
					pr_fail("%s: ftruncate failed, errno=%d (%s)\n",
						args->name, errno, strerror(errno));
					rc = EXIT_FAILURE;
					break;
				}
			}
			/*
			 *  ..and map it in, don't populate as this is expensive
			 */
			maps[i] = mmap(NULL, size, PROT_WRITE,
					MAP_FILE | MAP_SHARED, fds[i], 0);
			if (UNLIKELY(maps[i] == MAP_FAILED))
				continue;
			if (memfd_mlock)
				(void)shim_mlock(maps[i], size);
#if defined(HAVE_LINUX_MEMPOLICY_H)
			if (memfd_numa && numa_mask && numa_nodes)
				stress_numa_randomize_pages(args, numa_nodes, numa_mask, maps[i], size, page_size);
#endif
			stress_memfd_fill_pages_generic(stress_mwc64(), maps[i], size);
			if (memfd_madvise) {
				(void)stress_madvise_randomize(maps[i], size);
				(void)stress_madvise_mergeable(maps[i], size);
			}

#if defined(FALLOC_FL_PUNCH_HOLE) &&	\
    defined(FALLOC_FL_KEEP_SIZE)
			/*
			 *  ..and punch a hole
			 */
			whence = (off_t)(stress_mwc32modn(size) & ~(page_size - 1));
			VOID_RET(ssize_t, shim_fallocate(fds[i], FALLOC_FL_PUNCH_HOLE |
				FALLOC_FL_KEEP_SIZE, whence, (off_t)page_size));
#endif

			/*
			 *  ..and allocate space, this should fill file with zeros
			 *  and kernel compaction should kick in.
			 */
			VOID_RET(ssize_t, shim_fallocate(fds[i], 0, (off_t)size, 0));

			if (UNLIKELY(!stress_continue_flag()))
				goto memfd_unmap;
		}

		for (i = 0; i < memfd_fds; i++) {
			if (fds[i] < 0)
				continue;
			if (maps[i] == MAP_FAILED)
				continue;
#if defined(SEEK_SET)
			if (lseek(fds[i], (off_t)size >> 1, SEEK_SET) < 0) {
				if (UNLIKELY((errno != ENXIO) && (errno != EINVAL))) {
					pr_fail("%s: lseek SEEK_SET failed, errno=%d (%s)\n",
						args->name, errno, strerror(errno));
					rc = EXIT_FAILURE;
				}
			}
#endif
#if defined(SEEK_CUR)
			if (lseek(fds[i], (off_t)0, SEEK_CUR) < 0) {
				if (UNLIKELY((errno != ENXIO) && (errno != EINVAL))) {
					pr_fail("%s: lseek SEEK_CUR failed, errno=%d (%s)\n",
						args->name, errno, strerror(errno));
					rc = EXIT_FAILURE;
				}
			}
#endif
#if defined(SEEK_END)
			if (lseek(fds[i], (off_t)0, SEEK_END) < 0) {
				if (UNLIKELY((errno != ENXIO) && (errno != EINVAL))) {
					pr_fail("%s: lseek SEEK_END failed, errno=%d (%s)\n",
						args->name, errno, strerror(errno));
					rc = EXIT_FAILURE;
				}
			}
#endif
#if defined(SEEK_HOLE)
			if (lseek(fds[i], (off_t)0, SEEK_HOLE) < 0) {
				if (UNLIKELY((errno != ENXIO) && (errno != EINVAL))) {
					pr_fail("%s: lseek SEEK_HOLE failed, errno=%d (%s)\n",
						args->name, errno, strerror(errno));
					rc = EXIT_FAILURE;
				}
			}
#endif
#if defined(SEEK_DATA)
			if (lseek(fds[i], (off_t)0, SEEK_DATA) < 0) {
				if (UNLIKELY((errno != ENXIO) && (errno != EINVAL))) {
					pr_fail("%s: lseek SEEK_DATA failed, errno=%d (%s)\n",
						args->name, errno, strerror(errno));
					rc = EXIT_FAILURE;
				}
			}
#endif
			if (UNLIKELY(!stress_continue_flag()))
				goto memfd_unmap;
		}

memfd_unmap:
		for (i = 0; i < memfd_fds; i++) {
			if (maps[i] != MAP_FAILED)
				(void)munmap(maps[i], size);
		}
#if defined(HAVE_MADVISE) &&	\
    defined(MADV_PAGEOUT)
		if (UNLIKELY(memfd_zap_pte)) {
			/*
			 *  Check for zap_pte bug, see Linux commit
			 *  5abfd71d936a8aefd9f9ccd299dea7a164a5d455
			 */
			for (i = 0; LIKELY(stress_continue_flag() && (i < memfd_fds)); i++) {
				uint64_t *buf;
				uint64_t val;
				const ssize_t test_size = page_size << 1;

				if (UNLIKELY(ftruncate(fds[i], (off_t)test_size) < 0))
					continue;
				buf = mmap(NULL, test_size, PROT_READ | PROT_WRITE,
						MAP_PRIVATE, fds[i], 0);
				if (UNLIKELY(buf == MAP_FAILED))
					continue;
				if (memfd_mlock)
					(void)shim_mlock(buf, test_size);
				val = stress_mwc64();
				stress_memfd_fill_pages_generic(val, buf, test_size);

				if (UNLIKELY(madvise(buf, test_size, MADV_PAGEOUT) < 0))
					goto buf_unmap;
				if (UNLIKELY(ftruncate(fds[i], (off_t)page_size) < 0))
					goto buf_unmap;
				if (UNLIKELY(ftruncate(fds[i], (off_t)test_size) < 0))
					goto buf_unmap;
				if (UNLIKELY(!stress_memfd_check(val, buf, page_size, 1))) {
					pr_fail("%s: unexpected memfd %d data mismatch in first page\n",
						args->name, fds[i]);
					rc = EXIT_FAILURE;
				}
				if (UNLIKELY(!stress_memfd_check(0ULL, uint64_ptr_offset(buf, page_size), page_size, 0))) {
					pr_fail("%s: unexpected memfd %d data mismatch in zero'd second page\n",
						args->name, fds[i]);
					rc = EXIT_FAILURE;
				}
buf_unmap:
				(void)munmap((void *)buf, test_size);
				VOID_RET(int, ftruncate(fds[i], 0));
			}
		}
#endif
		if (shim_close_range(min_fd, max_fd, 0) < 0) {
			for (i = 0; i < memfd_fds; i++) {
				(void)close(fds[i]);
			}
		}

		/* Exercise illegal memfd name */
		fd = shim_memfd_create(filename_rndstr, 0);
		if (fd >= 0)
			(void)close(fd);

		/* Exercise illegal flags */
		fd = shim_memfd_create(filename_pid, ~0U);
		if (fd >= 0)
			(void)close(fd);

		/* Exercise spacy name */
		fd = shim_memfd_create(" ", ~0U);
		if (fd >= 0)
			(void)close(fd);

		/* Exercise illegal path / in name */
		fd = shim_memfd_create("/path/in/name", ~0U);
		if (fd >= 0)
			(void)close(fd);

		/* Exercise unusual chars in name */
		fd = shim_memfd_create(filename_unusual, 0);
		if (fd >= 0)
			(void)close(fd);

		/* Exercise illegal path / in name */
		fd = shim_memfd_create("/path/in/name", ~0U);
		if (fd >= 0)
			(void)close(fd);

		/* Exercise illegal zero length name */
		fd = shim_memfd_create("", ~0U);
		if (fd >= 0)
			(void)close(fd);

		/* Exercise next flag */
		t = stress_time_now();
		fd = shim_memfd_create(filename_pid, flags[flag_index]);
		if (fd >= 0) {
			duration += stress_time_now() - t;
			count += 1.0;
			(void)close(fd);
		}
		flag_index++;
		if (flag_index >= SIZEOF_ARRAY(flags))
			flag_index = 0;
		stress_bogo_inc(args);
	} while (stress_continue(args));

	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);

	rate = (count > 0.0) ? duration / count : 0.0;
	stress_metrics_set(args, 0, "nanosecs per memfd_create call",
		rate * STRESS_DBL_NANOSECOND, STRESS_METRIC_HARMONIC_MEAN);

#if defined(HAVE_LINUX_MEMPOLICY_H)
	if (numa_mask)
		stress_numa_mask_free(numa_mask);
	if (numa_nodes)
		stress_numa_mask_free(numa_nodes);
#endif
	free(maps);
	free(fds);

	return rc;
}

/*
 *  stress_memfd()
 *	stress memfd
 */
static int stress_memfd(stress_args_t *args)
{
	return stress_oomable_child(args, NULL, stress_memfd_child, STRESS_OOMABLE_NORMAL);
}

const stressor_info_t stress_memfd_info = {
	.stressor = stress_memfd,
	.classifier = CLASS_OS | CLASS_MEMORY,
	.opts = opts,
	.verify = VERIFY_ALWAYS,
	.help = help
};
#else
const stressor_info_t stress_memfd_info = {
	.stressor = stress_unimplemented,
	.classifier = CLASS_OS | CLASS_MEMORY,
	.opts = opts,
	.verify = VERIFY_ALWAYS,
	.help = help,
	.unimplemented_reason = "built without memfd_create() system call"
};
#endif
