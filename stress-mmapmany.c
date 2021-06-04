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

static const stress_help_t help[] = {
	{ NULL,	"mmapmany N",	  "start N workers stressing many mmaps and munmaps" },
	{ NULL,	"mmapmany-ops N", "stop after N mmapmany bogo operations" },
	{ NULL,	NULL,		  NULL }
};

#define MMAP_MAX	(256*1024)

static int stress_mmapmany_child(const stress_args_t *args, void *context)
{
	const size_t page_size = args->page_size;
	long max = sysconf(_SC_MAPPED_FILES);
	uint8_t **mappings;
	max = STRESS_MAXIMUM(max, MMAP_MAX);

	(void)context;

	if (max < 1) {
		pr_fail("%s: sysconf(_SC_MAPPED_FILES) is too low, max = %ld\n",
			args->name, max);
		return EXIT_NO_RESOURCE;
	}
	mappings = calloc((size_t)max, sizeof(*mappings));
	if (!mappings) {
		pr_fail("%s: malloc failed, out of memory\n", args->name);
		return EXIT_NO_RESOURCE;
	}

	stress_set_proc_state(args->name, STRESS_STATE_RUN);

	do {
		size_t i, n;

		for (n = 0; keep_stressing_flag() && (n < (size_t)max); n++) {
			if (!keep_stressing(args))
				break;

			mappings[n] = (uint8_t *)mmap(NULL,
				page_size * 3,
				PROT_READ | PROT_WRITE,
				MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
			if (mappings[n] == MAP_FAILED)
				break;
			if (munmap((void *)(mappings[n] + page_size), page_size) < 0)
				break;
			inc_counter(args);
		}

		for (i = 0; i < n; i++) {
			(void)munmap((void *)mappings[i], page_size);
			(void)munmap((void *)(mappings[i] + page_size), page_size);
			(void)munmap((void *)(mappings[i] + page_size + page_size), page_size);
		}
	} while (keep_stressing(args));

	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);

	free(mappings);
	return EXIT_SUCCESS;
}

/*
 *  stress_mmapmany()
 *	stress mmap with many pages being mapped
 */
static int stress_mmapmany(const stress_args_t *args)
{
	return stress_oomable_child(args, NULL, stress_mmapmany_child, STRESS_OOMABLE_NORMAL);
}

stressor_info_t stress_mmapmany_info = {
	.stressor = stress_mmapmany,
	.class = CLASS_VM | CLASS_OS,
	.help = help
};
