/*
 * Copyright (C) 2023-2025 Colin Ian King
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
#include "core-pragma.h"
#include "core-put.h"

#define ROTATE_LOOPS	(10000)

static const stress_help_t help[] = {
	{ NULL,	"rotate N",		"start N workers performing rotate ops" },
	{ NULL,	"rotate-method M",	"select rotate method M" },
	{ NULL,	"rotate-ops N",		"stop after N rotate bogo operations" },
	{ NULL,	NULL,			NULL }
};

typedef double (*stress_rotate_func_t)(stress_args_t *args, const bool verify, bool *success);

static double stress_rotate_all(stress_args_t *args, const bool verify, bool *success);

#if defined(HAVE_INT128_T)
static __uint128_t stress_mwc128(void)
{
	const uint64_t hi = stress_mwc64();
	const uint64_t lo = stress_mwc64();

	return ((__uint128_t)hi << 64) | lo;
}
#endif

/*
 *  stress_{ror|rol}{size}helper()
 *	helper function to perform looped rotates, note that
 *	the checksum puts are required to stop optimizer from
 *	merging in the verify step into the same computation
 *	as the non-verify step
 */
#define STRESS_ROTATE_HELPER(fname, type, size, rotate_macro)	\
static inline double 						\
stress_ ## fname ## size ## helper(stress_args_t *args, type *checksum)\
{								\
	double t1, t2;						\
	register type v0, v1, v2, v3;				\
	register int i;						\
								\
	v0 = stress_mwc ## size();				\
	v1 = stress_mwc ## size();				\
	v2 = stress_mwc ## size();				\
	v3 = stress_mwc ## size();				\
	*checksum = v0 + v1 + v2 + v3;				\
	stress_uint ## size ## _put(*checksum);			\
								\
	t1 = stress_time_now();					\
PRAGMA_UNROLL_N(8)						\
	for (i = 0; i < ROTATE_LOOPS; i++) {			\
		v0 = rotate_macro ## size(v0);			\
		v1 = rotate_macro ## size(v1);			\
		v2 = rotate_macro ## size(v2);			\
		v3 = rotate_macro ## size(v3);			\
	}							\
	t2 = stress_time_now();					\
	stress_bogo_inc(args);					\
	*checksum = v0 + v1 + v2 + v3;				\
	stress_uint ## size ## _put(*checksum);			\
	return t2 - t1;						\
}

#define STRESS_ROTATE(fname, type, size, rotate_macro)		\
static double OPTIMIZE3	\
stress_ ## fname ## size(stress_args_t *args, const bool verify, bool *success)\
{								\
	double duration;					\
	uint32_t w, z;						\
	type checksum0;						\
								\
	stress_mwc_get_seed(&w, &z);				\
	duration = stress_ ## fname ## size ## helper		\
			(args, &checksum0);			\
								\
	if (verify) {						\
		type checksum1;					\
								\
		stress_mwc_set_seed(w, z);			\
		duration += stress_ ## fname ## size ## helper	\
				(args, &checksum1);		\
		if (UNLIKELY(checksum0 != checksum1)) {		\
			pr_fail("%s: failed checksum with a "	\
				"%s uint%d_t operation\n",	\
				args->name, # fname, size);	\
			*success = false;			\
		}						\
	}							\
	return duration;					\
}

STRESS_ROTATE_HELPER(rol,     uint8_t,   8, shim_rol)
STRESS_ROTATE_HELPER(rol,    uint16_t,  16, shim_rol)
STRESS_ROTATE_HELPER(rol,    uint32_t,  32, shim_rol)
STRESS_ROTATE_HELPER(rol,    uint64_t,  64, shim_rol)
#if defined(HAVE_INT128_T)
STRESS_ROTATE_HELPER(rol, __uint128_t, 128, shim_ror)
#endif

STRESS_ROTATE(rol,     uint8_t,   8, shim_rol)
STRESS_ROTATE(rol,    uint16_t,  16, shim_rol)
STRESS_ROTATE(rol,    uint32_t,  32, shim_rol)
STRESS_ROTATE(rol,    uint64_t,  64, shim_rol)
#if defined(HAVE_INT128_T)
STRESS_ROTATE(rol, __uint128_t, 128, shim_ror)
#endif

STRESS_ROTATE_HELPER(ror,     uint8_t,   8, shim_ror)
STRESS_ROTATE_HELPER(ror,    uint16_t,  16, shim_ror)
STRESS_ROTATE_HELPER(ror,    uint32_t,  32, shim_ror)
STRESS_ROTATE_HELPER(ror,    uint64_t,  64, shim_ror)
#if defined(HAVE_INT128_T)
STRESS_ROTATE_HELPER(ror, __uint128_t, 128, shim_ror)
#endif

STRESS_ROTATE(ror,     uint8_t,   8, shim_ror)
STRESS_ROTATE(ror,    uint16_t,  16, shim_ror)
STRESS_ROTATE(ror,    uint32_t,  32, shim_ror)
STRESS_ROTATE(ror,    uint64_t,  64, shim_ror)
#if defined(HAVE_INT128_T)
STRESS_ROTATE(ror, __uint128_t, 128, shim_ror)
#endif

typedef struct {
	const char *name;
	const stress_rotate_func_t	rotate_func;
} stress_rotate_funcs_t;

static const stress_rotate_funcs_t stress_rotate_funcs[] = {
	{ "all",	stress_rotate_all, },
	{ "rol8",	stress_rol8 },
	{ "ror8",	stress_ror8 },
	{ "rol16",	stress_rol16 },
	{ "ror16",	stress_ror16 },
	{ "rol32",	stress_rol32 },
	{ "ror32",	stress_ror32 },
	{ "rol64",	stress_rol64 },
	{ "ror64",	stress_ror64 },
#if defined(HAVE_INT128_T)
	{ "rol128",	stress_rol128 },
#endif
#if defined(HAVE_INT128_T)
	{ "ror128",	stress_ror128 },
#endif
};

static stress_metrics_t stress_rotate_metrics[SIZEOF_ARRAY(stress_rotate_funcs)];

static void stress_rotate_call_method(
	stress_args_t *args,
	const size_t method,
	const bool verify,
	bool *success)
{
	const double dt = stress_rotate_funcs[method].rotate_func(args, verify, success);

	stress_rotate_metrics[method].duration += dt;
	stress_rotate_metrics[method].count += (double)ROTATE_LOOPS * 4.0 * (verify ? 2.0 : 1.0);
}

static double stress_rotate_all(stress_args_t *args, const bool verify, bool *success)
{
	size_t i;

	for (i = 1; i < SIZEOF_ARRAY(stress_rotate_funcs); i++) {
		stress_rotate_call_method(args, i, verify, success);
	}
	return 0.0;
}

static int stress_rotate(stress_args_t *args)
{
	size_t rotate_method = 0;	/* "all" */
	size_t i, j;
	bool success = true;
	const bool verify = !!(g_opt_flags & OPT_FLAGS_VERIFY);

	stress_zero_metrics(stress_rotate_metrics, SIZEOF_ARRAY(stress_rotate_metrics));

	(void)stress_get_setting("rotate-method", &rotate_method);

	stress_set_proc_state(args->name, STRESS_STATE_SYNC_WAIT);
	stress_sync_start_wait(args);
	stress_set_proc_state(args->name, STRESS_STATE_RUN);

	do {
		stress_rotate_call_method(args, rotate_method, verify, &success);
	} while (stress_continue(args));

	for (i = 1, j = 0; i < SIZEOF_ARRAY(stress_rotate_funcs); i++) {
		if (stress_rotate_metrics[i].duration > 0.0) {
			char msg[64];
			const double rate = stress_rotate_metrics[i].count / stress_rotate_metrics[i].duration;

			(void)snprintf(msg, sizeof(msg), "%s rotate ops per sec", stress_rotate_funcs[i].name);
			stress_metrics_set(args, j, msg,
				rate, STRESS_METRIC_HARMONIC_MEAN);
			j++;
		}
	}

	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);

	return success ? EXIT_SUCCESS : EXIT_FAILURE;
}

static const char *stress_rotate_method(const size_t i)
{
	return (i < SIZEOF_ARRAY(stress_rotate_funcs)) ? stress_rotate_funcs[i].name : NULL;
}

static const stress_opt_t opts[] = {
        { OPT_rotate_method, "rotate-method", TYPE_ID_SIZE_T_METHOD, 0, 0, stress_rotate_method },
	END_OPT,
};

const stressor_info_t stress_rotate_info = {
	.stressor = stress_rotate,
	.classifier = CLASS_CPU | CLASS_INTEGER,
	.opts = opts,
	.verify = VERIFY_OPTIONAL,
	.help = help
};
