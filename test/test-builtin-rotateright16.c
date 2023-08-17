// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C)      2023 Colin Ian King
 *
 */

#include <stdint.h>

int main(int argc, char **argv)
{
	uint16_t x = (uint16_t)argc;

	return (int)__builtin_rotateright16(x, 1);
}
