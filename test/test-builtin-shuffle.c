// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2022-2023 Colin Ian King
 *
 */
#include <stdint.h>
#include <string.h>

#define VEC_ELEMENTS	(32)

int main(int argc, char **argv)
{
	uint64_t data __attribute__ ((vector_size(VEC_ELEMENTS * sizeof(uint64_t))));
	uint64_t mask __attribute__ ((vector_size(VEC_ELEMENTS * sizeof(uint64_t))));
	uint64_t init[VEC_ELEMENTS];
	size_t i;
	uint64_t xsum = 0;

	for (i = 0; i < VEC_ELEMENTS; i++)
		init[i] = i;
	(void)memcpy(&data, &init, sizeof(data));

	for (i = 0; i < VEC_ELEMENTS; i++)
		init[i] = (i + 3) & (VEC_ELEMENTS - 1);
	(void)memcpy(&mask, &init, sizeof(mask));

	data = __builtin_shuffle(data, mask);

	(void)memcpy(&init, &data, sizeof(init));

	for (i = 0; i < VEC_ELEMENTS; i++)
		xsum ^= init[i];

	return (int)xsum;
}

