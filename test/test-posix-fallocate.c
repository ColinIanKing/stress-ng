// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2013-2021 Canonical, Ltd.
 * Copyright (C) 2022-2023 Colin Ian King
 *
 */

#include <unistd.h>
#include <fcntl.h>

int main(void)
{
	static const char filename[] = "/tmp/test-msync.tmp";
	int fd, ret;

	fd = open(filename, O_RDWR | O_CREAT, 0666);
	if (fd < 0)
		return 1;
	(void)unlink(filename);

	ret = posix_fallocate(fd, 4096, 512);
	(void)close(fd);

	return ret;
}
