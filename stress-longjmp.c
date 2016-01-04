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

#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sched.h>
#include <setjmp.h>

/*
 *  stress_jmp()
 *	stress system by setjmp/longjmp calls
 */
int stress_longjmp(
	uint64_t *const counter,
	const uint32_t instance,
	const uint64_t max_ops,
	const char *name)
{
	int ret;
	static jmp_buf buf;

	(void)instance;
	(void)name;

	ret = setjmp(buf);

	if (ret) {
		static int c = 0;

		c++;
		if (c >= 1000) {
			(*counter)++;
			c = 0;
		}
	}
	if (opt_do_run && (!max_ops || *counter < max_ops))
		longjmp(buf, 1);

	return EXIT_SUCCESS;
}
