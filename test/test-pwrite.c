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
	char data[] = "Test";
	int fd, rc;

	fd = open("/dev/null", O_WRONLY);
	if (fd < 0)
		return -1;

	rc = pwrite(fd, data, sizeof(data), 0);
	(void)close(fd);

	return rc;
}
