/*
 * Copyright (C) 2013-2016 Canonical, Ltd.
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
#define _GNU_SOURCE

#include "stress-ng.h"

#if defined(STRESS_REMAP_FILE_PAGES)

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/mman.h>

#define N_PAGES		(512)

typedef uint16_t mapdata_t;

/*
 *  check_order()
 *	check page order
 */
static void check_order(
	const char *name,
	const size_t stride,
	const mapdata_t *data,
	const size_t *order,
	const char *ordering)
{
	size_t i;
	bool failed;

	for (failed = false, i = 0; i < N_PAGES; i++) {
		if (data[i * stride] != order[i]) {
			failed = true;
			break;
		}
	}
	if (failed)
		pr_fail(stderr, "%s: remap %s order pages failed\n",
			name, ordering);
}

/*
 *  remap_order()
 *	remap based on given order
 */
static int remap_order(
	const char *name,
	const size_t stride,
	mapdata_t *data,
	const size_t *order,
	const size_t page_size)
{
	size_t i;

	for (i = 0; i < N_PAGES; i++) {
		int ret;

		ret = remap_file_pages(data + (i * stride), page_size,
			0, order[i], 0);
		if (ret < 0) {
			pr_fail(stderr, "%s: remap_file_pages: errno=%d (%s)\n",
				name, errno, strerror(errno));
			return -1;
		}
	}

	return 0;
}

/*
 *  stress_remap
 *	stress page remapping
 */
int stress_remap(
	uint64_t *const counter,
	const uint32_t instance,
	const uint64_t max_ops,
	const char *name)
{
	mapdata_t *data;
	const size_t page_size = stress_get_pagesize();
	const size_t data_size = N_PAGES * page_size;
	const size_t stride = page_size / sizeof(*data);
	size_t i;

	(void)instance;

	data = mmap(NULL, data_size, PROT_READ | PROT_WRITE,
			MAP_SHARED | MAP_ANONYMOUS, -1, 0);
	if (data == MAP_FAILED) {
		pr_err(stderr, "%s: mmap failed: errno=%d (%s)\n",
			name, errno, strerror(errno));
		return EXIT_NO_RESOURCE;
	}

	for (i = 0; i < N_PAGES; i++)
		data[i * stride] = i;

	do {
		size_t order[N_PAGES];

		/* Reverse pages */
		for (i = 0; i < N_PAGES; i++)
			order[i] = N_PAGES - 1 - i;

		if (remap_order(name, stride, data, order, page_size) < 0)
			break;
		check_order(name, stride, data, order, "reverse");

		/* random order pages */
		for (i = 0; i < N_PAGES; i++)
			order[i] = i;
		for (i = 0; i < N_PAGES; i++) {
			size_t tmp, j = mwc32() % N_PAGES;

			tmp = order[i];
			order[i] = order[j];
			order[j] = tmp;
		}

		if (remap_order(name, stride, data, order, page_size) < 0)
			break;
		check_order(name, stride, data, order, "random");

		/* all mapped to 1 page */
		for (i = 0; i < N_PAGES; i++)
			order[i] = 0;
		if (remap_order(name, stride, data, order, page_size) < 0)
			break;
		check_order(name, stride, data, order, "all-to-1");

		/* reorder pages back again */
		for (i = 0; i < N_PAGES; i++)
			order[i] = i;
		if (remap_order(name, stride, data, order, page_size) < 0)
			break;
		check_order(name, stride, data, order, "forward");

		(*counter)++;
	} while (opt_do_run && (!max_ops || *counter < max_ops));

	munmap(data, data_size);

	return EXIT_SUCCESS;
}

#endif
