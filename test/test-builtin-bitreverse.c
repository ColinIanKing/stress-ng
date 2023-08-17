// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2022-2023 Colin Ian King
 *
 */

#include <stdint.h>
#include <stdlib.h>

int main(int argc, char **argv)
{
	const uint64_t r = (uint64_t)random() ^ ((uint64_t)random() << 32);
	uint8_t u8 = __builtin_bitreverse8(r & 0xff);
	uint16_t u16 = __builtin_bitreverse16(r & 0xffff);
	uint32_t u32 = __builtin_bitreverse32(r & 0xffffffffUL);
	uint64_t u64 = __builtin_bitreverse64(r);

	return (int)u64 | (int)u32 | (int)u16 | (int)u8;
}
