/*
 * Copyright (C) 2013-2021 Canonical, Ltd.
 * Copyright (C) 2022-2025 Colin Ian King
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
	static unsigned long int stack[STACK_SIZE];

	pid = clone(clone_child, &stack[STACK_SIZE - 1], SIGCHLD | CLONE_VM, NULL);

	return pid;
}
