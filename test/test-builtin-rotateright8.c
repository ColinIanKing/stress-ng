// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C)      2023 Colin Ian King
 *
 */

#include <stdint.h>

int main(int argc, char **argv)
{
	uint8_t x = (uint8_t)argc;

	return (int)__builtin_rotateright8(x, 1);
}
