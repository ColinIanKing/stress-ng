// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2022-2023 Colin Ian King
 *
 */

#define _LARGEFILE64_SOURCE

#include <sys/types.h>
#include <unistd.h>
#include <fcntl.h>

int main(void)
{
	off64_t offset = 0, ret;
	int fd;

	fd = open("/dev/zero", O_RDONLY);
	if (fd >= 0) {
		ret = lseek64(fd, offset, SEEK_SET);
		(void)ret;
		(void)close(fd);
	}

	return 0;
}
