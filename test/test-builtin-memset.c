// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C)      2023 Colin Ian King
 *
 */

int main(int argc, char **argv)
{
	unsigned long dst;

	__builtin_memset(&dst, 0, sizeof(dst));

	return 0;
}

