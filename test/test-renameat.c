// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2013-2021 Canonical, Ltd.
 * Copyright (C) 2022-2023 Colin Ian King
 *
 */

#define _GNU_SOURCE

#include <stdio.h>
#include <fcntl.h>
#include <stdio.h>

int main(void)
{
	int ret;

	ret = renameat(AT_FDCWD, "test-old-file", AT_FDCWD, "test-new-file");
	return ret;
}
