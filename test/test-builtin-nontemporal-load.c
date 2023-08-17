// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2021-2023 Colin Ian King
 *
 */
#include <stdint.h>

int main(int argc, char **argv)
{
	__uint128_t v128, data128 = ~0;
	uint64_t v64, data64 = ~0;
	uint32_t v32, data32 = ~0;

	v128 = __builtin_nontemporal_load(&data128);
	(void)v128;
	v64 = __builtin_nontemporal_load(&data64);
	(void)v64;
	v32 = __builtin_nontemporal_load(&data32);
	(void)v32;

	return 0;
}
