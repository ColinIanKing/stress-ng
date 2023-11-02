// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2013-2021 Canonical, Ltd.
 * Copyright (C) 2022-2023 Colin Ian King.
 *
 */

#define _GNU_SOURCE

#include <fcntl.h>
#include <sys/uio.h>

int main(void)
{
	int fd = 3;
	struct iovec iov = { 0 };
	unsigned long nr_segs = 1;
	unsigned int flags = 0;

	return vmsplice(fd, &iov, nr_segs, flags);
}
