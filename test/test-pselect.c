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

#include <signal.h>
/* For POSIX.1-2001, POSIX.1-2008 */
#include <sys/select.h>
/* For earlier standards */
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>

#define MAX_FDS		(3)

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
