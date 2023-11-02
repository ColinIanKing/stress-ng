// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2022-2023 Colin Ian King
 *
 */

#include <stdint.h>
#include <stdlib.h>

int main(int argc, char **argv)
{
	uint32_t u32 = __builtin_bswap32(0x12345678);

	return (int)u32;
}
