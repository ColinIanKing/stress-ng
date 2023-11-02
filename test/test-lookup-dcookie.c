// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2013-2021 Canonical, Ltd.
 * Copyright (C) 2022-2023 Colin Ian King
 *
 */
#define _GNU_SOURCE

#include <unistd.h>
#include <sys/syscall.h>

#if !defined(__NR_lookup_dcookie)
#error __NR_lookup_dcookie syscall not defined
#endif

int main(void)
{
	char buf[4096];

	return syscall(__NR_lookup_dcookie, buf, sizeof(buf));
}
