/*
 * Copyright (C) 2013-2017 Canonical, Ltd.
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
 * This code is a complete clean re-write of the stress tool by
 * Colin Ian King <colin.king@canonical.com> and attempts to be
 * backwardly compatible with the stress tool by Amos Waterland
 * <apw@rossby.metr.ou.edu> but has more stress tests and more
 * functionality.
 *
 */
#include "stress-ng.h"

#define SECONDS_IN_MINUTE	(60.0)
#define SECONDS_IN_HOUR		(60.0 * SECONDS_IN_MINUTE)
#define SECONDS_IN_DAY		(24.0 * SECONDS_IN_HOUR)
#define SECONDS_IN_YEAR		(365.2425 * SECONDS_IN_DAY)
				/* Approx, for Gregorian calendar */
/*
 *  time_now()
 *	time in seconds as a double
 */
double time_now(void)
{
	struct timeval now;

	if (gettimeofday(&now, NULL) < 0)
		return -1.0;

	return timeval_to_double(&now);
}

/*
 *  format_time()
 *	format a unit of time into human readable format
 */
static inline void format_time(
	const bool last,		/* Last unit to format */
	const double secs_in_units,	/* Seconds in the specific time unit */
	const char *units,		/* Unit of time */
	char **ptr,			/* Destination string ptr */
	double *duration,		/* Duration left in seconds */
	size_t *len)			/* Length of string left at ptr */
{
	unsigned long val = (unsigned long)(*duration / secs_in_units);

	if (last || val > 0) {
		int ret;

		if (last)
			ret = snprintf(*ptr, *len, "%.2f %ss", *duration, units);
		else
			ret = snprintf(*ptr, *len, "%lu %s%s, ", val, units,
				(val > 1) ? "s" : "");
		if (ret > 0) {
			*len -= ret;
			*ptr += ret;
		}
	}
	*duration -= secs_in_units * (double)val;
}

/*
 *  duration_to_str
 *	duration in seconds to a human readable string
 */
const char *duration_to_str(const double duration)
{
	static char str[128];
	char *ptr = str;
	size_t len = sizeof(str) - 1;
	double dur = duration;

	*str = '\0';
	if (duration > 60.0) {
		strncpy(ptr, " (", len);
		ptr += 2;
		len -= 2;
		format_time(false, SECONDS_IN_YEAR, "year", &ptr, &dur, &len);
		format_time(false, SECONDS_IN_DAY, "day", &ptr, &dur, &len);
		format_time(false, SECONDS_IN_HOUR, "hour", &ptr, &dur, &len);
		format_time(false, SECONDS_IN_MINUTE, "min", &ptr, &dur, &len);
		format_time(true, 1, "sec", &ptr, &dur, &len);
		strncpy(ptr, ")", len);
	}
	return str;
}
