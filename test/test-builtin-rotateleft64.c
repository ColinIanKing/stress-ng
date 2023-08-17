// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C)      2023 Colin Ian King
 *
 */

#include <stdint.h>

int main(int argc, char **argv)
{
	uint64_t x = (uint64_t)argc;

	return (int)__builtin_rotateleft64(x, 1);
}
