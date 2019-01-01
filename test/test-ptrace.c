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

#include <sys/ptrace.h>

#define NULL ((void *)0)

/* Check if we can do basic ptrace functionality for stress-ng */

int main(void)
{
	int pid = 1, ret;
	void *addr = NULL;
	unsigned long data;

	ret = ptrace(PTRACE_SYSCALL, pid, 0, 0);
	(void)ret;

	ret = ptrace(PTRACE_TRACEME);
	(void)ret;

	ret = ptrace(PTRACE_SETOPTIONS, pid, 0, PTRACE_O_TRACESYSGOOD);
	(void)ret;

	ret = ptrace(PTRACE_ATTACH, pid, NULL, NULL);
	(void)ret;

	ret = ptrace(PTRACE_DETACH, pid, NULL, NULL);
	(void)ret;

	ret = ptrace(PTRACE_PEEKDATA, pid, addr, &data);
	(void)ret;

	return 0;
}
