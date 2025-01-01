/*
 * Copyright (C) 2022-2025 Colin Ian King
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

#define _LARGEFILE64_SOURCE

#include <sys/types.h>
#include <unistd.h>
#include <fcntl.h>

int main(void)
{
	int fd;

	fd = open("/dev/zero", O_RDONLY);
	if (fd >= 0) {
		off64_t offset = 0, ret;

		ret = lseek64(fd, offset, SEEK_SET);
		(void)ret;
		(void)close(fd);
	}

	return 0;
}
