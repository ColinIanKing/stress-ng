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
#include "core-builtin.h"
#include "core-parse-opts.h"
#include "core-cpu-cache.h"
#include "core-net.h"

#include <ctype.h>

typedef const char * (*stress_method_func)(const size_t idx);
typedef void (*stress_callback_func)(const char *opt_name, const char *opt_arg, stress_type_id_t *type_id, void *value);

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
		stress_no_return();
	}
}

/*
 *  stress_check_signed_range()
 *	Sanity check val against a lo - hi range
 */
void stress_check_signed_range(
	const char *const opt,
	const int64_t val,
	const int64_t lo,
	const int64_t hi)
{
	if ((val < lo) || (val > hi)) {
		(void)fprintf(stderr, "Value %" PRId64 " is out of range for %s,"
			" allowed: %" PRId64 " .. %" PRId64 "\n",
			val, opt, lo, hi);
		longjmp(g_error_env, 1);
		stress_no_return();
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
		(void)fprintf(stderr, "Value %" PRIu64 " is out of range for %s,"
			" allowed: %" PRIu64 " .. %" PRIu64 "\n",
			val, opt, lo, hi);
		longjmp(g_error_env, 1);
		stress_no_return();
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
			stress_uint64_to_str(strval, sizeof(strval), val, 1, false),
			opt,
			stress_uint64_to_str(strlo, sizeof(strlo), lo, 1, false),
			stress_uint64_to_str(strhi, sizeof(strhi), hi, 1, false));
		longjmp(g_error_env, 1);
		stress_no_return();
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
		if (!isdigit((unsigned char)*ptr))
			break;
		ptr++;
	}
	if (*ptr == '\0')
		return;
	(void)fprintf(stderr, "Value %s contains non-numeric: '%s'\n",
		str, ptr);
	longjmp(g_error_env, 1);
	stress_no_return();
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

		if (isdigit((unsigned char)*ptr)) {
			if (!negative)
				return;

			(void)fprintf(stderr, "Invalid negative number %s\n", str);
			longjmp(g_error_env, 1);
			stress_no_return();
		}
	}
}

/*
 *  stress_get_uint8()
 *	string to uint8_t
 */
uint8_t stress_get_uint8(const char *const str)
{
	uint64_t val;

	stress_ensure_positive(str);
	stress_ensure_numeric(str);
	if (sscanf(str, "%" SCNu64, &val) != 1) {
		(void)fprintf(stderr, "Invalid number %s\n", str);
		longjmp(g_error_env, 1);
		stress_no_return();
	}
	if (val > UINT8_MAX) {
		(void)fprintf(stderr, "Invalid number %s too large (> %d)\n", str, (int)UINT8_MAX);
		longjmp(g_error_env, 1);
		stress_no_return();
	}
	return (uint8_t)val;
}

/*
 *  stress_get_int8()
 *	string to int8_t
 */
int8_t stress_get_int8(const char *const str)
{
	int64_t val;

	stress_ensure_numeric(str);
	if (sscanf(str, "%" SCNd64, &val) != 1) {
		(void)fprintf(stderr, "Invalid number %s\n", str);
		longjmp(g_error_env, 1);
		stress_no_return();
	}
	if (val > INT8_MAX) {
		(void)fprintf(stderr, "Invalid number %s too large (> %ld)\n", str, (long)INT8_MAX);
		longjmp(g_error_env, 1);
		stress_no_return();
	}
	if (val < INT8_MIN) {
		(void)fprintf(stderr, "Invalid number %s too small (< %ld)\n", str, (long)INT8_MIN);
		longjmp(g_error_env, 1);
		stress_no_return();
	}
	return (int8_t)val;
}

/*
 *  stress_get_uint16()
 *	string to uint16_t
 */
uint16_t stress_get_uint16(const char *const str)
{
	uint64_t val;

	stress_ensure_positive(str);
	stress_ensure_numeric(str);
	if (sscanf(str, "%" SCNu64, &val) != 1) {
		(void)fprintf(stderr, "Invalid number %s\n", str);
		longjmp(g_error_env, 1);
		stress_no_return();
	}
	if (val > UINT16_MAX) {
		(void)fprintf(stderr, "Invalid number %s too large (> %d)\n", str, (int)UINT16_MAX);
		longjmp(g_error_env, 1);
		stress_no_return();
	}
	return (uint16_t)val;
}

/*
 *  stress_get_int16()
 *	string to int16_t
 */
int16_t stress_get_int16(const char *const str)
{
	int64_t val;

	stress_ensure_numeric(str);
	if (sscanf(str, "%" SCNd64, &val) != 1) {
		(void)fprintf(stderr, "Invalid number %s\n", str);
		longjmp(g_error_env, 1);
		stress_no_return();
	}
	if (val > INT16_MAX) {
		(void)fprintf(stderr, "Invalid number %s too large (> %ld)\n", str, (long)INT16_MAX);
		longjmp(g_error_env, 1);
		stress_no_return();
	}
	if (val < INT16_MIN) {
		(void)fprintf(stderr, "Invalid number %s too small (< %ld)\n", str, (long)INT16_MIN);
		longjmp(g_error_env, 1);
		stress_no_return();
	}
	return (int16_t)val;
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
		stress_no_return();
	}
	if (val > UINT32_MAX) {
		(void)fprintf(stderr, "Invalid number %s too large (> %u)\n", str, (unsigned int)UINT32_MAX);
		longjmp(g_error_env, 1);
		stress_no_return();
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
		stress_no_return();
	}
	if (val > INT32_MAX) {
		(void)fprintf(stderr, "Invalid number %s too large (> %ld)\n", str, (long)INT32_MAX);
		longjmp(g_error_env, 1);
		stress_no_return();
	}
	if (val < INT32_MIN) {
		(void)fprintf(stderr, "Invalid number %s too small (< %ld)\n", str, (long)INT32_MIN);
		longjmp(g_error_env, 1);
		stress_no_return();
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
		stress_no_return();
	}
	return val;
}

/*
 *  stress_get_int64()
 *	string to int64_t
 */
int64_t stress_get_int64(const char *const str)
{
	int64_t val;

	stress_ensure_numeric(str);
	if (sscanf(str, "%" SCNd64, &val) != 1) {
		(void)fprintf(stderr, "Invalid number %s\n", str);
		longjmp(g_error_env, 1);
		stress_no_return();
	}
	return val;
}

/*
 *  stress_get_uint()
 *	string to unsigned int
 */
unsigned int stress_get_uint(const char *const str)
{
	uint64_t val;

	stress_ensure_positive(str);
	stress_ensure_numeric(str);
	if (sscanf(str, "%" SCNu64, &val) != 1) {
		(void)fprintf(stderr, "Invalid number %s\n", str);
		longjmp(g_error_env, 1);
		stress_no_return();
	}
	if (val > UINT_MAX) {
		(void)fprintf(stderr, "Invalid number %s too large (> %u)\n", str, (unsigned int)UINT_MAX);
		longjmp(g_error_env, 1);
		stress_no_return();
	}
	return (unsigned int)val;
}

/*
 *  stress_get_int()
 *	string to int
*/
int stress_get_int(const char *const str)
{
	int64_t val;

	stress_ensure_numeric(str);
	if (sscanf(str, "%" SCNd64, &val) != 1) {
		(void)fprintf(stderr, "Invalid number %s\n", str);
		longjmp(g_error_env, 1);
		stress_no_return();
	}
	if (val > INT_MAX) {
		(void)fprintf(stderr, "Invalid number %s too large (> %ld)\n", str, (long)INT_MAX);
		longjmp(g_error_env, 1);
		stress_no_return();
	}
	if (val < INT_MIN) {
		(void)fprintf(stderr, "Invalid number %s too small (< %ld)\n", str, (long)INT_MIN);
		longjmp(g_error_env, 1);
		stress_no_return();
	}
	return (int)val;
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

	if (isdigit((unsigned char)ch))
		return val;

	ch = tolower(ch);
	for (i = 0; scales[i].ch; i++) {
		if (ch == scales[i].ch)
			return val * scales[i].scale;
	}

	(void)fprintf(stderr, "Illegal %s specifier %c\n", msg, str[len]);
err:
	longjmp(g_error_env, 1);
	stress_no_return();
	/* should never get here */
	return 0;
}

static const stress_scale_t size_scales[] = {
	{ 'b', 	1ULL },		/* bytes */
	{ 'k',  1ULL << 10 },	/* kilobytes */
	{ 'm',  1ULL << 20 },	/* megabytes */
	{ 'g',  1ULL << 30 },	/* gigabytes */
	{ 't',  1ULL << 40 },	/* terabytes */
	{ 'p',  1ULL << 50 },	/* petabytes */
	{ 'e',  1ULL << 60 },	/* exabytes */
	{ 0,    0 },
};

uint64_t stress_get_uint64_byte_scale(const char *const str)
{
	int ch, i;
	const int len = strlen(str);

	if (len < 1) {
		(void)fprintf(stderr, "Illegal empty specifier\n");
		goto err;
	} else if (len > 1) {
		goto illegal;
	}
	ch = tolower((int)str[0]);
	for (i = 1; size_scales[i].ch; i++) {
		if (ch == size_scales[i].ch)
			return size_scales[i].scale;
	}

illegal:
	(void)fprintf(stderr, "Illegal specifier '%s', allower specifiers: ", str);
err:

	for (i = 1; size_scales[i].ch; i++) {
		fprintf(stderr, "%s%c", ((i == 0) ? "" : ", "), size_scales[i].ch);
	}
	fprintf(stderr, "\n");
	longjmp(g_error_env, 1);
	stress_no_return();
	/* should never get here */
	return 0;
}

/*
 *  stress_get_uint64_byte()
 *	size in bytes, K bytes, M bytes or G bytes
 */
uint64_t stress_get_uint64_byte(const char *const str)
{
	size_t llc_size = 0, cache_line_size = 0;

	if (strncasecmp(str, "L", 1) != 0)
		return stress_get_uint64_scale(str, size_scales, "length");

	/* Try cache sizes */
	if (strcasecmp(str, "LLC")  == 0) {
		stress_cpu_cache_get_llc_size(&llc_size, &cache_line_size);
	} else {
		int cache_level;

		if (sscanf(str + 1, "%d", &cache_level) != 1)
			cache_level = 0;

		if ((cache_level < 0) || (cache_level > 5)) {
			(void)fprintf(stderr, "Illegal cache size '%s'\n", str);
			longjmp(g_error_env, 1);
			stress_no_return();
		}
		stress_cpu_cache_get_level_size((uint16_t)cache_level, &llc_size, &cache_line_size);
	}

	if (llc_size == 0) {
		(void)fprintf(stderr, "Cannot determine %s cache size\n", str);
		longjmp(g_error_env, 1);
		stress_no_return();
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
	bool *percentage,
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
			stress_no_return();
		}

		/* Should NEVER happen */
		if (instances < 1) {
			(void)fprintf(stderr, "Invalid number of instances\n");
			longjmp(g_error_env, 1);
			stress_no_return();
		}
		if (sscanf(str, "%lf", &val) != 1) {
			(void)fprintf(stderr, "Invalid percentage %s\n", str);
			longjmp(g_error_env, 1);
			stress_no_return();
		}
		if (val < 0.0) {
			(void)fprintf(stderr, "Invalid negative percentage %s\n", str);
			longjmp(g_error_env, 1);
			stress_no_return();
		}
		percent = ((double)max * (double)val) / (100.0 * (double)instances);
		if (percentage)
			*percentage = true;
		if ((uint64_t)percent > UINT64_MAX) {
			(void)fprintf(stderr, "Invalid too large percentage %s\n", str);
			longjmp(g_error_env, 1);
			stress_no_return();
		}
		return (uint64_t)percent;
	}
	if (percentage)
		*percentage = false;
	return stress_get_uint64_byte(str);
}

/*
 *  stress_get_int32_instance_percent()
 *	get instance by number or by percentage
 */
int32_t stress_get_int32_instance_percent(const char *const str)
{
	const size_t len = strlen(str);

	/* Convert to % over N instances */
	if ((len > 1) && (str[len - 1] == '%')) {
		double val;

		if (sscanf(str, "%lf", &val) != 1) {
			(void)fprintf(stderr, "Invalid percentage %s\n", str);
			longjmp(g_error_env, 1);
			stress_no_return();
		}
		if (val < 0.0) {
			return -1;
		} else if (val > 0.0) {
			const int32_t cpus = stress_get_processors_configured();

			val = (double)cpus * val / 100.0;
			if (val < 1.0)
				return 1;
			if (val > INT32_MAX) {
				(void)fprintf(stderr, "Invalid too large percentage %s\n", str);
				longjmp(g_error_env, 1);
				stress_no_return();
			}
			return (int32_t)val;
		} else {
			return 0;
		}
	}
	return stress_get_int32(str);
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
	bool percentage;

	return stress_get_uint64_percent(str, instances, phys_mem, &percentage,
		"Cannot determine physical memory size");
}

/*
 *  stress_get_uint64_byte_filesystem()
 *	get file size from string. If it contains %
 *	at the end, then convert it into the available
 *	file system space scaled by that percentage divided
 *	by the number of stressor instances
 */
static uint64_t stress_get_uint64_byte_filesystem(
	const char *const str,
	const uint32_t instances,
	bool *percentage)
{
	return stress_get_uint64_percent(str, instances, 100, percentage,
		"Cannot determine available space on file system");
}

/*
 *  stress_get_uint64_time()
 *	time in seconds, minutes, hours, days or years
 */
uint64_t stress_get_uint64_time(const char *const str)
{
	static const stress_scale_t time_scales[] = {
		{ 's', 	1ULL },			/* seconds */
		{ 'm',  60ULL },		/* minutes */
		{ 'h',  3600ULL },		/* hours */
		{ 'd',  24ULL * 3600 },		/* days */
		{ 'w',  24ULL * 3600 * 7 },	/* weeks */
		{ 'y',  31536000 },		/* years */
	};

	return stress_get_uint64_scale(str, time_scales, "time");
}

int stress_parse_opt(const char *stressor_name, const char *opt_arg, const stress_opt_t *opt)
{
	const char *opt_name = opt->opt_name;
	const uint64_t min = opt->min;
	const uint64_t max = opt->max;
	stress_setting_t setting;
	int domain_mask;
	stress_method_func method_func;
	stress_callback_func callback_func;
	stress_type_id_t type_id;
	const char *str;
	size_t i;
	bool percentage = false;

	(void)shim_memset(&setting, 0, sizeof(setting));

	switch (opt->type_id) {
	case TYPE_ID_UINT8:
		setting.u.uint8 = stress_get_uint8(opt_arg);
		stress_check_range(opt_name, (uint64_t)setting.u.uint8, min, max);
		return stress_set_setting(stressor_name, opt_name, TYPE_ID_UINT8, &setting.u.uint8);
	case TYPE_ID_INT8:
		setting.u.int8 = stress_get_int8(opt_arg);
		stress_check_signed_range(opt_name, (int64_t)setting.u.int8, (int64_t)min, (int64_t)max);
		return stress_set_setting(stressor_name, opt_name, TYPE_ID_INT8, &setting.u.int8);
	case TYPE_ID_UINT16:
		setting.u.uint16 = stress_get_uint16(opt_arg);
		stress_check_range(opt_name, (uint64_t)setting.u.uint16, min, max);
		return stress_set_setting(stressor_name, opt_name, TYPE_ID_UINT16, &setting.u.uint16);
	case TYPE_ID_INT16:
		setting.u.int16 = stress_get_int16(opt_arg);
		stress_check_signed_range(opt_name, (int64_t)setting.u.int16, (int64_t)min, (int64_t)max);
		return stress_set_setting(stressor_name, opt_name, TYPE_ID_INT16, &setting.u.int16);
	case TYPE_ID_UINT32:
		setting.u.uint32 = stress_get_uint32(opt_arg);
		stress_check_range(opt_name, (uint64_t)setting.u.uint32, min, max);
		return stress_set_setting(stressor_name, opt_name, TYPE_ID_UINT32, &setting.u.uint32);
	case TYPE_ID_INT32:
		setting.u.int32 = stress_get_int32(opt_arg);
		stress_check_signed_range(opt_name, (int64_t)setting.u.int32, (int64_t)min, (int64_t)max);
		return stress_set_setting(stressor_name, opt_name, TYPE_ID_INT32, &setting.u.int32);
	case TYPE_ID_UINT64:
		setting.u.uint64 = stress_get_uint64(opt_arg);
		stress_check_range(opt_name, setting.u.uint64, min, max);
		return stress_set_setting(stressor_name, opt_name, TYPE_ID_UINT64, &setting.u.uint64);
	case TYPE_ID_UINT64_BYTES_FS:
		/* uint64 in bytes units */
		setting.u.uint64 = stress_get_uint64_byte_filesystem(opt_arg, 1, &percentage);
		if (percentage)
			return stress_set_setting(stressor_name, opt_name, TYPE_ID_UINT64_BYTES_FS_PERCENT, &setting.u.uint64);
		stress_check_range_bytes(opt_name, (uint64_t)setting.u.uint64, min, max);
		return stress_set_setting(stressor_name, opt_name, TYPE_ID_UINT64_BYTES_FS, &setting.u.uint64);
	case TYPE_ID_UINT64_BYTES_VM:
		/* uint64 in bytes units */
		setting.u.uint64 = stress_get_uint64_byte_memory(opt_arg, 1);
		stress_check_range_bytes(opt_name, (uint64_t)setting.u.uint64, min, max);
		return stress_set_setting(stressor_name, opt_name, TYPE_ID_UINT64_BYTES_VM, &setting.u.uint64);
	case TYPE_ID_INT64:
		setting.u.int64 = stress_get_int64(opt_arg);
		stress_check_signed_range(opt_name, (int64_t)setting.u.int64, (int64_t)min, (int64_t)max);
		return stress_set_setting(stressor_name, opt_name, TYPE_ID_INT64, &setting.u.int64);
	case TYPE_ID_SIZE_T:
		setting.u.size = (size_t)stress_get_uint64(opt_arg);
		stress_check_range(opt_name, (uint64_t)setting.u.size, min, max);
		return stress_set_setting(stressor_name, opt_name, TYPE_ID_SIZE_T, &setting.u.size);
	case TYPE_ID_SIZE_T_BYTES_FS:
		/* size_t in bytes units */
		setting.u.size = (size_t)stress_get_uint64_byte_filesystem(opt_arg, 1, &percentage);
		if (percentage)
			return stress_set_setting(stressor_name, opt_name, TYPE_ID_SIZE_T_BYTES_FS_PERCENT, &setting.u.size);
		stress_check_range(opt_name, (uint64_t)setting.u.size, min, max);
		return stress_set_setting(stressor_name, opt_name, TYPE_ID_SIZE_T_BYTES_FS, &setting.u.size);
	case TYPE_ID_SIZE_T_BYTES_VM:
		/* size_t in bytes units */
		setting.u.size = (size_t)stress_get_uint64_byte_memory(opt_arg, 1);
		stress_check_range(opt_name, (uint64_t)setting.u.size, min, max);
		return stress_set_setting(stressor_name, opt_name, TYPE_ID_SIZE_T_BYTES_VM, &setting.u.size);
	case TYPE_ID_SIZE_T_METHOD:
		method_func = (stress_method_func)opt->data;
		if (!method_func) {
			fprintf(stderr, "%s: no method function provided for option\n", opt_name);
			longjmp(g_error_env, 1);
			stress_no_return();
		}
		for (i = 0; (str = method_func(i)) != NULL ; i++) {
			if (strcmp(str, opt_arg) == 0)
				return stress_set_setting(stressor_name, opt_name, TYPE_ID_SIZE_T_METHOD, &i);
		}
		if (i == 0) {
			(void)fprintf(stderr, "option %s choice '%s' not known, there are none available (stressor unimplemented)\n", opt_name, opt_arg);
		} else {
			(void)fprintf(stderr, "option %s choice '%s' not known, choices are:", opt_name, opt_arg);
			for (i = 0; (str = method_func(i)) != NULL ; i++)
				(void)fprintf(stderr, " %s", str);
			(void)fprintf(stderr, "\n");
		}
		longjmp(g_error_env, 1);
		stress_no_return();
		break;
	case TYPE_ID_SSIZE_T:
		setting.u.ssize = (ssize_t)stress_get_int64(opt_arg);
		stress_check_signed_range(opt_name, (int64_t)setting.u.ssize, (int64_t)min, (int64_t)max);
		return stress_set_setting(stressor_name, opt_name, TYPE_ID_SSIZE_T, &setting.u.ssize);
	case TYPE_ID_UINT:
		setting.u.uint = stress_get_uint(opt_arg);
		stress_check_range(opt_name, (uint64_t)setting.u.uint, min, max);
		return stress_set_setting(stressor_name, opt_name, TYPE_ID_UINT, &setting.u.uint);
	case TYPE_ID_INT:
		setting.u.sint = stress_get_int(opt_arg);
		stress_check_signed_range(opt_name, (int64_t)setting.u.sint, (int64_t)min, (int64_t)max);
		return stress_set_setting(stressor_name, opt_name, TYPE_ID_INT, &setting.u.sint);
	case TYPE_ID_INT_DOMAIN:
		domain_mask = (opt->data == NULL) ? 0 : *(int *)opt->data;
		if (stress_set_net_domain(domain_mask, opt_name, opt_arg, &setting.u.sint) < 0) {
			longjmp(g_error_env, 1);
			stress_no_return();
		}
		return stress_set_setting(stressor_name, opt_name, TYPE_ID_UINT, &setting.u.sint);
	case TYPE_ID_INT_PORT:
		stress_set_net_port(opt_name, opt_arg, (int)min, (int)max, &setting.u.sint);
		return stress_set_setting(stressor_name, opt_name, TYPE_ID_INT, &setting.u.sint);
	case TYPE_ID_OFF_T:
		/* off_t always in bytes units */
		setting.u.off = (off_t)stress_get_uint64_byte_filesystem(opt_arg, 1, &percentage);
		stress_check_range_bytes(opt_name, (uint64_t)setting.u.off, min, max);
		return stress_set_setting(stressor_name, opt_name, TYPE_ID_OFF_T, &setting.u.off);
	case TYPE_ID_STR:
		return stress_set_setting(stressor_name, opt_name, TYPE_ID_STR, opt_arg);
	case TYPE_ID_BOOL:
		if (!opt_arg)
			return stress_set_setting_true(stressor_name, opt_name, opt_arg);
		setting.u.boolean = (bool)stress_get_uint8(opt_arg);
		stress_check_range(opt_name, (uint64_t)setting.u.boolean, min, max);
		return stress_set_setting(stressor_name, opt_name, TYPE_ID_BOOL, &setting.u.boolean);
	case TYPE_ID_CALLBACK:
 		callback_func = (stress_callback_func)opt->data;
		if (!callback_func) {
			fprintf(stderr, "%s: no callback function provided for option\n", opt_name);
			longjmp(g_error_env, 1);
			stress_no_return();
		}
		type_id = TYPE_ID_UNDEFINED;
		callback_func(opt_name, opt_arg, &type_id, &setting.u);
		if (type_id != TYPE_ID_UNDEFINED)
			return stress_set_setting(stressor_name, opt_name, type_id, &setting.u);
		return EXIT_SUCCESS;
	case TYPE_ID_UNDEFINED:
	default:
		pr_inf("%s: unknown type %u for value '%s'\n", opt_name, opt->type_id, opt_arg);
		break;
	}
	return EXIT_SUCCESS;
}

/*
 *  stress_unimplemented_method()
 *	method handler for methods that are unimplemented
 *	(e.g. stressor not supported)
 */
const char * CONST stress_unimplemented_method(const size_t i)
{
	(void)i;

	return NULL;
}
