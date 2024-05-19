/*
 * Copyright (C) 2013-2021 Canonical, Ltd.
 * Copyright (C) 2022-2024 Colin Ian King.
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
#include "core-parse-opts.h"
#include "core-cpu-cache.h"

/*
 *  stress_check_max_stressors()
 *	sanity check number of stressors
 */
void stress_check_max_stressors(
	const char *const msg,
	const int val)
{
	if ((val < 0) || (val > STRESS_PROCS_MAX)) {
		(void)fprintf(stderr, "Number of %s stressors must be between "
			"0 and %d\n", msg, STRESS_PROCS_MAX);
		longjmp(g_error_env, 1);
	}
}


/*
 *  stress_check_range()
 *	Sanity check val against a lo - hi range
 */
void stress_check_range(
	const char *const opt,
	const uint64_t val,
	const uint64_t lo,
	const uint64_t hi)
{
	if ((val < lo) || (val > hi)) {
		(void)fprintf(stderr, "Value %" PRId64 " is out of range for %s,"
			" allowed: %" PRId64 " .. %" PRId64 "\n",
			val, opt, lo, hi);
		longjmp(g_error_env, 1);
	}
}

/*
 *  stress_check_range()
 *	Sanity check val against a lo - hi range
 */
void stress_check_range_bytes(
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
		longjmp(g_error_env, 1);
	}
}

/*
 *  stress_ensure_numeric()
 *	ensure just numeric values
 */
static void stress_ensure_numeric(const char *const str)
{
	const char *ptr = str;

	if (*ptr == '-')
		ptr++;
	while (*ptr) {
		if (!isdigit((int)*ptr))
			break;
		ptr++;
	}
	if (*ptr == '\0')
		return;
	(void)fprintf(stderr, "Value %s contains non-numeric: '%s'\n",
		str, ptr);
	longjmp(g_error_env, 1);
}


/*
 *  stress_ensure_positive()
 * 	ensure string contains just a +ve value
 */
static void stress_ensure_positive(const char *const str)
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
			longjmp(g_error_env, 1);
		}
	}
}

/*
 *  stress_get_uint32()
 *	string to uint32_t
 */
uint32_t stress_get_uint32(const char *const str)
{
	uint64_t val;

	stress_ensure_positive(str);
	stress_ensure_numeric(str);
	if (sscanf(str, "%" SCNu64, &val) != 1) {
		(void)fprintf(stderr, "Invalid number %s\n", str);
		longjmp(g_error_env, 1);
	}
	if (val > UINT32_MAX) {
		(void)fprintf(stderr, "Invalid number %s too large (> %u)\n",
			str, UINT32_MAX);
		longjmp(g_error_env, 1);
	}
	return (uint32_t)val;
}

/*
 *  stress_get_int32()
 *	string to int32_t
 */
int32_t stress_get_int32(const char *const str)
{
	int64_t val;

	stress_ensure_numeric(str);
	if (sscanf(str, "%" SCNd64, &val) != 1) {
		(void)fprintf(stderr, "Invalid number %s\n", str);
		longjmp(g_error_env, 1);
	}
	if (val > INT32_MAX) {
		(void)fprintf(stderr, "Invalid number %s too large (> %ld)\n",
			str, (long)INT32_MAX);
		longjmp(g_error_env, 1);
	}
	if (val < INT32_MIN) {
		(void)fprintf(stderr, "Invalid number %s too small (< %ld)\n",
			str, (long)INT32_MIN);
		longjmp(g_error_env, 1);
	}
	return (int32_t)val;
}

/*
 *  stress_get_uint64()
 *	string to uint64_t
 */
uint64_t stress_get_uint64(const char *const str)
{
	uint64_t val;

	stress_ensure_positive(str);
	stress_ensure_numeric(str);
	if (sscanf(str, "%" SCNu64, &val) != 1) {
		(void)fprintf(stderr, "Invalid number %s\n", str);
		longjmp(g_error_env, 1);
	}
	return val;
}

/*
 *  stress_get_uint64_scale()
 *	get a value and scale it by the given scale factor
 */
uint64_t stress_get_uint64_scale(
	const char *const str,
	const stress_scale_t scales[],
	const char *const msg)
{
	uint64_t val;
	size_t len = strlen(str);
	int ch;
	int i;

	stress_ensure_positive(str);
	if (sscanf(str, "%" SCNu64, &val) != 1) {
		(void)fprintf(stderr, "Invalid number %s\n", str);
		goto err;
	}

	if (!len) {
		(void)fprintf(stderr, "Value %s is an invalid size\n", str);
		goto err;
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
err:
	longjmp(g_error_env, 1);
	/* should never get here */
	return 0;
}

/*
 *  stress_get_uint64_byte()
 *	size in bytes, K bytes, M bytes or G bytes
 */
uint64_t stress_get_uint64_byte(const char *const str)
{
	static const stress_scale_t scales[] = {
		{ 'b', 	1ULL },		/* bytes */
		{ 'k',  1ULL << 10 },	/* kilobytes */
		{ 'm',  1ULL << 20 },	/* megabytes */
		{ 'g',  1ULL << 30 },	/* gigabytes */
		{ 't',  1ULL << 40 },	/* terabytes */
		{ 'p',  1ULL << 50 },	/* petabytes */
		{ 'e',  1ULL << 60 },	/* exabytes */
		{ 0,    0 },
	};
	size_t llc_size = 0, cache_line_size = 0;

	if (strncasecmp(str, "L", 1) != 0)
		return stress_get_uint64_scale(str, scales, "length");

	/* Try cache sizes */
	if (strcasecmp(str, "LLC")  == 0) {
		stress_cpu_cache_get_llc_size(&llc_size, &cache_line_size);
	} else {
		const int cache_level = atoi(str + 1);

		if ((cache_level < 0) || (cache_level > 5)) {
			(void)fprintf(stderr, "Illegal cache size '%s'\n", str);
			longjmp(g_error_env, 1);
		}
		stress_cpu_cache_get_level_size((uint16_t)cache_level, &llc_size, &cache_line_size);
	}

	if (llc_size == 0) {
		(void)fprintf(stderr, "Cannot determine %s cache size\n", str);
		longjmp(g_error_env, 1);
	}
	return (uint64_t)llc_size;
}

/*
 *  stress_get_uint64_percent()
 *	get a value by whole number or by percentage
 */
uint64_t stress_get_uint64_percent(
	const char *const str,
	const uint32_t instances,
	const uint64_t max,
	const char *const errmsg)
{
	const size_t len = strlen(str);

	/* Convert to % over N instances */
	if ((len > 1) && (str[len - 1] == '%')) {
		double val, percent;

		/* Avoid division by zero */
		if (max == 0) {
			(void)fprintf(stderr, "%s\n", errmsg);
			longjmp(g_error_env, 1);
		}

		/* Should NEVER happen */
		if (instances < 1) {
			(void)fprintf(stderr, "Invalid number of instances\n");
			longjmp(g_error_env, 1);
		}
		if (sscanf(str, "%lf", &val) != 1) {
			(void)fprintf(stderr, "Invalid percentage %s\n", str);
			longjmp(g_error_env, 1);
		}
		if (val < 0.0) {
			(void)fprintf(stderr, "Invalid percentage %s\n", str);
			longjmp(g_error_env, 1);
		}
		percent = ((double)max * (double)val) / (100.0 * (double)instances);
		return (uint64_t)percent;
	}
	return stress_get_uint64_byte(str);
}

/*
 *  stress_get_uint64_byte_memory()
 *	get memory size from string. If it contains %
 *	at the end, then convert it into the available
 *	physical memory scaled by that percentage divided
 *	by the number of stressor instances
 */
uint64_t stress_get_uint64_byte_memory(
	const char *const str,
	const uint32_t instances)
{
	const uint64_t phys_mem = stress_get_phys_mem_size();

	return stress_get_uint64_percent(str, instances, phys_mem,
		"Cannot determine physical memory size");
}

/*
 *  stress_get_uint64_byte_filesystem()
 *	get file size from string. If it contains %
 *	at the end, then convert it into the available
 *	file system space scaled by that percentage divided
 *	by the number of stressor instances
 */
uint64_t stress_get_uint64_byte_filesystem(
	const char *const str,
	const uint32_t instances)
{
	const uint64_t bytes = stress_get_filesystem_size();

	return stress_get_uint64_percent(str, instances, bytes,
		"Cannot determine available space on file system");
}

/*
 *  stress_get_uint64_time()
 *	time in seconds, minutes, hours, days or years
 */
uint64_t stress_get_uint64_time(const char *const str)
{
	static const stress_scale_t scales[] = {
		{ 's', 	1ULL },			/* seconds */
		{ 'm',  60ULL },		/* minutes */
		{ 'h',  3600ULL },		/* hours */
		{ 'd',  24ULL * 3600 },		/* days */
		{ 'w',  24ULL * 3600 * 7 },	/* weeks */
		{ 'y',  31536000 },		/* years */
	};

	return stress_get_uint64_scale(str, scales, "time");
}

/*
 *  stress_check_power_of_2()
 *  number must be power of 2
 */
void stress_check_power_of_2(
	const char *const opt,
	const uint64_t val,
	const uint64_t lo,
	const uint64_t hi)
{
	stress_check_range(opt, val, lo, hi);

	if ((val & (val - 1)) != 0) {
		(void)fprintf(stderr, "Value %" PRId64 " is not power of 2 for %s\n",
					  val, opt);
		longjmp(g_error_env, 1);
	}
}
