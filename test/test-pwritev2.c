// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2013-2021 Canonical, Ltd.
 * Copyright (C) 2022-2023 Colin Ian King
 *
 */
#define _GNU_SOURCE

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/uio.h>
#include <unistd.h>

int main(void)
{
	struct iovec iov;
	char buffer[] = "hello world\n";
	int fd, rc;

	fd = open("/dev/zero", O_WRONLY);
	if (fd < 0)
		return -1;

	iov.iov_base = buffer;
	iov.iov_len = sizeof(buffer);

	rc = pwritev2(fd, &iov, 1, -1, 0);
	(void)close(fd);

	return rc;
}
