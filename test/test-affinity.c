/*
 * Copyright (C) 2013-2019 Canonical, Ltd.
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
#include <sched.h>

#if defined(__gnu_hurd__)
#error sched_getaffinity and sched_setaffinity are not implemented
#endif

int main(void)
{
	cpu_set_t mask;
	int rc;

	CPU_ZERO(&mask);
	CPU_SET(0, &mask);
	rc = sched_setaffinity(0, sizeof(mask), &mask);
	(void)rc;

	rc = sched_getaffinity(0, sizeof(mask), &mask);
	if (rc == 0) {
		rc = CPU_ISSET(0, &mask);
		(void)rc;
	}
	return 0;
}
