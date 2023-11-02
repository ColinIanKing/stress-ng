// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2013-2021 Canonical, Ltd.
 * Copyright (C) 2022-2023 Colin Ian King
 *
 */
#define _GNU_SOURCE
#include <sched.h>
#include <signal.h>
#include <stdio.h>

#define STACK_SIZE	(65536)

static int clone_child(void *arg)
{
	(void)arg;

	return 0;
}

int main(void)
{
	pid_t pid;
	static unsigned long stack[STACK_SIZE];

	pid = clone(clone_child, &stack[STACK_SIZE - 1], SIGCHLD | CLONE_VM, NULL);

	return pid;
}
