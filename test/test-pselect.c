// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2013-2021 Canonical, Ltd.
 * Copyright (C) 2022-2023 Colin Ian King
 *
 */
#define _GNU_SOURCE

#include <signal.h>
/* For POSIX.1-2001, POSIX.1-2008 */
#include <sys/select.h>
/* For earlier standards */
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>

#define MAX_FDS		(3)

#if defined(__serenity__)
#error Serenity OS does not currently support pselect
#endif

int main(void)
{
	static fd_set rfds, wfds;

	struct timespec ts;
	sigset_t sigmask;

	FD_ZERO(&rfds);
	FD_ZERO(&wfds);

	FD_SET(0, &rfds);
	FD_SET(1, &wfds);
	FD_SET(2, &wfds);

	ts.tv_sec = 1;
	ts.tv_nsec = 999999999;

	(void)sigemptyset(&sigmask);
	(void)sigaddset(&sigmask, SIGTERM);

	return pselect(3, &rfds, &wfds, NULL, &ts, &sigmask);
}
