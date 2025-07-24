/*
 * Copyright (C) 2021-2025 Colin Ian King
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
#include "core-arch.h"
#include "core-builtin.h"
#include "core-madvise.h"
#include "core-mmap.h"
#include "core-put.h"
#include "core-pragma.h"
#include "core-target-clones.h"

#include <math.h>

#define D_ALIGNED	ALIGNED(64)	/* alignment for doubles */
#define F_ALIGNED       ALIGNED(64)	/* alignment for floats */

#define FMA_ELEMENTS	(512)
#define FMA_UNROLL	(8)
#define FMA_FUNCS	(6)

/* #define USE_FMA_FAST	 */

typedef struct {
	double  *double_a;
	double	double_init[FMA_ELEMENTS] D_ALIGNED;
	double	double_a1[FMA_ELEMENTS] D_ALIGNED;
	double	double_a2[FMA_ELEMENTS] D_ALIGNED;

	float	*float_a;
	float	float_init[FMA_ELEMENTS] F_ALIGNED;
	float	float_a1[FMA_ELEMENTS] F_ALIGNED;
	float	float_a2[FMA_ELEMENTS] F_ALIGNED;

	double	double_b;
	double	double_c;

	float	float_b;
	float	float_c;
} stress_fma_t;

typedef void (*stress_fma_func_t)(stress_fma_t *fma);

static const stress_help_t help[] = {
	{ NULL,	"fma N",	"start N workers performing floating point multiply-add ops" },
	{ NULL,	"fma-ops N",	"stop after N floating point multiply-add bogo operations" },
	{ NULL, "fma-libc",	"use fma libc fused multiply-add helpers" },
	{ NULL,	NULL,		 NULL }
};

static inline float stress_fma_rnd_float(void)
{
	register const float fhalfpwr32 = (float)1.0 / (float)(0x80000000);

	return (float)stress_mwc32() * fhalfpwr32;
}

static void TARGET_CLONES stress_fma_add132_double(stress_fma_t *fma)
{
	register size_t i;
	register double *a = fma->double_a;
	register double b = fma->double_b;
	register double c = fma->double_c;

PRAGMA_UNROLL_N(FMA_UNROLL)
	for (i = 0; i < FMA_ELEMENTS; i++)
		a[i] = (a[i] * c) + b;
}

static void TARGET_CLONES stress_fma_sub132_double(stress_fma_t *fma)
{
	register size_t i;
	register double *a = fma->double_a;
	register double b = fma->double_b;
	register double c = fma->double_c;

PRAGMA_UNROLL_N(FMA_UNROLL)
	for (i = 0; i < FMA_ELEMENTS; i++)
		a[i] = (a[i] * c) - b;
}

static void TARGET_CLONES stress_fma_add132_float(stress_fma_t *fma)
{
	register size_t i;
	register float *a = fma->float_a;
	register float b = fma->float_b;
	register float c = fma->float_c;

PRAGMA_UNROLL_N(FMA_UNROLL)
	for (i = 0; i < FMA_ELEMENTS; i++)
		a[i] = (a[i] * c) + b;
}

static void TARGET_CLONES stress_fma_sub132_float(stress_fma_t *fma)
{
	register size_t i;
	register float *a = fma->float_a;
	register float b = fma->float_b;
	register float c = fma->float_c;

PRAGMA_UNROLL_N(FMA_UNROLL)
	for (i = 0; i < FMA_ELEMENTS; i++)
		a[i] = (a[i] * c) - b;
}

static void TARGET_CLONES stress_fma_add213_double(stress_fma_t *pfma)
{
	register size_t i;
	register double *a = pfma->double_a;
	register double b = pfma->double_b;
	register double c = pfma->double_c;

PRAGMA_UNROLL_N(FMA_UNROLL)
	for (i = 0; i < FMA_ELEMENTS; i++)
		a[i] = (b * a[i]) + c;
}

static void TARGET_CLONES stress_fma_sub213_double(stress_fma_t *pfma)
{
	register size_t i;
	register double *a = pfma->double_a;
	register double b = pfma->double_b;
	register double c = pfma->double_c;

PRAGMA_UNROLL_N(FMA_UNROLL)
	for (i = 0; i < FMA_ELEMENTS; i++)
		a[i] = (b * a[i]) + c;
}

static void TARGET_CLONES stress_fma_add213_float(stress_fma_t *pfma)
{
	register size_t i;
	register float *a = pfma->float_a;
	register const float b = pfma->float_b;
	register const float c = pfma->float_c;

PRAGMA_UNROLL_N(FMA_UNROLL)
	for (i = 0; i < FMA_ELEMENTS; i++)
		a[i] = (b * a[i]) + c;
}

static void TARGET_CLONES stress_fma_sub213_float(stress_fma_t *pfma)
{
	register size_t i;
	register float *a = pfma->float_a;
	register const float b = pfma->float_b;
	register const float c = pfma->float_c;

PRAGMA_UNROLL_N(FMA_UNROLL)
	for (i = 0; i < FMA_ELEMENTS; i++)
		a[i] = (b * a[i]) + c;
}

static void TARGET_CLONES stress_fma_add231_double(stress_fma_t *pfma)
{
	register size_t i;
	register double *a = pfma->double_a;
	register double b = pfma->double_b;
	register const double c = pfma->double_c;

PRAGMA_UNROLL_N(FMA_UNROLL)
	for (i = 0; i < FMA_ELEMENTS; i++) {
		a[i] = (b * c) + a[i];
		b += 0.125;
	}
}

static void TARGET_CLONES stress_fma_sub231_double(stress_fma_t *pfma)
{
	register size_t i;
	register double *a = pfma->double_a;
	register double b = pfma->double_b;
	register const double c = pfma->double_c;

PRAGMA_UNROLL_N(FMA_UNROLL)
	for (i = 0; i < FMA_ELEMENTS; i++) {
		a[i] = (b * c) + a[i];
		b += 0.125;
	}
}

static void TARGET_CLONES stress_fma_add231_float(stress_fma_t *pfma)
{
	register size_t i;
	register float *a = pfma->float_a;
	register float b = pfma->float_b;
	register const float c = pfma->float_c;

PRAGMA_UNROLL_N(FMA_UNROLL)
	for (i = 0; i < FMA_ELEMENTS; i++) {
		a[i] = (b * c) + a[i];
		b += 0.125;
	}
}

static void TARGET_CLONES stress_fma_sub231_float(stress_fma_t *pfma)
{
	register size_t i;
	register float *a = pfma->float_a;
	register float b = pfma->float_b;
	register const float c = pfma->float_c;

PRAGMA_UNROLL_N(FMA_UNROLL)
	for (i = 0; i < FMA_ELEMENTS; i++) {
		a[i] = (b * c) + a[i];
		b += 0.125;
	}
}

stress_fma_func_t stress_fma_funcs[] = {
	stress_fma_add132_double,
	stress_fma_add132_float,
	stress_fma_add213_double,
	stress_fma_add213_float,
	stress_fma_add231_double,
	stress_fma_add231_float,

	stress_fma_sub132_double,
	stress_fma_sub132_float,
	stress_fma_sub213_double,
	stress_fma_sub213_float,
	stress_fma_sub231_double,
	stress_fma_sub231_float,
};

/* libc variants */
#if (defined(HAVE_FMA)  || defined(FP_FAST_FMA)) && 	\
    (defined(HAVE_FMAF) || defined(FP_FAST_FMAF))
static void TARGET_CLONES stress_fma_add132_libc_double(stress_fma_t *pfma)
{
	register size_t i;
	register double *a = pfma->double_a;
	register const double b = pfma->double_b;
	register const double c = pfma->double_c;

PRAGMA_UNROLL_N(FMA_UNROLL)
	for (i = 0; i < FMA_ELEMENTS; i++) {
#if defined(FP_FAST_FMA) &&	\
    defined(USE_FMA_FAST)
		a[i] = FP_FAST_FMA(a[i], c, b);
#else
		a[i] = shim_fma(a[i], c, b);
#endif
	}
}

static void TARGET_CLONES stress_fma_sub132_libc_double(stress_fma_t *pfma)
{
	register size_t i;
	register double *a = pfma->double_a;
	register const double b = -(pfma->double_b);
	register const double c = pfma->double_c;

PRAGMA_UNROLL_N(FMA_UNROLL)
	for (i = 0; i < FMA_ELEMENTS; i++) {
#if defined(FP_FAST_FMA) &&	\
    defined(USE_FMA_FAST)
		a[i] = FP_FAST_FMA(a[i], c, b);
#else
		a[i] = shim_fma(a[i], c, b);
#endif
	}
}

static void TARGET_CLONES stress_fma_add132_libc_float(stress_fma_t *pfma)
{
	register size_t i;
	register float *a = pfma->float_a;
	register const float b = pfma->float_b;
	register const float c = pfma->float_c;

PRAGMA_UNROLL_N(FMA_UNROLL)
	for (i = 0; i < FMA_ELEMENTS; i++) {
#if defined(FP_FAST_FMAF) &&	\
    defined(USE_FMA_FAST)
		a[i] = FP_FAST_FMAF(a[i], c, b);
#else
		a[i] = shim_fmaf(a[i], c, b);
#endif
	}
}

static void TARGET_CLONES stress_fma_sub132_libc_float(stress_fma_t *pfma)
{
	register size_t i;
	register float *a = pfma->float_a;
	register const float b = -(pfma->float_b);
	register const float c = pfma->float_c;

PRAGMA_UNROLL_N(FMA_UNROLL)
	for (i = 0; i < FMA_ELEMENTS; i++) {
#if defined(FP_FAST_FMAF) &&	\
    defined(USE_FMA_FAST)
		a[i] = FP_FAST_FMAF(a[i], c, b);
#else
		a[i] = shim_fmaf(a[i], c, b);
#endif
	}
}

static void TARGET_CLONES stress_fma_add213_libc_double(stress_fma_t *pfma)
{
	register size_t i;
	register double *a = pfma->double_a;
	register const double b = pfma->double_b;
	register const double c = pfma->double_c;

PRAGMA_UNROLL_N(FMA_UNROLL)
	for (i = 0; i < FMA_ELEMENTS; i++) {
#if defined(FP_FAST_FMA) && 	\
    defined(USE_FMA_FAST)
		a[i] = FP_FAST_FMA(b, a[i], c);
#else
		a[i] = shim_fma(b, a[i], c);
#endif
	}
}

static void TARGET_CLONES stress_fma_sub213_libc_double(stress_fma_t *pfma)
{
	register size_t i;
	register double *a = pfma->double_a;
	register const double b = pfma->double_b;
	register const double c = -(pfma->double_c);

PRAGMA_UNROLL_N(FMA_UNROLL)
	for (i = 0; i < FMA_ELEMENTS; i++) {
#if defined(FP_FAST_FMA) && 	\
    defined(USE_FMA_FAST)
		a[i] = FP_FAST_FMA(b, a[i], c);
#else
		a[i] = shim_fma(b, a[i], c);
#endif
	}
}

static void TARGET_CLONES stress_fma_add213_libc_float(stress_fma_t *pfma)
{
	register size_t i;
	register float *a = pfma->float_a;
	register const float b = pfma->float_b;
	register const float c = pfma->float_c;

PRAGMA_UNROLL_N(FMA_UNROLL)
	for (i = 0; i < FMA_ELEMENTS; i++) {
#if defined(FP_FAST_FMAF) &&	\
    defined(USE_FMA_FAST)
		a[i] = FP_FAST_FMAF(b, a[i], c);
#else
		a[i] = shim_fmaf(b, a[i], c);
#endif
	}
}

static void TARGET_CLONES stress_fma_sub213_libc_float(stress_fma_t *pfma)
{
	register size_t i;
	register float *a = pfma->float_a;
	register const float b = pfma->float_b;
	register const float c = -(pfma->float_c);

PRAGMA_UNROLL_N(FMA_UNROLL)
	for (i = 0; i < FMA_ELEMENTS; i++) {
#if defined(FP_FAST_FMAF) &&	\
    defined(USE_FMA_FAST)
		a[i] = FP_FAST_FMAF(b, a[i], c);
#else
		a[i] = shim_fmaf(b, a[i], c);
#endif
	}
}

static void TARGET_CLONES stress_fma_add231_libc_double(stress_fma_t *pfma)
{
	register size_t i;
	register double *a = pfma->double_a;
	register double b = pfma->double_b;
	register const double c = pfma->double_c;

PRAGMA_UNROLL_N(FMA_UNROLL)
	for (i = 0; i < FMA_ELEMENTS; i++) {
#if defined(FP_FAST_FMA) &&	\
    defined(USE_FMA_FAST)
		a[i] = FP_FAST_FMA(b, c, a[i]);
#else
		a[i] = shim_fma(b, c, a[i]);
#endif
		b += 0.125;
	}
}

static void TARGET_CLONES stress_fma_sub231_libc_double(stress_fma_t *pfma)
{
	register size_t i;
	register double *a = pfma->double_a;
	register double b = pfma->double_b;
	register const double c = pfma->double_c;

PRAGMA_UNROLL_N(FMA_UNROLL)
	for (i = 0; i < FMA_ELEMENTS; i++) {
#if defined(FP_FAST_FMA) &&	\
    defined(USE_FMA_FAST)
		a[i] = FP_FAST_FMA(b, c, -a[i]);
#else
		a[i] = shim_fma(b, c, -a[i]);
#endif
		b += 0.125;
	}
}

static void TARGET_CLONES stress_fma_add231_libc_float(stress_fma_t *pfma)
{
	register size_t i;
	register float *a = pfma->float_a;
	register float b = pfma->float_b;
	register const float c = pfma->float_c;

PRAGMA_UNROLL_N(FMA_UNROLL)
	for (i = 0; i < FMA_ELEMENTS; i++) {
#if defined(FP_FAST_FMAF) &&	\
    defined(USE_FMA_FAST)
		a[i] = FP_FAST_FMAF(b, c, a[i]);
#else
		a[i] = shim_fmaf(b, c, a[i]);
#endif
		b += 0.125;
	}
}

static void TARGET_CLONES stress_fma_sub231_libc_float(stress_fma_t *pfma)
{
	register size_t i;
	register float *a = pfma->float_a;
	register float b = pfma->float_b;
	register const float c = pfma->float_c;

PRAGMA_UNROLL_N(FMA_UNROLL)
	for (i = 0; i < FMA_ELEMENTS; i++) {
#if defined(FP_FAST_FMAF) &&	\
    defined(USE_FMA_FAST)
		a[i] = FP_FAST_FMAF(b, c,-a[i]);
#else
		a[i] = shim_fmaf(b, c, -a[i]);
#endif
		b += 0.125;
	}
}

stress_fma_func_t stress_fma_libc_funcs[] = {
	stress_fma_add132_libc_double,
	stress_fma_add132_libc_float,
	stress_fma_add213_libc_double,
	stress_fma_add213_libc_float,
	stress_fma_add231_libc_double,
	stress_fma_add231_libc_float,

	stress_fma_sub132_libc_double,
	stress_fma_sub132_libc_float,
	stress_fma_sub213_libc_double,
	stress_fma_sub213_libc_float,
	stress_fma_sub231_libc_double,
	stress_fma_sub231_libc_float,
};
#endif

static inline void OPTIMIZE3 TARGET_CLONES stress_fma_init(stress_fma_t *pfma)
{
	register size_t i;

PRAGMA_UNROLL_N(FMA_UNROLL)
	for (i = 0; i < FMA_ELEMENTS; i++) {
		register const float rnd = stress_fma_rnd_float();

		pfma->double_init[i] = (double)rnd;
		pfma->float_init[i] = rnd;
	}
}

static inline void OPTIMIZE3 TARGET_CLONES stress_fma_reset_a(stress_fma_t *pfma)
{
	(void)shim_memcpy(pfma->double_a1, pfma->double_init, sizeof(pfma->double_init));
	(void)shim_memcpy(pfma->double_a2, pfma->double_init, sizeof(pfma->double_init));

	(void)shim_memcpy(pfma->float_a1, pfma->float_init, sizeof(pfma->float_init));
	(void)shim_memcpy(pfma->float_a2, pfma->float_init, sizeof(pfma->float_init));
}

static int stress_fma(stress_args_t *args)
{
	stress_fma_t *pfma;
	register size_t idx_b = 0, idx_c = 0;
	const bool verify = !!(g_opt_flags & OPT_FLAGS_VERIFY);
	const stress_fma_func_t *fma_func_array;
	bool fma_libc = false;
	int rc = EXIT_SUCCESS;
	size_t offset = 0;

	(void)stress_get_setting("fma-libc", &fma_libc);
#if (defined(HAVE_FMA)  || defined(FP_FAST_FMA)) && 	\
    (defined(HAVE_FMAF) || defined(FP_FAST_FMAF))
	fma_func_array = fma_libc ? stress_fma_libc_funcs : stress_fma_funcs;
#else
	if (fma_libc) {
		pr_inf("%s: libc fma functions not available, defaulting "
			"to non-libc fma operations\n", args->name);
	}
	fma_func_array = stress_fma_funcs;
#endif

	stress_catch_sigill();

	pfma = (stress_fma_t *)stress_mmap_populate(NULL, sizeof(*pfma),
				PROT_READ | PROT_WRITE,
				MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
	if (pfma == MAP_FAILED) {
		pr_inf_skip("%s: failed to mmap %zu bytes for FMA data%s, skipping stressor\n",
			args->name, sizeof(*pfma), stress_get_memfree_str());
		return EXIT_NO_RESOURCE;
	}
	stress_set_vma_anon_name(pfma, sizeof(*pfma), "fma-data");
	stress_madvise_mergeable(pfma, sizeof(*pfma));

	stress_set_proc_state(args->name, STRESS_STATE_SYNC_WAIT);
	stress_sync_start_wait(args);
	stress_set_proc_state(args->name, STRESS_STATE_RUN);

	stress_fma_init(pfma);

	do {
		register size_t i;

		stress_fma_reset_a(pfma);

		idx_b++;
		if (idx_b >= FMA_ELEMENTS)
			idx_b = 0;
		idx_c += 3;
		if (idx_c >= FMA_ELEMENTS)
			idx_c = 0;

		pfma->double_a = pfma->double_a1;
		pfma->double_b = pfma->double_a[idx_b];
		pfma->double_c = pfma->double_a[idx_c];
		pfma->float_a = pfma->float_a1;
		pfma->float_b = pfma->float_a[idx_b];
		pfma->float_c = pfma->float_a[idx_c];

		for (i = 0; i < FMA_FUNCS; i++) {
			fma_func_array[i + offset](pfma);
		}
		stress_bogo_inc(args);

		if (verify) {
			pfma->double_a = pfma->double_a2;
			pfma->double_b = pfma->double_a[idx_b];
			pfma->double_c = pfma->double_a[idx_c];
			pfma->float_a = pfma->float_a2;
			pfma->float_b = pfma->float_a[idx_b];
			pfma->float_c = pfma->float_a[idx_c];

			for (i = 0; i < FMA_FUNCS; i++) {
				fma_func_array[i + offset](pfma);
			}
			stress_bogo_inc(args);

			if (shim_memcmp(pfma->double_a1, pfma->double_a2, sizeof(pfma->double_a1))) {
				pr_fail("%s: data difference between identical double fma computations\n", args->name);
				rc = EXIT_FAILURE;
			}
			if (shim_memcmp(pfma->float_a1, pfma->float_a2, sizeof(pfma->float_a1))) {
				pr_fail("%s: data difference between identical float fma computations\n", args->name);
				rc = EXIT_FAILURE;
			}
		}
		offset = 6 - offset;
	} while (stress_continue(args));

	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);

	(void)munmap((void *)pfma, sizeof(*pfma));

	return rc;
}

static const stress_opt_t opts[] = {
	{ OPT_fma_libc, "fma-libc", TYPE_ID_BOOL, 0, 1, NULL },
	END_OPT,
};

const stressor_info_t stress_fma_info = {
	.stressor = stress_fma,
	.classifier = CLASS_CPU | CLASS_FP | CLASS_COMPUTE,
	.opts = opts,
	.verify = VERIFY_OPTIONAL,
	.help = help
};
