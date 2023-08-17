// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2021-2023 Colin Ian King
 *
 */
#include <stdint.h>

int main(int argc, char **argv)
{
	__uint128_t v128 = ~0, data128;
	uint64_t v64 = ~0, data64;
	uint32_t v32 = ~0, data32;

	__builtin_nontemporal_store(v128, &data128);
	__builtin_nontemporal_store(v64, &data64);
	__builtin_nontemporal_store(v32, &data32);

	return 0;
}
