// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2013-2021 Canonical, Ltd.
 * Copyright (C) 2022-2023 Colin Ian King
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
