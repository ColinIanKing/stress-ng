/*
 * Copyright (C) 2013-2021 Canonical, Ltd.
 * Copyright (C) 2022-2025 Colin Ian King.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 *
 */
#include "stress-ng.h"
#include "core-attribute.h"

#include <time.h>

#define SECONDS_IN_MINUTE	(60.0)
#define SECONDS_IN_HOUR		(60.0 * SECONDS_IN_MINUTE)
#define SECONDS_IN_DAY		(24.0 * SECONDS_IN_HOUR)
#define SECONDS_IN_YEAR		(365.2425 * SECONDS_IN_DAY)
				/* Approx, for Gregorian calendar */

/*
 *  stress_timeval_to_double()
 *      convert timeval to seconds as a double
 */
double CONST OPTIMIZE3 stress_timeval_to_double(const struct timeval *tv)
{
	return (double)tv->tv_sec + ((double)tv->tv_usec * ONE_MILLIONTH);
}

/*
 *  stress_time_now_timeval()
 *	get time as double accurate to microseconds
 */
static OPTIMIZE3 int stress_time_now_timeval(double *dnow)
{
	struct timeval now;

	if (UNLIKELY(gettimeofday(&now, NULL) < 0))
		return -1;

	*dnow = stress_timeval_to_double(&now);
	return 0;
}

/*
 *  stress_time_now_timeval()
 *	get time as double accurate to nanoseconds
 */
static OPTIMIZE3 int stress_time_now_timespec(double *dnow)
{
#if defined(HAVE_CLOCK_GETTIME) &&     \
    defined(CLOCK_REALTIME)
	struct timespec ts;

	if (UNLIKELY(clock_gettime(CLOCK_REALTIME, &ts) < 0))
		return -1;

	*dnow = (double)ts.tv_sec + ((double)ts.tv_nsec * ONE_BILLIONTH);
	return 0;
#else
	(void)dnow;

	return -1;
#endif
}

static int (*stress_time_now_func)(double *now) = stress_time_now_timespec;

/*
 *  stress_time_now()
 *	time in seconds as a double
 */
double OPTIMIZE3 stress_time_now(void)
{
	double now;

	if (LIKELY(stress_time_now_func(&now) == 0))
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
		if (LIKELY(stress_time_now_timeval(&now) == 0))
			return now;
	}
	return -1.0;
}

/*
 *  stress_format_time()
 *	format a unit of time into human readable format
 */
static inline void stress_format_time(
	bool *emitted,			/* data has been emitted */
	const bool always_format,	/* true = always format a value */
	const bool int_val,		/* Last unit in integer form */
	const double secs_in_units,	/* Seconds in the specific time unit */
	const char *units,		/* Unit of time */
	char **ptr,			/* Destination string ptr */
	double *duration,		/* Duration left in seconds */
	size_t *len)			/* Length of string left at ptr */
{
	const unsigned long int val = (unsigned long int)(*duration / secs_in_units);

	if (always_format || (val > 0)) {
		int ret;

		if (*emitted) {
			ret = snprintf(*ptr, *len, ", ");
			if (ret > 0) {
				*len -= (size_t)ret;
				*ptr += ret;
			}
		}

		if (int_val) {
			ret = snprintf(*ptr, *len, "%.2f %s%s", *duration, units, (val > 1) ? "s" : "");
		} else {
			ret = snprintf(*ptr, *len, "%lu %s%s", val, units, (val > 1) ? "s" : "");
		}
		if (ret > 0) {
			*len -= (size_t)ret;
			*ptr += ret;
		}
		*emitted = true;
	}
	*duration -= secs_in_units * (double)val;
}

/*
 *  stress_duration_to_str
 *	duration in seconds to a human readable string
 */
const char *stress_duration_to_str(const double duration, const bool int_secs, const bool report_secs)
{
	static char str[128];
	char *ptr = str;
	size_t len = sizeof(str) - 1;
	double dur = duration;
	bool emitted = false;

	*str = '\0';
	stress_format_time(&emitted, false, false, SECONDS_IN_YEAR, "year", &ptr, &dur, &len);
	stress_format_time(&emitted, false, false, SECONDS_IN_DAY, "day", &ptr, &dur, &len);
	stress_format_time(&emitted, false, false, SECONDS_IN_HOUR, "hour", &ptr, &dur, &len);
	stress_format_time(&emitted, false, false, SECONDS_IN_MINUTE, "min", &ptr, &dur, &len);
	stress_format_time(&emitted, report_secs, int_secs, 1, "sec", &ptr, &dur, &len);

	if (!*str)
		shim_strscpy(str, "0 secs", sizeof(str));
	return str;
}
