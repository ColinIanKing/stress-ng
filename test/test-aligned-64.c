// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2017-2021 Canonical, Ltd.
 * Copyright (C) 2022-2023 Colin Ian King.
 *
 */
#include <stdint.h>

int __attribute__ ((aligned(64))) test_align64(void);

int main(void)
{
	const intptr_t addr = (intptr_t)test_align64;

	(void)test_align64();

	return addr & 63;
}

int __attribute__ ((aligned(64))) test_align64(void)
{
	return 0;
}
