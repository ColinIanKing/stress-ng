/*
 * Copyright (C) 2013-2021 Canonical, Ltd.
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
			pr_inf("%s stressor will be skipped, memfd_secret system call "
			       "is not supported\n", name);
			break;
		case ENOMEM:
			pr_inf("%s stressor will be skipped, secret memory not reserved, "
			       "e.g. use 'secretmem=1M' in the kernel boot command\n", name);
			break;
		default:
			pr_inf("%s stressor will be skipped, memfd_secret errno=%d (%s)\n",
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
			if ((munmap((void *)mappings[i], page_size) == 0) &&
			    (munmap((void *)(mappings[i] + page_size2), page_size) == 0)) {
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
static int stress_secretmem_child(const stress_args_t *args, void *context)
{
	const size_t page_size = args->page_size;
	const size_t page_size3 = page_size * 3;
	uint8_t **mappings;
	int fd;

	(void)context;

	mappings = calloc(MMAP_MAX, sizeof(*mappings));
	if (!mappings) {
		pr_fail("%s: calloc failed, out of memory\n", args->name);
		return EXIT_NO_RESOURCE;
	}

	fd = shim_memfd_secret(0);
	if (fd < 0) {
		pr_inf("%s: memfd_secret failed, skipping stressor\n",
			args->name);
		free(mappings);
		return EXIT_NO_RESOURCE;
	}

	stress_set_proc_state(args->name, STRESS_STATE_RUN);

	do {
		ssize_t n;
		uint8_t *redo_unmapping = NULL;
		size_t sz = 0;
		off_t offset;

		for (n = 0; keep_stressing_flag() && (n < MMAP_MAX); n++) {
			if (!keep_stressing(args))
				break;

			offset = (off_t)sz;
			sz += page_size3;

			/* expand secret memory size */
			if (ftruncate(fd, sz) != 0)
				break;

			mappings[n] = (uint8_t *)mmap(NULL, page_size3,
				PROT_READ | PROT_WRITE, MAP_SHARED, fd, offset);
			if (mappings[n] == MAP_FAILED) {
				mappings[n] = NULL;
				break;
			}

			/*
			 *  touch pages, this will trigger OOM SIGKILL
			 *  when we run low on secretmem pages
			 */
			(void)memset((void *)mappings[n], 0xff, page_size3);

			/*
			 *  Make an hole in the 3 page mapping on middle page
			 */
			if (munmap((void *)(mappings[n] + page_size), page_size) < 0) {
				/* Failed?, remember to retry later */
				redo_unmapping = mappings[n];
				break;
			}
			inc_counter(args);
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
			(void)munmap(redo_unmapping, page_size3);

	} while (keep_stressing(args));

	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);

	(void)close(fd);

	return EXIT_SUCCESS;
}

/*
 *  stress_secretmem()
 *	stress linux secretmem mappings
 */
static int stress_secretmem(const stress_args_t *args)
{
	return stress_oomable_child(args, NULL, stress_secretmem_child, STRESS_OOMABLE_QUIET);
}

stressor_info_t stress_secretmem_info = {
	.stressor = stress_secretmem,
	.class = CLASS_CPU,
	.help = help,
	.supported = stress_secretmem_supported
};
#else
stressor_info_t stress_secretmem_info = {
	.stressor = stress_not_implemented,
	.class = CLASS_CPU,
	.help = help
};
#endif
