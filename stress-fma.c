/*
 * Copyright (C) 2021-2023 Colin Ian King
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
#include "core-put.h"
#include "core-pragma.h"
#include "core-target-clones.h"

#define FMA_ELEMENTS	(512)
#define FMA_UNROLL	(8)

typedef struct {
	double	double_init[FMA_ELEMENTS];
	double	double_a[FMA_ELEMENTS];

	float	float_init[FMA_ELEMENTS];
	float	float_a[FMA_ELEMENTS];

	double	double_b;
	double	double_c;

	float	float_b;
	float	float_c;
} stress_fma_t;

typedef void (*stress_fma_func)(stress_fma_t *fma);

static const stress_help_t help[] = {
	{ NULL,	"fma N",	"start N workers performing floating point multiply-add ops" },
	{ NULL,	"fma-ops N",	"stop after N floating point multiply-add bogo operations" },
	{ NULL,	NULL,		 NULL }
};

static inline double stress_fma_rnd_float(void)
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
		a[i] = a[i] * c + b;
}

static void TARGET_CLONES stress_fma_add132_float(stress_fma_t *fma)
{
	register size_t i;
	register float *a = fma->float_a;
	register float b = fma->float_b;
	register float c = fma->float_c;

PRAGMA_UNROLL_N(FMA_UNROLL)
	for (i = 0; i < FMA_ELEMENTS; i++)
		a[i] = a[i] * c + b;
}

static void TARGET_CLONES stress_fma_add213_double(stress_fma_t *fma)
{
	register size_t i;
	register double *a = fma->double_a;
	register double b = fma->double_b;
	register double c = fma->double_c;

PRAGMA_UNROLL_N(FMA_UNROLL)
	for (i = 0; i < FMA_ELEMENTS; i++)
		a[i] = b * a[i] + c;
}

static void TARGET_CLONES stress_fma_add213_float(stress_fma_t *fma)
{
	register size_t i;
	register float *a = fma->float_a;
	register float b = fma->float_b;
	register float c = fma->float_c;

PRAGMA_UNROLL_N(FMA_UNROLL)
	for (i = 0; i < FMA_ELEMENTS; i++)
		a[i] = b * a[i] + c;
}

static void TARGET_CLONES stress_fma_add231_double(stress_fma_t *fma)
{
	register size_t i;
	register double *a = fma->double_a;
	register double b = fma->double_b;
	register double c = fma->double_c;

PRAGMA_UNROLL_N(FMA_UNROLL)
	for (i = 0; i < FMA_ELEMENTS; i++)
		a[i] = b * c + a[i];
}

static void TARGET_CLONES stress_fma_add231_float(stress_fma_t *fma)
{
	register size_t i;
	register float *a = fma->float_a;
	register float b = fma->float_b;
	register float c = fma->float_c;

PRAGMA_UNROLL_N(FMA_UNROLL)
	for (i = 0; i < FMA_ELEMENTS; i++)
		a[i] = b * c + a[i];
}

stress_fma_func stress_fma_funcs[] = {
	stress_fma_add132_double,
	stress_fma_add132_float,
	stress_fma_add213_double,
	stress_fma_add213_float,
	stress_fma_add231_double,
	stress_fma_add231_float,
};

static inline void stress_fma_init(stress_fma_t *fma)
{
	register size_t i;

	for (i = 0; i < FMA_ELEMENTS; i++) {
		register const float rnd = stress_fma_rnd_float();

		fma->double_init[i] = (double)rnd;
		fma->float_init[i] = rnd;
	}
}

static inline  void stress_fma_reset_a(stress_fma_t *fma)
{
	register size_t i;

	for (i = 0; i < FMA_ELEMENTS; i++)
		fma->double_a[i] = fma->double_init[i];

	for (i = 0; i < FMA_ELEMENTS; i++)
		fma->float_a[i] = fma->float_init[i];
}

static int stress_fma(const stress_args_t *args)
{
	stress_fma_t *fma;
	register size_t idx_b = 0, idx_c = 0;

	fma = (stress_fma_t *)mmap(NULL, sizeof(*fma), PROT_READ | PROT_WRITE,
				MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
	if (fma == MAP_FAILED) {
		pr_inf("%s: failed to mmap %zd bytes for FMA data\n",
			args->name, sizeof(*fma));
		return EXIT_NO_RESOURCE;
	}

	stress_set_proc_state(args->name, STRESS_STATE_RUN);
	stress_fma_init(fma);

	do {
		size_t i;

		stress_fma_reset_a(fma);

		idx_b++;
		if (idx_b >= FMA_ELEMENTS)
			idx_b = 0;
		idx_c += 3;
		if (idx_c >= FMA_ELEMENTS)
			idx_c = 0;

		fma->double_b = fma->double_a[idx_b];
		fma->double_c = fma->double_a[idx_c];

		fma->float_b = fma->float_a[idx_b];
		fma->float_c = fma->float_a[idx_c];

		for (i = 0; i < SIZEOF_ARRAY(stress_fma_funcs); i++)
			stress_fma_funcs[i](fma);
		inc_counter(args);
	} while (keep_stressing(args));

	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);

	(void)munmap((void *)fma, sizeof(*fma));

	return EXIT_SUCCESS;
}

stressor_info_t stress_fma_info = {
	.stressor = stress_fma,
	.class = CLASS_CPU,
	.help = help
};
