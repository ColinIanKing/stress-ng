// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2021-2023 Colin Ian King
 *
 */
#include <xmmintrin.h>

int main(void)
{
	const __v2di v = { 0, 0 };

	return sizeof(v);
}

