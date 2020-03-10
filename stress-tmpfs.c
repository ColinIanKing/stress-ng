/*
 * Copyright (C) 2017-2020 Canonical, Ltd.
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
	{ NULL,	"tmpfs N",	    "start N workers mmap'ing a file on tmpfs" },
	{ NULL,	"tmpfs-ops N",	    "stop after N tmpfs bogo ops" },
	{ NULL,	"tmpfs-mmap-async", "using asynchronous msyncs for tmpfs file based mmap" },
	{ NULL,	"tmpfs-mmap-file",  "mmap onto a tmpfs file using synchronous msyncs" },
	{ NULL,	NULL,		    NULL }
};

static int stress_set_tmpfs_mmap_file(const char *opt)
{
	bool tmpfs_mmap_file = true;

	(void)opt;
	return stress_set_setting("tmpfs-mmap-file", TYPE_ID_BOOL, &tmpfs_mmap_file);
}

static int stress_set_tmpfs_mmap_async(const char *opt)
{
	bool tmpfs_mmap_async = true;

	(void)opt;
	return stress_set_setting("tmpfs-mmap-async", TYPE_ID_BOOL, &tmpfs_mmap_async);
}

static const stress_opt_set_func_t opt_set_funcs[] = {
	{ OPT_tmpfs_mmap_async,	stress_set_tmpfs_mmap_async },
	{ OPT_tmpfs_mmap_file,	stress_set_tmpfs_mmap_file },
	{ 0,			NULL }
};

#if defined(HAVE_SYS_VFS_H) && \
    defined(HAVE_STATFS)

#define MAX_MOUNTS		(256)
#define NO_MEM_RETRIES_MAX	(256)
#define TMPFS_MAGIC		(0x01021994)

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
#if defined(MAP_LOCKED)
	MAP_LOCKED,
#endif
	0
};

typedef struct {
	int 	fd;
	off_t	sz;
} stress_tmpfs_context_t;

/*
 *  stress_tmpfs_open()
 *	attempts to find a writeable tmpfs file system and opens
 *	a tmpfs temp file. The file is unlinked so the final close
 *	will enforce and automatic space reap if the child process
 *	exits prematurely.
 */
static int stress_tmpfs_open(const stress_args_t *args, off_t *len)
{
	const uint32_t rnd = stress_mwc32();
	char path[PATH_MAX];
	char *mnts[MAX_MOUNTS];
	int i, n, fd = -1;

	(void)memset(mnts, 0, sizeof(mnts));

	*len = 0;
	n = stress_mount_get(mnts, SIZEOF_ARRAY(mnts));
	if (n < 0)
		return -1;

	for (i = 0; i < n; i++) {
		struct statfs buf;

		if (!mnts[i])
			continue;
		/* Some paths should be avoided... */
		if (!strncmp(mnts[i], "/dev", 4))
			continue;
		if (!strncmp(mnts[i], "/sys", 4))
			continue;
		if (!strncmp(mnts[i], "/run/lock", 9))
			continue;
		if (statfs(mnts[i], &buf) < 0)
			continue;

		/* ..and must be TMPFS too.. */
		if (buf.f_type != TMPFS_MAGIC)
			continue;

		/* We have a candidate, try to create a tmpfs file */
		(void)snprintf(path, sizeof(path), "%s/%s-%d-%" PRIu32 "-%" PRIu32,
			mnts[i], args->name, args->pid, args->instance, rnd);
		fd = open(path, O_RDWR | O_CREAT | O_TRUNC, S_IRUSR | S_IWUSR);
		if (fd >= 0) {
			int rc;
			const char data = 0;
			off_t max_size = buf.f_bsize * buf.f_bavail;

			/*
			 * Don't use all the tmpfs, just 98% for all instance
			 */
			max_size = (max_size * 98) / 100;
			max_size /= args->num_instances;

			(void)unlink(path);
			/*
			 *  make file with hole; we want this
			 *  to be autopopulated with pages
			 *  over time
			 */
			rc = lseek(fd, max_size, SEEK_SET);
			if (rc < 0) {
				(void)close(fd);
				continue;
			}
			rc = write(fd, &data, sizeof(data));
			if (rc < 0) {
				(void)close(fd);
				continue;
			}
			*len = max_size;
			break;
		}
	}
	stress_mount_free(mnts, n);

	return fd;
}

static int stress_tmpfs_child(const stress_args_t *args, void *ctxt)
{
	stress_tmpfs_context_t *context = (stress_tmpfs_context_t *)ctxt;
	const size_t page_size = args->page_size;
	const size_t sz = context->sz;
	const size_t pages4k = (size_t)sz / page_size;
	const int fd = context->fd;
	bool tmpfs_mmap_async = false;
	bool tmpfs_mmap_file = false;
	int no_mem_retries = 0;
	int ms_flags;
	int flags = MAP_SHARED;

#if defined(MAP_POPULATE)
	flags |= MAP_POPULATE;
#endif

	(void)stress_get_setting("tmpfs-mmap-async", &tmpfs_mmap_async);
	(void)stress_get_setting("tmpfs-mmap-file", &tmpfs_mmap_file);

	ms_flags = tmpfs_mmap_async ? MS_ASYNC : MS_SYNC;

	do {
		uint8_t mapped[pages4k];
		uint8_t *mappings[pages4k];
		size_t n;
		const int rnd = stress_mwc32() % SIZEOF_ARRAY(mmap_flags);
		const int rnd_flag = mmap_flags[rnd];
		uint8_t *buf = NULL;

		if (no_mem_retries >= NO_MEM_RETRIES_MAX) {
			pr_err("%s: gave up trying to mmap, no available memory\n",
				args->name);
			break;
		}

		if (!keep_stressing_flag())
			break;
		buf = (uint8_t *)mmap(NULL, sz,
			PROT_READ | PROT_WRITE, flags | rnd_flag, fd, 0);
		if (buf == MAP_FAILED) {
#if defined(MAP_POPULATE)
			/* Force MAP_POPULATE off, just in case */
			if (flags & MAP_POPULATE) {
				flags &= ~MAP_POPULATE;
				no_mem_retries++;
				continue;
			}
#endif
#if defined(MAP_HUGETLB)
			/* Force MAP_HUGETLB off, just in case */
			if (flags & MAP_HUGETLB) {
				flags &= ~MAP_HUGETLB;
				no_mem_retries++;
				continue;
			}
#endif
			no_mem_retries++;
			if (no_mem_retries > 1)
				(void)shim_usleep(10000);
			continue;	/* Try again */
		}
		if (tmpfs_mmap_file) {
			(void)memset(buf, 0xff, sz);
			(void)shim_msync((void *)buf, sz, ms_flags);
		}
		(void)stress_madvise_random(buf, sz);
		(void)stress_mincore_touch_pages(buf, sz);
		(void)memset(mapped, PAGE_MAPPED, sizeof(mapped));
		for (n = 0; n < pages4k; n++)
			mappings[n] = buf + (n * page_size);

		/* Ensure we can write to the mapped pages */
		stress_mmap_set(buf, sz, page_size);
		if (g_opt_flags & OPT_FLAGS_VERIFY) {
			if (stress_mmap_check(buf, sz, page_size) < 0)
				pr_fail("%s: mmap'd region of %zu bytes does "
					"not contain expected data\n", args->name, sz);
		}

		/*
		 *  Step #1, unmap all pages in random order
		 */
		(void)stress_mincore_touch_pages(buf, sz);
		for (n = pages4k; n; ) {
			uint64_t j, i = stress_mwc64() % pages4k;

			for (j = 0; j < n; j++) {
				uint64_t page = (i + j) % pages4k;

				if (mapped[page] == PAGE_MAPPED) {
					mapped[page] = 0;
					(void)stress_madvise_random(mappings[page], page_size);
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
					off_t offset = tmpfs_mmap_file ? page * page_size : 0;
					/*
					 * Attempt to map them back into the original address, this
					 * may fail (it's not the most portable operation), so keep
					 * track of failed mappings too
					 */
					mappings[page] = (uint8_t *)mmap((void *)mappings[page],
						page_size, PROT_READ | PROT_WRITE, MAP_FIXED | flags, fd, offset);
					if (mappings[page] == MAP_FAILED) {
						mapped[page] = PAGE_MAPPED_FAIL;
						mappings[page] = NULL;
					} else {
						(void)stress_mincore_touch_pages(mappings[page], page_size);
						(void)stress_madvise_random(mappings[page], page_size);
						mapped[page] = PAGE_MAPPED;
						/* Ensure we can write to the mapped page */
						stress_mmap_set(mappings[page], page_size, page_size);
						if (stress_mmap_check(mappings[page], page_size, page_size) < 0)
							pr_fail("%s: mmap'd region of %zu bytes does "
								"not contain expected data\n", args->name, page_size);
						if (tmpfs_mmap_file) {
							(void)memset(mappings[page], n, page_size);
							(void)shim_msync((void *)mappings[page], page_size, ms_flags);
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
				(void)munmap((void *)mappings[n], page_size);
			}
		}
		inc_counter(args);
	} while (keep_stressing());

	(void)close(fd);

	return EXIT_SUCCESS;
}

/*
 *  stress_tmpfs()
 *	stress tmpfs
 */
static int stress_tmpfs(const stress_args_t *args)
{
	stress_tmpfs_context_t context;
	int ret;

	context.fd = stress_tmpfs_open(args, &context.sz);
	if (context.fd < 0) {
		pr_err("%s: cannot find writeable free space on a "
			"tmpfs filesystem\n", args->name);
		return EXIT_NO_RESOURCE;
	}

	ret = stress_oomable_child(args, &context, stress_tmpfs_child, STRESS_OOMABLE_NORMAL);

	(void)close(context.fd);

	return ret;
}
stressor_info_t stress_tmpfs_info = {
	.stressor = stress_tmpfs,
	.class = CLASS_MEMORY | CLASS_VM | CLASS_OS,
	.opt_set_funcs = opt_set_funcs,
	.help = help
};
#else
stressor_info_t stress_tmpfs_info = {
	.stressor = stress_not_implemented,
	.class = CLASS_MEMORY | CLASS_VM | CLASS_OS,
	.opt_set_funcs = opt_set_funcs,
	.help = help
};
#endif
