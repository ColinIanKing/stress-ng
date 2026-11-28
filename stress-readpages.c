/*
 * Copyright (C) 2026      Colin Ian King.
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
#include "core-asm-generic.h"
#include "core-builtin.h"
#include "core-cpu-cache.h"
#include "core-madvise.h"
#include "core-mmap.h"
#include "core-sort.h"

static const stress_help_t help[] = {
	{ NULL, "readpages",          "start N workers reading their readable pages" },
	{ NULL,	"readpages-ops N",    "stop after N bogo read operations" },
	{ NULL, "readpages-prefetch", "prefetch data (maybe a no-op on some systems)" },
	{ NULL, "readpages-order M",  "select page read order [ forward | random | reverse ]" },
	{ NULL,	NULL,                 NULL }
};

#define STRESS_READPAGE_SIGSEGV		(0x01)

#define READPAGES_ORDER_FORWARD		(0)
#define READPAGES_ORDER_RANDOM		(1)
#define READPAGES_ORDER_REVERSE		(2)

static const char * const readpages_ordering[] = {
	"forward",
	"random",
	"reverse",
};

static const char *stress_readpages_order(const size_t i)
{
        return (i < SIZEOF_ARRAY(readpages_ordering)) ? readpages_ordering[i] : NULL;
}

static const stress_opt_t opts[] = {
	{ OPT_readpages_prefetch, "readpages-prefetch", TYPE_ID_BOOL, 0, 1, NULL },
	{ OPT_readpages_order,    "readpages-order",    TYPE_ID_SIZE_T_METHOD, 0, 0, stress_readpages_order },
	END_OPT,
};

#if defined(__linux__)

typedef void (*readpages_func_t)(const size_t page_size, const stress_mmap_t *map);

static volatile stress_mmap_t *stress_readpages_current_mmap;

#if defined(HAVE_SIGLONGJMP)
static volatile bool do_jmp = true;
static sigjmp_buf jmp_env;
#endif

#define PREFETCH_OFFSET		(128)
#define PREFETCH_RD		(0)
#define PREFETCH_LOCALIITY	(3)

#define PTR_OFFSET(ptr)		(void *)(((uintptr_t)ptr) + PREFETCH_OFFSET)

#if defined(HAVE_INT128_T)
/*
 *  stress_readpages_mapping_prefetch()
 *	read mapped pages between begin and end with prefetching
 */
static void OPTIMIZE3 stress_readpages_mapping_prefetch(
	const size_t page_size,
	const stress_mmap_t *mmap)
{
	register size_t i;
	register const uintptr_t *addrs = mmap->addrs;

	for (i = 0; i < mmap->n_pages; i++) {
		register volatile const __uint128_t *ptr = (__uint128_t *)addrs[i];
		register volatile const __uint128_t *ptr_end = (__uint128_t *)(addrs[i] + page_size);

		while (ptr < ptr_end) {
			shim_builtin_prefetch(PTR_OFFSET(ptr), PREFETCH_RD, PREFETCH_LOCALIITY);
			(void)*(ptr + 0);
			(void)*(ptr + 1);
			(void)*(ptr + 2);
			(void)*(ptr + 3);
			stress_asm_mb();

			shim_builtin_prefetch(PTR_OFFSET(ptr + 4), PREFETCH_RD, PREFETCH_LOCALIITY);
			(void)*(ptr + 4);
			(void)*(ptr + 5);
			(void)*(ptr + 6);
			(void)*(ptr + 7);
			stress_asm_mb();

			ptr += 8;
		}
	}
}

/*
 *  stress_readpages_mapping()
 *	read mapped pages between begin and end
 */
static void OPTIMIZE3 stress_readpages_mapping(
	const size_t page_size,
	const stress_mmap_t *mmap)
{
	register size_t i;
	register const uintptr_t *addrs = mmap->addrs;

	for (i = 0; i < mmap->n_pages; i++) {
		register volatile const __uint128_t *ptr = (__uint128_t *)addrs[i];
		register volatile const __uint128_t *ptr_end = (__uint128_t *)(addrs[i] + page_size);

		while (ptr < ptr_end) {
			(void)*(ptr + 0);
			(void)*(ptr + 1);
			(void)*(ptr + 2);
			(void)*(ptr + 3);
			(void)*(ptr + 4);
			(void)*(ptr + 5);
			(void)*(ptr + 6);
			(void)*(ptr + 7);

			ptr += 8;
		}
	}
}
#else

/*
 *  stress_readpages_mapping_prefetch()
 *	read mapped pages between begin and end with prefetching
 */
static void OPTIMIZE3 stress_readpages_mapping_prefetch(
	const size_t page_size,
	const stress_mmap_t *mmap)
{
	register size_t i;
	register const uintptr_t *addrs = mmap->addrs;

	for (i = 0; i < mmap->n_pages; i++) {
		register volatile const uint64_t *ptr = (uint64_t *)addrs[i];
		register volatile const uint64_t *ptr_end = (uint64_t *)(addrs[i] + page_size);

		while (ptr < ptr_end) {
			shim_builtin_prefetch(PTR_OFFSET(ptr), PREFETCH_RD, PREFETCH_LOCALIITY);
			(void)*(ptr + 0);
			(void)*(ptr + 1);
			(void)*(ptr + 2);
			(void)*(ptr + 3);
			(void)*(ptr + 4);
			(void)*(ptr + 5);
			(void)*(ptr + 6);
			(void)*(ptr + 7);
			stress_asm_mb();

			shim_builtin_prefetch(PTR_OFFSET(ptr + 8), PREFETCH_RD, PREFETCH_LOCALIITY);
			(void)*(ptr + 8);
			(void)*(ptr + 9);
			(void)*(ptr + 10);
			(void)*(ptr + 11);
			(void)*(ptr + 12);
			(void)*(ptr + 13);
			(void)*(ptr + 14);
			(void)*(ptr + 15);
			stress_asm_mb();

			ptr += 16;
		}
	}
}

/*
 *  stress_readpages_mapping()
 *	read mapped pages between begin and end
 */
static void OPTIMIZE3 stress_readpages_mapping(
	const size_t page_size,
	const stress_mmap_t *mmap)
{
	register size_t i;
	register const uintptr_t *addrs = mmap->addrs;

	for (i = 0; i < mmap->n_pages; i++) {
		register volatile const uint64_t *ptr = (uint64_t *)addrs[i];
		register volatile const uint64_t *ptr_end = (uint64_t *)(addrs[i] + page_size);

		while (ptr < ptr_end) {
			(void)*(ptr + 0);
			(void)*(ptr + 1);
			(void)*(ptr + 2);
			(void)*(ptr + 3);
			(void)*(ptr + 4);
			(void)*(ptr + 5);
			(void)*(ptr + 6);
			(void)*(ptr + 7);
			(void)*(ptr + 8);
			(void)*(ptr + 9);
			(void)*(ptr + 10);
			(void)*(ptr + 11);
			(void)*(ptr + 12);
			(void)*(ptr + 13);
			(void)*(ptr + 14);
			(void)*(ptr + 15);

			ptr += 16;
		}
	}
}
#endif

STRESS_SHUFFLE(stress_readpages_page_shuffle, uintptr_t)

/*
 *  stress_readpages_order_set()
 *	set page ordering based on readpages_order: forward, random
 *	or reverse.
 */
static void stress_readpages_order_set(
	stress_mmaps_t *mmaps,
	const size_t page_size,
	const size_t readpages_order)
{
	register uintptr_t ptr;
	register size_t i;

	switch (readpages_order) {
	default:
	case READPAGES_ORDER_FORWARD:
		/* addresses are already in page order */
		break;
	case READPAGES_ORDER_RANDOM:
		/* randomly shuffle addresses */
		for (i = 0; i < mmaps->n_mmaps; i++) {
			stress_mmap_t *mmap = &mmaps->mmaps[i];

			stress_readpages_page_shuffle(mmap->addrs, mmap->n_pages);
		}
		break;
	case READPAGES_ORDER_REVERSE:
		/* addresses in reverse page order */
		for (i = 0; i < mmaps->n_mmaps; i++) {
			register size_t j;
			stress_mmap_t *mmap = &mmaps->mmaps[i];

			for (j = 0, ptr = mmap->mmap_end; j < mmap->n_pages; j++) {
				ptr -= page_size;
				mmap->addrs[i] = ptr;
			}
		}
		break;
	}
}

static size_t OPTIMIZE3 stress_readpages_count(
	const stress_mmaps_t *mmaps)
{
	size_t n_pages = 0;
	register size_t i;

	for (i = 0; i < mmaps->n_mmaps; i++)
		n_pages += mmaps->mmaps[i].n_pages;

	return n_pages;
}

/*
 *  stress_readpages_exercise()
 */
static size_t OPTIMIZE3 stress_readpages_exercise(
	const size_t page_size,
	readpages_func_t readpages_func,
	const volatile stress_mmaps_t *mmaps)
{
	size_t n_pages = 0;
	register size_t i;

	for (i = 0; i < mmaps->n_mmaps; i++) {
		stress_mmap_t *mmap = &mmaps->mmaps[i];

#if defined(HAVE_SIGLONGJMP)
		if (mmap->flags & STRESS_READPAGE_SIGSEGV)
			continue;
#endif
		stress_readpages_current_mmap = mmap;
		readpages_func(page_size, mmap);
		stress_readpages_current_mmap = NULL;

		n_pages += mmap->n_pages;
	}
	return n_pages;
}

STRESS_SHUFFLE(stress_readpages_mmap_shuffle, stress_mmap_t)

#if defined(HAVE_SIGLONGJMP)
/*
 *  stress_readpage_segv_handler()
 *  	handler for read-only mappings that caused a SIGSEGV
 */
static void stress_readpage_segv_handler(int signum)
{
	if (stress_readpages_current_mmap)
		stress_readpages_current_mmap->flags |= STRESS_READPAGE_SIGSEGV;

	stress_readpages_current_mmap = NULL;
	stress_signal_siglongjmp_flag(signum, jmp_env, 1, &do_jmp);
}
#endif

/*
 *  stress_readpages()
 *	stress by forking and exiting
 */
static int stress_readpages(stress_args_t *args)
{
#if defined(HAVE_SIGLONGJMP)
        struct sigaction old_action;
	size_t segv_mappings;
	int ret;
#endif
	const size_t page_size = args->page_size;
	double duration;
	stress_mmaps_t mmaps;
	CLOBBERED double total_pages = 0.0;
	CLOBBERED double t;
	double rate;
	char msg[64];
	const size_t data_size = 4 * STRESS_MB;
	bool readpages_prefetch = false;
	CLOBBERED readpages_func_t readpages_func;
	void *data;
	CLOBBERED size_t max_pages = 0;
	const pid_t pid = getpid();
	CLOBBERED int rc = EXIT_SUCCESS;
	int advice[3];
	size_t idx;
	size_t i;
	size_t readpages_order = READPAGES_ORDER_FORWARD;

	(void)stress_setting_get("readpages-order", &readpages_order);

	stress_readpages_current_mmap = NULL;
	if (stress_mmaps_get(-1, page_size, STRESS_MMAP_READ, true, true, &mmaps) < 0) {
		pr_inf_skip("%s: failed to read process mappings, skipping stressor\n",
			args->name);
		return EXIT_NO_RESOURCE;
	}
	if (stress_readpages_count(&mmaps) == 0) {
		pr_inf_skip("%s: no mappings are readable, skipping stressor\n", args->name);
		rc = EXIT_NO_RESOURCE;
		goto tidy_mmaps;
	}

	stress_readpages_order_set(&mmaps, page_size, readpages_order);

	(void)stress_setting_get("readpages-prefetch", &readpages_prefetch);
	readpages_func = readpages_prefetch ?
				stress_readpages_mapping_prefetch :
				stress_readpages_mapping;

	data = stress_mmap_populate(NULL, data_size, PROT_READ | PROT_WRITE,
			MAP_SHARED | MAP_ANONYMOUS, -1, 0);
	if (data != MAP_FAILED) {
		stress_mmap_set(data, data_size, page_size);
		if (stress_instance_zero(args))
			stress_memory_usage_get(args, data_size, data_size * args->instances);
	}

	idx = 0;
#if defined(MADV_HUGEPAGE)
	advice[idx++] = MADV_HUGEPAGE;
#endif
#if defined(MADV_COLLAPSE)
	advice[idx++] = MADV_COLLAPSE;
#endif
#if defined(MADV_POPULATE_READ)
	advice[idx++] = MADV_POPULATE_READ;
#endif
	for (i = 0; i < idx; i++)
		stress_madvise_pid_all_pages(pid, &advice[i], 1);

	stress_proc_state_set(args->name, STRESS_STATE_SYNC_WAIT);
	stress_sync_start_wait(args);
	stress_proc_state_set(args->name, STRESS_STATE_RUN);

	t = stress_time_now();
#if defined(HAVE_SIGLONGJMP)
	/* ret = 1 means we've hit a SIGSEGV, fall back into do loop */
        ret = sigsetjmp(jmp_env, 1);
	if (ret) {
		if (!stress_continue(args))
			goto tidy;
	} else {
		if (stress_signal_handler(args->name, SIGSEGV, stress_readpage_segv_handler, &old_action) < 0) {
			rc = EXIT_NO_RESOURCE;
			goto tidy;
		}
        }
	do_jmp = true;
#endif

	do {
		const size_t pages = stress_readpages_exercise(page_size, readpages_func, &mmaps);

		total_pages += (double)pages;
		if (pages > max_pages)
			max_pages = pages;
		stress_readpages_mmap_shuffle(mmaps.mmaps, mmaps.n_mmaps);
		stress_bogo_inc(args);
	} while (stress_continue(args));
	duration = stress_time_now() - t;

#if defined(HAVE_SIGLONGJMP)
	do_jmp = false;
	(void)stress_signal_restore(args->name, SIGSEGV, &old_action);
#endif

	rate = (duration > 0.0) ? total_pages / duration : 0.0;
	(void)snprintf(msg, sizeof(msg), "%zuK pages read per second", page_size >> 10);
	stress_metrics_set(args, msg, rate, STRESS_METRIC_GEOMETRIC_MEAN);
	rate *= (double)page_size;
	stress_metrics_set(args, "MB read per second", rate / (double)STRESS_MB, STRESS_METRIC_GEOMETRIC_MEAN);
	stress_metrics_set(args, "unique pages were read", (double)max_pages, STRESS_METRIC_GEOMETRIC_MEAN);

#if defined(HAVE_SIGLONGJMP)
	for (segv_mappings = 0, i = 0; i < mmaps.n_mmaps; i++) {
		segv_mappings += (mmaps.mmaps[i].flags & STRESS_READPAGE_SIGSEGV) ? 1 : 0;
	}
	rate = (mmaps.n_mmaps > 0) ? (100.0 * (double)segv_mappings) / (double)mmaps.n_mmaps : 0.0;
	stress_metrics_set(args, "% of mappings generated SIGSEGV", rate, STRESS_METRIC_HARMONIC_MEAN);
#endif

#if defined(HAVE_SIGLONGJMP)
tidy:
#endif
	stress_proc_state_set(args->name, STRESS_STATE_DEINIT);

	if (data != MAP_FAILED)
		(void)munmap(data, data_size);

tidy_mmaps:
	stress_mmaps_free(&mmaps);

	return rc;
}

static const stress_exercises_t exercises[] = {
	STRESS_EX_FEATURE("d-cache-read-miss"),
	STRESS_EX_FEATURE("memory-bound"),
	STRESS_EX_FEATURE("memory-bus"),
	STRESS_EX_FEATURE("memory-loads"),
	STRESS_EX_FEATURE("tlb"),

	STRESS_EX_END,
};

const stressor_info_t stress_readpages_info = {
	.stressor = stress_readpages,
	.classifier = CLASS_CPU_CACHE | CLASS_MEMORY | CLASS_VM,
	.verify = VERIFY_OPTIONAL,
	.help = help,
	.opts = opts,
	.exercises = exercises,
};

#else

const stressor_info_t stress_readpages_info = {
	.stressor = stress_unimplemented,
	.classifier = CLASS_CPU_CACHE | CLASS_MEMORY | CLASS_VM,
	.verify = VERIFY_OPTIONAL,
	.help = help,
	.opts = opts,
	.unimplemented_reason = "built without linux /proc/self/maps",
};

#endif
