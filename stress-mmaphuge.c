/*
 * Copyright (C) 2013-2021 Canonical, Ltd.
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
#include "core-mmap.h"
#include "core-numa.h"
#include "core-out-of-memory.h"

#define MIN_MMAPHUGE_MMAPS	(1)
#define MAX_MMAPHUGE_MMAPS	(65536)

static const stress_help_t help[] = {
	{ NULL,	"mmaphuge N",		"start N workers stressing mmap with huge mappings" },
	{ NULL, "mmaphuge-file",	"perform mappings on a temporary file" },
	{ NULL,	"mmaphuge-mlock",	"attempt to mlock pages into memory" },
	{ NULL, "mmaphuge-mmaps N",	"select number of memory mappings per iteration" },
	{ NULL, "mmaphuge-numa",	"bind memory mappings to randomly selected NUMA nodes" },
	{ NULL,	"mmaphuge-ops N",	"stop after N mmaphuge bogo operations" },
	{ NULL,	NULL,			NULL }
};

static const stress_opt_t opts[] = {
	{ OPT_mmaphuge_file,  "mmaphuge-file",  TYPE_ID_BOOL, 0, 1, NULL },
	{ OPT_mmaphuge_mlock, "mmaphuge-mlock", TYPE_ID_BOOL, 0, 1, NULL },
	{ OPT_mmaphuge_mmaps, "mmaphuge-mmaps", TYPE_ID_SIZE_T, MIN_MMAPHUGE_MMAPS, MAX_MMAPHUGE_MMAPS, NULL },
	{ OPT_mmaphuge_numa,  "mmaphuge-numa",  TYPE_ID_BOOL, 0, 1, NULL },
	END_OPT
};

#if defined(MAP_HUGETLB)

#define MAX_MMAP_BUFS	(8192)

#if !defined(MAP_HUGE_2MB) && defined(MAP_HUGE_SHIFT)
#define MAP_HUGE_2MB    (21 << MAP_HUGE_SHIFT)
#endif

#if !defined(MAP_HUGE_1GB) && defined(MAP_HUGE_SHIFT)
#define MAP_HUGE_1GB    (30 << MAP_HUGE_SHIFT)
#endif

#if !defined(MAP_HUGE_512MB) && defined(MAP_HUGE_SHIFT)
#define MAP_HUGE_512MB	(29 << MAP_HUGE_SHIFT)
#endif

typedef struct {
	uint8_t	*buf;		/* mapping start */
	size_t	sz;		/* mapping size */
} stress_mmaphuge_buf_t;

typedef struct {
	const int	flags;	/* MMAP flag */
	const size_t	sz;	/* MMAP size */
} stress_mmaphuge_setting_t;

typedef struct {
	stress_mmaphuge_buf_t	*bufs;	/* mmap'd buffers */
	size_t mmaphuge_mmaps;	/* number of mmap'd buffers */
	size_t sz;		/* size of mmap'd file */
	bool mmaphuge_file;	/* true if using mmap'd file */
	bool mmaphuge_mlock;	/* true if using mlocked mmaps */
	bool mmaphuge_numa;	/* true if using numa binding */
	int fd;
#if defined(HAVE_LINUX_MEMPOLICY_H)
	stress_numa_mask_t *numa_mask;
	stress_numa_mask_t *numa_nodes;
#endif
} stress_mmaphuge_context_t;

static const stress_mmaphuge_setting_t stress_mmap_settings[] =
{
#if defined(MAP_HUGE_2MB)
	{ MAP_HUGETLB | MAP_HUGE_2MB,	2 * MB },
#endif
#if defined(MAP_HUGE_1GB)
	{ MAP_HUGETLB | MAP_HUGE_1GB,	1 * GB },
#endif
#if defined(MAP_HUGE_512MB)
	{ MAP_HUGETLB | MAP_HUGE_512MB,	512 * MB },
#endif
	{ MAP_HUGETLB, 1 * GB },
	{ MAP_HUGETLB, 16 * MB },	/* ppc64 */
	{ MAP_HUGETLB, 2 * MB },
	{ 0, 1 * GB },			/* for THP */
	{ 0, 16 * MB },			/* for THP */
	{ 0, 2 * MB },			/* for THP */
};

static int stress_mmaphuge_child(stress_args_t *args, void *v_context)
{
	stress_mmaphuge_context_t *context = (stress_mmaphuge_context_t *)v_context;
	const size_t page_size = args->page_size;
	stress_mmaphuge_buf_t *bufs = (stress_mmaphuge_buf_t *)context->bufs;
	size_t idx = 0;
	int rc = EXIT_SUCCESS;

	stress_set_proc_state(args->name, STRESS_STATE_SYNC_WAIT);
	stress_sync_start_wait(args);
	stress_set_proc_state(args->name, STRESS_STATE_RUN);

	do {
		size_t i;

		for (i = 0; i < context->mmaphuge_mmaps; i++)
			bufs[i].buf = MAP_FAILED;

		for (i = 0; LIKELY(stress_continue(args) && (i < context->mmaphuge_mmaps)); i++) {
			size_t shmall, freemem, totalmem, freeswap, totalswap, last_freeswap, last_totalswap;
			size_t j;

			stress_get_memlimits(&shmall, &freemem, &totalmem, &last_freeswap, &last_totalswap);

			for (j = 0; j < SIZEOF_ARRAY(stress_mmap_settings); j++) {
				uint8_t *buf = MAP_FAILED;
				const size_t sz = stress_mmap_settings[idx].sz;
				int flags = MAP_ANONYMOUS;

				flags |= (stress_mwc1() ? MAP_PRIVATE : MAP_SHARED);
				flags |= stress_mmap_settings[idx].flags;

				if ((g_opt_flags & OPT_FLAGS_OOM_AVOID) && stress_low_memory(page_size))
					break;

				bufs[i].sz = sz;
				/* If we're mapping onto a file, try it first */
				if (context->mmaphuge_file) {
					const off_t offset = (off_t)4096 * stress_mwc8modn(16);

					if (sz + offset < context->sz) {
						buf = (uint8_t *)mmap(NULL, sz,
								PROT_READ | PROT_WRITE,
								flags & ~MAP_ANONYMOUS, context->fd, offset);
						if (buf == MAP_FAILED)
							buf = (uint8_t *)mmap(NULL, sz,
								PROT_READ | PROT_WRITE,
								flags & ~MAP_ANONYMOUS, context->fd, 0);
					}
				}
				/* file mapping failed or not mapped yet, try anonymous map */
				if (buf == MAP_FAILED) {
					buf = (uint8_t *)mmap(NULL, sz,
							PROT_READ | PROT_WRITE,
							flags, -1, 0);
				}
				bufs[i].buf = buf;
				idx++;
				if (UNLIKELY(idx >= SIZEOF_ARRAY(stress_mmap_settings)))
					idx = 0;

				if (buf != MAP_FAILED) {
					const uint64_t rndval = stress_mwc64();
					register const size_t stride = (page_size * 64) / sizeof(uint64_t);
					register uint64_t *ptr, val;
					const uint64_t *buf_end = (uint64_t *)(buf + sz);

#if defined(HAVE_LINUX_MEMPOLICY_H)
					if (context->mmaphuge_numa)
						stress_numa_randomize_pages(args, context->numa_nodes, context->numa_mask, buf, page_size, sz);
#endif

					if (context->mmaphuge_mlock)
						(void)shim_mlock(buf, sz);

					/* Touch every other 64 pages.. */
					for (val = rndval, ptr = (uint64_t *)buf; ptr < buf_end; ptr += stride, val++) {
						*ptr = val;
					}
					/* ..and sanity check */
					for (val = rndval, ptr = (uint64_t *)buf; ptr < buf_end; ptr += stride, val++) {
						if (UNLIKELY(*ptr != val)) {
							pr_fail("%s: memory %p at offset 0x%zx check error, "
								"got 0x%" PRIx64 ", expecting 0x%" PRIx64 "\n",
								args->name, buf, (uint8_t *)ptr - buf, *ptr, val);
							rc = EXIT_FAILURE;
						}
					}

					stress_bogo_inc(args);
					break;
				}
			}
			stress_get_memlimits(&shmall, &freemem, &totalmem, &freeswap, &totalswap);

			/* Check if we eat into swap */
			if (last_freeswap > freeswap)
				break;
		}

		for (i = 0; LIKELY(stress_continue(args) && (i < context->mmaphuge_mmaps)); i++) {
			if (bufs[i].buf == MAP_FAILED)
				continue;
			/* Try Transparent Huge Pages THP */
#if defined(MADV_HUGEPAGE)
			(void)shim_madvise(bufs[i].buf, bufs[i].sz, MADV_NOHUGEPAGE);
#endif
#if defined(MADV_HUGEPAGE)
			(void)shim_madvise(bufs[i].buf, bufs[i].sz, MADV_HUGEPAGE);
#endif
		}

		for (i = 0; i < context->mmaphuge_mmaps; i++) {
			uint8_t *buf = bufs[i].buf;
			size_t sz;

			if (buf == MAP_FAILED)
				continue;

			sz = bufs[i].sz;
			if (page_size < sz) {
				uint8_t *end_page = buf + (sz - page_size);
				int ret;

				*buf = stress_mwc8();
				*end_page = stress_mwc8();
				/* Unmapping small page may fail on huge pages */
				ret = stress_munmap_force((void *)end_page, page_size);
				if (ret == 0)
					ret = stress_munmap_force((void *)buf, sz - page_size);
				if (ret != 0)
					(void)stress_munmap_force((void *)buf, sz);
			} else {
				*buf = stress_mwc8();
				(void)stress_munmap_force((void *)buf, sz);
			}
			bufs[i].buf = MAP_FAILED;
		}
	} while (stress_continue(args));

	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);

	return rc;
}

/*
 *  stress_mmaphuge()
 *	stress huge page mmappings and unmappings
 */
static int stress_mmaphuge(stress_args_t *args)
{
	stress_mmaphuge_context_t context;

	int ret;

#if defined(HAVE_LINUX_MEMPOLICY_H)
	context.numa_mask = NULL;
	context.numa_nodes = NULL;
#endif
	context.sz = 16 * MB;
	context.fd = -1;
	context.mmaphuge_mmaps = MAX_MMAP_BUFS;
	if (!stress_get_setting("mmaphuge-mmaps", &context.mmaphuge_mmaps)) {
		if (g_opt_flags & OPT_FLAGS_MAXIMIZE)
			context.mmaphuge_mmaps = MAX_MMAPHUGE_MMAPS;
		if (g_opt_flags & OPT_FLAGS_MINIMIZE)
			context.mmaphuge_mmaps = MIN_MMAPHUGE_MMAPS;
	}
	context.mmaphuge_file = false;
	(void)stress_get_setting("mmaphuge-file", &context.mmaphuge_file);
	context.mmaphuge_numa = false;
	(void)stress_get_setting("mmaphuge-numa", &context.mmaphuge_numa);
	context.mmaphuge_mlock = false;
	(void)stress_get_setting("mmaphuge-mlock", &context.mmaphuge_mlock);

	context.bufs = (stress_mmaphuge_buf_t *)calloc(context.mmaphuge_mmaps, sizeof(*context.bufs));
	if (!context.bufs) {
		pr_inf_skip("%s: cannot allocate %zu byte buffer array%s, skipping stressor\n",
			args->name, sizeof(context.mmaphuge_mmaps) * sizeof(*context.bufs),
			stress_get_memfree_str());
		return EXIT_NO_RESOURCE;
	}

	if (context.mmaphuge_file) {
		char filename[PATH_MAX];
		ssize_t rc;

		rc = stress_temp_dir_mk_args(args);
		if (rc < 0) {
			free(context.bufs);
			return stress_exit_status((int)-rc);
		}

		(void)stress_temp_filename_args(args,
			filename, sizeof(filename), stress_mwc32());
		context.fd = open(filename, O_CREAT | O_RDWR, S_IRUSR | S_IWUSR);
		if (context.fd < 0) {
			rc = stress_exit_status(errno);
			pr_fail("%s: open %s failed, errno=%d (%s)\n",
				args->name, filename, errno, strerror(errno));
			(void)shim_unlink(filename);
			(void)stress_temp_dir_rm_args(args);
			free(context.bufs);

			return (int)rc;
		}
		(void)shim_unlink(filename);
		if (lseek(context.fd, (off_t)(context.sz - args->page_size), SEEK_SET) < 0) {
			pr_fail("%s: lseek failed, errno=%d (%s)\n",
				args->name, errno, strerror(errno));
			(void)close(context.fd);
			(void)stress_temp_dir_rm_args(args);
			free(context.bufs);

			return EXIT_FAILURE;
		}
		/*
		 *  Allocate a 16 MB aligned chunk of data.
		 */
		if (shim_fallocate(context.fd, 0, 0, (off_t)context.sz) < 0) {
			rc = stress_exit_status(errno);
			pr_fail("%s: fallocate of %zu MB failed, errno=%d (%s)\n",
				args->name, (size_t)(context.fd / MB), errno, strerror(errno));
			(void)close(context.fd);
			(void)stress_temp_dir_rm_args(args);
			free(context.bufs);
			return (int)rc;
		}
	}

	if (context.mmaphuge_numa) {
#if defined(HAVE_LINUX_MEMPOLICY_H)
		stress_numa_mask_and_node_alloc(args, &context.numa_nodes,
						&context.numa_mask, "--mmaphuge-numa",
						&context.mmaphuge_numa);
#else
		if (stress_instance_zero(args))
			pr_inf("%s: --mmaphuge-numa selected but not supported by this system, disabling option\n",
				args->name);
		context.mmaphuge_numa = false;
#endif
	}

	ret = stress_oomable_child(args, (void *)&context, stress_mmaphuge_child, STRESS_OOMABLE_QUIET);

#if defined(HAVE_LINUX_MEMPOLICY_H)
	if (context.numa_mask)
		stress_numa_mask_free(context.numa_mask);
	if (context.numa_nodes)
		stress_numa_mask_free(context.numa_nodes);
#endif
	free(context.bufs);

	if (context.mmaphuge_file) {
		(void)close(context.fd);
		(void)stress_temp_dir_rm_args(args);
	}

	return ret;
}

const stressor_info_t stress_mmaphuge_info = {
	.stressor = stress_mmaphuge,
	.classifier = CLASS_VM | CLASS_OS,
	.opts = opts,
	.verify = VERIFY_ALWAYS,
	.help = help
};

#else

const stressor_info_t stress_mmaphuge_info = {
	.stressor = stress_unimplemented,
	.classifier = CLASS_VM | CLASS_OS,
	.opts = opts,
	.verify = VERIFY_ALWAYS,
	.help = help,
	.unimplemented_reason = "built without mmap() MAP_HUGETLB support"
};

#endif
