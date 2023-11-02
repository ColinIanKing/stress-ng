// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2022-2023 Colin Ian King
 *
 */
#define _GNU_SOURCE

#include <sys/time.h>

int main(void)
{
	__itimer_which_t i = 0;

	return (int)i;
}
