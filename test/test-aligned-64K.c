// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2017-2021 Canonical, Ltd.
 * Copyright (C) 2022-2023 Colin Ian King.
 *
 */
#include <stdint.h>

int __attribute__ ((aligned(65536))) test_align64K(void);

int main(void)
{
	const intptr_t addr = (intptr_t)test_align64K;

	(void)test_align64K();

	return addr & 65535;
}

int __attribute__ ((aligned(65536))) test_align64K(void)
{
	return 0;
}
