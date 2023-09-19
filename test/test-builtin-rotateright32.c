// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C)      2023 Colin Ian King
 *
 */

#include <stdint.h>

int main(int argc, char **argv)
{
	uint32_t x = (uint32_t)argc;

	return (int)__builtin_rotateright32(x, 1);
}
