// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2013-2021 Canonical, Ltd.
 * Copyright (C) 2022-2023 Colin Ian King
 *
 */
#define  _GNU_SOURCE

#include <sys/io.h>

#define IO_PORT		0x80

int main(void)
{
	int ret;

	ret = ioperm(IO_PORT, 1, 1);
	if (ret < 0)
		return 1;

	ret = inb(IO_PORT);
	outb(0xff, IO_PORT);
	(void)ioperm(IO_PORT, 1, 0);

	return ret;
}
