// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C)      2023 Colin Ian King
 *
 */

int main(int argc, char **argv)
{
	unsigned long dst;
	unsigned long src = ~0;

	return __builtin_memcmp(&dst, &src, sizeof(dst));
}

