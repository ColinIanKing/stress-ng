/*
 * Copyright (C) 2013-2016 Canonical, Ltd.
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
#define _GNU_SOURCE

#include "stress-ng.h"

#if defined(STRESS_CONTEXT)

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>
#include <ucontext.h>

#define STACK_SIZE	(16384)

static ucontext_t uctx_main, uctx_thread1, uctx_thread2, uctx_thread3;
static uint64_t __counter, __max_ops;

static void thread1(void)
{
	do {
		__counter++;
		swapcontext(&uctx_thread1, &uctx_thread2);
	} while (opt_do_run && (!__max_ops || __counter < __max_ops));
}

static void thread2(void)
{
	do {
		__counter++;
		swapcontext(&uctx_thread2, &uctx_thread3);
	} while (opt_do_run && (!__max_ops || __counter < __max_ops));
}

static void thread3(void)
{
	do {
		__counter++;
		swapcontext(&uctx_thread3, &uctx_thread1);
	} while (opt_do_run && (!__max_ops || __counter < __max_ops));
}

static int stress_context_init(
	const char *name,
	void (*func)(void),
	ucontext_t *link,
	ucontext_t *uctx,
	void *stack,
	const size_t stack_size)
{
	if (getcontext(uctx) < 0) {
		pr_err(stderr, "%s: getcontext failed: %d (%s)\n",
			name, errno, strerror(errno));
		return -1;
	}
	uctx->uc_stack.ss_sp = stack;
	uctx->uc_stack.ss_size = stack_size;
	uctx->uc_link = link;
	makecontext(uctx, func, 0);

	return 0;
}

/*
 *  stress_context()
 *	stress that exercises CPU context save/restore
 */
int stress_context(
	uint64_t *const counter,
	const uint32_t instance,
	const uint64_t max_ops,
	const char *name)
{
	char stack_thread1[STACK_SIZE] ALIGN64,
	     stack_thread2[STACK_SIZE] ALIGN64,
	     stack_thread3[STACK_SIZE] ALIGN64;

	(void)instance;

	__counter = 0;
	__max_ops = max_ops * 1000;

	/* Create 3 micro threads */
	if (stress_context_init(name, thread1, &uctx_main,
				&uctx_thread1, stack_thread1,
				sizeof(stack_thread1)) < 0)
		return EXIT_FAILURE;
	if (stress_context_init(name, thread2, &uctx_main,
				&uctx_thread2, stack_thread2,
				sizeof(stack_thread2)) < 0)
		return EXIT_FAILURE;
	if (stress_context_init(name, thread3, &uctx_main,
				&uctx_thread3, stack_thread3,
				sizeof(stack_thread3)) < 0)
		return EXIT_FAILURE;

	/* And start.. */
	if (swapcontext(&uctx_main, &uctx_thread1) < 0) {
		pr_err(stderr, "%s: swapcontext failed: %d (%s)\n",
			name, errno, strerror(errno));
		return EXIT_FAILURE;
	}

	*counter = __counter / 1000;

	return EXIT_SUCCESS;
}

#endif
