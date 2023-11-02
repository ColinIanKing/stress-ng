// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2013-2021 Canonical, Ltd.
 * Copyright (C) 2022-2023 Colin Ian King.
 *
 */

#define _GNU_SOURCE

#include <fcntl.h>
#include <stdlib.h>

int main(void)
{
	int fd_in = 3;
	int fd_out = 4;

	return splice(fd_in, NULL, fd_out, NULL, 1024, 0);

}
