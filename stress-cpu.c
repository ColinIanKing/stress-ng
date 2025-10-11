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
#include "core-arch.h"
#include "core-attribute.h"
#include "core-bitops.h"
#include "core-builtin.h"
#include "core-cpu.h"
#include "core-net.h"
#include "core-pragma.h"
#include "core-put.h"
#include "core-target-clones.h"

#include <math.h>
#include <sched.h>
#include <time.h>

#if defined(HAVE_SYS_SELECT_H)
#include <sys/select.h>
#endif

#if defined(HAVE_COMPLEX_H)
#include <complex.h>
#endif

/*
 *  S390x on QEMU (and maybe H/W) trips SIGILL for decimal
 *  math with some compilers, so disable this for now
 */
#if defined(STRESS_ARCH_S390)
#undef HAVE_Decimal32
#undef HAVE_Decimal64
#undef HAVE_Decimal128
#endif

#define GAMMA 		(0.57721566490153286060651209008240243104215933593992L)
#define OMEGA		(0.56714329040978387299996866221035554975381578718651L)
#define PSI		(3.35988566624317755317201130291892717968890513373197L)
#define PI		(3.14159265358979323846264338327950288419716939937511L)

#define STATS_MAX		(250)
#define FFT_SIZE		(4096)
#define STRESS_CPU_DITHER_X	(1024)
#define STRESS_CPU_DITHER_Y	(768)
#define MATRIX_PROD_SIZE 	(128)
#define CORRELATE_DATA_LEN	(8192)
#define CORRELATE_LEN		(CORRELATE_DATA_LEN / 16)
#define SIEVE_SIZE              (104730)

/* do nothing */
#if defined(HAVE_ASM_NOP)
#define FORCE_DO_NOTHING() 	stress_asm_nop()
#elif defined(HAVE_ASM_NOTHING)
#define FORCE_DO_NOTHING() 	stress_asm_nothing()
#else
#define FORCE_DO_NOTHING() 	while (0)
#endif

#if defined(HAVE_INT128_T)
#define STRESS_UINT128(hi, lo)	((((__uint128_t)hi << 64) | (__uint128_t)lo))
#endif

/*
 *  the CPU stress test has different classes of cpu stressor
 */
typedef int (*stress_cpu_func)(const char *name);

typedef struct {
	const char		*name;	/* human readable form of stressor */
	const stress_cpu_func	func;	/* the cpu method function */
	const double bogo_op_rate;	/* normalizing bogo-ops rate */
} stress_cpu_method_info_t;

static const stress_help_t help[] = {
	{ "c N", "cpu N",		"start N workers that perform CPU only loading" },
	{ "l P", "cpu-load P",		"load CPU by P %, 0=sleep, 100=full load (see -c)" },
	{ NULL,	 "cpu-load-slice S",	"specify time slice during busy load" },
	{ NULL,  "cpu-method M",	"specify stress cpu method M, default is all" },
	{ NULL,	 "cpu-old-metrics",	"use old CPU metrics instead of normalized metrics" },
	{ NULL,  "cpu-ops N",		"stop after N cpu bogo operations" },
	{ NULL,	 NULL,			NULL }
};

static const stress_cpu_method_info_t stress_cpu_methods[];

/* Don't make this static to ensure dithering does not get optimised out */
uint8_t pixels[STRESS_CPU_DITHER_X][STRESS_CPU_DITHER_Y];

/*
 *  stress_cpu_sqrt()
 *	stress CPU on square roots
 */
static int TARGET_CLONES stress_cpu_sqrt(const char *name)
{
	int i;

	for (i = 0; i < 16384; i++) {
		const uint64_t rnd = stress_mwc32();
		double r_d = shim_sqrt((double)rnd) * shim_sqrt((double)rnd);
		long double r_ld = shim_sqrtl((long double)rnd) * shim_sqrtl((long double)rnd);
		register uint64_t tmp;

		r_d = shim_rint(r_d);
		tmp = (uint64_t)r_d;
		if (UNLIKELY((g_opt_flags & OPT_FLAGS_VERIFY) && (tmp != rnd))) {
			pr_fail("%s: sqrt error detected on "
				"sqrt(%" PRIu64 ")\n", name, rnd);
			return EXIT_FAILURE;
		}

		r_ld = shim_rintl(r_ld);
		tmp = (uint64_t)r_ld;
		if (UNLIKELY((g_opt_flags & OPT_FLAGS_VERIFY) && (tmp != rnd))) {
			pr_fail("%s: sqrtf error detected on "
				"sqrt(%" PRIu64 ")\n", name, rnd);
			return EXIT_FAILURE;
		}
	}
	return EXIT_SUCCESS;
}

static bool stress_is_affinity_set(void)
{
#if defined(HAVE_SCHED_GETAFFINITY)
	cpu_set_t mask;
	int i;
	const int cpus_online = (int)stress_get_processors_online();

	CPU_ZERO(&mask);
	if (sched_getaffinity(0, sizeof(mask), &mask) < 0)
		return false;	/* Can't tell, so assume not */

	/*
	 * If any of the CPU affinities across all the CPUs
	 * are zero then we know the stressor as been pinned
	 * to some CPUs and not to others, so affinity has been
	 * set which can lead to load balancing difficulties
	 */
	for (i = 0; i < cpus_online; i++) {
		if (!CPU_ISSET(i, &mask))
			return true;
	}
	return false;
#else
	return false;	/* Don't know, so assume not */
#endif
}

/*
 *  stress_cpu_loop()
 *	simple CPU busy loop
 */
static int OPTIMIZE0 stress_cpu_loop(const char *name)
{
	uint32_t i, i_sum = 0;
	const uint32_t sum = 134209536UL;

	for (i = 0; LIKELY(i < 16384); i++) {
		i_sum += i;
		FORCE_DO_NOTHING();
	}
	if ((g_opt_flags & OPT_FLAGS_VERIFY) && (i_sum != sum)) {
		pr_fail("%s: cpu loop 0..16383 sum was %" PRIu32 " and "
			"did not match the expected value of %" PRIu32 "\n",
			name, i_sum, sum);
		return EXIT_FAILURE;
	}
	return EXIT_SUCCESS;
}

/*
 *  stress_cpu_gcd()
 *	compute Greatest Common Divisor
 */
static int OPTIMIZE3 TARGET_CLONES stress_cpu_gcd(const char *name)
{
	uint32_t i, gcd_sum = 0;
	const uint32_t gcd_checksum = 63000868UL;
	uint64_t lcm_sum = 0;
	const uint64_t lcm_checksum = 41637399273ULL;

	for (i = 0; i < 16384; i++) {
		register uint32_t a = i, b = i % (3 + (1997 ^ i));
		register uint64_t lcm = ((uint64_t)a * b);

		while (b != 0) {
			register const uint32_t r = b;

			b = a % b;
			a = r;
		}
		if (a)
			lcm_sum += (lcm / a);
		gcd_sum += a;
		FORCE_DO_NOTHING();
	}
	if ((g_opt_flags & OPT_FLAGS_VERIFY) &&
	    (gcd_sum != gcd_checksum) &&
	    (lcm_sum != lcm_checksum)) {
		pr_fail("%s: gcd error detected, failed modulo "
			"or assignment operations\n", name);
		return EXIT_FAILURE;
	}
	return EXIT_SUCCESS;
}

/*
 *  stress_cpu_bitops()
 *	various bit manipulation hacks from bithacks
 *	https://graphics.stanford.edu/~seander/bithacks.html
 */
static int OPTIMIZE3 TARGET_CLONES stress_cpu_bitops(const char *name)
{
	uint32_t i, i_sum = 0;
	const uint32_t sum = 0x8aac4aab;

	for (i = 0; i < 16384; i++) {
		i_sum += stress_bitreverse32(i);
		i_sum += stress_parity32(i);
		i_sum += stress_popcount32(i);
		i_sum += stress_nextpwr2(i);
	}
	if ((g_opt_flags & OPT_FLAGS_VERIFY) && (i_sum != sum)) {
		pr_fail("%s: bitops error detected, failed "
			"bitops operations\n", name);
		return EXIT_FAILURE;
	}
	return EXIT_SUCCESS;
}

/*
 *  stress_cpu_trig()
 *	simple sin, cos trig functions
 */
static int OPTIMIZE_FAST_MATH stress_cpu_trig(const char *name)
{
	int i;
	long double d_sum = 0.0L;

	(void)name;

	for (i = 0; i < 1500; i++) {
		const long double theta = (2.0L * PI * (long double)i) / 1500.0L;
		{
			const double thetad = (double)theta;
			const float thetaf = (float)theta;

			d_sum += (shim_cosl(theta) * shim_sinl(theta));
			d_sum += ((long double)shim_cos(thetad) * (long double)shim_sin(thetad));
			d_sum += ((long double)shim_cosf(thetaf) * (long double)shim_sinf(thetaf));
		}
		{
			const long double thetal = theta * 2.0L;
			const double thetad = (double)thetal;
			const float thetaf = (float)thetal;

			d_sum += shim_cosl(thetal);
			d_sum += (long double)shim_cos(thetad);
			d_sum += (long double)shim_cosf(thetaf);
		}
		{
			const long double thetal = theta * 3.0L;
			const double thetad = (double)thetal;
			const float thetaf = (float)thetal;

			d_sum += shim_sinl(thetal);
			d_sum += (long double)shim_sin(thetad);
			d_sum += (long double)shim_sinf(thetaf);
		}
	}
	stress_long_double_put(d_sum);
	return EXIT_SUCCESS;
}

/*
 *  stress_cpu_hyperbolic()
 *	simple hyperbolic sinh, cosh functions
 */
static int OPTIMIZE_FAST_MATH stress_cpu_hyperbolic(const char *name)
{
	int i;
	long double d_sum = 0.0L;

	(void)name;

	for (i = 0; i < 1500; i++) {
		const long double theta = (2.0L * PI * (long double)i) / 1500.0L;
		{
			const double thetad = (double)theta;
			const float thetaf = (float)theta;

			d_sum += (shim_coshl(theta) * shim_sinhl(theta));
			d_sum += ((long double)cosh(thetad) * (long double)shim_sinh(thetad));
			d_sum += ((long double)coshf(thetaf) * (long double)shim_sinhf(thetaf));
		}
		{
			const long double thetal = theta * 2.0L;
			const double thetad = (double)theta;
			const float thetaf = (float)theta;

			d_sum += shim_coshl(thetal);
			d_sum += (long double)shim_cosh(thetad);
			d_sum += (long double)shim_coshf(thetaf);
		}
		{
			const long double thetal = theta * 3.0L;
			const double thetad = (double)theta;
			const float thetaf = (float)theta;

			d_sum += shim_sinhl(thetal);
			d_sum += (long double)shim_sinh(thetad);
			d_sum += (long double)shim_sinhf(thetaf);
		}
	}
	stress_long_double_put(d_sum);
	return EXIT_SUCCESS;
}

/*
 *  stress_cpu_rand()
 *	generate lots of pseudo-random integers
 */
static int OPTIMIZE3 stress_cpu_rand(const char *name)
{
	int i;
	uint32_t i_sum = 0;
	const uint32_t sum = 0xc253698c;

	stress_mwc_default_seed();
PRAGMA_UNROLL_N(8)
	for (i = 0; LIKELY(i < 16384); i++)
		i_sum += stress_mwc32();

	if ((g_opt_flags & OPT_FLAGS_VERIFY) && (i_sum != sum)) {
		pr_fail("%s: rand error detected, failed sum of "
			"pseudo-random values\n", name);
		return EXIT_FAILURE;
	}
	return EXIT_SUCCESS;
}

/*
 *  stress_cpu_logmap()
 *	Compute logistic map of x = r * x * (x - 1.0) where r is the
 * 	accumulation point based on A098587. Data is scaled in the
 *	range 0..255
 */
static int OPTIMIZE3 stress_cpu_logmap(const char *name)
{
	static double x = 0.4;
	/*
	 * Use an accumulation point that is slightly larger
	 * than the point where chaotic behaviour starts
	 */
	const double r = 3.569945671870944901842L * 1.0999999L;
	register int i;

	(void)name;

PRAGMA_UNROLL_N(8)
	for (i = 0; i < 16384; i++) {
		/*
		 *  Scale up a fractional part of x
		 *  by an arbitrary value and take
		 *  the bottom 8 bits of the result
		 *  to make a quasi-chaotic random-ish
		 *  value
		 */
		x = x * r * (1.0 - x);
	}
	stress_double_put(x);
	return EXIT_SUCCESS;
}

#if defined(HAVE_SRAND48) &&	\
    defined(HAVE_LRAND48) &&	\
    defined(HAVE_DRAND48)

/*
 * Some libc's such as OpenBSD don't implement rand48 the same
 * as linux's glibc, so beware of different implementations with
 * the verify mode checking.
 */
#if defined(__linux__) && 	\
    defined(__GLIBC__)
#define STRESS_CPU_RAND48_VERIFY
#endif
/*
 *  stress_cpu_rand48()
 *	generate random values using rand48 family of functions
 */
static int OPTIMIZE3 stress_cpu_rand48(const char *name)
{
	int i;
	double d = 0;
	long long int l = 0;
#if defined(STRESS_CPU_RAND48_VERIFY)
	const double d_expected_sum = 8184.618041L;
	const long long int l_expected_sum = 17522760427916;
#endif

	(void)name;

	srand48(0x0defaced);
	for (i = 0; LIKELY(i < 16384); i++) {
		d += drand48();
		l += lrand48();
	}

#if defined(STRESS_CPU_RAND48_VERIFY)
	if (g_opt_flags & OPT_FLAGS_VERIFY) {
		double d_error = d - d_expected_sum;
		if (shim_fabs(d_error) > 0.0001) {
			pr_fail("%s: drand48 error detected, failed sum\n", name);
			return EXIT_FAILURE;
		}
		if (l != l_expected_sum) {
			pr_fail("%s: lrand48 error detected, failed sum\n", name);
			return EXIT_FAILURE;
		}
	}
#endif

	stress_double_put(d);
	stress_uint64_put((uint64_t)l);
	return EXIT_SUCCESS;
}
#endif

/*
 *  stress_cpu_lfsr32()
 *	generate 16384 values from the Galois polynomial
 *	x^32 + x^31 + x^29 + x + 1
 */
static int OPTIMIZE3 stress_cpu_lfsr32(const char *name)
{
        static uint32_t lfsr = 0xf63acb01;
	register int i;

	(void)name;

PRAGMA_UNROLL_N(8)
	for (i = 0; LIKELY(i < 16384); i++) {
		lfsr = (lfsr >> 1) ^ (unsigned int)(-(lfsr & 1u) & 0xd0000001U);
	}
	stress_uint32_put(lfsr);
	return EXIT_SUCCESS;
}

/*
 *  stress_cpu_nsqrt()
 *	iterative Newton-Raphson square root
 */
static int OPTIMIZE3 TARGET_CLONES stress_cpu_nsqrt(const char *name)
{
	int i;
	const long double precision = 1.0e-12L;
	const int max_iter = 56;

	for (i = 16300; i < 16384; i++) {
		const long double n = (long double)i;
		long double lo = (n < 1.0L) ? n : 1.0L;
		long double hi = (n < 1.0L) ? 1.0L : n;
		long double rt;
		int j = 0;

		while ((j++ < max_iter) && ((hi - lo) > precision)) {
			const long double g = (lo + hi) / 2.0L;
			if ((g * g) > n)
				hi = g;
			else
				lo = g;
		}
		rt = (lo + hi) / 2.0L;

		if (g_opt_flags & OPT_FLAGS_VERIFY) {
			const long double r2 = shim_rintl(rt * rt);

			if (j >= max_iter) {
				pr_fail("%s: Newton-Raphson sqrt "
					"computation took more iterations "
					"than expected\n", name);
				return EXIT_FAILURE;
			}
			if ((int)r2 != i) {
				pr_fail("%s: Newton-Raphson sqrt not "
					"accurate enough\n", name);
				return EXIT_FAILURE;
			}
		}
	}
	return EXIT_SUCCESS;
}

/*
 *  stress_cpu_phi()
 *	compute the Golden Ratio
 */
static int OPTIMIZE3 TARGET_CLONES stress_cpu_phi(const char *name)
{
	long double phi; /* Golden ratio */
	const long double precision = 1.0e-15L;
	const long double phi_ = (1.0L + shim_sqrtl(5.0L)) / 2.0L;
	register uint64_t a, b;
	const uint64_t mask = 1ULL << 63;
	int i;

	/* Pick any two starting points */
	a = stress_mwc64modn(99);
	b = stress_mwc64modn(99);

	/* Iterate until we approach overflow */
	for (i = 0; (i < 64) && !((a | b) & mask); i++) {
		/* Find nth term */
		register const uint64_t c = a + b;

		a = b;
		b = c;
	}
	/* And we have the golden ratio */
	phi = (long double)b / (long double)a;

	if ((g_opt_flags & OPT_FLAGS_VERIFY) &&
	    (shim_fabsl(phi - phi_) > precision)) {
		pr_fail("%s: Golden Ratio phi not accurate enough\n",
			name);
		return EXIT_FAILURE;
	}
	return EXIT_SUCCESS;
}

/*
 *  stress_cpu_apery()
 *      compute Apéry's constant
 */
static int OPTIMIZE3 stress_cpu_apery(const char *name)
{
	uint32_t n;
	long double a = 0.0L, a_ = a;
	const long double precision = 1.0e-14L;

	(void)name;

	for (n = 1; LIKELY(n < 100000); n++) {
		register long double n3 = (long double)n;

		a_ = a;
		n3 = n3 * n3 * n3;
		a += (1.0L / n3);
		if (shim_fabsl(a - a_) < precision)
			break;
	}
	if (shim_fabsl(a - a_) > precision) {
		pr_fail("%s: Apéry's constant not accurate enough\n", name);
		return EXIT_FAILURE;
	}
	return EXIT_SUCCESS;
}

#if defined(HAVE_COMPLEX_H) &&		\
    defined(HAVE_COMPLEX) &&		\
    defined(__STDC_IEC_559_COMPLEX__) &&\
    !defined(__UCLIBC__)
/*
 *  fft_partial()
 *  	partial Fast Fourier Transform
 */
static void OPTIMIZE3 fft_partial(
	double complex * RESTRICT data,
	double complex * RESTRICT tmp,
	const int n,
	const int m)
{
	if (m < n) {
		const int m2 = m * 2;
		int i;

		fft_partial(tmp, data, n, m2);
		fft_partial(tmp + m, data + m, n, m2);
		for (i = 0; i < n; i += m2) {
			const double complex negI = -(double complex)I;
			register double complex v = tmp[i];
			register double complex t =
				shim_cexp((negI * (double)PI * (double)i) /
				     (double)n) * tmp[i + m];
			data[i / 2] = v + t;
			data[(i + n) / 2] = v - t;
		}
	}
}

/*
 *  stress_cpu_fft()
 *	Fast Fourier Transform
 */
static int TARGET_CLONES stress_cpu_fft(const char *name)
{
	static double complex buf[FFT_SIZE] ALIGN64, tmp[FFT_SIZE] ALIGN64;
	int i;

	(void)name;

	for (i = 0; i < FFT_SIZE; i++)
		buf[i] = (double complex)(i % 63);

	(void)shim_memcpy(tmp, buf, sizeof(*tmp) * FFT_SIZE);
	fft_partial(buf, tmp, FFT_SIZE, 1);
	return EXIT_SUCCESS;
}
#else
	UNEXPECTED
#endif

/*
 *   stress_cpu_euler()
 *	compute e using series
 */
static int OPTIMIZE3 TARGET_CLONES stress_cpu_euler(const char *name)
{
	long double e = 1.0L, last_e;
	long double fact = 1.0L;
	const long double precision = 1.0e-20L;
	int n = 1;

	do {
		last_e = e;
		fact *= n;
		n++;
		e += (1.0L / fact);
	} while ((n < 25) && (shim_fabsl(e - last_e) > precision));

	if ((g_opt_flags & OPT_FLAGS_VERIFY) && (n >= 25)) {
		pr_fail("%s: Euler computation took more iterations "
			"than expected\n", name);
		return EXIT_FAILURE;
	}
	return EXIT_SUCCESS;
}

/*
 *  random_buffer()
 *	fill a uint8_t buffer full of random data
 *	buffer *must* be multiple of 4 bytes in size
 */
static void random_buffer(uint8_t *data, const size_t len)
{
	size_t i;

	for (i = 0; i < len / 4; i++) {
		register uint32_t v = stress_mwc32();

		*data++ = (uint8_t)v;
		v >>= 8;
		*data++ = (uint8_t)v;
		v >>= 8;
		*data++ = (uint8_t)v;
		v >>= 8;
		*data++ = (uint8_t)v;
	}
}

/*
 *  stress_cpu_collatz()
 *	stress test integer collatz conjecture
 */
static int OPTIMIZE3 TARGET_CLONES stress_cpu_collatz(const char *name)
{
	register uint64_t n = 989345275647ULL;	/* Has 1348 steps in cycle */
	register uint64_t s = stress_mwc8();
	register int i;

	/*
	 *  We need to put in the accumulation of s to force the compiler
	 *  to generate code that does the following computation at run time.
	 */
	for (i = 0; n != 1; i++) {
		n = (n & 1) ? (3 * n) + 1 : n / 2;
		s += n;		/* Force compiler to do iterative computations */
	}
	stress_uint64_put(s);
	if ((g_opt_flags & OPT_FLAGS_VERIFY) && (i != 1348)) {
		pr_fail("%s: error detected, failed collatz progression\n",
			name);
		return EXIT_FAILURE;
	}
	return EXIT_SUCCESS;
}

/*
 *  stress_cpu_idct()
 *	compute 8x8 Inverse Discrete Cosine Transform
 */
static int OPTIMIZE3 TARGET_CLONES stress_cpu_idct(const char *name)
{
#define IDCT_SIZE	(8)

	const double invsqrt2 = 1.0 / shim_sqrt(2.0);
	const double pi_over_16 = (double)PI / 16.0;
	int i, j, u, v;
	float data[IDCT_SIZE][IDCT_SIZE];
	float idct[IDCT_SIZE][IDCT_SIZE];

	/*
	 *  Set up DCT
	 */
	for (i = 0; i < IDCT_SIZE; i++) {
PRAGMA_UNROLL_N(8)
		for (j = 0; j < IDCT_SIZE; j++) {
			data[i][j] = (i + j == 0) ? 2040: 0;
		}
	}
	for (i = 0; i < IDCT_SIZE; i++) {
		const double pi_i = (i + i + 1) * pi_over_16;

		for (j = 0; j < IDCT_SIZE; j++) {
			const double pi_j = (j + j + 1) * pi_over_16;
			double sum = 0.0;

			for (u = 0; u < IDCT_SIZE; u++) {
				const double cos_pi_i_u = shim_cos(pi_i * u);
				const double tmp = cos_pi_i_u * (u ? 1.0 : invsqrt2);

				for (v = 0; v < IDCT_SIZE; v++) {
					const double cos_pi_j_v = shim_cos(pi_j * v);

					sum += (double)data[u][v] *
						(v ? 1.0 : invsqrt2) * cos_pi_j_v * tmp;
				}
			}
			idct[i][j] = (float)(0.25 * sum);
		}
	}
	/* Final output should be a 8x8 matrix of values 255 */
	if (g_opt_flags & OPT_FLAGS_VERIFY) {
		for (i = 0; i < IDCT_SIZE; i++) {
			for (j = 0; j < IDCT_SIZE; j++) {
				if ((int)idct[i][j] != 255) {
					pr_fail("%s: IDCT error detected, "
						"IDCT[%d][%d] was %d, "
						"expecting 255\n",
						name, i, j, (int)idct[i][j]);
					return EXIT_FAILURE;
				}
			}
			if (UNLIKELY(!stress_continue_flag()))
				return EXIT_SUCCESS;
		}
	}

#undef IDCT_SIZE
	return EXIT_SUCCESS;
}

#define int_ops(type, a, b, c1, c2, c3)	\
	do {				\
		a += b;			\
		b ^= a;			\
		a >>= 1;		\
		b <<= 2;		\
		b -= a;			\
		a ^= (type)~0;		\
		b ^= ~(c1);		\
		a *= 3;			\
		b *= 7;			\
		a += 2;			\
		b -= 3;			\
		a /= 77;		\
		b /= 3;			\
		a <<= 1;		\
		b <<= 2;		\
		a |= 1;			\
		b |= 3;			\
		a *= stress_mwc32();	\
		b ^= stress_mwc32();	\
		a += stress_mwc32();	\
		b -= stress_mwc32();	\
		a /= 7;			\
		b /= 9;			\
		a |= (c2);		\
		b &= (c3);		\
	} while (0);

#define C1 	(0xf0f0f0f0f0f0f0f0ULL)
#define C2	(0x1000100010001000ULL)
#define C3	(0xffeffffefebefffeULL)

/*
 *  Generic int stressor macro
 */
#define stress_cpu_int(type, sz, int_a, int_b, int_c1, int_c2, int_c3)	\
static int OPTIMIZE3 TARGET_CLONES stress_cpu_int ## sz(const char *name)\
{								\
	const type mask = (type)~(type)0;			\
	const type a_final = int_a;				\
	const type b_final = int_b;				\
	const type c1 = int_c1 & mask;				\
	const type c2 = int_c2 & mask;				\
	const type c3 = int_c3 & mask;				\
	register type a, b;					\
	int i;							\
								\
	stress_mwc_default_seed();				\
	a = (type)stress_mwc32();				\
	b = (type)stress_mwc32();				\
								\
	for (i = 0; i < 1000; i++) {				\
		int_ops(type, a, b, c1, c2, c3)			\
	}							\
								\
	if ((g_opt_flags & OPT_FLAGS_VERIFY) &&			\
	    ((a != a_final) || (b != b_final)))	{		\
		pr_fail("%s: int" # sz " error detected, " 	\
			"failed int" # sz 			\
			" math operations\n", name);		\
		return EXIT_FAILURE;				\
	}							\
	return EXIT_SUCCESS;					\
}								\

/* For compilers that support int128 .. */
#if defined(HAVE_INT128_T)

stress_cpu_int(__uint128_t, 128,
	STRESS_UINT128(0x132af604d8b9183a,0x5e3af8fa7a663d74),
	STRESS_UINT128(0x62f086e6160e4e,0xd84c9f800365858),
	STRESS_UINT128(C1, C1), STRESS_UINT128(C2, C2), STRESS_UINT128(C3, C3))
#endif

stress_cpu_int(uint64_t, 64, \
	0x013f7f6dc1d79197cULL, 0x01863d2c6969a51ceULL,
	C1, C2, C3)

stress_cpu_int(uint32_t, 32, \
	0x1ce9b547UL, 0xa24b33aUL,
	C1, C2, C3)

stress_cpu_int(uint16_t, 16, \
	0x1871, 0x07f0,
	C1, C2, C3)

stress_cpu_int(uint8_t, 8, \
	0x12, 0x1a,
	C1, C2, C3)

#define float_thresh(x, type)	x = (type)		\
	((shim_fabs((double)x) > 1.0) ?	\
	((type)(0.1 + (double)x - (double)(long)x)) :	\
	((type)(x)))

#define float_ops(type, a, b, c, d, f_sin, f_cos)	\
	do {						\
		a = a + b;				\
		b = a * c;				\
		c = a - b;				\
		d = a / (type)8.1;			\
		float_thresh(d, type);			\
		a = c / (type)5.1923;			\
		float_thresh(a, type);			\
		float_thresh(c, type);			\
		b = c + a;				\
		c = b * (type)f_sin(b);			\
		d = d + b + (type)f_sin(a);		\
		a = (type)f_cos(b + c);			\
		b = b * c;				\
		c = c + (type)1.5;			\
		d = d - (type)f_sin(c);			\
		a = a * (type)f_cos(b);			\
		b = b + (type)f_cos(c);			\
		c = (type)f_sin(a + b) / (type)2.344;	\
		b = d - (type)0.5;			\
	} while (0)

/*
 *  Generic floating point stressor macro
 */
#define stress_cpu_fp(type, fp_name, f_sin, f_cos)	\
static int OPTIMIZE3 TARGET_CLONES stress_cpu_ ## fp_name(const char *name)\
{							\
	int i;						\
	const uint32_t r1 = stress_mwc32(),		\
		       r2 = stress_mwc32();		\
	type a = (type)0.18728L, 			\
	     b = (type)((double)r1 / 65536.0),		\
	     c = (type)((double)r2 / 65536.0),		\
	     d = (type)0.0,				\
	     r;						\
							\
	(void)name;					\
							\
	for (i = 0; i < 1000; i++) {			\
		float_ops(type, a, b, c, d,		\
			f_sin, f_cos);			\
	}						\
	r = a + b + c + d;				\
	stress_double_put((double)r);			\
	return EXIT_SUCCESS;				\
}

stress_cpu_fp(float, float, shim_sinf, shim_cosf)
stress_cpu_fp(double, double, shim_sin, shim_cos)
stress_cpu_fp(long double, longdouble, shim_sinl, shim_cosl)
#if defined(HAVE_Decimal32) &&	\
    !defined(HAVE_COMPILER_CLANG)
stress_cpu_fp(_Decimal32, decimal32, shim_sinf, shim_cosf)
#endif
#if defined(HAVE_Decimal64) &&	\
    !defined(HAVE_COMPILER_CLANG)
stress_cpu_fp(_Decimal64, decimal64, shim_sin, shim_cos)
#endif
#if defined(HAVE_Decimal128) &&	\
    !defined(HAVE_COMPILER_CLANG)
stress_cpu_fp(_Decimal128, decimal128, shim_sinl, shim_cosl)
#endif
#if defined(HAVE_fp16) &&	\
    !defined(HAVE_COMPILER_CLANG)
stress_cpu_fp(__fp16, float16, shim_sin, shim_cos)
#endif
#if defined(HAVE_Float32) &&	\
    !defined(HAVE_COMPILER_CLANG)
stress_cpu_fp(_Float32, float32, shim_sin, shim_cos)
#endif
#if defined(HAVE_Float64) &&	\
    !defined(HAVE_COMPILER_CLANG)
stress_cpu_fp(_Float64, float64, shim_sin, shim_cos)
#endif
#if defined(HAVE__float80) &&	\
    !defined(HAVE_COMPILER_CLANG)
stress_cpu_fp(__float80, float80, shim_sinl, shim_cosl)
#endif
#if defined(HAVE__float128) &&	\
    !defined(HAVE_COMPILER_CLANG)
stress_cpu_fp(__float128, float128, shim_sinl, shim_cosl)
#elif defined(HAVE_Float128) &&	\
    !defined(HAVE_COMPILER_CLANG)
stress_cpu_fp(_Float128, float128, shim_sinl, shim_cosl)
#endif

/* Append floating point literal specifier to literal value */
#define FP(val, ltype)	val ## ltype

#if defined(HAVE_COMPLEX_H) &&		\
    defined(HAVE_COMPLEX) &&		\
    defined(__STDC_IEC_559_COMPLEX__) &&\
    !defined(__UCLIBC__)

static inline void stress_cpu_complex_float_put(complex float v)
{
	stress_float_put((float)(v * v));
}

static inline void stress_cpu_complex_double_put(complex double v)
{
	stress_double_put((double)(v * v));
}

static inline void stress_cpu_complex_long_double_put(complex long double v)
{
	stress_long_double_put((long double)(v * v));
}

/*
 *  Generic complex stressor macro
 */
#define stress_cpu_complex(type, ltype, c_name, f_csin, f_ccos, f_put)	\
static int OPTIMIZE3 TARGET_CLONES stress_cpu_ ## c_name(const char *name) \
{								\
	int i;							\
	const uint32_t r1 = stress_mwc32(),			\
		       r2 = stress_mwc32();			\
	type cI = (type)I;					\
	type a = FP(0.18728, ltype) + 				\
		cI * FP(0.2762, ltype),				\
		b = (type)((double)r1/(double)(1UL<<31)) - cI * FP(0.11121, ltype),	\
		c = (type)((double)r2/(double)(1UL<<31)) + cI * stress_mwc32(),	\
		d = (type)0.5,					\
		r;						\
								\
	(void)name;						\
								\
	for (i = 0; i < 1000; i++) {				\
		float_ops(type, a, b, c, d, f_csin, f_ccos);	\
	}							\
	r = a + b + c + d;					\
	f_put(r);						\
	return EXIT_SUCCESS;					\
}

stress_cpu_complex(complex float, f, complex_float, shim_csinf, shim_ccosf, stress_cpu_complex_float_put)
stress_cpu_complex(complex double, , complex_double, shim_csin, shim_ccos, stress_cpu_complex_double_put)
stress_cpu_complex(complex long double, l, complex_long_double, shim_csinl, shim_ccosl, stress_cpu_complex_long_double_put)
#endif

#define int_float_ops(ftype, flt_a, flt_b, flt_c, flt_d,	\
	f_sin, f_cos, inttype, int_a, int_b, 			\
	int_c1, int_c2, int_c3)					\
								\
	do {							\
		int_a += int_b;					\
		int_b ^= int_a;					\
		flt_a = flt_a + flt_b;				\
		int_a >>= 1;					\
		int_b <<= 2;					\
		flt_b = flt_a * flt_c;				\
		int_b -= int_a;					\
		int_a ^= ~(inttype)0;				\
		flt_c = flt_a - flt_b;				\
		int_b ^= ~(int_c1);				\
		int_a *= 3;					\
		flt_d = flt_a / flt_b;				\
		int_b *= 7;					\
		int_a += 2;					\
		flt_a = flt_c / (ftype)0.1923L;			\
		int_b -= 3;					\
		int_a /= 77;					\
		flt_b = flt_c + flt_a;				\
		int_b /= 3;					\
		int_a <<= 1;					\
		flt_c = flt_b * (ftype)3.12L;			\
		int_b <<= 2;					\
		int_a |= 1;					\
		flt_d = flt_d + flt_b + (ftype)f_sin(flt_a);	\
		int_b |= 3;					\
		int_a *= stress_mwc32();			\
		flt_a = (flt_b + flt_c) / flt_c;		\
		int_b ^= stress_mwc32();			\
		int_a += stress_mwc32();			\
		flt_b = flt_b * flt_c;				\
		int_b -= stress_mwc32();			\
		int_a /= 7;					\
		flt_c = flt_c + (ftype)1.0L;			\
		int_b /= 9;					\
		flt_d = flt_d - (ftype)f_sin(flt_c);		\
		int_a |= (int_c2);				\
		flt_a = flt_a * (ftype)f_cos(flt_b);		\
		flt_b = flt_b + (ftype)f_cos(flt_c);		\
		int_b &= (int_c3);				\
		flt_c = (ftype)f_sin(flt_a + flt_b) / (ftype)2.344L;	\
		flt_b = flt_d - (ftype)1.0L;			\
	} while (0)

/*
 *  Generic integer and floating point stressor macro
 */
#define stress_cpu_int_fp(inttype, sz, ftype, fp_name, 		\
	int_a, int_b, int_c1, int_c2, int_c3, f_sinf, f_cosf)	\
static int OPTIMIZE3 TARGET_CLONES stress_cpu_int ## sz ## _ ## fp_name(const char *name)\
{								\
	int i;							\
	inttype a, b;						\
	const inttype mask = (inttype)~0;			\
	const inttype a_final = int_a;				\
	const inttype b_final = int_b;				\
	const inttype c1 = int_c1 & mask;			\
	const inttype c2 = int_c2 & mask;			\
	const inttype c3 = int_c3 & mask;			\
	const uint32_t r1 = stress_mwc32(),			\
		       r2 = stress_mwc32();			\
	ftype flt_a = (ftype)0.18728L,				\
	      flt_b = (ftype)r1,				\
	      flt_c = (ftype)r2,				\
	      flt_d = (ftype)0.0,				\
	      flt_r;						\
								\
	stress_mwc_default_seed();				\
	a = stress_mwc32();					\
	b = stress_mwc32();					\
								\
	for (i = 0; i < 1000; i++) {				\
		int_float_ops(ftype, flt_a, flt_b, flt_c, 	\
			flt_d,f_sinf, f_cosf, inttype,		\
			a, b, c1, c2, c3);			\
	}							\
	if ((g_opt_flags & OPT_FLAGS_VERIFY) &&			\
	    ((a != a_final) || (b != b_final)))	{		\
		pr_fail("%s: int" # sz " error detected, "	\
			"failed int" # sz "" # ftype		\
			" math operations\n", name);		\
		return EXIT_FAILURE;				\
	}							\
								\
	flt_r = flt_a + flt_b + flt_c + flt_d;			\
	stress_double_put((double)flt_r);			\
	return EXIT_SUCCESS;					\
}

stress_cpu_int_fp(uint32_t, 32, float, float,
	0x1ce9b547UL, 0xa24b33aUL,
	C1, C2, C3, shim_sinf, shim_cosf)
stress_cpu_int_fp(uint32_t, 32, double, double,
	0x1ce9b547UL, 0xa24b33aUL,
	C1, C2, C3, shim_sin, shim_cos)
stress_cpu_int_fp(uint32_t, 32, long double, longdouble,
	0x1ce9b547UL, 0xa24b33aUL,
	C1, C2, C3, shim_sinl, shim_cosl)
stress_cpu_int_fp(uint64_t, 64, float, float,
	0x13f7f6dc1d79197cULL, 0x1863d2c6969a51ceULL,
	C1, C2, C3, shim_sinf, shim_cosf)
stress_cpu_int_fp(uint64_t, 64, double, double,
	0x13f7f6dc1d79197cULL, 0x1863d2c6969a51ceULL,
	C1, C2, C3, shim_sin, shim_cos)
stress_cpu_int_fp(uint64_t, 64, long double, longdouble,
	0x13f7f6dc1d79197cULL, 0x1863d2c6969a51ceULL,
	C1, C2, C3, shim_sinl, shim_cosl)

#if defined(HAVE_INT128_T)
stress_cpu_int_fp(__uint128_t, 128, float, float,
	STRESS_UINT128(0x132af604d8b9183a,0x5e3af8fa7a663d74),
	STRESS_UINT128(0x0062f086e6160e4e,0x0d84c9f800365858),
	STRESS_UINT128(C1, C1), STRESS_UINT128(C2, C2), STRESS_UINT128(C3, C3),
	shim_sinf, shim_cosf)
stress_cpu_int_fp(__uint128_t, 128, double, double,
	STRESS_UINT128(0x132af604d8b9183a,0x5e3af8fa7a663d74),
	STRESS_UINT128(0x0062f086e6160e4e,0x0d84c9f800365858),
	STRESS_UINT128(C1, C1), STRESS_UINT128(C2, C2), STRESS_UINT128(C3, C3),
	shim_sin, shim_cos)
stress_cpu_int_fp(__uint128_t, 128, long double, longdouble,
	STRESS_UINT128(0x132af604d8b9183a,0x5e3af8fa7a663d74),
	STRESS_UINT128(0x0062f086e6160e4e,0x0d84c9f800365858),
	STRESS_UINT128(C1, C1), STRESS_UINT128(C2, C2), STRESS_UINT128(C3, C3),
	shim_sinl, shim_cosl)
#if defined(HAVE_Decimal32) &&	\
    !defined(HAVE_COMPILER_CLANG)
stress_cpu_int_fp(__uint128_t, 128, _Decimal32, decimal32,
	STRESS_UINT128(0x132af604d8b9183a,0x5e3af8fa7a663d74),
	STRESS_UINT128(0x0062f086e6160e4e,0x0d84c9f800365858),
	STRESS_UINT128(C1, C1), STRESS_UINT128(C2, C2), STRESS_UINT128(C3, C3),
	(_Decimal32)shim_sinf, (_Decimal32)shim_cosf)
#endif
#if defined(HAVE_Decimal64) &&	\
    !defined(HAVE_COMPILER_CLANG)
stress_cpu_int_fp(__uint128_t, 128, _Decimal64, decimal64,
	STRESS_UINT128(0x132af604d8b9183a,0x5e3af8fa7a663d74),
	STRESS_UINT128(0x0062f086e6160e4e,0x0d84c9f800365858),
	STRESS_UINT128(C1, C1), STRESS_UINT128(C2, C2), STRESS_UINT128(C3, C3),
	(_Decimal64)shim_sin, (_Decimal64)shim_cos)
#endif
#if defined(HAVE_Decimal128) &&	\
    !defined(HAVE_COMPILER_CLANG)
stress_cpu_int_fp(__uint128_t, 128, _Decimal128, decimal128,
	STRESS_UINT128(0x132af604d8b9183a,0x5e3af8fa7a663d74),
	STRESS_UINT128(0x0062f086e6160e4e,0x0d84c9f800365858),
	STRESS_UINT128(C1, C1), STRESS_UINT128(C2, C2), STRESS_UINT128(C3, C3),
	(_Decimal128)shim_sinl, (_Decimal128)shim_cosl)
#endif
#endif

/*
 *  stress_cpu_rgb()
 *	CCIR 601 RGB to YUV to RGB conversion
 */
static int OPTIMIZE3 TARGET_CLONES stress_cpu_rgb(const char *name)
{
	int i;
	uint32_t rgb = stress_mwc32() & 0xffffff;
	uint8_t r = (uint8_t)(rgb >> 16);
	uint8_t g = (uint8_t)(rgb >> 8);
	uint8_t b = (uint8_t)rgb;

	(void)name;

	/* Do a 1000 colours starting from the rgb seed */
PRAGMA_UNROLL_N(8)
	for (i = 0; i < 1000; i++) {
		float y, u, v;

		/* RGB to CCIR 601 YUV */
		y = (0.299f * r) + (0.587f * g) + (0.114f * b);
		u = (b - y) * 0.565f;
		v = (r - y) * 0.713f;

		/* YUV back to RGB */
		r = (uint8_t)(y + (1.403f * v));
		g = (uint8_t)(y - (0.344f * u) - (0.714f * v));
		b = (uint8_t)(y + (1.770f * u));

		/* And bump each colour to make next round */
		r += 1;
		g += 2;
		b += 3;
		stress_uint64_put(r + g + b);
	}
	return EXIT_SUCCESS;
}

/*
 *  stress_cpu_matrix_prod(void)
 *	matrix product
 */
static int OPTIMIZE3 TARGET_CLONES stress_cpu_matrix_prod(const char *name)
{
	int i, j, k;

	static long double a[MATRIX_PROD_SIZE][MATRIX_PROD_SIZE] ALIGN64,
			   b[MATRIX_PROD_SIZE][MATRIX_PROD_SIZE] ALIGN64,
			   r[MATRIX_PROD_SIZE][MATRIX_PROD_SIZE] ALIGN64;
	const long double v = 1 / (long double)((uint32_t)~0);
	long double sum = 0.0L;

	(void)name;

	for (i = 0; i < MATRIX_PROD_SIZE; i++) {
		for (j = 0; j < MATRIX_PROD_SIZE; j++) {
			const uint32_t r1 = stress_mwc32();
			const uint32_t r2 = stress_mwc32();

			a[i][j] = (long double)r1 * v;
			b[i][j] = (long double)r2 * v;
			r[i][j] = 0.0L;
		}
	}

	for (i = 0; i < MATRIX_PROD_SIZE; i++) {
		for (j = 0; j < MATRIX_PROD_SIZE; j++) {
			for (k = 0; k < MATRIX_PROD_SIZE; k++) {
				r[i][j] += a[i][k] * b[k][j];
			}
		}
	}

	for (i = 0; i < MATRIX_PROD_SIZE; i++)
PRAGMA_UNROLL_N(8)
		for (j = 0; j < MATRIX_PROD_SIZE; j++)
			sum += r[i][j];
	stress_long_double_put(sum);
	return EXIT_SUCCESS;
}

/*
 *   stress_cpu_fibonacci()
 *	compute fibonacci series
 */
static int OPTIMIZE3 stress_cpu_fibonacci(const char *name)
{
	const uint64_t fn_res = 0xa94fad42221f2702ULL;
	register uint64_t f1 = 0, f2 = 1, fn;

	do {
		fn = f1 + f2;
		f1 = f2;
		f2 = fn;
	} while (!(fn & 0x8000000000000000ULL));

	if ((g_opt_flags & OPT_FLAGS_VERIFY) && (fn_res != fn)) {
		pr_fail("%s: fibonacci error detected, summation "
			"or assignment failure\n", name);
		return EXIT_FAILURE;
	}
	return EXIT_SUCCESS;
}

/*
 *  stress_cpu_psi
 *	compute the constant psi,
 * 	the reciprocal Fibonacci constant
 */
static int OPTIMIZE3 stress_cpu_psi(const char *name)
{
	long double f1 = 0.0L, f2 = 1.0L;
	long double psi = 0.0L, last_psi;
	const long double precision = 1.0e-20L;
	int i = 0;
	const int max_iter = 100;

	do {
		const long double fn = f1 + f2;

		f1 = f2;
		f2 = fn;
		last_psi = psi;
		psi += 1.0L / f1;
		i++;
	} while ((i < max_iter) && (shim_fabsl(psi - last_psi) > precision));

	if (g_opt_flags & OPT_FLAGS_VERIFY) {
		if (shim_fabsl(psi - PSI) > 1.0e-15L) {
			pr_fail("%s: calculation of reciprocal "
				"Fibonacci constant phi not as accurate "
				"as expected\n", name);
			return EXIT_FAILURE;
		}
		if (i >= max_iter) {
			pr_fail("%s: calculation of reciprocal "
				"Fibonacci constant took more iterations "
				"than expected\n", name);
			return EXIT_FAILURE;
		}
	}

	stress_long_double_put(psi);
	return EXIT_SUCCESS;
}

/*
 *   stress_cpu_ln2
 *	compute ln(2) using series
 */
static int OPTIMIZE3 TARGET_CLONES OPTIMIZE_FAST_MATH stress_cpu_ln2(const char *name)
{
	long double ln2 = 0.0L, last_ln2 = 0.0L;
	const long double precision = 1.0e-7L;
	register int n = 1;
	const int max_iter = 10000;

	/* Not the fastest converging series */
	do {
		last_ln2 = ln2;
		/* Unroll, do several ops */
		ln2 += (long double)1.0L / (long double)n++;
		ln2 -= (long double)1.0L / (long double)n++;
		ln2 += (long double)1.0L / (long double)n++;
		ln2 -= (long double)1.0L / (long double)n++;
		ln2 += (long double)1.0L / (long double)n++;
		ln2 -= (long double)1.0L / (long double)n++;
		ln2 += (long double)1.0L / (long double)n++;
		ln2 -= (long double)1.0L / (long double)n++;
	} while ((n < max_iter) && (shim_fabsl(ln2 - last_ln2) > precision));

	if ((g_opt_flags & OPT_FLAGS_VERIFY) && (n >= max_iter)) {
		pr_fail("%s: calculation of ln(2) took more "
			"iterations than expected\n", name);
		return EXIT_FAILURE;
	}

	stress_long_double_put(ln2);
	return EXIT_SUCCESS;
}

/*
 *  ackermann()
 *	a naive/simple implementation of the ackermann function
 */
static uint32_t ackermann(const uint32_t m, const uint32_t n)
{
	if (m == 0)
		return n + 1;
	else if (n == 0)
		return ackermann(m - 1, 1);
	else
		return ackermann(m - 1, ackermann(m, n - 1));
}

/*
 *   stress_cpu_ackermann
 *	compute ackermann function
 */
static int stress_cpu_ackermann(const char *name)
{
	uint32_t a = ackermann(3, 7);

	if ((g_opt_flags & OPT_FLAGS_VERIFY) && (a != 1021)) {
		pr_fail("%s: ackermann error detected, "
			"ackermann(3, 7) miscalculated, got %" PRIu32
			", expected %d\n", name, a, 1021);
		return EXIT_FAILURE;
	}
	return EXIT_SUCCESS;
}

/*
 *   stress_cpu_explog
 *	compute exp(log(n))
 */
static int OPTIMIZE_FAST_MATH stress_cpu_explog(const char *name)
{
	uint32_t i;
	double n = 1e6 + (double)stress_mwc8();
	double m = 0.0;

	(void)name;

	for (i = 1; LIKELY(i < 100000); i++) {
		n = shim_log(n) / 1.00002;
		m += n;
		n = shim_exp(n);
		m += n;
	}
	stress_double_put(m);
	stress_double_put(n);
	return EXIT_SUCCESS;
}

/*
 *  This could be a ternary operator, v = (v op val) ? a : b
 *  but it may be optimised down, so force a compare and jmp
 *  with -O0 and a if/else construct
 */
#define JMP(v, op, val, a, b)			\
do {						\
	if (v op val)				\
		v = a;				\
	else					\
		v = b;				\
	stress_uint32_put((uint32_t)(next + i));\
} while (0)

/*
 *   stress_cpu_jmp
 *	jmp conditionals
 */
static int OPTIMIZE0 stress_cpu_jmp(const char *name)
{
	register int i, next = 0;

	(void)name;

	for (i = 1; i < 1000; i++) {
		/* Force lots of compare jmps */
		JMP(next, ==, 1, 2, 3);
		JMP(next, >, 2, 0, 1);
		JMP(next, <, 1, 1, 0);
		JMP(next, ==, 1, 2, 3);
		JMP(next, >, 2, 0, 1);
		JMP(next, <, 1, 1, 0);
		JMP(next, ==, 1, 2, 3);
		JMP(next, >, 2, 0, 1);
		JMP(next, <, 1, 1, 0);
		JMP(next, ==, 1, 2, 3);
		JMP(next, >, 2, 0, 1);
		JMP(next, <, 1, 1, 0);
	}
	return EXIT_SUCCESS;
}

/*
 *  ccitt_crc16()
 *	perform naive CCITT CRC16
 */
static uint16_t CONST OPTIMIZE3 ccitt_crc16(const uint8_t *data, size_t n)
{
	/*
	 *  The CCITT CRC16 polynomial is
	 *     16    12    5
	 *    x   + x   + x  + 1
	 *
	 *  which is 0x11021, but to make the computation
	 *  simpler, this has been reversed to 0x8408 and
	 *  the top bit ignored..
	 *  We can get away with a 17 bit polynomial
	 *  being represented by a 16 bit value because
	 *  we are assuming the top bit is always set.
	 */
	const uint16_t polynomial = 0x8408;
	register uint16_t crc = 0xffff;

	if (!n)
		return 0;

	for (; n; n--) {
		register uint8_t i;
		register uint8_t val = (uint16_t)0xff & *data++;

		for (i = 8; i; --i, val >>= 1) {
			bool do_xor = 1 & (val ^ crc);
			crc >>= 1;
			crc ^= do_xor ? polynomial : 0;
		}
	}

	crc = ~crc;
	return ((uint16_t)(crc << 8)) | (crc >> 8);
}

/*
 *   stress_cpu_crc16
 *	compute 1024 rounds of CCITT CRC16
 */
static int stress_cpu_crc16(const char *name)
{
	uint8_t buffer[1024];
	size_t i;

	(void)name;

	random_buffer(buffer, sizeof(buffer));
	for (i = 1; i < sizeof(buffer); i++)
		stress_uint64_put(ccitt_crc16(buffer, i));
	return EXIT_SUCCESS;
}

/*
 *  fletcher16
 *	naive implementation of fletcher16 checksum
 */
static uint16_t CONST fletcher16(const uint8_t *data, const size_t len)
{
	register uint16_t sum1 = 0, sum2 = 0;
	register size_t i;

	for (i = 0; i < len; i++) {
		sum1 = (sum1 + data[i]) % 255;
		sum2 = (sum2 + sum1) % 255;
	}
	return ((uint16_t)(sum2 << 8)) | sum1;
}

/*
 *   stress_cpu_fletcher16()
 *	compute 1024 rounds of fletcher16 checksum
 */
static int stress_cpu_fletcher16(const char *name)
{
	uint8_t buffer[1024];
	size_t i;

	(void)name;

	random_buffer((uint8_t *)buffer, sizeof(buffer));
	for (i = 1; i < sizeof(buffer); i++)
		stress_uint16_put(fletcher16(buffer, i));
	return EXIT_SUCCESS;
}

/*
 *   stress_cpu_ipv4checksum
 *	compute 1024 rounds of IPv4 checksum
 */
static int stress_cpu_ipv4checksum(const char *name)
{
	uint16_t buffer[512];
	size_t i;

	(void)name;

	random_buffer((uint8_t *)buffer, sizeof(buffer));
	for (i = 1; i < sizeof(buffer); i++)
		stress_uint16_put(stress_ipv4_checksum(buffer, i));
	return EXIT_SUCCESS;
}

#if defined(HAVE_COMPLEX_H) &&		\
    defined(HAVE_COMPLEX) &&		\
    defined(__STDC_IEC_559_COMPLEX__) &&\
    !defined(__UCLIBC__)
/*
 *  zeta()
 *	Riemann zeta function
 */
static inline long double complex CONST OPTIMIZE3 OPTIMIZE_FAST_MATH zeta(
	const long double complex s,
	long double precision)
{
	int i = 1;
	long double complex z = 0.0L, zold = 0.0L;

	do {
		const double complex pwr = shim_cpow(i++, (complex double)s);

		zold = z;
		z += 1.0L / (long double complex)pwr;
	} while (shim_cabsl(z - zold) > precision);

	return z;
}

/*
 * stress_cpu_zeta()
 *	stress test Zeta(2.0)..Zeta(10.0)
 */
static int OPTIMIZE3 OPTIMIZE_FAST_MATH stress_cpu_zeta(const char *name)
{
	long double precision = 0.00000001L;
	int i;

	(void)name;

	for (i = 2; i < 11; i++) {
		const long double complex z = zeta((long double complex)i, precision);

		stress_long_double_put((long double)z);
	}
	return EXIT_SUCCESS;
}
#else
	UNEXPECTED
#endif

/*
 * stress_cpu_gamma()
 *	stress Euler-Mascheroni constant gamma
 */
static int OPTIMIZE3 OPTIMIZE_FAST_MATH stress_cpu_gamma(const char *name)
{
	const long double precision = 1.0e-10L;
	long double sum = 0.0L, k = 1.0L, gammanew = 0.0L, gammaold;

	do {
		gammaold = gammanew;
		sum += 1.0L / k;
		gammanew = sum - shim_logl(k);
		k += 1.0L;
	} while ((k < 1e6L) && shim_fabsl(gammanew - gammaold) > precision);

	stress_long_double_put(gammanew);

	if (g_opt_flags & OPT_FLAGS_VERIFY) {
		if (shim_fabsl(gammanew - GAMMA) > 1.0e-5L) {
			pr_fail("%s: calculation of Euler-Mascheroni "
				"constant not as accurate as expected\n", name);
			return EXIT_FAILURE;
		}
		if (k > 80000.0L) {
			pr_fail("%s: calculation of Euler-Mascheroni "
				"constant took more iterations than "
				"expected\n", name);
			return EXIT_FAILURE;
		}
	}
	return EXIT_SUCCESS;
}

/*
 * stress_cpu_correlate()
 *
 *  Introduction to Signal Processing,
 *  Prentice-Hall, 1995, ISBN: 0-13-209172-0.
 */
static int OPTIMIZE3 stress_cpu_correlate(const char *name)
{
	size_t i, j;
	double data_average = 0.0;
	static double data[CORRELATE_DATA_LEN];
	static double corr[CORRELATE_LEN + 1];

	(void)name;

	/* Generate some random data */
	for (i = 0; i < CORRELATE_DATA_LEN; i++) {
		const uint64_t r = stress_mwc64();

		data[i] = (double)r;
		data_average += data[i];
	}
	data_average /= (double)CORRELATE_DATA_LEN;

	/* And correlate */
	for (i = 0; i <= CORRELATE_LEN; i++) {
		corr[i] = 0.0;
		for (j = 0; j < CORRELATE_DATA_LEN - i; j++) {
			corr[i] += (data[i + j] - data_average) *
				   (data[j] - data_average);
		}
		corr[i] /= (double)CORRELATE_LEN;
		stress_double_put(corr[i]);
	}
	return EXIT_SUCCESS;
}

/*
 * stress_cpu_sieve()
 * 	slightly optimised Sieve of Eratosthenes
 */
static int OPTIMIZE3 stress_cpu_sieve(const char *name)
{
	const double dsqrt = shim_sqrt(SIEVE_SIZE);
	const uint32_t nsqrt = (uint32_t)dsqrt;
	static uint32_t sieve[(SIEVE_SIZE + 31) / 32];
	uint32_t i, j;

	(void)shim_memset(sieve, 0xff, sizeof(sieve));
	for (i = 2; i < nsqrt; i++)
		if (STRESS_GETBIT(sieve, i))
			for (j = i * i; j < SIEVE_SIZE; j += i)
				STRESS_CLRBIT(sieve, j);

	/* And count up number of primes */
PRAGMA_UNROLL_N(8)
	for (j = 0, i = 2; i < SIEVE_SIZE; i++) {
		if (STRESS_GETBIT(sieve, i))
			j++;
	}
	if ((g_opt_flags & OPT_FLAGS_VERIFY) && (j != 10000)) {
		pr_fail("%s: sieve error detected, number of "
			"primes has been miscalculated\n", name);
		return EXIT_FAILURE;
	}
	return EXIT_SUCCESS;
}

/*
 *  is_prime()
 *	return true if n is prime
 *	http://en.wikipedia.org/wiki/Primality_test
 */
static inline CONST OPTIMIZE3 ALWAYS_INLINE uint32_t is_prime(uint32_t n)
{
	register uint32_t i, max;
	double dsqrt;

	if (UNLIKELY(n <= 3))
		return n >= 2;
	if ((n % 2 == 0) || (n % 3 == 0))
		return 0;

	dsqrt = shim_sqrt(n);
	max = (uint32_t)dsqrt + 1;
	for (i = 5; i < max; i += 6)
		if ((n % i == 0) || (n % (i + 2) == 0))
			return 0;
	return 1;
}

/*
 *  stress_cpu_prime()
 *
 */
static int stress_cpu_prime(const char *name)
{
	uint32_t i, nprimes = 0;

	for (i = 0; i < SIEVE_SIZE; i++) {
		nprimes += is_prime(i);
	}

	if ((g_opt_flags & OPT_FLAGS_VERIFY) && (nprimes != 10000)) {
		pr_fail("%s: prime error detected, number of primes "
			"has been miscalculated\n", name);
		return EXIT_FAILURE;
	}
	return EXIT_SUCCESS;
}

/*
 *  stress_cpu_gray()
 *	compute gray codes
 */
static int OPTIMIZE3 TARGET_CLONES stress_cpu_gray(const char *name)
{
	register uint32_t i;
	register uint64_t sum = 0;

	for (i = 0; LIKELY(i < 0x10000); i++) {
		register uint32_t gray_code;

		/* Binary to Gray code */
		gray_code = (i >> 1) ^ i;
		sum += gray_code;

		/* Gray code back to binary */
#if 0
		{
			/* Slow iterative method */
			register uint32_t mask;

			for (mask = gray_code >> 1; mask; mask >>= 1)
				gray_code ^= mask;
		}
#else
		/* Fast non-loop method */
		gray_code ^= (gray_code >> 1);
		gray_code ^= (gray_code >> 2);
		gray_code ^= (gray_code >> 4);
		gray_code ^= (gray_code >> 8);
		gray_code ^= (gray_code >> 16);
#endif
		sum += gray_code;
	}
	if ((g_opt_flags & OPT_FLAGS_VERIFY) && (sum != 0xffff0000)) {
		pr_fail("%s: gray code error detected, sum of gray "
			"codes between 0x00000 and 0x10000 miscalculated\n",
			name);
		return EXIT_FAILURE;
	}
	return EXIT_SUCCESS;
}

/*
 * hanoi()
 *	do a Hanoi move
 */
static uint32_t hanoi(
	const uint16_t n,
	const char p1,
	const char p2,
	const char p3)
{
	if (UNLIKELY(n == 0)) {
		/* Move p1 -> p2 */
		return 1;
	} else {
		uint32_t m = hanoi(n - 1, p1, p3, p2);

		/* Move p1 -> p2 */
		m += hanoi(n - 1, p3, p2, p1);
		return m;
	}
}

/*
 *  stress_cpu_hanoi
 *	stress with recursive Towers of Hanoi
 */
static int stress_cpu_hanoi(const char *name)
{
	const uint32_t n = hanoi(20, 'X', 'Y', 'Z');

	if ((g_opt_flags & OPT_FLAGS_VERIFY) && (n != 1048576)) {
		pr_fail("%s: number of hanoi moves different from "
			"the expected number\n", name);
		return EXIT_FAILURE;
	}
	stress_uint64_put(n);

	return EXIT_SUCCESS;
}

/*
 *  stress_floatconversion
 *	exercise conversion to/from different floating point values
 */
static int TARGET_CLONES OPTIMIZE_FAST_MATH stress_cpu_floatconversion(const char *name)
{
	float f_sum = 0.0;
	double d_sum = 0.0;
	long double ld_sum = 0.0L;
	register uint32_t i, j_sum = 0;

	(void)name;

	for (i = 0; i < 65536; i++) {
		float f;
		double d;
		long double ld;

		f = (float)i;
		d = (double)f;
		ld = (long double)d;

		f_sum += f;
		d_sum += d;
		ld_sum += ld;
		j_sum += (uint32_t)ld;

		f = (float)(double)i;
		f_sum += f;
		f = (float)(long double)i;
		f_sum += f;
		f = (float)(double)(long double)i;
		f_sum += f;
		f = (float)(long double)(double)i;
		f_sum += f;

		d = (double)(long double)f;
		d_sum += d;
		d = (double)(float)f;
		d_sum += d;
		d = (double)(long double)(float)f;
		d_sum += d;
		d = (double)(float)(long double)f;
		d_sum += d;

		ld = (long double)(float)d;
		ld_sum += ld;
		ld = (long double)(double)d;
		ld_sum += ld;
		ld = (long double)(float)(double)d;
		ld_sum += ld;
		ld = (long double)(double)(float)d;
		ld_sum += ld;
	}
	stress_long_double_put(ld_sum);
	stress_double_put(d_sum);
	stress_float_put(f_sum);
	stress_uint32_put(j_sum);

	return EXIT_SUCCESS;
}

/*
 *  stress_intconversion
 *	exercise conversion to/from different int values
 */
static int stress_cpu_intconversion(const char *name)
{
	int16_t i16_sum = (int16_t)stress_mwc16();
	int32_t i32_sum = (int32_t)stress_mwc32();
	int64_t i64_sum = (int64_t)stress_mwc64();

	register uint32_t i;

	(void)name;

	for (i = 0; i < 65536; i++) {
		int16_t i16;
		int32_t i32;
		int64_t	i64;

		i16 = (int16_t)i;
		i32 = (int32_t)i;
		i64 = (int64_t)i;

		i16_sum += i16;
		i32_sum += i32;
		i64_sum += i64;

		i16 = -(int16_t)(uint32_t)-(int64_t)(uint64_t)i64_sum;
		i16_sum -= i16;
		i32 = -(int16_t)(uint32_t)-(int64_t)(uint64_t)i16_sum;
		i32_sum -= i32;
		i64 = -(int16_t)(uint32_t)-(int64_t)(uint64_t)i32_sum;
		i64_sum -= i64;

		i16 = -(int16_t)(uint64_t)-(int32_t)(uint64_t)i64_sum;
		i16_sum += i16;
		i32 = -(int16_t)(uint64_t)-(int32_t)(uint64_t)i16_sum;
		i32_sum += i32;
		i64 = -(int16_t)(uint64_t)-(int32_t)(uint64_t)i32_sum;
		i64_sum += i64;

		i16 = (int16_t)-((int32_t)(uint16_t)-(int64_t)(uint64_t)i64_sum);
		i16_sum -= i16;
		i32 = -(int32_t)(uint16_t)-(int64_t)(uint64_t)i16_sum;
		i32_sum -= i32;
		i64 = -(int32_t)(uint16_t)-(int64_t)(uint64_t)i32_sum;
		i64_sum -= i64;

		i16 = (int16_t)-((int32_t)(uint64_t)-(int16_t)(uint64_t)i64_sum);
		i16_sum += i16;
		i32 = -(int32_t)(uint64_t)-(int16_t)(uint64_t)i16_sum;
		i32_sum += i32;
		i64 = -(int32_t)(uint64_t)-(int16_t)(uint64_t)i32_sum;
		i64_sum += i64;

		i16 = (int16_t)-((int64_t)(uint16_t)-(int32_t)(uint64_t)i64_sum);
		i16_sum -= i16;
		i32 = (int32_t)-((int64_t)(uint16_t)-(int32_t)(uint64_t)i16_sum);
		i32_sum -= i32;
		i64 = (int64_t)(uint16_t)-(int32_t)(uint64_t)i32_sum;
		i64_sum -= i64;

		i16 = (int16_t)-((int64_t)(uint32_t)-(int16_t)(uint64_t)i64_sum);
		i16_sum += i16;
		i32 = (int32_t)-((int64_t)(uint32_t)-(int16_t)(uint64_t)i16_sum);
		i32_sum += i32;
		i64 = -(int64_t)(uint32_t)-(int16_t)(uint64_t)i32_sum;
		i64_sum += i64;
	}
	stress_uint16_put((uint16_t)i16_sum);
	stress_uint32_put((uint32_t)i32_sum);
	stress_uint64_put((uint64_t)i64_sum);

	return EXIT_SUCCESS;
}

/*
 *  factorial()
 *	compute n!
 */
static inline long double CONST OPTIMIZE3 factorial(int n)
{
	static const long double factorials[] = {
		1.0L,
		1.0L,
		2.0L,
		6.0L,
		24.0L,
		120.0L,
		720.0L,
		5040.0L,
		40320.0L,
		362880.0L,
		3628800.0L,
		39916800.0L,
		479001600.0L,
		6227020800.0L,
		87178291200.0L,
		1307674368000.0L,
		20922789888000.0L,
		355687428096000.0L,
		6402373705728000.0L,
		121645100408832000.0L,
		2432902008176640000.0L,
		51090942171709440000.0L,
		1124000727777607680000.0L,
		25852016738884976640000.0L,
		620448401733239439360000.0L,
		15511210043330985984000000.0L,
		403291461126605635592388608.0L,
		10888869450418352161430700032.0L,
		304888344611713860511469666304.0L,
		8841761993739701954695181369344.0L,
		265252859812191058647452510846976.0L,
		8222838654177922818071027836256256.0L,
		263130836933693530178272890760200192.0L
	};

	if (n < (int)SIZEOF_ARRAY(factorials))
		return factorials[n];

	return shim_roundl(shim_expl(shim_lgammal((long double)(n + 1))));
}

/*
 *  stress_cpu_pi()
 *	compute pi using the Srinivasa Ramanujan
 *	fast convergence algorithm
 */
static int OPTIMIZE3 stress_cpu_pi(const char *name)
{
	long double s = 0.0L, pi = 0.0L, last_pi = 0.0L;
	const long double precision = 1.0e-20L;
	const long double c = 2.0L * shim_sqrtl(2.0L) / 9801.0L;
	const int max_iter = 5;
	int k = 0;

	do {
		last_pi = pi;
		s += (factorial(4 * k) *
			((26390.0L * (long double)k) + 1103)) /
			(shim_powl(factorial(k), 4.0L) * shim_powl(396.0L, 4.0L * k));
		pi = 1.0 / (s * c);
		k++;
	} while ((k < max_iter) && (shim_fabsl(pi - last_pi) > precision));

	/* Quick sanity checks */
	if (g_opt_flags & OPT_FLAGS_VERIFY) {
		if (k >= max_iter) {
			pr_fail("%s: number of iterations to compute "
				"pi was more than expected\n", name);
			return EXIT_FAILURE;
		}
		if (shim_fabsl(pi - PI) > 1.0e-15L) {
			pr_fail("%s: accuracy of computed pi is not "
				"as good as expected\n", name);
			return EXIT_FAILURE;
		}
	}

	stress_long_double_put(pi);

	return EXIT_SUCCESS;
}

/*
 *  stress_cpu_omega()
 *	compute the constant omega
 *	See http://en.wikipedia.org/wiki/Omega_constant
 */
static int OPTIMIZE3 OPTIMIZE_FAST_MATH stress_cpu_omega(const char *name)
{
	long double omega = 0.5 + ((double)stress_mwc16() * 1.0E-9), last_omega = 0.0L;
	const long double precision = 1.0e-20L;
	const int max_iter = 6;
	int n = 0;

	/*
	 * Omega converges very quickly, on most CPUs it is
	 * within 6 iterations.
	 */
	do {
		last_omega = omega;
		omega = (1.0L + omega) / (1.0L + shim_expl(omega));
		n++;
	} while ((n < max_iter) && (shim_fabsl(omega - last_omega) > precision));

	if (g_opt_flags & OPT_FLAGS_VERIFY) {
		if (n > max_iter) {
			pr_fail("%s: number of iterations to compute "
				"omega was more than expected (%d vs %d)\n",
				name, n, max_iter);
			return EXIT_FAILURE;
		}
		if (shim_fabsl(omega - OMEGA) > 1.0e-16L) {
			pr_fail("%s: accuracy of computed omega is "
				"not as good as expected\n", name);
			return EXIT_FAILURE;
		}
	}

	stress_long_double_put(omega);

	return EXIT_SUCCESS;
}

#define HAMMING(G, i, nybble, code) 			\
do {							\
	int8_t res;					\
							\
	res = (((G[3] >> i) & (nybble >> 3)) & 1) ^	\
	      (((G[2] >> i) & (nybble >> 2)) & 1) ^	\
	      (((G[1] >> i) & (nybble >> 1)) & 1) ^	\
	      (((G[0] >> i) & (nybble >> 0)) & 1);	\
	code ^= ((res & 1) << i);			\
} while (0)

/*
 *  hamming84()
 *	compute Hamming (8,4) codes
 */
static uint8_t CONST OPTIMIZE3 hamming84(const uint8_t nybble)
{
	/*
	 * Hamming (8,4) Generator matrix
	 * (4 parity bits, 4 data bits)
	 *
	 *  p1 p2 p3 p4 d1 d2 d3 d4
	 *  0  1  1  1  1  0  0  0
	 *  1  0  1  1  0  1  0  0
	 *  1  1  0  1  0  0  1  0
	 *  1  1  1  0  0  0  0  1
	 *
	 * Where:
	 *  d1..d4 = 4 data bits
	 *  p1..p4 = 4 parity bits:
	 *    p1 = d2 + d3 + d4
	 *    p2 = d1 + d3 + d4
	 *    p3 = d1 + d2 + d4
	 *    p4 = d1 + d2 + d3
	 *
	 * G[] is reversed to turn G[3-j] into G[j] to save a subtraction
	 */
	static const uint8_t G[] = {
		0xf1,	/* 0b11110001 */
		0xd2,	/* 0b11010010 */
		0xb4,	/* 0b10110100 */
		0x78,	/* 0b01111000 */
	};

	register uint8_t code = 0;

	/* Unrolled 8 bit loop x unrolled 4 bit loop  */
	HAMMING(G, 7, nybble, code);
	HAMMING(G, 6, nybble, code);
	HAMMING(G, 5, nybble, code);
	HAMMING(G, 4, nybble, code);
	HAMMING(G, 3, nybble, code);
	HAMMING(G, 2, nybble, code);
	HAMMING(G, 1, nybble, code);
	HAMMING(G, 0, nybble, code);

	return code;
}

/*
 *  stress_cpu_hamming()
 *	compute hamming code on 65536 x 4 nybbles
 */
static int OPTIMIZE3 TARGET_CLONES stress_cpu_hamming(const char *name)
{
	uint32_t i;
	uint32_t sum = 0;

	for (i = 0; i < 65536; i++) {
		/* 4 x 4 bits to 4 x 8 bits hamming encoded */
		register const uint32_t encoded =
			(uint32_t)(hamming84((i >> 12) & 0xf) << 24) |
			(uint32_t)(hamming84((i >> 8) & 0xf) << 16) |
			(uint32_t)(hamming84((i >> 4) & 0xf) << 8) |
			(uint32_t)(hamming84((i >> 0) & 0xf) << 0);
		sum += encoded;
	}

	if ((g_opt_flags & OPT_FLAGS_VERIFY) && (sum != 0xffff8000)) {
		pr_fail("%s: hamming error detected, sum of 65536 "
			"hamming codes not correct\n", name);
		return EXIT_FAILURE;
	}
	return EXIT_SUCCESS;
}

static ptrdiff_t stress_cpu_callfunc_func(
	ssize_t		n,
	uint64_t	u64arg,
	uint32_t	u32arg,
	uint16_t	u16arg,
	uint8_t		u8arg,
	uint64_t	*p_u64arg,
	uint32_t	*p_u32arg,
	uint16_t	*p_u16arg,
	uint8_t		*p_u8arg)
{
	if (LIKELY(n > 0))
		return stress_cpu_callfunc_func(n - 1,
			u64arg + (uint64_t)u32arg,
			u32arg,
			u16arg,
			u8arg,
			p_u64arg,
			p_u32arg,
			p_u16arg,
			p_u8arg);
	else
		return &u64arg - p_u64arg;
}

/*
 *  stress_cpu_callfunc()
 *	deep function calls
 */
static int stress_cpu_callfunc(const char *name)
{
	uint64_t u64arg = stress_mwc64();
	uint32_t u32arg = stress_mwc32();
	uint16_t u16arg = stress_mwc16();
	uint8_t u8arg = stress_mwc8();
	ptrdiff_t ret;

	(void)name;

	ret = stress_cpu_callfunc_func(1024,
		u64arg, u32arg, u16arg, u8arg,
		&u64arg, &u32arg, &u16arg, &u8arg);
	stress_uint64_put((uint64_t)ret);

	return EXIT_SUCCESS;
}


#define P2(n) n, n^1, n^1, n
#define P4(n) P2(n), P2(n^1), P2(n^1), P2(n)
#define P6(n) P4(n), P4(n^1), P4(n^1), P4(n)

static const bool stress_cpu_parity_table[256] = {
	P6(0), P6(1), P6(1), P6(0)
};

/*
 *  stress_cpu_parity
 *	compute parity different ways
 */
static int stress_cpu_parity(const char *name)
{
	uint32_t val = 0x83fb5acf;
	size_t i;

	for (i = 0; i < 1000; i++, val++) {
		register uint32_t parity, p;
		uint32_t v;
		union {
			uint32_t v32;
			uint8_t  v8[4];
		} u;

		/*
		 * Naive way
		 */
		v = val;
		parity = 0;
		while (v) {
			if (v & 1)
				parity = !parity;
			v >>= 1;
		}

		/*
		 * Naive way with Brian Kernigan's bit counting optimisation
		 * https://graphics.stanford.edu/~seander/bithacks.html
		 */
		v = val;
		p = 0;
		while (v) {
			p = !p;
			v = v & (v - 1);
		}
		if ((g_opt_flags & OPT_FLAGS_VERIFY) && (p != parity)) {
			pr_fail("%s: parity error detected, using "
				"optimised naive method\n",  name);
			return EXIT_FAILURE;
		}

		/*
		 * "Compute parity of a word with a multiply"
		 * the Andrew Shapira method,
		 * https://graphics.stanford.edu/~seander/bithacks.html
		 */
		v = val;
		v ^= v >> 1;
		v ^= v >> 2;
		v = (v & 0x11111111U) * 0x11111111U;
		p = (v >> 28) & 1;
		if ((g_opt_flags & OPT_FLAGS_VERIFY) && (p != parity)) {
			pr_fail("%s: parity error detected, using the "
				"multiply Shapira method\n",  name);
			return EXIT_FAILURE;
		}

		/*
		 * "Compute parity in parallel"
		 * https://graphics.stanford.edu/~seander/bithacks.html
		 */
		v = val;
		v ^= v >> 16;
		v ^= v >> 8;
		v ^= v >> 4;
		v &= 0xf;
		p = (0x6996 >> v) & 1;
		if ((g_opt_flags & OPT_FLAGS_VERIFY) && (p != parity)) {
			pr_fail("%s: parity error detected, using "
				"the parallel method\n",  name);
			return EXIT_FAILURE;
		}

		/*
		 * "Compute parity by lookup table"
		 * https://graphics.stanford.edu/~seander/bithacks.html
		 * Variation #1
		 */
		v = val;
		v ^= v >> 16;
		v ^= v >> 8;
		p = stress_cpu_parity_table[v & 0xff];
		if ((g_opt_flags & OPT_FLAGS_VERIFY) && (p != parity)) {
			pr_fail("%s: parity error detected, using "
				"the lookup method, variation 1\n",  name);
			return EXIT_FAILURE;
		}

		/*
		 * "Compute parity by lookup table"
		 * https://graphics.stanford.edu/~seander/bithacks.html
		 * Variation #2
		 */
		u.v32 = val;
		p = stress_cpu_parity_table[u.v8[0] ^ u.v8[1] ^ u.v8[2] ^ u.v8[3]];
		if ((g_opt_flags & OPT_FLAGS_VERIFY) && (p != parity)) {
			pr_fail("%s: parity error detected, using the "
				"lookup method, variation 2\n",  name);
			return EXIT_FAILURE;
		}
#if defined(HAVE_BUILTIN_PARITY)
		/*
		 *  Compute parity using built-in function
		 */
		p = (uint32_t)__builtin_parity((unsigned int)val);
		if ((g_opt_flags & OPT_FLAGS_VERIFY) && (p != parity)) {
			pr_fail("%s: parity error detected, using "
				"the __builtin_parity function\n",  name);
			return EXIT_FAILURE;
		}
#endif
	}
	return EXIT_SUCCESS;
}

/*
 *  stress_cpu_dither
 *	perform 8 bit to 1 bit gray scale
 *	Floyd-Steinberg dither
 */
static int TARGET_CLONES stress_cpu_dither(const char *name)
{
	size_t x, y;

	(void)name;

	/*
	 *  Generate some random 8 bit image
	 */
	for (y = 0; y < STRESS_CPU_DITHER_Y; y += 8) {
PRAGMA_UNROLL_N(8)
		for (x = 0; x < STRESS_CPU_DITHER_X; x ++) {
			register uint32_t v1, v2;

			v1 = stress_mwc32();
			pixels[x][y + 0] = (uint8_t)v1;
			v1 >>= 8;
			pixels[x][y + 1] = (uint8_t)v1;
			v1 >>= 8;
			pixels[x][y + 2] = (uint8_t)v1;
			v1 >>= 8;
			pixels[x][y + 3] = (uint8_t)v1;

			v2 = stress_mwc32();
			pixels[x][y + 4] = (uint8_t)v2;
			v2 >>= 8;
			pixels[x][y + 5] = (uint8_t)v2;
			v2 >>= 8;
			pixels[x][y + 6] = (uint8_t)v2;
			v2 >>= 8;
			pixels[x][y + 7] = (uint8_t)v2;
		}
	}

	/*
	 *  ..and dither
	 */
	for (y = 0; y < STRESS_CPU_DITHER_Y; y++) {
		for (x = 0; x < STRESS_CPU_DITHER_X; x++) {
			const uint8_t pixel = pixels[x][y];
			const uint8_t quant = (pixel < 128) ? 0 : 255;
			const int32_t error = pixel - quant;

			const bool xok1 = x < (STRESS_CPU_DITHER_X - 1);
			const bool xok2 = x > 0;
			const bool yok1 = y < (STRESS_CPU_DITHER_Y - 1);

			if (xok1)
				pixels[x + 1][y] +=
					(error * 7) >> 4;
			if (xok2 && yok1)
				pixels[x - 1][y + 1] +=
					(error * 3) >> 4;
			if (yok1)
				pixels[x][y + 1] +=
					(error * 5) >> 4;
			if (xok1 && yok1)
				pixels[x + 1][y + 1] +=
					error >> 4;
		}
	}
	return EXIT_SUCCESS;
}

/*
 *  stress_cpu_div8
 *	perform 50000 x 8 bit divisions, these are traditionally
 *	slow ops
 */
static int TARGET_CLONES stress_cpu_div8(const char *name)
{
	register uint16_t i = 50000, j = 0;
	const uint8_t delta = 0xff / 224;
	uint8_t sum = 0;

	(void)name;

	while (i > 0) {
		const uint8_t n = (uint8_t)STRESS_MINIMUM(i, 224);
		register uint8_t k, l;

		for (l = 0, k = 1; l < n; l++, k += delta) {
			register uint8_t r = (uint8_t)(j / k);
			sum += r;
		}
		i -= n;
		j += delta;
	}
	stress_uint8_put(sum);

	return EXIT_SUCCESS;
}

/*
 *  stress_cpu_div16
 *	perform 50000 x 16 bit divisions, these are traditionally
 *	slow ops
 */
static int TARGET_CLONES stress_cpu_div16(const char *name)
{
	register uint16_t i = 50000, j = 0;
	const uint16_t delta = 0xffff / 224;
	uint16_t sum = 0;

	(void)name;

	while (i > 0) {
		const uint16_t n = STRESS_MINIMUM(i, 224);
		register uint16_t k, l;

		for (l = 0, k = 1; l < n; l++, k += delta) {
			register const uint16_t r = j / k;

			sum += r;
		}
		i -= n;
		j += delta;
	}
	stress_uint16_put(sum);

	return EXIT_SUCCESS;
}

/*
 *  stress_cpu_div32
 *	perform 50000 x 32 bit divisions, these are traditionally
 *	slow ops
 */
static int TARGET_CLONES stress_cpu_div32(const char *name)
{
	register uint32_t i = 50000, j = 0;
	const uint32_t delta = 0xffffffff / 224;
	uint32_t sum = 0;

	(void)name;

	while (i > 0) {
		const uint32_t n = STRESS_MINIMUM(i, 224);
		register uint32_t k, l;

		for (l = 0, k = 1; l < n; l++, k += delta) {
			register const uint32_t r = j / k;

			sum += r;
		}
		i -= n;
		j += delta;
	}
	stress_uint32_put(sum);

	return EXIT_SUCCESS;
}

/*
 *  stress_cpu_div64
 *	perform 50000 x 64 bit divisions, these are traditionally
 *	slow ops
 */
static int TARGET_CLONES stress_cpu_div64(const char *name)
{
	register uint64_t i = 50000, j = 0;
	const uint64_t delta = 0xffffffffffffffffULL / 224;
	uint64_t sum = 0;

	(void)name;

	while (i > 0) {
		const uint64_t n = STRESS_MINIMUM(i, 224);
		register uint64_t k, l;

		for (l = 0, k = 1; l < n; l++, k += delta) {
			register const uint64_t r = j / k;

			sum += r;
		}
		i -= n;
		j += delta;
	}
	stress_uint64_put(sum);

	return EXIT_SUCCESS;
}

#if defined(HAVE_INT128_T)
/*
 *  stress_cpu_div128
 *	perform 50000 x 128 bit divisions, these are traditionally
 *	slow ops
 */
static int TARGET_CLONES stress_cpu_div128(const char *name)
{
	register __uint128_t i = 50000, j = 0;
	const uint64_t delta64 = 0xffffffffffffffffULL;
	const __uint128_t delta = STRESS_UINT128(delta64, delta64) / 224;
	__uint128_t sum = 0;

	(void)name;

	while (i > 0) {
		const __uint128_t n = STRESS_MINIMUM(i, 224);
		register __uint128_t k, l;

		for (l = 0, k = 1; l < n; l++, k += delta) {
			register const __uint128_t r = j / k;

			sum += r;
		}
		i -= n;
		j += delta;
	}
	stress_uint64_put((uint64_t)sum);

	return EXIT_SUCCESS;
}
#endif

/*
 *  stress_cpu_union
 *	perform bit field operations on a union
 */
static int TARGET_CLONES stress_cpu_union(const char *name)
{
	typedef union {
		struct {
			uint64_t	b1:1;
			uint64_t	b10:10;
			uint64_t	b2:2;
			uint64_t	b9:9;
			uint64_t	b3:3;
			uint64_t	b8:8;
			uint64_t	b4:4;
			uint64_t	b7:7;
			uint64_t	b5:5;
			uint64_t	b6:6;
		} bits64;
		uint64_t	u64:64;
		union {
			uint8_t		b1:1;
			uint8_t		b7:7;
			uint8_t		b8:8;
		} bits8;
		struct {
			uint16_t	b15:15;
			uint16_t	b1:1;
		} bits16;
		struct {
			uint32_t	b10:10;
			uint32_t	b20:20;
#if defined(HAVE_COMPILER_TCC)
			uint32_t	f:1;	/* cppcheck-suppress unusedStructMember */
#else
			uint32_t	:1;	/* cppcheck-suppress unusedStructMember */
#endif
			uint32_t	b1:1;
		} bits32;
		uint32_t	u32:30;
	} stress_u_t;

	static stress_u_t u;
	size_t i;

	(void)name;
	for (i = 0; i < 1000; i++) {
		u.bits64.b1 ^= 1;
		u.bits64.b2--;
		u.bits32.b10 ^= ~0;
		u.bits64.b3++;
		u.bits16.b1--;
		u.bits8.b1++;
		u.bits64.b4 *= 2;
		u.bits32.b20 += 3;
		u.u64 += 0x1037fc2ae21ef829ULL;
		u.bits64.b6--;
		u.bits8.b7 *= 3;
		u.bits64.b5 += (u.bits64.b4 << 1);
		u.bits32.b1 ^= 1;
		u.bits64.b7++;
		/*
		 *  The following operation on .b8 causes an assembler
		 *  warning with pcc:
		 *  "/tmp/ctm.sd7Pfx:12897: Warning: 0xffffffffffffff00 shortened to 0x0"
		 */
		u.bits8.b8 ^= 0xaa;
		u.bits64.b8--;
		u.bits16.b15 ^= 0xbeef;
		u.bits64.b9++;
		u.bits64.b10 *= 5;
		u.u32 += 1;
	}
	return EXIT_SUCCESS;
}

/*
 *  Solution from http://www.cl.cam.ac.uk/~mr10/backtrk.pdf
 *     see section 2.1
 */
static uint32_t queens_try(
	uint32_t left_diag,
	uint32_t cols,
	uint32_t right_diag,
	uint32_t all)
{
	register uint32_t solutions = 0;
	register uint32_t poss = ~(left_diag | cols | right_diag) & all;

	while (poss) {
		register uint32_t inv = -poss;
		register uint32_t bit = poss & inv;
		register uint32_t new_cols = cols | bit;

		poss -= bit;
		solutions += (new_cols == all) ?
			1 : queens_try((left_diag | bit) << 1,
				new_cols, (right_diag | bit) >> 1, all);
	}
	return solutions;
}


/*
 *  stress_cpu_queens
 *	solve the queens problem for sizes 1..11
 */
static int stress_cpu_queens(const char *name)
{
	uint32_t all, n;

	static const uint32_t queens_solutions[] = {
		0, 1, 0, 0, 2, 10, 4, 40, 92, 352, 724, 2680, 14200
	};

	for (all = 1, n = 1; n < 12; n++) {
		const uint32_t solutions = queens_try(0, 0, 0, all);

		if ((g_opt_flags & OPT_FLAGS_VERIFY) &&
		    (solutions != queens_solutions[n])) {
			pr_fail("%s: queens solution error detected "
				"on board size %" PRIu32 "\n",
				name, n);
			return EXIT_FAILURE;
		}
		all = (all + all) + 1;
	}
	return EXIT_SUCCESS;
}

/*
 *  stress_cpu_factorial
 *	find factorials from 1..150 using
 *	Stirling's and Ramanujan's Approximations.
 */
static int OPTIMIZE_FAST_MATH stress_cpu_factorial(const char *name)
{
	int n;
	long double f = 1.0L;
	const long double precision = 1.0e-6L;
	const long double sqrt_pi = shim_sqrtl(PI);

	for (n = 1; n < 150; n++) {
		const long double np1 = (long double)(n + 1);
		long double fact = shim_roundl(shim_expl(shim_lgammal(np1)));
		long double dn;

		f *= (long double)n;

		/* Stirling */
		if ((g_opt_flags & OPT_FLAGS_VERIFY) &&
		    ((f - fact) / fact > precision)) {
			pr_fail("%s: Stirling's approximation of factorial(%d) out of range\n",
				name, n);
			return EXIT_FAILURE;
		}

		/* Ramanujan */
		dn = (long double)n;
		fact = sqrt_pi * shim_powl((dn / (long double)M_E), dn);
		fact *= shim_powl((((((((8 * dn) + 4)) * dn) + 1) * dn) + 1.0L / 30.0L), (1.0L / 6.0L));
		if ((g_opt_flags & OPT_FLAGS_VERIFY) &&
		    ((f - fact) / fact > precision)) {
			pr_fail("%s: Ramanujan's approximation of factorial(%d) out of range\n",
				name, n);
			return EXIT_FAILURE;
		}
	}
	return EXIT_SUCCESS;
}

/*
 *  stress_cpu_stats
 *	Exercise some standard stats computations on random data
 */
static int stress_cpu_stats(const char *name)
{
	size_t i;
	double data[STATS_MAX];
	double min, max, am = 0.0, gm, hm = 0.0, stddev = 0.0;
	int64_t expon = 0;
	double mant = 1.0;
	const double inverse_n = 1.0 / (double)STATS_MAX;

	for (i = 0; i < STATS_MAX; i++)
		data[i] = ((double)(stress_mwc32() + 1)) / 4294967296.0;

	min = max = data[0];

	for (i = 0; i < STATS_MAX; i++) {
		int e;
		const double d = data[i];
		const double f = frexp(d, &e);

		mant *= f;
		expon += e;

		if (min > d)
			min = d;
		if (max < d)
			max = d;

		am += d;
		hm += 1 / d;
	}
	/* Arithmetic mean (average) */
	am = am / STATS_MAX;
	/* Geometric mean */
	gm = pow(mant, inverse_n) *
	     pow(2.0, (double)expon * inverse_n);
	/* Harmonic mean */
	hm = STATS_MAX / hm;

	for (i = 0; i < STATS_MAX; i++) {
		const double d = data[i] - am;

		stddev += (d * d);
	}
	/* Standard Deviation */
	stddev = shim_sqrt(stddev);

	stress_double_put(am);
	stress_double_put(gm);
	stress_double_put(hm);
	stress_double_put(stddev);

	if (min > hm) {
		pr_fail("%s: stats: minimum %f > harmonic mean %f\n",
			name, min, hm);
		return EXIT_FAILURE;
	}
	if (hm > gm) {
		pr_fail("%s: stats: harmonic mean %f > geometric mean %f\n",
			name, hm, gm);
		return EXIT_FAILURE;
	}
	if (gm > am) {
		pr_fail("%s: stats: geometric mean %f > arithmetic mean %f\n",
			name, gm, am);
		return EXIT_FAILURE;
	}
	if (am > max) {
		pr_fail("%s: stats: arithmetic mean %f > maximum %f\n",
			name, am, max);
		return EXIT_FAILURE;
	}

	return EXIT_SUCCESS;
}

/*
 *  stress_cpu_all()
 *	dummy function, not called
 */
static int OPTIMIZE3 stress_cpu_all(const char *name)
{
	(void)name;

	return EXIT_SUCCESS;
}

/*
 * Table of cpu stress methods
 */
static const stress_cpu_method_info_t stress_cpu_methods[] = {
	{ "all",		stress_cpu_all,			10000.0	},	/* Special "all test */

	{ "ackermann",		stress_cpu_ackermann,		2008.64	},
	{ "apery",		stress_cpu_apery,		9344.95	},
	{ "bitops",		stress_cpu_bitops,		6573.24	},
	{ "callfunc",		stress_cpu_callfunc,		1246.34 },
#if defined(HAVE_COMPLEX_H) &&		\
    defined(HAVE_COMPLEX) &&		\
    defined(__STDC_IEC_559_COMPLEX__) &&\
    !defined(__UCLIBC__)
	{ "cdouble",		stress_cpu_complex_double,	8969.40 },
	{ "cfloat",		stress_cpu_complex_float,	2656.19 },
	{ "clongdouble",	stress_cpu_complex_long_double,	794.78 },
#else
	UNEXPECTED
#endif
	{ "collatz",		stress_cpu_collatz,		860193.45 },
	{ "correlate",		stress_cpu_correlate,		216.02 },
	{ "crc16",		stress_cpu_crc16,		249.93 },
#if defined(HAVE_Decimal32) &&	\
    !defined(HAVE_COMPILER_CLANG)
	{ "decimal32",		stress_cpu_decimal32,		724.47 },
#endif
#if defined(HAVE_Decimal64) &&	\
    !defined(HAVE_COMPILER_CLANG)
	{ "decimal64",		stress_cpu_decimal64,		916.39 },
#endif
#if defined(HAVE_Decimal128) &&	\
    !defined(HAVE_COMPILER_CLANG)
	{ "decimal128",		stress_cpu_decimal128,		330.52 },
#endif
	{ "dither",		stress_cpu_dither,		234.77 },
	{ "div8",		stress_cpu_div8,		8815.50 },
	{ "div16",		stress_cpu_div16,		8843.39 },
	{ "div32",		stress_cpu_div32,		9937.25 },
	{ "div64",		stress_cpu_div64,		2833.72 },
#if defined(HAVE_INT128_T)
	{ "div128",		stress_cpu_div128,		1536.66 },
#endif
	{ "double",		stress_cpu_double,		11172.11 },
	{ "euler",		stress_cpu_euler,		26895846.22 },
	{ "explog",		stress_cpu_explog,		343.85 },
	{ "factorial",		stress_cpu_factorial,		9474.40 },
	{ "fibonacci",		stress_cpu_fibonacci,		35976352.02 },
#if defined(HAVE_COMPLEX_H) &&		\
    defined(HAVE_COMPLEX) &&		\
    defined(__STDC_IEC_559_COMPLEX__) &&\
    !defined(__UCLIBC__)
	{ "fft",		stress_cpu_fft,			1516.20 },
#endif
	{ "fletcher16",		stress_cpu_fletcher16,		650.59 },
	{ "float",		stress_cpu_float,		11085.77 },
#if defined(HAVE_fp16) &&	\
    !defined(HAVE_COMPILER_CLANG)
	{ "float16",		stress_cpu_float16,		8885.55 },
#endif
#if defined(HAVE_Float32) &&	\
    !defined(HAVE_COMPILER_CLANG)
	{ "float32",		stress_cpu_float32,		8885.55 },
#endif
#if defined(HAVE_Float64) &&	\
    !defined(HAVE_COMPILER_CLANG)
	{ "float64",		stress_cpu_float64,		10582.13},
#endif
#if defined(HAVE__float80) &&	\
    !defined(HAVE_COMPILER_CLANG)
	{ "float80",		stress_cpu_float80,		1699.80 },
#endif
#if defined(HAVE__float128) &&	\
    !defined(HAVE_COMPILER_CLANG)
	{ "float128",		stress_cpu_float128,		725.41 },
#elif defined(HAVE_Float128) &&	\
    !defined(HAVE_COMPILER_CLANG)
	{ "float128",		stress_cpu_float128,		725.41 },
#endif
	{ "floatconversion",	stress_cpu_floatconversion,	2705.07 },
	{ "gamma",		stress_cpu_gamma,		492.74 },
	{ "gcd",		stress_cpu_gcd,			1586.81 },
	{ "gray",		stress_cpu_gray,		62248.86 },
	{ "hamming",		stress_cpu_hamming,		1036.58 },
	{ "hanoi",		stress_cpu_hanoi,		54804.26 },
	{ "hyperbolic",		stress_cpu_hyperbolic,		1556.08 },
	{ "idct",		stress_cpu_idct,		71989.07 },
#if defined(HAVE_INT128_T)
	{ "int128",		stress_cpu_int128,		30658.82 },
#endif
	{ "int64",		stress_cpu_int64,		61950.47 },
	{ "int32",		stress_cpu_int32,		65527.15 },
	{ "int16",		stress_cpu_int16,		65656.46 },
	{ "int8",		stress_cpu_int8,		65610.66 },
#if defined(HAVE_INT128_T)
	{ "int128float",	stress_cpu_int128_float,	18312.51 },
	{ "int128double",	stress_cpu_int128_double,	9798.38 },
	{ "int128longdouble",	stress_cpu_int128_longdouble,	1397.33 },
#if defined(HAVE_Decimal32) &&	\
    !defined(HAVE_COMPILER_CLANG)
	{ "int128decimal32",	stress_cpu_int128_decimal32,	1696.86 },
#endif
#if defined(HAVE_Decimal64) &&	\
    !defined(HAVE_COMPILER_CLANG)
	{ "int128decimal64",	stress_cpu_int128_decimal64,	2242.01 },
#endif
#if defined(HAVE_Decimal128) &&	\
    !defined(HAVE_COMPILER_CLANG)
	{ "int128decimal128",	stress_cpu_int128_decimal128,	347.19 },
#endif
#endif
	{ "int64float",		stress_cpu_int64_float,		25297.02 },
	{ "int64double",	stress_cpu_int64_double,	11390.72 },
	{ "int64longdouble",	stress_cpu_int64_longdouble,	1399.64 },
	{ "int32float",		stress_cpu_int32_float,		26033.76 },
	{ "int32double",	stress_cpu_int32_double,	11617.23 },
	{ "int32longdouble",	stress_cpu_int32_longdouble,	1408.24 },
	{ "intconversion",	stress_cpu_intconversion,	1390.75 },
	{ "ipv4checksum",	stress_cpu_ipv4checksum,	23394.01 },
	{ "jmp",		stress_cpu_jmp,			122704.94 },
	{ "lfsr32",		stress_cpu_lfsr32,		52110.43 },
	{ "ln2",		stress_cpu_ln2,			118136.97 },
	{ "logmap",		stress_cpu_logmap,		23417.85 },
	{ "longdouble",		stress_cpu_longdouble,		1801.60 },
	{ "loop",		stress_cpu_loop,		31424.67 },
	{ "matrixprod",		stress_cpu_matrix_prod,		263.84 },
	{ "nsqrt",		stress_cpu_nsqrt,		32783.07 },
	{ "omega",		stress_cpu_omega,		4038977.72 },
	{ "parity",		stress_cpu_parity,		30013.93 },
	{ "phi",		stress_cpu_phi,			20462354.45 },
	{ "pi",			stress_cpu_pi,			469787.65 },
	{ "prime",		stress_cpu_prime,		443.99 },
	{ "psi",		stress_cpu_psi,			6121992.09 },
	{ "queens",		stress_cpu_queens,		637.03 },
	{ "rand",		stress_cpu_rand,		16530.45 },
#if defined(HAVE_SRAND48) &&	\
    defined(HAVE_LRAND48) &&	\
    defined(HAVE_DRAND48)
	{ "rand48",		stress_cpu_rand48,		5037.84 },
#endif
	{ "rgb",		stress_cpu_rgb,			71888.49 },
	{ "sieve",		stress_cpu_sieve,		3437.93 },
	{ "stats",		stress_cpu_stats,		428002.41 },
	{ "sqrt", 		stress_cpu_sqrt,		5821.64 },
	{ "trig",		stress_cpu_trig,		1200.32 },
	{ "union",		stress_cpu_union,		30434.40 },
#if defined(HAVE_COMPLEX_H) &&		\
    defined(HAVE_COMPLEX) &&		\
    defined(__STDC_IEC_559_COMPLEX__) &&\
    !defined(__UCLIBC__)
	{ "zeta",		stress_cpu_zeta,		1339.96 },
#endif
};

static double stress_cpu_counter_scale[SIZEOF_ARRAY(stress_cpu_methods)];

static int stress_call_cpu_method(size_t method, stress_args_t *args, double *counter)
{
	int rc;

	if (method == 0) {
		static size_t i = 1;	/* Skip over stress_cpu_all */

		method = i;
		i++;
		if (i >= SIZEOF_ARRAY(stress_cpu_methods))
			i = 1;
	}
	rc = stress_cpu_methods[method].func(args->name);
	*counter += stress_cpu_counter_scale[method];
	stress_bogo_set(args, (uint64_t)*counter);

	return rc;
}

/*
 *  stress_per_cpu_time()
 *	try to get accurage CPU time from CPUTIME clock,
 *	or fall back to wall clock time if not possible.
 */
static double stress_per_cpu_time(void)
{
#if defined(CLOCK_PROCESS_CPUTIME_ID)
	struct timespec ts;
	static bool use_clock_gettime = true;

	/*
	 *  Where possible try to get time used on the CPU
	 *  rather than wall clock time to get more accurate
	 *  CPU consumption measurements
	 */
	if (use_clock_gettime) {
		if (clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &ts) == 0) {
			return (double)ts.tv_sec + ((double)ts.tv_nsec) / (double)STRESS_NANOSECOND;
		} else {
			use_clock_gettime = false;
		}
	}
#endif
	/*
	 *  Can't get CPU clock time, fall back to wall clock time
	 */
	return stress_time_now();
}

/*
 *  stress_cpu()
 *	stress CPU by doing floating point math ops
 */
static int OPTIMIZE3 stress_cpu(stress_args_t *args)
{
	double bias;
	size_t cpu_method = 0;
	int32_t cpu_load = 100;
	int32_t cpu_load_slice = -64;
	double counter = 0.0;
	bool cpu_old_metrics = false;
	size_t i;
	int rc = EXIT_SUCCESS;

	stress_catch_sigill();

	(void)stress_get_setting("cpu-load-slice", &cpu_load_slice);
	(void)stress_get_setting("cpu-old-metrics", &cpu_old_metrics);
	(void)stress_get_setting("cpu-method", &cpu_method);
	if (stress_get_setting("cpu-load", &cpu_load)) {
		if (cpu_method == 0)
			pr_inf("%s: for stable load results, select a "
				"specific cpu stress method with "
				"--cpu-method other than 'all'\n",
				args->name);
	}

	if (cpu_old_metrics) {
		for (i = 0; i < SIZEOF_ARRAY(stress_cpu_counter_scale); i++)
			stress_cpu_counter_scale[i] = 1.0;
	} else {
		for (i = 0; i < SIZEOF_ARRAY(stress_cpu_counter_scale); i++)
			stress_cpu_counter_scale[i] = 1484.50 / stress_cpu_methods[i].bogo_op_rate;
	}

	if (stress_instance_zero(args))
		pr_dbg("%s: using method '%s'\n", args->name, stress_cpu_methods[cpu_method].name);

	/*
	 * It is unlikely, but somebody may request to do a zero
	 * load stress test(!)
	 */
	if (cpu_load == 0) {
		(void)sleep((unsigned int)g_opt_timeout);

		stress_set_proc_state(args->name, STRESS_STATE_DEINIT);
		return EXIT_SUCCESS;
	}

	stress_set_proc_state(args->name, STRESS_STATE_SYNC_WAIT);
	stress_sync_start_wait(args);
	stress_set_proc_state(args->name, STRESS_STATE_RUN);


	/*
	 * Normal use case, 100% load, simple spinning on CPU
	 */
	if (cpu_load == 100) {
		stress_cpu_disable_fp_subnormals();
		do {
			rc = stress_call_cpu_method(cpu_method, args, &counter);
		} while ((rc == EXIT_SUCCESS) && stress_continue(args));
		stress_cpu_enable_fp_subnormals();

		stress_set_proc_state(args->name, STRESS_STATE_DEINIT);
		return rc;
	}

	/*
	 * More complex percentage CPU utilisation.  This is
	 * not intended to be 100% accurate timing, it is good
	 * enough for most purposes.
	 */
	stress_cpu_disable_fp_subnormals();
	bias = 0.0;
	do {
		double delay_cpu_clock, t1_cpu_clock, t2_cpu_clock;
		double delay_wall_clock, t1_wall_clock, t2_wall_clock;
		double delay;
#if defined(HAVE_SELECT)
		struct timeval tv;
#endif

		t1_wall_clock = stress_time_now();
		t1_cpu_clock = stress_per_cpu_time();
		if (cpu_load_slice < 0) {
			/* < 0 specifies number of iterations to do per slice */
			int j;

			for (j = 0; j < -cpu_load_slice; j++) {
				rc = stress_call_cpu_method(cpu_method, args, &counter);
				if ((rc != EXIT_SUCCESS) || !stress_continue_flag())
					break;
			}
			t2_wall_clock = stress_time_now();
			t2_cpu_clock = stress_per_cpu_time();
		} else if (cpu_load_slice == 0) {
			/* == 0, random time slices */
			const uint16_t r = stress_mwc16();
			double slice_end = t1_cpu_clock + ((double)r / 131072.0);
			do {
				rc = stress_call_cpu_method(cpu_method, args, &counter);
				t2_wall_clock = stress_time_now();
				t2_cpu_clock = stress_per_cpu_time();
				if ((rc != EXIT_SUCCESS) || !stress_continue_flag())
					break;
			} while (t2_cpu_clock < slice_end);
		} else {
			/* > 0, time slice in milliseconds */
			const double slice_end = t1_cpu_clock + ((double)cpu_load_slice / STRESS_DBL_MILLISECOND);

			do {
				rc = stress_call_cpu_method(cpu_method, args, &counter);
				t2_wall_clock = stress_time_now();
				t2_cpu_clock = stress_per_cpu_time();
				if ((rc != EXIT_SUCCESS) || !stress_continue_flag())
					break;
			} while (t2_cpu_clock < slice_end);
		}

		/* Must not calculate this with zero % load */
		delay_cpu_clock = t2_cpu_clock - t1_cpu_clock;
		delay_wall_clock = t2_wall_clock - t1_wall_clock;
		delay = (((100 - cpu_load) * delay_cpu_clock) / (double)cpu_load) + (delay_cpu_clock - delay_wall_clock);
		delay -= bias;

		/* We may have clock warping so don't sleep for -ve delays */
		if (delay < 0.0) {
			bias = 0.0;
		} else {
			/*
			 *  We need to sleep for a small amount of
			 *  time, measurements need to be based on
			 *  wall clock time and NOT on cpu time used.
			 */
			double t3_wall_clock;

			t2_wall_clock = stress_time_now();

#if defined(HAVE_SELECT)
			tv.tv_sec = (time_t)delay;
			tv.tv_usec = (long)((delay - (double)tv.tv_sec) * STRESS_DBL_MICROSECOND);
			(void)select(0, NULL, NULL, NULL, &tv);
#else
			(void)shim_nanosleep_uint64((uint64_t)(delay * STRESS_DBL_NANOSECOND));
#endif

			t3_wall_clock = stress_time_now();
			/* Bias takes account of the time to do the delay */
			bias = (t3_wall_clock - t2_wall_clock) - delay;
		}
	} while ((rc == EXIT_SUCCESS) && stress_continue(args));
	stress_cpu_enable_fp_subnormals();

	if (stress_is_affinity_set() && (stress_instance_zero(args))) {
		pr_inf("%s: CPU affinity probably set, this can affect CPU loading\n",
			args->name);
	}

	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);

	return rc;
}

static const char *stress_cpu_method(const size_t i)
{
	return (i <  SIZEOF_ARRAY(stress_cpu_methods)) ? stress_cpu_methods[i].name : NULL;
}

static const stress_opt_t opts[] = {
	{ OPT_cpu_load,        "cpu-load",        TYPE_ID_INT32, 0, 100, NULL },
	{ OPT_cpu_load_slice,  "cpu-load-slice",  TYPE_ID_INT32, (uint64_t)-5000, (uint64_t)5000, NULL },
	{ OPT_cpu_method,      "cpu-method",      TYPE_ID_SIZE_T_METHOD, 0, 0, stress_cpu_method },
	{ OPT_cpu_old_metrics, "cpu-old-metrics", TYPE_ID_BOOL,  0, 1, NULL },
	END_OPT,
};

const stressor_info_t stress_cpu_info = {
	.stressor = stress_cpu,
	.classifier = CLASS_CPU | CLASS_COMPUTE,
	.opts = opts,
	.verify = VERIFY_OPTIONAL,
	.help = help
};
