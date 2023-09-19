// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2013-2021 Canonical, Ltd.
 * Copyright (C) 2022-2023 Colin Ian King
 *
 */

#include <stdint.h>
#include <sys/mman.h>

static char buffer[8192];

int main(void)
{
	uintptr_t ptr = (((uintptr_t)buffer) & ~(4096 -1));

	return munlock((void *)ptr, 4096);
}
