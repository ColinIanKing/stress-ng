// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2013-2021 Canonical, Ltd.
 * Copyright (C) 2022-2023 Colin Ian King.
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
