/*
 * Copyright (C)      2023 Colin Ian King
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
#include "core-put.h"

#define ROTATE_LOOPS	(10000)

static const stress_help_t help[] = {
	{ NULL,	"rotate N",		"start N workers performing rotate ops" },
	{ NULL,	"rotate-method M",	"select rotate method M" },
	{ NULL,	"rotate-ops N",		"stop after N rotate bogo operations" },
	{ NULL,	NULL,			NULL }
};

typedef double (*stress_rotate_func_t)(const stress_args_t *args);

static double stress_rotate_all(const stress_args_t *args);

#if defined(HAVE_INT128_T)
static __uint128_t stress_mwc128(void)
{
	uint64_t hi = stress_mwc64();
	uint64_t lo = stress_mwc64();

	return ((__uint128_t)hi << 64) | lo;
}
#endif

#define STRESS_ROTATE(name, type, size, rotate_macro)	\
static double 						\
stress_ ## name ## size(const stress_args_t *args)	\
{							\
	type v0 = stress_mwc ## size();			\
	type v1 = stress_mwc ## size();			\
	type v2 = stress_mwc ## size();			\
	type v3 = stress_mwc ## size();			\
	double t1, t2;					\
	register int i = 0;				\
							\
	t1 = stress_time_now();				\
	for (i = 0; i < ROTATE_LOOPS; i++) {		\
		rotate_macro ## size(v0);		\
		rotate_macro ## size(v1);		\
		rotate_macro ## size(v2);		\
		rotate_macro ## size(v3);		\
	}						\
	t2 = stress_time_now();				\
							\
	stress_uint ## size ## _put(v0);		\
	stress_uint ## size ## _put(v1);		\
	stress_uint ## size ## _put(v2);		\
	stress_uint ## size ## _put(v3);		\
							\
	inc_counter(args);				\
	return t2 - t1;					\
}

STRESS_ROTATE(rol,     uint8_t,   8, shim_rol)
STRESS_ROTATE(rol,    uint16_t,  16, shim_rol)
STRESS_ROTATE(rol,    uint32_t,  32, shim_rol)
STRESS_ROTATE(rol,    uint64_t,  64, shim_rol)
#if defined(HAVE_INT128_T)
STRESS_ROTATE(rol, __uint128_t, 128, shim_ror)
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
	stress_rotate_func_t	rotate_func;
} stress_rotate_funcs_t;

typedef struct {
	double	duration;
	double	count;
} stress_rotate_metrics_t;

static stress_rotate_funcs_t stress_rotate_funcs[] = {
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

static stress_rotate_metrics_t stress_rotate_metrics[SIZEOF_ARRAY(stress_rotate_funcs)];

static void stress_rotate_call_method(
	const stress_args_t *args,
	const size_t method)
{
	const double dt = stress_rotate_funcs[method].rotate_func(args);

	stress_rotate_metrics[method].duration += dt;
	stress_rotate_metrics[method].count += (double)ROTATE_LOOPS * 4.0;
}

static double stress_rotate_all(const stress_args_t *args)
{
	size_t i;

	for (i = 1; i < SIZEOF_ARRAY(stress_rotate_funcs); i++) {
		stress_rotate_call_method(args, i);
	}
	return 0.0;
}

/*
 *  stress_set_rotate_method()
 *	set the default vector floating point stress method
 */
static int stress_set_rotate_method(const char *name)
{
	size_t i;

	for (i = 0; i < SIZEOF_ARRAY(stress_rotate_funcs); i++) {
		if (!strcmp(stress_rotate_funcs[i].name, name)) {
			stress_set_setting("rotate-method", TYPE_ID_SIZE_T, &i);
			return 0;
		}
	}

	(void)fprintf(stderr, "rotate-method must be one of:");
	for (i = 0; i < SIZEOF_ARRAY(stress_rotate_funcs); i++) {
		(void)fprintf(stderr, " %s", stress_rotate_funcs[i].name);
	}
	(void)fprintf(stderr, "\n");

	return -1;
}

static int stress_rotate(const stress_args_t *args)
{
	size_t rotate_method = 0;	/* "all" */
	size_t i, j;

	for (i = 1; i < SIZEOF_ARRAY(stress_rotate_metrics); i++) {
		stress_rotate_metrics[i].duration = 0.0;
		stress_rotate_metrics[i].count = 0.0;
	}

	(void)stress_get_setting("rotate-method", &rotate_method);

	stress_set_proc_state(args->name, STRESS_STATE_RUN);

	do {
		stress_rotate_call_method(args, rotate_method);
	} while (keep_stressing(args));


	for (i = 1, j = 0; i < SIZEOF_ARRAY(stress_rotate_funcs); i++) {
		if (stress_rotate_metrics[i].duration > 0.0) {
			char msg[64];
			const double rate = stress_rotate_metrics[i].count / stress_rotate_metrics[i].duration;

			(void)snprintf(msg, sizeof(msg), "%s rotate ops per sec", stress_rotate_funcs[i].name);
			stress_metrics_set(args, j, msg, rate);
			j++;
		}
	}

	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);

	return EXIT_SUCCESS;
}

static const stress_opt_set_func_t opt_set_funcs[] = {
        { OPT_rotate_method,	stress_set_rotate_method },
};

stressor_info_t stress_rotate_info = {
	.stressor = stress_rotate,
	.class = CLASS_CPU | CLASS_CPU_CACHE,
	.opt_set_funcs = opt_set_funcs,
	.help = help
};
