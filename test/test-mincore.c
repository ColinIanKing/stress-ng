// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2013-2021 Canonical, Ltd.
 * Copyright (C) 2022-2023 Colin Ian King
 *
 */

#include <unistd.h>
#include <stdint.h>
#include <sys/mman.h>

static char buffer[8192];

#if defined(__gnu_hurd__)
#error mincore is defined but not implemented and will always fail
#endif

int main(void)
{
	unsigned char vec[1];
	uintptr_t ptr = (((uintptr_t)buffer) & ~(4096 -1));

	return mincore((void *)ptr, sizeof(vec), vec);
}
