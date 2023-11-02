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
#error sigwaitinfo is defined but not implemented and will always fail
#endif

int main(void)
{
	sigset_t mask;
	siginfo_t info;

	(void)sigemptyset(&mask);
	(void)sigaddset(&mask, SIGUSR1);

	return sigwaitinfo(&mask, &info);
}
