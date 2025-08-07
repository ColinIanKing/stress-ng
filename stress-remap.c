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
#include "core-arch.h"
#include "core-mmap.h"
#include "core-pragma.h"

#define MIN_REMAP_PAGES		(1)
#define MAX_REMAP_PAGES		(0x80000000ULL)
#define DEFAULT_REMAP_PAGES	(512)		/* Must be a power of 2 */

static const stress_help_t help[] = {
	{ NULL,	"remap N",		"start N workers exercising page remappings" },
	{ NULL,	"remap-mlock",		"attempt to mlock pages into memory" },
	{ NULL,	"remap-ops N",		"stop after N remapping bogo operations" },
	{ NULL,	"remap-pages N",	"specify N pages to remap (N must be power of 2)" },
	{ NULL,	NULL,			NULL }
};

static const stress_opt_t opts[] = {
	{ OPT_remap_mlock, "remap-mlock", TYPE_ID_BOOL, 0, 1, NULL },
	{ OPT_remap_pages, "remap-pages", TYPE_ID_SIZE_T, MIN_REMAP_PAGES, MAX_REMAP_PAGES, NULL },
	END_OPT,
};

#if defined(HAVE_REMAP_FILE_PAGES) &&	\
    !defined(STRESS_ARCH_SPARC)

typedef uint16_t stress_mapdata_t;

static inline void *stress_get_umapped_addr(const size_t sz)
{
	void *addr;

	addr = mmap(NULL, sz, PROT_READ | PROT_WRITE,
		MAP_SHARED | MAP_ANONYMOUS, -1, 0);
	if (addr == MAP_FAILED)
		return NULL;

	(void)stress_munmap_force(addr, sz);
	return addr;
}

/*
 *  check_order()
 *	check page order
 */
static bool OPTIMIZE3 check_order(
	stress_args_t *args,
	const size_t stride,
	const stress_mapdata_t *data,
	const size_t remap_pages,
	const size_t *order,
	const char *ordering)
{
	size_t i;

	for (i = 0; i < remap_pages; i++) {
		if (UNLIKELY(data[i * stride] != order[i])) {
			pr_fail("%s: remap %s order pages failed\n",
				args->name, ordering);
			return true;
		}
	}
	return false;
}

/*
 *  remap_order()
 *	remap based on given order
 */
static int OPTIMIZE3 remap_order(
	stress_args_t *args,
	const size_t stride,
	stress_mapdata_t *data,
	const size_t remap_pages,
	const size_t *order,
	const size_t page_size,
	bool remap_mlock,
	double *duration,
	double *count)
{
	size_t i;

	for (i = 0; i < remap_pages; i++) {
		double t;
		int ret;
#if defined(HAVE_MLOCK)
		int lock_ret;

		lock_ret = mlock(data + (i * stride), page_size);
#endif
		t = stress_time_now();
		ret = remap_file_pages(data + (i * stride), page_size,
			0, order[i], 0);
		if (LIKELY(ret == 0)) {
			(*duration) += stress_time_now() - t;
			(*count) += 1.0;
		}
#if defined(HAVE_MLOCK)
		if ((lock_ret == 0) && (!remap_mlock)) {
			(void)munlock(data + (i * stride), page_size);
		}
		if (ret) {
			/* mlocked remap failed? try unlocked remap */
			ret = remap_file_pages(data + (i * stride), page_size,
				0, order[i], 0);
		}
#else
		(void)remap_mlock;
#endif
		if (UNLIKELY(ret < 0)) {
			pr_inf_skip("%s: remap_file_pages failed, errno=%d (%s), skipping stressor\n",
				args->name, errno, strerror(errno));
			return -1;
		}
	}

	return 0;
}

/*
 *  stress_remap
 *	stress page remapping
 */
static int stress_remap(stress_args_t *args)
{
	stress_mapdata_t *data;
	size_t *order;
	size_t remap_pages = DEFAULT_REMAP_PAGES;
	uint8_t *unmapped, *mapped;
	const size_t page_size = args->page_size;
	const size_t stride = page_size / sizeof(*data);
	size_t data_size, order_size, i, mapped_size = page_size + page_size;
	double duration = 0.0, count = 0.0, rate = 0.0;
	bool remap_mlock = false;
	int rc = EXIT_SUCCESS;

	(void)stress_get_setting("remap-mlock", &remap_mlock);
	if (!stress_get_setting("remap-pages", &remap_pages)) {
		if (g_opt_flags & OPT_FLAGS_MINIMIZE)
			remap_pages = MIN_REMAP_PAGES;
	}

	if ((remap_pages & (remap_pages - 1)) != 0) {
		(void)pr_inf("%s: value for option --remap-pages %zu must be a power of 2, falling back to using default %d\n",
			args->name, remap_pages, DEFAULT_REMAP_PAGES);
		remap_pages = DEFAULT_REMAP_PAGES;
	}

	data_size = remap_pages * page_size;
	data = (stress_mapdata_t *)stress_mmap_populate(NULL, data_size,
			PROT_READ | PROT_WRITE,
			MAP_SHARED | MAP_ANONYMOUS, -1, 0);
	if (data == MAP_FAILED) {
		pr_inf_skip("%s: failed to mmap %zu bytes "
			"(%zu pages)%s, errno=%d (%s), skipping stressor\n",
			args->name, data_size, remap_pages,
			stress_get_memfree_str(),
			errno, strerror(errno));
		return EXIT_NO_RESOURCE;
	}
	stress_set_vma_anon_name(data, data_size, "remap-pages");
	if (remap_mlock)
		(void)shim_mlock(data, data_size);

	order_size = remap_pages * sizeof(*order);
	order = (size_t *)stress_mmap_populate(NULL, order_size,
			PROT_READ | PROT_WRITE,
			MAP_SHARED | MAP_ANONYMOUS, -1, 0);
	if (order == MAP_FAILED) {
		pr_inf_skip("%s: failed to mmap %zu bytes%s, errno=%d (%s), "
			"skipping stressor\n", args->name, order_size,
			stress_get_memfree_str(), errno, strerror(errno));
		(void)munmap((void *)data, data_size);
		return EXIT_NO_RESOURCE;
	}
	stress_set_vma_anon_name(order, order_size, "remap-ordering");
	if (remap_mlock)
		(void)shim_mlock(order, order_size);

	for (i = 0; i < remap_pages; i++)
		data[i * stride] = (stress_mapdata_t)i;

	unmapped = stress_get_umapped_addr(page_size);
	mapped = (uint8_t *)stress_mmap_populate(NULL, mapped_size,
			PROT_READ | PROT_WRITE,
			MAP_SHARED | MAP_ANONYMOUS, -1, 0);
	if (mapped != MAP_FAILED) {
		stress_set_vma_anon_name(mapped, mapped_size, "mapped-data");
		if (remap_mlock)
			(void)shim_mlock(mapped, mapped_size);
		/*
		 * attempt to unmap last page so we know there
		 * is an unmapped page following the
		 * mapped address space
		 */
		if (munmap((void *)(mapped + page_size), page_size) == 0) {
			mapped_size = page_size;
		} else {
			/* failed */
			(void)munmap((void *)mapped, mapped_size);
			mapped_size = 0;
			mapped = NULL;
		}
	} else {
		/* we tried and failed */
		mapped = NULL;
	}

	stress_set_proc_state(args->name, STRESS_STATE_SYNC_WAIT);
	stress_sync_start_wait(args);
	stress_set_proc_state(args->name, STRESS_STATE_RUN);

	do {
		/* Reverse pages */
		for (i = 0; i < remap_pages; i++)
			order[i] = remap_pages - 1 - i;

		if (UNLIKELY(remap_order(args, stride, data, remap_pages, order, page_size, remap_mlock, &duration, &count) < 0)) {
			rc = EXIT_NO_RESOURCE;
			break;
		}
		if (UNLIKELY(check_order(args, stride, data, remap_pages, order, "reverse"))) {
			rc = EXIT_FAILURE;
			break;
		}

		/* random order pages */
PRAGMA_UNROLL_N(4)
		for (i = 0; i < remap_pages; i++)
			order[i] = i;
PRAGMA_UNROLL_N(4)
		for (i = 0; i < remap_pages; i++) {
			size_t tmp, j = stress_mwc16() & (remap_pages - 1);

			tmp = order[i];
			order[i] = order[j];
			order[j] = tmp;
		}

		if (UNLIKELY(remap_order(args, stride, data, remap_pages, order, page_size, remap_mlock, &duration, &count)) < 0) {
			rc = EXIT_NO_RESOURCE;
			break;
		}
		if (UNLIKELY(check_order(args, stride, data, remap_pages, order, "random"))) {
			rc = EXIT_FAILURE;
			break;
		}

		/* all mapped to 1 page */
PRAGMA_UNROLL_N(4)
		for (i = 0; i < remap_pages; i++)
			order[i] = 0;
		if (UNLIKELY(remap_order(args, stride, data, remap_pages, order, page_size, remap_mlock, &duration, &count) < 0)) {
			rc = EXIT_NO_RESOURCE;
			break;
		}
		if (UNLIKELY(check_order(args, stride, data, remap_pages, order, "all-to-1"))) {
			rc = EXIT_FAILURE;
			break;
		}

		/* reorder pages back again */
PRAGMA_UNROLL_N(4)
		for (i = 0; i < remap_pages; i++)
			order[i] = i;
		if (UNLIKELY(remap_order(args, stride, data, remap_pages, order, page_size, remap_mlock, &duration, &count) < 0)) {
			rc = EXIT_NO_RESOURCE;
			break;
		}
		if (UNLIKELY(check_order(args, stride, data, remap_pages, order, "forward"))) {
			rc = EXIT_FAILURE;
			break;
		}

		/*
		 *  exercise some illegal remapping calls
		 */
		if (unmapped) {
			VOID_RET(int, remap_file_pages((void *)unmapped, page_size, 0, 0, 0));

			/* Illegal flags */
			VOID_RET(int, remap_file_pages((void *)unmapped, page_size, 0, 0, ~0));

			/* Invalid prot */
			VOID_RET(int, remap_file_pages((void *)unmapped, page_size, ~0, order[0], 0));
		}
		if (mapped) {
			VOID_RET(int, remap_file_pages((void *)(mapped + page_size), page_size, 0, 0, 0));

			/* Illegal flags */
			VOID_RET(int, remap_file_pages((void *)(mapped + page_size), page_size, 0, 0, ~0));

			/* Invalid prot */
			VOID_RET(int, remap_file_pages((void *)(mapped + page_size), page_size, ~0, order[0], 0));
		}
		stress_bogo_inc(args);
	} while (stress_continue(args));

	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);

	rate = (count > 0.0) ? duration / count : 0.0;
	stress_metrics_set(args, 0, "nanosecs per page remap",
		rate * 1000000000, STRESS_METRIC_HARMONIC_MEAN);

	(void)munmap((void *)order, order_size);
	(void)munmap((void *)data, data_size);
	if (mapped)
		(void)munmap((void *)mapped, mapped_size);
	if (unmapped)
		(void)munmap((void *)unmapped, page_size);

	return rc;
}

const stressor_info_t stress_remap_info = {
	.stressor = stress_remap,
	.opts = opts,
	.classifier = CLASS_MEMORY | CLASS_OS,
	.verify = VERIFY_ALWAYS,
	.help = help
};
#else
const stressor_info_t stress_remap_info = {
	.stressor = stress_unimplemented,
	.opts = opts,
	.classifier = CLASS_MEMORY | CLASS_OS,
	.verify = VERIFY_ALWAYS,
	.help = help,
	.unimplemented_reason = "built without remap_file_pages() or unsupported for SPARC Linux"
};
#endif
