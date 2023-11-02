// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2022-2023 Colin Ian King
 *
 */

#define _GNU_SOURCE

#include <string.h>
#include <sched.h>

int main(void)
{
	cpu_set_t mask;

	(void)memset(&mask, 0, sizeof(mask));
	return sched_setaffinity(0, sizeof(mask), &mask);
}
