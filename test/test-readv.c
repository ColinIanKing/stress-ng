/*
 * Copyright (C) 2025      Colin Ian King
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

	rc = readv(fd, iov, 1);
	(void)close(fd);

	return rc;
}
