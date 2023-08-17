// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2013-2021 Canonical, Ltd.
 * Copyright (C) 2022-2023 Colin Ian King
 *
 */
#define _GNU_SOURCE

#include <sys/mman.h>

int main(void)
{
	return remap_file_pages((void *)0, 4096, 0, 0, MAP_SHARED);
}
