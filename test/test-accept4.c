/*
 * Copyright (C) 2013-2021 Canonical, Ltd.
 * Copyright (C) 2022-2025 Colin Ian King.
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

#define _GNU_SOURCE

#include <sys/types.h>
#include <sys/socket.h>

int main(void)
{
	int fd = -1;
	struct sockaddr addr = { 0 };
	socklen_t addrlen = 0;

	/*
	 *  We don't care about invalid parameters, this code is just
	 *  to see if this compiles
	 */
	return accept4(fd, &addr, &addrlen, SOCK_NONBLOCK | SOCK_CLOEXEC);
}
