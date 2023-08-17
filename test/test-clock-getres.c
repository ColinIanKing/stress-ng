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
	struct timespec res;

#if defined(CLOCK_REALTIME)
	return clock_getres(CLOCK_REALTIME, &res);
#elif defined(CLOCK_MONOTONIC)
	return clock_getres(CLOCK_MONOTONIC, &res);
#else
#error no POSIX clock types CLOCK_REALTIME or CLOCK_MONOTONIC
#endif
}
