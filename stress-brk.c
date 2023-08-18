// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2013-2021 Canonical, Ltd.
 * Copyright (C) 2022-2023 Colin Ian King.
 *
 */
#include "stress-ng.h"
#include "core-nt-load.h"

#define MIN_BRK_BYTES		(64 * KB)
#define MAX_BRK_BYTES		(MAX_MEM_LIMIT)
#define DEFAULT_BRK_BYTES	(MAX_MEM_LIMIT)

static const stress_help_t help[] = {
	{ NULL,	"brk N",	"start N workers performing rapid brk calls" },
	{ NULL,	"brk-bytes N",	"grow brk region up to N bytes in total" },
	{ NULL, "brk-mlock",	"attempt to mlock newly mapped brk pages" },
	{ NULL,	"brk-notouch",	"don't touch (page in) new data segment page" },
	{ NULL,	"brk-ops N",	"stop after N brk bogo operations" },
	{ NULL,	NULL,		NULL }
};

typedef struct {
	bool brk_mlock;
	bool brk_notouch;
} brk_context_t;

/*
 *  stress_set_bigheap_bytes()
 *     Set maximum allocation amount in bytes
 */
static int stress_set_brk_bytes(const char *opt)
{
	size_t brk_bytes;

	brk_bytes = (size_t)stress_get_uint64_byte_memory(opt, 1);
	stress_check_range_bytes("brk-bytes", (uint64_t)brk_bytes,
		MIN_BRK_BYTES, MAX_BRK_BYTES);
	return stress_set_setting("brk-bytes", TYPE_ID_SIZE_T, &brk_bytes);
}

static int stress_set_brk_mlock(const char *opt)
{
	return stress_set_setting_true("brk-mlock", opt);
}

static int stress_set_brk_notouch(const char *opt)
{
	return stress_set_setting_true("brk-notouch", opt);
}

static const stress_opt_set_func_t opt_set_funcs[] = {
	{ OPT_brk_bytes,	stress_set_brk_bytes },
	{ OPT_brk_mlock,	stress_set_brk_mlock },
	{ OPT_brk_notouch,	stress_set_brk_notouch },
	{ 0,			NULL }
};

static int stress_brk_supported(const char *name)
{
	void *ptr;

	/*
	 *  Some flavours of FreeBSD don't support sbrk()
	 *  so check for this
	 */
	ptr = shim_sbrk(0);
	if (UNLIKELY((ptr == (void *)-1) && (errno == ENOSYS))) {
		pr_inf_skip("%s: stressor will be skipped, sbrk() is not "
			"implemented on this system\n", name);
		return -1;
	}

	/*
	 *  check for brk() not being implemented too
	 */
	if (UNLIKELY((shim_brk(ptr) < 0) && (errno == ENOSYS))) {
		pr_inf_skip("%s: stressor will be skipped, brk() is not "
			"implemented on this system\n", name);
		return -1;
	}

	return 0;
}

static inline void OPTIMIZE3 stress_brk_page_resident(
	register uint8_t *addr,
	const size_t page_size,
	const bool brk_touch)
{
#if defined(__APPLE__)
	(void)addr;
	(void)page_size;
	(void)brk_touch;
#endif

#if !defined(__APPLE__)
	/* Touch page, force it to be resident */
	if (LIKELY(brk_touch)) {
#if defined(HAVE_NT_LOAD32)
		(void)stress_nt_load32((uint32_t *)(addr - page_size));
#else
		(void )*(volatile uint8_t *)(addr - page_size);
#endif
	}
#endif
#if defined(HAVE_MADVISE) &&	\
    defined(MADV_MERGEABLE)
	(void)madvise((void *)(addr - page_size), page_size, MADV_MERGEABLE);
#else
	UNEXPECTED
#endif
}

static inline size_t stress_brk_abs(const uint8_t *ptr1, const uint8_t *ptr2)
{
	return (size_t)((ptr1 > ptr2) ? ptr1 - ptr2 : ptr2 - ptr1);
}

static int OPTIMIZE3 stress_brk_child(const stress_args_t *args, void *context)
{
	uint8_t *start_ptr, *unmap_ptr = NULL;
	int i = 0;
	size_t brk_bytes = DEFAULT_BRK_BYTES;
	const size_t page_size = args->page_size;
	const brk_context_t *brk_context = (brk_context_t *)context;
	double sbrk_exp_duration = 0.0, sbrk_exp_count = 0.0;
	double sbrk_shr_duration = 0.0, sbrk_shr_count = 0.0;
	double rate;
	const bool brk_touch = !brk_context->brk_notouch;
	uint8_t *ptr;

	(void)stress_get_setting("brk-bytes", &brk_bytes);

	start_ptr = shim_sbrk(0);
	if (start_ptr == (void *) -1) {
		pr_fail("%s: sbrk(0) failed, errno=%d (%s)\n",
			args->name, errno, strerror(errno));
		return EXIT_FAILURE;
	}
	ptr = start_ptr;

#if defined(MCL_FUTURE)
	if (brk_context->brk_mlock)
		(void)shim_mlockall(MCL_FUTURE);
#else
	UNEXPECTED
#endif

	do {
		double t;

		if (stress_brk_abs(ptr, start_ptr) > brk_bytes) {
			ptr = start_ptr;
			VOID_RET(int, shim_brk(ptr));
			i = 0;
		}

		/* Low memory avoidance, re-start */
		if ((g_opt_flags & OPT_FLAGS_OOM_AVOID) && stress_low_memory(page_size)) {
			VOID_RET(int, shim_brk(start_ptr));
			i = 0;
		}

		i++;
		if (LIKELY(i < 8)) {
			/* Expand brk by 1 page */
			t = stress_time_now();
			if (LIKELY(shim_sbrk((intptr_t)page_size) != (void *)-1)) {
				uintptr_t *tmp;

				sbrk_exp_duration += stress_time_now() - t;
				sbrk_exp_count += 1.0;

				ptr += page_size;
				if (!unmap_ptr)
					unmap_ptr = ptr;
				stress_brk_page_resident(ptr, page_size, brk_touch);

				/* stash a check value */
				tmp = (uintptr_t *)((uintptr_t)ptr - sizeof(uintptr_t));
				*tmp = (uintptr_t)tmp;
			}
		} else if (i < 9) {
			/* brk to same brk position */
			if (UNLIKELY(shim_brk(ptr) < 0)) {
				ptr = start_ptr;
				i = 0;
			}
		} else if (i < 10) {
			/* Shrink brk by 1 page */
			t = stress_time_now();
			if (LIKELY(shim_sbrk(-page_size) != (void *)-1)) {
				sbrk_shr_duration += stress_time_now() - t;
				sbrk_shr_count += 1.0;
				ptr -= page_size;
			}
			if (UNLIKELY(shim_brk(ptr) < 0)) {
				ptr = start_ptr;
				i = 0;
			} else {
				uintptr_t *tmp;

				stress_brk_page_resident(ptr, page_size, brk_touch);
				tmp = (uintptr_t *)((uintptr_t)ptr - sizeof(uintptr_t));
				if (*tmp != (uintptr_t)tmp) {
					pr_fail("%s: brk shrink page at %p contains incorrect "
						"check value 0x%" PRIxPTR ", expected "
						"0x%" PRIxPTR "\n",
						args->name, tmp, *tmp, (uintptr_t)tmp);
				}
			}
		} else {
			i = 0;
			/* remove a page from brk region */
			if (unmap_ptr) {
				(void)stress_munmap_retry_enomem((void *)unmap_ptr, page_size);
				unmap_ptr = NULL;
			}
			continue;
		}

		if (UNLIKELY(ptr == (void *)-1)) {
			if (LIKELY((errno == ENOMEM) || (errno == EAGAIN))) {
				VOID_RET(int, shim_brk(start_ptr));
				i = 0;
			} else {
				pr_fail("%s: sbrk(%d) failed: errno=%d (%s)\n",
					args->name, (int)page_size, errno,
					strerror(errno));
				return EXIT_FAILURE;
			}
		}
		stress_bogo_inc(args);
	} while (stress_continue(args));

	rate = (sbrk_exp_count > 0.0) ? (double)sbrk_exp_duration / sbrk_exp_count : 0.0;
	stress_metrics_set(args, 0, "nanosecs per sbrk page expand", rate * STRESS_DBL_NANOSECOND);
	rate = (sbrk_shr_count > 0.0) ? (double)sbrk_shr_duration / sbrk_shr_count : 0.0;
	stress_metrics_set(args, 1, "nanosecs per sbrk page shrink", rate * STRESS_DBL_NANOSECOND);

	return EXIT_SUCCESS;
}

/*
 *  stress_brk()
 *	stress brk and sbrk
 */
static int stress_brk(const stress_args_t *args)
{
	int rc;
	brk_context_t brk_context;

	brk_context.brk_mlock = false;
	brk_context.brk_notouch = false;

	(void)stress_get_setting("brk-mlock", &brk_context.brk_mlock);
	(void)stress_get_setting("brk-notouch", &brk_context.brk_notouch);

#if !defined(MCL_FUTURE)
	if ((args->instance == 0) && brk_context.brk_mlock) {
		pr_inf("%s: --brk-mlock option was enabled but support for "
			"mlock(MCL_FUTURE) is not available\n",
			args->name);
	}
#endif
	stress_set_proc_state(args->name, STRESS_STATE_RUN);

	rc = stress_oomable_child(args, (void *)&brk_context, stress_brk_child, STRESS_OOMABLE_DROP_CAP);

	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);

	return rc;
}

stressor_info_t stress_brk_info = {
	.stressor = stress_brk,
	.supported = stress_brk_supported,
	.class = CLASS_OS | CLASS_VM,
	.opt_set_funcs = opt_set_funcs,
	.verify = VERIFY_ALWAYS,
	.help = help
};
