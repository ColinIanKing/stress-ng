/*
 * Copyright (C) 2026      Colin Ian King
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
#include "core-builtin.h"
#include <math.h>

static const stress_help_t help[] = {
	{ NULL,	"strnum N",         "start N workers string/numeric conversions" },
	{ NULL,	"strnum-method M",   "select the string/numeric conversion method to operate with" },
	{ NULL,	"strnum-ops N",      "stop after N string/numeric bogo operations" },
	{ NULL,	NULL,                NULL }
};

#define LOOPS_PER_BOGO_OP	(1000)

struct stress_strnum_method;

typedef bool (*stress_strnum_func_t)(stress_args_t *args, const struct stress_strnum_method *this);

typedef struct stress_strnum_method {
	const char *name;
	const char *description;
	const stress_strnum_func_t func;
} stress_strnum_method_t;

static bool stress_strnum_all(stress_args_t *args, const stress_strnum_method_t *this);

static char stress_strnum_i_str[32] ALIGNED(16);
static char stress_strnum_li_str[32] ALIGNED(16);
static char stress_strnum_lli_str[32] ALIGNED(16);

static char stress_strnum_u_str[32] ALIGNED(16);
static char stress_strnum_lu_str[32] ALIGNED(16);
static char stress_strnum_llu_str[32] ALIGNED(16);

static char stress_strnum_float_str[32] ALIGNED(16);
static char stress_strnum_double_str[32] ALIGNED(16);
static char stress_strnum_long_double_str[32] ALIGNED(16);

static int stress_strnum_i;
static long int stress_strnum_li;
static long long int stress_strnum_lli;

static unsigned int stress_strnum_u;
static unsigned long int stress_strnum_lu;
static unsigned long long int stress_strnum_llu;

static float stress_strnum_float;
static double stress_strnum_double;
static long double stress_strnum_long_double;

static void stress_strnum_set_values(void)
{
	stress_strnum_i = (int)stress_mwc32() - (INT_MAX / 2);
	stress_strnum_li = (long int)stress_mwc64() - (LONG_MAX / 2);
	stress_strnum_lli = (long long int)stress_mwc64() - (LONG_LONG_MAX / 2);

	stress_strnum_u = (unsigned int)stress_mwc32();
	stress_strnum_lu = (unsigned long int)stress_mwc64();
	stress_strnum_llu = (unsigned long long int)stress_mwc64();

	(void)snprintf(stress_strnum_i_str, sizeof(stress_strnum_i_str), "%d", stress_strnum_i);
	(void)snprintf(stress_strnum_li_str, sizeof(stress_strnum_li_str), "%ld", stress_strnum_li);
	(void)snprintf(stress_strnum_lli_str, sizeof(stress_strnum_lli_str), "%lld", stress_strnum_lli);

	(void)snprintf(stress_strnum_u_str, sizeof(stress_strnum_u_str), "%u", stress_strnum_u);
	(void)snprintf(stress_strnum_lu_str, sizeof(stress_strnum_lu_str), "%lu", stress_strnum_lu);
	(void)snprintf(stress_strnum_llu_str, sizeof(stress_strnum_llu_str), "%llu", stress_strnum_llu);

	stress_strnum_float = (float)stress_strnum_i / (float)INT_MAX;
	stress_strnum_double = (double)stress_strnum_li / (double)LONG_MAX;
	stress_strnum_long_double = (long double)stress_strnum_lli /(long double)LONG_LONG_MAX;

	(void)snprintf(stress_strnum_float_str, sizeof(stress_strnum_float_str), "%.7f", stress_strnum_float);
	(void)snprintf(stress_strnum_double_str, sizeof(stress_strnum_double_str), "%.7g", stress_strnum_double);
	(void)snprintf(stress_strnum_long_double_str, sizeof(stress_strnum_long_double_str), "%.7Lf", stress_strnum_long_double);
}

static bool stress_strnum_atoi(stress_args_t *args, const stress_strnum_method_t *this)
{
	int i;

	i = atoi(stress_strnum_i_str);
	if (UNLIKELY(i != stress_strnum_i)) {
		pr_fail("%s: %s(%s) failed, got %d, expecting %d\n",
			args->name, this->name, stress_strnum_i_str, i, stress_strnum_i);
		return false;
	}
	return true;
}

static bool stress_strnum_atol(stress_args_t *args, const stress_strnum_method_t *this)
{
	long int li;

	li = atol(stress_strnum_li_str);
	if (UNLIKELY(li != stress_strnum_li)) {
		pr_fail("%s: %s(%s) failed, got %ld, expecting %ld\n",
			args->name, this->name, stress_strnum_li_str, li, stress_strnum_li);
		return false;
	}
	return true;
}

static bool stress_strnum_atoll(stress_args_t *args, const stress_strnum_method_t *this)
{
	long long int lli;

	lli = atoll(stress_strnum_lli_str);
	if (UNLIKELY(lli != stress_strnum_lli)) {
		pr_fail("%s: %s(%s) failed, got %lld, expecting %lld\n",
			args->name, this->name, stress_strnum_lli_str, lli, stress_strnum_lli);
		return false;
	}
	return true;
}

#if defined(HAVE_STRTOUL)
static bool stress_strnum_strtoul(stress_args_t *args, const stress_strnum_method_t *this)
{
	unsigned long int lu;

	errno = 0;
	lu = strtoul(stress_strnum_lu_str, NULL, 10);
	if (UNLIKELY(errno == ERANGE)) {
		pr_fail("%s: %s(%s) failed, got error ERANGE\n",
			args->name, this->name, stress_strnum_lu_str);
		return false;
	}
	if (UNLIKELY(lu != stress_strnum_lu)) {
		pr_fail("%s: %s(%s) failed, got %lu, expecting %lu\n",
			args->name, this->name, stress_strnum_lu_str, lu, stress_strnum_lu);
		return false;
	}
	return true;
}
#endif

#if defined(HAVE_STRTOULL)
static bool stress_strnum_strtoull(stress_args_t *args, const stress_strnum_method_t *this)
{
	unsigned long long int llu;

	errno = 0;
	llu = strtoull(stress_strnum_llu_str, NULL, 10);
	if (UNLIKELY(errno == ERANGE)) {
		pr_fail("%s: %s(%s) failed, got error ERANGE\n",
			args->name, this->name, stress_strnum_llu_str);
		return false;
	}
	if (UNLIKELY(llu != stress_strnum_llu)) {
		pr_fail("%s: %s(%s) failed, got %llu, expecting %llu\n",
			args->name, this->name, stress_strnum_llu_str, llu, stress_strnum_llu);
		return false;
	}
	return true;
}
#endif

static bool stress_strnum_sscanf_i(stress_args_t *args, const stress_strnum_method_t *this)
{
	int i;
	int ret;

	(void)this;

	ret = sscanf(stress_strnum_i_str, "%d", &i);
	if (UNLIKELY(ret != 1)) {
		pr_fail("%s: sscanf(%s, \"%%d\", &i) failed, scanning didn't parse an integer\n",
			args->name, stress_strnum_i_str);
		return false;
	}
	if (UNLIKELY(i != stress_strnum_i)) {
		pr_fail("%s: sscanf(%s, \"%%d\", &i) failed, got %d, expecting %d\n",
			args->name, stress_strnum_i_str, i, stress_strnum_i);
		return false;
	}
	return true;
}

static bool stress_strnum_sscanf_li(stress_args_t *args, const stress_strnum_method_t *this)
{
	long int li;
	int ret;

	(void)this;

	ret = sscanf(stress_strnum_li_str, "%ld", &li);
	if (UNLIKELY(ret != 1)) {
		pr_fail("%s: sscanf(%s, \"%%ld\", &li) failed, scanning didn't parse an integer\n",
			args->name, stress_strnum_li_str);
		return false;
	}
	if (UNLIKELY(li != stress_strnum_li)) {
		pr_fail("%s: sscanf(%s, \"%%ld\", &li) failed, got %ld, expecting %ld\n",
			args->name, stress_strnum_li_str, li, stress_strnum_li);
		return false;
	}
	return true;
}

static bool stress_strnum_sscanf_lli(stress_args_t *args, const stress_strnum_method_t *this)
{
	long long int lli;
	int ret;

	(void)this;

	ret = sscanf(stress_strnum_lli_str, "%lld", &lli);
	if (UNLIKELY(ret != 1)) {
		pr_fail("%s: sscanf(%s, \"%%lld\", &lli) failed, scanning didn't parse an integer\n",
			args->name, stress_strnum_lli_str);
		return false;
	}
	if (UNLIKELY(lli != stress_strnum_lli)) {
		pr_fail("%s: sscanf(%s, \"%%lld\", &lli) failed, got %lld, expecting %lld\n",
			args->name, stress_strnum_lli_str, lli, stress_strnum_lli);
		return false;
	}
	return true;
}

static bool stress_strnum_sscanf_u(stress_args_t *args, const stress_strnum_method_t *this)
{
	unsigned int u;
	int ret;

	(void)this;

	ret = sscanf(stress_strnum_u_str, "%u", &u);
	if (UNLIKELY(ret != 1)) {
		pr_fail("%s: sscanf(%s, \"%%u\", &u) failed, scanning didn't parse an integer\n",
			args->name, stress_strnum_u_str);
		return false;
	}
	if (UNLIKELY(u != stress_strnum_u)) {
		pr_fail("%s: sscanf(%s, \"%%u\", &u) failed, got %u, expecting %u\n",
			args->name, stress_strnum_u_str, u, stress_strnum_u);
		return false;
	}
	return true;
}

static bool stress_strnum_sscanf_lu(stress_args_t *args, const stress_strnum_method_t *this)
{
	unsigned long int lu;
	int ret;

	(void)this;

	ret = sscanf(stress_strnum_lu_str, "%lu", &lu);
	if (UNLIKELY(ret != 1)) {
		pr_fail("%s: sscanf(%s, \"%%lu\", &lu) failed, scanning didn't parse an integer\n",
			args->name, stress_strnum_lu_str);
		return false;
	}
	if (UNLIKELY(lu != stress_strnum_lu)) {
		pr_fail("%s: sscanf(%s, \"%%lu\", &lu) failed, got %lu, expecting %lu\n",
			args->name, stress_strnum_lu_str, lu, stress_strnum_lu);
		return false;
	}
	return true;
}

static bool stress_strnum_sscanf_llu(stress_args_t *args, const stress_strnum_method_t *this)
{
	unsigned long long int llu;
	int ret;

	(void)this;

	ret = sscanf(stress_strnum_llu_str, "%llu", &llu);
	if (UNLIKELY(ret != 1)) {
		pr_fail("%s: sscanf(%s, \"%%llu\", &llu) failed, scanning didn't parse an integer\n",
			args->name, stress_strnum_llu_str);
		return false;
	}
	if (UNLIKELY(llu != stress_strnum_llu)) {
		pr_fail("%s: sscanf(%s, \"%%llu\", &llu) failed, got %llu, expecting %llu\n",
			args->name, stress_strnum_llu_str, llu, stress_strnum_llu);
		return false;
	}
	return true;
}

#if defined(HAVE_STRTOF)
static bool stress_strnum_strtof(stress_args_t *args, const stress_strnum_method_t *this)
{
	float val;
	const float precision = 1.0E-5;

	val = strtof(stress_strnum_float_str, NULL);
	if (UNLIKELY(shim_fabsf(val - stress_strnum_float) > precision)) {
		pr_fail("%s: %s(%s) failed, got %f, expecting %f\n",
			args->name, this->name, stress_strnum_float_str, val, stress_strnum_float);
		return false;
	}
	return true;
}
#endif

#if defined(HAVE_STRTOD)
static bool stress_strnum_strtod(stress_args_t *args, const stress_strnum_method_t *this)
{
	double val;
	const double precision = 1.0E-5;

	val = strtof(stress_strnum_double_str, NULL);
	if (UNLIKELY(shim_fabs(val - stress_strnum_double) > precision)) {
		pr_fail("%s: %s(%s) failed, got %g, expecting %g\n",
			args->name, this->name, stress_strnum_double_str, val, stress_strnum_double);
		return false;
	}
	return true;
}
#endif

#if defined(HAVE_STRTOLD)
static bool stress_strnum_strtold(stress_args_t *args, const stress_strnum_method_t *this)
{
	long double val;
	const long double precision = 1.0E-5;

	val = strtof(stress_strnum_long_double_str, NULL);
	if (UNLIKELY(shim_fabsl(val - stress_strnum_long_double) > precision)) {
		pr_fail("%s: %s(%s) failed, got %Lf, expecting %Lf\n",
			args->name, this->name, stress_strnum_long_double_str, val, stress_strnum_long_double);
		return false;
	}
	return true;
}
#endif

static bool stress_strnum_snprintf_i(stress_args_t *args, const stress_strnum_method_t *this)
{
	char str[32];
	int ret;

	(void)this;

	ret = snprintf(str, sizeof(str), "%d", stress_strnum_i);
	if (UNLIKELY(ret < 0)) {
		pr_fail("%s: snprintf(str, sizeof(str), \"%%d\", %d) failed, didn't format an integer\n",
			args->name, stress_strnum_i);
		return false;
	}
	if (UNLIKELY(strcmp(str, stress_strnum_i_str))) {
		pr_fail("%s: snprintf(str, sizeof(str), \"%%d\", %d) failed, got '%s', expected '%s'\n",
			args->name, stress_strnum_i, str, stress_strnum_i_str);
		return false;
	}
	return true;
}

static bool stress_strnum_snprintf_li(stress_args_t *args, const stress_strnum_method_t *this)
{
	char str[32];
	int ret;

	(void)this;

	ret = snprintf(str, sizeof(str), "%ld", stress_strnum_li);
	if (UNLIKELY(ret < 0)) {
		pr_fail("%s: snprintf(str, sizeof(str), \"%%ld\", %ld) failed, didn't format an integer\n",
			args->name, stress_strnum_li);
		return false;
	}
	if (UNLIKELY(strcmp(str, stress_strnum_li_str))) {
		pr_fail("%s: snprintf(str, sizeof(str), \"%%ld\", %ld) failed, got '%s', expected '%s'\n",
			args->name, stress_strnum_li, str, stress_strnum_li_str);
		return false;
	}
	return true;
}

static bool stress_strnum_snprintf_lli(stress_args_t *args, const stress_strnum_method_t *this)
{
	char str[32];
	int ret;

	(void)this;

	ret = snprintf(str, sizeof(str), "%lld", stress_strnum_lli);
	if (UNLIKELY(ret < 0)) {
		pr_fail("%s: snprintf(str, sizeof(str), \"%%lld\", %lld) failed, didn't format an integer\n",
			args->name, stress_strnum_lli);
		return false;
	}
	if (UNLIKELY(strcmp(str, stress_strnum_lli_str))) {
		pr_fail("%s: snprintf(str, sizeof(str), \"%%lld\", %lld) failed, got '%s', expected '%s'\n",
			args->name, stress_strnum_lli, str, stress_strnum_lli_str);
		return false;
	}
	return true;
}

static bool stress_strnum_sscanf_f(stress_args_t *args, const stress_strnum_method_t *this)
{
	float val;
	const float precision = 1.0E-6;
	int ret;

	(void)this;

	ret = sscanf(stress_strnum_float_str, "%f", &val);
	if (ret != 1) {
		pr_fail("%s: sscanf(%s, \"%%f\", &val) failed, scanning didn't parse an integer\n",
			args->name, stress_strnum_float_str);
		return false;
	}
	if (UNLIKELY(shim_fabsf(val - stress_strnum_float) > precision)) {
		pr_fail("%s: sscanf(%s, \"%%f\", &val) failed, got %f, expecting %f\n",
			args->name, stress_strnum_float_str, val, stress_strnum_float);
		return false;
	}
	return true;
}

static bool stress_strnum_sscanf_d(stress_args_t *args, const stress_strnum_method_t *this)
{
	double val;
	const double precision = 1.0E-6;
	int ret;

	(void)this;

	ret = sscanf(stress_strnum_double_str, "%lf", &val);
	if (UNLIKELY(ret != 1)) {
		pr_fail("%s: sscanf(%s, \"%%lf\", &val) failed, scanning didn't parse an integer\n",
			args->name, stress_strnum_double_str);
		return false;
	}
	if (UNLIKELY(shim_fabs(val - stress_strnum_double) > precision)) {
		pr_fail("%s: sscanf(%s, \"%%lf\", &val) failed, got %g, expecting %g\n",
			args->name, stress_strnum_double_str, val, stress_strnum_double);
		return false;
	}
	return true;
}

static bool stress_strnum_sscanf_ld(stress_args_t *args, const stress_strnum_method_t *this)
{
	long double val;
	const long double precision = 1.0E-6;
	int ret;

	(void)this;

	ret = sscanf(stress_strnum_long_double_str, "%Lf", &val);
	if (UNLIKELY(ret != 1)) {
		pr_fail("%s: sscanf(%s, \"%%Lf\", &val) failed, scanning didn't parse an integer\n",
			args->name, stress_strnum_long_double_str);
		return false;
	}
	if (UNLIKELY(shim_fabsl(val - stress_strnum_long_double) > precision)) {
		pr_fail("%s: sscanf(%s, \"%%Lf\", &val) failed, got %Lf, expecting %Lf\n",
			args->name, stress_strnum_long_double_str, val, stress_strnum_long_double);
		return false;
	}
	return true;
}

#if defined(HAVE_STRFROMF)
static bool stress_strnum_strfromf(stress_args_t *args, const stress_strnum_method_t *this)
{
	char str[32];

	(void)this;

	(void)strfromf(str, sizeof(str), "%.7f", stress_strnum_float);
	if (strcmp(str, stress_strnum_float_str)) {
		pr_fail("%s: strfromf(str, sizeof(str), \"%%.7f\", %.7f) failed, got %s, expecting %s\n",
			args->name, stress_strnum_float, str, stress_strnum_float_str);
		return false;
	}
	return true;
}
#endif

#if defined(HAVE_STRFROMD)
static bool stress_strnum_strfromd(stress_args_t *args, const stress_strnum_method_t *this)
{
	char str[32];

	(void)this;

	(void)strfromd(str, sizeof(str), "%.7g", stress_strnum_double);
	if (strcmp(str, stress_strnum_double_str)) {
		pr_fail("%s: strfromd(str, sizeof(str), \"%%.7g\", %.7g) failed, got %s, expecting %s\n",
			args->name, stress_strnum_double, str, stress_strnum_double_str);
		return false;
	}
	return true;
}
#endif

#if defined(HAVE_STRFROML)
static bool stress_strnum_strfroml(stress_args_t *args, const stress_strnum_method_t *this)
{
	char str[32];

	(void)this;

	(void)strfroml(str, sizeof(str), "%.7f", stress_strnum_long_double);
	if (strcmp(str, stress_strnum_long_double_str)) {
		pr_fail("%s: strfroml(str, sizeof(str), \"%%.7f\", %.7Lf) failed, got %s, expecting %s\n",
			args->name, stress_strnum_long_double, str, stress_strnum_long_double_str);
		return false;
	}
	return true;
}
#endif

static const stress_strnum_method_t stress_strnum_methods[] = {
	{ "all",		"all strnum methods",                        stress_strnum_all },
	{ "atoi",		"string to int (atoi)",                      stress_strnum_atoi },
	{ "atol",		"string to long int (atol)",                 stress_strnum_atol },
	{ "atoll",		"string to long long int (atoll)",           stress_strnum_atoll },
#if defined(HAVE_STRTOUL)
	{ "strtoul",		"string to unsigned long (strtoul)",         stress_strnum_strtoul },
#endif
#if defined(HAVE_STRTOULL)
	{ "strtoull",		"string to unsigned long long (strtoull)",   stress_strnum_strtoull },
#endif
	{ "sscanf-i",		"string to int (sscanf)",                    stress_strnum_sscanf_i },
	{ "sscanf-li",		"string to long int (sscanf)",               stress_strnum_sscanf_li },
	{ "sscanf-lli",		"string to long long int (sscanf)",          stress_strnum_sscanf_lli },
	{ "sscanf-u",		"string to unsigned int (sscanf)",           stress_strnum_sscanf_u },
	{ "sscanf-lu",		"string to unsigned long int (sscanf)",      stress_strnum_sscanf_lu },
	{ "sscanf-llu",		"string to unsigned long long int (sscanf)", stress_strnum_sscanf_llu },

	{ "snprintf-i",		"string from int (snprintf)",                stress_strnum_snprintf_i },
	{ "snprintf-li",	"string from long int (snprintf)",           stress_strnum_snprintf_li },
	{ "snprintf-lli",	"string from long long int (snprintf)",      stress_strnum_snprintf_lli },

#if defined(HAVE_STRTOF)
	{ "strtof",		"string to float (strtof)", 		     stress_strnum_strtof },
#endif
#if defined(HAVE_STRTOD)
	{ "strtod",		"string to double (strtod)",		     stress_strnum_strtod },
#endif
#if defined(HAVE_STRTOLD)
	{ "strtold",		"string to long double (strtold)",           stress_strnum_strtold },
#endif
	{ "sscanf-f",		"string to float (sscanf)",                  stress_strnum_sscanf_f },
	{ "sscanf-d",		"string to double (sscanf)",                 stress_strnum_sscanf_d },
	{ "sscanf-ld",		"string to long double int (sscanf)",        stress_strnum_sscanf_ld },
#if defined(HAVE_STRFROMF)
	{ "strfromf",		"string from float (strfromf)",              stress_strnum_strfromf },
#endif
#if defined(HAVE_STRFROMD)
	{ "strfromd",		"string from double (strfromd)",             stress_strnum_strfromd },
#endif
#if defined(HAVE_STRFROML)
	{ "strfroml",		"string from long double (strfroml)",        stress_strnum_strfroml },
#endif
};

static stress_metrics_t stress_strnum_metrics[SIZEOF_ARRAY(stress_strnum_methods)];

#define STRESS_NUM_STRNUM_METHODS	(SIZEOF_ARRAY(stress_strnum_methods))

static int stress_strnum_call_method(
	stress_args_t *args,
	const size_t method)
{
	double t;
	stress_metrics_t *metrics = &stress_strnum_metrics[method];
	const stress_strnum_method_t *strnum_method = &stress_strnum_methods[method];
	bool passed;
	int i;

	t = stress_time_now();
	for (i = 0; i < LOOPS_PER_BOGO_OP; i++) {
		passed = strnum_method->func(args, strnum_method);
		if (!passed)
			return false;
	}
	metrics->duration += stress_time_now() - t;
	metrics->count += (double)LOOPS_PER_BOGO_OP;
	stress_bogo_inc(args);
	return true;
}

static bool stress_strnum_all(stress_args_t *args, const stress_strnum_method_t *this)
{
	size_t i;

	(void)this;

	for (i = 1; i < STRESS_NUM_STRNUM_METHODS; i++) {
		if (!stress_strnum_call_method(args, i))
			return false;
	}
	return true;
}

static int stress_strnum(stress_args_t *args)
{
	size_t i;
	size_t strnum_method = 0;	/* "all" */
	int rc = EXIT_SUCCESS;

	(void)stress_get_setting("strnum-method", &strnum_method);

	stress_set_proc_state(args->name, STRESS_STATE_SYNC_WAIT);
	stress_sync_start_wait(args);
	stress_set_proc_state(args->name, STRESS_STATE_RUN);

	for (i = 0; i < SIZEOF_ARRAY(stress_strnum_metrics); i++) {
		stress_strnum_metrics[i].duration = 0.0;
		stress_strnum_metrics[i].count = 0.0;
	}


	i = 0;
	stress_strnum_set_values();

	do {
		if (!stress_strnum_call_method(args, strnum_method))
			rc = EXIT_FAILURE;
		i++;
		if (i > 1000) {
			i = 0;
			stress_strnum_set_values();
		}
	} while (stress_continue(args));

	for (i = 1; i < STRESS_NUM_STRNUM_METHODS; i++) {
		const double count = stress_strnum_metrics[i].count;
		const double duration = stress_strnum_metrics[i].duration;

		if ((duration > 0.0) && (count > 0.0)) {
			char msg[64];
			const double rate = count / duration;

			(void)snprintf(msg, sizeof(msg), "calls per sec, %-20s", stress_strnum_methods[i].description);
			stress_metrics_set(args, i - 1, msg,
				rate, STRESS_METRIC_HARMONIC_MEAN);
		}
	}

	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);

	return rc;
}

static const char *stress_strnum_method(const size_t i)
{
	return (i < STRESS_NUM_STRNUM_METHODS) ? stress_strnum_methods[i].name : NULL;
}

static const stress_opt_t opts[] = {
	{ OPT_strnum_method, "strnum-method", TYPE_ID_SIZE_T_METHOD, 0, 1, (void *)stress_strnum_method },
	END_OPT,
};

const stressor_info_t stress_strnum_info = {
	.stressor = stress_strnum,
	.classifier = CLASS_CPU | CLASS_COMPUTE,
	.opts = opts,
	.verify = VERIFY_ALWAYS,
	.help = help
};
