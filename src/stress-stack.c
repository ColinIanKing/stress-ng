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

static sigjmp_buf jmp_env;

static const stress_help_t help[] = {
	{ NULL,	"stack N",	"start N workers generating stack overflows" },
	{ NULL,	"stack-ops N",	"stop after N bogo stack overflows" },
	{ NULL,	"stack-fill",	"fill stack, touches all new pages " },
	{ NULL, "stack-mlock",	"mlock stack, force pages to be unswappable" },
	{ NULL,	NULL,		NULL }
};

static int stress_set_stack_fill(const char *opt)
{
	bool stack_fill = true;

	(void)opt;
	return stress_set_setting("stack-fill", TYPE_ID_BOOL, &stack_fill);
}

static int stress_set_stack_mlock(const char *opt)
{
	bool stack_mlock = true;

	(void)opt;
	return stress_set_setting("stack-mlock", TYPE_ID_BOOL, &stack_mlock);
}

static const stress_opt_set_func_t opt_set_funcs[] = {
	{ OPT_stack_fill,	stress_set_stack_fill },
	{ OPT_stack_mlock,	stress_set_stack_mlock },
	{ 0,			NULL }
};

/*
 *  stress_segvhandler()
 *	SEGV handler
 */
static void MLOCKED_TEXT stress_segvhandler(int signum)
{
	(void)signum;

	siglongjmp(jmp_env, 1);		/* Ugly, bounce back */
}

/*
 *  stress_stack_alloc()
 *	eat up stack. The default is to eat up lots of pages
 *	but only have 25% of the pages actually in memory
 *	so we a large stack with lots of pages not physically
 *	resident.
 */
static void stress_stack_alloc(
	const stress_args_t *args,
	void *start,
	const bool stack_fill,
	bool stack_mlock,
	ssize_t last_size)
{
	const size_t sz = 256 * KB;
	const size_t page_size = args->page_size;
	const size_t page_size4 = page_size << 2;
	uint8_t data[sz];

	if (stack_fill) {
		(void)memset(data, 0, sz);
	} else {
		register size_t i;

		/*
		 *  Touch 25% of the pages, ensure data
		 *  is random and non-zero to avoid
		 *  kernel same page merging
		 */
		for (i = 0; i < sz; i += page_size4) {
			uint32_t *ptr = (uint32_t *)(data + i);

			*ptr = stress_mwc32();
			*(ptr + 1) = stress_mwc32() | 1;
		}
	}
#if defined(HAVE_MLOCK)
	if (stack_mlock) {
		intptr_t ptr = ((intptr_t)data) + (page_size - 1);
		ssize_t mlock_sz = (uint8_t *)start - (uint8_t *)ptr;

		if (mlock_sz < 0)
			mlock_sz = -mlock_sz;

		if (mlock_sz > (last_size + 8 * (ssize_t)MB)) {
			int ret;

			ptr &= ~(page_size - 1);
			ret = shim_mlock((void *)ptr, mlock_sz - last_size);
			if (ret < 0)
				stack_mlock = false;
			last_size = mlock_sz;
		}
	}
#endif
	inc_counter(args);

	if (keep_stressing(args))
		stress_stack_alloc(args, start, stack_fill, stack_mlock, last_size);
}

static int stress_stack_child(const stress_args_t *args, void *context)
{
	char *start_ptr = shim_sbrk(0);
	void *altstack;
	bool stack_fill = false;
	bool stack_mlock = false;

	(void)context;

	(void)stress_get_setting("stack-fill", &stack_fill);
	(void)stress_get_setting("stack-mlock", &stack_mlock);

	/*
	 *  Allocate altstack on heap rather than an
	 *  autoexpanding stack that may trip a segfault
	 *  if there is no memory to back it later. Stack
	 *  must be privately mapped.
	 */
	altstack = mmap(NULL, STRESS_SIGSTKSZ, PROT_READ | PROT_WRITE,
		MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if (altstack == MAP_FAILED) {
		pr_inf("%s: cannot allocate signal stack: errno = %d (%s)\n",
			args->name, errno, strerror(errno));
		return EXIT_NO_RESOURCE;
	}
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

	(void)setpgid(0, g_pgrp);
	stress_parent_died_alarm();

	if (start_ptr == (void *) -1) {
		pr_err("%s: sbrk(0) failed: errno=%d (%s)\n",
			args->name, errno, strerror(errno));
		return EXIT_FAILURE;
	}

	/* Make sure this is killable by OOM killer */
	stress_set_oom_adjustment(args->name, true);

	for (;;) {
		struct sigaction new_action;
		int ret;

		if (!keep_stressing(args))
			break;

		(void)memset(&new_action, 0, sizeof new_action);
		new_action.sa_handler = stress_segvhandler;
		(void)sigemptyset(&new_action.sa_mask);
		new_action.sa_flags = SA_ONSTACK;

		if (sigaction(SIGSEGV, &new_action, NULL) < 0) {
			pr_fail("%s: sigaction on SIGSEGV failed, errno=%d (%s)\n",
				args->name, errno, strerror(errno));
			return EXIT_FAILURE;
		}
		if (sigaction(SIGBUS, &new_action, NULL) < 0) {
			pr_fail("%s: sigaction on SIGBUS failed, errno=%d (%s)\n",
				args->name, errno, strerror(errno));
			return EXIT_FAILURE;
		}
		ret = sigsetjmp(jmp_env, 1);
		/*
		 * We return here if we segfault, so
		 * first check if we need to terminate
		 */
		if (!keep_stressing(args))
			break;

		if (ret) {
			/* We end up here after handling the fault */
			inc_counter(args);
		} else {
			char start;

			/* Expand the stack and cause a fault */
			stress_stack_alloc(args, &start, stack_fill, stack_mlock, 0);
		}
	}
	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);

	(void)munmap((void *)altstack, STRESS_SIGSTKSZ);

	return EXIT_SUCCESS;
}

/*
 *  stress_stack
 *	stress by forcing stack overflows
 */
static int stress_stack(const stress_args_t *args)
{
	stress_set_proc_state(args->name, STRESS_STATE_RUN);

	return stress_oomable_child(args, NULL, stress_stack_child, STRESS_OOMABLE_NORMAL);
}

stressor_info_t stress_stack_info = {
	.stressor = stress_stack,
	.class = CLASS_VM | CLASS_MEMORY,
	.opt_set_funcs = opt_set_funcs,
	.help = help
};
