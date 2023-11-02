// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2013-2021 Canonical, Ltd.
 * Copyright (C) 2022-2023 Colin Ian King.
 *
 */

#include <sys/types.h>
#include <unistd.h>
#include <signal.h>

#if defined(__gnu_hurd__)
#error sigqueue is defined but not implemented and will always fail
#endif

int main(void)
{
	const pid_t pid = getpid();
	union sigval value;

	value.sival_int = 0;

	return sigqueue(pid, SIGALRM, value);
}
