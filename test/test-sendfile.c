// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2022-2023 Colin Ian King
 *
 */

#define _GNU_SOURCE

#include <sys/sendfile.h>

int main(void)
{
	off_t offset = 0;
	size_t count = 4096;

	return sendfile(0, 1, &offset, count);
}
