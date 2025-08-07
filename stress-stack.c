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
#include "core-builtin.h"
#include "core-cpu-cache.h"
#include "core-mincore.h"
#include "core-mmap.h"
#include "core-out-of-memory.h"
#include "core-pragma.h"
#include "core-put.h"

#define STRESS_DATA_SIZE	(256 * KB)

/*
 *  stress_stack_check sanity check list
 */
typedef struct stress_stack_check {
	struct stress_stack_check *prev;	/* Previous item on stack list */
	struct stress_stack_check *self_addr;	/* Address of this struct to check */
} stress_stack_check_t;

static const stress_help_t help[] = {
	{ NULL,	"stack N",	"start N workers generating stack overflows" },
	{ NULL,	"stack-fill",	"fill stack, touches all new pages " },
	{ NULL, "stack-mlock",	"mlock stack, force pages to be unswappable" },
	{ NULL,	"stack-ops N",	"stop after N bogo stack overflows" },
	{ NULL, "stack-pageout","use madvise to try to swap out stack" },
	{ NULL,	"stack-unmap",	"unmap a page in the stack on each iteration" },
	{ NULL,	NULL,		NULL }
};

static const stress_opt_t opts[] = {
	{ OPT_stack_fill,    "stack-fill",    TYPE_ID_BOOL, 0, 1, NULL },
	{ OPT_stack_mlock,   "stack-mlock",   TYPE_ID_BOOL, 0, 1, NULL },
	{ OPT_stack_pageout, "stack-pageout", TYPE_ID_BOOL, 0, 1, NULL },
	{ OPT_stack_unmap,   "stack-unmap",   TYPE_ID_BOOL, 0, 1, NULL },
	END_OPT,
};

#if defined(HAVE_SIGLONGJMP)

static sigjmp_buf jmp_env;

/*
 *  stress_segvhandler()
 *	SEGV handler
 */
static void MLOCKED_TEXT NORETURN stress_segvhandler(int signum)
{
	(void)signum;

	siglongjmp(jmp_env, 1);		/* Ugly, bounce back */
	stress_no_return();
}

/*
 *  stress_stack_alloc()
 *	eat up stack. The default is to eat up lots of pages
 *	but only have 25% of the pages actually in memory
 *	so we a large stack with lots of pages not physically
 *	resident.
 */
static bool OPTIMIZE3 stress_stack_alloc(
	stress_args_t *args,
	void *start,
	stress_stack_check_t *check_prev,
	const bool stack_fill,
	bool stack_mlock,
	const bool stack_pageout,
	const bool stack_unmap,
	ssize_t last_size)
{
	const size_t page_size = args->page_size;
	const size_t page_size4 = page_size << 2;
	uint32_t data[STRESS_DATA_SIZE / sizeof(uint32_t)];
	stress_stack_check_t check, *check_ptr;
	bool check_success = true;

	if ((g_opt_flags & OPT_FLAGS_OOM_AVOID) && stress_low_memory(STRESS_DATA_SIZE))
		return true;

	if (stack_fill) {
		(void)shim_memset(data, stress_mwc8(), STRESS_DATA_SIZE);
	} else {
		register size_t i;

		/*
		 *  Touch 25% of the pages, ensure data
		 *  is random and non-zero to avoid
		 *  kernel same page merging
		 */
		for (i = 0; i < STRESS_DATA_SIZE / sizeof(uint32_t); i += page_size4) {
			uint32_t *ptr = data + i;

			*ptr = stress_mwc32();
			*(ptr + 1) = stress_mwc32() | 1;
		}
	}
	if (g_opt_flags & OPT_FLAGS_AGGRESSIVE)
		stress_cpu_data_cache_flush(data, STRESS_DATA_SIZE);
#if defined(HAVE_MLOCK)
	if (stack_mlock) {
		intptr_t ptr = ((intptr_t)data) + ((intptr_t)page_size - 1);
		ssize_t mlock_sz = (uint8_t *)start - (uint8_t *)ptr;

		if (mlock_sz < 0)
			mlock_sz = -mlock_sz;

		if (mlock_sz > (last_size + 8 * (ssize_t)MB)) {
			int ret;

			ptr &= ~(page_size - 1);
			ret = shim_mlock((void *)ptr, (size_t)(mlock_sz - last_size));
			if (ret < 0)
				stack_mlock = false;
			last_size = mlock_sz;
		}
	}
#else
	UNEXPECTED
#endif

#if defined(MADV_PAGEOUT)
	if (stack_pageout) {
		intptr_t ptr = ((intptr_t)data) + ((intptr_t)page_size - 1);
		ptr &= ~(page_size - 1);

		(void)madvise((void *)ptr, sizeof(data), MADV_PAGEOUT);
	}
#endif

	if (stack_unmap) {
		const uintptr_t page_mask = ~(uintptr_t)(page_size - 1);
		const uintptr_t unmap_ptr = ((uintptr_t)&data[0] + (sizeof(data) >> 1)) & page_mask;

		(void)stress_munmap_force((void *)unmap_ptr, page_size);
	}

	/* traverse back down the stack to touch 128 pages on the stack */
	{
		register int i = 0;
		check.self_addr = &check;
		check.prev = check_prev;

PRAGMA_UNROLL_N(4)
		for (check_ptr = &check; check_ptr; check_ptr = check_ptr->prev) {
			if (UNLIKELY(i++ >= 128))
				break;
			if (UNLIKELY(check_ptr->self_addr != check_ptr)) {
				pr_fail("%s: corrupt self check data on stack, got %p, expected %p\n",
					args->name, check_ptr->self_addr, check_ptr);
				check_success = false;
				break;
			}
		}
		stress_void_ptr_put(check_ptr);
	}

	stress_bogo_inc(args);

	if (!check_success)
		return false;

	if (LIKELY(stress_continue(args)))
		return stress_stack_alloc(args, start, &check, stack_fill, stack_mlock, stack_pageout, stack_unmap, last_size);
	return true;
}

static int stress_stack_child(stress_args_t *args, void *context)
{
	const char *start_ptr = shim_sbrk(0);
	void *altstack;
	bool stack_fill = false;
	bool stack_mlock = false;
	bool stack_pageout = false;
	bool stack_unmap = false;
	NOCLOBBER int rc = EXIT_SUCCESS;

	(void)context;

	if (!stress_get_setting("stack-fill", &stack_fill)) {
		if (g_opt_flags & OPT_FLAGS_AGGRESSIVE)
			stack_fill = true;
	}
	if (!stress_get_setting("stack-mlock", &stack_mlock)) {
		if (g_opt_flags & OPT_FLAGS_AGGRESSIVE)
			stack_mlock = true;
	}
	if (!stress_get_setting("stack-pageout", &stack_pageout)) {
		if (g_opt_flags & OPT_FLAGS_AGGRESSIVE)
			stack_pageout = true;
	}
	if (!stress_get_setting("stack-unmap", &stack_unmap)) {
		if (g_opt_flags & OPT_FLAGS_AGGRESSIVE)
			stack_unmap = true;
	}

#if !defined(MADV_PAGEOUT)
	if (stack_pageout && (stress_instance_zero(args))) {
		pr_inf("%s: stack-pageout not supported on this system\n", args->name);
		stack_pageout = false;
	}
#endif

	/*
	 *  Allocate altstack on heap rather than an
	 *  autoexpanding stack that may trip a segfault
	 *  if there is no memory to back it later. Stack
	 *  must be privately mapped.
	 */
	altstack = stress_mmap_populate(NULL, STRESS_SIGSTKSZ,
		PROT_READ | PROT_WRITE,
		MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if (altstack == MAP_FAILED) {
		pr_inf_skip("%s: failed to mmap %zd byte signal stack%s, "
			"errno=%d (%s), skipping stressor\n",
			args->name, (size_t)STRESS_SIGSTKSZ,
			stress_get_memfree_str(), errno, strerror(errno));
		return EXIT_NO_RESOURCE;
	}
	stress_set_vma_anon_name(altstack, STRESS_SIGSTKSZ, "altstack");
	(void)stress_mincore_touch_pages(altstack, STRESS_SIGSTKSZ);

	/*
	 *  We need to create an alternative signal
	 *  stack so when a segfault occurs we use
	 *  this already allocated signal stack rather
	 *  than try to push onto an already overflowed
	 *  stack
	 */
	if (stress_sigaltstack(altstack, STRESS_SIGSTKSZ) < 0) {
		(void)munmap(altstack, STRESS_SIGSTKSZ);
		return EXIT_NO_RESOURCE;
	}

	stress_parent_died_alarm();

	if (start_ptr == (void *) -1) {
		pr_err("%s: sbrk(0) failed, errno=%d (%s)\n",
			args->name, errno, strerror(errno));
		return EXIT_FAILURE;
	}

	/* Make sure this is killable by OOM killer */
	stress_set_oom_adjustment(args, true);

	for (;;) {
		struct sigaction new_action;
		int ret;

		if (UNLIKELY(!stress_continue(args)))
			break;

		(void)shim_memset(&new_action, 0, sizeof new_action);
		new_action.sa_handler = stress_segvhandler;
		(void)sigemptyset(&new_action.sa_mask);
#if defined(HAVE_SIGALTSTACK)
		new_action.sa_flags = SA_ONSTACK;
#endif

		if (UNLIKELY(sigaction(SIGSEGV, &new_action, NULL) < 0)) {
			pr_fail("%s: sigaction on SIGSEGV failed, errno=%d (%s)\n",
				args->name, errno, strerror(errno));
			return EXIT_FAILURE;
		}
		if (UNLIKELY(sigaction(SIGBUS, &new_action, NULL) < 0)) {
			pr_fail("%s: sigaction on SIGBUS failed, errno=%d (%s)\n",
				args->name, errno, strerror(errno));
			return EXIT_FAILURE;
		}
		ret = sigsetjmp(jmp_env, 1);
		/*
		 * We return here if we segfault, so
		 * first check if we need to terminate
		 */
		if (UNLIKELY(!stress_continue(args)))
			break;

		if (ret) {
			/* We end up here after handling the fault */
			stress_bogo_inc(args);
		} else {
			char start;

			/* Expand the stack and cause a fault */
			if (!stress_stack_alloc(args, &start, NULL, stack_fill, stack_mlock, stack_pageout, stack_unmap, 0)) {
				rc = EXIT_FAILURE;
				break;
			}
		}
	}
	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);

	(void)munmap((void *)altstack, STRESS_SIGSTKSZ);

	return rc;
}

/*
 *  stress_stack
 *	stress by forcing stack overflows
 */
static int stress_stack(stress_args_t *args)
{
	stress_set_proc_state(args->name, STRESS_STATE_SYNC_WAIT);
	stress_sync_start_wait(args);
	stress_set_proc_state(args->name, STRESS_STATE_RUN);

	return stress_oomable_child(args, NULL, stress_stack_child, STRESS_OOMABLE_NORMAL);
}

const stressor_info_t stress_stack_info = {
	.stressor = stress_stack,
	.classifier = CLASS_VM | CLASS_MEMORY,
	.opts = opts,
	.verify = VERIFY_ALWAYS,
	.help = help
};

#else

const stressor_info_t stress_stack_info = {
	.stressor = stress_unimplemented,
	.classifier = CLASS_VM | CLASS_MEMORY,
	.opts = opts,
	.verify = VERIFY_ALWAYS,
	.help = help,
	.unimplemented_reason = "built without siglongjmp support"
};

#endif
