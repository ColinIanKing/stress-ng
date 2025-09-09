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
#include "core-attribute.h"
#include "core-mmap.h"
#include "core-nt-load.h"
#include "core-out-of-memory.h"

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
	uint64_t out_of_memory;
	uint64_t sbrk_expands;
	uint64_t sbrk_shrinks;
	double sbrk_exp_duration;
	double sbrk_exp_count;
	double sbrk_shr_duration;
	double sbrk_shr_count;
	size_t brk_bytes;
	bool brk_mlock;
	bool brk_notouch;
} brk_context_t;

static brk_context_t *brk_context;

static const stress_opt_t opts[] = {
	{ OPT_brk_bytes,   "brk-bytes",   TYPE_ID_SIZE_T_BYTES_VM, MIN_BRK_BYTES, MAX_BRK_BYTES, NULL },
	{ OPT_brk_mlock,   "brk-mlock",   TYPE_ID_BOOL,            0,             1,             NULL },
	{ OPT_brk_notouch, "brk-notouch", TYPE_ID_BOOL,            0,             1,             NULL },
	END_OPT,
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

static inline size_t CONST stress_brk_abs(const uint8_t *ptr1, const uint8_t *ptr2)
{
	return (size_t)((ptr1 > ptr2) ? ptr1 - ptr2 : ptr2 - ptr1);
}

static int OPTIMIZE3 stress_brk_child(stress_args_t *args, void *context)
{
	uint8_t *start_ptr, *new_start_ptr, *unmap_ptr = NULL;
	const uint8_t *brk_failed_ptr = NULL;
	int i = 0, brk_failed_count = 0;
	const size_t page_size = args->page_size;
	const bool brk_touch = !brk_context->brk_notouch;
	bool reset_brk = false;
	uint8_t *ptr;

	(void)context;

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
		int saved_errno = 0;

		if (reset_brk || (stress_brk_abs(ptr, start_ptr) >= brk_context->brk_bytes)) {
			intptr_t diff;

			VOID_RET(int, shim_brk(start_ptr));
			VOID_RET(void *, shim_sbrk(0));

			/* Get brk address, should not fail */
			ptr = shim_sbrk(0);
			if (ptr == (void *)-1) {
				pr_fail("%s: sbrk(0) failed, errno=%d (%s)\n",
					args->name, errno, strerror(errno));
				return EXIT_FAILURE;
			}
			diff = ptr - start_ptr;
			/*
			 *  Apply hammer to brk, really push it back
			 *  to start
			 */
			if (diff > 0) {
				ptr = shim_sbrk(-diff);
				if (ptr == (void *)-1) {
					pr_fail("%s: sbrk(%" PRIxPTR ") failed, errno=%d (%s)\n",
						args->name, -diff, errno, strerror(errno));
					return EXIT_FAILURE;
				} else {
					brk_context->sbrk_shrinks++;
				}
				/* Get brk address, should not fail */
				ptr = shim_sbrk(0);
				if (ptr == (void *)-1) {
					pr_fail("%s: sbrk(0) failed, errno=%d (%s)\n",
						args->name, errno, strerror(errno));
					return EXIT_FAILURE;
				}
			}
			reset_brk = false;
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
			new_start_ptr = shim_sbrk((intptr_t)page_size);
			if (new_start_ptr != (void *)-1) {
				uintptr_t *tmp;

				brk_context->sbrk_expands++;
				brk_context->sbrk_exp_duration += stress_time_now() - t;
				brk_context->sbrk_exp_count += 1.0;

				ptr += page_size;
				brk_failed_ptr = NULL;
				brk_failed_count = 0;
				if (!unmap_ptr)
					unmap_ptr = ptr;
				if (new_start_ptr != (ptr - page_size))
					ptr = new_start_ptr + page_size;
				stress_brk_page_resident(ptr, page_size, brk_touch);

				/* stash a check value */
				tmp = (uintptr_t *)((uintptr_t)ptr - sizeof(uintptr_t));
				*tmp = (uintptr_t)tmp;
			} else {
				brk_context->out_of_memory++;
				saved_errno = errno;
				if (brk_failed_ptr == ptr) {
					brk_failed_count++;
					if (brk_failed_count > 32) {
						reset_brk = true;
						continue;
					}
				}
				i = 0;
				brk_failed_ptr = ptr;
			}
		} else if (i < 9) {
			/* brk to same brk position */
			if (UNLIKELY(shim_brk(ptr) < 0)) {
				saved_errno = errno;
				ptr = start_ptr;
				i = 0;
			}
		} else if (i < 10) {
			/* Shrink brk by 1 page */
			t = stress_time_now();
			if (LIKELY(shim_sbrk(-page_size) != (void *)-1)) {
				brk_context->sbrk_shrinks++;
				saved_errno = errno;
				brk_context->sbrk_shr_duration += stress_time_now() - t;
				brk_context->sbrk_shr_count += 1.0;
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
					return EXIT_FAILURE;
				}
			}
		} else {
			i = 0;
			/* remove a page from brk region */
			if (unmap_ptr) {
				(void)stress_munmap_force((void *)unmap_ptr, page_size);
				unmap_ptr = NULL;
			}
			continue;
		}

		if (UNLIKELY(ptr == (void *)-1)) {
			if (LIKELY((saved_errno == ENOMEM) || (saved_errno == EAGAIN))) {
				VOID_RET(int, shim_brk(start_ptr));
				i = 0;
			} else {
				pr_fail("%s: sbrk(%d) failed, errno=%d (%s)\n",
					args->name, (int)page_size, saved_errno,
					strerror(saved_errno));
				return EXIT_FAILURE;
			}
		}
		stress_bogo_inc(args);
	} while (stress_continue(args));

	return EXIT_SUCCESS;
}

/*
 *  stress_brk()
 *	stress brk and sbrk
 */
static int stress_brk(stress_args_t *args)
{
	int rc;
	double rate;

	brk_context = (brk_context_t *)stress_mmap_populate(NULL, sizeof(*brk_context),
						PROT_READ | PROT_WRITE,
						MAP_ANONYMOUS | MAP_SHARED, -1, 0);
	if (brk_context == MAP_FAILED) {
		pr_inf_skip("%s: cannot mmap brk context region%s, errno=%d (%s), skipping stressor\n",
			args->name, stress_get_memfree_str(),
			errno, strerror(errno));
		return EXIT_NO_RESOURCE;
	}

	brk_context->brk_bytes = DEFAULT_BRK_BYTES;
	brk_context->sbrk_exp_duration = 0.0;
	brk_context->sbrk_exp_count = 0.0;
	brk_context->sbrk_shr_duration = 0.0;
	brk_context->sbrk_shr_count = 0.0;
	brk_context->brk_mlock = false;
	brk_context->brk_notouch = false;

	(void)stress_get_setting("brk-bytes", &brk_context->brk_bytes);
	(void)stress_get_setting("brk-mlock", &brk_context->brk_mlock);
	(void)stress_get_setting("brk-notouch", &brk_context->brk_notouch);

#if !defined(MCL_FUTURE)
	if (stress_instance_zero(args) && brk_context->brk_mlock) {
		pr_inf("%s: --brk-mlock option was enabled but support for "
			"mlockall(MCL_FUTURE) is not available\n",
			args->name);
	}
#endif
	stress_set_proc_state(args->name, STRESS_STATE_SYNC_WAIT);
	stress_sync_start_wait(args);
	stress_set_proc_state(args->name, STRESS_STATE_RUN);

	rc = stress_oomable_child(args, NULL, stress_brk_child, STRESS_OOMABLE_DROP_CAP);

	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);

	pr_dbg("%s: %" PRIu64 " occurrences of sbrk out of memory\n",
		args->name, brk_context->out_of_memory);
	pr_dbg("%s: %" PRIu64 " successful sbrk expands, %" PRIu64 " succussful sbrk shinks\n",
		args->name, brk_context->sbrk_expands, brk_context->sbrk_shrinks);

	rate = (brk_context->sbrk_exp_count > 0.0) ? (double)brk_context->sbrk_exp_duration / brk_context->sbrk_exp_count : 0.0;
	stress_metrics_set(args, 0, "nanosecs per sbrk page expand",
		rate * STRESS_DBL_NANOSECOND, STRESS_METRIC_HARMONIC_MEAN);
	rate = (brk_context->sbrk_shr_count > 0.0) ? (double)brk_context->sbrk_shr_duration / brk_context->sbrk_shr_count : 0.0;
	stress_metrics_set(args, 1, "nanosecs per sbrk page shrink",
		rate * STRESS_DBL_NANOSECOND, STRESS_METRIC_HARMONIC_MEAN);


	(void)munmap((void *)brk_context, sizeof(*brk_context));

	return rc;
}

const stressor_info_t stress_brk_info = {
	.stressor = stress_brk,
	.supported = stress_brk_supported,
	.classifier = CLASS_OS | CLASS_VM,
	.opts = opts,
	.verify = VERIFY_ALWAYS,
	.help = help
};
