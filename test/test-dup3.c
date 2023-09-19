// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2013-2021 Canonical, Ltd.
 * Copyright (C) 2022-2023 Colin Ian King
 *
 */

#define _GNU_SOURCE
#include <fcntl.h>
#include <unistd.h>

#if defined(__FreeBSD_kernel__)
#error dup3 is not implemented with FreeBSD kernel
#endif

int main(void)
{
	int fd1, fd2, fd3, ret = 1;

	fd1 = open("/dev/zero", O_RDONLY);
	if (fd1 < 0)
		goto err0;

	fd2 = open("/dev/null", O_WRONLY);
	if (fd2 < 0)
		goto err1;

	/* fd2 is closed by the dup3 */
	fd3 = dup3(fd1, fd2, O_CLOEXEC);
	if (fd3 < 0)
		goto err2;
	fd2 = fd3;
	(void)close(fd3);

	ret = 0;
	goto err1:
err2:
	(void)close(fd2);
err1:
	(void)close(fd1);
err0:
	return ret;
}
