// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2013-2021 Canonical, Ltd.
 * Copyright (C) 2022-2023 Colin Ian King.
 *
 */
#define  _GNU_SOURCE

#if defined(__gnu_hurd__) || defined(__aarch64__)
#error ustat is not implemented and will always fail on this system
#endif

#include <sys/types.h>
#include <unistd.h>
#include <ustat.h>
#if defined(__linux__)
#include <sys/sysmacros.h>
#endif

int main(void)
{
	dev_t dev = makedev(8, 1);
	struct ustat ubuf;

	return ustat(dev, &ubuf);
}
