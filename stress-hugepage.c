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
#include "core-arch.h"
#include "core-builtin.h"
#include "core-mmap.h"
#include "core-pthread.h"

#define MIN_HUGEPAGE_NUM	(1)
#define MAX_HUGEPAGE_NUM	(256)
#define DEFAULT_HUGEPAGE_NUM	(2)

#define HUGEPAGE_METHOD_RANDOM	(0)
#define HUGEPAGE_METHOD_SEQ	(1)

typedef size_t (*stress_hugepage_offset_func_t)(const size_t n_pages);

/* hugepage mapping info */
typedef struct stress_hugepage_info {
	uint64_t *addr64;		/* mmap'd hugepage(s) */
	size_t size;			/* mmap'd size */
	size_t page_size;		/* size of a normal page */
	size_t n_normal_pages;		/* number of normal pages allocated */
	uint64_t main_madv_count;	/* madvise call count, main process */
	uint64_t pthread_madv_count;	/* madvise call count, pthread */
	uint64_t pthread_mprt_count;	/* mprotect call count, pthread */
	stress_hugepage_offset_func_t func; /* random/forward/reverse offset */
	bool dontneed;			/* --hugepage-dontneed option */
	bool remove;			/* --hugepage-remove option */
} stress_hugepage_info_t;

typedef struct stress_hugepage_size {
	const int flags;		/* hugepage mmap flag */
	const size_t size;		/* hugepage size (bytes) */
} stress_hugepage_size_t;

typedef struct stress_hugepage_method {
	const char *name;
	const stress_hugepage_offset_func_t func;
} stress_hugepage_method_t;

static const stress_help_t help[] = {
	{ NULL,	"hugepage N",	     "start N workers that split and merge pages on huge pages" },
	{ NULL, "hugepage-dontneed", "madvise MADV_DONTNEED on page size chunks of hugepage" },
	{ NULL, "hugepage-method",   "select method of splitting [ forward | random | reverse ]" },
	{ NULL,	"hugepage-num N",    "number of hugepages" },
	{ NULL,	"hugepage-ops N",    "stop hugepage workers after N bogo huge page rebuild operations" },
	{ NULL, "hugepage-remove",   "madvise MADV_REMOVE on page size chunks of hugepage" },
	{ NULL,	NULL,                NULL }
};

static size_t OPTIMIZE3 stress_hugepage_offset_random(const size_t n_pages)
{
	return stress_mwcsizemodn(n_pages);
}

static size_t OPTIMIZE3 stress_hugepage_offset_forward(const size_t n_pages)
{
	static size_t offset = 0;
	size_t ret = offset;

	offset++;
	offset = (offset >= n_pages) ? 0 : offset;
	return ret;
}

static size_t OPTIMIZE3 stress_hugepage_offset_reverse(const size_t n_pages)
{
	static ssize_t offset = 0;

	offset--;
	offset = (offset < 0) ? (ssize_t)n_pages - 1 : offset;
	return (size_t)offset;
}

static const stress_hugepage_method_t hugepage_methods[] = {
	{ "forward",	stress_hugepage_offset_forward },
	{ "random",	stress_hugepage_offset_random },
	{ "reverse",	stress_hugepage_offset_reverse },
};

static const char *stress_hugepage_method(const size_t i)
{
	return (i < SIZEOF_ARRAY(hugepage_methods)) ? hugepage_methods[i].name : NULL;
}

static const stress_opt_t opts[] = {
	{ OPT_hugepage_dontneed, "hugepage-dontneed", TYPE_ID_BOOL, 0, 1, NULL },
	{ OPT_hugepage_method,   "hugepage-method",   TYPE_ID_SIZE_T_METHOD, 0, 0, stress_hugepage_method },

	{ OPT_hugepage_num,      "hugepage-num",      TYPE_ID_SIZE_T, MIN_HUGEPAGE_NUM, MAX_HUGEPAGE_NUM, NULL },
	{ OPT_hugepage_remove,   "hugepage-remove",   TYPE_ID_BOOL, 0, 1, NULL },
	END_OPT
};

#if defined(HAVE_LIB_PTHREAD) &&	\
    defined(MAP_HUGETLB) &&     	\
    (defined(MAP_HUGE_2MB) ||   	\
     defined(MAP_HUGE_1GB))

static size_t hugepage_size = 0;
static int hugepage_mmap_flag = 0;

/*
 *  hugepage_sizes - ordered in preferred mapping flags
 *  and huge page sizes
 */
static const stress_hugepage_size_t hugepage_sizes[] = {
	/* HUGE_* options */
#if defined(MAP_HUGE_2MB)
	/* preferred option */
	{ MAP_HUGETLB | MAP_HUGE_2MB, 2 * MB },
#endif
#if defined(MAP_HUGE_8MB)
	{ MAP_HUGETLB | MAP_HUGE_8MB, 8 * MB },
#endif
#if defined(MAP_HUGE_16MB)
	{ MAP_HUGETLB | MAP_HUGE_16MB, 16 * MB },
#endif
#if defined(MAP_HUGE_32MB)
	{ MAP_HUGETLB | MAP_HUGE_32MB, 32 * MB },
#endif
#if defined(MAP_HUGE_256MB)
	{ MAP_HUGETLB | MAP_HUGE_256MB, 256 * MB },
#endif
#if defined(MAP_HUGE_512MB)
	{ MAP_HUGETLB | MAP_HUGE_512MB, 512 * MB },
#endif
#if defined(MAP_HUGE_1MB)
	{ MAP_HUGETLB | MAP_HUGE_1MB, 1 * MB },
#endif
#if defined(MAP_HUGE_512K)
	{ MAP_HUGETLB | MAP_HUGE_512K, 512 * KB },
#endif
#if defined(MAP_HUGE_64K)
	{ MAP_HUGETLB | MAP_HUGE_64K, 64 * KB },
#endif
#if defined(MAP_HUGE_16K)
	{ MAP_HUGETLB | MAP_HUGE_16K, 16 * KB },
#endif
#if defined(MAP_HUGE_1GB)
	{ MAP_HUGETLB | MAP_HUGE_1GB, 1 * GB },
#endif
#if defined(MAP_HUGE_2GB)
	{ MAP_HUGETLB | MAP_HUGE_2GB, 2 * GB },
#endif
#if defined(MAP_HUGE_16GB)
	/* least preferred option */
	{ MAP_HUGETLB | MAP_HUGE_16GB, 16 * GB },
#endif

	/* MAP_HUGETLB only options */
	{ MAP_HUGETLB, 2 * MB },
	{ MAP_HUGETLB, 8 * MB },
	{ MAP_HUGETLB, 16 * MB },
	{ MAP_HUGETLB, 32 * MB },
	{ MAP_HUGETLB, 256 * MB },
	{ MAP_HUGETLB, 512 * MB },
	{ MAP_HUGETLB, 1 * MB },
	{ MAP_HUGETLB, 512 * KB },
	{ MAP_HUGETLB, 64 * KB },
	{ MAP_HUGETLB, 16 * KB },
	{ MAP_HUGETLB, 1 * GB },
	{ MAP_HUGETLB, 2 * GB },
	{ MAP_HUGETLB, 16 * GB },

	/*
	 *  vanilla non-huge page mmap options,
	 *  try to select sizes that may end up
	 *  being huge page sized for transparent
	 *  huge page allocation
	 */
#if (defined(STRESS_ARCH_ARM) && defined(__aarch64__)) ||	\
    defined(STRESS_ARCH_X86_64)
	{ 0, 2 * MB },
#elif defined(STRESS_ARCH_X86_32)
	{ 0, 4 * MB },
	/* or PAE mode */
	{ 0, 2 * MB },
#elif defined(STRESS_ARCH_PPC64)
	{ 0, 16 * MB },
#else
	{ 0, 2 * MB },
#endif
	{ 0, 8 * MB },
	{ 0, 16 * MB },
	{ 0, 32 * MB },
	{ 0, 256 * MB },
	{ 0, 512 * MB },
	{ 0, 1 * MB },
	{ 0, 512 * KB },
	{ 0, 64 * KB },
	{ 0, 16 * KB },
	{ 0, 1 * GB },
	{ 0, 2 * GB },
	{ 0, 16 * GB },
};

/*
 *  stress_hugepage_pthread()
 *	thread that attempts to break up huge pages into
 *	smaller pages, races against main stressor instance process
 */
static OPTIMIZE3 void *stress_hugepage_pthread(void *arg)
{
	stress_pthread_args_t *pthread_args = (stress_pthread_args_t *)arg;
	stress_hugepage_info_t *hugepage_info = (stress_hugepage_info_t *)pthread_args->data;
	const size_t page_size = hugepage_info->page_size;
	const size_t page_scale = page_size / sizeof(uint64_t);
	const size_t n_normal_pages = hugepage_info->n_normal_pages;
	const stress_hugepage_offset_func_t func = hugepage_info->func;
	uint64_t * const addr64 = hugepage_info->addr64;
	const bool madv_dontneed = hugepage_info->dontneed;
	const bool madv_remove = hugepage_info->remove;
	uint64_t counter = stress_mwc64();

#if defined(MADV_RANDOM)
	(void)madvise((void *)addr64, hugepage_info->size, MADV_RANDOM);
#endif
	do {
		register const size_t offset = func(n_normal_pages) * page_scale;
		register uint64_t *page64 = addr64 + offset;
		register unsigned int madv_count = 0;
		register unsigned int mprt_count = 0;

		/* ensure page has some non-zero data in it */
		page64[0] = counter++;
		page64[1] = counter++;
#if defined(MADV_DONTNEED)
		if (madv_dontneed)
			madv_count += (madvise((void *)page64, page_size, MADV_DONTNEED) == 0);
#endif
#if defined(MADV_REMOVE)
		if (madv_remove)
			madv_count += (madvise((void *)page64, page_size, MADV_REMOVE) == 0);
#endif
		mprt_count += (mprotect((void *)page64, page_size, PROT_READ) == 0);
		mprt_count += (mprotect((void *)page64, page_size, PROT_READ | PROT_WRITE) == 0);
		hugepage_info->pthread_mprt_count += mprt_count;
#if defined(MADV_WILLNEED)
		if (madv_dontneed)
			madv_count += (madvise((void *)page64, page_size, MADV_WILLNEED) == 0);
#endif
		page64[0] = counter++;
		page64[1] = counter++;
		hugepage_info->pthread_madv_count += madv_count;
	} while (stress_continue(pthread_args->args));

	return &g_nowt;
}

/*
 *  stress_hugepage_init()
 *	determine a usable huge page size, with 2MB as
 *	preferred and working up after that in size order
 */
static void stress_hugepage_init(const uint32_t instances)
{
	size_t i;

	(void)instances;
	hugepage_size = 0;

	/* Determine default huge page size */
	for (i = 0; i < SIZEOF_ARRAY(hugepage_sizes); i++) {
		void *addr;
		const size_t sz = hugepage_sizes[i].size;

		addr = mmap(NULL, sz, PROT_READ | PROT_WRITE,
				hugepage_sizes[i].flags | MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
		if (addr != MAP_FAILED) {
			(void)munmap(addr, sz);
			hugepage_size = sz;
			hugepage_mmap_flag = hugepage_sizes[i].flags;
			break;
		}
	}
}

/*
 *  stress_hugepage
 *	stress hugepages
 */
static int stress_hugepage(stress_args_t *args)
{
	pthread_t pthread;
	double t;
	double duration;
	double rate;
	stress_pthread_args_t pthread_args;
	stress_hugepage_info_t hugepage_info;
	int ret;
	int rc = EXIT_SUCCESS;
	size_t hugepage_num = DEFAULT_HUGEPAGE_NUM;
	size_t hugepage_method = 1;	/* random */
	char numstr1[32];
	char numstr2[32];

	if (hugepage_size == 0) {
		pr_inf("%s: mmap hugepages failed, skipping stressor%s\n",
			args->name, stress_memory_free_get());
		return EXIT_NO_RESOURCE;
	}

	hugepage_info.addr64 = MAP_FAILED;
	hugepage_info.page_size = args->page_size;
	hugepage_info.dontneed = false;
	hugepage_info.remove = false;
	hugepage_info.main_madv_count = 0ULL;
	hugepage_info.pthread_madv_count = 0ULL;
	hugepage_info.pthread_mprt_count = 0ULL;

	(void)stress_setting_get("hugepage-dontneed", &hugepage_info.dontneed);
	(void)stress_setting_get("hugepage-method", &hugepage_method);
	(void)stress_setting_get("hugepage-num", &hugepage_num);
	(void)stress_setting_get("hugepage-remove", &hugepage_info.remove);

	hugepage_info.size = hugepage_size * hugepage_num;
	hugepage_info.n_normal_pages = hugepage_info.size / args->page_size;
	hugepage_info.func = hugepage_methods[hugepage_method].func;

	pthread_args.args = args;
	pthread_args.data = &hugepage_info;
	pthread_args.pthread_ret = 0;

	hugepage_info.addr64 = (uint64_t *)mmap(NULL, hugepage_info.size, PROT_READ | PROT_WRITE,
				  MAP_ANONYMOUS | MAP_PRIVATE | hugepage_mmap_flag, -1, 0);
	if (hugepage_info.addr64 == MAP_FAILED) {
		pr_inf_skip("%s: mmap %zuKB failed, skipping stressor\n", args->name, hugepage_info.size >> 10);
		return EXIT_NO_RESOURCE;
	}

	if (stress_instance_zero(args)) {
		pr_inf("%s: using %zu x %sB pages (%sB) per instance\n", args->name, hugepage_num,
			stress_uint64_to_str(numstr1, sizeof(numstr1), (uint64_t)hugepage_size, 2, true),
			stress_uint64_to_str(numstr2, sizeof(numstr2), (uint64_t)hugepage_info.size, 2, true));
	}

	/* ensure pages are populated */
#if defined(MADV_POPULATE_WRITE)
	(void)madvise((void *)hugepage_info.addr64, hugepage_info.size, MADV_POPULATE_WRITE);
#endif
	stress_mmap_set_light((void *)hugepage_info.addr64, hugepage_info.size, args->page_size);

#if defined(MADV_HUGEPAGE)
	if (madvise((void *)hugepage_info.addr64, hugepage_info.size, MADV_HUGEPAGE) < 0) {
		pr_inf_skip("%s: madvise MADV_HUGEPAGE failed, errno=%d (%s), skipping stressor\n",
			args->name, errno, strerror(errno));
		rc = EXIT_NO_RESOURCE;
		goto hugepage_unmap;
	}
#endif

	stress_proc_state_set(args->name, STRESS_STATE_SYNC_WAIT);
	stress_sync_start_wait(args);
	stress_proc_state_set(args->name, STRESS_STATE_RUN);

	ret = pthread_create(&pthread, NULL, stress_hugepage_pthread, &pthread_args);
	if (ret) {
		pr_inf_skip("%s: create pthread failed, errno=%d (%s), skipping stressor\n",
			args->name, ret, strerror(ret));
		rc = EXIT_NO_RESOURCE;
		goto hugepage_unmap;
	}

	t = stress_time_now();
	do {
		register int madv_count = 0;

		/* try to force mapping into huge pages */
#if defined(MADV_HUGEPAGE)
		madv_count += (madvise((void *)hugepage_info.addr64, hugepage_info.size, MADV_HUGEPAGE) == 0);
#endif
		/* try to collapse pages into huge pages */
#if defined(MADV_COLLAPSE)
		madv_count += (madvise((void *)hugepage_info.addr64, hugepage_info.size, MADV_COLLAPSE) == 0);
#endif
		/* try to keep pages in core */
#if defined(MADV_WILLNEED)
		madv_count += (madvise((void *)hugepage_info.addr64, hugepage_info.size, MADV_WILLNEED) == 0);
#endif
		hugepage_info.main_madv_count += madv_count;
		stress_bogo_inc(args);
	} while (stress_continue(args));

	ret = pthread_cancel(pthread);
	if (ret) {
		pr_fail("%s: pthread_cancel failed, errno=%d (%s)\n",
			args->name, ret, strerror(ret));
	}
	duration = stress_time_now() - t;

	rate = (duration > 0.0) ? ((double)hugepage_info.main_madv_count +
			           (double)hugepage_info.pthread_madv_count) / duration : 0.0;
	stress_metrics_set(args, "madvise calls per sec", rate, STRESS_METRIC_HARMONIC_MEAN);
	rate = (duration > 0.0) ? (double)hugepage_info.pthread_mprt_count / duration : 0.0;
	stress_metrics_set(args, "mprotect calls per sec", rate, STRESS_METRIC_HARMONIC_MEAN);

hugepage_unmap:
	(void)munmap((void *)hugepage_info.addr64, hugepage_info.size);

	return rc;
}

static const stress_exercises_t exercises[] = {
	STRESS_EX_FEATURE("kmalloc"),
	STRESS_EX_FEATURE("maple-tree-write"),
	STRESS_EX_FEATURE("memory-store"),
	STRESS_EX_FEATURE("page-faults-minor"),
	STRESS_EX_FEATURE("page-faults-user"),
	STRESS_EX_FEATURE("tlb-flush"),

	STRESS_EX_SYSCALL("madvise"),
	STRESS_EX_SYSCALL("mprotect"),
#if defined(HAVE_LIB_PTHREAD)
        STRESS_EX_LIBRARY("pthread"),
#endif

	STRESS_EX_END,
};


const stressor_info_t stress_hugepage_info = {
	.stressor = stress_hugepage,
	.classifier = CLASS_OS | CLASS_VM,
	.verify = VERIFY_NONE,
	.help = help,
	.opts = opts,
	.init = stress_hugepage_init,
	.exercises = exercises,
};
#else
const stressor_info_t stress_hugepage_info = {
	.stressor = stress_unimplemented,
	.classifier = CLASS_OS | CLASS_VM,
	.verify = VERIFY_NONE,
	.help = help,
	.opts = opts,
	.unimplemented_reason = "built without pthread or HUGE_TLB support"
};
#endif
