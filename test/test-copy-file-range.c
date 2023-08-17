// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2013-2021 Canonical, Ltd.
 * Copyright (C) 2022-2023 Colin Ian King
 *
 */

#define _GNU_SOURCE

#include <unistd.h>
#include <fcntl.h>

int main(void)
{
	return copy_file_range(0, NULL, 0, NULL, 1024, 0);
}
