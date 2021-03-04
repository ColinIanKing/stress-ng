/*
 * Copyright (C) 2013-2021 Canonical, Ltd.
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
 * This code is a complete clean re-write of the stress tool by
 * Colin Ian King <colin.king@canonical.com> and attempts to be
 * backwardly compatible with the stress tool by Amos Waterland
 * <apw@rossby.metr.ou.edu> but has more stress tests and more
 * functionality.
 *
 */
#include "stress-ng.h"

#define GAMMA 		(0.57721566490153286060651209008240243104215933593992L)
#define OMEGA		(0.56714329040978387299996866221035554975381578718651L)
#define PSI		(3.359885666243177553172011302918927179688905133732L)

#define STATS_MAX		(250)
#define FFT_SIZE		(4096)
#define STRESS_CPU_DITHER_X	(1024)
#define STRESS_CPU_DITHER_Y	(768)
#define MATRIX_PROD_SIZE 	(128)
#define CORRELATE_DATA_LEN	(8192)
#define CORRELATE_LEN		(CORRELATE_DATA_LEN / 16)
#define SIEVE_SIZE              (104730)

/*
 * Some awful math lib workarounds for functions that some
 * math libraries don't have implemented (yet)
 */
#if !defined(HAVE_CABSL)
#define cabsl	cabs
#endif

#if !defined(HAVE_LGAMMAL)
#define lgammal	lgamma
#endif

#if !defined(HAVE_CCOSL)
#define	ccosl	ccos
#endif

#if !defined(HAVE_CSINL)
#define	csinl	csin
#endif

#if !defined(HAVE_CPOW)
#define cpow	pow
#endif

#if !defined(HAVE_POWL)
#define powl	pow
#endif

#if !defined(HAVE_RINTL)
#define rintl	rint
#endif

#if !defined(HAVE_LOGL)
#define logl	log
#endif

#if !defined(HAVE_EXPL) || defined(__HAIKU__)
#define expl	exp
#endif

#if !defined(HAVE_COSL)
#define cosl	cos
#endif

#if !defined(HAVE_SINL)
#define	sinl	sin
#endif

#if !defined(HAVE_COSHL)
#define coshl	cosh
#endif

#if !defined(HAVE_SINHL)
#define	sinhl	sinh
#endif

#if !defined(HAVE_SQRTL)
#define sqrtl	sqrt
#endif

/*
 *  the CPU stress test has different classes of cpu stressor
 */
typedef void (*stress_cpu_func)(const char *name);

typedef struct {
	const char		*name;	/* human readable form of stressor */
	const stress_cpu_func	func;	/* the cpu method function */
} stress_cpu_method_info_t;

static const stress_help_t help[] = {
	{ "c N", "cpu N",		"start N workers spinning on sqrt(rand())" },
	{ NULL,  "cpu-ops N",		"stop after N cpu bogo operations" },
	{ "l P", "cpu-load P",		"load CPU by P %, 0=sleep, 100=full load (see -c)" },
	{ NULL,	 "cpu-load-slice S",	"specify time slice during busy load" },
	{ NULL,  "cpu-method M",	"specify stress cpu method M, default is all" },
	{ NULL,	 NULL,			NULL }
};

static const stress_cpu_method_info_t cpu_methods[];

/* Don't make this static to ensure dithering does not get optimised out */
uint8_t pixels[STRESS_CPU_DITHER_X][STRESS_CPU_DITHER_Y];

static int stress_set_cpu_load(const char *opt) {
	int32_t cpu_load;

	cpu_load = stress_get_int32(opt);
	stress_check_range("cpu-load", cpu_load, 0, 100);
	return stress_set_setting("cpu-load", TYPE_ID_INT32, &cpu_load);
}

/*
 *  stress_set_cpu_load_slice()
 *	< 0   - number of iterations per busy slice
 *	= 0   - random duration between 0..0.5 seconds
 *	> 0   - milliseconds per busy slice
 */
static int stress_set_cpu_load_slice(const char *opt)
{
	int32_t cpu_load_slice;

	cpu_load_slice = stress_get_int32(opt);
	if ((cpu_load_slice < -5000) || (cpu_load_slice > 5000)) {
		(void)fprintf(stderr, "cpu-load-slice must in the range -5000 to 5000.\n");
		_exit(EXIT_FAILURE);
	}
	return stress_set_setting("cpu-load-slice", TYPE_ID_INT32, &cpu_load_slice);
}

/*
 *  stress_cpu_sqrt()
 *	stress CPU on square roots
 */
static void HOT TARGET_CLONES stress_cpu_sqrt(const char *name)
{
	int i;

	for (i = 0; i < 16384; i++) {
		uint64_t rnd = stress_mwc32();
		double r_d = sqrt((double)rnd) * sqrt((double)rnd);
		long double r_ld = sqrtl((long double)rnd) * sqrtl((long double)rnd);

		if (UNLIKELY((g_opt_flags & OPT_FLAGS_VERIFY) &&
		    (uint64_t)rint(r_d) != rnd)) {
			pr_fail("%s: sqrt error detected on "
				"sqrt(%" PRIu64 ")\n", name, rnd);
			if (!keep_stressing_flag())
				break;
		}

		if (UNLIKELY((g_opt_flags & OPT_FLAGS_VERIFY) &&
		    (uint64_t)rint(r_ld) != rnd)) {
			pr_fail("%s: sqrtf error detected on "
				"sqrt(%" PRIu64 ")\n", name, rnd);
			if (!keep_stressing_flag())
				break;
		}
	}
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
static void OPTIMIZE0 stress_cpu_loop(const char *name)
{
	uint32_t i, i_sum = 0;
	const uint32_t sum = 134209536UL;

	for (i = 0; i < 16384; i++) {
		i_sum += i;
		FORCE_DO_NOTHING();
	}
	if ((g_opt_flags & OPT_FLAGS_VERIFY) && (i_sum != sum))
		pr_fail("%s: cpu loop 0..16383 sum was %" PRIu32 " and "
			"did not match the expected value of %" PRIu32 "\n",
			name, i_sum, sum);
}

/*
 *  stress_cpu_gcd()
 *	compute Greatest Common Divisor
 */
static void HOT OPTIMIZE3 TARGET_CLONES stress_cpu_gcd(const char *name)
{
	uint32_t i, gcd_sum = 0;
	const uint32_t gcd_checksum = 63000868UL;
	uint64_t lcm_sum = 0;
	const uint64_t lcm_checksum = 41637399273ULL;

	for (i = 0; i < 16384; i++) {
		register uint32_t a = i, b = i % (3 + (1997 ^ i));
		register uint64_t lcm = ((uint64_t)a * b);

		while (b != 0) {
			register uint32_t r = b;
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
	    (lcm_sum != lcm_checksum))
		pr_fail("%s: gcd error detected, failed modulo "
			"or assignment operations\n", name);
}

/*
 *  stress_cpu_bitops()
 *	various bit manipulation hacks from bithacks
 *	https://graphics.stanford.edu/~seander/bithacks.html
 */
static void HOT OPTIMIZE3 TARGET_CLONES stress_cpu_bitops(const char *name)
{
	uint32_t i, i_sum = 0;
	const uint32_t sum = 0x8aac0aab;

	for (i = 0; i < 16384; i++) {
		{
			register uint32_t r, v, s = (sizeof(v) * 8) - 1;

			/* Reverse bits */
			r = v = i;
			for (v >>= 1; v; v >>= 1, s--) {
				r <<= 1;
				r |= v & 1;
			}
			r <<= s;
			i_sum += r;
		}
		{
			/* parity check */
			register uint32_t v = i;

			v ^= v >> 16;
			v ^= v >> 8;
			v ^= v >> 4;
			v &= 0xf;
			i_sum += (0x6996 >> v) & 1;
		}
		{
			/* Brian Kernighan count bits */
			register uint32_t j, v = i;

			for (j = 0; v; j++)
				v &= v - 1;
			i_sum += j;
		}
		{
			/* round up to nearest highest power of 2 */
			register uint32_t v = i - 1;

			v |= v >> 1;
			v |= v >> 2;
			v |= v >> 4;
			v |= v >> 8;
			v |= v >> 16;
			i_sum += v;
		}
	}
	if ((g_opt_flags & OPT_FLAGS_VERIFY) && (i_sum != sum))
		pr_fail("%s: bitops error detected, failed "
			"bitops operations\n", name);
}

/*
 *  stress_cpu_trig()
 *	simple sin, cos trig functions
 */
static void HOT stress_cpu_trig(const char *name)
{
	int i;
	long double d_sum = 0.0L;

	(void)name;

	for (i = 0; i < 1500; i++) {
		long double theta = (2.0L * M_PI * (double)i)/1500.0L;
		{
			d_sum += (cosl(theta) * sinl(theta));
			d_sum += (cos(theta) * sin(theta));
			d_sum += (cosf(theta) * sinf(theta));
		}
		{
			long double theta2 = theta * 2.0L;

			d_sum += cosl(theta2);
			d_sum += cos(theta2);
			d_sum += cosf(theta2);
		}
		{
			long double theta3 = theta * 3.0L;

			d_sum += sinl(theta3);
			d_sum += sin(theta3);
			d_sum += sinf(theta3);
		}
	}
	stress_double_put(d_sum);
}

/*
 *  stress_cpu_hyperbolic()
 *	simple hyperbolic sinh, cosh functions
 */
static void HOT stress_cpu_hyperbolic(const char *name)
{
	int i;
	double d_sum = 0.0;

	(void)name;

	for (i = 0; i < 1500; i++) {
		long double theta = (2.0L * M_PI * (double)i)/1500.0L;
		{
			d_sum += (coshl(theta) * sinhl(theta));
			d_sum += (cosh(theta) * sinh(theta));
			d_sum += (double)(coshf(theta) * sinhf(theta));
		}
		{
			long double theta2 = theta * 2.0L;

			d_sum += coshl(theta2);
			d_sum += cosh(theta2);
			d_sum += (double)coshf(theta2);
		}
		{
			long double theta3 = theta * 3.0L;

			d_sum += sinhl(theta3);
			d_sum += sinh(theta3);
			d_sum += (double)sinhf(theta3);
		}
	}
	stress_double_put(d_sum);
}

/*
 *  stress_cpu_rand()
 *	generate lots of pseudo-random integers
 */
static void HOT OPTIMIZE3 stress_cpu_rand(const char *name)
{
	int i;
	uint32_t i_sum = 0;
	const uint32_t sum = 0xc253698c;

	STRESS_MWC_SEED();
	for (i = 0; i < 16384; i++)
		i_sum += stress_mwc32();

	if ((g_opt_flags & OPT_FLAGS_VERIFY) && (i_sum != sum))
		pr_fail("%s: rand error detected, failed sum of "
			"pseudo-random values\n", name);
}

/*
 *  stress_cpu_rand48()
 *	generate random values using rand48 family of functions
 */
static void HOT OPTIMIZE3 stress_cpu_rand48(const char *name)
{
	int i;
	double d = 0;
	long int l = 0;

	(void)name;

	srand48(0x0defaced);
	for (i = 0; i < 16384; i++) {
		d += drand48();
		l += lrand48();
	}
	stress_double_put(d);
	stress_uint64_put(l);
}

/*
 *  stress_cpu_nsqrt()
 *	iterative Newton–Raphson square root
 */
static void HOT OPTIMIZE3 TARGET_CLONES stress_cpu_nsqrt(const char *name)
{
	int i;
	const long double precision = 1.0e-12L;
	const int max_iter = 56;

	for (i = 16300; i < 16384; i++) {
		long double n = (double)i;
		long double lo = (n < 1.0L) ? n : 1.0L;
		long double hi = (n < 1.0L) ? 1.0L : n;
		long double rt;
		int j = 0;

		while ((j++ < max_iter) && ((hi - lo) > precision)) {
			long double g = (lo + hi) / 2.0L;
			if ((g * g) > n)
				hi = g;
			else
				lo = g;
		}
		rt = (lo + hi) / 2.0L;

		if (g_opt_flags & OPT_FLAGS_VERIFY) {
			if (j >= max_iter)
				pr_fail("%s: Newton-Raphson sqrt "
					"computation took more iterations "
					"than expected\n", name);
			if ((int)rintl(rt * rt) != i)
				pr_fail("%s: Newton-Raphson sqrt not "
					"accurate enough\n", name);
		}
	}
}

/*
 *  stress_cpu_phi()
 *	compute the Golden Ratio
 */
static void HOT OPTIMIZE3 TARGET_CLONES stress_cpu_phi(const char *name)
{
	long double phi; /* Golden ratio */
	const long double precision = 1.0e-15L;
	const long double phi_ = (1.0L + sqrtl(5.0L)) / 2.0L;
	register uint64_t a, b;
	const uint64_t mask = 1ULL << 63;
	int i;

	/* Pick any two starting points */
	a = stress_mwc64() % 99;
	b = stress_mwc64() % 99;

	/* Iterate until we approach overflow */
	for (i = 0; (i < 64) && !((a | b) & mask); i++) {
		/* Find nth term */
		register uint64_t c = a + b;

		a = b;
		b = c;
	}
	/* And we have the golden ratio */
	phi = (long double)b / (long double)a;

	if ((g_opt_flags & OPT_FLAGS_VERIFY) &&
	    (fabsl(phi - phi_) > precision))
		pr_fail("%s: Golden Ratio phi not accurate enough\n",
			name);
}

/*
 *  stress_cpu_apery()
 *      compute Apéry's constant
 */
static void HOT OPTIMIZE3 stress_cpu_apery(const char *name)
{
	uint32_t n;
	long double a = 0.0, a_;
	const long double precision = 1.0e-14L;

	(void)name;

	for (n = 1; n < 100000; n++) {
		long double n3 = (long double)n;

		a_ = a;
		n3 = n3 * n3 * n3;
		a += (1.0L / n3);
		if (fabsl(a - a_) < precision)
			break;
	}
	if (fabsl(a - a_) > precision)
		pr_fail("%s: Apéry's const not accurate enough\n", name);
}


#if defined(HAVE_COMPLEX_H) &&		\
    defined(HAVE_COMPLEX) &&		\
    defined(__STDC_IEC_559_COMPLEX__) &&\
    !defined(__UCLIBC__)
/*
 *  fft_partial()
 *  	partial Fast Fourier Transform
 */
static void HOT OPTIMIZE3 fft_partial(
	double complex *data,
	double complex *tmp,
	const int n,
	const int m)
{
	if (m < n) {
		const int m2 = m * 2;
		int i;

		fft_partial(tmp, data, n, m2);
		fft_partial(tmp + m, data + m, n, m2);
		for (i = 0; i < n; i += m2) {
			const double complex negI = -I;
			double complex v = tmp[i];
			double complex t =
				cexp((negI * M_PI * (double)i) /
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
static void HOT TARGET_CLONES stress_cpu_fft(const char *name)
{
	static double complex buf[FFT_SIZE], tmp[FFT_SIZE];
	int i;

	(void)name;

	for (i = 0; i < FFT_SIZE; i++)
		buf[i] = (double complex)(i % 63);

	(void)memcpy(tmp, buf, sizeof(*tmp) * FFT_SIZE);
	fft_partial(buf, tmp, FFT_SIZE, 1);
}
#endif

/*
 *   stress_cpu_euler()
 *	compute e using series
 */
static void HOT OPTIMIZE3 TARGET_CLONES stress_cpu_euler(const char *name)
{
	long double e = 1.0L, last_e;
	long double fact = 1.0L;
	long double precision = 1.0e-20L;
	int n = 1;

	do {
		last_e = e;
		fact *= n;
		n++;
		e += (1.0L / fact);
	} while ((n < 25) && (fabsl(e - last_e) > precision));

	if ((g_opt_flags & OPT_FLAGS_VERIFY) && (n >= 25))
		pr_fail("%s: Euler computation took more iterations "
			"than expected\n", name);
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
		uint32_t v = stress_mwc32();

		*data++ = v;
		v >>= 8;
		*data++ = v;
		v >>= 8;
		*data++ = v;
		v >>= 8;
		*data++ = v;
	}
}

/*
 *  stress_cpu_collatz()
 *	stress test integer collatz conjecture
 */
static void HOT OPTIMIZE3 TARGET_CLONES stress_cpu_collatz(const char *name)
{
	register uint64_t n = 989345275647ULL;	/* Has 1348 steps in cycle */
	register int i;

	for (i = 0; n != 1; i++) {
		n = (n & 1) ? (3 * n) + 1 : n / 2;
	}
	if ((g_opt_flags & OPT_FLAGS_VERIFY) && (i != 1348))
		pr_fail("%s: error detected, failed collatz progression\n",
			name);
}

/*
 *  stress_cpu_hash_generic()
 *	stress test generic string hash function
 */
static void stress_cpu_hash_generic(
	const char *name,
	const char *hash_name,
	uint32_t (*hash_func)(const char *str),
	const uint32_t result)
{
	char buffer[128];
	size_t i;
	uint32_t i_sum = 0;

	STRESS_MWC_SEED();
	random_buffer((uint8_t *)buffer, sizeof(buffer));
	/* Make it ASCII range ' '..'_' */
	for (i = 0; i < sizeof(buffer); i++)
		buffer[i] = (buffer[i] & 0x3f) + ' ';

	for (i = sizeof(buffer) - 1; i; i--) {
		buffer[i] = '\0';
		i_sum += hash_func(buffer);
	}
	if ((g_opt_flags & OPT_FLAGS_VERIFY) && (i_sum != result))
		pr_fail("%s: %s error detected, failed hash %s sum\n",
			name, hash_name, hash_name);
}

/*
 *  stress_cpu_jenkin()
 *	multiple iterations on jenkin hash
 */
static void stress_cpu_jenkin(const char *name)
{
	uint8_t buffer[128];
	size_t i;
	uint32_t i_sum = 0;
	const uint32_t sum = 0xc53302a5;

	STRESS_MWC_SEED();
	random_buffer(buffer, sizeof(buffer));

	for (i = sizeof(buffer) - 1; i; i--) {
		buffer[i] = '\0';
		i_sum += stress_hash_jenkin(buffer, sizeof(buffer));
	}

	if ((g_opt_flags & OPT_FLAGS_VERIFY) && (i_sum != sum))
		pr_fail("%s: jenkin error detected, failed hash jenkin sum\n",
			name);
}

/*
 *  stress_cpu_little_endian()
 *	returns true if CPU is little endian
 */
static inline bool stress_cpu_little_endian(void)
{
	const uint32_t x = 0x12345678;
	const uint8_t *y = (const uint8_t *)&x;

	return *y == 0x78;
}

/*
 *  stress_cpu_murmur3_32
 *	 multiple iterations on murmur3_32 hash, based on
 *	 Austin Appleby's Murmur3 hash, code derived from
 *	 https://en.wikipedia.org/wiki/MurmurHash
 */
static void stress_cpu_murmur3_32(const char *name)
{
	uint8_t buffer[128];
	size_t i;
	uint32_t sum, i_sum = 0;
	const uint32_t seed = 0xf12b35e1; /* arbitrary value */

	STRESS_MWC_SEED();
	random_buffer(buffer, sizeof(buffer));
	for (i = sizeof(buffer) - 1; i; i--) {
		buffer[i] = '\0';
		i_sum += stress_hash_murmur3_32((uint8_t *)buffer, sizeof(buffer), seed);
	}

	/*
	 *  Murmur produces different results depending on the Endianness
	 */
	sum = stress_cpu_little_endian() ? 0xa53a4bb1 : 0x71eb83cc;

	if ((g_opt_flags & OPT_FLAGS_VERIFY) && (i_sum != sum))
		pr_fail("%s: murmur3_32 error detected, failed hash murmur3_32 sum\n",
			name);
}

/*
 *  stress_cpu_pjw()
 *	stress test hash pjw
 */
static void stress_cpu_pjw(const char *name)
{
	stress_cpu_hash_generic(name, "pjw", stress_hash_pjw, 0xa89a91c0);
}

/*
 *  stress_cpu_djb2a()
 *	stress test hash djb2a
 */
static void stress_cpu_djb2a(const char *name)
{
	stress_cpu_hash_generic(name, "djb2a", stress_hash_djb2a, 0x6a60cb5a);
}

/*
 *  stress_cpu_fnv1a()
 *	stress test hash fnv1a
 */
static void HOT stress_cpu_fnv1a(const char *name)
{
	stress_cpu_hash_generic(name, "fnv1a", stress_hash_fnv1a, 0x8ef17e80);
}

/*
 *  stress_cpu_sdbm()
 *	stress test hash sdbm
 */
static void stress_cpu_sdbm(const char *name)
{
	stress_cpu_hash_generic(name, "sdbm", stress_hash_sdbm, 0x46357819);
}

/*
 *  stress_cpu_idct()
 *	compute 8x8 Inverse Discrete Cosine Transform
 */
static void HOT OPTIMIZE3 TARGET_CLONES stress_cpu_idct(const char *name)
{
	const double invsqrt2 = 1.0 / sqrt(2.0);
	const double pi_over_16 = M_PI / 16.0;
	const int sz = 8;
	int i, j, u, v;
	float data[sz][sz], idct[sz][sz];

	/*
	 *  Set up DCT
	 */
	for (i = 0; i < sz; i++) {
		for (j = 0; j < sz; j++) {
			data[i][j] = (i + j == 0) ? 2040: 0;
		}
	}
	for (i = 0; i < sz; i++) {
		const double pi_i = (i + i + 1) * pi_over_16;

		for (j = 0; j < sz; j++) {
			const double pi_j = (j + j + 1) * pi_over_16;
			double sum = 0.0;

			for (u = 0; u < sz; u++) {
				const double cos_pi_i_u = cos(pi_i * u);

				for (v = 0; v < sz; v++) {
					const double cos_pi_j_v =
						cos(pi_j * v);

					sum += (data[u][v] *
						(u ? 1.0 : invsqrt2) *
						(v ? 1.0 : invsqrt2) *
						cos_pi_i_u * cos_pi_j_v);
				}
			}
			idct[i][j] = 0.25 * sum;
		}
	}
	/* Final output should be a 8x8 matrix of values 255 */
	if (g_opt_flags & OPT_FLAGS_VERIFY) {
		for (i = 0; i < sz; i++) {
			for (j = 0; j < sz; j++) {
				if ((int)idct[i][j] != 255) {
					pr_fail("%s: IDCT error detected, "
						"IDCT[%d][%d] was %d, "
						"expecting 255\n",
						name, i, j, (int)idct[i][j]);
				}
			}
			if (!keep_stressing_flag())
				return;
		}
	}
}

#define int_ops(a, b, c1, c2, c3)	\
	do {				\
		a += b;			\
		b ^= a;			\
		a >>= 1;		\
		b <<= 2;		\
		b -= a;			\
		a ^= ~0;		\
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
#define stress_cpu_int(_type, _sz, _a, _b, _c1, _c2, _c3)	\
static void HOT OPTIMIZE3 TARGET_CLONES stress_cpu_int ## _sz(const char *name)\
{								\
	const _type mask = ~0;					\
	const _type a_final = _a;				\
	const _type b_final = _b;				\
	const _type c1 = _c1 & mask;				\
	const _type c2 = _c2 & mask;				\
	const _type c3 = _c3 & mask;				\
	register _type a, b;					\
	int i;							\
								\
	STRESS_MWC_SEED();					\
	a = stress_mwc32();					\
	b = stress_mwc32();					\
								\
	for (i = 0; i < 1000; i++) {				\
		int_ops(a, b, c1, c2, c3)			\
	}							\
								\
	if ((g_opt_flags & OPT_FLAGS_VERIFY) &&			\
	    ((a != a_final) || (b != b_final)))			\
		pr_fail("%s: int" # _sz " error detected, " 	\
			"failed int" # _sz 			\
			" math operations\n", name);		\
}								\

/* For compilers that support int128 .. */
#if defined(HAVE_INT128_T)

#define _UINT128(hi, lo)	((((__uint128_t)hi << 64) | (__uint128_t)lo))

stress_cpu_int(__uint128_t, 128,
	_UINT128(0x132af604d8b9183a,0x5e3af8fa7a663d74),
	_UINT128(0x62f086e6160e4e,0xd84c9f800365858),
	_UINT128(C1, C1), _UINT128(C2, C2), _UINT128(C3, C3))
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

#define float_ops(_type, a, b, c, d, _sin, _cos)	\
	do {						\
		a = a + b;				\
		b = a * c;				\
		c = a - b;				\
		d = a / b;				\
		a = c / (_type)0.1923L;			\
		b = c + a;				\
		c = b * (_type)3.12L;			\
		d = d + b + (_type)_sin(a);		\
		a = (b + c) / c;			\
		b = b * c;				\
		c = c + (_type)1.0L;			\
		d = d - (_type)_sin(c);			\
		a = a * (_type)_cos(b);			\
		b = b + (_type)_cos(c);			\
		c = (_type)_sin(a + b) / (_type)2.344L;	\
		b = d - (_type)1.0L;			\
	} while (0)

/*
 *  Generic floating point stressor macro
 */
#define stress_cpu_fp(_type, _name, _sin, _cos)		\
static void HOT OPTIMIZE3 TARGET_CLONES stress_cpu_ ## _name(const char *name)\
{							\
	int i;						\
	_type a = 0.18728L, b = stress_mwc32(), 	\
	      c = stress_mwc32(), d;			\
							\
	(void)name;					\
							\
	for (i = 0; i < 1000; i++) {			\
		float_ops(_type, a, b, c, d,		\
			_sin, _cos);			\
	}						\
	stress_double_put(a + b + c + d);		\
}

stress_cpu_fp(float, float, sinf, cosf)
stress_cpu_fp(double, double, sin, cos)
stress_cpu_fp(long double, longdouble, sinl, cosl)
#if defined(HAVE_FLOAT_DECIMAL32) && !defined(__clang__)
stress_cpu_fp(_Decimal32, decimal32, sinf, cosf)
#endif
#if defined(HAVE_FLOAT_DECIMAL64) && !defined(__clang__)
stress_cpu_fp(_Decimal64, decimal64, sin, cos)
#endif
#if defined(HAVE_FLOAT_DECIMAL128) && !defined(__clang__)
stress_cpu_fp(_Decimal128, decimal128, sinl, cosl)
#endif
#if defined(HAVE_FLOAT16) && !defined(__clang__)
stress_cpu_fp(__fp16, float16, sin, cos)
#endif
#if defined(HAVE_FLOAT32) && !defined(__clang__)
stress_cpu_fp(_Float32, float32, sin, cos)
#endif
#if defined(HAVE_FLOAT80) && !defined(__clang__)
stress_cpu_fp(__float80, float80, sinl, cosl)
#endif
#if defined(HAVE_FLOAT128) && !defined(__clang__)
stress_cpu_fp(__float128, float128, sinl, cosl)
#endif

/* Append floating point literal specifier to literal value */
#define FP(val, ltype)	val ## ltype

#if defined(HAVE_COMPLEX_H) &&		\
    defined(HAVE_COMPLEX) &&		\
    defined(__STDC_IEC_559_COMPLEX__) &&\
    !defined(__UCLIBC__)
/*
 *  Generic complex stressor macro
 */
#define stress_cpu_complex(_type, _ltype, _name, _csin, _ccos)	\
static void HOT OPTIMIZE3 TARGET_CLONES stress_cpu_ ## _name(const char *name)\
{								\
	int i;							\
	_type cI = I;						\
	_type a = FP(0.18728, _ltype) + 			\
		cI * FP(0.2762, _ltype),			\
		b = stress_mwc32() - cI * FP(0.11121, _ltype),	\
		c = stress_mwc32() + cI * stress_mwc32(), d;	\
								\
	(void)name;						\
								\
	for (i = 0; i < 1000; i++) {				\
		float_ops(_type, a, b, c, d,			\
			_csin, _ccos);				\
	}							\
	stress_double_put(a + b + c + d);			\
}

stress_cpu_complex(complex float, f, complex_float, csinf, ccosf)
stress_cpu_complex(complex double, , complex_double, csin, ccos)
stress_cpu_complex(complex long double, l, complex_long_double, csinl, ccosl)
#endif

#define int_float_ops(_ftype, flt_a, flt_b, flt_c, flt_d,	\
	_sin, _cos, int_a, int_b, _c1, _c2, _c3)		\
	do {							\
		int_a += int_b;					\
		int_b ^= int_a;					\
		flt_a = flt_a + flt_b;				\
		int_a >>= 1;					\
		int_b <<= 2;					\
		flt_b = flt_a * flt_c;				\
		int_b -= int_a;					\
		int_a ^= ~0;					\
		flt_c = flt_a - flt_b;				\
		int_b ^= ~(_c1);				\
		int_a *= 3;					\
		flt_d = flt_a / flt_b;				\
		int_b *= 7;					\
		int_a += 2;					\
		flt_a = flt_c / (_ftype)0.1923L;		\
		int_b -= 3;					\
		int_a /= 77;					\
		flt_b = flt_c + flt_a;				\
		int_b /= 3;					\
		int_a <<= 1;					\
		flt_c = flt_b * (_ftype)3.12L;			\
		int_b <<= 2;					\
		int_a |= 1;					\
		flt_d = flt_d + flt_b + (_ftype)_sin(flt_a);	\
		int_b |= 3;					\
		int_a *= stress_mwc32();			\
		flt_a = (flt_b + flt_c) / flt_c;		\
		int_b ^= stress_mwc32();			\
		int_a += stress_mwc32();			\
		flt_b = flt_b * flt_c;				\
		int_b -= stress_mwc32();			\
		int_a /= 7;					\
		flt_c = flt_c + (_ftype)1.0L;			\
		int_b /= 9;					\
		flt_d = flt_d - (_ftype)_sin(flt_c);		\
		int_a |= (_c2);					\
		flt_a = flt_a * (_ftype)_cos(flt_b);		\
		flt_b = flt_b + (_ftype)_cos(flt_c);		\
		int_b &= (_c3);					\
		flt_c = (_ftype)_sin(flt_a + flt_b) / (_ftype)2.344L;	\
		flt_b = flt_d - (_ftype)1.0L;			\
	} while (0)


/*
 *  Generic integer and floating point stressor macro
 */
#define stress_cpu_int_fp(_inttype, _sz, _ftype, _name, _a, _b, \
	_c1, _c2, _c3, _sinf, _cosf)				\
static void HOT OPTIMIZE3 TARGET_CLONES stress_cpu_int ## _sz ## _ ## _name(const char *name)\
{								\
	int i;							\
	_inttype int_a, int_b;					\
	const _inttype mask = ~0;				\
	const _inttype a_final = _a;				\
	const _inttype b_final = _b;				\
	const _inttype c1 = _c1 & mask;				\
	const _inttype c2 = _c2 & mask;				\
	const _inttype c3 = _c3 & mask;				\
	_ftype flt_a = 0.18728L, flt_b = stress_mwc32(),	\
		flt_c = stress_mwc32(), flt_d;			\
								\
	STRESS_MWC_SEED();					\
	int_a = stress_mwc32();					\
	int_b = stress_mwc32();					\
								\
	for (i = 0; i < 1000; i++) {				\
		int_float_ops(_ftype, flt_a, flt_b, flt_c, flt_d,\
			_sinf, _cosf, int_a, int_b, c1, c2, c3);\
	}							\
	if ((g_opt_flags & OPT_FLAGS_VERIFY) &&			\
	    ((int_a != a_final) || (int_b != b_final)))		\
		pr_fail("%s: int" # _sz " error detected, "	\
			"failed int" # _sz "" # _ftype		\
			" math operations\n", name);		\
								\
	stress_double_put(flt_a + flt_b + flt_c + flt_d);	\
}

stress_cpu_int_fp(uint32_t, 32, float, float,
	0x1ce9b547UL, 0xa24b33aUL,
	C1, C2, C3, sinf, cosf)
stress_cpu_int_fp(uint32_t, 32, double, double,
	0x1ce9b547UL, 0xa24b33aUL,
	C1, C2, C3, sin, cos)
stress_cpu_int_fp(uint32_t, 32, long double, longdouble,
	0x1ce9b547UL, 0xa24b33aUL,
	C1, C2, C3, sinl, cosl)
stress_cpu_int_fp(uint64_t, 64, float, float,
	0x13f7f6dc1d79197cULL, 0x1863d2c6969a51ceULL,
	C1, C2, C3, sinf, cosf)
stress_cpu_int_fp(uint64_t, 64, double, double,
	0x13f7f6dc1d79197cULL, 0x1863d2c6969a51ceULL,
	C1, C2, C3, sin, cos)
stress_cpu_int_fp(uint64_t, 64, long double, longdouble,
	0x13f7f6dc1d79197cULL, 0x1863d2c6969a51ceULL,
	C1, C2, C3, sinl, cosl)

#if defined(HAVE_INT128_T)
stress_cpu_int_fp(__uint128_t, 128, float, float,
	_UINT128(0x132af604d8b9183a,0x5e3af8fa7a663d74),
	_UINT128(0x0062f086e6160e4e,0x0d84c9f800365858),
	_UINT128(C1, C1), _UINT128(C2, C2), _UINT128(C3, C3),
	sinf, cosf)
stress_cpu_int_fp(__uint128_t, 128, double, double,
	_UINT128(0x132af604d8b9183a,0x5e3af8fa7a663d74),
	_UINT128(0x0062f086e6160e4e,0x0d84c9f800365858),
	_UINT128(C1, C1), _UINT128(C2, C2), _UINT128(C3, C3),
	sin, cos)
stress_cpu_int_fp(__uint128_t, 128, long double, longdouble,
	_UINT128(0x132af604d8b9183a,0x5e3af8fa7a663d74),
	_UINT128(0x0062f086e6160e4e,0x0d84c9f800365858),
	_UINT128(C1, C1), _UINT128(C2, C2), _UINT128(C3, C3),
	sinl, cosl)
#if defined(HAVE_FLOAT_DECIMAL32) && !defined(__clang__)
stress_cpu_int_fp(__uint128_t, 128, _Decimal32, decimal32,
	_UINT128(0x132af604d8b9183a,0x5e3af8fa7a663d74),
	_UINT128(0x0062f086e6160e4e,0x0d84c9f800365858),
	_UINT128(C1, C1), _UINT128(C2, C2), _UINT128(C3, C3),
	(_Decimal32)sinf, (_Decimal32)cosf)
#endif
#if defined(HAVE_FLOAT_DECIMAL64) && !defined(__clang__)
stress_cpu_int_fp(__uint128_t, 128, _Decimal64, decimal64,
	_UINT128(0x132af604d8b9183a,0x5e3af8fa7a663d74),
	_UINT128(0x0062f086e6160e4e,0x0d84c9f800365858),
	_UINT128(C1, C1), _UINT128(C2, C2), _UINT128(C3, C3),
	(_Decimal64)sin, (_Decimal64)cos)
#endif
#if defined(HAVE_FLOAT_DECIMAL128) && !defined(__clang__)
stress_cpu_int_fp(__uint128_t, 128, _Decimal128, decimal128,
	_UINT128(0x132af604d8b9183a,0x5e3af8fa7a663d74),
	_UINT128(0x0062f086e6160e4e,0x0d84c9f800365858),
	_UINT128(C1, C1), _UINT128(C2, C2), _UINT128(C3, C3),
	(_Decimal128)sinl, (_Decimal128)cosl)
#endif
#endif

/*
 *  stress_cpu_rgb()
 *	CCIR 601 RGB to YUV to RGB conversion
 */
static void HOT OPTIMIZE3 TARGET_CLONES stress_cpu_rgb(const char *name)
{
	int i;
	uint32_t rgb = stress_mwc32() & 0xffffff;
	uint8_t r = rgb >> 16;
	uint8_t g = rgb >> 8;
	uint8_t b = rgb;

	(void)name;

	/* Do a 1000 colours starting from the rgb seed */
	for (i = 0; i < 1000; i++) {
		float y, u, v;

		/* RGB to CCIR 601 YUV */
		y = (0.299f * r) + (0.587f * g) + (0.114f * b);
		u = (b - y) * 0.565f;
		v = (r - y) * 0.713f;

		/* YUV back to RGB */
		r = y + (1.403f * v);
		g = y - (0.344f * u) - (0.714f * v);
		b = y + (1.770f * u);

		/* And bump each colour to make next round */
		r += 1;
		g += 2;
		b += 3;
		stress_uint64_put(r + g + b);
	}
}

/*
 *  stress_cpu_matrix_prod(void)
 *	matrix product
 */
static void HOT OPTIMIZE3 TARGET_CLONES stress_cpu_matrix_prod(const char *name)
{
	int i, j, k;

	static long double a[MATRIX_PROD_SIZE][MATRIX_PROD_SIZE],
		    	   b[MATRIX_PROD_SIZE][MATRIX_PROD_SIZE],
		    	   r[MATRIX_PROD_SIZE][MATRIX_PROD_SIZE];
	long double v = 1 / (long double)((uint32_t)~0);
	long double sum = 0.0L;

	(void)name;

	for (i = 0; i < MATRIX_PROD_SIZE; i++) {
		for (j = 0; j < MATRIX_PROD_SIZE; j++) {
			a[i][j] = (long double)stress_mwc32() * v;
			b[i][j] = (long double)stress_mwc32() * v;
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
		for (j = 0; j < MATRIX_PROD_SIZE; j++)
			sum += r[i][j];
	stress_double_put(sum);
}

/*
 *   stress_cpu_fibonacci()
 *	compute fibonacci series
 */
static void HOT OPTIMIZE3 stress_cpu_fibonacci(const char *name)
{
	const uint64_t fn_res = 0xa94fad42221f2702ULL;
	register uint64_t f1 = 0, f2 = 1, fn;

	do {
		fn = f1 + f2;
		f1 = f2;
		f2 = fn;
	} while (!(fn & 0x8000000000000000ULL));

	if ((g_opt_flags & OPT_FLAGS_VERIFY) && (fn_res != fn))
		pr_fail("%s: fibonacci error detected, summation "
			"or assignment failure\n", name);
}

/*
 *  stress_cpu_psi
 *	compute the constant psi,
 * 	the reciprocal Fibonacci constant
 */
static void HOT OPTIMIZE3 stress_cpu_psi(const char *name)
{
	long double f1 = 0.0L, f2 = 1.0L;
	long double psi = 0.0L, last_psi;
	long double precision = 1.0e-20L;
	int i = 0;
	const int max_iter = 100;

	do {
		long double fn = f1 + f2;
		f1 = f2;
		f2 = fn;
		last_psi = psi;
		psi += 1.0L / f1;
		i++;
	} while ((i < max_iter) && (fabsl(psi - last_psi) > precision));

	if (g_opt_flags & OPT_FLAGS_VERIFY) {
		if (fabsl(psi - PSI) > 1.0e-15L)
			pr_fail("%s: calculation of reciprocal "
				"Fibonacci constant phi not as accurate "
				"as expected\n", name);
		if (i >= max_iter)
			pr_fail("%s: calculation of reciprocal "
				"Fibonacci constant took more iterations "
				"than expected\n", name);
	}

	stress_double_put(psi);
}

/*
 *   stress_cpu_ln2
 *	compute ln(2) using series
 */
static void HOT OPTIMIZE3 TARGET_CLONES stress_cpu_ln2(const char *name)
{
	long double ln2 = 0.0L, last_ln2 = 0.0L;
	long double precision = 1.0e-7L;
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
	} while ((n < max_iter) && (fabsl(ln2 - last_ln2) > precision));

	if ((g_opt_flags & OPT_FLAGS_VERIFY) && (n >= max_iter))
		pr_fail("%s: calculation of ln(2) took more "
			"iterations than expected\n", name);

	stress_double_put(ln2);
}

/*
 *  ackermann()
 *	a naive/simple implementation of the ackermann function
 */
static uint32_t HOT ackermann(const uint32_t m, const uint32_t n)
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
static void stress_cpu_ackermann(const char *name)
{
	uint32_t a = ackermann(3, 7);

	if ((g_opt_flags & OPT_FLAGS_VERIFY) && (a != 0x3fd))
		pr_fail("%s: ackermann error detected, "
			"ackermann(3,9) miscalculated\n", name);
}

/*
 *   stress_cpu_explog
 *	compute exp(log(n))
 */
static void HOT stress_cpu_explog(const char *name)
{
	uint32_t i;
	double n = 1e6;

	(void)name;

	for (i = 1; i < 100000; i++)
		n = exp(log(n) / 1.00002);
}

/*
 *  This could be a ternary operator, v = (v op val) ? a : b
 *  but it may be optimised down, so force a compare and jmp
 *  with -O0 and a if/else construct
 */
#define JMP(v, op, val, a, b)		\
	if (v op val)			\
		v = a;			\
	else				\
		v = b;			\
	stress_uint64_put(next + i);	\

/*
 *   stress_cpu_jmp
 *	jmp conditionals
 */
static void HOT OPTIMIZE0 stress_cpu_jmp(const char *name)
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
}

/*
 *  ccitt_crc16()
 *	perform naive CCITT CRC16
 */
static uint16_t HOT OPTIMIZE3 ccitt_crc16(const uint8_t *data, size_t n)
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
	register uint16_t crc = ~0;

	if (!n)
		return 0;

	for (; n; n--) {
		uint8_t i;
		uint8_t val = (uint16_t)0xff & *data++;

		for (i = 8; i; --i, val >>= 1) {
			bool do_xor = 1 & (val ^ crc);
			crc >>= 1;
			crc ^= do_xor ? polynomial : 0;
		}
	}

	crc = ~crc;
	return (crc << 8) | (crc >> 8);
}

/*
 *   stress_cpu_crc16
 *	compute 1024 rounds of CCITT CRC16
 */
static void stress_cpu_crc16(const char *name)
{
	uint8_t buffer[1024];
	size_t i;

	(void)name;

	random_buffer(buffer, sizeof(buffer));
	for (i = 0; i < sizeof(buffer); i++)
		stress_uint64_put(ccitt_crc16(buffer, i));
}

/*
 *  fletcher16
 *	naive implementation of fletcher16 checksum
 */
static uint16_t HOT OPTIMIZE3 fletcher16(const uint8_t *data, const size_t len)
{
	register uint16_t sum1 = 0, sum2 = 0;
	register size_t i;

	for (i = 0; i < len; i++) {
		sum1 = (sum1 + data[i]) % 255;
		sum2 = (sum2 + sum1) % 255;
	}
	return (sum2 << 8) | sum1;
}

/*
 *   stress_cpu_fletcher16()
 *	compute 1024 rounds of fletcher16 checksum
 */
static void stress_cpu_fletcher16(const char *name)
{
	uint8_t buffer[1024];
	size_t i;

	(void)name;

	random_buffer((uint8_t *)buffer, sizeof(buffer));
	for (i = 0; i < sizeof(buffer); i++)
		stress_uint16_put(fletcher16(buffer, i));
}

/*
 *   stress_cpu_ipv4checksum
 *	compute 1024 rounds of IPv4 checksum
 */
static void stress_cpu_ipv4checksum(const char *name)
{
	uint16_t buffer[512];
	size_t i;

	(void)name;

	random_buffer((uint8_t *)buffer, sizeof(buffer));
	for (i = 0; i < sizeof(buffer); i++)
		stress_uint16_put(stress_ipv4_checksum(buffer, i));
}

#if defined(HAVE_COMPLEX_H) &&		\
    defined(HAVE_COMPLEX) &&		\
    defined(__STDC_IEC_559_COMPLEX__) &&\
    !defined(__UCLIBC__)
/*
 *  zeta()
 *	Riemann zeta function
 */
static inline long double complex HOT OPTIMIZE3 zeta(
	const long double complex s,
	long double precision)
{
	int i = 1;
	long double complex z = 0.0L, zold = 0.0L;

	do {
		zold = z;
		z += 1 / cpow(i++, s);
	} while (cabsl(z - zold) > precision);

	return z;
}

/*
 * stress_cpu_zeta()
 *	stress test Zeta(2.0)..Zeta(10.0)
 */
static void stress_cpu_zeta(const char *name)
{
	long double precision = 0.00000001L;
	int i;

	(void)name;

	for (i = 2; i < 11; i++)
		stress_double_put(zeta((double complex)i, precision));
}
#endif

/*
 * stress_cpu_gamma()
 *	stress Euler–Mascheroni constant gamma
 */
static void HOT OPTIMIZE3 stress_cpu_gamma(const char *name)
{
	long double precision = 1.0e-10L;
	long double sum = 0.0L, k = 1.0L, _gamma = 0.0L, gammaold;

	do {
		gammaold = _gamma;
		sum += 1.0L / k;
		_gamma = sum - logl(k);
		k += 1.0L;
	} while (k < 1e6 && fabsl(_gamma - gammaold) > precision);

	stress_double_put(_gamma);

	if (g_opt_flags & OPT_FLAGS_VERIFY) {
		if (fabsl(_gamma - GAMMA) > 1.0e-5L)
			pr_fail("%s: calculation of Euler-Mascheroni "
				"constant not as accurate as expected\n", name);
		if (k > 80000.0L)
			pr_fail("%s: calculation of Euler-Mascheroni "
				"constant took more iterations than "
				"expected\n", name);
	}

}

/*
 * stress_cpu_correlate()
 *
 *  Introduction to Signal Processing,
 *  Prentice-Hall, 1995, ISBN: 0-13-209172-0.
 */
static void HOT OPTIMIZE3 stress_cpu_correlate(const char *name)
{
	size_t i, j;
	double data_average = 0.0;
	static double data[CORRELATE_DATA_LEN];
	static double corr[CORRELATE_LEN + 1];

	(void)name;

	/* Generate some random data */
	for (i = 0; i < CORRELATE_DATA_LEN; i++) {
		data[i] = stress_mwc64();
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
}


/*
 * stress_cpu_sieve()
 * 	slightly optimised Sieve of Eratosthenes
 */
static void HOT OPTIMIZE3 stress_cpu_sieve(const char *name)
{
	const uint32_t nsqrt = sqrt(SIEVE_SIZE);
	static uint32_t sieve[(SIEVE_SIZE + 31) / 32];
	uint32_t i, j;

	(void)memset(sieve, 0xff, sizeof(sieve));
	for (i = 2; i < nsqrt; i++)
		if (STRESS_GETBIT(sieve, i))
			for (j = i * i; j < SIEVE_SIZE; j += i)
				STRESS_CLRBIT(sieve, j);

	/* And count up number of primes */
	for (j = 0, i = 2; i < SIEVE_SIZE; i++) {
		if (STRESS_GETBIT(sieve, i))
			j++;
	}
	if ((g_opt_flags & OPT_FLAGS_VERIFY) && (j != 10000))
		pr_fail("%s: sieve error detected, number of "
			"primes has been miscalculated\n", name);
}

/*
 *  is_prime()
 *	return true if n is prime
 *	http://en.wikipedia.org/wiki/Primality_test
 */
static inline HOT OPTIMIZE3 ALWAYS_INLINE int is_prime(uint32_t n)
{
	register uint32_t i, max;

	if (UNLIKELY(n <= 3))
		return n >= 2;
	if ((n % 2 == 0) || (n % 3 == 0))
		return 0;
	max = sqrt(n) + 1;
	for (i = 5; i < max; i+= 6)
		if ((n % i == 0) || (n % (i + 2) == 0))
			return 0;
	return 1;
}

/*
 *  stress_cpu_prime()
 *
 */
static void stress_cpu_prime(const char *name)
{
	uint32_t i, nprimes = 0;

	for (i = 0; i < SIEVE_SIZE; i++) {
		nprimes += is_prime(i);
	}

	if ((g_opt_flags & OPT_FLAGS_VERIFY) && (nprimes != 10000))
		pr_fail("%s: prime error detected, number of primes "
			"has been miscalculated\n", name);
}

/*
 *  stress_cpu_gray()
 *	compute gray codes
 */
static void HOT OPTIMIZE3 TARGET_CLONES stress_cpu_gray(const char *name)
{
	register uint32_t i;
	register uint64_t sum = 0;

	for (i = 0; i < 0x10000; i++) {
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
	if ((g_opt_flags & OPT_FLAGS_VERIFY) && (sum != 0xffff0000))
		pr_fail("%s: gray code error detected, sum of gray "
			"codes between 0x00000 and 0x10000 miscalculated\n",
			name);
}

/*
 * hanoi()
 *	do a Hanoi move
 */
static uint32_t HOT hanoi(
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
static void stress_cpu_hanoi(const char *name)
{
	uint32_t n = hanoi(20, 'X', 'Y', 'Z');

	if ((g_opt_flags & OPT_FLAGS_VERIFY) && (n != 1048576))
		pr_fail("%s: number of hanoi moves different from "
			"the expected number\n", name);

	stress_uint64_put(n);
}

/*
 *  stress_floatconversion
 *	exercise conversion to/from different floating point values
 */
static void TARGET_CLONES stress_cpu_floatconversion(const char *name)
{
	float f_sum = 0.0;
	double d_sum = 0.0;
	long double ld_sum = 0.0;
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
}

/*
 *  stress_intconversion
 *	exercise conversion to/from different int values
 */
static void stress_cpu_intconversion(const char *name)
{
	int16_t i16_sum = stress_mwc16();
	int32_t i32_sum = stress_mwc32();
	int64_t i64_sum = stress_mwc64();

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

		i16 = -(int32_t)(uint16_t)-(int64_t)(uint64_t)i64_sum;
		i16_sum -= i16;
		i32 = -(int32_t)(uint16_t)-(int64_t)(uint64_t)i16_sum;
		i32_sum -= i32;
		i64 = -(int32_t)(uint16_t)-(int64_t)(uint64_t)i32_sum;
		i64_sum -= i64;

		i16 = -(int32_t)(uint64_t)-(int16_t)(uint64_t)i64_sum;
		i16_sum += i16;
		i32 = -(int32_t)(uint64_t)-(int16_t)(uint64_t)i16_sum;
		i32_sum += i32;
		i64 = -(int32_t)(uint64_t)-(int16_t)(uint64_t)i32_sum;
		i64_sum += i64;

		i16 = -(int64_t)(uint16_t)-(int32_t)(uint64_t)i64_sum;
		i16_sum -= i16;
		i32 = -(int64_t)(uint16_t)-(int32_t)(uint64_t)i16_sum;
		i32_sum -= i32;
		i64 = (int64_t)(uint16_t)-(int32_t)(uint64_t)i32_sum;
		i64_sum -= i64;

		i16 = -(int64_t)(uint32_t)-(int16_t)(uint64_t)i64_sum;
		i16_sum += i16;
		i32 = -(int64_t)(uint32_t)-(int16_t)(uint64_t)i16_sum;
		i32_sum += i32;
		i64 = -(int64_t)(uint32_t)-(int16_t)(uint64_t)i32_sum;
		i64_sum += i64;
	}
	stress_uint16_put(i16_sum);
	stress_uint32_put(i32_sum);
	stress_uint64_put(i64_sum);
}

/*
 *  factorial()
 *	compute n!
 */
static inline long double HOT OPTIMIZE3 factorial(int n)
{
	static long double factorials[] = {
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

	return roundl(expl(lgammal((long double)(n + 1))));
}

/*
 *  stress_cpu_pi()
 *	compute pi using the Srinivasa Ramanujan
 *	fast convergence algorithm
 */
static void HOT OPTIMIZE3 stress_cpu_pi(const char *name)
{
	long double s = 0.0L, pi = 0.0L, last_pi = 0.0L;
	const long double precision = 1.0e-20L;
	const long double c = 2.0L * sqrtl(2.0L) / 9801.0L;
	const int max_iter = 5;
	int k = 0;

	do {
		last_pi = pi;
		s += (factorial(4 * k) *
			((26390.0L * (long double)k) + 1103)) /
			(powl(factorial(k), 4.0L) * powl(396.0L, 4.0L * k));
		pi = 1 / (s * c);
		k++;
	} while ((k < max_iter) && (fabsl(pi - last_pi) > precision));

	/* Quick sanity checks */
	if (g_opt_flags & OPT_FLAGS_VERIFY) {
		if (k >= max_iter)
			pr_fail("%s: number of iterations to compute "
				"pi was more than expected\n", name);
		if (fabsl(pi - M_PI) > 1.0e-15L)
			pr_fail("%s: accuracy of computed pi is not "
				"as good as expected\n", name);
	}

	stress_double_put(pi);
}

/*
 *  stress_cpu_omega()
 *	compute the constant omega
 *	See http://en.wikipedia.org/wiki/Omega_constant
 */
static void HOT OPTIMIZE3 stress_cpu_omega(const char *name)
{
	long double omega = 0.5L, last_omega = 0.0L;
	const long double precision = 1.0e-20L;
	const int max_iter = 6;
	int n = 0;

	/* Omega converges very quickly */
	do {
		last_omega = omega;
		omega = (1 + omega) / (1 + expl(omega));
		n++;
	} while ((n < max_iter) && (fabsl(omega - last_omega) > precision));

	if (g_opt_flags & OPT_FLAGS_VERIFY) {
		if (n >= max_iter)
			pr_fail("%s: number of iterations to compute "
				"omega was more than expected\n", name);
		if (fabsl(omega - OMEGA) > 1.0e-16L)
			pr_fail("%s: accuracy of computed omega is "
				"not as good as expected\n", name);
	}

	stress_double_put(omega);
}

#define HAMMING(G, i, nybble, code) 			\
{							\
	int8_t res;					\
	res = (((G[3] >> i) & (nybble >> 3)) & 1) ^	\
	      (((G[2] >> i) & (nybble >> 2)) & 1) ^	\
	      (((G[1] >> i) & (nybble >> 1)) & 1) ^	\
	      (((G[0] >> i) & (nybble >> 0)) & 1);	\
	code ^= ((res & 1) << i);			\
}

/*
 *  hamming84()
 *	compute Hamming (8,4) codes
 */
static uint8_t HOT OPTIMIZE3 hamming84(const uint8_t nybble)
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
static void HOT OPTIMIZE3 TARGET_CLONES stress_cpu_hamming(const char *name)
{
	uint32_t i;
	uint32_t sum = 0;

	for (i = 0; i < 65536; i++) {
		uint32_t encoded;

		/* 4 x 4 bits to 4 x 8 bits hamming encoded */
		encoded = (hamming84((i >> 12) & 0xf) << 24) |
			  (hamming84((i >> 8) & 0xf) << 16) |
			  (hamming84((i >> 4) & 0xf) << 8) |
			  (hamming84((i >> 0) & 0xf) << 0);
		sum += encoded;
	}

	if ((g_opt_flags & OPT_FLAGS_VERIFY) && (sum != 0xffff8000))
		pr_fail("%s: hamming error detected, sum of 65536 "
			"hamming codes not correct\n", name);
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
			u64arg, u32arg, u16arg, u8arg,
			p_u64arg, p_u32arg, p_u16arg, p_u8arg);
	else
		return &u64arg - p_u64arg;
}

/*
 *  stress_cpu_callfunc()
 *	deep function calls
 */
static void stress_cpu_callfunc(const char *name)
{
	uint64_t	u64arg = stress_mwc64();
	uint32_t	u32arg = stress_mwc32();
	uint16_t	u16arg = stress_mwc16();
	uint8_t		u8arg  = stress_mwc8();
	ptrdiff_t	ret;

	(void)name;

	ret = stress_cpu_callfunc_func(1024,
		u64arg, u32arg, u16arg, u8arg,
		&u64arg, &u32arg, &u16arg, &u8arg);

	stress_uint64_put((uint64_t)ret);
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
static void stress_cpu_parity(const char *name)
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
		if ((g_opt_flags & OPT_FLAGS_VERIFY) && (p != parity))
			pr_fail("%s: parity error detected, using "
				"optimised naive method\n",  name);

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
		if ((g_opt_flags & OPT_FLAGS_VERIFY) && (p != parity))
			pr_fail("%s: parity error detected, using the "
				"multiply Shapira method\n",  name);

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
		if ((g_opt_flags & OPT_FLAGS_VERIFY) && (p != parity))
			pr_fail("%s: parity error detected, using "
				"the parallel method\n",  name);

		/*
		 * "Compute parity by lookup table"
		 * https://graphics.stanford.edu/~seander/bithacks.html
		 * Variation #1
		 */
		v = val;
		v ^= v >> 16;
		v ^= v >> 8;
		p = stress_cpu_parity_table[v & 0xff];
		if ((g_opt_flags & OPT_FLAGS_VERIFY) && (p != parity))
			pr_fail("%s: parity error detected, using "
				"the lookup method, variation 1\n",  name);

		/*
		 * "Compute parity by lookup table"
		 * https://graphics.stanford.edu/~seander/bithacks.html
		 * Variation #2
		 */
		u.v32 = val;
		p = stress_cpu_parity_table[u.v8[0] ^ u.v8[1] ^ u.v8[2] ^ u.v8[3]];
		if ((g_opt_flags & OPT_FLAGS_VERIFY) && (p != parity))
			pr_fail("%s: parity error detected, using the "
				"lookup method, variation 1\n",  name);
	}
}

/*
 *  stress_cpu_dither
 *	perform 8 bit to 1 bit gray scale
 *	Floyd–Steinberg dither
 */
static void TARGET_CLONES stress_cpu_dither(const char *name)
{
	size_t x, y;

	(void)name;

	/*
	 *  Generate some random 8 bit image
	 */
	for (y = 0; y < STRESS_CPU_DITHER_Y; y += 8) {
		for (x = 0; x < STRESS_CPU_DITHER_X; x ++) {
			uint64_t v = stress_mwc64();

			pixels[x][y + 0] = v;
			v >>= 8;
			pixels[x][y + 1] = v;
			v >>= 8;
			pixels[x][y + 2] = v;
			v >>= 8;
			pixels[x][y + 3] = v;
			v >>= 8;
			pixels[x][y + 4] = v;
			v >>= 8;
			pixels[x][y + 5] = v;
			v >>= 8;
			pixels[x][y + 6] = v;
			v >>= 8;
			pixels[x][y + 7] = v;
		}
	}

	/*
	 *  ..and dither
	 */
	for (y = 0; y < STRESS_CPU_DITHER_Y; y++) {
		for (x = 0; x < STRESS_CPU_DITHER_X; x++) {
			uint8_t pixel = pixels[x][y];
			uint8_t quant = (pixel < 128) ? 0 : 255;
			int32_t error = pixel - quant;

			bool xok1 = x < (STRESS_CPU_DITHER_X - 1);
			bool xok2 = x > 0;
			bool yok1 = y < (STRESS_CPU_DITHER_Y - 1);

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
}

/*
 *  stress_cpu_div64
 *	perform 64 bit divisions, these are traditionally
 *	really slow ops
 */
static void TARGET_CLONES stress_cpu_div64(const char *name)
{
	register uint64_t i, j;
	const uint64_t di = 0x000014ced130f7513LL;
	const uint64_t dj = 0x000013cba9876543ULL;
	const uint64_t max = 0xfe00000000000000ULL;
	int k = 0;

	(void)name;

	for (i = 0, j = 0x7fffffffffffULL; i < max; i += di, j -= dj) {
		register uint64_t r = i / j;
		stress_uint64_put(r);
		k++;
	}
}

/*
 *  stress_cpu_cpuid()
 *	get CPU id info, x86 only
 */
#if defined(HAVE_CPUID_H) &&	\
    defined(STRESS_ARCH_X86) &&	\
    defined(HAVE_CPUID) &&	\
    NEED_GNUC(4,6,0)
static void TARGET_CLONES stress_cpu_cpuid(const char *name)
{
	register int i;

	(void)name;

	for (i = 0; i < 1000; i++) {
		uint32_t eax, ebx, ecx, edx;

		/*  Highest Function Parameter and Manufacturer ID */
		__cpuid(0x00, eax, ebx, ecx, edx);
		stress_uint32_put(eax);

		/* Processor Info and Feature Bits */
		__cpuid(0x01, eax, ebx, ecx, edx);
		stress_uint32_put(eax);

		/*  Cache and TLB Descriptor information */
		__cpuid(0x02, eax, ebx, ecx, edx);
		stress_uint32_put(eax);

		/* Processor Serial Number */
		__cpuid(0x03, eax, ebx, ecx, edx);
		stress_uint32_put(eax);

		/* Intel thread/core and cache topology */
		__cpuid(0x04, eax, ebx, ecx, edx);
		stress_uint32_put(eax);
		__cpuid(0x0b, eax, ebx, ecx, edx);
		stress_uint32_put(eax);

		/* Get highest extended function index */
		__cpuid(0x80000000, eax, ebx, ecx, edx);
		stress_uint32_put(eax);

		/* Extended processor info */
		__cpuid(0x80000001, eax, ebx, ecx, edx);
		stress_uint32_put(eax);

		/* Processor brand string */
		__cpuid(0x80000002, eax, ebx, ecx, edx);
		stress_uint32_put(eax);
		__cpuid(0x80000003, eax, ebx, ecx, edx);
		stress_uint32_put(eax);
		__cpuid(0x80000004, eax, ebx, ecx, edx);
		stress_uint32_put(eax);

		/* L1 Cache and TLB Identifiers */
		__cpuid(0x80000005, eax, ebx, ecx, edx);

		/* Extended L2 Cache Features */
		__cpuid(0x80000006, eax, ebx, ecx, edx);

		/* Advanced Power Management information */
		__cpuid(0x80000007, eax, ebx, ecx, edx);

		/* Virtual and Physical address size */
		__cpuid(0x80000008, eax, ebx, ecx, edx);
	}
}
#endif

/*
 *  stress_cpu_union
 *	perform bit field operations on a union
 */
static void TARGET_CLONES stress_cpu_union(const char *name)
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
#if defined(__TINYC__)
			uint32_t	f:1;
#else
			uint32_t	:1;
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
		u.bits8.b8 ^= 0xaa;
		u.bits64.b8--;
		u.bits16.b15 ^= 0xbeef;
		u.bits64.b9++;
		u.bits64.b10 *= 5;
		u.u32 += 1;
	}
}

static const uint32_t queens_solutions[] = {
	-1, 1, 0, 0, 2, 10, 4, 40, 92, 352, 724, 2680, 14200
};

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
static void stress_cpu_queens(const char *name)
{
	uint32_t all, n;

	for (all = 1, n = 1; n < 12; n++) {
		uint32_t solutions = queens_try(0, 0, 0, all);
		if ((g_opt_flags & OPT_FLAGS_VERIFY) &&
		    (solutions != queens_solutions[n]))
			pr_fail("%s: queens solution error detected "
				"on board size %" PRIu32 "\n",
				name, n);
		all = (all + all) + 1;
	}
}

/*
 *  stress_cpu_factorial
 *	find factorials from 1..150 using
 *	Stirling's and Ramanujan's Approximations.
 */
static void stress_cpu_factorial(const char *name)
{
	int n;
	double f = 1.0;
	const double precision = 1.0e-6;
	const double sqrt_pi = sqrtl(M_PI);

	for (n = 1; n < 150; n++) {
		double fact = roundl(expl(lgammal((double)(n + 1))));
		double dn;

		f *= (double)n;

		/* Stirling */
		if ((g_opt_flags & OPT_FLAGS_VERIFY) &&
		    ((f - fact) / fact > precision)) {
			pr_fail("%s: Stirling's approximation of factorial(%d) out of range\n",
				name, n);
		}

		/* Ramanujan */
		dn = (double)n;
		fact = sqrt_pi * powl((dn / M_E), dn);
		fact *= powl((((((((8 * dn) + 4)) * dn) + 1) * dn) + 1.0/30.0), (1.0/6.0));
		if ((g_opt_flags & OPT_FLAGS_VERIFY) &&
		    ((f - fact) / fact > precision)) {
			pr_fail("%s: Ramanujan's approximation of factorial(%d) out of range\n",
				name, n);
		}
	}
}

/*
 *  stress_cpu_stats
 *	Exercise some standard stats computations on random data
 */
static void stress_cpu_stats(const char *name)
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
		double d = data[i];
		double f;
		int e;

		f = frexp(d, &e);
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
		double d = data[i] - am;
		stddev += (d * d);
	}
	/* Standard Deviation */
	stddev = sqrt(stddev);

	stress_double_put(am);
	stress_double_put(gm);
	stress_double_put(hm);
	stress_double_put(stddev);

	if (min > hm)
		pr_fail("%s: stats: minimum %f > harmonic mean %f\n",
			name, min, hm);
	if (hm > gm)
		pr_fail("%s: stats: harmonic mean %f > geometric mean %f\n",
			name, hm, gm);
	if (gm > am)
		pr_fail("%s: stats: geometric mean %f > arithmetic mean %f\n",
			name, gm, am);
	if (am > max)
		pr_fail("%s: stats: arithmetic mean %f > maximum %f\n",
			name, am, max);
}

/*
 *  stress_cpu_all()
 *	iterate over all cpu stressors
 */
static HOT OPTIMIZE3 void stress_cpu_all(const char *name)
{
	static int i = 1;	/* Skip over stress_cpu_all */

	cpu_methods[i++].func(name);
	if (!cpu_methods[i].func)
		i = 1;
}

/*
 * Table of cpu stress methods
 */
static const stress_cpu_method_info_t cpu_methods[] = {
	{ "all",		stress_cpu_all },	/* Special "all test */

	{ "ackermann",		stress_cpu_ackermann },
	{ "apery",		stress_cpu_apery },
	{ "bitops",		stress_cpu_bitops },
	{ "callfunc",		stress_cpu_callfunc },
#if defined(HAVE_COMPLEX_H) &&		\
    defined(HAVE_COMPLEX) &&		\
    defined(__STDC_IEC_559_COMPLEX__) &&\
    !defined(__UCLIBC__)
	{ "cdouble",		stress_cpu_complex_double },
	{ "cfloat",		stress_cpu_complex_float },
	{ "clongdouble",	stress_cpu_complex_long_double },
#endif
	{ "collatz",		stress_cpu_collatz },
	{ "correlate",		stress_cpu_correlate },
#if defined(HAVE_CPUID_H) &&	\
    defined(STRESS_ARCH_X86) &&	\
    defined(HAVE_CPUID) &&	\
    NEED_GNUC(4,6,0)
	{ "cpuid",		stress_cpu_cpuid },
#endif
	{ "crc16",		stress_cpu_crc16 },
#if defined(HAVE_FLOAT_DECIMAL32) && !defined(__clang__)
	{ "decimal32",		stress_cpu_decimal32 },
#endif
#if defined(HAVE_FLOAT_DECIMAL64) && !defined(__clang__)
	{ "decimal64",		stress_cpu_decimal64 },
#endif
#if defined(HAVE_FLOAT_DECIMAL128) && !defined(__clang__)
	{ "decimal128",		stress_cpu_decimal128 },
#endif
	{ "dither",		stress_cpu_dither },
	{ "div64",		stress_cpu_div64 },
	{ "djb2a",		stress_cpu_djb2a },
	{ "double",		stress_cpu_double },
	{ "euler",		stress_cpu_euler },
	{ "explog",		stress_cpu_explog },
	{ "factorial",		stress_cpu_factorial },
	{ "fibonacci",		stress_cpu_fibonacci },
#if defined(HAVE_COMPLEX_H) &&		\
    defined(HAVE_COMPLEX) &&		\
    defined(__STDC_IEC_559_COMPLEX__) &&\
    !defined(__UCLIBC__)
	{ "fft",		stress_cpu_fft },
#endif
	{ "fletcher16",		stress_cpu_fletcher16 },
	{ "float",		stress_cpu_float },
#if defined(HAVE_FLOAT16) && !defined(__clang__)
	{ "float16",		stress_cpu_float16 },
#endif
#if defined(HAVE_FLOAT32) && !defined(__clang__)
	{ "float32",		stress_cpu_float32 },
#endif
#if defined(HAVE_FLOAT80) && !defined(__clang__)
	{ "float80",		stress_cpu_float80 },
#endif
#if defined(HAVE_FLOAT128) && !defined(__clang__)
	{ "float128",		stress_cpu_float128 },
#endif
	{ "floatconversion",	stress_cpu_floatconversion },
	{ "fnv1a",		stress_cpu_fnv1a },
	{ "gamma",		stress_cpu_gamma },
	{ "gcd",		stress_cpu_gcd },
	{ "gray",		stress_cpu_gray },
	{ "hamming",		stress_cpu_hamming },
	{ "hanoi",		stress_cpu_hanoi },
	{ "hyperbolic",		stress_cpu_hyperbolic },
	{ "idct",		stress_cpu_idct },
#if defined(HAVE_INT128_T)
	{ "int128",		stress_cpu_int128 },
#endif
	{ "int64",		stress_cpu_int64 },
	{ "int32",		stress_cpu_int32 },
	{ "int16",		stress_cpu_int16 },
	{ "int8",		stress_cpu_int8 },
#if defined(HAVE_INT128_T)
	{ "int128float",	stress_cpu_int128_float },
	{ "int128double",	stress_cpu_int128_double },
	{ "int128longdouble",	stress_cpu_int128_longdouble },
#if defined(HAVE_FLOAT_DECIMAL32) && !defined(__clang__)
	{ "int128decimal32",	stress_cpu_int128_decimal32 },
#endif
#if defined(HAVE_FLOAT_DECIMAL64) && !defined(__clang__)
	{ "int128decimal64",	stress_cpu_int128_decimal64 },
#endif
#if defined(HAVE_FLOAT_DECIMAL128) && !defined(__clang__)
	{ "int128decimal128",	stress_cpu_int128_decimal128 },
#endif
#endif
	{ "int64float",		stress_cpu_int64_float },
	{ "int64double",	stress_cpu_int64_double },
	{ "int64longdouble",	stress_cpu_int64_longdouble },
	{ "int32float",		stress_cpu_int32_float },
	{ "int32double",	stress_cpu_int32_double },
	{ "int32longdouble",	stress_cpu_int32_longdouble },
	{ "intconversion",	stress_cpu_intconversion },
	{ "ipv4checksum",	stress_cpu_ipv4checksum },
	{ "jenkin",		stress_cpu_jenkin },
	{ "jmp",		stress_cpu_jmp },
	{ "ln2",		stress_cpu_ln2 },
	{ "longdouble",		stress_cpu_longdouble },
	{ "loop",		stress_cpu_loop },
	{ "matrixprod",		stress_cpu_matrix_prod },
	{ "murmur3_32",		stress_cpu_murmur3_32 },
	{ "nsqrt",		stress_cpu_nsqrt },
	{ "omega",		stress_cpu_omega },
	{ "parity",		stress_cpu_parity },
	{ "phi",		stress_cpu_phi },
	{ "pi",			stress_cpu_pi },
	{ "pjw",		stress_cpu_pjw },
	{ "prime",		stress_cpu_prime },
	{ "psi",		stress_cpu_psi },
	{ "queens",		stress_cpu_queens },
	{ "rand",		stress_cpu_rand },
	{ "rand48",		stress_cpu_rand48 },
	{ "rgb",		stress_cpu_rgb },
	{ "sdbm",		stress_cpu_sdbm },
	{ "sieve",		stress_cpu_sieve },
	{ "stats",		stress_cpu_stats },
	{ "sqrt", 		stress_cpu_sqrt },
	{ "trig",		stress_cpu_trig },
	{ "union",		stress_cpu_union },
#if defined(HAVE_COMPLEX_H) &&		\
    defined(HAVE_COMPLEX) &&		\
    defined(__STDC_IEC_559_COMPLEX__) &&\
    !defined(__UCLIBC__)
	{ "zeta",		stress_cpu_zeta },
#endif
	{ NULL,			NULL }
};

/*
 *  stress_set_cpu_method()
 *	set the default cpu stress method
 */
static int stress_set_cpu_method(const char *name)
{
	stress_cpu_method_info_t const *info;

	for (info = cpu_methods; info->func; info++) {
		if (!strcmp(info->name, name)) {
			stress_set_setting("cpu-method", TYPE_ID_UINTPTR_T, &info);
			return 0;
		}
	}

	(void)fprintf(stderr, "cpu-method must be one of:");
	for (info = cpu_methods; info->func; info++) {
		(void)fprintf(stderr, " %s", info->name);
	}
	(void)fprintf(stderr, "\n");

	return -1;
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
			return ts.tv_sec + ((double)ts.tv_nsec) / (double)STRESS_NANOSECOND;
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
static int HOT OPTIMIZE3 stress_cpu(const stress_args_t *args)
{
	double bias;
	const stress_cpu_method_info_t *cpu_method = &cpu_methods[0];
	stress_cpu_func func;
	int32_t cpu_load = 100;
	int32_t cpu_load_slice = -64;

	(void)stress_get_setting("cpu-load", &cpu_load);
	(void)stress_get_setting("cpu-load-slice", &cpu_load_slice);
	(void)stress_get_setting("cpu-method", &cpu_method);

	func = cpu_method->func;

	pr_dbg("%s using method '%s'\n", args->name, cpu_method->name);

	/*
	 * It is unlikely, but somebody may request to do a zero
	 * load stress test(!)
	 */
	if (cpu_load == 0) {
		(void)sleep((int)g_opt_timeout);
		return EXIT_SUCCESS;
	}

	stress_set_proc_state(args->name, STRESS_STATE_RUN);

	/*
	 * Normal use case, 100% load, simple spinning on CPU
	 */
	if (cpu_load == 100) {
		do {
			(void)func(args->name);
			inc_counter(args);
		} while (keep_stressing(args));
		return EXIT_SUCCESS;
	}

	/*
	 * More complex percentage CPU utilisation.  This is
	 * not intended to be 100% accurate timing, it is good
	 * enough for most purposes.
	 */
	bias = 0.0;
	do {
		double delay, t1, t2;
		struct timeval tv;

		t1 = stress_per_cpu_time();
		if (cpu_load_slice < 0) {
			/* < 0 specifies number of iterations to do per slice */
			int j;

			for (j = 0; j < -cpu_load_slice; j++) {
				(void)func(args->name);
				if (!keep_stressing_flag())
					break;
				inc_counter(args);
			}
			t2 = stress_per_cpu_time();
		} else if (cpu_load_slice == 0) {
			/* == 0, random time slices */
			double slice_end = t1 + (((double)stress_mwc16()) / 131072.0);
			do {
				(void)func(args->name);
				t2 = stress_per_cpu_time();
				if (!keep_stressing_flag())
					break;
				inc_counter(args);
			} while (t2 < slice_end);
		} else {
			/* > 0, time slice in milliseconds */
			const double slice_end = t1 + ((double)cpu_load_slice / 1000.0);

			do {
				(void)func(args->name);
				t2 = stress_per_cpu_time();
				if (!keep_stressing_flag())
					break;
				inc_counter(args);
			} while (t2 < slice_end);
		}

		/* Must not calculate this with zero % load */
		delay = (((100 - cpu_load) * (t2 - t1)) / (double)cpu_load);
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
			double t3;

			t2 = stress_time_now();

			tv.tv_sec = delay;
			tv.tv_usec = (delay - tv.tv_sec) * 1000000.0;
			(void)select(0, NULL, NULL, NULL, &tv);
			t3 = stress_time_now();
			/* Bias takes account of the time to do the delay */
			bias = (t3 - t2) - delay;
		}
	} while (keep_stressing(args));

	if (stress_is_affinity_set() && (args->instance == 0)) {
		pr_inf("%s: CPU affinity probably set, this can affect CPU loading\n",
			args->name);
	}

	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);

	return EXIT_SUCCESS;
}

static void stress_cpu_set_default(void)
{
	stress_set_cpu_method("all");
}

static const stress_opt_set_func_t opt_set_funcs[] = {
	{ OPT_cpu_load,		stress_set_cpu_load },
	{ OPT_cpu_load_slice,	stress_set_cpu_load_slice },
	{ OPT_cpu_method,	stress_set_cpu_method },
	{ 0,			NULL },
};

stressor_info_t stress_cpu_info = {
	.stressor = stress_cpu,
	.set_default = stress_cpu_set_default,
	.class = CLASS_CPU,
	.opt_set_funcs = opt_set_funcs,
	.help = help
};
