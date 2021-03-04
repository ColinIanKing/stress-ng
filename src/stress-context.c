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

static stress_help_t help[] = {
	{ NULL,	"context N",	 "start N workers exercising user context" },
	{ NULL,	"context-ops N", "stop context workers after N bogo operations" },
	{ NULL,	NULL,		 NULL }
};

#if defined(HAVE_SWAPCONTEXT) &&	\
    defined(HAVE_UCONTEXT_H)

#define CONTEXT_STACK_SIZE	(16384)

typedef struct {
	uint32_t check0;	/* memory clobbering check canary */
	ucontext_t uctx;	/* swapcontext context */
	uint32_t check1;	/* memory clobbering check canary */
} chk_ucontext_t;

typedef struct {
	uint32_t check0;	/* copy of original check1 canary */
	uint32_t check1;	/* copy of original check1 canary */
} chk_canary_t;

typedef struct {
	chk_ucontext_t	cu;	/* check ucontext */
	uint8_t		stack[CONTEXT_STACK_SIZE + STACK_ALIGNMENT]; /* stack */
	chk_canary_t	canary;	/* copy of canary */
} context_info_t;

static context_info_t context[3];
static ucontext_t uctx_main;
static uint64_t __counter, __max_ops;

static void thread1(void)
{
	do {
		__counter++;
		(void)swapcontext(&context[0].cu.uctx, &context[1].cu.uctx);
	} while (keep_stressing_flag() && (!__max_ops || __counter < __max_ops));

	(void)swapcontext(&context[0].cu.uctx, &uctx_main);
}

static void thread2(void)
{
	do {
		__counter++;
		(void)swapcontext(&context[1].cu.uctx, &context[2].cu.uctx);
	} while (keep_stressing_flag() && (!__max_ops || __counter < __max_ops));
	(void)swapcontext(&context[1].cu.uctx, &uctx_main);
}

static void thread3(void)
{
	do {
		__counter++;
		(void)swapcontext(&context[2].cu.uctx, &context[0].cu.uctx);
	} while (keep_stressing_flag() && (!__max_ops || __counter < __max_ops));
	(void)swapcontext(&context[2].cu.uctx, &uctx_main);
}

static int stress_context_init(
	const stress_args_t *args,
	void (*func)(void),
	ucontext_t *uctx_link,
	context_info_t *context_info)
{
	if (getcontext(&context_info->cu.uctx) < 0) {
		pr_err("%s: getcontext failed: %d (%s)\n",
			args->name, errno, strerror(errno));
		return -1;
	}

	context_info->canary.check0 = stress_mwc32();
	context_info->canary.check1 = stress_mwc32();

	context_info->cu.check0 = context_info->canary.check0;
	context_info->cu.check1 = context_info->canary.check1;
	context_info->cu.uctx.uc_stack.ss_sp =
		(void *)stress_align_address(context_info->stack, STACK_ALIGNMENT);
	context_info->cu.uctx.uc_stack.ss_size = CONTEXT_STACK_SIZE;
	context_info->cu.uctx.uc_link = uctx_link;
	makecontext(&context_info->cu.uctx, func, 0);

	return 0;
}

/*
 *  stress_context()
 *	stress that exercises CPU context save/restore
 */
static int stress_context(const stress_args_t *args)
{
	uint8_t *stack_sig;
	size_t i;

	stack_sig = (uint8_t *)mmap(NULL, STRESS_SIGSTKSZ, PROT_READ | PROT_WRITE,
			MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
	if (stack_sig == MAP_FAILED) {
		pr_inf("%s: cannot allocate signal stack, errno=%d (%s)\n",
			args->name, errno, strerror(errno));
		return EXIT_NO_RESOURCE;
	}

	if (stress_sigaltstack(stack_sig, STRESS_SIGSTKSZ) < 0)
		return EXIT_FAILURE;

	__counter = 0;
	__max_ops = args->max_ops * 1000;

	/* Create 3 micro threads */
	if (stress_context_init(args, thread1, &uctx_main, &context[0]) < 0)
		return EXIT_FAILURE;
	if (stress_context_init(args, thread2, &uctx_main, &context[1]) < 0)
		return EXIT_FAILURE;
	if (stress_context_init(args, thread3, &uctx_main, &context[2]) < 0)
		return EXIT_FAILURE;

	/* And start.. */
	if (swapcontext(&uctx_main, &context[0].cu.uctx) < 0) {
		pr_err("%s: swapcontext failed: %d (%s)\n",
			args->name, errno, strerror(errno));
		return EXIT_FAILURE;
	}

	set_counter(args, __counter / 1000);

	stress_set_proc_state(args->name, STRESS_STATE_RUN);

	for (i = 0; i < SIZEOF_ARRAY(context); i++) {
		if (context[i].canary.check0 != context[i].cu.check0) {
			pr_fail("%s: swapcontext clobbered data before context region\n",
				args->name);
		}
		if (context[i].canary.check1 != context[i].cu.check1) {
			pr_fail("%s: swapcontext clobbered data after context region\n",
				args->name);
		}
	}
	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);
	(void)munmap((void *)stack_sig, STRESS_SIGSTKSZ);

	return EXIT_SUCCESS;
}

stressor_info_t stress_context_info = {
	.stressor = stress_context,
	.class = CLASS_MEMORY | CLASS_CPU,
	.help = help
};
#else
stressor_info_t stress_context_info = {
	.stressor = stress_not_implemented,
	.class = CLASS_MEMORY | CLASS_CPU,
	.help = help
};
#endif
