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

/*
 *  check_value()
 *	sanity check number of workers
 */
void check_value(
	const char *const msg,
	const int val)
{
	if ((val < 0) || (val > STRESS_PROCS_MAX)) {
		(void)fprintf(stderr, "Number of %s workers must be between "
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
		(void)fprintf(stderr, "Value %" PRId64 " is out of range for %s,"
			" allowed: %" PRId64 " .. %" PRId64 "\n",
			val, opt, lo, hi);
		exit(EXIT_FAILURE);
	}
}

/*
 *  check_range()
 *	Sanity check val against a lo - hi range
 */
void check_range_bytes(
	const char *const opt,
	const uint64_t val,
	const uint64_t lo,
	const uint64_t hi)
{
	if ((val < lo) || (val > hi)) {
		char strval[32], strlo[32], strhi[32];

		(void)fprintf(stderr, "Value %sB is out of range for %s,"
			" allowed: %sB .. %sB\n",
			stress_uint64_to_str(strval, sizeof(strval), val),
			opt,
			stress_uint64_to_str(strlo, sizeof(strlo), lo),
			stress_uint64_to_str(strhi, sizeof(strhi), hi));
		exit(EXIT_FAILURE);
	}
}


/*
 *  ensure_positive()
 * 	ensure string contains just a +ve value
 */
static void ensure_positive(const char *const str)
{
	const char *ptr;
	bool negative = false;

	for (ptr = str; *ptr; ptr++) {
		if (*ptr == '-') {
			negative = true;
			continue;
		}

		if (isdigit((int)*ptr)) {
			if (!negative)
				return;

			(void)fprintf(stderr, "Invalid negative number %s\n", str);
			exit(EXIT_FAILURE);
		}
	}
}

/*
 *  get_uint32()
 *	string to uint32_t
 */
uint32_t get_uint32(const char *const str)
{
	uint32_t val;

	ensure_positive(str);
	if (sscanf(str, "%12" SCNu32, &val) != 1) {
		(void)fprintf(stderr, "Invalid number %s\n", str);
		exit(EXIT_FAILURE);
	}
	return val;
}

/*
 *  get_int32()
 *	string to int32_t
 */
int32_t get_int32(const char *const str)
{
	int32_t val;

	if (sscanf(str, "%12" SCNd32, &val) != 1) {
		(void)fprintf(stderr, "Invalid number %s\n", str);
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
		(void)fprintf(stderr, "Invalid number %s\n", str);
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
		(void)fprintf(stderr, "Value %s is an invalid size\n", str);
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

	(void)fprintf(stderr, "Illegal %s specifier %c\n", msg, str[len]);
	exit(EXIT_FAILURE);
}

/*
 *  get_uint64_byte()
 *	size in bytes, K bytes, M bytes or G bytes
 */
uint64_t get_uint64_byte(const char *const str)
{
	static const scale_t scales[] = {
		{ 'b', 	1ULL },		/* bytes */
		{ 'k',  1ULL << 10 },	/* kilobytes */
		{ 'm',  1ULL << 20 },	/* megabytes */
		{ 'g',  1ULL << 30 },	/* gigabytes */
		{ 't',  1ULL << 40 },	/* terabytes */
		{ 'p',  1ULL << 50 },	/* petabytes */
		{ 'e',  1ULL << 60 },	/* exabytes */
		{ 0,    0 },
	};

	return get_uint64_scale(str, scales, "length");
}

/*
 *  get_uint64_byte_memory()
 *	get memory size from string. If it contains %
 *	at the end, then covert it into the available
 *	physical memory scaled by that percentage divided
 *	by the number of stressor instances
 */
uint64_t get_uint64_byte_memory(
	const char *const str,
	const uint32_t instances)
{
	size_t len = strlen(str);

	/* Convert to % of memory over N instances */
	if ((len > 1) && (str[len - 1] == '%')) {
		uint64_t phys_mem = stress_get_phys_mem_size();
		double val;

		/* Should NEVER happen */
		if (instances < 1) {
			(void)fprintf(stderr, "Invalid number of instances\n");
			exit(EXIT_FAILURE);
		}
		if (sscanf(str, "%lf", &val) != 1) {
			(void)fprintf(stderr, "Invalid percentage %s\n", str);
			exit(EXIT_FAILURE);
		}
		if (phys_mem == 0) {
			(void)fprintf(stderr, "Cannot determine physical memory size\n");
			exit(EXIT_FAILURE);
		}
		return (uint64_t)((double)(phys_mem * val) / (100.0 * instances));
        }
	return get_uint64_byte(str);
}

/*
 *  get_uint64_byte_filesystem()
 *	get file size from string. If it contains %
 *	at the end, then covert it into the available
 *	file system space scaled by that percentage divided
 *	by the number of stressor instances
 */
uint64_t get_uint64_byte_filesystem(
	const char *const str,
	const uint32_t instances)
{
	size_t len = strlen(str);

	/* Convert to % of available filesystem space over N instances */
	if ((len > 1) && (str[len - 1] == '%')) {
		uint64_t bytes = stress_get_filesystem_size();
		double val;

		/* Should NEVER happen */
		if (instances < 1) {
			(void)fprintf(stderr, "Invalid number of instances\n");
			exit(EXIT_FAILURE);
		}
		if (sscanf(str, "%lf", &val) != 1) {
			(void)fprintf(stderr, "Invalid percentage %s\n", str);
			exit(EXIT_FAILURE);
		}
		if (bytes == 0) {
			(void)fprintf(stderr, "Cannot determine available space on file system\n");
			exit(EXIT_FAILURE);
		}
		return (uint64_t)((double)(bytes * val) / (100.0 * instances));
        }
	return get_uint64_byte(str);
}

/*
 *  get_uint64_time()
 *	time in seconds, minutes, hours, days or years
 */
uint64_t get_uint64_time(const char *const str)
{
	static const scale_t scales[] = {
		{ 's', 	1ULL },			/* seconds */
		{ 'm',  60ULL },		/* minutes */
		{ 'h',  3600ULL },		/* hours */
		{ 'd',  24ULL * 3600 },		/* days */
		{ 'w',  24ULL * 3600 * 7 },	/* weeks */
		{ 'y',  31556926ULL },		/* years (equinoctial) */
	};

	return get_uint64_scale(str, scales, "time");
}
