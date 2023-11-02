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
#include <unistd.h>
#include <sched.h>

int main(void)
{
	int fd;
	int rc = 0;

	fd = open("/proc/self/ns/uts", O_RDONLY);
	if (fd < 0)
		return 1;

	if (setns(fd, 0) < 0)
		rc = 1;
	(void)close(fd);

	return rc;
}
