// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2013-2021 Canonical, Ltd.
 * Copyright (C) 2022-2023 Colin Ian King.
 *
 */
#define  _GNU_SOURCE

#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

int main(void)
{
	siginfo_t info;

	return waitid(P_PID, getpid(), &info, 0);
}
