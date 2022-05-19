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

#if defined(__NR_mmap2)
#define HAVE_MMAP2
#endif

#define MIN_MMAP_BYTES		(4 * KB)
#define MAX_MMAP_BYTES		(MAX_MEM_LIMIT)
#define DEFAULT_MMAP_BYTES	(256 * MB)

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

typedef void * (*mmap_func_t)(void *addr, size_t length, int prot, int flags, int fd, off_t offset);

typedef struct {
	int fd;
	int flags;
	size_t sz;
	size_t mmap_bytes;
	bool mmap_mprotect;
	bool mmap_file;
	bool mmap_async;
	mmap_func_t mmap;
	size_t mmap_prot_count;
	int *mmap_prot_perms;
	size_t mmap_flag_count;
	int *mmap_flag_perms;
} stress_mmap_context_t;

#define NO_MEM_RETRIES_MAX	(65536)

static const int mmap_prot[] = {
#if defined(PROT_NONE)
	PROT_NONE,
#endif
#if defined(PROT_EXEC)
	PROT_EXEC,
#endif
#if defined(PROT_READ)
	PROT_READ,
#endif
#if defined(PROT_WRITE)
	PROT_WRITE,
#endif
};

static const int mmap_std_flags[] = {
#if defined(MAP_ANONYMOUS)
	MAP_ANONYMOUS,
#endif
#if defined(MAP_SHARED)
	MAP_SHARED,
#endif
#if defined(MAP_SHARED_VALIDATE)
	MAP_SHARED_VALIDATE,
#endif
#if defined(MAP_PRIVATE)
	MAP_PRIVATE,
#endif
};

/* Misc randomly chosen mmap flags */
static const int mmap_flags[] = {
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
/* NetBSD */
#if defined(MAP_WIRED)
	MAP_WIRED,
#endif
/* NetBSD */
#if defined(MAP_ALIGNMENT_64KB)
	MAP_ALIGNMENT_64KB,
#endif
/* NetBSD */
#if defined(MAP_ALIGNMENT_16MB)
	MAP_ALIGNMENT_16MB,
#endif
/* OpenBSD */
#if defined(MAP_CONCEAL)
	MAP_CONCEAL,
#endif
/* OpenBSD */
#if defined(MAP_COPY)
	MAP_COPY,
#endif
/* Solaris */
#if defined(MAP_LOW32)
	MAP_LOW32,
#endif
	0,
};

/*
 *   mmap2_try()
 *	If mmap2 is requested then try to use it for 4K page aligned
 *	offsets. Fall back to mmap() if not possible.
 */
#if defined(HAVE_MMAP2)
static void *mmap2_try(void *addr, size_t length, int prot, int flags,
	int fd, off_t offset)
{
	void *ptr;
	off_t pgoffset;

	/* Non 4K-page aligned offsets need to use mmap() */
	if (offset & 4095)
		return mmap(addr, length, prot, flags, fd, offset);
	pgoffset = offset >> 12;
	ptr = (void *)syscall(__NR_mmap2, addr, length, prot, flags, fd, pgoffset);
	if (ptr == MAP_FAILED) {
		/* For specific failure cases retry with mmap() */
		if ((errno == ENOSYS) || (errno == EINVAL))
			ptr = mmap(addr, length, prot, flags, fd, offset);
	}
	return ptr;
}
#endif

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

static int stress_set_mmap_mmap2(const char *opt)
{
	bool mmap_mmap2 = true;

	(void)opt;
	return stress_set_setting("mmap-mmap2", TYPE_ID_BOOL, &mmap_mmap2);
}

/*
 *  stress_mmap_mprotect()
 *	cycle through page settings on a region of mmap'd memory
 */
static void stress_mmap_mprotect(
	const char *name,
	void *addr,
	const size_t len,
	const size_t page_size,
	const bool mmap_mprotect)
{
#if defined(HAVE_MPROTECT)
	if (mmap_mprotect) {
		int ret;
		void *last_page = (void *)(~(uintptr_t)0 & ~(page_size - 1));

		/* Invalid mix of PROT_GROWSDOWN | PROT_GROWSUP */
#if defined(PROT_GROWSDOWN) &&	\
    defined(PROT_GROWSUP)
		ret = mprotect(addr, len, PROT_READ | PROT_WRITE | PROT_GROWSDOWN | PROT_GROWSUP);
		(void)ret;
#endif

		/* Invalid non-page aligned start address */
		ret = mprotect((void *)(((uint8_t *)addr) + 7), len, PROT_READ | PROT_WRITE);
		(void)ret;

		/* Exercise zero len (should succeed) */
		ret = mprotect(addr, 0, PROT_READ | PROT_WRITE);
		(void)ret;

		/* Exercise flags all set */
		ret = mprotect(addr, len, ~0);
		(void)ret;

		/* Exercise invalid unmapped addressed, should return ENOMEM */
		ret = mprotect(last_page, page_size, PROT_READ | PROT_WRITE);
		(void)ret;

		/* Exercise invalid wrapped range, should return EINVAL */
		ret = mprotect(last_page, page_size << 1, PROT_READ | PROT_WRITE);
		(void)ret;

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
	(void)page_size;
	(void)mmap_mprotect;
#endif
}

/*
 *  stress_mmap_invalid()
 *	exercise invalid mmap mapping, munmap allocation if
 *	it succeeds (which it is not expected to do).
 */
static void stress_mmap_invalid(
	void *addr,
	size_t length,
	int prot,
	int flags,
	int fd,
	off_t offset)
{
	void *ptr;

	ptr = mmap(addr, length, prot, flags, fd, offset);
	if (ptr != MAP_FAILED)
		(void)munmap(ptr, length);

#if defined(__NR_mmap)
	/*
	 *  libc may detect offset is invalid and not do the syscall so
	 *  do direct syscall if possible
	 */
	ptr = (void *)syscall(__NR_mmap, addr, length, prot, flags, fd, offset + 1);
	if (ptr != MAP_FAILED)
		(void)munmap(ptr, length);
#endif
	/* Do the above via libc */
	ptr = mmap(addr, length, prot, flags, fd, offset + 1);
	if (ptr != MAP_FAILED)
		(void)munmap(ptr, length);
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
	const int bad_fd = stress_get_bad_fd();
	const int ms_flags = context->mmap_async ? MS_ASYNC : MS_SYNC;
	uint8_t *mapped, **mappings;
	void *hint;

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
		const int rnd = stress_mwc32() % SIZEOF_ARRAY(mmap_flags); /* cppcheck-suppress moduloofone */
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
		/*
		 *  ARM64, one can opt-int to getting VAs from 52 bit
		 *  space by hinting with an address that is > 48 bits.
		 *  Since this is a hint, we can try this for all
		 *  architectures.
		 */
		hint = stress_mwc1() ? NULL : (void *)~(uintptr_t)0;
		buf = (uint8_t *)context->mmap(hint, sz,
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
		stress_mmap_mprotect(args->name, buf, sz, page_size, context->mmap_mprotect);
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
#if defined(HAVE_MQUERY) &&	\
    defined(MAP_FIXED)
					{
						/* Exercise OpenBSD mquery */
						void *query;

						query = mquery(mappings[page], page_size,
								PROT_READ, MAP_FIXED, -1, 0);
						(void)query;
					}
#endif
					(void)stress_madvise_random(mappings[page], page_size);
					stress_mmap_mprotect(args->name, mappings[page],
						page_size, page_size, context->mmap_mprotect);
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
					off_t offset = mmap_file ? (off_t)(page * page_size) : 0;
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
					mappings[page] = (uint8_t *)context->mmap((void *)mappings[page],
						page_size, PROT_READ | PROT_WRITE, fixed_flags | context->flags, fd, offset);

					if (mappings[page] == MAP_FAILED) {
						mapped[page] = PAGE_MAPPED_FAIL;
						mappings[page] = NULL;
					} else {
						(void)stress_mincore_touch_pages(mappings[page], page_size);
						(void)stress_madvise_random(mappings[page], page_size);
						stress_mmap_mprotect(args->name, mappings[page],
							page_size, page_size, context->mmap_mprotect);
						mapped[page] = PAGE_MAPPED;
						/* Ensure we can write to the mapped page */
						stress_mmap_set(mappings[page], page_size, page_size);
						if (stress_mmap_check(mappings[page], page_size, page_size) < 0)
							pr_fail("%s: mmap'd region of %zu bytes does "
								"not contain expected data\n", args->name, page_size);
						if (mmap_file) {
							(void)memset(mappings[page], (int)n, page_size);
							(void)shim_msync((void *)mappings[page], page_size, ms_flags);
#if defined(FALLOC_FL_KEEP_SIZE) &&	\
    defined(FALLOC_FL_PUNCH_HOLE)
							(void)shim_fallocate(fd, FALLOC_FL_PUNCH_HOLE | FALLOC_FL_KEEP_SIZE,
								offset, (off_t)page_size);
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
					page_size, page_size, context->mmap_mprotect);
				(void)munmap((void *)mappings[n], page_size);
			}
		}

		/*
		 *  Step #4, invalid unmapping on the first found page that
		 *  was successfully mapped earlier. This page should be now
		 *  unmapped so unmap it again in various ways
		 */
		for (n = 0; n < pages4k; n++) {
			if (mapped[n] & PAGE_MAPPED) {
				(void)munmap((void *)mappings[n], 0);
				(void)munmap((void *)mappings[n], page_size);
				break;
			}
		}

		/*
		 *  Step #5, invalid mappings
		 */
		stress_mmap_invalid(NULL, 0, PROT_READ | PROT_WRITE,
				MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
		stress_mmap_invalid((void *)(~(uintptr_t)0), 0, PROT_READ | PROT_WRITE,
				MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
		stress_mmap_invalid(NULL, ~(size_t)0, PROT_READ | PROT_WRITE,
				MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
		stress_mmap_invalid((void *)(~(uintptr_t)0), ~(size_t)0, PROT_READ | PROT_WRITE,
				MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
		stress_mmap_invalid(NULL, args->page_size, PROT_READ | PROT_WRITE,
				MAP_PRIVATE, bad_fd, 0);
		stress_mmap_invalid(NULL, args->page_size, PROT_READ | PROT_WRITE,
				MAP_PRIVATE, bad_fd, 0);
		if (fd >= 0)
			stress_mmap_invalid(NULL, args->page_size << 2, PROT_READ | PROT_WRITE,
					MAP_PRIVATE, fd,
					(((~(size_t)0) & ~(args->page_size - 1)) - args->page_size));

		/*
		 *  Step #6, invalid unmappings
		 */
		(void)munmap(NULL, 0);
		(void)munmap(NULL, ~(size_t)0);

		/*
		 *  Step #7, random choice from any of the valid/invalid
		 *  mmap flag permutations
		 */
		if ((context->mmap_prot_perms) && (context->mmap_prot_count > 0)) {
			const size_t rnd = stress_mwc16() % context->mmap_prot_count;
			const int rnd_prot = context->mmap_prot_perms[rnd];

			buf = mmap(NULL, sz, rnd_prot, MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
			if (buf != MAP_FAILED)
				(void)munmap((void *)buf, sz);
		}

		/*
		 *  Step #8, work through all flag permutations
		 */
		if ((context->mmap_flag_perms) && (context->mmap_flag_count > 0)) {
			static int index;
			const int flag = context->mmap_flag_perms[index];
			int tmpfd;

			if (flag & MAP_ANONYMOUS)
				tmpfd = -1;
			else
				tmpfd = open("/dev/zero", O_RDONLY);

			buf = mmap(NULL, page_size, PROT_READ, flag, tmpfd, 0);
			if (buf != MAP_FAILED)
				(void)munmap((void *)buf, page_size);
			if (tmpfd >= 0)
				(void)close(tmpfd);
			index++;
			index %= context->mmap_flag_count;
		}
		inc_counter(args);
	} while (keep_stressing(args));

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
	bool mmap_mmap2 = false;
	int ret, all_flags;
	stress_mmap_context_t context;
	size_t i;

	context.fd = -1;
	context.mmap = (mmap_func_t)mmap;
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
	(void)stress_get_setting("mmap-mmap2", &mmap_mmap2);

	for (all_flags = 0, i = 0; i < SIZEOF_ARRAY(mmap_prot); i++)
		all_flags |= mmap_prot[i];
	context.mmap_prot_count = stress_flag_permutation(all_flags, &context.mmap_prot_perms);

	for (all_flags = 0, i = 0; i < SIZEOF_ARRAY(mmap_std_flags); i++)
		all_flags |= mmap_std_flags[i];
	for (i = 0; i < SIZEOF_ARRAY(mmap_flags); i++)
		all_flags |= mmap_flags[i];
	context.mmap_flag_count = stress_flag_permutation(all_flags, &context.mmap_flag_perms);

	if (mmap_osync || mmap_odirect)
		context.mmap_file = true;

	if (mmap_mmap2) {
#if defined(HAVE_MMAP2)
		context.mmap = (mmap_func_t)mmap2_try;
#else
		pr_inf("%s: using mmap instead of mmap2 as it is not available\n",
			args->name);
#endif
	}

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
			return exit_status((int)-rc);

		(void)stress_temp_filename_args(args,
			filename, sizeof(filename), stress_mwc32());

		if (mmap_odirect) {
#if defined(O_DIRECT)
			file_flags |= O_DIRECT;
#else
			pr_inf("%s: --mmap-odirect selected by not supported by this system\n",
				args->name);
			UNEXPECTED
#endif
		}
		if (mmap_osync) {
#if defined(O_SYNC)
			file_flags |= O_SYNC;
#else
			pr_inf("%s: --mmap-osync selected by not supported by this system\n",
				args->name);
			UNEXPECTED
#endif
		}

		context.fd = open(filename, file_flags, S_IRUSR | S_IWUSR);
		if (context.fd < 0) {
			rc = exit_status(errno);
			pr_fail("%s: open %s failed, errno=%d (%s)\n",
				args->name, filename, errno, strerror(errno));
			(void)shim_unlink(filename);
			(void)stress_temp_dir_rm_args(args);

			return (int)rc;
		}
		(void)shim_unlink(filename);
		if (lseek(context.fd, (off_t)(context.sz - args->page_size), SEEK_SET) < 0) {
			pr_fail("%s: lseek failed, errno=%d (%s)\n",
				args->name, errno, strerror(errno));
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
			pr_fail("%s: write failed, errno=%d (%s)\n",
				args->name, errno, strerror(errno));
			(void)close(context.fd);
			(void)stress_temp_dir_rm_args(args);

			return (int)rc;
		}
		context.flags &= ~(MAP_ANONYMOUS | MAP_PRIVATE);
		context.flags |= MAP_SHARED;
	}

	stress_set_proc_state(args->name, STRESS_STATE_RUN);

	ret = stress_oomable_child(args, &context, stress_mmap_child, STRESS_OOMABLE_NORMAL);

	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);

	if (context.mmap_file) {
		(void)close(context.fd);
		(void)stress_temp_dir_rm_args(args);
	}
	if (context.mmap_prot_perms)
		free(context.mmap_prot_perms);
	if (context.mmap_flag_perms)
		free(context.mmap_flag_perms);

	return ret;
}

static const stress_opt_set_func_t opt_set_funcs[] = {
	{ OPT_mmap_async,	stress_set_mmap_async },
	{ OPT_mmap_bytes,	stress_set_mmap_bytes },
	{ OPT_mmap_file,	stress_set_mmap_file },
	{ OPT_mmap_mprotect,	stress_set_mmap_mprotect },
	{ OPT_mmap_osync,	stress_set_mmap_osync },
	{ OPT_mmap_odirect,	stress_set_mmap_odirect },
	{ OPT_mmap_mmap2,	stress_set_mmap_mmap2 },
	{ 0,			NULL }
};

stressor_info_t stress_mmap_info = {
	.stressor = stress_mmap,
	.class = CLASS_VM | CLASS_OS,
	.opt_set_funcs = opt_set_funcs,
	.verify = VERIFY_OPTIONAL,
	.help = help
};
