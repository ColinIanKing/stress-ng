/*
 * Copyright (C) 2013-2015 Canonical, Ltd.
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

#include <stdio.h>
#include <stdlib.h>
#include <setjmp.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#if !defined(__FreeBSD__) && !defined(__OpenBSD__) && !defined(__NetBSD__)
#include <alloca.h>
#endif

#include "stress-ng.h"

static sigjmp_buf jmp_env;

/*
 *  stress_segvhandler()
 *	SEGV handler
 */
static void stress_segvhandler(int dummy)
{
	(void)dummy;

	siglongjmp(jmp_env, 1);		/* Ugly, bounce back */
}

/*
 *  stress_stack
 *	stress by forcing stack overflows
 */
int stress_stack(
	uint64_t *const counter,
	const uint32_t instance,
	const uint64_t max_ops,
	const char *name)
{
	uint8_t stack[SIGSTKSZ];
	stack_t ss;

	(void)instance;

	/*
	 *  We need to create an alternative signal
	 *  stack so when a segfault occurs we use
	 *  this already allocated signal stack rather
	 *  than try to push onto an already overflowed
	 *  stack
	 */
	memset(stack, 0, sizeof(stack));
	ss.ss_sp = (void *)stack;
	ss.ss_size = SIGSTKSZ;
	ss.ss_flags = 0;
	if (sigaltstack(&ss, NULL) < 0) {
		pr_failed_err(name, "sigaltstack");
		return EXIT_FAILURE;
	}

	for (;;) {
		struct sigaction new_action;
		int ret;

		if (!opt_do_run || (max_ops && *counter >= max_ops))
			break;

		memset(&new_action, 0, sizeof new_action);
		new_action.sa_handler = stress_segvhandler;
		sigemptyset(&new_action.sa_mask);
		new_action.sa_flags = SA_ONSTACK;

		if (sigaction(SIGSEGV, &new_action, NULL) < 0) {
			pr_failed_err(name, "sigaction");
			return EXIT_FAILURE;
		}
		ret = sigsetjmp(jmp_env, 1);
		/*
		 * We return here if we segfault, so
		 * first check if we need to terminate
		 */
		if (!opt_do_run || (max_ops && *counter >= max_ops))
			break;

		if (ret) {
			/* We end up here after handling the fault */
			(*counter)++;
		} else {
			/* Expand the stack and cause a fault */
			char *last_ptr = 0;
			do {
				char *ptr = alloca(256 * KB);

				/* need this else gcc optimises out the alloca */
				*ptr = 0;

				/* Force gcc to actually do the alloca */
				uint64_put((uint64_t)(last_ptr - ptr));
				last_ptr = ptr;
			} while (opt_do_run);
		}
	}

	return EXIT_SUCCESS;
}
