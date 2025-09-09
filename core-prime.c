/*
 * Copyright (C) 2014-2021 Canonical, Ltd.
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
#include "core-builtin.h"
#include "core-prime.h"

#include <math.h>

/*
 *  stress_is_prime64()
 *      return true if 64 bit value n is prime
 *      http://en.wikipedia.org/wiki/Primality_test
 */
bool CONST stress_is_prime64(const uint64_t n)
{
	register uint64_t i, max;
	double max_d;

	if (UNLIKELY(n <= 3))
		return n >= 2;
	if ((n % 2 == 0) || (n % 3 == 0))
		return false;
	max_d = 1.0 + shim_sqrt((double)n);
	max = (uint64_t)max_d;
	for (i = 5; i < max; i += 6)
		if ((n % i == 0) || (n % (i + 2) == 0))
			return false;
	return true;
}

/*
 *  stress_get_next_prime64()
 *	find a prime that is not a multiple of n,
 *	used for file name striding. Minimum is 1009,
 *	max is unbounded. Return a prime > n, each
 *	call will return the next prime to keep the
 *	primes different each call.
 */
uint64_t stress_get_next_prime64(const uint64_t n)
{
	static uint64_t p = 1009;
	const uint64_t odd_n = (n & 0x0ffffffffffffffeUL) + 1;
	int i;

	if (LIKELY(p < odd_n))
		p = odd_n;

	/* Search for next prime.. */
	for (i = 0; LIKELY(stress_continue_flag() && (i < 2000)); i++) {
		p += 2;

		if ((n % p) && stress_is_prime64(p))
			return p;
	}
	/* Give up */
	p = 1009;
	return p;
}

/*
 *  stress_get_prime64()
 *	find a prime that is not a multiple of n,
 *	used for file name striding. Minimum is 1009,
 *	max is unbounded. Return a prime > n.
 */
uint64_t stress_get_prime64(const uint64_t n)
{
	uint64_t p = 1009;
	const uint64_t odd_n = (n & 0x0ffffffffffffffeUL) + 1;
	int i;

	if (LIKELY(p < odd_n))
		p = odd_n;

	/* Search for next prime.. */
	for (i = 0; LIKELY(stress_continue_flag() && (i < 2000)); i++) {
		p += 2;

		if ((n % p) && stress_is_prime64(p))
			return p;
	}
	/* Give up */
	return 18446744073709551557ULL;	/* Max 64 bit prime */
}
