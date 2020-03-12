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
	{ NULL,	"mmap N",	 "start N workers stressing mmap and munmap" },
	{ NULL,	"mmap-ops N",	 "stop after N mmap bogo operations" },
	{ NULL,	"mmap-async",	 "using asynchronous msyncs for file based mmap" },
	{ NULL,	"mmap-bytes N",	 "mmap and munmap N bytes for each stress iteration" },
	{ NULL,	"mmap-file",	 "mmap onto a file using synchronous msyncs" },
	{ NULL,	"mmap-mprotect", "enable mmap mprotect stressing" },
	{ NULL, "mmap-osync",	 "enable O_SYNC on file" },
	{ NULL, "mmap-odirect",	 "enable O_DIRECT on file" },
	{ NULL,	NULL,		 NULL }
};

typedef struct {
	int fd;
	int flags;
	size_t sz;
	size_t mmap_bytes;
	bool mmap_mprotect;
	bool mmap_file;
	bool mmap_async;
} stress_mmap_context_t;

#define NO_MEM_RETRIES_MAX	(65536)

/* Misc randomly chosen mmap flags */
static const int mmap_flags[] = {
#if defined(MAP_HUGE_2MB) && defined(MAP_HUGETLB)
	MAP_HUGE_2MB | MAP_HUGETLB,
#endif
#if defined(MAP_HUGE_1GB) && defined(MAP_HUGETLB)
	MAP_HUGE_1GB | MAP_HUGETLB,
#endif
#if defined(MAP_HUGETLB)
	MAP_HUGETLB,
#endif
#if defined(MAP_NONBLOCK)
	MAP_NONBLOCK,
#endif
#if defined(MAP_GROWSDOWN)
	MAP_GROWSDOWN,
#endif
#if defined(MAP_LOCKED)
	MAP_LOCKED,
#endif
#if defined(MAP_32BIT) && (defined(__x86_64__) || defined(__x86_64))
	MAP_32BIT,
#endif
#if defined(MAP_NOCACHE)	/* Mac OS X */
	MAP_NOCACHE,
#endif
#if defined(MAP_HASSEMAPHORE)	/* Mac OS X */
	MAP_HASSEMAPHORE,
#endif
#if defined(MAP_NORESERVE)
	MAP_NORESERVE,
#endif
#if defined(MAP_STACK)
	MAP_STACK,
#endif
#if defined(MAP_EXECUTABLE)
	MAP_EXECUTABLE,
#endif
#if defined(MAP_UNINITIALIZED)
	MAP_UNINITIALIZED,
#endif
#if defined(MAP_DENYWRITE)
	MAP_DENYWRITE,
#endif
/* OpenBSD */
#if defined(MAP_HASSEMAPHORE)
	 MAP_HASSEMAPHORE,
#endif
/* OpenBSD */
#if defined(MAP_INHERIT)
	MAP_INHERIT,
#endif
/* FreeBSD */
#if defined(MAP_NOCORE)
	MAP_NOCORE,
#endif
/* FreeBSD */
#if defined(MAP_NOSYNC)
	MAP_NOSYNC,
#endif
/* FreeBSD */
#if defined(MAP_PREFAULT_READ)
	MAP_PREFAULT_READ,
#endif
	0
};

static int stress_set_mmap_bytes(const char *opt)
{
	size_t mmap_bytes;

	mmap_bytes = (size_t)stress_get_uint64_byte_memory(opt, 1);
	stress_check_range_bytes("mmap-bytes", mmap_bytes,
		MIN_MMAP_BYTES, MAX_MEM_LIMIT);
	return stress_set_setting("mmap-bytes", TYPE_ID_SIZE_T, &mmap_bytes);
}

static int stress_set_mmap_mprotect(const char *opt)
{
	bool mmap_mprotect = true;

	(void)opt;
	return stress_set_setting("mmap-mprotect", TYPE_ID_BOOL, &mmap_mprotect);
}

static int stress_set_mmap_file(const char *opt)
{
	bool mmap_file = true;

	(void)opt;
	return stress_set_setting("mmap-file", TYPE_ID_BOOL, &mmap_file);
}

static int stress_set_mmap_async(const char *opt)
{
	bool mmap_async = true;

	(void)opt;
	return stress_set_setting("mmap-async", TYPE_ID_BOOL, &mmap_async);
}

static int stress_set_mmap_osync(const char *opt)
{
	bool mmap_osync = true;

	(void)opt;
	return stress_set_setting("mmap-osync", TYPE_ID_BOOL, &mmap_osync);
}

static int stress_set_mmap_odirect(const char *opt)
{
	bool mmap_odirect = true;

	(void)opt;
	return stress_set_setting("mmap-odirect", TYPE_ID_BOOL, &mmap_odirect);
}

/*
 *  stress_mmap_mprotect()
 *	cycle through page settings on a region of mmap'd memory
 */
static void stress_mmap_mprotect(
	const char *name,
	void *addr,
	const size_t len,
	const bool mmap_mprotect)
{
#if defined(HAVE_MPROTECT)
	if (mmap_mprotect) {
		/* Cycle through potection */
		if (mprotect(addr, len, PROT_NONE) < 0)
			pr_fail("%s: mprotect set to PROT_NONE failed\n", name);
		if (mprotect(addr, len, PROT_READ) < 0)
			pr_fail("%s: mprotect set to PROT_READ failed\n", name);
		if (mprotect(addr, len, PROT_WRITE) < 0)
			pr_fail("%s: mprotect set to PROT_WRITE failed\n", name);
		if (mprotect(addr, len, PROT_EXEC) < 0)
			pr_fail("%s: mprotect set to PROT_EXEC failed\n", name);
		if (mprotect(addr, len, PROT_READ | PROT_WRITE) < 0)
			pr_fail("%s: mprotect set to PROT_READ | PROT_WRITE failed\n", name);
	}
#else
	(void)name;
	(void)addr;
	(void)len;
	(void)mmap_mprotect;
#endif
}

static int stress_mmap_child(const stress_args_t *args, void *ctxt)
{
	stress_mmap_context_t *context = (stress_mmap_context_t *)ctxt;
	const size_t page_size = args->page_size;
	const size_t sz = context->sz;
	const size_t pages4k = sz / page_size;
	const bool mmap_file = context->mmap_file;
	const int fd = context->fd;
	int no_mem_retries = 0;
	const int ms_flags = context->mmap_async ? MS_ASYNC : MS_SYNC;
	uint8_t *mapped, **mappings;

	mapped = calloc(pages4k, sizeof(*mapped));
	if (!mapped) {
		pr_dbg("%s: cannot allocate mapped buffer: %d (%s)\n",
			args->name, errno, strerror(errno));
		return EXIT_NO_RESOURCE;
	}
	mappings = calloc(pages4k, sizeof(*mappings));
	if (!mappings) {
		pr_dbg("%s: cannot allocate mappings buffer: %d (%s)\n",
			args->name, errno, strerror(errno));
		free(mapped);
		return EXIT_NO_RESOURCE;
	}

	do {
		size_t n;
		const int rnd = stress_mwc32() % SIZEOF_ARRAY(mmap_flags);
		int rnd_flag = mmap_flags[rnd];
		uint8_t *buf = NULL;

#if defined(MAP_HUGETLB) ||		\
    defined(MAP_UNINITIALIZED) || 	\
    defined(MAP_DENYWRITE)
retry:
#endif
		if (no_mem_retries >= NO_MEM_RETRIES_MAX) {
			pr_inf("%s: gave up trying to mmap, no available memory\n",
				args->name);
			break;
		}

		if (!keep_stressing_flag())
			break;
		buf = (uint8_t *)mmap(NULL, sz,
			PROT_READ | PROT_WRITE, context->flags | rnd_flag, fd, 0);
		if (buf == MAP_FAILED) {
#if defined(MAP_POPULATE)
			/* Force MAP_POPULATE off, just in case */
			if (context->flags & MAP_POPULATE) {
				context->flags &= ~MAP_POPULATE;
				no_mem_retries++;
				continue;
			}
#endif
#if defined(MAP_HUGETLB)
			/* Force MAP_HUGETLB off, just in case */
			if (rnd_flag & MAP_HUGETLB) {
				rnd_flag &= ~MAP_HUGETLB;
				no_mem_retries++;
				goto retry;
			}
#endif
#if defined(MAP_UNINITIALIZED)
			/* Force MAP_UNINITIALIZED off, just in case */
			if (rnd_flag & MAP_UNINITIALIZED) {
				rnd_flag &= ~MAP_UNINITIALIZED;
				no_mem_retries++;
				goto retry;
			}
#endif
#if defined(MAP_DENYWRITE)
			/* Force MAP_DENYWRITE off, just in case */
			if (rnd_flag & MAP_DENYWRITE) {
				rnd_flag &= ~MAP_DENYWRITE;
				no_mem_retries++;
				goto retry;
			}
#endif
			no_mem_retries++;
			if (no_mem_retries > 1)
				(void)shim_usleep(100000);
			continue;	/* Try again */
		}
		no_mem_retries = 0;
		if (mmap_file) {
			(void)memset(buf, 0xff, sz);
			(void)shim_msync((void *)buf, sz, ms_flags);
		}
		(void)stress_madvise_random(buf, sz);
		(void)stress_mincore_touch_pages(buf, context->mmap_bytes);
		stress_mmap_mprotect(args->name, buf, sz, context->mmap_mprotect);
		for (n = 0; n < pages4k; n++) {
			mapped[n] = PAGE_MAPPED;
			mappings[n] = buf + (n * page_size);
		}

		/* Ensure we can write to the mapped pages */
		stress_mmap_set(buf, sz, page_size);
		if (g_opt_flags & OPT_FLAGS_VERIFY) {
			if (stress_mmap_check(buf, sz, page_size) < 0)
				pr_fail("%s: mmap'd region of %zu bytes does "
					"not contain expected data\n", args->name, sz);
		}

		/*
		 *  Step #0, write + read the mmap'd data from the file back into
		 *  the mappings.
		 */
		if ((fd >= 0) && (mmap_file)) {
			off_t offset = 0;

			for (n = 0; n < pages4k; n++, offset += page_size) {
				ssize_t ret;

				if (lseek(fd, offset, SEEK_SET) < 0)
					continue;

				ret = write(fd, mappings[n], page_size);
				(void)ret;
				ret = read(fd, mappings[n], page_size);
				(void)ret;
			}
		}

		/*
		 *  Step #1, unmap all pages in random order
		 */
		(void)stress_mincore_touch_pages(buf, context->mmap_bytes);
		for (n = pages4k; n; ) {
			uint64_t j, i = stress_mwc64() % pages4k;
			for (j = 0; j < n; j++) {
				uint64_t page = (i + j) % pages4k;
				if (mapped[page] == PAGE_MAPPED) {
					mapped[page] = 0;
					(void)stress_madvise_random(mappings[page], page_size);
					stress_mmap_mprotect(args->name, mappings[page],
						page_size, context->mmap_mprotect);
					(void)munmap((void *)mappings[page], page_size);
					n--;
					break;
				}
				if (!keep_stressing_flag())
					goto cleanup;
			}
		}
		(void)munmap((void *)buf, sz);
#if defined(MAP_FIXED)
		/*
		 *  Step #2, map them back in random order
		 */
		for (n = pages4k; n; ) {
			uint64_t j, i = stress_mwc64() % pages4k;

			for (j = 0; j < n; j++) {
				uint64_t page = (i + j) % pages4k;

				if (!mapped[page]) {
					off_t offset = mmap_file ? page * page_size : 0;
					int fixed_flags = MAP_FIXED;

					/*
					 * Attempt to map them back into the original address, this
					 * may fail (it's not the most portable operation), so keep
					 * track of failed mappings too
					 */
#if defined(MAP_FIXED_NOREPLACE)
					if (stress_mwc1())
						fixed_flags = MAP_FIXED_NOREPLACE;
#endif
					mappings[page] = (uint8_t *)mmap((void *)mappings[page],
						page_size, PROT_READ | PROT_WRITE, fixed_flags | context->flags, fd, offset);

					if (mappings[page] == MAP_FAILED) {
						mapped[page] = PAGE_MAPPED_FAIL;
						mappings[page] = NULL;
					} else {
						(void)stress_mincore_touch_pages(mappings[page], page_size);
						(void)stress_madvise_random(mappings[page], page_size);
						stress_mmap_mprotect(args->name, mappings[page],
							page_size, context->mmap_mprotect);
						mapped[page] = PAGE_MAPPED;
						/* Ensure we can write to the mapped page */
						stress_mmap_set(mappings[page], page_size, page_size);
						if (stress_mmap_check(mappings[page], page_size, page_size) < 0)
							pr_fail("%s: mmap'd region of %zu bytes does "
								"not contain expected data\n", args->name, page_size);
						if (mmap_file) {
							(void)memset(mappings[page], n, page_size);
							(void)shim_msync((void *)mappings[page], page_size, ms_flags);
#if defined(FALLOC_FL_KEEP_SIZE) && defined(FALLOC_FL_PUNCH_HOLE)
							(void)shim_fallocate(fd, FALLOC_FL_PUNCH_HOLE | FALLOC_FL_KEEP_SIZE,
								offset, page_size);
#endif
						}
					}
					n--;
					break;
				}
				if (!keep_stressing_flag())
					goto cleanup;
			}
		}
#endif
cleanup:
		/*
		 *  Step #3, unmap them all
		 */
		for (n = 0; n < pages4k; n++) {
			if (mapped[n] & PAGE_MAPPED) {
				(void)stress_madvise_random(mappings[n], page_size);
				stress_mmap_mprotect(args->name, mappings[n],
					page_size, context->mmap_mprotect);
				(void)munmap((void *)mappings[n], page_size);
			}
		}
		inc_counter(args);
	} while (keep_stressing());

	free(mappings);
	free(mapped);

	return EXIT_SUCCESS;
}

/*
 *  stress_mmap()
 *	stress mmap
 */
static int stress_mmap(const stress_args_t *args)
{
	const size_t page_size = args->page_size;
	char filename[PATH_MAX];
	bool mmap_osync = false;
	bool mmap_odirect = false;
	int ret;
	stress_mmap_context_t context;

	context.fd = -1;
	context.mmap_bytes = DEFAULT_MMAP_BYTES;
	context.mmap_async = false;
	context.mmap_file = false;
	context.mmap_mprotect = false;
	context.flags = MAP_PRIVATE | MAP_ANONYMOUS;
#if defined(MAP_POPULATE)
	context.flags |= MAP_POPULATE;
#endif

	(void)stress_get_setting("mmap-async", &context.mmap_async);
	(void)stress_get_setting("mmap-file", &context.mmap_file);
	(void)stress_get_setting("mmap-mprotect", &context.mmap_mprotect);
	(void)stress_get_setting("mmap-osync", &mmap_osync);
	(void)stress_get_setting("mmap-odirect", &mmap_odirect);

	if (mmap_osync || mmap_odirect)
		context.mmap_file = true;

	if (!stress_get_setting("mmap-bytes", &context.mmap_bytes)) {
		if (g_opt_flags & OPT_FLAGS_MAXIMIZE)
			context.mmap_bytes = MAX_32;
		if (g_opt_flags & OPT_FLAGS_MINIMIZE)
			context.mmap_bytes = MIN_MMAP_BYTES;
	}
	context.mmap_bytes /= args->num_instances;
	if (context.mmap_bytes < MIN_MMAP_BYTES)
		context.mmap_bytes = MIN_MMAP_BYTES;
	if (context.mmap_bytes < page_size)
		context.mmap_bytes = page_size;
	context.sz = context.mmap_bytes & ~(page_size - 1);

	if (context.mmap_file) {
		int file_flags = O_CREAT | O_RDWR;
		ssize_t wr_ret, rc;

		rc = stress_temp_dir_mk_args(args);
		if (rc < 0)
			return exit_status(-rc);

		(void)stress_temp_filename_args(args,
			filename, sizeof(filename), stress_mwc32());

		if (mmap_odirect) {
#if defined(O_DIRECT)
			file_flags |= O_DIRECT;
#else
			pr_inf("%s: --mmap-odirect selected by not supported by this system\n",
				args->name);
#endif
		}
		if (mmap_osync) {
#if defined(O_SYNC)
			file_flags |= O_SYNC;
#else
			pr_inf("%s: --mmap-osync selected by not supported by this system\n",
				args->name);
#endif
		}

		context.fd = open(filename, file_flags, S_IRUSR | S_IWUSR);
		if (context.fd < 0) {
			rc = exit_status(errno);
			pr_fail_err("open");
			(void)unlink(filename);
			(void)stress_temp_dir_rm_args(args);

			return rc;
		}
		(void)unlink(filename);
		if (lseek(context.fd, context.sz - args->page_size, SEEK_SET) < 0) {
			pr_fail_err("lseek");
			(void)close(context.fd);
			(void)stress_temp_dir_rm_args(args);

			return EXIT_FAILURE;
		}
redo:
		/*
		 *  Write a page aligned chunk of data, we can
		 *  use g_shared as this is mmap'd and hence
		 *  page algned and always available for reading
		 */
		wr_ret = write(context.fd, g_shared, args->page_size);
		if (wr_ret != (ssize_t)args->page_size) {
			if ((errno == EAGAIN) || (errno == EINTR))
				goto redo;
			rc = exit_status(errno);
			pr_fail_err("write");
			(void)close(context.fd);
			(void)stress_temp_dir_rm_args(args);

			return rc;
		}
		context.flags &= ~(MAP_ANONYMOUS | MAP_PRIVATE);
		context.flags |= MAP_SHARED;
	}

	ret = stress_oomable_child(args, &context, stress_mmap_child, STRESS_OOMABLE_NORMAL);

	if (context.mmap_file) {
		(void)close(context.fd);
		(void)stress_temp_dir_rm_args(args);
	}
	return ret;
}

static const stress_opt_set_func_t opt_set_funcs[] = {
	{ OPT_mmap_async,	stress_set_mmap_async },
	{ OPT_mmap_bytes,	stress_set_mmap_bytes },
	{ OPT_mmap_file,	stress_set_mmap_file },
	{ OPT_mmap_mprotect,	stress_set_mmap_mprotect },
	{ OPT_mmap_osync,	stress_set_mmap_osync },
	{ OPT_mmap_odirect,	stress_set_mmap_odirect },
	{ 0,			NULL }
};

stressor_info_t stress_mmap_info = {
	.stressor = stress_mmap,
	.class = CLASS_VM | CLASS_OS,
	.opt_set_funcs = opt_set_funcs,
	.help = help
};
