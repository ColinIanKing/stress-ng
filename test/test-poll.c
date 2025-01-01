/*
 * Copyright (C) 2024-2025 Colin Ian King
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
#include <signal.h>
#include <poll.h>

#define MAX_FDS		(3)

int main(void)
{
	struct pollfd fds[MAX_FDS];
	int fd;

	for (fd = 0; fd < MAX_FDS; fd++) {
		fds[fd].fd = fd;
		fds[fd].events = (fd == 0) ? POLLIN : POLLOUT;
		fds[fd].revents = 0;
	}

	return poll(fds, 3, 0);
}
