/*
 * Copyright (C) 2013-2019 Canonical, Ltd.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 *
 * This code is a complete clean re-write of the stress tool by
 * Colin Ian King <colin.king@canonical.com> and attempts to be
 * backwardly compatible with the stress tool by Amos Waterland
 * <apw@rossby.metr.ou.edu> but has more stress tests and more
 * functionality.
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

	ret = 0;
err2:
	(void)close(fd2);
err1:
	(void)close(fd1);
err0:
	return ret;
}
