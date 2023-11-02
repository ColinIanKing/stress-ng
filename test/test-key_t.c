// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2022-2023 Colin Ian King
 *
 */
#define _GNU_SOURCE

#include <sys/sem.h>

int main(void)
{
	key_t key = (key_t)0;

	return (int)key;
}
