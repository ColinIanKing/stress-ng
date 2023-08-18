// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2013-2021 Canonical, Ltd.
 * Copyright (C) 2022-2023 Colin Ian King.
 *
 */
#include "stress-ng.h"
#include "core-arch.h"

static const stress_help_t help[] = {
	{ NULL,	"remap N",		"start N workers exercising page remappings" },
	{ NULL,	"remap-mlock",		"attempt to mlock pages into memory" },
	{ NULL,	"remap-ops N",		"stop after N remapping bogo operations" },
	{ NULL,	"remap-pages N",	"specify N pages to remap (N must be power of 2)" },
	{ NULL,	NULL,			NULL }
};

static int stress_set_remap_mlock(const char *opt)
{
	return stress_set_setting_true("remap-mlock", opt);
}

static int stress_set_remap_pages(const char *opt)
{
        size_t remap_pages;

        remap_pages = (size_t)stress_get_uint64(opt);
        stress_check_range("remap-pages", (uint64_t)remap_pages, 1, 0x80000000);

	if ((remap_pages & (remap_pages - 1)) != 0) {
		(void)fprintf(stderr, "Value for option --remap-pages %zu must be a power of 2\n", remap_pages);
                longjmp(g_error_env, 1);
	}
        return stress_set_setting("remap-pages", TYPE_ID_SIZE_T, &remap_pages);
}

static const stress_opt_set_func_t opt_set_funcs[] = {
	{ OPT_remap_mlock,	stress_set_remap_mlock },
	{ OPT_remap_pages,	stress_set_remap_pages },
	{ 0,			NULL },
};

#if defined(HAVE_REMAP_FILE_PAGES) &&	\
    !defined(STRESS_ARCH_SPARC)

#define N_PAGES		(512)		/* Must be a power of 2 */

typedef uint16_t stress_mapdata_t;

static inline void *stress_get_umapped_addr(const size_t sz)
{
	void *addr;

	addr = mmap(NULL, sz, PROT_READ | PROT_WRITE,
		MAP_SHARED | MAP_ANONYMOUS, -1, 0);
	if (addr == MAP_FAILED)
		return NULL;

	(void)stress_munmap_retry_enomem(addr, sz);
	return addr;
}

/*
 *  check_order()
 *	check page order
 */
static void OPTIMIZE3 check_order(
	const stress_args_t *args,
	const size_t stride,
	const stress_mapdata_t *data,
	const size_t remap_pages,
	const size_t *order,
	const char *ordering)
{
	size_t i;
	bool failed;

	for (failed = false, i = 0; i < remap_pages; i++) {
		if (data[i * stride] != order[i]) {
			failed = true;
			break;
		}
	}
	if (failed)
		pr_fail("%s: remap %s order pages failed\n",
			args->name, ordering);
}

/*
 *  remap_order()
 *	remap based on given order
 */
static int OPTIMIZE3 remap_order(
	const stress_args_t *args,
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
static int stress_remap(const stress_args_t *args)
{
	stress_mapdata_t *data;
	size_t *order;
	size_t remap_pages = N_PAGES;
	uint8_t *unmapped, *mapped;
	const size_t page_size = args->page_size;
	const size_t stride = page_size / sizeof(*data);
	size_t data_size, order_size, i, mapped_size = page_size + page_size;
	double duration = 0.0, count = 0.0, rate = 0.0;
	bool remap_mlock = false;
	int rc = EXIT_SUCCESS;

	(void)stress_get_setting("remap-mlock", &remap_mlock);
	(void)stress_get_setting("remap-pages", &remap_pages);

	data_size = remap_pages * page_size;
	data = (stress_mapdata_t *)mmap(NULL, data_size, PROT_READ | PROT_WRITE,
			MAP_SHARED | MAP_ANONYMOUS, -1, 0);
	if (data == MAP_FAILED) {
		pr_inf_skip("%s: mmap failed to allocate %zu bytes "
			"(%zu pages), errno=%d (%s), skipping stressor\n",
			args->name, data_size, remap_pages,
			errno, strerror(errno));
		return EXIT_NO_RESOURCE;
	}
	if (remap_mlock)
		(void)shim_mlock(data, data_size);

	order_size = remap_pages * sizeof(*order);
	order = (size_t *)mmap(NULL, order_size, PROT_READ | PROT_WRITE,
			MAP_SHARED | MAP_ANONYMOUS, -1, 0);
	if (order == MAP_FAILED) {
		pr_inf_skip("%s: mmap failed to allocate %zu bytes, errno=%d (%s), "
			"skipping stressor\n", args->name, order_size,
			errno, strerror(errno));
		(void)munmap((void *)data, data_size);
		return EXIT_NO_RESOURCE;
	}
	if (remap_mlock)
		(void)shim_mlock(order, order_size);

	for (i = 0; i < remap_pages; i++)
		data[i * stride] = (stress_mapdata_t)i;

	unmapped = stress_get_umapped_addr(page_size);
	mapped = (uint8_t *)mmap(NULL, mapped_size, PROT_READ | PROT_WRITE,
			MAP_SHARED | MAP_ANONYMOUS, -1, 0);
	if (mapped != MAP_FAILED) {
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

	stress_set_proc_state(args->name, STRESS_STATE_RUN);

	do {
		/* Reverse pages */
		for (i = 0; i < remap_pages; i++)
			order[i] = remap_pages - 1 - i;

		if (UNLIKELY(remap_order(args, stride, data, remap_pages, order, page_size, remap_mlock, &duration, &count) < 0)) {
			rc = EXIT_NO_RESOURCE;
			break;
		}
		check_order(args, stride, data, remap_pages, order, "reverse");

		/* random order pages */
		for (i = 0; i < remap_pages; i++)
			order[i] = i;
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
		check_order(args, stride, data, remap_pages, order, "random");

		/* all mapped to 1 page */
		for (i = 0; i < remap_pages; i++)
			order[i] = 0;
		if (UNLIKELY(remap_order(args, stride, data, remap_pages, order, page_size, remap_mlock, &duration, &count) < 0)) {
			rc = EXIT_NO_RESOURCE;
			break;
		}
		check_order(args, stride, data, remap_pages, order, "all-to-1");

		/* reorder pages back again */
		for (i = 0; i < remap_pages; i++)
			order[i] = i;
		if (UNLIKELY(remap_order(args, stride, data, remap_pages, order, page_size, remap_mlock, &duration, &count) < 0)) {
			rc = EXIT_NO_RESOURCE;
			break;
		}
		check_order(args, stride, data, remap_pages, order, "forward");

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
	stress_metrics_set(args, 0, "nanosecs per page remap", rate * 1000000000);

	(void)munmap((void *)order, order_size);
	(void)munmap((void *)data, data_size);
	if (mapped)
		(void)munmap((void *)mapped, mapped_size);
	if (unmapped)
		(void)munmap((void *)unmapped, page_size);

	return rc;
}

stressor_info_t stress_remap_info = {
	.stressor = stress_remap,
	.opt_set_funcs = opt_set_funcs,
	.class = CLASS_MEMORY | CLASS_OS,
	.verify = VERIFY_ALWAYS,
	.help = help
};
#else
stressor_info_t stress_remap_info = {
	.stressor = stress_unimplemented,
	.opt_set_funcs = opt_set_funcs,
	.class = CLASS_MEMORY | CLASS_OS,
	.verify = VERIFY_ALWAYS,
	.help = help,
	.unimplemented_reason = "built without remap_file_pages() or unsupported for SPARC Linux"
};
#endif
