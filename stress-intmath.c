/*
 * Copyright (C) 2024-2025 Colin Ian King.
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
#include "core-pragma.h"
#include "core-target-clones.h"

#define NO_TARGET_CLONES

static const stress_help_t help[] = {
	{ NULL,	"intmath N",	  "start N workers that exercising signed integer math operations" },
	{ NULL,	"intmath-method", "select the method of integer math operation" },
	{ NULL,	"intmath-ops N",  "stop after N bogo signed integer math operations" },
	{ NULL,	NULL,		  NULL }
};

typedef struct {
#if defined(HAVE_INT128_T)
	__int128_t init[4];
#else
	int64_t init[4];
#endif
	struct {
#if defined(HAVE_INT128_T)
		__int128_t result128[2];
#endif
		int64_t result64[2];
		int32_t result32[2];
		int16_t result16[2];
		int8_t result8[2];
	} add ALIGN64;
	struct {
#if defined(HAVE_INT128_T)
		__int128_t result128[2];
#endif
		int64_t result64[2];
		int32_t result32[2];
		int16_t result16[2];
		int8_t result8[2];
	} sub ALIGN64;
	struct {
#if defined(HAVE_INT128_T)
		__int128_t result128[2];
#endif
		int64_t result64[2];
		int32_t result32[2];
		int16_t result16[2];
		int8_t result8[2];
	} mul ALIGN64;
	struct {
#if defined(HAVE_INT128_T)
		__int128_t result128[2];
#endif
		int64_t result64[2];
		int32_t result32[2];
		int16_t result16[2];
		int8_t result8[2];
	} div ALIGN64;
	struct {
#if defined(HAVE_INT128_T)
		__int128_t result128[2];
#endif
		int64_t result64[2];
		int32_t result32[2];
		int16_t result16[2];
		int8_t result8[2];
	} mod ALIGN64;
} stress_intmath_vals_t;

typedef bool (*intmath_func_t)(stress_intmath_vals_t *val, const int idx, const bool verify, double *duration);

typedef struct {
	const char *name;
	int ops;
	intmath_func_t func;
} stress_intmath_method_t;

#define STRESS_INTMATH_ADD(type, n, clones)			\
static bool OPTIMIZE3 clones stress_intmath_add_ ## n(		\
	stress_intmath_vals_t *vals,				\
	const int idx,						\
	const bool verify,					\
	double *duration)					\
{								\
	unsigned int i;						\
	double t;						\
								\
	register type r0, r1, r2, r3;				\
	register type i0, i1, i2, i3;				\
								\
	r0 = (type)vals->init[0];				\
	r1 = (type)vals->init[1];				\
	r2 = (type)vals->init[2];				\
	r3 = (type)vals->init[3];				\
								\
	i0 = ~r0;						\
	i1 = ~r1;						\
	i2 = ~r2;						\
	i3 = ~r3;						\
								\
	t = stress_time_now();					\
PRAGMA_UNROLL_N(8)						\
	for (i = 0; i < 100; i++) {				\
		r0 += i0;					\
		r1 += i1;					\
		r2 += i2;					\
		r3 += i3;					\
								\
		i0 += r0;					\
		i1 += r1;					\
		i2 += r2;					\
		i3 += r3;					\
	}							\
	*duration = stress_time_now() - t;			\
								\
	vals->add.result ## n[idx] = r0	+ r1 + r2 + r3;		\
	if (verify)						\
		return vals->add.result ## n[0] ==		\
		       vals->add.result ## n[1];		\
	return true;						\
}

#define STRESS_INTMATH_SUB(type, n, clones)			\
static bool OPTIMIZE3 clones stress_intmath_sub_ ## n(		\
	stress_intmath_vals_t *vals,				\
	const int idx,						\
	const bool verify,					\
	double *duration)					\
{								\
	unsigned int i;						\
	double t;						\
								\
	register type r0, r1, r2, r3;				\
	register type i0, i1, i2, i3;				\
								\
	r0 = (type)vals->init[0];				\
	r1 = (type)vals->init[1];				\
	r2 = (type)vals->init[2];				\
	r3 = (type)vals->init[3];				\
								\
	i0 = r3;						\
	i1 = r2;						\
	i2 = r1;						\
	i3 = r0;						\
								\
	t = stress_time_now();					\
PRAGMA_UNROLL_N(8)						\
	for (i = 0; i < 100; i++) {				\
		r0 = i0 - r0;					\
		r1 = i1 - r1;					\
		r2 = i2 - r2;					\
		r3 = i3 - r3;					\
								\
		i0 = r0 - i0;					\
		i1 = r1 - i1;					\
		i2 = r2 - i2;					\
		i3 = r3 - r3;					\
	}							\
	*duration = stress_time_now() - t;			\
								\
	vals->sub.result ## n[idx] = r0	- r1 - r2 - r3;		\
	if (verify)						\
		return vals->sub.result ## n[0] ==		\
		       vals->sub.result ## n[1];		\
	return true;						\
}

#define STRESS_INTMATH_MUL(type, n, clones)			\
static bool OPTIMIZE3 clones stress_intmath_mul_ ## n(		\
	stress_intmath_vals_t *vals,				\
	const int idx,						\
	const bool verify,					\
	double *duration)					\
{								\
	type i;							\
	double t;						\
								\
	register type r0, r1, r2, r3;				\
	register type i0, i1, i2, i3;				\
	register type s0 = 1;					\
	register type s1 = 1;					\
	register type s2 = 1;					\
	register type s3 = 1;					\
								\
	i0 = (type)vals->init[0];				\
	i1 = (type)vals->init[1];				\
	i2 = (type)vals->init[2];				\
	i3 = (type)vals->init[3];				\
	r3 = ~i0;						\
								\
	t = stress_time_now();					\
PRAGMA_UNROLL_N(8)						\
	for (i = 0; i < 100; i++) {				\
		r0 = i0 * r3;					\
		s0 ^= r0;					\
		r1 = i1 * r0;					\
		s1 ^= r1;					\
		r2 = i2 * r1;					\
		s2 ^= r2;					\
		r3 = i3 * r2;					\
		s3 ^= s3;					\
	}							\
	*duration = stress_time_now() - t;			\
								\
	vals->mul.result ## n[idx] = s0 + s1 + s2 + s3;		\
	if (verify)						\
		return vals->mul.result ## n[0] ==		\
		       vals->mul.result ## n[1];		\
	return true;						\
}

#define STRESS_INTMATH_DIV(type, n, clones)			\
static bool OPTIMIZE3 clones stress_intmath_div_ ## n(		\
	stress_intmath_vals_t *vals,				\
	const int idx,						\
	const bool verify,					\
	double *duration)					\
{								\
	type i;							\
	double t;						\
								\
	register type r0, r1, r2, r3;				\
	register type i0, i1, i2, i3;				\
	register type s0 = 1;					\
	register type s1 = 1;					\
	register type s2 = 1;					\
	register type s3 = 1;					\
								\
	i0 = (type)vals->init[0];				\
	i1 = (type)vals->init[1];				\
	i2 = (type)vals->init[2];				\
	i3 = (type)vals->init[3];				\
								\
	t = stress_time_now();					\
PRAGMA_UNROLL_N(8)						\
	for (i = 1; i < 101; i++) {				\
		r0 = i0 / i;					\
		s0 ^= r0;					\
		r1 = i1 / i;					\
		s1 ^= r1;					\
		r2 = i2 / i;					\
		s2 ^= r2;					\
		r3 = i3 / i;					\
		s3 ^= r3;					\
	}							\
	*duration = stress_time_now() - t;			\
								\
	vals->div.result ## n[idx] = s0 + s1 + s2 + s3;		\
	if (verify)						\
		return vals->div.result ## n[0] ==		\
		       vals->div.result ## n[1];		\
	return true;						\
}

#define STRESS_INTMATH_MOD(type, n, clones)			\
static bool OPTIMIZE3 clones stress_intmath_mod_ ## n(		\
	stress_intmath_vals_t *vals,				\
	const int idx,						\
	const bool verify,					\
	double *duration)					\
{								\
	type i;							\
	double t;						\
								\
	register type r0, r1, r2, r3;				\
	register type i0, i1, i2, i3;				\
	register type s0 = 1;					\
	register type s1 = 1;					\
	register type s2 = 1;					\
	register type s3 = 1;					\
								\
	i0 = (type)vals->init[0];				\
	i1 = (type)vals->init[1];				\
	i2 = (type)vals->init[2];				\
	i3 = (type)vals->init[3];				\
								\
	t = stress_time_now();					\
PRAGMA_UNROLL_N(8)						\
	for (i = 1; i < 101; i++) {				\
		r0 = i0 % i;					\
		s0 ^= r0;					\
		r1 = i1 % i;					\
		s1 ^= r1;					\
		r2 = i2 % i;					\
		s2 ^= r2;					\
		r3 = i3 % i;					\
		s3 ^= r3;					\
	}							\
	*duration = stress_time_now() - t;			\
								\
	vals->mod.result ## n[idx] = s0 + s1 + s2 + s3;		\
	if (verify)						\
		return vals->mod.result ## n[0] ==		\
		       vals->mod.result ## n[1];		\
	return true;						\
}

#if defined(HAVE_INT128_T)
STRESS_INTMATH_ADD(__int128_t, 128, NO_TARGET_CLONES)
#endif
STRESS_INTMATH_ADD(int64_t, 64, NO_TARGET_CLONES)
STRESS_INTMATH_ADD(int32_t, 32, NO_TARGET_CLONES)
STRESS_INTMATH_ADD(int16_t, 16, TARGET_CLONES)
STRESS_INTMATH_ADD(int8_t,   8, TARGET_CLONES)

#if defined(HAVE_INT128_T)
STRESS_INTMATH_SUB(__int128_t, 128, NO_TARGET_CLONES)
#endif
STRESS_INTMATH_SUB(int64_t, 64, NO_TARGET_CLONES)
STRESS_INTMATH_SUB(int32_t, 32, NO_TARGET_CLONES)
STRESS_INTMATH_SUB(int16_t, 16, NO_TARGET_CLONES)
STRESS_INTMATH_SUB(int8_t,   8, NO_TARGET_CLONES)

#if defined(HAVE_INT128_T)
STRESS_INTMATH_MUL(__int128_t, 128, NO_TARGET_CLONES)
#endif
STRESS_INTMATH_MUL(int64_t, 64, NO_TARGET_CLONES)
STRESS_INTMATH_MUL(int32_t, 32, NO_TARGET_CLONES)
STRESS_INTMATH_MUL(int16_t, 16, NO_TARGET_CLONES)
STRESS_INTMATH_MUL(int8_t,   8, NO_TARGET_CLONES)

#if defined(HAVE_INT128_T)
STRESS_INTMATH_DIV(__int128_t, 128, NO_TARGET_CLONES)
#endif
STRESS_INTMATH_DIV(int64_t, 64, NO_TARGET_CLONES)
STRESS_INTMATH_DIV(int32_t, 32, NO_TARGET_CLONES)
STRESS_INTMATH_DIV(int16_t, 16, NO_TARGET_CLONES)
STRESS_INTMATH_DIV(int8_t,   8, NO_TARGET_CLONES)

#if defined(HAVE_INT128_T)
STRESS_INTMATH_MOD(__int128_t, 128, NO_TARGET_CLONES)
#endif
STRESS_INTMATH_MOD(int64_t, 64, NO_TARGET_CLONES)
STRESS_INTMATH_MOD(int32_t, 32, NO_TARGET_CLONES)
STRESS_INTMATH_MOD(int16_t, 16, NO_TARGET_CLONES)
STRESS_INTMATH_MOD(int8_t,   8, NO_TARGET_CLONES)

static const stress_intmath_method_t stress_intmath_methods[] = {
	{ "all",	0,	NULL },

#if defined(HAVE_INT128_T)
	{ "add128",	800,	stress_intmath_add_128 },
#endif
	{ "add64",	800,	stress_intmath_add_64 },
	{ "add32",	800,	stress_intmath_add_32 },
	{ "add16",	800,	stress_intmath_add_16 },
	{ "add8",	800,	stress_intmath_add_8 },

#if defined(HAVE_INT128_T)
	{ "sub128",	800,	stress_intmath_sub_128 },
#endif
	{ "sub64",	800,	stress_intmath_sub_64 },
	{ "sub32",	800,	stress_intmath_sub_32 },
	{ "sub16",	800,	stress_intmath_sub_16 },
	{ "sub8",	800,	stress_intmath_sub_8 },

#if defined(HAVE_INT128_T)
	{ "mul128",	400,	stress_intmath_mul_128 },
#endif
	{ "mul64",	400,	stress_intmath_mul_64 },
	{ "mul32",	400,	stress_intmath_mul_32 },
	{ "mul16",	400,	stress_intmath_mul_16 },
	{ "mul8",	400,	stress_intmath_mul_8 },

#if defined(HAVE_INT128_T)
	{ "div128",	400,	stress_intmath_div_128 },
#endif
	{ "div64",	400,	stress_intmath_div_64 },
	{ "div32",	400,	stress_intmath_div_32 },
	{ "div16",	400,	stress_intmath_div_16 },
	{ "div8",	400,	stress_intmath_div_8 },

#if defined(HAVE_INT128_T)
	{ "mod128",	400,	stress_intmath_mod_128 },
#endif
	{ "mod64",	400,	stress_intmath_mod_64 },
	{ "mod32",	400,	stress_intmath_mod_32 },
	{ "mod16",	400,	stress_intmath_mod_16 },
	{ "mod8",	400,	stress_intmath_mod_8 },
};

#define STRESS_INTMATH_MAX_METHODS	(SIZEOF_ARRAY(stress_intmath_methods))

static stress_metrics_t stress_intmath_metrics[STRESS_INTMATH_MAX_METHODS];
static bool stress_intmath_initialized[STRESS_INTMATH_MAX_METHODS];

static bool stress_intmath_exercise(
	stress_args_t *args,
	stress_intmath_vals_t *vals,
	const size_t method,
	const bool verify)
{
	bool correct;
	double duration;

	if (!stress_intmath_initialized[method]) {
		(void)stress_intmath_methods[method].func(vals, 0, false, &duration);
		stress_intmath_initialized[method] = true;
		stress_intmath_metrics[method].duration += duration;
		stress_intmath_metrics[method].count += 1.0;
	}

	correct = stress_intmath_methods[method].func(vals, 1, verify, &duration);
	if (correct) {
		stress_intmath_metrics[method].duration += duration;
		stress_intmath_metrics[method].count += 1.0;
	}
	if (!correct)
		pr_fail("%s: %s failed verification\n", args->name, stress_intmath_methods[method].name);

	return correct;
}

/*
 *  stress_intmath
 *	stress by generating SIGCHLD signals on exiting
 *	child processes.
 */
static int stress_intmath(stress_args_t *args)
{
	const bool verify = !!(g_opt_flags & OPT_FLAGS_VERIFY);
	stress_intmath_vals_t vals ALIGN64;
	size_t intmath_method = 0;	/* all */
	size_t i, j;

	(void)stress_get_setting("intmath-method", &intmath_method);

	for (i = 0; i < 4; i++)
#if defined(HAVE_INT128_T)
		vals.init[i] = ((__int128_t)stress_mwc64() << 64) | stress_mwc64();
#else
		vals.init[i] = (int64_t)stress_mwc64();
#endif

	(void)shim_memset(stress_intmath_initialized, 0, sizeof(stress_intmath_initialized));

	stress_zero_metrics(stress_intmath_metrics, STRESS_INTMATH_MAX_METHODS);

	stress_set_proc_state(args->name, STRESS_STATE_SYNC_WAIT);
	stress_sync_start_wait(args);
	stress_set_proc_state(args->name, STRESS_STATE_RUN);

	do {
		if (intmath_method == 0) {
			for (i = 1; i < STRESS_INTMATH_MAX_METHODS; i++) {
				stress_intmath_exercise(args, &vals, i, verify);
			}
		} else {
			stress_intmath_exercise(args, &vals, intmath_method, verify);
		}

		stress_bogo_inc(args);
	} while (stress_continue(args));

	for (i = 1, j = 0; i < STRESS_INTMATH_MAX_METHODS; i++) {
		if (stress_intmath_metrics[i].duration > 0.0) {
			const double rate = stress_intmath_metrics[i].count * stress_intmath_methods[i].ops / stress_intmath_metrics[i].duration;
			char msg[64];

			(void)snprintf(msg, sizeof(msg), "%s M-ops per sec", stress_intmath_methods[i].name);
                        stress_metrics_set(args, j, msg, rate / 1000000.0, STRESS_METRIC_HARMONIC_MEAN);
			j++;
		}
	}
	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);

	return EXIT_SUCCESS;
}

static const char *stress_intmath_method(const size_t i)
{
	return (i < STRESS_INTMATH_MAX_METHODS) ? stress_intmath_methods[i].name : NULL;
}

static const stress_opt_t opts[] = {
	{ OPT_intmath_method, "intmath-method", TYPE_ID_SIZE_T_METHOD, 0, 1, stress_intmath_method },
	END_OPT,
};

const stressor_info_t stress_intmath_info = {
	.stressor = stress_intmath,
	.class = CLASS_CPU | CLASS_INTEGER | CLASS_COMPUTE,
	.verify = VERIFY_ALWAYS,
	.opts = opts,
	.help = help
};
