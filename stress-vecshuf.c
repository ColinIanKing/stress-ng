/*
 * Copyright (C) 2022-2025 Colin Ian King
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

#define VECTOR_SIZE_BYTES	(64)
#define LOOPS_PER_CALL		(65536)
#define SHUFFLES_PER_LOOP	(4)

static const stress_help_t help[] = {
	{ NULL,	"vecshuf N",		"start N workers performing vector shuffle ops" },
	{ NULL,	"vecshuf-method M",	"select vector shuffling method" },
	{ NULL,	"vecshuf-ops N",	"stop after N vector shuffle bogo operations" },
	{ NULL,	NULL,			NULL }
};

#if defined(HAVE_VECMATH)

/* define vector types with unions to arrays of base type */
#define VEC_TYPE_T(type, tag, elements)							\
typedef union {										\
	type  v	 ALIGNED(256) __attribute__ ((vector_size(elements * sizeof(type))));	\
	type  i[elements] ALIGNED(256);							\
} stress_vec_ ## tag ## _ ## elements ## _t;						\
											\
typedef type stress_scalar_ ## tag ## _t;

VEC_TYPE_T(uint8_t,  u8,     64)
VEC_TYPE_T(uint16_t, u16,    32)
VEC_TYPE_T(uint32_t, u32,    16)
VEC_TYPE_T(uint64_t, u64,     8)
#if defined(HAVE_INT128_T)
VEC_TYPE_T(__uint128_t, u128, 4)
#endif

/*
 *  various 64 byte vectors and shuffle masks
 *	s = vector being shuffled
 *	o = vector of original data to sanity check shuffle
 *	mask1 = vector of shuffling positions
 *	mask2 = vector to shuffle positions to shuffle back to
 *		original position
 */
typedef struct {
	/* 64 byte vectors */
	struct {
		stress_vec_u8_64_t s;
		stress_vec_u8_64_t o;
		stress_vec_u8_64_t mask1;
		stress_vec_u8_64_t mask2;
	} u8_64;
	struct {
		stress_vec_u16_32_t s;
		stress_vec_u16_32_t o;
		stress_vec_u16_32_t mask1;
		stress_vec_u16_32_t mask2;
	} u16_32;
	struct {
		stress_vec_u32_16_t s;
		stress_vec_u32_16_t o;
		stress_vec_u32_16_t mask1;
		stress_vec_u32_16_t mask2;
	} u32_16;
	struct {
		stress_vec_u64_8_t s;
		stress_vec_u64_8_t o;
		stress_vec_u64_8_t mask1;
		stress_vec_u64_8_t mask2;
	} u64_8;
#if defined(HAVE_INT128_T)
	struct {
		stress_vec_u128_4_t s;
		stress_vec_u128_4_t o;
		stress_vec_u128_4_t mask1;
		stress_vec_u128_4_t mask2;
	} u128_4;
#endif
} stress_vec_data_t;

typedef double (*stress_vecshuf_func_t)(
	stress_args_t *args,
	stress_vec_data_t *vec_data);

static double stress_vecshuf_all(
	stress_args_t *args,
	stress_vec_data_t *vec_data);

#if defined(HAVE_BUILTIN_SHUFFLE)
#define SHIM_SHUFFLE_u8_64(dst, src, mask)	dst = __builtin_shuffle(src, mask)
#define SHIM_SHUFFLE_u16_32(dst, src, mask)	dst = __builtin_shuffle(src, mask)
#define SHIM_SHUFFLE_u32_16(dst, src, mask)	dst = __builtin_shuffle(src, mask)
#define SHIM_SHUFFLE_u64_8(dst, src, mask)	dst = __builtin_shuffle(src, mask)
#if defined(HAVE_INT128_T)
#define SHIM_SHUFFLE_u128_4(dst, src, mask)	dst = __builtin_shuffle(src, mask)
#endif
#else

#define STRESS_VEC_BUILTIN_SHUFFLE(tag, elements)		\
static inline ALWAYS_INLINE void shim_builtin_shuffle_ ## tag ## _ ## elements(\
	stress_scalar_ ## tag ## _t *dst,			\
	stress_scalar_ ## tag ## _t *src,			\
	stress_scalar_ ## tag ## _t *mask)			\
{								\
	register int i;						\
								\
PRAGMA_UNROLL_N(elements)					\
	for (i = 0; i < elements; i++) {			\
		const stress_scalar_ ## tag ## _t m = mask[i];	\
		dst[i] = src[m];				\
	}							\
}

#define SHIM_SHUFFLE_u8_64(dst, src, mask)	\
	shim_builtin_shuffle_u8_64((void *)&(dst), (void *)&(src), (void *)&(mask))
STRESS_VEC_BUILTIN_SHUFFLE(u8, 64)

#define SHIM_SHUFFLE_u16_32(dst, src, mask) \
	shim_builtin_shuffle_u16_32((void *)&(dst), (void *)&(src), (void *)&(mask))
STRESS_VEC_BUILTIN_SHUFFLE(u16, 32)

#define SHIM_SHUFFLE_u32_16(dst, src, mask) \
	shim_builtin_shuffle_u32_16((void *)&(dst), (void *)&(src), (void *)&(mask))
STRESS_VEC_BUILTIN_SHUFFLE(u32, 16)

#define SHIM_SHUFFLE_u64_8(dst, src, mask) \
	shim_builtin_shuffle_u64_8((void *)&(dst), (void *)&(src), (void *)&(mask))
STRESS_VEC_BUILTIN_SHUFFLE(u64, 8)
#if defined(HAVE_INT128_T)
STRESS_VEC_BUILTIN_SHUFFLE(u128, 4)
#define SHIM_SHUFFLE_u128_4(dst, src, mask) \
	shim_builtin_shuffle_u128_4((void *)&(dst), (void *)&(src), (void *)&(mask))
#endif
#endif

#define STRESS_VEC_SHUFFLE(tag, elements, opt)				\
static double TARGET_CLONES opt stress_vecshuf_ ## tag ## _ ## elements (	\
	stress_args_t *args,						\
	stress_vec_data_t *data)					\
{									\
	stress_vec_ ## tag ## _ ## elements ## _t *RESTRICT s;		\
	stress_vec_ ## tag ## _ ## elements ## _t *RESTRICT mask1;	\
	stress_vec_ ## tag ## _ ## elements ## _t *RESTRICT mask2;	\
	double t1, t2;							\
	register int i;							\
									\
	s = &data->tag ## _ ## elements.s;				\
	mask1 = &data->tag ## _ ## elements.mask1;			\
	mask2 = &data->tag ## _ ## elements.mask2;			\
									\
	t1 = stress_time_now();						\
PRAGMA_UNROLL_N(4)							\
	for (i = 0; i < LOOPS_PER_CALL; i++) {				\
		stress_vec_ ## tag ## _ ## elements ## _t tmp;		\
									\
		SHIM_SHUFFLE_ ## tag ## _ ## elements (tmp.v, s->v, mask1->v);\
		SHIM_SHUFFLE_ ## tag ## _ ## elements (s->v, tmp.v, mask2->v);\
	}								\
	t2 = stress_time_now();						\
									\
	stress_bogo_inc(args);						\
	return t2 - t1;							\
}

#if defined(STRESS_ARCH_X86)
/*
 *  Force optimization levels 1 for non-64 bit
 *  shuffles on x86 to workaround gcc/clang
 *  store-to-load forwarding optimization issue
 */
STRESS_VEC_SHUFFLE(u8,   64, OPTIMIZE2)
STRESS_VEC_SHUFFLE(u16,  32, OPTIMIZE1)
STRESS_VEC_SHUFFLE(u32,  16, OPTIMIZE1)
STRESS_VEC_SHUFFLE(u64,   8, OPTIMIZE1)
#if defined(HAVE_INT128_T)
STRESS_VEC_SHUFFLE(u128,  4, OPTIMIZE1)
#endif
#else
STRESS_VEC_SHUFFLE(u8,   64, OPTIMIZE2)
STRESS_VEC_SHUFFLE(u16,  32, OPTIMIZE2)
STRESS_VEC_SHUFFLE(u32,  16, OPTIMIZE2)
STRESS_VEC_SHUFFLE(u64,   8, OPTIMIZE2)
#if defined(HAVE_INT128_T)
STRESS_VEC_SHUFFLE(u128,  4, OPTIMIZE2)
#endif
#endif

typedef struct {
	const char *name;
	const stress_vecshuf_func_t vecshuf_func;
	const size_t elements;
} stress_vecshuf_funcs_t;

typedef struct {
	stress_metrics_t metrics;
	double bytes;
} stress_vecshuf_data_t;

static const stress_vecshuf_funcs_t stress_vecshuf_funcs[] = {
	{ "all",	stress_vecshuf_all,     0 },
	{ "u8x64",	stress_vecshuf_u8_64,  64 },
	{ "u16x32",	stress_vecshuf_u16_32, 32 },
	{ "u32x16",	stress_vecshuf_u32_16, 16 },
	{ "u64x8",	stress_vecshuf_u64_8,   8 },
#if defined(HAVE_INT128_T)
	{ "u128x4",	stress_vecshuf_u128_4,  4 },
#endif
};

static stress_vecshuf_data_t stress_vecshuf_data[SIZEOF_ARRAY(stress_vecshuf_funcs)];

static void stress_vecshuf_call_method(
	stress_args_t *args,
	stress_vec_data_t *data,
	const size_t method)
{
	double dt, ops, bytes;
	const stress_vecshuf_funcs_t *const func = &stress_vecshuf_funcs[method];
	stress_vecshuf_data_t *vecshuf_data = &stress_vecshuf_data[method];

	dt = func->vecshuf_func(args, data);
	vecshuf_data->metrics.duration += dt;

	ops = (double)(LOOPS_PER_CALL * func->elements) * SHUFFLES_PER_LOOP;
	vecshuf_data->metrics.count += ops;

	bytes = (double)(LOOPS_PER_CALL * VECTOR_SIZE_BYTES) * SHUFFLES_PER_LOOP;
	vecshuf_data->bytes += bytes;
}

static double stress_vecshuf_all(
	stress_args_t *args,
	stress_vec_data_t *data)
{
	size_t i;

	for (i = 1; i < SIZEOF_ARRAY(stress_vecshuf_funcs); i++) {
		stress_vecshuf_call_method(args, data, i);
	}
	return 0.0;
}

#define VEC_SET_DATA(type, elements, mwc)			\
do {								\
	for (i = 0; i < elements; i++) {			\
		data->type ## _ ## elements.s.i[i] = mwc();	\
		data->type ## _ ## elements.o.i[i] = 		\
			data->type ## _ ## elements.s.i[i];	\
	}							\
} while (0)

#if defined(HAVE_INT128_T)
static inline __uint128_t vec_mwc128(void)
{
	return ((__uint128_t)stress_mwc64() << 64) |
		(__uint128_t)stress_mwc64();
}
#endif

/*
 *  stress_vecshuf_set_data()
 *	set random data, initial value r and shuffled data s
 */
static void stress_vecshuf_set_data(stress_vec_data_t *data)
{
	register size_t i;

	VEC_SET_DATA(u8,   64, stress_mwc8);
	VEC_SET_DATA(u16,  32, stress_mwc16);
	VEC_SET_DATA(u32,  16, stress_mwc32);
	VEC_SET_DATA(u64,   8, stress_mwc64);
#if defined(HAVE_INT128_T)
	VEC_SET_DATA(u128,  4, vec_mwc128);
#endif

}

#define VEC_SET_MASK(type, elements)					\
do {									\
	s = (stress_mwc8() & ((elements >> 1) - 1)) + 1;		\
	for (i = 0; i < elements; i++) {				\
		data->type ## _ ## elements.mask1.i[i] = (i + s) & (elements - 1);	\
		data->type ## _ ## elements.mask2.i[i] = (i - s) & (elements - 1);	\
	}								\
} while (0)

/*
 *  stress_vecshuf_set_mask()
 *	set shuffle mask to shuffle vector of N elements to
 *	a random position of position x -> (x + 1..(N / 2)) % N
 *	and back again
 */
static void stress_vecshuf_set_mask(stress_vec_data_t *data)
{
	register size_t i;
	uint8_t s;

	VEC_SET_MASK(u8,  64);
	VEC_SET_MASK(u16, 32);
	VEC_SET_MASK(u32, 16);
	VEC_SET_MASK(u64,  8);
#if defined(HAVE_INT128_T)
	VEC_SET_MASK(u128, 4);
#endif
}

#define VEC_CHECK(type, elements, fail)					\
do {									\
	for (i = 0; i < elements; i++) {				\
		if (UNLIKELY(data-> type ## _ ## elements.s.i[i] !=	\
			     data-> type ## _ ## elements.o.i[i])) {	\
			pr_fail("%s: shuffling error, in "		\
				# type "x" # elements			\
				"vector\n", args->name);		\
			fail = true;					\
			break;						\
		}							\
	}								\
} while (0)

static bool stress_vecshuf_check_data(
	stress_args_t *args,
	const stress_vec_data_t *data)
{
	register size_t i;
	bool fail = false;

	VEC_CHECK(u8,  64, fail);
	VEC_CHECK(u16, 32, fail);
	VEC_CHECK(u32, 16, fail);
	VEC_CHECK(u64,  8, fail);
#if defined(HAVE_INT128_T)
	VEC_CHECK(u128, 4, fail);
#endif
	return fail;
}

static int stress_vecshuf(stress_args_t *args)
{
	stress_vec_data_t *data;
	size_t vecshuf_method = 0;	/* "all" */
	size_t i;
	int rc = EXIT_SUCCESS;

	stress_catch_sigill();

	data = (stress_vec_data_t *)stress_mmap_populate(NULL, sizeof(*data),
			PROT_READ | PROT_WRITE,
			MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if (data == MAP_FAILED) {
		pr_inf_skip("%s: failed to mmap %zu bytes for vectors%s, "
			"errno=%d (%s), skipping stressor\n",
			args->name, sizeof(*data),
			stress_get_memfree_str(), errno, strerror(errno));
		return EXIT_NO_RESOURCE;
	}
	stress_set_vma_anon_name(data, sizeof(*data), "vecshuf-data");

	for (i = 1; i < SIZEOF_ARRAY(stress_vecshuf_data); i++) {
		stress_vecshuf_data[i].metrics.duration = 0.0;
		stress_vecshuf_data[i].metrics.count = 0.0;
		stress_vecshuf_data[i].bytes = 0.0;
	}

	(void)stress_get_setting("vecshuf-method", &vecshuf_method);

	stress_vecshuf_set_data(data);

	stress_set_proc_state(args->name, STRESS_STATE_SYNC_WAIT);
	stress_sync_start_wait(args);
	stress_set_proc_state(args->name, STRESS_STATE_RUN);

	do {
		stress_vecshuf_set_mask(data);
		stress_vecshuf_call_method(args, data, vecshuf_method);
		if (UNLIKELY(stress_vecshuf_check_data(args, data))) {
			rc = EXIT_FAILURE;
			break;
		}
	} while (stress_continue(args));

	if (stress_instance_zero(args)) {
		double total_duration = 0.0;
		double inverse_sum_ops = 0.0;
		double inverse_sum_bytes = 0.0;
		double n = 0.0;

		pr_block_begin();
		pr_dbg("%s: shuffle throughput for just stressor instance 0:\n", args->name);
		pr_dbg("%s: %14.14s %13.13s %13.13s %13.13s\n",
			args->name, "Method", "MB/sec", "Mshuffles/sec", "% exec time");

		for (i = 1; i < SIZEOF_ARRAY(stress_vecshuf_funcs); i++) {
			total_duration += stress_vecshuf_data[i].metrics.duration;
		}

		for (i = 1; i < SIZEOF_ARRAY(stress_vecshuf_funcs); i++) {
			const double ops = stress_vecshuf_data[i].metrics.count;
			const double duration = stress_vecshuf_data[i].metrics.duration;
			const double bytes = stress_vecshuf_data[i].bytes;

			if ((duration > 0.0) && (ops > 0.0) &&
			    (bytes > 0.0) && (total_duration > 0.0)) {
				const double ops_rate = (ops / duration) / 1000000.0;
				const double bytes_rate = (bytes / duration) / (1.0 * MB);

				inverse_sum_ops += 1.0 / ops_rate;
				inverse_sum_bytes += 1.0 / bytes_rate;
				n += 1.0;

				pr_dbg("%s: %14.14s %13.3f %13.3f %13.3f\n", args->name,
					stress_vecshuf_funcs[i].name, bytes_rate, ops_rate,
					100.0 * duration / total_duration);
			}
		}

		if (total_duration > 0.0) {
			double ops_rate, bytes_rate;

			ops_rate = n / inverse_sum_ops;
			bytes_rate = n / inverse_sum_bytes;
			pr_dbg("%s: %14.14s %13.3f %13.3f\n", args->name,
				"Harmonic Mean:", bytes_rate, ops_rate);
		}
		pr_block_end();
	}

	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);

	(void)munmap((void *)data, sizeof(*data));

	return rc;
}

static const char *stress_vecshuf_method(const size_t i)
{
	return (i < SIZEOF_ARRAY(stress_vecshuf_funcs)) ? stress_vecshuf_funcs[i].name : NULL;
}

static const stress_opt_t opts[] = {
        { OPT_vecshuf_method, "vecshuf-method", TYPE_ID_SIZE_T_METHOD, 0, 0, stress_vecshuf_method },
	END_OPT,
};

const stressor_info_t stress_vecshuf_info = {
	.stressor = stress_vecshuf,
	.classifier = CLASS_CPU | CLASS_INTEGER | CLASS_COMPUTE | CLASS_VECTOR,
	.opts = opts,
	.verify = VERIFY_ALWAYS,
	.help = help
};
#else

static const stress_opt_t opts[] = {
        { OPT_vecshuf_method, "vecshuf_method", TYPE_ID_SIZE_T_METHOD, 0, 0, stress_unimplemented_method },
	END_OPT,
};

const stressor_info_t stress_vecshuf_info = {
	.stressor = stress_unimplemented,
	.classifier = CLASS_CPU | CLASS_INTEGER | CLASS_COMPUTE | CLASS_VECTOR,
	.opts = opts,
	.verify = VERIFY_ALWAYS,
	.help = help,
	.unimplemented_reason = "built without compiler support for vector shuffling operations"
};
#endif
