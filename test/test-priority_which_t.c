// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2022-2023 Colin Ian King
 *
 */
#define _GNU_SOURCE

#include <sys/resource.h>

int main(void)
{
	__priority_which_t p = 0;

	return (int)p;
}
