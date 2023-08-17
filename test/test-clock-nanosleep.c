// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2013-2021 Canonical, Ltd.
 * Copyright (C) 2022-2023 Colin Ian King
 *
 */

#define _GNU_SOURCE

#include <time.h>

int main(void)
{
	struct timespec req = { 0 };
	struct timespec rem;

#if defined(CLOCK_REALTIME)
	return clock_nanosleep(CLOCK_REALTIME, 0, &req, &rem);
#elif defined(CLOCK_MONOTONIC)
	return clock_settime(CLOCK_MONOTONIC, 0, &req, &rem);
#else
#error no POSIX clock types CLOCK_REALTIME or CLOCK_MONOTONIC
#endif
}
