// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2013-2021 Canonical, Ltd.
 * Copyright (C) 2022-2023 Colin Ian King
 *
 */
#define _GNU_SOURCE
#include <signal.h>
#include <poll.h>

#define MAX_FDS		(3)

int main(void)
{
	struct pollfd fds[MAX_FDS];
	struct timespec ts;
	sigset_t sigmask;
	int fd;

	for (fd = 0; fd < MAX_FDS; fd++) {
		fds[fd].fd = fd;
		fds[fd].events = (fd == 0) ? POLLIN : POLLOUT;
		fds[fd].revents = 0;
	}
	ts.tv_sec = 1;
	ts.tv_nsec = 999999999;

	(void)sigemptyset(&sigmask);
	(void)sigaddset(&sigmask, SIGTERM);

	return ppoll(fds, 3, &ts, &sigmask);
}
