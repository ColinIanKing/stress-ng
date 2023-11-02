// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2022-2023 Colin Ian King.
 *
 */

int main(void)
{
	__asm__ __volatile__("" ::: "memory");

	return 0;
}

