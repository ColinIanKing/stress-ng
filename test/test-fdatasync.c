// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2013-2021 Canonical, Ltd.
 * Copyright (C) 2022-2023 Colin Ian King
 *
 */

#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

int main(void)
{
	static const char filename[] = "/tmp/test-fdatasync.tmp";
	int fd, err = 1;

	fd = open(filename, O_RDWR | O_CREAT, 0666);
	if (fd < 0)
		return 1;
	(void)unlink(filename);

	if (fdatasync(fd) < 0)
		goto err;
	err = 0;
err:
	(void)close(fd);

	return err;
}
