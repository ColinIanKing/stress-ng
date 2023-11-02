// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2013-2021 Canonical, Ltd.
 * Copyright (C) 2022-2023 Colin Ian King.
 *
 */

#define _GNU_SOURCE

#include <signal.h>
#include <time.h>

int main(void)
{
	struct sigevent sev = { 0 };
	timer_t timerid;

	sev.sigev_notify = SIGEV_SIGNAL;
	sev.sigev_signo = SIGRTMIN;
	sev.sigev_value.sival_ptr = &timerid;

#if defined(CLOCK_REALTIME)
	return timer_create(CLOCK_REALTIME, &sev, &timerid);
#elif defined(CLOCK_MONOTONIC)
	return clock_settime(CLOCK_MONOTONIC, &sev, &timerid);
#else
#error no POSIX clock types CLOCK_REALTIME or CLOCK_MONOTONIC
#endif
}
