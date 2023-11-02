// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2022-2023 Colin Ian King
 *
 */
#define _GNU_SOURCE

#include <stdlib.h>
/* For POSIX.1-2001, POSIX.1-2008 */
#include <sys/select.h>

#if defined(__serenity__)
#error Serenity OS does not currently support pselect
#endif

int main(void)
{
	static fd_set rfds, wfds;

	struct timeval tv;

	FD_ZERO(&rfds);
	FD_ZERO(&wfds);

	FD_SET(0, &rfds);
	FD_SET(1, &wfds);
	FD_SET(2, &wfds);

	tv.tv_sec = 1;
	tv.tv_usec = 999999;

	return select(3, &rfds, &wfds, NULL, &tv);
}
