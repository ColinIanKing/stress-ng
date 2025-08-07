/*
 * Copyright (C) 2024-2025 Colin Ian King.
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
#include "core-put.h"

static const stress_help_t help[] = {
	{ NULL,	"mmapfiles N",		"start N workers stressing many mmaps and munmaps" },
	{ NULL, "mmapfiles-numa",	"bind memory mappings to randomly selected NUMA nodes" },
	{ NULL,	"mmapfiles-ops N",	"stop after N mmapfiles bogo operations" },
	{ NULL, "mmapfiles-populate",	"populate memory mappings" },
	{ NULL, "mmapfiles-shared",	"enable shared mappings instead of private mappings" },
	{ NULL,	NULL,		  	NULL }
};

#define MMAP_MAX	(512 * 1024)

typedef struct {
	void *addr;
	size_t len;
} stress_mapping_t;

typedef struct {
	double mmap_page_count;
	double mmap_count;
	double mmap_duration;
	double munmap_page_count;
	double munmap_count;
	double munmap_duration;
	bool mmapfiles_numa;
	bool mmapfiles_populate;
	bool mmapfiles_shared;
	bool enomem;
	stress_mapping_t *mappings;
#if defined(HAVE_LINUX_MEMPOLICY_H)
	stress_numa_mask_t *numa_mask;
	stress_numa_mask_t *numa_nodes;
#endif
} stress_mmapfile_info_t;

static const stress_opt_t opts[] = {
	{ OPT_mmapfiles_numa,     "mmapfiles-numa",     TYPE_ID_BOOL, 0, 1, NULL },
	{ OPT_mmapfiles_populate, "mmapfiles-populate", TYPE_ID_BOOL, 0, 1, NULL },
	{ OPT_mmapfiles_shared,   "mmapfiles-shared",   TYPE_ID_BOOL, 0, 1, NULL },
	END_OPT,
};

static size_t stress_mmapfiles_dir(
	stress_args_t *args,
	stress_mmapfile_info_t *mmapfile_info,
	const char *path,
	size_t n_mappings)
{
	DIR *dir;
	const struct dirent *d;
	int flags = 0;

	flags |= mmapfile_info->mmapfiles_shared ? MAP_SHARED : MAP_PRIVATE;
#if defined(MAP_POPULATE)
	flags |= mmapfile_info->mmapfiles_populate ? MAP_POPULATE : 0;
#endif
	dir = opendir(path);
	if (!dir)
		return n_mappings;

	while (!(mmapfile_info->enomem) && ((d = readdir(dir)) != NULL)) {
		unsigned char type;

		if (UNLIKELY(n_mappings >= MMAP_MAX))
			break;
		if (UNLIKELY(!stress_continue_flag()))
			break;
		if (UNLIKELY(stress_is_dot_filename(d->d_name)))
			continue;
		type = shim_dirent_type(path, d);
		if (type == SHIM_DT_DIR) {
			char newpath[PATH_MAX];

			(void)snprintf(newpath, sizeof(newpath), "%s/%s", path, d->d_name);
			n_mappings = stress_mmapfiles_dir(args, mmapfile_info, newpath, n_mappings);
		} else if (type == SHIM_DT_REG) {
			char filename[PATH_MAX];
			uint8_t *ptr;
			struct stat statbuf;
			int fd;
			double t, delta;
			size_t len;
			const size_t page_size = args->page_size;

			(void)snprintf(filename, sizeof(filename), "%s/%s", path, d->d_name);
			fd = open(filename, O_RDONLY);
			if (UNLIKELY(fd < 0))
				continue;

			if (UNLIKELY(fstat(fd, &statbuf) < 0)) {
				(void)close(fd);
				continue;
			}
			len = statbuf.st_size;
			if (UNLIKELY((g_opt_flags & OPT_FLAGS_OOM_AVOID) && stress_low_memory(len))) {
				(void)close(fd);
				break;
			}

			t = stress_time_now();
			ptr = (uint8_t *)mmap(NULL, len, PROT_READ, flags, fd, 0);
			delta = stress_time_now() - t;
			if (LIKELY(ptr != MAP_FAILED)) {
#if defined(HAVE_LINUX_MEMPOLICY_H)
				if (mmapfile_info->mmapfiles_numa) {
					const size_t page_len = (len + (page_size - 1)) & ~(page_size - 1);
					if (page_len > 0)
						stress_numa_randomize_pages(args, mmapfile_info->numa_nodes, mmapfile_info->numa_mask, ptr, page_len, page_size);
				}
#endif
				if (mmapfile_info->mmapfiles_populate) {
					register size_t i;

					for (i = 0; i < len; i += page_size) {
						stress_uint8_put(*(ptr + i));
					}
				}
				mmapfile_info->mappings[n_mappings].addr = (void *)ptr;
				mmapfile_info->mappings[n_mappings].len = len;
				n_mappings++;
				mmapfile_info->mmap_count += 1.0;
				mmapfile_info->mmap_duration += delta;
				mmapfile_info->mmap_page_count += (double)(len + page_size - 1) / page_size;
				stress_bogo_inc(args);
			} else {
				if (errno == ENOMEM) {
					mmapfile_info->enomem = true;
					(void)close(fd);
					break;
				}
			}
			(void)close(fd);
		}
	}
	(void)closedir(dir);
	return n_mappings;
}

static int stress_mmapfiles_child(stress_args_t *args, void *context)
{
	size_t idx = 0;
	stress_mmapfile_info_t *mmapfile_info = (stress_mmapfile_info_t *)context;
	static const char * const dirs[] = {
		"/lib",
		"/lib32",
		"/lib64",
		"/boot",
		"/bin",
		"/etc,",
		"/sbin",
		"/usr",
		"/var",
		"/sys",
		"/proc",
	};

	mmapfile_info->mappings = (stress_mapping_t *)calloc((size_t)MMAP_MAX, sizeof(*mmapfile_info->mappings));
	if (UNLIKELY(!mmapfile_info->mappings)) {
		pr_fail("%s: malloc failed, out of memory\n", args->name);
		return EXIT_NO_RESOURCE;
	}

	stress_set_proc_state(args->name, STRESS_STATE_SYNC_WAIT);
	stress_sync_start_wait(args);
	stress_set_proc_state(args->name, STRESS_STATE_RUN);

	do {
		size_t i, n;

		for (n = 0, i = 0; i < SIZEOF_ARRAY(dirs); i++) {
			mmapfile_info->enomem = false;

			n = stress_mmapfiles_dir(args, mmapfile_info, dirs[idx], n);
			idx++;
			if (UNLIKELY(idx >= SIZEOF_ARRAY(dirs)))
				idx = 0;
			if (mmapfile_info->enomem)
				break;
		}

		for (i = 0; i < n; i++) {
			double t;
			const size_t len = mmapfile_info->mappings[i].len;

			t = stress_time_now();
			if (LIKELY(munmap((void *)mmapfile_info->mappings[i].addr, len) == 0)) {
				const double delta = stress_time_now() - t;

				mmapfile_info->munmap_duration += delta;
				mmapfile_info->munmap_count += 1.0;
				mmapfile_info->munmap_page_count += (double)(len + args->page_size - 1) / args->page_size;
			} else {
				(void)stress_munmap_force((void *)mmapfile_info->mappings[i].addr, mmapfile_info->mappings[i].len);
			}
			mmapfile_info->mappings[i].addr = NULL;
			mmapfile_info->mappings[i].len = 0;
		}
	} while (stress_continue(args));

	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);

	free(mmapfile_info->mappings);
	return EXIT_SUCCESS;
}

/*
 *  stress_mmapfiles()
 *	stress mmap with many pages being mapped
 */
static int stress_mmapfiles(stress_args_t *args)
{
	stress_mmapfile_info_t *mmapfile_info;
	int ret;
	double metric;

	mmapfile_info = (stress_mmapfile_info_t *)stress_mmap_populate(NULL, sizeof(*mmapfile_info),
				PROT_READ | PROT_WRITE,
				MAP_SHARED | MAP_ANONYMOUS,
				-1, 0);
	if (mmapfile_info == MAP_FAILED) {
		pr_inf("%s: cannot mmap %zu byte mmap file information%s, "
			"errno=%d (%s), skipping stressor\n",
			args->name, sizeof(*mmapfile_info),
			stress_get_memfree_str(),
			errno, strerror(errno));
		return EXIT_NO_RESOURCE;
	}
	stress_set_vma_anon_name(mmapfile_info, sizeof(*mmapfile_info), "mmapfile-info");
	mmapfile_info->mmap_page_count = 0.0;
	mmapfile_info->mmap_count = 0.0;
	mmapfile_info->mmap_duration = 0.0;
	mmapfile_info->munmap_page_count = 0.0;
	mmapfile_info->munmap_count = 0.0;
	mmapfile_info->munmap_duration = 0.0;
	mmapfile_info->mmapfiles_numa = false;
	mmapfile_info->mmapfiles_populate = false;
	mmapfile_info->mmapfiles_shared = false;
	mmapfile_info->mappings = NULL;
#if defined(HAVE_LINUX_MEMPOLICY_H)
	mmapfile_info->numa_mask = NULL;
	mmapfile_info->numa_nodes = NULL;
#endif

	(void)stress_get_setting("mmapfiles-numa", &mmapfile_info->mmapfiles_numa);
	(void)stress_get_setting("mmapfiles-populate", &mmapfile_info->mmapfiles_populate);
	(void)stress_get_setting("mmapfiles-shared", &mmapfile_info->mmapfiles_shared);

	if (mmapfile_info->mmapfiles_numa) {
#if defined(HAVE_LINUX_MEMPOLICY_H)
		stress_numa_mask_and_node_alloc(args, &mmapfile_info->numa_nodes,
						&mmapfile_info->numa_mask, "--mmapfiles-numa",
						&mmapfile_info->mmapfiles_numa);
#else
		if (stress_instance_zero(args))
			pr_inf("%s: --mmapfiles-numa selected but not supported by this system, disabling option\n",
				args->name);
		mmapfile_info->mmapfiles_numa = false;
#endif
	}

	ret = stress_oomable_child(args, (void *)mmapfile_info, stress_mmapfiles_child, STRESS_OOMABLE_NORMAL);

	metric = (mmapfile_info->mmap_duration > 0.0) ? mmapfile_info->mmap_count / mmapfile_info->mmap_duration : 0.0;
	stress_metrics_set(args, 0, "file mmaps per sec ", metric, STRESS_METRIC_HARMONIC_MEAN);
	metric = (mmapfile_info->munmap_duration > 0.0) ? mmapfile_info->munmap_count / mmapfile_info->munmap_duration : 0.0;
	stress_metrics_set(args, 1, "file munmap per sec", metric, STRESS_METRIC_HARMONIC_MEAN);

	metric = (mmapfile_info->mmap_duration > 0.0) ? mmapfile_info->mmap_page_count / mmapfile_info->mmap_duration: 0.0;
	stress_metrics_set(args, 2, "file pages mmap'd per sec", metric, STRESS_METRIC_HARMONIC_MEAN);
	metric = (mmapfile_info->munmap_duration > 0.0) ? mmapfile_info->munmap_page_count / mmapfile_info->munmap_duration: 0.0;
	stress_metrics_set(args, 3, "file pages munmap'd per sec", metric, STRESS_METRIC_HARMONIC_MEAN);
	metric = (mmapfile_info->mmap_count > 0.0) ? mmapfile_info->mmap_page_count / mmapfile_info->mmap_count : 0.0;
	stress_metrics_set(args, 4, "pages per mapping", metric, STRESS_METRIC_HARMONIC_MEAN);

#if defined(HAVE_LINUX_MEMPOLICY_H)
	if (mmapfile_info->numa_mask)
		stress_numa_mask_free(mmapfile_info->numa_mask);
	if (mmapfile_info->numa_nodes)
		stress_numa_mask_free(mmapfile_info->numa_nodes);
#endif
	(void)munmap((void *)mmapfile_info, sizeof(*mmapfile_info));

	return ret;
}

const stressor_info_t stress_mmapfiles_info = {
	.stressor = stress_mmapfiles,
	.classifier = CLASS_VM | CLASS_OS,
	.verify = VERIFY_ALWAYS,
	.opts = opts,
	.help = help
};
