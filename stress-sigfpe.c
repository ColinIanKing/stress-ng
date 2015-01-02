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
#include <stdint.h>
#include <setjmp.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>

#include "stress-ng.h"

static sigjmp_buf jmp_env;

/*
 *  stress_fpehandler()
 *	SIGFPE handler
 */
static void stress_fpehandler(int dummy)
{
	(void)dummy;

	siglongjmp(jmp_env, 1);		/* Ugly, bounce back */
}

/*
 *  stress_sigfpe
 *	stress by generating floating point errors
 */
int stress_sigfpe(
	uint64_t *const counter,
	const uint32_t instance,
	const uint64_t max_ops,
	const char *name)
{
	(void)instance;

	for (;;) {
		struct sigaction new_action;
		int ret;

		memset(&new_action, 0, sizeof new_action);
		new_action.sa_handler = stress_fpehandler;
		sigemptyset(&new_action.sa_mask);
		new_action.sa_flags = 0;

		if (sigaction(SIGFPE, &new_action, NULL) < 0) {
			pr_failed_err(name, "sigfpe");
			return EXIT_FAILURE;
		}
		ret = sigsetjmp(jmp_env, 1);
		/*
		 * We return here if we get SIGFPE, so
		 * first check if we need to terminate
		 */
		if (!opt_do_run || (max_ops && *counter >= max_ops))
			break;

		if (ret)
			(*counter)++;	/* SIGFPE occurred */
		else
			uint64_put(1 / uint64_zero());	/* force division by zero */
	}

	return EXIT_SUCCESS;
}
