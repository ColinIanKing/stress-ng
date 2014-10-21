/*
 * Copyright (C) 2013-2014 Canonical, Ltd.
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
#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <complex.h>
#include <sys/time.h>
#include "stress-ng.h"

/*
 *  stress_cpu_sqrt()
 *	stress CPU on square roots
 */
static void stress_cpu_sqrt(void)
{
	int i;

	for (i = 0; i < 16384; i++) {
		uint64_t rnd = mwc();
		double r = sqrt((double)rnd) * sqrt((double)rnd);
		if ((opt_flags & OPT_FLAGS_VERIFY) &&
		    (uint64_t)rint(r) != rnd) {
			pr_fail(stderr, "sqrt error detected on sqrt(%" PRIu64 ")\n", rnd);
			if (!opt_do_run)
				break;
		}
	}
}

/*
 *  stress_cpu_loop()
 *	simple CPU busy loop
 */
static void stress_cpu_loop(void)
{
	uint32_t i, i_sum = 0;
	const uint32_t sum = 134209536UL;

	for (i = 0; i < 16384; i++) {
		i_sum += i;
		__asm__ __volatile__("");	/* Stop optimising out */
	}
	if ((opt_flags & OPT_FLAGS_VERIFY) && (i_sum != sum))
		pr_fail(stderr, "cpu loop 0..16383 sum was %" PRIu32 " and "
			"did not match the expected value of %" PRIu32 "\n",
			i_sum, sum);
}

/*
 *  stress_cpu_gcd()
 *	compute Greatest Common Divisor
 */
static void stress_cpu_gcd(void)
{
	uint32_t i, i_sum = 0;
	const uint32_t sum = 63000868UL;

	for (i = 0; i < 16384; i++) {
		register uint32_t a = i, b = i % (3 + (1997 ^ i));

		while (b != 0) {
			register uint32_t r = b;
			b = a % b;
			a = r;
		}
		i_sum += a;
		__asm__ __volatile__("");	/* Stop optimising out */
	}
	if ((opt_flags & OPT_FLAGS_VERIFY) && (i_sum != sum))
		pr_fail(stderr, "gcd error detected, failed modulo or assigment operations\n");
}

/*
 *  stress_cpu_bitops()
 *	various bit manipulation hacks from bithacks
 *	https://graphics.stanford.edu/~seander/bithacks.html
 */
static void stress_cpu_bitops(void)
{
	uint32_t i, i_sum = 0;
	const uint32_t sum = 0x8aadcaab;

	for (i = 0; i < 16384; i++) {
		{
			uint32_t r, v, s = (sizeof(v) * 8) - 1;

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
			uint32_t v = i;
			v ^= v >> 16;
			v ^= v >> 8;
			v ^= v >> 4;
			v &= 0xf;
			i_sum += v;
		}
		{
			/* Brian Kernighan count bits */
			uint32_t j, v = i;
			for (j = 0; v; j++)
				v &= v - 1;
			i_sum += j;
		}
		{
			/* round up to nearest highest power of 2 */
			uint32_t v = i - 1;
			v |= v >> 1;
			v |= v >> 2;
			v |= v >> 4;
			v |= v >> 8;
			v |= v >> 16;
			i_sum += v;
		}
	}
	if ((opt_flags & OPT_FLAGS_VERIFY) && (i_sum != sum))
		pr_fail(stderr, "bitops error detected, failed bitops operations\n");
}

/*
 *  stress_cpu_trig()
 *	simple sin, cos trig functions
 */
static void stress_cpu_trig(void)
{
	int i;
	double d_sum = 0.0;

	for (i = 0; i < 16384; i++) {
		double theta = (2.0 * M_PI * (double)i)/16384.0;
		d_sum += (cos(theta) * sin(theta));
	}
	double_put(d_sum);
}

/*
 *  stress_cpu_hyperbolic()
 *	simple hyperbolic sinh, cosh functions
 */
static void stress_cpu_hyperbolic(void)
{
	int i;
	double d_sum = 0.0;

	for (i = 0; i < 16384; i++) {
		double theta = (2.0 * M_PI * (double)i)/16384.0;
		d_sum += (cosh(theta) * sinh(theta));
	}
	double_put(d_sum);
}

/*
 *  stress_cpu_rand()
 *	generate lots of pseudo-random integers
 */
static void stress_cpu_rand(void)
{
	int i;
	uint32_t i_sum = 0;
	const uint32_t sum = 0xc253698c;

	MWC_SEED();
	for (i = 0; i < 16384; i++)
		i_sum += mwc();

	if ((opt_flags & OPT_FLAGS_VERIFY) && (i_sum != sum))
		pr_fail(stderr, "rand error detected, failed sum of pseudo-random values\n");
}

/*
 *  stress_cpu_nsqrt()
 *	iterative Newton–Raphson square root
 */
static void stress_cpu_nsqrt(void)
{
	int i;

	for (i = 0; i < 16384; i++) {
		double n = (double)i;
		double lo = (n < 1.0) ? n : 1.0;
		double hi = (n < 1.0) ? 1.0 : n;

		while ((hi - lo) > 0.00001) {
			double g = (lo + hi) / 2.0;
			if (g * g > n)
				hi = g;
			else
				lo = g;
		}
		double_put((lo + hi) / 2.0);
	}
}

/*
 *  stress_cpu_phi()
 *	compute the Golden Ratio
 */
static void stress_cpu_phi(void)
{
	double phi; /* Golden ratio */
	register uint64_t a, b;
	const uint64_t mask = 1ULL << 63;
	int i;

	/* Pick any two starting points */
	a = mwc() % 99;
	b = mwc() % 99;

	/* Iterate until we approach overflow */
	for (i = 0; (i < 64) && !((a | b) & mask); i++) {
		/* Find nth term */
		register uint64_t c = a + b;

		a = b;
		b = c;
	}
	/* And we have the golden ratio */
	phi = (double)a / (double)b;

	double_put(phi);
}

/*
 *  fft_partial()
 *  	partial Fast Fourier Transform
 */
static void fft_partial(complex *data, complex *tmp, const int n, const int m)
{
	if (m < n) {
		const int m2 = m * 2;
		int i;

		fft_partial(tmp, data, n, m2);
		fft_partial(tmp + m, data + m, n, m2);
		for (i = 0; i < n; i += m2) {
			complex v = tmp[i];
			complex t =
				cexp((-I * M_PI * (double)i) /
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
static void stress_cpu_fft(void)
{
	complex buf[FFT_SIZE], tmp[FFT_SIZE];
	int i;

	for (i = 0; i < FFT_SIZE; i++)
		buf[i] = (complex)(i % 63);

	memcpy(tmp, buf, sizeof(complex) * FFT_SIZE);
	fft_partial(buf, tmp, FFT_SIZE, 1);
}

/*
 *   stress_cpu_euler()
 *	compute e using series
 */
static void stress_cpu_euler(void)
{
	long double e = 1.0;
	long double fact = 1.0;
	int n = 0;

	/* Arbitary precision chosen */
	for (n = 1; n < 32; n++) {
		fact *= n;
		e += (1.0 / fact);
	}

	double_put(e);
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
		uint32_t v = (uint32_t)mwc();

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
 *  jenkin()
 *	Jenkin's hash on random data
 *	http://www.burtleburtle.net/bob/hash/doobs.html
 */
static uint32_t jenkin(const uint8_t *data, const size_t len)
{
	uint8_t i;
	register uint32_t h = 0;

	for (i = 0; i < len; i++) {
		h += *data++;
		h += h << 10;
		h ^= h >> 6;
	}
	h += h << 3;
	h ^= h >> 11;
	h += h << 15;

	return h;
}

/*
 *  stress_cpu_jenkin()
 *	multiple iterations on jenkin hash
 */
static void stress_cpu_jenkin(void)
{
	uint8_t buffer[128];
	size_t i;
	uint32_t i_sum = 0;
	const uint32_t sum = 0x96673680;

	MWC_SEED();
	random_buffer(buffer, sizeof(buffer));
	for (i = 0; i < sizeof(buffer); i++)
		i_sum += jenkin(buffer, sizeof(buffer));

	if ((opt_flags & OPT_FLAGS_VERIFY) && (i_sum != sum))
		pr_fail(stderr, "jenkin error detected, failed hash jenkin sum\n");
}

/*
 *  pjw()
 *	Hash a string, from Aho, Sethi, Ullman, Compiling Techniques.
 */
static uint32_t pjw(const char *str)
{
	uint32_t h = 0;

	while (*str) {
		uint32_t g;
		h = (h << 4) + (*str);
		if (0 != (g = h & 0xf0000000)) {
			h = h ^ (g >> 24);
			h = h ^ g;
		}
		str++;
	}
	return h;
}

/*
 *  stress_cpu_pjw()
 *	stress test hash pjw
 */
static void stress_cpu_pjw(void)
{
	char buffer[128];
	size_t i;
	uint32_t i_sum = 0;
	const uint32_t sum = 0xa89a91c0;

	MWC_SEED();
	random_buffer((uint8_t *)buffer, sizeof(buffer));
	/* Make it ASCII range ' '..'_' */
	for (i = 0; i < sizeof(buffer); i++)
		buffer[i] = (buffer[i] & 0x3f) + ' ';

	for (i = sizeof(buffer) - 1; i; i--) {
		buffer[i] = '\0';
		i_sum += pjw(buffer);
	}
	if ((opt_flags & OPT_FLAGS_VERIFY) && (i_sum != sum))
		pr_fail(stderr, "pjw error detected, failed hash pjw sum\n");
}

/*
 *  stress_cpu_idct()
 *	compute 8x8 Inverse Discrete Cosine Transform
 */
static void stress_cpu_idct(void)
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
					const double cos_pi_j_v = cos(pi_j * v);

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
	for (i = 0; i < sz; i++) {
		for (j = 0; j < sz; j++) {
			if ((int)idct[i][j] != 255) {
				uint64_put(1);
				return;
			}
		}
	}
}

#define int_ops(a, b, mask)			\
	do {					\
		a += b;				\
		b ^= a;				\
		a >>= 1;			\
		b <<= 2;			\
		b -= a;				\
		a ^= ~0;			\
		b ^= ((~0xf0f0f0f0f0f0f0f0ULL) & mask);	\
		a *= 3;				\
		b *= 7;				\
		a += 2;				\
		b -= 3;				\
		a /= 77;			\
		b /= 3;				\
		a <<= 1;			\
		b <<= 2;			\
		a |= 1;				\
		b |= 3;				\
		a *= mwc();			\
		b ^= mwc();			\
		a += mwc();			\
		b -= mwc();			\
		a /= 7;				\
		b /= 9;				\
		a |= ((0x1000100010001000ULL) & mask);	\
		b &= ((0xffeffffefebefffeULL) & mask);	\
	} while (0);

/*
 *  stress_cpu_int64()
 *	mix of integer ops
 */
static void stress_cpu_int64(void)
{
	const uint64_t a_final = 0x199b182aba853658ULL;
	const uint64_t b_final = 0x21c06cb28f08ULL;
	register uint64_t a, b;
	int i;

	MWC_SEED();
	a = mwc();
	b = mwc();

	for (i = 0; i < 10000; i++) {
		int_ops(a, b, 0xffffffffffffULL);
		if (!opt_do_run)
			break;
	}
	if ((opt_flags & OPT_FLAGS_VERIFY) && ((a != a_final) || (b != b_final)))
		pr_fail(stderr, "int64 error detected, failed int64 math operations\n");
}

/*
 *  stress_cpu_int32()
 *	mix of integer ops
 */
static void stress_cpu_int32(void)
{
	const uint32_t a_final = 0x19ecd617UL;
	const uint32_t b_final = 0x4b6de8eUL;
	register uint32_t a, b;
	int i;

	MWC_SEED();
	a = mwc();
	b = mwc();

	for (i = 0; i < 10000; i++) {
		int_ops(a, b, 0xffffffffUL);
		if (!opt_do_run)
			break;
	}
	if ((opt_flags & OPT_FLAGS_VERIFY) && ((a != a_final) || (b != b_final)))
		pr_fail(stderr, "int32 error detected, failed int32 math operations\n");
}

/*
 *  stress_cpu_int16()
 *	mix of integer ops
 */
static void stress_cpu_int16(void)
{
	const uint16_t a_final = 0x11ae;
	const uint16_t b_final = 0x0f5e;
	register uint16_t a, b;
	int i;

	MWC_SEED();
	a = mwc();
	b = mwc();

	for (i = 0; i < 10000; i++) {
		int_ops(a, b, 0xffff);
		if (!opt_do_run)
			break;
	}
	if ((opt_flags & OPT_FLAGS_VERIFY) && ((a != a_final) || (b != b_final)))
		pr_fail(stderr, "int16 error detected, failed int16 math operations\n");
}

/*
 *  stress_cpu_int8()
 *	mix of integer ops
 */
static void stress_cpu_int8(void)
{
	const uint8_t a_final = 0x24;
	const uint8_t b_final = 0x16;
	register uint8_t a, b;
	int i;

	MWC_SEED();
	a = mwc();
	b = mwc();

	for (i = 0; i < 10000; i++) {
		int_ops(a, b, 0xff);
		if (!opt_do_run)
			break;
	}
	if ((opt_flags & OPT_FLAGS_VERIFY) && ((a != a_final) || (b != b_final)))
		pr_fail(stderr, "int16 error detected, failed int16 math operations\n");
}

#define float_ops(a, b, c, d)		\
	do {				\
		a = a + b;		\
		b = a * c;		\
		c = a - b;		\
		d = a / b;		\
		a = c / 0.1923;		\
		b = c + a;		\
		c = b * 3.12;		\
		d = d + b + sin(a);	\
		a = (b + c) / c;	\
		b = b * c;		\
		c = c + 1.0;		\
		d = d - sin(c);		\
		a = a * cos(b);		\
		b = b + cos(c);		\
		c = sin(a) / 2.344;	\
		b = d - 1.0;		\
	} while (0)

/*
 *  stress_cpu_float()
 *	mix of floating point ops
 */
static void stress_cpu_float(void)
{
	uint32_t i;
	float a = 0.18728, b = mwc(), c = mwc(), d;

	for (i = 0; i < 10000; i++) {
		float_ops(a, b, c, d);
		if (!opt_do_run)
			break;
	}
	double_put(a + b + c + d);
}

/*
 *  stress_cpu_double()
 *	mix of floating point ops
 */
static void stress_cpu_double(void)
{
	uint32_t i;
	double a = 0.18728, b = mwc(), c = mwc(), d;

	for (i = 0; i < 10000; i++) {
		float_ops(a, b, c, d);
		if (!opt_do_run)
			break;
	}
	double_put(a + b + c + d);
}

/*
 *  stress_cpu_longdouble()
 *	mix of floating point ops
 */
static void stress_cpu_longdouble(void)
{
	uint32_t i;
	long double a = 0.18728, b = mwc(), c = mwc(), d;

	for (i = 0; i < 10000; i++) {
		float_ops(a, b, c, d);
		if (!opt_do_run)
			break;
	}
	double_put(a + b + c + d);
}

/*
 *  stress_cpu_rgb()
 *	CCIR 601 RGB to YUV to RGB conversion
 */
static void stress_cpu_rgb(void)
{
	int i;
	uint32_t rgb = mwc() & 0xffffff;
	uint8_t r = rgb >> 16;
	uint8_t g = rgb >> 8;
	uint8_t b = rgb;

	/* Do a 1000 colours starting from the rgb seed */
	for (i = 0; i < 1000; i++) {
		float y,u,v;

		/* RGB to CCIR 601 YUV */
		y = (0.299 * r) + (0.587 * g) + (0.114 * b);
		u = (b - y) * 0.565;
		v = (r - y) * 0.713;

		/* YUV back to RGB */
		r = y + (1.403 * v);
		g = y - (0.344 * u) - (0.714 * v);
		b = y + (1.770 * u);

		/* And bump each colour to make next round */
		r += 1;
		g += 2;
		b += 3;
	}
	uint64_put(r + g + b);
}

/*
 *  stress_cpu_matrix_prod(void)
 *	matrix product
 */
static void stress_cpu_matrix_prod(void)
{
	int i, j, k;
	const int n = 128;

	long double a[n][n], b[n][n], r[n][n];
	long double v = 1 / (long double)((uint32_t)~0);

	for (i = 0; i < n; i++) {
		for (j = 0; j < n; j++) {
			a[i][j] = (long double)mwc() * v;
			b[i][j] = (long double)mwc() * v;
			r[i][j] = 0.0;
		}
	}

	for (i = 0; i < n; i++)
		for (j = 0; j < n; j++)
			for (k = 0; k < n; k++)
				r[i][j] += a[i][k] * b[k][j];
}

/*
 *   stress_cpu_fibonacci()
 *	compute fibonacci series
 */
static void stress_cpu_fibonacci(void)
{
	const uint64_t fn_res = 0xa94fad42221f2702;
	register uint64_t f1 = 0, f2 = 1, fn;

	do {
		fn = f1 + f2;
		f1 = f2;
		f2 = fn;
	} while (!(fn & 0x8000000000000000ULL));

	if ((opt_flags & OPT_FLAGS_VERIFY) && (fn_res != fn))
		pr_fail(stderr, "fibonacci error detected, summation or assignment failure\n");
}

/*
 *  stress_cpu_psi
 *	compute the constant psi,
 * 	the reciprocal Fibonacci constant
 */
static void stress_cpu_psi(void)
{
	double long f1 = 0.0, f2 = 1.0, fn;
	double long psi = 0.0, last_psi;
	double long precision = 1.0e-20;

	do {
		fn = f1 + f2;
		f1 = f2;
		f2 = fn;
		last_psi = psi;
		psi += 1.0 / f1;
	} while (fabsl(psi - last_psi) > precision);

	double_put(psi);
}

/*
 *   stress_cpu_ln2
 *	compute ln(2) using series
 */
static void stress_cpu_ln2(void)
{
	register double ln2 = 0.0;
	register const double math_ln2 = log(2.0);
	register uint32_t n = 1;

	/* Arbitary precision chosen */
	while (n < 1000000) {
		double delta;
		/* Unroll, do several ops */
		ln2 += (long double)1.0 / (long double)n++;
		ln2 -= (long double)1.0 / (long double)n++;
		ln2 += (long double)1.0 / (long double)n++;
		ln2 -= (long double)1.0 / (long double)n++;
		ln2 += (long double)1.0 / (long double)n++;
		ln2 -= (long double)1.0 / (long double)n++;
		ln2 += (long double)1.0 / (long double)n++;
		ln2 -= (long double)1.0 / (long double)n++;

		/* Arbitarily accurate enough? */
		delta = ln2 - math_ln2;
		delta = (delta < 0.0) ? -delta : delta;
		if (delta < 0.000001)
			break;
	}

	double_put(ln2);
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
static void stress_cpu_ackermann(void)
{
	uint32_t a = ackermann(3, 10);

	if ((opt_flags & OPT_FLAGS_VERIFY) && (a != 0x1ffd))
		pr_fail(stderr, "ackermann error detected, ackermann(3,10) miscalculated\n");
}

/*
 *   stress_cpu_explog
 *	compute exp(log(n))
 */
static void stress_cpu_explog(void)
{
	uint32_t i;
	double n = 1e6;

	for (i = 1; i < 100000; i++)
		n = exp(log(n) / 1.00002);
}

/*
 *  Undocumented gcc-ism, force -O0 optimisation
 */
static void stress_cpu_jmp(void)  __attribute__((optimize("-O0")));

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

/*
 *   stress_cpu_jmp
 *	jmp conditionals
 */
static void stress_cpu_jmp(void)
{
	register int i, next = 0;

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
		uint64_put(next + i);
	}
}

/*
 *  ccitt_crc16()
 *	perform naive CCITT CRC16
 */
static uint16_t ccitt_crc16(const uint8_t *data, size_t n)
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
	uint16_t crc = ~0;

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
static void stress_cpu_crc16(void)
{
	uint8_t buffer[1024];
	size_t i;

	random_buffer(buffer, sizeof(buffer));
	for (i = 0; i < sizeof(buffer); i++)
		uint64_put(ccitt_crc16(buffer, i));
}

/*
 *  zeta()
 *	Riemann zeta function
 */
static inline long double complex zeta(
	const long double complex s,
	long double precision)
{
	int i = 1;
	long double complex z = 0.0, zold = 0.0;

	do {
		zold = z;
		z += 1 / cpow(i++, s);
	} while (cabs(z - zold) > precision);

	return z;
}

/*
 * stress_cpu_zeta()
 *	stress test Zeta(2.0)..Zeta(10.0)
 */
static void stress_cpu_zeta(void)
{
	long double precision = 0.00000001;
	double f;

	for (f = 2.0; f < 11.0; f += 1.0)
		double_put(zeta(f, precision));
}

/*
 * stress_cpu_correlate()
 *
 *  Introduction to Signal Processing,
 *  Prentice-Hall, 1995, ISBN: 0-13-209172-0.
 */
static void stress_cpu_correlate(void)
{
	const size_t data_len = 16384;
	const size_t corr_len = data_len / 16;
	size_t i, j;
	double data_average = 0.0;
	double data[data_len], corr[corr_len + 1];

	/* Generate some random data */
	for (i = 0; i < data_len; i++) {
		data[i] = mwc();
		data_average += data[i];
	}
	data_average /= (double)data_len;

	/* And correlate */
	for (i = 0; i <= corr_len; i++) {
		corr[i] = 0.0;
		for (j = 0; j < data_len - i; j++) {
			corr[i] += (data[i + j] - data_average) *
				   (data[j] - data_average);
		}
		corr[i] /= (double)corr_len;
		double_put(corr[i]);
	}
}


/*
 * stress_cpu_sieve()
 * 	slightly optimised Sieve of Eratosthenes
 */
static void stress_cpu_sieve(void)
{
	const uint32_t nsqrt = sqrt(SIEVE_SIZE);
	static uint32_t sieve[(SIEVE_SIZE + 31) / 32];
	uint32_t i, j;

	memset(sieve, 0xff, sizeof(sieve));
	for (i = 2; i < nsqrt; i++)
		if (SIEVE_GETBIT(sieve, i))
			for (j = i * i; j < SIEVE_SIZE; j += i)
				SIEVE_CLRBIT(sieve, j);

	/* And count up number of primes */
	for (j = 0, i = 2; i < SIEVE_SIZE; i++) {
		if (SIEVE_GETBIT(sieve, i))
			j++;
	}
	if ((opt_flags & OPT_FLAGS_VERIFY) && (j != 664579))
		pr_fail(stderr, "sieve error detected, number of primes has been miscalculated\n");
}

/*
 *  is_prime()
 *	return true if n is prime
 *	http://en.wikipedia.org/wiki/Primality_test
 */
static inline bool is_prime(uint32_t n)
{
	uint32_t i, max;

	if (n <= 3)
		return n >= 2;
	if ((n % 2 == 0) || (n % 3 == 0))
		return false;
	max = sqrt(n) + 1;
	for (i = 5; i < max; i+= 6)
		if ((n % i == 0) || (n % (i + 2) == 0))
			return false;
	return true;
}

/*
 *  stress_cpu_prime()
 *
 */
static void stress_cpu_prime(void)
{
	uint32_t i, nprimes = 0;

	for (i = 0; i < 1000000; i++) {
		if (is_prime(i))
			nprimes++;
	}

	if ((opt_flags & OPT_FLAGS_VERIFY) && (nprimes != 78498))
		pr_fail(stderr, "prime error detected, number of primes between 0 and 1000000 miscalculated\n");
}

/*
 *  stress_cpu_gray()
 *	compute gray codes
 */
static void stress_cpu_gray(void)
{
	uint32_t i;
	uint64_t sum = 0;

	for (i = 0; i < 0x10000; i++) {
		register uint32_t gray_code, mask;

		/* Binary to Gray code */
		gray_code = (i >> 1) ^ i;
		sum += gray_code;

		/* Gray code back to binary */
		for (mask = gray_code >> 1; mask; mask >>= 1)
			gray_code ^= mask;
		sum += gray_code;
	}
	if ((opt_flags & OPT_FLAGS_VERIFY) && (sum != 0xffff0000))
		pr_fail(stderr, "gray code error detected, sum of gray codes "
			"between 0x00000 and 0x10000 miscalculated\n");
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
	if (n == 0) {
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
static void stress_cpu_hanoi(void)
{
	uint64_put(hanoi(20, 'X', 'Y', 'Z'));
}

/*
 *  factorial()
 *	compute n!
 */
static long double factorial(int n)
{
	long double f = 1;

	while (n > 0) {
		f *= (long double)n;
		n--;
	}
	return f;
}

/*
 *  stress_cpu_pi()
 *	compute pi using the Srinivasa Ramanujan
 *	fast convergence algorithm
 */
static void stress_cpu_pi(void)
{
	long double s = 0.0, pi = 0.0, last_pi = 0.0;
	const long double precision = 1.0e-18;
	const long double c = 2.0 * sqrtl(2.0) / 9801.0;
	int k = 0;

	do {
		last_pi = pi;
		s += (factorial(4 * k) *
			((26390.0 * (long double)k) + 1103)) /
			(powl(factorial(k), 4.0) * powl(396.0, 4.0 * k));
		pi = 1 / (s * c);
		k++;
	} while (fabsl(pi - last_pi) > precision);

	double_put(pi);
}


/*
 *  stress_cpu_omega()
 *	compute the constant omega
 */
static void stress_cpu_omega(void)
{
	long double omega = 0.5, last_omega = 0.0;
	const long double precision = 1.0e-20;

	do {
		last_omega = omega;
		omega = (1 + omega) / (1 + expl(omega));
	} while (fabsl(omega - last_omega) > precision);
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
static uint8_t hamming84(const uint8_t nybble)
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
		0b11110001,
		0b11010010,
		0b10110100,
		0b01111000,
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
static void stress_cpu_hamming(void)
{
	uint32_t i;
	uint32_t sum = 0;

	for (i = 0; i < 65536; i++) {
		uint32_t encoded;

		/* 4 x 4 bits to 4 x 8 bits hamming encoded */
		encoded =
			  (hamming84((i >> 12) & 0xf) << 24) |
			  (hamming84((i >> 8) & 0xf) << 16) |
			  (hamming84((i >> 4) & 0xf) << 8) |
			  (hamming84((i >> 0) & 0xf) << 0);
		sum += encoded;
	}

	if ((opt_flags & OPT_FLAGS_VERIFY) && (sum != 0xffff8000))
		pr_fail(stderr, "hamming error detected, sum of 65536 hamming codes not correct\n");
}

/*
 *  stress_cpu_all()
 *	iterate over all cpu stressors
 */
static void stress_cpu_all(void)
{
	static int i = 1;	/* Skip over stress_cpu_all */

	cpu_methods[i++].func();
	if (!cpu_methods[i].func)
		i = 1;
}

/*
 * Table of cpu stress methods
 */
stress_cpu_stressor_info_t cpu_methods[] = {
	{ "all",	stress_cpu_all },	/* Special "all test */

	{ "ackermann",	stress_cpu_ackermann },
	{ "bitops",	stress_cpu_bitops },
	{ "crc16",	stress_cpu_crc16 },
	{ "correlate",	stress_cpu_correlate },
	{ "double",	stress_cpu_double },
	{ "euler",	stress_cpu_euler },
	{ "explog",	stress_cpu_explog },
	{ "fibonacci",	stress_cpu_fibonacci },
	{ "fft",	stress_cpu_fft },
	{ "float",	stress_cpu_float },
	{ "gcd",	stress_cpu_gcd },
	{ "gray",	stress_cpu_gray },
	{ "hamming",	stress_cpu_hamming },
	{ "hanoi",	stress_cpu_hanoi },
	{ "hyperbolic",	stress_cpu_hyperbolic },
	{ "idct",	stress_cpu_idct },
	{ "int64",	stress_cpu_int64 },
	{ "int32",	stress_cpu_int32 },
	{ "int16",	stress_cpu_int16 },
	{ "int8",	stress_cpu_int8 },
	{ "jenkin",	stress_cpu_jenkin },
	{ "jmp",	stress_cpu_jmp },
	{ "ln2",	stress_cpu_ln2 },
	{ "longdouble",	stress_cpu_longdouble },
	{ "loop",	stress_cpu_loop },
	{ "matrixprod",	stress_cpu_matrix_prod },
	{ "nsqrt",	stress_cpu_nsqrt },
	{ "omega",	stress_cpu_omega },
	{ "phi",	stress_cpu_phi },
	{ "pi",		stress_cpu_pi },
	{ "pjw",	stress_cpu_pjw },
	{ "prime",	stress_cpu_prime },
	{ "psi",	stress_cpu_psi },
	{ "rand",	stress_cpu_rand },
	{ "rgb",	stress_cpu_rgb },
	{ "sieve",	stress_cpu_sieve },
	{ "sqrt", 	stress_cpu_sqrt },
	{ "trig",	stress_cpu_trig },
	{ "zeta",	stress_cpu_zeta },
	{ NULL,		NULL }
};

/*
 *  stress_cpu_find_by_name()
 *	find cpu stress method by name
 */
stress_cpu_stressor_info_t *stress_cpu_find_by_name(const char *name)
{
	stress_cpu_stressor_info_t *info = cpu_methods;

	for (info = cpu_methods; info->func; info++) {
		if (!strcmp(info->name, name))
			return info;
	}
	return NULL;
}

/*
 *  stress_cpu()
 *	stress CPU by doing floating point math ops
 */
int stress_cpu(
	uint64_t *const counter,
	const uint32_t instance,
	const uint64_t max_ops,
	const char *name)
{
	double bias;
	stress_cpu_func func = opt_cpu_stressor->func;

	(void)instance;
	(void)name;

	/*
	 * Normal use case, 100% load, simple spinning on CPU
	 */
	if (opt_cpu_load == 100) {
		do {
			(void)func();
			(*counter)++;
		} while (opt_do_run && (!max_ops || *counter < max_ops));
		return EXIT_SUCCESS;
	}

	/*
	 * It is unlikely, but somebody may request to do a zero
	 * load stress test(!)
	 */
	if (opt_cpu_load == 0) {
		sleep((int)opt_timeout);
		return EXIT_SUCCESS;
	}

	/*
	 * More complex percentage CPU utilisation.  This is
	 * not intended to be 100% accurate timing, it is good
	 * enough for most purposes.
	 */
	bias = 0.0;
	do {
		int j;
		double t, delay;
		struct timeval tv1, tv2, tv3;

		gettimeofday(&tv1, NULL);
		for (j = 0; j < 64; j++) {
			(void)func();
			if (!opt_do_run)
				break;
			(*counter)++;
		}
		gettimeofday(&tv2, NULL);
		t = timeval_to_double(&tv2) - timeval_to_double(&tv1);
		/* Must not calculate this with zero % load */
		delay = t * (((100.0 / (double) opt_cpu_load)) - 1.0);
		delay -= bias;

		tv1.tv_sec = delay;
		tv1.tv_usec = (delay - tv1.tv_sec) * 1000000.0;
		select(0, NULL, NULL, NULL, &tv1);
		gettimeofday(&tv3, NULL);
		/* Bias takes account of the time to do the delay */
		bias = (timeval_to_double(&tv3) - timeval_to_double(&tv2)) - delay;
	} while (opt_do_run && (!max_ops || *counter < max_ops));

	return EXIT_SUCCESS;
}
