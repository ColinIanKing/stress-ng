// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2022-2023 Colin Ian King
 *
 */

#define _GNU_SOURCE

#include <string.h>
#include <time.h>
#include <sys/timex.h>

int main(void)
{
	struct timex buf;

	(void)memset(&buf, 0, sizeof(buf));
	buf.modes = 0;

#if defined(CLOCK_REALTIME)
	return clock_adjtime(CLOCK_REALTIME, &buf);
#elif defined(CLOCK_MONOTONIC)
	return clock_adjtime(CLOCK_MONOTONIC, &buf);
#else
#error no POSIX clock types CLOCK_REALTIME or CLOCK_MONOTONIC
#endif
}
