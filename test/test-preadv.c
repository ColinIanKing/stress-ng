// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2013-2021 Canonical, Ltd.
 * Copyright (C) 2022-2023 Colin Ian King
 *
 */
#define  _GNU_SOURCE

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/uio.h>
#include <unistd.h>

#define IO_LEN	(64)

int main(void)
{
	struct iovec iov[1];
	char data[IO_LEN];
	int fd, rc;

	fd = open("/dev/zero", O_RDONLY);
	if (fd < 0)
		return -1;

	iov[0].iov_base = data;
	iov[0].iov_len = (size_t)IO_LEN;

	rc = preadv(fd, iov, 1, 0);
	(void)close(fd);

	return rc;
}
