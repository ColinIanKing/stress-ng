// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2013-2021 Canonical, Ltd.
 * Copyright (C) 2022-2023 Colin Ian King
 *
 */
#define _GNU_SOURCE

#define _FILE_OFFSET_BITS       (64)

#include <sys/types.h>

int main(void)
{
	const ino64_t ino = 0;

	(void)ino;

	return 0;
}
