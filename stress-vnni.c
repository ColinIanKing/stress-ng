/*
 * Copyright (C) 2023-2025 Colin Ian King.
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
#include "core-bitops.h"
#include "core-builtin.h"
#include "core-cpu.h"
#include "core-put.h"
#include "core-pragma.h"
#include "core-target-clones.h"

#if defined(HAVE_COMPILER_MUSL)
#undef HAVE_IMMINTRIN_H
#endif

#if defined(HAVE_IMMINTRIN_H)
#include <immintrin.h>
#endif

static const stress_help_t help[] = {
	{ NULL,	"vnni N",		"start N workers performing vector neural network ops" },
	{ NULL,	"vnni-intrinsic",	"use x86 intrinsic vnni methods, disable generic methods" },
	{ NULL,	"vnni-method M",	"specify specific vnni methods to exercise" },
	{ NULL,	"vnni-ops N",		"stop after N vnni bogo operations" },
	{ NULL,	NULL,		 NULL }
};

#if (defined(HAVE_COMPILER_GCC) ||	\
     defined(HAVE_COMPILER_CLANG) ||	\
     defined(HAVE_COMPILER_ICX)) &&	\
    !defined(HAVE_COMPILER_ICC)
#define	TARGET_AVXVNNI		__attribute__ ((target("avxvnni")))
#define TARGET_AVX512BW		__attribute__ ((target("avx512bw")))
#define TARGET_AVX512VNNI	__attribute__ ((target("avx512vnni")))
#else
#define TARGET_AVXVNNI
#define TARGET_AVX512BW
#define TARGET_AVX512VNNI
#endif

#define VEC_SIZE_BYTES		(256)

#define VEC_VNNI512_BITS	(512)
#define VEC_VNNI512_BYTES	(VEC_VNNI512_BITS >> 3)
#define VEC_VNNI512_LOOPS	(VEC_SIZE_BYTES / VEC_VNNI512_BYTES)

#define VEC_VNNI256_BITS	(256)
#define VEC_VNNI256_BYTES	(VEC_VNNI256_BITS >> 3)
#define VEC_VNNI256_LOOPS	(VEC_SIZE_BYTES / VEC_VNNI256_BYTES)

#define VEC_VNNI128_BITS	(128)
#define VEC_VNNI128_BYTES	(VEC_VNNI128_BITS >> 3)
#define VEC_VNNI128_LOOPS	(VEC_SIZE_BYTES / VEC_VNNI128_BYTES)

static uint8_t a_init[VEC_SIZE_BYTES] ALIGNED(8);
static uint8_t b_init[VEC_SIZE_BYTES] ALIGNED(8);
static uint8_t c_init[VEC_SIZE_BYTES] ALIGNED(8);
static uint8_t result[VEC_SIZE_BYTES] ALIGNED(8);
static bool avx_capable;
static bool vnni_intrinsic;
static bool little_endian;

typedef void (*stress_vnni_func_t)(stress_args_t *args);
typedef bool (*stress_vnni_capable_func_t)(void);

typedef struct {
	const char 			*name;			/* method name */
	const stress_vnni_func_t	 vnni_func;		/* method function */
	const stress_vnni_capable_func_t vnni_capable_func;	/* capability check */
	const uint32_t			 vnni_checksum_le;	/* little endian */
	const uint32_t			 vnni_checksum_be;	/* big endian */
	const bool			 vnni_intrinsic;	/* uses intrinsics */
} stress_vnni_method_t;

typedef struct {
	stress_metrics_t	metrics;		/* usage metrics */
	bool			vnni_capable;		/* cable of VNNI operations */
} stress_vnni_data_t;

static bool vnni_checksum_okay;

static uint32_t OPTIMIZE3 stress_vnni_checksum(void)
{
	uint32_t sum = 0;
	const uint8_t *ptr = result;
	const uint8_t *end = result + VEC_SIZE_BYTES;

	while (ptr < end) {
		sum += *ptr;
		ptr++;
		sum = shim_rol32(sum);
	}
	return sum;
}

#if defined(STRESS_ARCH_X86_64) &&	\
    defined(HAVE_IMMINTRIN_H) &&	\
    defined(HAVE_MM512_LOADU_SI512) &&	\
    defined(HAVE_MM512_ADD_EPI8) &&	\
    defined(HAVE_MM512_STOREU_SI512)
#define HAVE_STRESS_VNNI_VPADDB512
static void TARGET_AVX512BW OPTIMIZE3 stress_vnni_vpaddb512(stress_args_t *args)
{
	register int i;

	(void)args;

PRAGMA_UNROLL_N(VEC_VNNI512_LOOPS)
	for (i = 0; i < VEC_SIZE_BYTES; i += VEC_VNNI512_BYTES) {
		__m512i a, b, r;

		a = _mm512_loadu_si512((void *)&a_init[i]);
		b = _mm512_loadu_si512((void *)&b_init[i]);
		r = _mm512_add_epi8(a, b);
		_mm512_storeu_si512((void *)&result[i], r);
	}
}
#endif

#if defined(STRESS_ARCH_X86_64) &&	\
    defined(HAVE_IMMINTRIN_H) &&	\
    defined(HAVE_MM256_LOADU_SI256) &&	\
    defined(HAVE_MM256_ADD_EPI8) &&	\
    defined(HAVE_MM256_STOREU_SI256) &&	\
    defined(HAVE_TARGET_CLONES_AVXVNNI)
#define HAVE_STRESS_VNNI_VPADDB256
static void TARGET_AVXVNNI OPTIMIZE3 stress_vnni_vpaddb256(stress_args_t *args)
{
	register int i;

	(void)args;

PRAGMA_UNROLL_N(VEC_VNNI256_LOOPS)
	for (i = 0; i < VEC_SIZE_BYTES; i += VEC_VNNI256_BYTES) {
		__m256i a, b, r;

		a = _mm256_loadu_si256((void *)&a_init[i]);
		b = _mm256_loadu_si256((void *)&b_init[i]);
		r = _mm256_add_epi8(a, b);
		_mm256_storeu_si256((void *)&result[i], r);
	}
}
#endif

#if defined(STRESS_ARCH_X86_64) && 	\
    defined(HAVE_IMMINTRIN_H) &&	\
    defined(HAVE_MM_LOADU_SI128) &&	\
    defined(HAVE_MM_ADD_EPI8) &&	\
    defined(HAVE_MM_STOREU_SI128) &&	\
    defined(HAVE_TARGET_CLONES_AVXVNNI)
#define HAVE_STRESS_VNNI_VPADDB128
static void TARGET_AVXVNNI OPTIMIZE3 stress_vnni_vpaddb128(stress_args_t *args)
{
	register int i;

	(void)args;

PRAGMA_UNROLL_N(VEC_VNNI128_LOOPS)
	for (i = 0; i < VEC_SIZE_BYTES; i += VEC_VNNI128_BYTES) {
		__m128i a, b, r;

		a = _mm_loadu_si128((void *)&a_init[i]);
		b = _mm_loadu_si128((void *)&b_init[i]);
		r = _mm_add_epi8(a, b);
		_mm_storeu_si128((void *)&result[i], r);
	}
}
#endif

static void TARGET_CLONES OPTIMIZE3 stress_vnni_vpaddb(stress_args_t *args)
{
	register int i;

	(void)args;

PRAGMA_UNROLL_N(8)
	for (i = 0; i < VEC_SIZE_BYTES; i++)
		result[i] = (int16_t)(int8_t)a_init[i] + (int16_t)(int8_t)b_init[i];
}

#if defined(STRESS_ARCH_X86_64) &&	\
    defined(HAVE_IMMINTRIN_H) &&	\
    defined(HAVE_MM512_LOADU_SI512) &&	\
    defined(HAVE_MM512_DPBUSD_EPI32) &&	\
    defined(HAVE_MM512_STOREU_SI512)
#define HAVE_STRESS_VNNI_VPDPBUSD512
static void TARGET_AVX512VNNI OPTIMIZE3 stress_vnni_vpdpbusd512(stress_args_t *args)
{
	register int i;

	(void)args;

PRAGMA_UNROLL_N(VEC_VNNI512_LOOPS)
	for (i = 0; i < VEC_SIZE_BYTES; i += VEC_VNNI512_BYTES) {
		__m512i a, b, c, r;

		a = _mm512_loadu_si512((void *)&a_init[i]);
		b = _mm512_loadu_si512((void *)&b_init[i]);
		c = _mm512_loadu_si512((void *)&c_init[i]);
		r = _mm512_dpbusd_epi32(c, a, b);
		_mm512_storeu_si512((void *)&result[i], r);
	}
}
#endif

#if defined(STRESS_ARCH_X86_64) &&	\
    defined(HAVE_IMMINTRIN_H) &&	\
    defined(HAVE_MM256_LOADU_SI256) &&	\
    defined(HAVE_MM256_DPBUSD_EPI32) &&	\
    defined(HAVE_MM256_STOREU_SI256) &&	\
    defined(HAVE_TARGET_CLONES_AVXVNNI)
#define HAVE_STRESS_VNNI_VPDPBUSD256
static void TARGET_AVXVNNI OPTIMIZE3 stress_vnni_vpdpbusd256(stress_args_t *args)
{
	register int i;

	(void)args;

PRAGMA_UNROLL_N(VEC_VNNI256_LOOPS)
	for (i = 0; i < VEC_SIZE_BYTES; i += VEC_VNNI256_BYTES) {
		__m256i a, b, c, r;

		a = _mm256_loadu_si256((void *)&a_init[i]);
		b = _mm256_loadu_si256((void *)&b_init[i]);
		c = _mm256_loadu_si256((void *)&c_init[i]);
		r = _mm256_dpbusd_epi32(c, a, b);
		_mm256_storeu_si256((void *)&result[i], r);
	}
}
#endif

#if defined(STRESS_ARCH_X86_64) &&	\
    defined(HAVE_IMMINTRIN_H) &&	\
    defined(HAVE_MM_LOADU_SI128) &&	\
    defined(HAVE_MM_DPBUSD_EPI32) &&	\
    defined(HAVE_MM_STOREU_SI128) &&	\
    defined(HAVE_TARGET_CLONES_AVXVNNI)
#define HAVE_STRESS_VNNI_VPDPBUSD128
static void TARGET_AVXVNNI OPTIMIZE3 stress_vnni_vpdpbusd128(stress_args_t *args)
{
	register int i;

	(void)args;

PRAGMA_UNROLL_N(VEC_VNNI128_LOOPS)
	for (i = 0; i < VEC_SIZE_BYTES; i += VEC_VNNI128_BYTES) {
		__m128i a, b, c, r;

		a = _mm_loadu_si128((void *)&a_init[i]);
		b = _mm_loadu_si128((void *)&b_init[i]);
		c = _mm_loadu_si128((void *)&c_init[i]);
		r = _mm_dpbusd_epi32(c, a, b);
		_mm_storeu_si128((void *)&result[i], r);
	}
}
#endif

static void TARGET_CLONES OPTIMIZE3 stress_vnni_vpdpbusd(stress_args_t *args)
{
	register int i, j;
	uint32_t *r32 = (uint32_t *)result;
	register const uint32_t *c32 = (uint32_t *)c_init;

	(void)args;

	for (i = 0, j = 0; i < VEC_SIZE_BYTES; i += 4, j++) {
		r32[j] =
			((uint16_t)a_init[i + 0] * (int16_t)(int8_t)b_init[i + 0]) +
			((uint16_t)a_init[i + 1] * (int16_t)(int8_t)b_init[i + 1]) +
			((uint16_t)a_init[i + 2] * (int16_t)(int8_t)b_init[i + 2]) +
			((uint16_t)a_init[i + 3] * (int16_t)(int8_t)b_init[i + 3]) +
			c32[j];
	}
}

#if defined(STRESS_ARCH_X86_64) &&	\
    defined(HAVE_IMMINTRIN_H) &&	\
    defined(HAVE_MM512_LOADU_SI512) &&	\
    defined(HAVE_MM512_DPWSSD_EPI32) &&	\
    defined(HAVE_MM512_STOREU_SI512)
#define HAVE_STRESS_VNNI_VPDPWSSD512
static void TARGET_AVX512VNNI OPTIMIZE3 stress_vnni_vpdpwssd512(stress_args_t *args)
{
	register int i;

	(void)args;

PRAGMA_UNROLL_N(VEC_VNNI512_LOOPS)
	for (i = 0; i < VEC_SIZE_BYTES; i += VEC_VNNI512_BYTES) {
		__m512i a, b, c, r;

		a = _mm512_loadu_si512((void *)&a_init[i]);
		b = _mm512_loadu_si512((void *)&b_init[i]);
		c = _mm512_loadu_si512((void *)&c_init[i]);
		r = _mm512_dpwssd_epi32(c, a, b);
		_mm512_storeu_si512((void *)&result[i], r);
	}
}
#endif

#if defined(STRESS_ARCH_X86_64) &&	\
    defined(HAVE_IMMINTRIN_H) &&	\
    defined(HAVE_MM256_LOADU_SI256) &&	\
    defined(HAVE_MM256_DPWSSD_EPI32) &&	\
    defined(HAVE_MM256_STOREU_SI256) &&	\
    defined(HAVE_TARGET_CLONES_AVXVNNI)
#define HAVE_STRESS_VNNI_VPDPWSSD256
static void TARGET_AVXVNNI OPTIMIZE3 stress_vnni_vpdpwssd256(stress_args_t *args)
{
	register int i;

	(void)args;

PRAGMA_UNROLL_N(VEC_VNNI256_LOOPS)
	for (i = 0; i < VEC_SIZE_BYTES; i += VEC_VNNI256_BYTES) {
		__m256i a, b, c, r;

		a = _mm256_loadu_si256((void *)&a_init[i]);
		b = _mm256_loadu_si256((void *)&b_init[i]);
		c = _mm256_loadu_si256((void *)&c_init[i]);
		r = _mm256_dpwssd_epi32(c, a, b);
		_mm256_storeu_si256((void *)&result[i], r);
	}
}
#endif

#if defined(STRESS_ARCH_X86_64) &&	\
    defined(HAVE_IMMINTRIN_H) &&	\
    defined(HAVE_MM_LOADU_SI128) &&	\
    defined(HAVE_MM_DPWSSD_EPI32) &&	\
    defined(HAVE_MM_STOREU_SI128) &&	\
    defined(HAVE_TARGET_CLONES_AVXVNNI)
#define HAVE_STRESS_VNNI_VPDPWSSD128
static void TARGET_AVXVNNI OPTIMIZE3 stress_vnni_vpdpwssd128(stress_args_t *args)
{
	register int i;

	(void)args;

PRAGMA_UNROLL_N(VEC_VNNI128_LOOPS)
	for (i = 0; i < VEC_SIZE_BYTES; i += VEC_VNNI128_BYTES) {
		__m128i a, b, c, r;

		a = _mm_loadu_si128((void *)&a_init[i]);
		b = _mm_loadu_si128((void *)&b_init[i]);
		c = _mm_loadu_si128((void *)&c_init[i]);
		r = _mm_dpwssd_epi32(c, a, b);
		_mm_storeu_si128((void *)&result[i], r);
	}
}
#endif

static void TARGET_CLONES OPTIMIZE3 stress_vnni_vpdpwssd(stress_args_t *args)
{
	register int i, j;
	const int16_t *a16 = (int16_t *)a_init;
	const int16_t *b16 = (int16_t *)b_init;
	int32_t *r32 = (int32_t *)result;
	register const int32_t *c32 = (int32_t *)c_init;

	(void)args;

	for (i = 0, j = 0; i < VEC_SIZE_BYTES / 2; i += 2, j++) {
		r32[j] =
			((int32_t)a16[i + 0] * (int32_t)b16[i + 0]) +
			((int32_t)a16[i + 1] * (int32_t)b16[i + 1]) +
			c32[j];
	}
}

#if defined(HAVE_STRESS_VNNI_VPADDB512)
static bool stress_avx512_bw_capable(void)
{
	if (stress_cpu_x86_has_avx512_bw()) {
		avx_capable = true;
		return true;
	}
	return false;
}
#endif

#if defined(HAVE_STRESS_VNNI_VPADDB256) ||	\
    defined(HAVE_STRESS_VNNI_VPADDB128) ||	\
    defined(HAVE_STRESS_VNNI_VPDPBUSD256) ||	\
    defined(HAVE_STRESS_VNNI_VPDPBUSD128) ||	\
    defined(HAVE_STRESS_VNNI_VPDPWSSD256) ||	\
    defined(HAVE_STRESS_VNNI_VPDPWSSD128)
static bool stress_avx_vnni_capable(void) {
	if (stress_cpu_x86_has_avx_vnni()) {
		avx_capable = true;
		return true;
	}
	return false;
}
#endif

#if defined(HAVE_STRESS_VNNI_VPDPBUSD512)
static bool stress_avx512_vnni_capable(void) {
	if (stress_cpu_x86_has_avx512_vnni()) {
		avx_capable = true;
		return true;
	}
	return false;
}
#endif

static bool stress_always_capable(void)
{
	return true;
}

static void stress_vnni_all(stress_args_t *args);

static const stress_vnni_method_t stress_vnni_methods[] = {
	{ "all",	 stress_vnni_all,	  stress_always_capable,      0xffffffff, 0xffffffff, false },
#if defined(HAVE_STRESS_VNNI_VPADDB512)
	{ "vpaddb512",	 stress_vnni_vpaddb512,   stress_avx512_bw_capable,   0xd93496ff, 0xd93496ff, true  },
#endif
#if defined(HAVE_STRESS_VNNI_VPADDB256)
	{ "vpaddb256",	 stress_vnni_vpaddb256,   stress_avx_vnni_capable,    0xd93496ff, 0xd93496ff, true  },
#endif
#if defined(HAVE_STRESS_VNNI_VPADDB128)
	{ "vpaddb128",	 stress_vnni_vpaddb128,   stress_avx_vnni_capable,    0xd93496ff, 0xd93496ff, true  },
#endif
	{ "vpaddb",	 stress_vnni_vpaddb,      stress_always_capable,      0xd93496ff, 0xd93496ff, false },
#if defined(HAVE_STRESS_VNNI_VPDPBUSD512)
	{ "vpdpbusd512", stress_vnni_vpdpbusd512, stress_avx512_vnni_capable, 0xc10ef48a, 0x1b509895, true  },
#endif
#if defined(HAVE_STRESS_VNNI_VPDPBUSD256)
	{ "vpdpbusd256", stress_vnni_vpdpbusd256, stress_avx_vnni_capable,    0xc10ef48a, 0x1b509895, true  },
#endif
#if defined(HAVE_STRESS_VNNI_VPDPBUSD128)
	{ "vpdpbusd128", stress_vnni_vpdpbusd128, stress_avx_vnni_capable,    0xc10ef48a, 0x1b509895, true  },
#endif
	{ "vpdpbusd",	 stress_vnni_vpdpbusd,    stress_always_capable,      0xc10ef48a, 0x1b509895, false },
#if defined(HAVE_STRESS_VNNI_VPDPWSSD512)
	{ "vpdpwssd512", stress_vnni_vpdpwssd512, stress_avx512_vnni_capable, 0x8e323fb8, 0xeef5d2a3, true  },
#endif
#if defined(HAVE_STRESS_VNNI_VPDPWSSD256)
	{ "vpdpwssd256", stress_vnni_vpdpwssd256, stress_avx_vnni_capable,    0x8e323fb8, 0xeef5d2a3, true  },
#endif
#if defined(HAVE_STRESS_VNNI_VPDPWSSD128)
	{ "vpdpwssd128", stress_vnni_vpdpwssd128, stress_avx_vnni_capable,    0x8e323fb8, 0xeef5d2a3, true  },
#endif
	{ "vpdpwssd",	 stress_vnni_vpdpwssd,    stress_always_capable,      0x8e323fb8, 0xeef5d2a3, false },
};

static stress_vnni_data_t stress_vnni_data[SIZEOF_ARRAY(stress_vnni_methods)];

static void OPTIMIZE3 stress_vnni_exercise(stress_args_t *args, const size_t n)
{
	uint32_t checksum, expected_checksum;
	const stress_vnni_method_t * const method = &stress_vnni_methods[n];
	stress_vnni_data_t *data = &stress_vnni_data[n];
	register int j;
	register const stress_vnni_func_t func = method->vnni_func;
	double t;

	if (vnni_intrinsic && !method->vnni_intrinsic)
		return;

	t = stress_time_now();
	for (j = 0; j < 1024; j++) {
		func(args);
	}
	data->metrics.duration += stress_time_now() - t;
	data->metrics.count += (double)j;
	/* and checksum the last computation */
	checksum = stress_vnni_checksum();
	expected_checksum = little_endian ? method->vnni_checksum_le : method->vnni_checksum_be;
	if (UNLIKELY(checksum != expected_checksum)) {
		pr_fail("%s: checksum mismatch for %s, got %" PRIx32 ", expected %" PRIx32 "\n",
			args->name, method->name, checksum, expected_checksum);
		vnni_checksum_okay = false;
	}
	stress_bogo_inc(args);
}

static void stress_vnni_all(stress_args_t *args)
{
	size_t i;

	for (i = 1; LIKELY(stress_continue(args) && (i < SIZEOF_ARRAY(stress_vnni_methods))); i++) {
		if (LIKELY(stress_vnni_data[i].vnni_capable)) {
			stress_vnni_exercise(args, i);
		}
	}
}

/*
 *  stress_vnni()
 *	stress intel VNNI ops
 */
static int stress_vnni(stress_args_t *args)
{
	size_t i, j, vnni_method = 0, intrinsic_count = 0;

	stress_catch_sigill();

	vnni_checksum_okay = true;
	little_endian = stress_little_endian();

	stress_mwc_set_seed(0x172fb3ea, 0xd9c02f73);
	stress_uint8rnd4((uint8_t *)&a_init, sizeof(a_init));
	stress_uint8rnd4((uint8_t *)&b_init, sizeof(b_init));
	stress_uint8rnd4((uint8_t *)&c_init, sizeof(c_init));

	vnni_intrinsic = false;
	(void)stress_get_setting("vnni-method", &vnni_method);
	(void)stress_get_setting("vnni-intrinsic", &vnni_intrinsic);

	avx_capable = false;
	for (i = 0; i < SIZEOF_ARRAY(stress_vnni_methods); i++) {
		stress_vnni_data[i].vnni_capable = stress_vnni_methods[i].vnni_capable_func();

		/* Keep count of capable intrinsic functions */
		if ((stress_vnni_data[i].vnni_capable) && (stress_vnni_methods[i].vnni_intrinsic))
			intrinsic_count++;
	}

	if (!stress_vnni_data[vnni_method].vnni_capable) {
		if (stress_instance_zero(args)) {
			pr_inf_skip("%s: vnni method '%s' not available for this processor model, "
				"skipping stressor\n",
				args->name, stress_vnni_methods[vnni_method].name);
		}
		return EXIT_NO_RESOURCE;
	}

	if (vnni_intrinsic &&
	    ((intrinsic_count == 0) ||
	    ((vnni_method != 0) /* all */ && !stress_vnni_methods[vnni_method].vnni_intrinsic))) {
		pr_inf_skip("%s: no vector neural network instructions available "
			"and --vmmi-intrinsic selected, skipping stressor\n",
			args->name);
		return EXIT_NO_RESOURCE;
	}

	if ((!avx_capable) && (stress_instance_zero(args))) {
		pr_inf("%s: no vector neural network instructions available, using generic optimized versions\n",
			args->name);
	}

	stress_set_proc_state(args->name, STRESS_STATE_SYNC_WAIT);
	stress_sync_start_wait(args);
	stress_set_proc_state(args->name, STRESS_STATE_RUN);

	do {
		if (UNLIKELY(vnni_method)) {
			stress_vnni_exercise(args, vnni_method);
		} else {
			stress_vnni_all(args);
		}
	} while (vnni_checksum_okay && stress_continue(args));

	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);

	for (i = 1, j = 0; i < SIZEOF_ARRAY(stress_vnni_methods); i++) {
		if (stress_vnni_data[i].vnni_capable &&
		    (stress_vnni_data[i].metrics.count > 0.0)) {
			char buf[64];
			double rate = stress_vnni_data[i].metrics.duration > 0.0 ?
				stress_vnni_data[i].metrics.count / stress_vnni_data[i].metrics.duration : 0.0;

			(void)snprintf(buf, sizeof(buf), "%s ops per sec", stress_vnni_methods[i].name);
			stress_metrics_set(args, j, buf,
				rate, STRESS_METRIC_HARMONIC_MEAN);
			j++;
		}
	}

	return vnni_checksum_okay ? EXIT_SUCCESS : EXIT_FAILURE;
}

static const char *stress_vnni_method(const size_t i)
{
	return (i < SIZEOF_ARRAY(stress_vnni_methods)) ? stress_vnni_methods[i].name : NULL;
}

static const stress_opt_t opts[] = {
	{ OPT_vnni_intrinsic, "vnni-intrinsic", TYPE_ID_BOOL, 0, 1, NULL },
	{ OPT_vnni_method,    "vnni-method",    TYPE_ID_SIZE_T_METHOD, 0, 0, stress_vnni_method },
	END_OPT,
};

const stressor_info_t stress_vnni_info = {
	.stressor = stress_vnni,
	.classifier = CLASS_CPU | CLASS_INTEGER | CLASS_COMPUTE | CLASS_VECTOR,
	.opts = opts,
	.verify = VERIFY_ALWAYS,
	.help = help
};
