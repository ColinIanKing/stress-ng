// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2021-2023 Colin Ian King
 *
 */
#include <stdint.h>

int main(int argc, char **argv)
{
	uint32_t *ptr, val1 = 0x01, val2 = 0x00, data = 0x00;

	ptr = &data;
	__sync_bool_compare_and_swap(ptr, val1, val2);

	return *ptr;
}

