/*
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
#include "core-out-of-memory.h"

static const stress_help_t help[] = {
	{ NULL,	"msyncmany N",		"start N workers stressing msync on many mapped pages" },
	{ NULL,	"msyncmany-ops N",	"stop after N msyncmany bogo operations" },
	{ NULL,	NULL,		  NULL }
};

#if defined(HAVE_MSYNC)

#define MMAP_MAX	(32768)

static int stress_msyncmany_child(stress_args_t *args, void *context)
{
	const size_t page_size = args->page_size;
	long int max = sysconf(_SC_MAPPED_FILES);
	uint64_t **mappings;
	int fd = *(int *)context;
	size_t i, n;
	uint64_t *mapped = NULL;
	int rc = EXIT_SUCCESS;

	(void)context;

	max = STRESS_MINIMUM(max, MMAP_MAX);
	if (max < 1) {
		pr_fail("%s: sysconf(_SC_MAPPED_FILES) is too low, max = %ld\n",
			args->name, max);
		return EXIT_NO_RESOURCE;
	}
	mappings = (uint64_t **)calloc((size_t)max, sizeof(*mappings));
	if (!mappings) {
		pr_fail("%s: calloc of %zu bytes failed%s, out of memory\n",
			args->name, (size_t)max * sizeof(*mappings),
			stress_get_memfree_str());
		return EXIT_NO_RESOURCE;
	}

	for (n = 0; LIKELY(stress_continue_flag() && (n < (size_t)max)); n++) {
		uint64_t *ptr;

		if (UNLIKELY(!stress_continue(args)))
			break;
		if ((g_opt_flags & OPT_FLAGS_OOM_AVOID) && stress_low_memory(page_size))
			break;

		ptr = (uint64_t *)mmap(NULL, page_size, PROT_READ | PROT_WRITE,
			MAP_SHARED, fd, 0);
		if (UNLIKELY(ptr == MAP_FAILED))
			break;
		if (!mapped)
			mapped = ptr;
		mappings[n] = ptr;
		stress_set_vma_anon_name(ptr, page_size, "msync-rw-page");
	}

	stress_set_proc_state(args->name, STRESS_STATE_SYNC_WAIT);
	stress_sync_start_wait(args);
	stress_set_proc_state(args->name, STRESS_STATE_RUN);

	if (!mapped) {
		pr_inf("%s: no mappings made, out of resources%s\n",
			args->name, stress_get_memfree_str());
		rc = EXIT_NO_RESOURCE;
		goto finish;
	}

	do {
		int ret, failed = 0;
		const uint64_t pattern = stress_mwc64();

		*mapped = pattern;

		ret = msync((void *)mapped, args->page_size, MS_SYNC | MS_INVALIDATE);
		if (UNLIKELY(ret < 0)) {
			pr_fail("%s: msync failed, errno=%d (%s)\n",
				args->name, errno, strerror(errno));
			rc = EXIT_FAILURE;
			break;
		}
		for (i = 0; i < n; i++) {
			const uint64_t *ptr = (uint64_t *)mappings[i];

			if (UNLIKELY(!ptr))
				continue;
			if (UNLIKELY(*ptr != pattern)) {
				pr_fail("%s: failed: mapping %zd at %p contained %" PRIx64 " and not %" PRIx64 "\n",
					args->name, i, (const void *)ptr, *ptr, pattern);
				rc = EXIT_FAILURE;
				failed++;
				if (UNLIKELY(failed >= 5))
					break;
			}
		}
		stress_bogo_inc(args);
	} while (stress_continue(args));

finish:
	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);

	for (i = 0; i < n; i++)
		(void)munmap((void *)mappings[i], page_size);

	free(mappings);
	(void)close(fd);
	return rc;
}

/*
 *  stress_msyncmany()
 *	stress mmap with many pages being mapped
 */
static int stress_msyncmany(stress_args_t *args)
{
	int ret, fd;
	char filename[PATH_MAX];

	ret = stress_temp_dir_mk_args(args);
        if (ret < 0)
                return stress_exit_status(-ret);

	(void)stress_temp_filename_args(args, filename, sizeof(filename), stress_mwc32());
	fd = open(filename, O_CREAT | O_RDWR, S_IRUSR | S_IWUSR);
	if (fd < 0) {
		pr_inf_skip("%s: cannot create %s, skipping stressor\n", args->name, filename);
		(void)stress_temp_dir_rm_args(args);
		return EXIT_NO_RESOURCE;
	}
	(void)shim_unlink(filename);

	ret = shim_fallocate(fd, 0, 0, (off_t)args->page_size);
	if (ret < 0) {
		pr_inf_skip("%s: cannot allocate data for file %s, skipping stressor\n", args->name, filename);
		(void)stress_temp_dir_rm_args(args);
		return EXIT_NO_RESOURCE;
	}
	ret = stress_oomable_child(args, (void *)&fd, stress_msyncmany_child, STRESS_OOMABLE_NORMAL);
	(void)close(fd);
	(void)stress_temp_dir_rm_args(args);

	return ret;
}

const stressor_info_t stress_msyncmany_info = {
	.stressor = stress_msyncmany,
	.classifier = CLASS_VM | CLASS_OS,
	.verify = VERIFY_ALWAYS,
	.help = help
};
#else
const stressor_info_t stress_msyncmany_info = {
	.stressor = stress_unimplemented,
	.classifier = CLASS_VM | CLASS_OS,
	.verify = VERIFY_ALWAYS,
	.help = help,
	.unimplemented_reason = "built without msync() system call support"
};
#endif
