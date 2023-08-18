// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2013-2021 Canonical, Ltd.
 * Copyright (C) 2022-2023 Colin Ian King.
 *
 */
#include "stress-ng.h"

#define SECONDS_IN_MINUTE	(60.0)
#define SECONDS_IN_HOUR		(60.0 * SECONDS_IN_MINUTE)
#define SECONDS_IN_DAY		(24.0 * SECONDS_IN_HOUR)
#define SECONDS_IN_YEAR		(365.2425 * SECONDS_IN_DAY)
				/* Approx, for Gregorian calendar */


/*
 *  stress_timeval_to_double()
 *      convert timeval to seconds as a double
 */
double OPTIMIZE3 stress_timeval_to_double(const struct timeval *tv)
{
	return (double)tv->tv_sec + ((double)tv->tv_usec * ONE_MILLIONTH);
}

/*
 *  stress_timespec_to_double()
 *      convert timespec to seconds as a double
 */
double OPTIMIZE3 stress_timespec_to_double(const struct timespec *ts)
{
	return (double)ts->tv_sec + ((double)ts->tv_nsec * ONE_BILLIONTH);
}

static OPTIMIZE3 double stress_time_now_timeval(void)
{
	struct timeval now;

	if (gettimeofday(&now, NULL) < 0)
		return -1.0;

	return stress_timeval_to_double(&now);
}

static OPTIMIZE3 double stress_time_now_timespec(void)
{
#if defined(HAVE_CLOCK_GETTIME) &&     \
    defined(CLOCK_MONOTONIC)
	struct timespec ts;

	if (clock_gettime(CLOCK_REALTIME, &ts) < 0)
		return -1.0;

	return stress_timespec_to_double(&ts);
#else
	return -1.0;
#endif
}

static double (*stress_time_now_func)(void) = stress_time_now_timespec;

/*
 *  stress_time_now()
 *	time in seconds as a double
 */
double OPTIMIZE3 stress_time_now(void)
{
	const double now = stress_time_now_func();

	if (LIKELY(now) >= 0.0)
		return now;

	/*
	 *  It failed, check if we used clock_gettime and if so
	 *  drop back to use gettimeofday instead
	 */
	if (LIKELY(stress_time_now_func == stress_time_now_timespec)) {
		/*
		 *  Drop to older 1/1000000th second resolution clock
		 */
		stress_time_now_func = stress_time_now_timeval;
		return stress_time_now_timeval();
	}

	/* Unlikely, no time available! */
	return -1.0;
}

/*
 *  stress_format_time()
 *	format a unit of time into human readable format
 */
static inline void stress_format_time(
	const bool last,		/* Last unit to format */
	const bool int_val,		/* Last unit in integer form */
	const double secs_in_units,	/* Seconds in the specific time unit */
	const char *units,		/* Unit of time */
	char **ptr,			/* Destination string ptr */
	double *duration,		/* Duration left in seconds */
	size_t *len)			/* Length of string left at ptr */
{
	const unsigned long val = (unsigned long)(*duration / secs_in_units);

	if (last || (val > 0)) {
		int ret;

		if (last) {
			if (int_val) {
				ret = snprintf(*ptr, *len, "%.2f %ss", *duration, units);
			} else {
				ret = snprintf(*ptr, *len, "%lu %ss", val, units);
			}
		} else {
			ret = snprintf(*ptr, *len, "%lu %s%s, ", val, units,
				(val > 1) ? "s" : "");
		}
		if (ret > 0) {
			*len -= (size_t)ret;
			*ptr += ret;
		}
	}
	*duration -= secs_in_units * (double)val;
}

/*
 *  stress_duration_to_str
 *	duration in seconds to a human readable string
 */
const char *stress_duration_to_str(const double duration, const bool int_secs)
{
	static char str[128];
	char *ptr = str;
	size_t len = sizeof(str) - 1;
	double dur = duration;

	*str = '\0';
	stress_format_time(false, false, SECONDS_IN_YEAR, "year", &ptr, &dur, &len);
	stress_format_time(false, false, SECONDS_IN_DAY, "day", &ptr, &dur, &len);
	stress_format_time(false, false, SECONDS_IN_HOUR, "hour", &ptr, &dur, &len);
	stress_format_time(false, false, SECONDS_IN_MINUTE, "min", &ptr, &dur, &len);
	stress_format_time(true, int_secs, 1, "sec", &ptr, &dur, &len);
	return str;
}
