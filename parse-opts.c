/*
 * Copyright (C) 2013-2016 Canonical, Ltd.
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
#define _GNU_SOURCE

#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <ctype.h>
#include <inttypes.h>
#include <errno.h>

#include "stress-ng.h"

/*
 *  check_value()
 *	sanity check number of workers
 */
void check_value(
	const char *const msg,
	const int val)
{
	if ((val < 0) || (val > STRESS_PROCS_MAX)) {
		fprintf(stderr, "Number of %s workers must be between "
			"0 and %d\n", msg, STRESS_PROCS_MAX);
		exit(EXIT_FAILURE);
	}
}

/*
 *  check_range()
 *	Sanity check val against a lo - hi range
 */
void check_range(
	const char *const opt,
	const uint64_t val,
	const uint64_t lo,
	const uint64_t hi)
{
	if ((val < lo) || (val > hi)) {
		fprintf(stderr, "Value %" PRId64 " is out of range for %s,"
			" allowed: %" PRId64 " .. %" PRId64 "\n",
			val, opt, lo, hi);
		exit(EXIT_FAILURE);
	}
}

/*
 *  ensure_positive()
 * 	ensure string contains just a +ve value
 */
static void ensure_positive(const char *const str)
{
	char *ptr;
	bool negative = false;

	for (ptr = (char *)str; *ptr; ptr++) {
		if (*ptr == '-') {
			negative = true;
			continue;
		}

		if (isdigit((int)*ptr)) {
			if (!negative)
				return;

			fprintf(stderr, "Invalid negative number %s\n", str);
			exit(EXIT_FAILURE);
		}
	}
}

/*
 *  get_unsigned_long()
 *	string to unsigned long
 */
unsigned long get_unsigned_long(const char *const str)
{
	unsigned long val;

	ensure_positive(str);
	if (sscanf(str, "%lu" , &val) != 1) {
		fprintf(stderr, "Invalid number %s\n", str);
		exit(EXIT_FAILURE);
	}
	return val;
}

/*
 *  get_int32()
 *	string to int
 */
int32_t get_int32(const char *const str)
{
	int32_t val;

	if (sscanf(str, "%12" SCNd32, &val) != 1) {
		fprintf(stderr, "Invalid number %s\n", str);
		exit(EXIT_FAILURE);
	}
	return val;
}

/*
 *  get_uint64()
 *	string to uint64_t
 */
uint64_t get_uint64(const char *const str)
{
	uint64_t val;

	ensure_positive(str);
	if (sscanf(str, "%" SCNu64, &val) != 1) {
		fprintf(stderr, "Invalid number %s\n", str);
		exit(EXIT_FAILURE);
	}
	return val;
}

/*
 *  get_uint64_scale()
 *	get a value and scale it by the given scale factor
 */
uint64_t get_uint64_scale(
	const char *const str,
	const scale_t scales[],
	const char *const msg)
{
	uint64_t val;
	size_t len = strlen(str);
	int ch;
	int i;

	val = get_uint64(str);
	if (!len)  {
		fprintf(stderr, "Value %s is an invalid size\n", str);
		exit(EXIT_FAILURE);
	}
	len--;
	ch = str[len];

	if (isdigit(ch))
		return val;

	ch = tolower(ch);
	for (i = 0; scales[i].ch; i++) {
		if (ch == scales[i].ch)
			return val * scales[i].scale;
	}

	printf("Illegal %s specifier %c\n", msg, str[len]);
	exit(EXIT_FAILURE);
}

/*
 *  get_uint64_byte()
 *	size in bytes, K bytes, M bytes or G bytes
 */
uint64_t get_uint64_byte(const char *const str)
{
	static const scale_t scales[] = {
		{ 'b', 	1 },
		{ 'k',  1 << 10 },
		{ 'm',  1 << 20 },
		{ 'g',  1 << 30 },
		{ 0,    0 },
	};

	return get_uint64_scale(str, scales, "length");
}

/*
 *  get_uint64_time()
 *	time in seconds, minutes, hours, days or years
 */
uint64_t get_uint64_time(const char *const str)
{
	static const scale_t scales[] = {
		{ 's', 	1 },
		{ 'm',  60 },
		{ 'h',  3600 },
		{ 'd',  24 * 3600 },
		{ 'y',  365 * 24 * 3600 },
	};

	return get_uint64_scale(str, scales, "time");
}
