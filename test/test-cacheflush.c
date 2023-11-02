// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2022-2023 Colin Ian King.
 *
 */
#include <asm/cachectl.h>

int main(void)
{
	static char buffer[4096];

	return cacheflush(buffer, 64, 1);
}
