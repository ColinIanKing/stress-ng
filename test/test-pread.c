// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2022-2023 Colin Ian King
 *
 */
#define  _GNU_SOURCE

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

int main(void)
{
	char data[1024];
	int fd, rc;

	fd = open("/dev/zero", O_RDONLY);
	if (fd < 0)
		return -1;

	rc = pread(fd, data, sizeof(data), 0);
	(void)close(fd);

	return rc;
}
