// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2013-2021 Canonical, Ltd.
 * Copyright (C) 2022-2023 Colin Ian King
 *
 */
#define _GNU_SOURCE

#include <sys/time.h>
#include <sys/resource.h>
#include <sys/types.h>
#include <unistd.h>

int main(void)
{
	struct rlimit old_rlim;
	const pid_t pid = getpid();

	return prlimit(pid, RLIMIT_CPU, NULL, &old_rlim);
}
