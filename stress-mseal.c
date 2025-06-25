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
#include "core-shim.h"
#include "core-mmap.h"

static const stress_help_t help[] = {
	{ NULL,	"mseal N",	"start N workers exercising sealing of mmap'd memory" },
	{ NULL,	"mseal-ops N",	"stop mseal workers after N bogo mseal operations" },
	{ NULL,	NULL,		NULL }
};

typedef int (*mseal_func_t)(stress_args_t *args);

static void *mapping;		/* mmap of 2 pages */
static size_t mapping_size;	/* size in bytes of 2 pages */
static void *no_mapping;	/* unmapped non-mapped 2 pages */
static double mseal_duration;	/* mseal execution duration (secs) */
static double mseal_count;	/* mseal call count */

static void stress_mseal_mapping_size(size_t *size)
{
	if (*size == 0)
		*size = stress_get_page_size() * 2;
}

static int stress_mseal_expect_addr(
	stress_args_t *args,
	void *addr,
	char *msg,
	void *expect_addr,
	int expect_errno)
{
	if (LIKELY((addr == expect_addr) && (errno == expect_errno)))
		return 0;
	pr_fail("%s: %s, returned errno %d (%s), expected errno %d (%s)\n",
		args->name, msg, errno, strerror(errno),
		expect_errno, strerror(expect_errno));
	return -1;
}

static int stress_mseal_expect_error(
	stress_args_t *args,
	int ret,
	char *msg,
	int expect_ret,
	int expect_errno)
{
	if (LIKELY((ret == expect_ret) && (errno == expect_errno)))
		return 0;
	pr_fail("%s: %s, returned errno %d (%s), expected errno %d (%s)\n",
		args->name, msg, errno, strerror(errno),
		expect_errno, strerror(expect_errno));
	return -1;
}

#if defined(HAVE_MADVISE) &&	\
    defined(MADV_DONTNEED)
static int stress_mseal_madvise_dontneed(stress_args_t *args)
{
	return stress_mseal_expect_error(args, madvise(mapping, mapping_size, MADV_DONTNEED),
		"madvise() using MADV_DONTNEED", -1, EPERM);
}
#endif

#if defined(HAVE_MREMAP) &&	\
    NEED_GLIBC(2, 4, 0)
static int stress_mseal_mremap_size(stress_args_t *args)
{
	return stress_mseal_expect_addr(args,
		mremap(mapping, mapping_size, mapping_size / 2, 0),
		"mremap() unexpectedly succeeded", MAP_FAILED, EPERM);
}
#endif

#if defined(HAVE_MREMAP) &&	\
    defined(MREMAP_FIXED) &&	\
    defined(MREMAP_MAYMOVE) &&	\
    NEED_GLIBC(2, 4, 0)
static int stress_mseal_mremap_addr(stress_args_t *args)
{
	return stress_mseal_expect_addr(args,
		mremap(mapping, mapping_size, mapping_size, MREMAP_FIXED | MREMAP_MAYMOVE, no_mapping),
		"mremap() unexpectedly succeeded", MAP_FAILED, EPERM);
}
#endif

static int stress_mseal_munmap(stress_args_t *args)
{
	return stress_mseal_expect_error(args, munmap(mapping, mapping_size),
		"munmap()", -1, EPERM);
}

#if defined(HAVE_MPROTECT) &&	\
    defined(PROT_NONE)
static int stress_mseal_mprotect_none(stress_args_t *args)
{
	return stress_mseal_expect_error(args, mprotect(mapping, mapping_size, PROT_NONE),
		"mprotect() using PROT_NONE", -1, EPERM);
}
#endif

#if defined(HAVE_MPROTECT) &&	\
    defined(PROT_READ)
static int stress_mseal_mprotect_ro(stress_args_t *args)
{
	return stress_mseal_expect_error(args, mprotect(mapping, mapping_size, PROT_READ),
		"mprotect() using PROT_READ", -1, EPERM);
}
#endif

#if defined(HAVE_MPROTECT) &&	\
    defined(PROT_WRITE)
static int stress_mseal_mprotect_wo(stress_args_t *args)
{
	return stress_mseal_expect_error(args, mprotect(mapping, mapping_size, PROT_WRITE),
		"mprotect() using PROT_WRITE", -1, EPERM);
}
#endif

#if defined(MAP_FIXED)
static int stress_mseal_mmap_fixed(stress_args_t *args)
{
	return stress_mseal_expect_addr(args,
		mmap(mapping, mapping_size * 2, PROT_READ, MAP_ANONYMOUS | MAP_PRIVATE | MAP_FIXED, -1, 0),
		"mmap fixed on existing memory mapping", MAP_FAILED, EPERM);
}
#endif

static int stress_mseal_unmapped_first_page(stress_args_t *args)
{
	if (no_mapping != MAP_FAILED) {
		const size_t size = mapping_size / 2;

		return stress_mseal_expect_error(args, shim_mseal(no_mapping, size, 0),
			"mseal of unmapped address unexpectedly succeeded", -1, ENOMEM);
	}
	return 0;
}

static int stress_mseal_unmapped_last_page(stress_args_t *args)
{
	if (no_mapping != MAP_FAILED) {
		const size_t size = mapping_size / 2;

		return stress_mseal_expect_error(args, shim_mseal((void *)((uint8_t *)no_mapping + size), size, 0),
			"mseal of unmapped address unexpectedly succeeded", -1, ENOMEM);
	}
	return 0;
}

static int stress_mseal_unmapped_pages(stress_args_t *args)
{
	if (no_mapping != MAP_FAILED) {
		return stress_mseal_expect_error(args, shim_mseal(no_mapping, mapping_size, 0),
			"mseal of unmapped address unexpectedly succeeded", -1, ENOMEM);
	}
	return 0;
}

static int stress_mseal_mapped_first_page(stress_args_t *args)
{
	const size_t size = mapping_size / 2;
	int ret;
	double t;

	t = stress_time_now();
	ret = shim_mseal(mapping, size, 0);
	if (LIKELY(ret == 0)) {
		mseal_duration += stress_time_now() - t;
		mseal_count += 1.0;
	}
	return stress_mseal_expect_error(args, ret,
		"mseal of msealed address unexpectedly failed", 0, 0);
}

static int stress_mseal_mapped_last_page(stress_args_t *args)
{
	const size_t size = mapping_size / 2;
	int ret;
	double t;

	t = stress_time_now();
	ret = shim_mseal((void *)((uint8_t *)mapping + size), size, 0);
	if (LIKELY(ret == 0)) {
		mseal_duration += stress_time_now() - t;
		mseal_count += 1.0;
	}

	return stress_mseal_expect_error(args, ret,
		"mseal of msealed address unexpectedly failed", 0, 0);
}

static int stress_mseal_mapped_pages(stress_args_t *args)
{
	int ret;
	double t;

	t = stress_time_now();
	ret = shim_mseal(mapping, mapping_size, 0);
	if (LIKELY(ret == 0)) {
		mseal_duration += stress_time_now() - t;
		mseal_count += 1.0;
	}

	return stress_mseal_expect_error(args, ret,
		"mseal of msealed address unexpectedly failed", 0, 0);
}

static void *stress_mseal_mmap(const size_t size)
{
	return mmap(NULL, size, PROT_READ, MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
}

static int stress_mseal_supported(const char *name)
{
	int ret;

	stress_mseal_mapping_size(&mapping_size);
	mapping = stress_mseal_mmap(mapping_size);
	if (mapping == MAP_FAILED) {
		pr_inf_skip("%s: cannot check if mseal is supported, memory mapping of "
			"%zd bytes failed, skipping stressor\n", name, mapping_size);
		return -1;
	}

	ret = shim_mseal(mapping, mapping_size, 0);
	if (ret < 0) {
		if (errno == ENOSYS) {
			pr_inf_skip("%s: mseal system call not supported, "
				"skipping stressor\n", name);
		} else {
			pr_inf_skip("%s: mseal of memory mapped pages failed, errno=%d (%s), "
				"skipping stressor\n", name,
				errno, strerror(errno));
		}
		return -1;
	}

	return 0;
}

static const mseal_func_t mseal_funcs[] = {
#if defined(HAVE_MADVISE) &&	\
    defined(MADV_DONTNEED)
	stress_mseal_madvise_dontneed,
#endif
#if defined(HAVE_MREMAP) &&	\
    NEED_GLIBC(2, 4, 0)
	stress_mseal_mremap_size,
#endif
#if defined(HAVE_MREMAP) &&	\
    defined(MREMAP_FIXED) &&	\
    defined(MREMAP_MAYMOVE) &&	\
    NEED_GLIBC(2, 4, 0)
	stress_mseal_mremap_addr,
#endif
	stress_mseal_munmap,
#if defined(HAVE_MPROTECT) &&	\
    defined(PROT_NONE)
	stress_mseal_mprotect_none,
#endif
#if defined(HAVE_MPROTECT) &&	\
    defined(PROT_READ)
	stress_mseal_mprotect_ro,
#endif
#if defined(HAVE_MPROTECT) &&	\
    defined(PROT_WRITE)
	stress_mseal_mprotect_wo,
#endif
#if defined(MAP_FIXED)
	stress_mseal_mmap_fixed,
#endif
	stress_mseal_unmapped_first_page,
	stress_mseal_unmapped_last_page,
	stress_mseal_unmapped_pages,
	stress_mseal_mapped_first_page,
	stress_mseal_mapped_last_page,
	stress_mseal_mapped_pages,
};

/*
 *  stress_mseal
 *	stress mseal
 */
static int stress_mseal(stress_args_t *args)
{
	bool keep_running = true;
	double rate;

	mseal_duration = 0.0;
	mseal_count = 0.0;

	stress_mseal_mapping_size(&mapping_size);
	if (!mapping || (mapping == MAP_FAILED))
		mapping = stress_mseal_mmap(mapping_size);
	if (mapping == MAP_FAILED) {
		pr_inf_skip("%s: mmap of a page failed, errno=%d (%s), "
			"skipping stressor\n", args->name,
			errno, strerror(errno));
		return EXIT_FAILURE;
	}
	stress_set_vma_anon_name(mapping, mapping_size, "mapping-data");

	/*
	 *  Map and ummap some pages, if this failed no_mapping
	 *  is MAP_FAILED, otherwise it is a valid non-mapped address
	 */
	no_mapping = mmap(NULL, mapping_size, PROT_READ | PROT_WRITE,
			MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
	if (no_mapping != MAP_FAILED)
		if (munmap(no_mapping, mapping_size) < 0)
			no_mapping = MAP_FAILED;

	stress_set_proc_state(args->name, STRESS_STATE_SYNC_WAIT);
	stress_sync_start_wait(args);
	stress_set_proc_state(args->name, STRESS_STATE_RUN);

	do {
		size_t i;

		for (i = 0; i < SIZEOF_ARRAY(mseal_funcs); i++) {
			errno = 0;
			if (UNLIKELY(mseal_funcs[i](args) < 0))
				keep_running = false;
		}
		stress_bogo_inc(args);
	} while (keep_running && stress_continue(args));

	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);

	rate = (mseal_duration > 0.0) ? mseal_count / mseal_duration : 0.0;
	stress_metrics_set(args, 0, "mseal calls per sec", rate, STRESS_METRIC_HARMONIC_MEAN);

	/* This will fail if mseal works, ignore failure */
	(void)munmap(mapping, mapping_size);

	return EXIT_SUCCESS;
}

const stressor_info_t stress_mseal_info = {
	.stressor = stress_mseal,
	.supported = stress_mseal_supported,
	.classifier = CLASS_VM | CLASS_OS,
	.verify = VERIFY_ALWAYS,
	.help = help
};
