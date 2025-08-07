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
#include "core-builtin.h"
#include "core-madvise.h"
#include "core-out-of-memory.h"

#define MMAP_MAX	(256*1024)

static const stress_help_t help[] = {
	{ NULL,	"secretmem N",		"start N workers that use secretmem mappings" },
	{ NULL,	"secretmem-ops N",	"stop after N secretmem bogo operations" },
	{ NULL,	NULL,		NULL }
};

#if defined(__NR_memfd_secret) &&	\
    defined(__linux__)

static int stress_secretmem_supported(const char *name)
{
	int fd;

	fd = shim_memfd_secret(0);
	if (fd < 0) {
		switch (errno) {
		case ENOSYS:
			pr_inf_skip("%s stressor will be skipped, memfd_secret system call "
			       "is not supported\n", name);
			break;
		case ENOMEM:
			pr_inf_skip("%s stressor will be skipped, secret memory not reserved, "
			       "e.g. use 'secretmem=1M' in the kernel boot command\n", name);
			break;
		default:
			pr_inf_skip("%s stressor will be skipped, memfd_secret errno=%d (%s)\n",
				name, errno, strerror(errno));
			break;
		}
		return -1;
	}
	return 0;
}

/*
 *   stress_secretmem_unmap()
 *      unmap first and last page of 3 page mapping
 */
static bool stress_secretmem_unmap(
	uint8_t **mappings,
	const ssize_t n,
	const size_t page_size)
{
	ssize_t i;
	bool retry = false;
	const size_t page_size2 = page_size << 1;

	for (i = 0; i < n; i++) {
		if (mappings[i]) {
			if (LIKELY((stress_munmap_force((void *)mappings[i], page_size) == 0) &&
				   (stress_munmap_force((void *)(mappings[i] + page_size2), page_size) == 0))) {
				mappings[i] = NULL;
			} else {
				/* munmap failed, e.g. ENOMEM, so flag it */
				retry = true;
			}
		}
	}
	return retry;
}

/*
 *  stress_secretmem_child()
 *     OOMable secretmem stressor
 */
static int stress_secretmem_child(stress_args_t *args, void *context)
{
	const size_t page_size = args->page_size;
	const size_t page_size3 = page_size * 3;
	uint8_t **mappings;
	int fd;

	(void)context;

	mappings = (uint8_t **)calloc(MMAP_MAX, sizeof(*mappings));
	if (UNLIKELY(!mappings)) {
		pr_inf_skip("%s: failed to allocate %zu bytes%s, skipping stressor\n",
			args->name, (size_t)MMAP_MAX * sizeof(*mappings),
			stress_get_memfree_str());
		return EXIT_NO_RESOURCE;
	}

	fd = shim_memfd_secret(0);
	if (fd < 0) {
		pr_inf_skip("%s: memfd_secret failed, skipping stressor\n",
			args->name);
		free(mappings);
		return EXIT_NO_RESOURCE;
	}

	stress_set_proc_state(args->name, STRESS_STATE_SYNC_WAIT);
	stress_sync_start_wait(args);
	stress_set_proc_state(args->name, STRESS_STATE_RUN);

	do {
		ssize_t n;
		uint8_t *redo_unmapping = NULL;
		off_t sz = 0;

		for (n = 0; LIKELY(stress_continue_flag() && (n < MMAP_MAX)); n++) {
			const off_t offset = sz;

			if (UNLIKELY(!stress_continue(args)))
				break;

			sz += page_size3;

			/* expand secret memory size */
			if (UNLIKELY(ftruncate(fd, sz) != 0))
				break;

			if (UNLIKELY((g_opt_flags & OPT_FLAGS_OOM_AVOID) && stress_low_memory(page_size3)))
				break;

			mappings[n] = (uint8_t *)mmap(NULL, page_size3,
				PROT_READ | PROT_WRITE, MAP_SHARED, fd, offset);
			if (UNLIKELY(mappings[n] == MAP_FAILED)) {
				mappings[n] = NULL;
				break;
			}

			(void)stress_madvise_mergeable(mappings[n], page_size3);
			/*
			 *  touch pages, this will trigger OOM SIGKILL
			 *  when we run low on secretmem pages
			 */
			(void)shim_memset((void *)mappings[n], 0xff, page_size3);

			/*
			 *  Make an hole in the 3 page mapping on middle page
			 */
			if (UNLIKELY(stress_munmap_force((void *)(mappings[n] + page_size), page_size) < 0)) {
				/* Failed?, remember to retry later */
				redo_unmapping = mappings[n];
				break;
			}
			stress_bogo_inc(args);
		}

		if (stress_secretmem_unmap(mappings, n, page_size)) {
			/*
			 *  Since munmap above may fail with ENOMEM, retry any
			 *  failed unmappings once more...
			 */
			(void)stress_secretmem_unmap(mappings, n, page_size);
		}

		/*
		 *  ..and now redo an unmapping that failed earlier
		 */
		if (redo_unmapping)
			(void)stress_munmap_force(redo_unmapping, page_size3);

	} while (stress_continue(args));

	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);

	(void)close(fd);
	free(mappings);

	return EXIT_SUCCESS;
}

/*
 *  stress_secretmem()
 *	stress linux secretmem mappings
 */
static int stress_secretmem(stress_args_t *args)
{
	return stress_oomable_child(args, NULL, stress_secretmem_child, STRESS_OOMABLE_QUIET);
}

const stressor_info_t stress_secretmem_info = {
	.stressor = stress_secretmem,
	.classifier = CLASS_CPU,
	.help = help,
	.supported = stress_secretmem_supported
};
#else
const stressor_info_t stress_secretmem_info = {
	.stressor = stress_unimplemented,
	.classifier = CLASS_CPU,
	.help = help,
	.unimplemented_reason = "built with headers that did not define memfd_secret() system call"
};
#endif
