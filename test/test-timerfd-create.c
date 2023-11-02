// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2013-2021 Canonical, Ltd.
 * Copyright (C) 2022-2023 Colin Ian King.
 *
 */

#define _GNU_SOURCE

#include <sys/timerfd.h>

int main(void)
{
#if defined(CLOCK_REALTIME)
	return timerfd_create(CLOCK_REALTIME, 0);
#elif defined(CLOCK_MONOTONIC)
	return timerfd_create(CLOCK_MONOTONIC, 0);
#else
#error no POSIX clock types CLOCK_REALTIME or CLOCK_MONOTONIC
#endif
}
