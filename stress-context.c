/*
 * Copyright (C) 2013-2017 Canonical, Ltd.
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

#if !defined(__OpenBSD__)

#include <ucontext.h>

#define STACK_SIZE	(16384)

static uint8_t stack_sig[SIGSTKSZ + STACK_ALIGNMENT];

static ucontext_t uctx_main, uctx_thread1, uctx_thread2, uctx_thread3;
static uint64_t __counter, __max_ops;

static void thread1(void)
{
	do {
		__counter++;
		swapcontext(&uctx_thread1, &uctx_thread2);
	} while (g_keep_stressing_flag && (!__max_ops || __counter < __max_ops));
}

static void thread2(void)
{
	do {
		__counter++;
		swapcontext(&uctx_thread2, &uctx_thread3);
	} while (g_keep_stressing_flag && (!__max_ops || __counter < __max_ops));
}

static void thread3(void)
{
	do {
		__counter++;
		swapcontext(&uctx_thread3, &uctx_thread1);
	} while (g_keep_stressing_flag && (!__max_ops || __counter < __max_ops));
}

static int stress_context_init(
	const args_t *args,
	void (*func)(void),
	ucontext_t *link,
	ucontext_t *uctx,
	void *stack,
	const size_t stack_size)
{
	if (getcontext(uctx) < 0) {
		pr_err("%s: getcontext failed: %d (%s)\n",
			args->name, errno, strerror(errno));
		return -1;
	}
	uctx->uc_stack.ss_sp = (void *)stack;
	uctx->uc_stack.ss_size = stack_size;
	uctx->uc_link = link;
	makecontext(uctx, func, 0);

	return 0;
}

/*
 *  stress_context()
 *	stress that exercises CPU context save/restore
 */
int stress_context(const args_t *args)
{
	char stack_thread1[STACK_SIZE + STACK_ALIGNMENT],
	     stack_thread2[STACK_SIZE + STACK_ALIGNMENT],
	     stack_thread3[STACK_SIZE + STACK_ALIGNMENT];

	if (stress_sigaltstack(stack_sig, SIGSTKSZ) < 0)
		return EXIT_FAILURE;

	__counter = 0;
	__max_ops = args->max_ops * 1000;

	/* Create 3 micro threads */
	if (stress_context_init(args, thread1, &uctx_main,
				&uctx_thread1,
				align_address(stack_thread1, STACK_ALIGNMENT),
				STACK_SIZE) < 0)
		return EXIT_FAILURE;
	if (stress_context_init(args, thread2, &uctx_main,
				&uctx_thread2,
				align_address(stack_thread2, STACK_ALIGNMENT),
				STACK_SIZE) < 0)
		return EXIT_FAILURE;
	if (stress_context_init(args, thread3, &uctx_main,
				&uctx_thread3,
				align_address(stack_thread3, STACK_ALIGNMENT),
				STACK_SIZE) < 0)
		return EXIT_FAILURE;

	/* And start.. */
	if (swapcontext(&uctx_main, &uctx_thread1) < 0) {
		pr_err("%s: swapcontext failed: %d (%s)\n",
			args->name, errno, strerror(errno));
		return EXIT_FAILURE;
	}

	*args->counter = __counter / 1000;

	return EXIT_SUCCESS;
}
#else
int stress_context(const args_t *args)
{
	return stress_not_implemented(args);
}
#endif
