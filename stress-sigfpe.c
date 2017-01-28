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

static sigjmp_buf jmp_env;

/*
 *  stress_fpehandler()
 *	SIGFPE handler
 */
static void MLOCKED stress_fpehandler(int dummy)
{
	(void)dummy;

	siglongjmp(jmp_env, 1);		/* Ugly, bounce back */
}

/*
 *  stress_sigfpe
 *	stress by generating floating point errors
 */
int stress_sigfpe(const args_t *args)
{
	for (;;) {
		int ret;

		if (stress_sighandler(args->name, SIGFPE, stress_fpehandler, NULL) < 0)
			return EXIT_FAILURE;

		ret = sigsetjmp(jmp_env, 1);
		/*
		 * We return here if we get SIGFPE, so
		 * first check if we need to terminate
		 */
		if (!keep_stressing())
			break;

		if (ret)
			inc_counter(args);	/* SIGFPE occurred */
		else
			uint64_put(1 / uint64_zero());	/* force division by zero */
	}

	return EXIT_SUCCESS;
}
