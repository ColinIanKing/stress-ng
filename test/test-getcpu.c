// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2021-2023 Colin Ian King.
 *
 */
#define _GNU_SOURCE
#include <sched.h>

int main(void)
{
	int cpu = 0;
	unsigned int node = 0;

	return getcpu(&cpu, &node);
}
