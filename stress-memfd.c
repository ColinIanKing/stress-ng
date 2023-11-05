/*
 * Copyright (C) 2013-2021 Canonical, Ltd.
 * Copyright (C) 2021-2023 Colin Ian King.
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
#include "core-out-of-memory.h"
#include "core-target-clones.h"

#define MIN_MEMFD_BYTES		(2 * MB)
#define DEFAULT_MEMFD_BYTES	(256 * MB)

#define MIN_MEMFD_FDS		(8)
#define MAX_MEMFD_FDS		(4096)
#define DEFAULT_MEMFD_FDS	(256)

static const stress_help_t help[] = {
	{ NULL,	"memfd N",	 "start N workers allocating memory with memfd_create" },
	{ NULL,	"memfd-bytes N", "allocate N bytes for each stress iteration" },
	{ NULL,	"memfd-fds N",	 "number of memory fds to open per stressors" },
	{ NULL,	"memfd-mlock",	 "attempt to mlock pages into memory" },
	{ NULL,	"memfd-ops N",	 "stop after N memfd bogo operations" },
	{ NULL,	NULL,		 NULL }
};

static int stress_set_memfd_mlock(const char *opt)
{
	return stress_set_setting_true("memfd-mlock", opt);
}

/*
 *  stress_set_memfd_bytes
 *	set max size of each memfd size
 */
static int stress_set_memfd_bytes(const char *opt)
{
	size_t memfd_bytes;

	memfd_bytes = (size_t)stress_get_uint64_byte_memory(opt, 1);
	stress_check_range_bytes("memfd-bytes", memfd_bytes,
		MIN_MEMFD_BYTES, MAX_MEM_LIMIT);
	return stress_set_setting("memfd-bytes", TYPE_ID_SIZE_T, &memfd_bytes);
}

/*
 *  stress_set_memfd_fds()
 *      set number of memfd file descriptors
 */
static int stress_set_memfd_fds(const char *opt)
{
	uint32_t memfd_fds;

	memfd_fds = (uint32_t)stress_get_uint64(opt);
	stress_check_range("memfd-fds", memfd_fds,
		MIN_MEMFD_FDS, MAX_MEMFD_FDS);
	return stress_set_setting("memfd-fds", TYPE_ID_UINT32, &memfd_fds);
}

static const stress_opt_set_func_t opt_set_funcs[] = {
	{ OPT_memfd_bytes,	stress_set_memfd_bytes },
	{ OPT_memfd_fds,	stress_set_memfd_fds },
	{ OPT_memfd_mlock,	stress_set_memfd_mlock },
	{ 0,			NULL }
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
static void TARGET_CLONES stress_memfd_fill_pages_generic(void *ptr, const size_t size)
{
	register uint64_t *u64ptr = (uint64_t *)ptr;
	register const uint64_t *u64end = uint64_ptr_offset(ptr, size);
	register uint64_t v = stress_mwc64();

	while (u64ptr < u64end) {
		*u64ptr++ = v;
		*u64ptr++ = v;
		*u64ptr++ = v;
		*u64ptr++ = v;
		*u64ptr++ = v;
		*u64ptr++ = v;
		*u64ptr++ = v;
		*u64ptr++ = v;
		*u64ptr++ = v;
		*u64ptr++ = v;
		*u64ptr++ = v;
		*u64ptr++ = v;
		*u64ptr++ = v;
		*u64ptr++ = v;
		*u64ptr++ = v;
		*u64ptr++ = v;
		*u64ptr++ = v;
		*u64ptr++ = v;
		*u64ptr++ = v;
		*u64ptr++ = v;
		*u64ptr++ = v;
		*u64ptr++ = v;
		*u64ptr++ = v;
		*u64ptr++ = v;
		*u64ptr++ = v;
		*u64ptr++ = v;
		*u64ptr++ = v;
		*u64ptr++ = v;
		*u64ptr++ = v;
		*u64ptr++ = v;
		*u64ptr++ = v;
		*u64ptr++ = v;
		v++;
	}
}

#if defined(HAVE_NT_STORE64)
/*
 *  stress_memfd_fill_pages_nt_store()
 *	fill pages with random uin64_t values using nt_store
 */
static void OPTIMIZE3 stress_memfd_fill_pages_nt_store(void *ptr, const size_t size)
{
	register uint64_t *u64ptr = (uint64_t *)ptr;
	register const uint64_t *u64end = uint64_ptr_offset(ptr, size);
	register uint64_t v = stress_mwc64();

	while (u64ptr < u64end) {
		stress_nt_store64(u64ptr++, v);
		stress_nt_store64(u64ptr++, v);
		stress_nt_store64(u64ptr++, v);
		stress_nt_store64(u64ptr++, v);
		stress_nt_store64(u64ptr++, v);
		stress_nt_store64(u64ptr++, v);
		stress_nt_store64(u64ptr++, v);
		stress_nt_store64(u64ptr++, v);
		stress_nt_store64(u64ptr++, v);
		stress_nt_store64(u64ptr++, v);
		stress_nt_store64(u64ptr++, v);
		stress_nt_store64(u64ptr++, v);
		stress_nt_store64(u64ptr++, v);
		stress_nt_store64(u64ptr++, v);
		stress_nt_store64(u64ptr++, v);
		stress_nt_store64(u64ptr++, v);
		stress_nt_store64(u64ptr++, v);
		stress_nt_store64(u64ptr++, v);
		stress_nt_store64(u64ptr++, v);
		stress_nt_store64(u64ptr++, v);
		stress_nt_store64(u64ptr++, v);
		stress_nt_store64(u64ptr++, v);
		stress_nt_store64(u64ptr++, v);
		stress_nt_store64(u64ptr++, v);
		stress_nt_store64(u64ptr++, v);
		stress_nt_store64(u64ptr++, v);
		stress_nt_store64(u64ptr++, v);
		stress_nt_store64(u64ptr++, v);
		stress_nt_store64(u64ptr++, v);
		stress_nt_store64(u64ptr++, v);
		stress_nt_store64(u64ptr++, v);
		stress_nt_store64(u64ptr++, v);
		v++;
	}
}
#endif

#if defined(HAVE_MADVISE) &&	\
    defined(MADV_PAGEOUT)
/*
 *  stress_memfd_check()
 *	check if buffer buf contains uint64_t values val, return true if OK, false if not
 */
static bool stress_memfd_check(
	const uint64_t val,
	const uint64_t *buf,
	const size_t size)
{
	const uint64_t *ptr, *end_ptr = uint64_ptr_offset(buf, size);

	for (ptr = buf; ptr < end_ptr; ptr++) {
		if (*ptr != val)
			return false;
	}
	return true;
}
#endif

/*
 *  Create allocations using memfd_create, ftruncate and mmap
 */
static int stress_memfd_child(const stress_args_t *args, void *context)
{
	int *fds, fd;
	void **maps;
	uint64_t i;
	const size_t page_size = args->page_size;
	const size_t min_size = 2 * page_size;
	size_t size;
	size_t memfd_bytes = DEFAULT_MEMFD_BYTES;
	uint32_t memfd_fds = DEFAULT_MEMFD_FDS;
	int mmap_flags = MAP_FILE | MAP_SHARED;
	double duration = 0.0, count = 0.0, rate;
	bool memfd_mlock = false;
#if defined(HAVE_NT_STORE64)
	void (*stress_memfd_fill_pages)(void *ptr, const size_t size) =
		stress_cpu_x86_has_sse2() ? stress_memfd_fill_pages_nt_store : stress_memfd_fill_pages_generic;
#else
	void (*stress_memfd_fill_pages)(void *ptr, const size_t size) = stress_memfd_fill_pages_generic;
#endif

#if defined(MAP_POPULATE)
	mmap_flags |= MAP_POPULATE;
#endif

	stress_catch_sigill();

	(void)context;

	(void)stress_get_setting("memfd-mlock", &memfd_mlock);

	if (!stress_get_setting("memfd-bytes", &memfd_bytes)) {
		if (g_opt_flags & OPT_FLAGS_MAXIMIZE)
			memfd_bytes = MAX_32;
		if (g_opt_flags & OPT_FLAGS_MINIMIZE)
			memfd_bytes = MIN_MEMFD_BYTES;
	}
	memfd_bytes /= args->num_instances;
	if (memfd_bytes < MIN_MEMFD_BYTES)
		memfd_bytes = MIN_MEMFD_BYTES;

	(void)stress_get_setting("memfd-fds", &memfd_fds);

	size = memfd_bytes / memfd_fds;
	if (size < min_size)
		size = min_size;

	fds = calloc(memfd_fds, sizeof(*fds));
	if (!fds) {
		pr_inf("%s: cannot allocate fds buffer: %d (%s)\n",
			args->name, errno, strerror(errno));
		return EXIT_NO_RESOURCE;
	}
	maps = calloc(memfd_fds, sizeof(*maps));
	if (!maps) {
		pr_inf("%s: cannot allocate maps buffer: %d (%s)\n",
			args->name, errno, strerror(errno));
		free(fds);
		return EXIT_NO_RESOURCE;
	}

	stress_set_proc_state(args->name, STRESS_STATE_RUN);

	do {
		char filename[PATH_MAX];
		double t;

		for (i = 0; i < memfd_fds; i++) {
			fds[i] = -1;
			maps[i] = MAP_FAILED;
		}

		for (i = 0; i < memfd_fds; i++) {
			/* Low memory avoidance, re-start */
			if ((g_opt_flags & OPT_FLAGS_OOM_AVOID) && stress_low_memory(size))
				break;

			(void)snprintf(filename, sizeof(filename),
				"memfd-%" PRIdMAX "-%" PRIu64,
				(intmax_t)args->pid, i);

			t = stress_time_now();
			fds[i] = shim_memfd_create(filename, 0);
			if (fds[i] >= 0) {
				duration += stress_time_now() - t;
				count += 1.0;
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
					pr_fail("%s: memfd_create failed: errno=%d (%s)\n",
						args->name, errno, strerror(errno));
					stress_continue_set_flag(false);
					goto memfd_unmap;
				}
			}
			if (!stress_continue_flag())
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

			if (!stress_continue_flag())
				break;

			/* Allocate space */
			ret = ftruncate(fds[i], (off_t)size);
			if (ret < 0) {
				switch (errno) {
				case EINTR:
					break;
				default:
					pr_fail("%s: ftruncate failed, errno=%d (%s)\n",
						args->name, errno, strerror(errno));
					break;
				}
			}
			/*
			 * ..and map it in, using MAP_POPULATE
			 * to force page it in
			 */
			maps[i] = mmap(NULL, size, PROT_WRITE, mmap_flags, fds[i], 0);
			if (maps[i] == MAP_FAILED)
				continue;
			if (memfd_mlock)
				(void)shim_mlock(maps[i], size);
			stress_memfd_fill_pages(maps[i], size);
			(void)stress_madvise_random(maps[i], size);
			(void)stress_madvise_mergeable(maps[i], size);

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

			if (!stress_continue_flag())
				goto memfd_unmap;
		}

		for (i = 0; i < memfd_fds; i++) {
			if (fds[i] < 0)
				continue;
			if (maps[i] == MAP_FAILED)
				continue;
#if defined(SEEK_SET)
			if (lseek(fds[i], (off_t)size >> 1, SEEK_SET) < 0) {
				if ((errno != ENXIO) && (errno != EINVAL))
					pr_fail("%s: lseek SEEK_SET failed, errno=%d (%s)\n",
						args->name, errno, strerror(errno));
			}
#endif
#if defined(SEEK_CUR)
			if (lseek(fds[i], (off_t)0, SEEK_CUR) < 0) {
				if ((errno != ENXIO) && (errno != EINVAL))
					pr_fail("%s: lseek SEEK_CUR failed, errno=%d (%s)\n",
						args->name, errno, strerror(errno));
			}
#endif
#if defined(SEEK_END)
			if (lseek(fds[i], (off_t)0, SEEK_END) < 0) {
				if ((errno != ENXIO) && (errno != EINVAL))
					pr_fail("%s: lseek SEEK_END failed, errno=%d (%s)\n",
						args->name, errno, strerror(errno));
			}
#endif
#if defined(SEEK_HOLE)
			if (lseek(fds[i], (off_t)0, SEEK_HOLE) < 0) {
				if ((errno != ENXIO) && (errno != EINVAL))
					pr_fail("%s: lseek SEEK_HOLE failed, errno=%d (%s)\n",
						args->name, errno, strerror(errno));
			}
#endif
#if defined(SEEK_DATA)
			if (lseek(fds[i], (off_t)0, SEEK_DATA) < 0) {
				if ((errno != ENXIO) && (errno != EINVAL))
					pr_fail("%s: lseek SEEK_DATA failed, errno=%d (%s)\n",
						args->name, errno, strerror(errno));
			}
#endif
			if (!stress_continue_flag())
				goto memfd_unmap;
		}

memfd_unmap:
		for (i = 0; i < memfd_fds; i++) {
			if (maps[i] != MAP_FAILED)
				(void)munmap(maps[i], size);
		}
#if defined(HAVE_MADVISE) &&	\
    defined(MADV_PAGEOUT)
		/*
		 *  Check for zap_pte bug, see Linux commit
		 *  5abfd71d936a8aefd9f9ccd299dea7a164a5d455
		 */
		for (i = 0; stress_continue_flag() && (i < memfd_fds); i++) {
			uint64_t *buf, *ptr;
			const uint64_t *end_ptr;
			uint64_t val = stress_mwc64();
			const ssize_t test_size = page_size << 1;

			if (ftruncate(fds[i], (off_t)test_size) < 0)
				continue;
			buf = mmap(NULL, test_size, PROT_READ | PROT_WRITE,
					MAP_PRIVATE, fds[i], 0);
			if (buf == MAP_FAILED)
				continue;
			if (memfd_mlock)
				(void)shim_mlock(buf, test_size);
			end_ptr = uint64_ptr_offset(buf, test_size);
			for (ptr = buf; ptr < end_ptr; ptr++)
				*ptr = val;

			if (madvise(buf, test_size, MADV_PAGEOUT) < 0)
				goto buf_unmap;
			if (ftruncate(fds[i], (off_t)page_size) < 0)
				goto buf_unmap;
			if (ftruncate(fds[i], (off_t)test_size) < 0)
				goto buf_unmap;
			if (!stress_memfd_check(val, buf, page_size))
				pr_fail("%s: unexpected memfd %d data mismatch in first page\n",
					args->name, fds[i]);
			if (!stress_memfd_check(0ULL, uint64_ptr_offset(buf, page_size), page_size))
				pr_fail("%s: unexpected memfd %d data mismatch in zero'd second page\n",
					args->name, fds[i]);
buf_unmap:
			(void)munmap((void *)buf, test_size);
			VOID_RET(int, ftruncate(fds[i], 0));
		}
#endif

		stress_close_fds(fds, memfd_fds);

		/* Exercise illegal memfd name */
		stress_rndstr(filename, sizeof(filename));
		fd = shim_memfd_create(filename, 0);
		if (fd >= 0)
			(void)close(fd);

		/* Exercise illegal flags */
		(void)snprintf(filename, sizeof(filename),
			"memfd-%" PRIdMAX "-%" PRIu64,
			(intmax_t)args->pid, stress_mwc64());
		fd = shim_memfd_create(filename, ~0U);
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
		(void)snprintf(filename, sizeof(filename),
			"memfd-%c[H%c%c:?*~", 27, 7, 255);
		fd = shim_memfd_create(filename, 0);
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

		/* Exercise all flags */
		for (i = 0; i < (uint64_t)SIZEOF_ARRAY(flags); i++) {
			(void)snprintf(filename, sizeof(filename),
				"memfd-%" PRIdMAX"-%" PRIu64,
				(intmax_t)args->pid, i);
			t = stress_time_now();
			fd = shim_memfd_create(filename, flags[i]);
			if (fd >= 0) {
				duration += stress_time_now() - t;
				count += 1.0;
				(void)close(fd);
			}
		}
		stress_bogo_inc(args);
	} while (stress_continue(args));

	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);

	rate = (count > 0.0) ? duration / count : 0.0;
	stress_metrics_set(args, 0, "nanosecs per memfd_create call", rate * STRESS_DBL_NANOSECOND);

	free(maps);
	free(fds);

	return EXIT_SUCCESS;
}

/*
 *  stress_memfd()
 *	stress memfd
 */
static int stress_memfd(const stress_args_t *args)
{
	return stress_oomable_child(args, NULL, stress_memfd_child, STRESS_OOMABLE_NORMAL);
}

stressor_info_t stress_memfd_info = {
	.stressor = stress_memfd,
	.class = CLASS_OS | CLASS_MEMORY,
	.opt_set_funcs = opt_set_funcs,
	.verify = VERIFY_ALWAYS,
	.help = help
};
#else
stressor_info_t stress_memfd_info = {
	.stressor = stress_unimplemented,
	.class = CLASS_OS | CLASS_MEMORY,
	.opt_set_funcs = opt_set_funcs,
	.verify = VERIFY_ALWAYS,
	.help = help,
	.unimplemented_reason = "built without memfd_create() system call"
};
#endif
