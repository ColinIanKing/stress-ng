// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2013-2021 Canonical, Ltd.
 * Copyright (C) 2022-2023 Colin Ian King.
 *
 */
#define  _GNU_SOURCE

#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#if defined(__FreeBSD_kernel__)
#error syncfs is not implemented with FreeBSD kernel
#endif

int main(void)
{
	static const char filename[] = "/tmp/test-syncfs.tmp";
	int fd, err = 1;

	fd = open(filename, O_RDWR | O_CREAT, 0666);
	if (fd < 0)
		return 1;
	(void)unlink(filename);

	if (syncfs(fd) < 0)
		goto err;
	err = 0;
err:
	(void)close(fd);

	return err;
}
