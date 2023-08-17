// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2013-2021 Canonical, Ltd.
 * Copyright (C) 2022-2023 Colin Ian King
 *
 */

#define _GNU_SOURCE

#include <sched.h>

int main(void)
{
	cpu_set_t mask;

	return sched_getaffinity(0, sizeof(mask), &mask);
}
