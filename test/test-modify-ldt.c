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

#include <unistd.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <asm/ldt.h>
#include <string.h>

#if !defined(__NR_modify_ldt)
#error modify_ldt syscall not defined
#endif

/* Arch specific, x86 */
#if defined(__x86_64__) || defined(__x86_64) || \
    defined(__i386__)   || defined(__i386)
#else
#error modify_ldt syscall not applicable for non-x86 architectures
#endif

int main(void)
{
	struct user_desc ud;
	int ret;

	(void)memset(&ud, 0, sizeof(ud));
	ret = syscall(__NR_modify_ldt, 0, &ud, sizeof(ud));
	if (ret == 0)
		ret = syscall(__NR_modify_ldt, 1, &ud, sizeof(ud));

	return ret;
}
