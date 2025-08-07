/*
 * Copyright (C) 2017-2021 Canonical, Ltd.
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
#include "core-madvise.h"
#include "core-mincore.h"
#include "core-mmap.h"
#include "core-mounts.h"
#include "core-out-of-memory.h"

#if defined(HAVE_SYS_STATFS_H)
#include <sys/statfs.h>
#else
UNEXPECTED
#endif

#if defined(HAVE_SYS_XATTR_H)
#include <sys/xattr.h>
#undef HAVE_ATTR_XATTR_H
#elif defined(HAVE_ATTR_XATTR_H)
#include <attr/xattr.h>
#endif

/* mapping address and mmap'd state information */
typedef struct {
	uint8_t *addr;		/* mmap address */
	uint8_t	state;		/* mmap state, e.g. unmapped, mapped, etc */
#if defined(UINTPTR_MAX)
#if (UINTPTR_MAX == 0xFFFFFFFFUL)
	uint8_t pad[3];		/* make struct 8 bytes in size */
#else
	uint8_t pad[7];		/* make struct 16 bytes in size */
#endif
#endif
} mapping_info_t;

static const stress_help_t help[] = {
	{ NULL,	"tmpfs N",	    "start N workers mmap'ing a file on tmpfs" },
	{ NULL,	"tmpfs-mmap-async", "using asynchronous msyncs for tmpfs file based mmap" },
	{ NULL,	"tmpfs-mmap-file",  "mmap onto a tmpfs file using synchronous msyncs" },
	{ NULL,	"tmpfs-ops N",	    "stop after N tmpfs bogo ops" },
	{ NULL,	NULL,		    NULL }
};

static const stress_opt_t opts[] = {
	{ OPT_tmpfs_mmap_async,	"tmpfs-mmap-async", TYPE_ID_BOOL, 0, 1, NULL },
	{ OPT_tmpfs_mmap_file,  "tmpfs-mmap-file",  TYPE_ID_BOOL, 0, 1, NULL },
	END_OPT,
};

#if defined(HAVE_SYS_VFS_H) && \
    defined(HAVE_STATFS)

#define MAX_MOUNTS		(256)
#define NO_MEM_RETRIES_MAX	(256)
#if !defined(TMPFS_MAGIC)
#define TMPFS_MAGIC		(0x01021994)
#endif
#define MAX_TMPFS_SIZE		(512 * MB)

/* Misc randomly chosen mmap flags */
static const int mmap_flags[] = {
#if defined(MAP_HUGE_2MB) &&	\
    defined(MAP_HUGETLB)
	MAP_HUGE_2MB | MAP_HUGETLB,
#endif
#if defined(MAP_HUGE_1GB) &&	\
    defined(MAP_HUGETLB)
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
	off_t	sz;	/* size of mapping */
	int 	fd;	/* mmap file descriptor */
} stress_tmpfs_context_t;

/*
 *  stress_tmpfs_open()
 *	attempts to find a writeable tmpfs file system and open
 *	a tmpfs temp file. The file is unlinked so the final close
 *	will enforce and automatic space reap if the child process
 *	exits prematurely.
 */
static int stress_tmpfs_open(stress_args_t *args, off_t *len)
{
	const uint32_t rnd = stress_mwc32();
	char path[PATH_MAX];
	char *mnts[MAX_MOUNTS];
	int i, n, fd = -1;

	(void)shim_memset(mnts, 0, sizeof(mnts));

	*len = 0;
	n = stress_mount_get(mnts, SIZEOF_ARRAY(mnts));
	if (UNLIKELY(n < 0))
		return -1;

	for (i = 0; i < n; i++) {
		struct statfs buf;

		if (UNLIKELY(!mnts[i]))
			continue;
		/* Some paths should be avoided... */
		if (!strncmp(mnts[i], "/dev", 4))
			continue;
		if (!strncmp(mnts[i], "/sys", 4))
			continue;
		if (!strncmp(mnts[i], "/run/lock", 9))
			continue;
		(void)shim_memset(&buf, 0, sizeof(buf));
		if (statfs(mnts[i], &buf) < 0)
			continue;

		/* ..and must be TMPFS too.. */
		if (buf.f_type != TMPFS_MAGIC)
			continue;

		/* We have a candidate, try to create a tmpfs file */
		(void)snprintf(path, sizeof(path), "%s/%s-%" PRIdMAX "-%" PRIu32 "-%" PRIu32,
			mnts[i], args->name, (intmax_t)args->pid, args->instance, rnd);
		fd = open(path, O_RDWR | O_CREAT | O_TRUNC, S_IRUSR | S_IWUSR);
		if (LIKELY(fd >= 0)) {
			const char data = 0;
			off_t rc, max_size = (off_t)buf.f_bsize * (off_t)buf.f_bavail;

			/*
			 * Don't use all the tmpfs, just 98% for all instance
			 */
			max_size = (max_size * 98) / 100;
			if (!(g_opt_flags & OPT_FLAGS_MAXIMIZE)) {
				if (max_size > (off_t)MAX_TMPFS_SIZE)
					max_size = (off_t)MAX_TMPFS_SIZE;
			}
			max_size /= args->instances;
			max_size += (args->page_size - 1);
			max_size &= ~(off_t)(args->page_size - 1);

			(void)shim_unlink(path);
			/*
			 *  make file with hole; we want this
			 *  to be autopopulated with pages
			 *  over time
			 */
			rc = lseek(fd, max_size, SEEK_SET);
			if (UNLIKELY(rc < 0)) {
				(void)close(fd);
				fd = -1;
				continue;
			}
			rc = write(fd, &data, sizeof(data));
			if (UNLIKELY(rc < 0)) {
				(void)close(fd);
				fd = -1;
				continue;
			}
			*len = max_size;
			break;
		}
	}
	stress_mount_free(mnts, n);

	return fd;
}

static int stress_tmpfs_child(stress_args_t *args, void *ctxt)
{
	const stress_tmpfs_context_t *context = (stress_tmpfs_context_t *)ctxt;
	const size_t page_size = args->page_size;
	const size_t sz = (size_t)context->sz;
	const size_t pages = (size_t)sz / page_size;
	const int fd = context->fd;
	bool tmpfs_mmap_async = false;
	bool tmpfs_mmap_file = false;
	int no_mem_retries = 0;
	int ms_flags;
	int flags = MAP_SHARED;
	int rc = EXIT_SUCCESS;
	mapping_info_t *mappings;

#if defined(MAP_POPULATE)
	flags |= MAP_POPULATE;
#endif

	mappings = (mapping_info_t *)calloc(pages, sizeof(*mappings));
	if (UNLIKELY(!mappings)) {
		pr_inf_skip("%s: failed to allocate %zu byte mapping array%s, skipping stressor\n",
			args->name, pages * sizeof(*mappings),
			stress_get_memfree_str());
		return EXIT_NO_RESOURCE;
	}

	(void)stress_get_setting("tmpfs-mmap-async", &tmpfs_mmap_async);
	(void)stress_get_setting("tmpfs-mmap-file", &tmpfs_mmap_file);

	ms_flags = tmpfs_mmap_async ? MS_ASYNC : MS_SYNC;

	do {
		size_t n;
		const int rnd = stress_mwc32modn(SIZEOF_ARRAY(mmap_flags));
		const int rnd_flag = mmap_flags[rnd];
		uint8_t *buf = NULL;
		off_t offset;

		if (UNLIKELY(no_mem_retries >= NO_MEM_RETRIES_MAX)) {
			pr_err("%s: gave up trying to mmap, no available memory\n",
				args->name);
			break;
		}

		/*
		 *  exercise some random file operations
		 */
		offset = (off_t)stress_mwc64modn(sz + 1);
		if (LIKELY(lseek(fd, offset, SEEK_SET) >= 0)) {
			char data[1];

			VOID_RET(ssize_t, read(fd, data, sizeof(data)));
		}
		if (UNLIKELY(!stress_continue_flag()))
			break;

#if (defined(HAVE_SYS_XATTR_H) ||       \
     defined(HAVE_ATTR_XATTR_H)) &&     \
    defined(HAVE_FREMOVEXATTR) &&       \
    defined(HAVE_FSETXATTR)
		{
			int ret;
			char attrname[32];
			char attrdata[32];

			(void)snprintf(attrname, sizeof(attrname), "user.var_%" PRIx32, stress_mwc32());
			(void)snprintf(attrdata, sizeof(attrdata), "data-%" PRIx32, stress_mwc32());

			/* Not supported, but exercise it anyhow */
			ret = shim_fsetxattr(fd, attrname, attrdata, strlen(attrdata), XATTR_CREATE);
			if (ret == 0)
				VOID_RET(int, shim_fremovexattr(fd, attrname));
		}
#endif
		offset = (off_t)stress_mwc64modn(sz + 1);
		if (LIKELY(lseek(fd, offset, SEEK_SET) >= 0)) {
			char data[1];
			ssize_t wr;

			data[0] = (char)0xff;
			wr = write(fd, data, sizeof(data));
			(void)wr;
		}
		(void)shim_fsync(fd);

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
			(void)shim_memset(buf, 0xff, sz);
			(void)shim_msync((void *)buf, sz, ms_flags);
		}
		(void)stress_madvise_randomize(buf, sz);
		(void)stress_mincore_touch_pages(buf, sz);
		for (n = 0; n < pages; n++) {
			mappings[n].state = PAGE_MAPPED;
			mappings[n].addr = buf + (n * page_size);
		}

		/* Ensure we can write to the mapped pages */
		stress_mmap_set(buf, sz, page_size);
		if (g_opt_flags & OPT_FLAGS_VERIFY) {
			if (UNLIKELY(stress_mmap_check(buf, sz, page_size) < 0)) {
				pr_fail("%s: mmap'd region of %zu bytes does "
					"not contain expected data\n", args->name, sz);
				rc = EXIT_FAILURE;
				break;
			}
		}

		/*
		 *  Step #1, unmap all pages in random order
		 */
		(void)stress_mincore_touch_pages(buf, sz);
		for (n = pages; n; ) {
			uint64_t j;
			const uint64_t i = stress_mwc64modn(pages);

			for (j = 0; j < n; j++) {
				const uint64_t page = (i + j) % pages;

				if (mappings[page].state == PAGE_MAPPED) {
					mappings[page].state = 0;
					(void)stress_madvise_randomize(mappings[page].addr, page_size);
					(void)stress_munmap_force((void *)mappings[page].addr, page_size);
					n--;
					break;
				}
				if (UNLIKELY(!stress_continue_flag()))
					goto cleanup;
			}
		}
		(void)stress_munmap_force((void *)buf, sz);
#if defined(MAP_FIXED)
		/*
		 *  Step #2, map them back in random order
		 */
		for (n = pages; n; ) {
			uint64_t j;
			const uint64_t i = stress_mwc64modn(pages);

			for (j = 0; j < n; j++) {
				const uint64_t page = (i + j) % pages;

				if (!mappings[page].state) {
					offset = tmpfs_mmap_file ? (off_t)(page * page_size) : 0;
					/*
					 * Attempt to map them back into the original address, this
					 * may fail (it's not the most portable operation), so keep
					 * track of failed mappings too
					 */
					mappings[page].addr = (uint8_t *)mmap((void *)mappings[page].addr,
						page_size, PROT_READ | PROT_WRITE, MAP_FIXED | flags, fd, offset);
					if (mappings[page].addr == MAP_FAILED) {
						mappings[page].state = PAGE_MAPPED_FAIL;
						mappings[page].addr = NULL;
					} else {
						(void)stress_mincore_touch_pages(mappings[page].addr, page_size);
						(void)stress_madvise_randomize(mappings[page].addr, page_size);
						mappings[page].state = PAGE_MAPPED;
						/* Ensure we can write to the mapped page */
						stress_mmap_set(mappings[page].addr, page_size, page_size);
						if (UNLIKELY(stress_mmap_check(mappings[page].addr, page_size, page_size) < 0)) {
							pr_fail("%s: mmap'd region of %zu bytes does "
								"not contain expected data\n", args->name, page_size);
							rc = EXIT_FAILURE;
							break;
						}
						if (tmpfs_mmap_file) {
							(void)shim_memset(mappings[page].addr, (int)n, page_size);
							(void)shim_msync((void *)mappings[page].addr, page_size, ms_flags);
						}
					}
					n--;
					break;
				}
				if (UNLIKELY(!stress_continue_flag()))
					goto cleanup;
			}
		}
#endif
cleanup:
		/*
		 *  Step #3, unmap them all
		 */
		for (n = 0; n < pages; n++) {
			if (mappings[n].state & PAGE_MAPPED) {
				(void)stress_madvise_randomize(mappings[n].addr, page_size);
				(void)stress_munmap_force((void *)mappings[n].addr, page_size);
			}
		}
		stress_bogo_inc(args);
	} while (stress_continue(args));

	(void)close(fd);
	free(mappings);

	return rc;
}

/*
 *  stress_tmpfs()
 *	stress tmpfs
 */
static int stress_tmpfs(stress_args_t *args)
{
	stress_tmpfs_context_t context;
	int ret;

	context.fd = stress_tmpfs_open(args, &context.sz);
	if (context.fd < 0) {
		pr_err("%s: cannot find writeable free space on a "
			"tmpfs filesystem\n", args->name);
		return EXIT_NO_RESOURCE;
	}

	stress_set_proc_state(args->name, STRESS_STATE_SYNC_WAIT);
	stress_sync_start_wait(args);
	stress_set_proc_state(args->name, STRESS_STATE_RUN);

	ret = stress_oomable_child(args, &context, stress_tmpfs_child, STRESS_OOMABLE_NORMAL);

	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);

	(void)close(context.fd);

	return ret;
}
const stressor_info_t stress_tmpfs_info = {
	.stressor = stress_tmpfs,
	.classifier = CLASS_MEMORY | CLASS_VM | CLASS_OS,
	.opts = opts,
	.verify = VERIFY_OPTIONAL,
	.help = help
};
#else
const stressor_info_t stress_tmpfs_info = {
	.stressor = stress_unimplemented,
	.classifier = CLASS_MEMORY | CLASS_VM | CLASS_OS,
	.opts = opts,
	.verify = VERIFY_OPTIONAL,
	.help = help,
	.unimplemented_reason = "built without sys/vfs.h or statfs() system call"
};
#endif
