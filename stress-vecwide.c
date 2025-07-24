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
#include "core-mmap.h"
#include "core-pragma.h"
#include "core-put.h"
#include "core-target-clones.h"
#include "core-vecmath.h"

/* Workaround GCC 9.4.0 target clones build failure issue */
#if EQUAL_GNUC(9,4,0)
#undef TARGET_CLONES
#define TARGET_CLONES
#endif

#define VERY_WIDE	(0)

static const stress_help_t help[] = {
	{ NULL,	"vecwide N",	 "start N workers performing vector math ops" },
	{ NULL,	"vecwide-ops N", "stop after N vector math bogo operations" },
	{ NULL,	NULL,		 NULL }
};

#if defined(HAVE_VECMATH)

/*
 *  8 bit vectors, named by int8wN where N = number of bits width
 */
#if VERY_WIDE
typedef int8_t stress_vint8w8192_t	__attribute__ ((vector_size(8192 / 8)));
typedef int8_t stress_vint8w4096_t	__attribute__ ((vector_size(4096 / 8)));
#endif
typedef int8_t stress_vint8w2048_t	__attribute__ ((vector_size(2048 / 8)));
typedef int8_t stress_vint8w1024_t	__attribute__ ((vector_size(1024 / 8)));
typedef int8_t stress_vint8w512_t	__attribute__ ((vector_size(512 / 8)));
typedef int8_t stress_vint8w256_t	__attribute__ ((vector_size(256 / 8)));
typedef int8_t stress_vint8w128_t	__attribute__ ((vector_size(128 / 8)));
typedef int8_t stress_vint8w64_t	__attribute__ ((vector_size(64 / 8)));
typedef int8_t stress_vint8w32_t	__attribute__ ((vector_size(32 / 8)));
#if VERY_SMALL
typedef int8_t stress_vint8w16_t	__attribute__ ((vector_size(16 / 8)));
#endif

#if VERY_WIDE
#define VEC_MAX_SZ	sizeof(stress_vint8w8192_t)
#else
#define VEC_MAX_SZ	sizeof(stress_vint8w2048_t)
#endif

typedef struct {
	uint8_t	a[VEC_MAX_SZ];
	uint8_t	b[VEC_MAX_SZ];
	uint8_t	c[VEC_MAX_SZ];
	uint8_t	s[VEC_MAX_SZ];
	uint8_t v23[VEC_MAX_SZ];
	uint8_t v3[VEC_MAX_SZ];
	uint8_t	res1[VEC_MAX_SZ];
	uint8_t	res2[VEC_MAX_SZ];
	uint8_t *res;	/* pointer to res1 and/or res2 */
} vec_args_t;

typedef void (*stress_vecwide_func_t)(vec_args_t *vec_args);

typedef struct {
	const stress_vecwide_func_t vecwide_func;
	const size_t byte_size;
} stress_vecwide_funcs_t;


#define STRESS_VECWIDE(name, type)				\
static void TARGET_CLONES OPTIMIZE3 name (vec_args_t *vec_args) \
{								\
	type ALIGN64 a;						\
	type ALIGN64 b;						\
	type ALIGN64 c;						\
	type ALIGN64 s;						\
	type ALIGN64 v23;					\
	type ALIGN64 v3;					\
	type ALIGN64 res;					\
	register int i;						\
								\
	(void)shim_memcpy(&a, vec_args->a, sizeof(a));		\
	(void)shim_memcpy(&b, vec_args->b, sizeof(b));		\
	(void)shim_memcpy(&c, vec_args->c, sizeof(c));		\
	(void)shim_memcpy(&s, vec_args->s, sizeof(s));		\
	(void)shim_memcpy(&v23, vec_args->v23, sizeof(s));	\
	(void)shim_memcpy(&v3, vec_args->v23, sizeof(s));	\
								\
PRAGMA_UNROLL_N(8)						\
	for (i = 0; i < 2048; i++) {				\
		a += b;						\
		b -= c;						\
		c += v3;					\
		s ^= b;						\
		a += v23;					\
		b *= v3;					\
		a *= s;						\
	}							\
								\
	res = a + b + c;					\
	(void)shim_memcpy(vec_args->res, &res, sizeof(res));	\
								\
PRAGMA_UNROLL							\
	for (i = 0; i < (int)sizeof(res); i++) {		\
		stress_uint8_put((uint8_t)res[i]);		\
	}							\
}

#if VERY_WIDE
STRESS_VECWIDE(stress_vecwide_8192, stress_vint8w8192_t)
STRESS_VECWIDE(stress_vecwide_4096, stress_vint8w4096_t)
#endif
STRESS_VECWIDE(stress_vecwide_2048, stress_vint8w2048_t)
STRESS_VECWIDE(stress_vecwide_1024, stress_vint8w1024_t)
STRESS_VECWIDE(stress_vecwide_512, stress_vint8w512_t)
STRESS_VECWIDE(stress_vecwide_256, stress_vint8w256_t)
STRESS_VECWIDE(stress_vecwide_128, stress_vint8w128_t)
STRESS_VECWIDE(stress_vecwide_64, stress_vint8w64_t)
STRESS_VECWIDE(stress_vecwide_32, stress_vint8w32_t)
#if VERY_SMALL
STRESS_VECWIDE(stress_vecwide_16, stress_vint8w16_t)
#endif

static const stress_vecwide_funcs_t stress_vecwide_funcs[] = {
#if VERY_WIDE
	{ stress_vecwide_8192, sizeof(stress_vint8w8192_t) },
	{ stress_vecwide_4096, sizeof(stress_vint8w4096_t) },
#endif
	{ stress_vecwide_2048, sizeof(stress_vint8w2048_t) },
	{ stress_vecwide_1024, sizeof(stress_vint8w1024_t) },
	{ stress_vecwide_512,  sizeof(stress_vint8w512_t)  },
	{ stress_vecwide_256,  sizeof(stress_vint8w256_t)  },
	{ stress_vecwide_128,  sizeof(stress_vint8w128_t)  },
	{ stress_vecwide_64,   sizeof(stress_vint8w64_t)   },
	{ stress_vecwide_32,   sizeof(stress_vint8w32_t)   },
#if VERY_SMALL
	{ stress_vecwide_16,   sizeof(stress_vint8w16_t)   },
#endif
};

static stress_metrics_t stress_vecwide_metrics[SIZEOF_ARRAY(stress_vecwide_funcs)];

static int stress_vecwide(stress_args_t *args)
{
	static vec_args_t *vec_args;
	size_t i;
	double total_duration = 0.0;
	size_t total_bytes = 0;
	const size_t vec_args_size = (sizeof(*vec_args) + args->page_size - 1) & ~(args->page_size - 1);
	const bool verify = !!(g_opt_flags & OPT_FLAGS_VERIFY);
	int rc = EXIT_SUCCESS;

	stress_catch_sigill();

	vec_args = (vec_args_t *)stress_mmap_populate(NULL, vec_args_size,
					PROT_READ | PROT_WRITE,
					MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
	if (vec_args == MAP_FAILED) {
		pr_inf_skip("%s: failed to mmap %zu byte vector%s "
			"errno=%d (%s), skipping stressor\n",
			args->name, vec_args_size,
			stress_get_memfree_str(), errno, strerror(errno));
		return EXIT_NO_RESOURCE;
	}
	stress_set_vma_anon_name(vec_args, vec_args_size, "vec-args");

	for (i = 0; i < SIZEOF_ARRAY(stress_vecwide_funcs); i++) {
		stress_vecwide_metrics[i].duration = 0.0;
		stress_vecwide_metrics[i].count = 0.0;
	}

	for (i = 0; i < SIZEOF_ARRAY(vec_args->a); i++) {
		vec_args->a[i] = (uint8_t)i;
		vec_args->b[i] = stress_mwc8();
		vec_args->c[i] = stress_mwc8();
		vec_args->s[i] = stress_mwc8();
	}

	(void)shim_memset(&vec_args->v23, 23, sizeof(vec_args->v23));
	(void)shim_memset(&vec_args->v3, 3, sizeof(vec_args->v3));

	stress_set_proc_state(args->name, STRESS_STATE_SYNC_WAIT);
	stress_sync_start_wait(args);
	stress_set_proc_state(args->name, STRESS_STATE_RUN);

	do {
		for (i = 0; i < SIZEOF_ARRAY(stress_vecwide_funcs); i++) {
			double t1, t2, dt;

			vec_args->res = vec_args->res1;
			t1 = stress_time_now();
			stress_vecwide_funcs[i].vecwide_func(vec_args);
			t2 = stress_time_now();
			dt = (t2 - t1);

			total_duration += dt;
			stress_vecwide_metrics[i].duration += dt;
			stress_vecwide_metrics[i].count += 1.0;
			stress_bogo_inc(args);

			if (verify) {
				vec_args->res = vec_args->res2;
				t1 = stress_time_now();
				stress_vecwide_funcs[i].vecwide_func(vec_args);
				t2 = stress_time_now();
				dt = (t2 - t1);

				total_duration += dt;
				stress_vecwide_metrics[i].duration += dt;
				stress_vecwide_metrics[i].count += 1.0;
				stress_bogo_inc(args);

				if (shim_memcmp(vec_args->res1, vec_args->res2, sizeof(vec_args->res1))) {
					pr_fail("%s: data difference between identical vector computations\n", args->name);
					rc = EXIT_FAILURE;
					break;
				}
			}
		}
	} while ((rc == EXIT_SUCCESS) && stress_continue(args));

	for (i = 0; i < SIZEOF_ARRAY(stress_vecwide_funcs); i++) {
		total_bytes += stress_vecwide_funcs[i].byte_size;
	}

	if (stress_instance_zero(args)) {
		pr_block_begin();
		pr_dbg("%s: Bits  %% Dur  %% Exp (x Win) (> 1.0 is better than expected)\n", args->name);
		for (i = 0; i < SIZEOF_ARRAY(stress_vecwide_funcs); i++) {
			double dur_pc, exp_pc, win;

			dur_pc = stress_vecwide_metrics[i].duration / total_duration * 100.0;
			exp_pc = (double)stress_vecwide_funcs[i].byte_size / (double)total_bytes * 100.0;
			win    = exp_pc / dur_pc;

			pr_dbg("%s: %5zd %5.2f%% %5.2f%% %5.2f\n",
				args->name, 8 * stress_vecwide_funcs[i].byte_size,
				dur_pc, exp_pc, win);
		}
		pr_dbg("%s: Key: Bits = vector width in bits, Dur = %% total run time,\n", args->name);
		pr_dbg("%s       Exp = %% expected run time, Win = performance gain\n", args->name);
		pr_block_end();
	}

	for (i = 0; i < SIZEOF_ARRAY(stress_vecwide_funcs); i++) {
		char str[64];
		const double rate = (stress_vecwide_metrics[i].duration > 0) ?
				stress_vecwide_metrics[i].count / stress_vecwide_metrics[i].duration : 0.0;

		(void)snprintf(str, sizeof(str), "vecwide%zd ops per sec", stress_vecwide_funcs[i].byte_size * 8);
		stress_metrics_set(args, i, str,
			rate, STRESS_METRIC_HARMONIC_MEAN);
	}

	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);

	(void)munmap((void *)vec_args, vec_args_size);

	return rc;
}

const stressor_info_t stress_vecwide_info = {
	.stressor = stress_vecwide,
	.classifier = CLASS_CPU | CLASS_INTEGER | CLASS_COMPUTE | CLASS_VECTOR,
	.verify = VERIFY_OPTIONAL,
	.help = help
};
#else
const stressor_info_t stress_vecwide_info = {
	.stressor = stress_unimplemented,
	.classifier = CLASS_CPU | CLASS_INTEGER | CLASS_COMPUTE | CLASS_VECTOR,
	.verify = VERIFY_OPTIONAL,
	.help = help,
	.unimplemented_reason = "built without compiler support for vector data/operations"
};
#endif
