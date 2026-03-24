/*
 * Copyright (C) 2022-2026 Colin Ian King.
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
#include "core-sort.h"
#include "core-pragma.h"
#include "core-target-clones.h"

#define THRESH	(63)

uint64_t stress_sort_compares ALIGN64;

/*
 *  stress_sort_compare_reset
 *	reset stress_sort_compares counter
 */
void stress_sort_compare_reset(void)
{
	stress_sort_compares = 0;
}

/*
 *  stress_sort_compare_get
 *	get stress_sort_compares counter
 */
uint64_t stress_sort_compare_get(void)
{
	return stress_sort_compares;
}

/*
 *  stress_sort_data_int32_mangle()
 *	flip bits to re-order 16 and 32 bit comparisons
 */
void OPTIMIZE3 stress_sort_data_int32_mangle(int32_t *data, const size_t n)
{
	const int32_t *end = data + n;

PRAGMA_UNROLL_N(8)
	while (data < end) {
		*(data++) ^= 0x80008000;
	}
}

/*
 *  Monotonically increasing values
 */
#define SORT_SETDATA(d, i, v, prev)	\
do {					\
	prev = 1 + prev + (v & 0x7);	\
	d[i] = prev;			\
	v >>= 2;			\
	i++;				\
} while (0)

void OPTIMIZE3 stress_sort_data_int32_init(int32_t *data, const size_t n)
{
	register int32_t prev = 0;
	register size_t i;

	for (i = 0; i < n;) {
		register int32_t v = (int32_t)stress_mwc32();

		SORT_SETDATA(data, i, v, prev);
		SORT_SETDATA(data, i, v, prev);
		SORT_SETDATA(data, i, v, prev);
		SORT_SETDATA(data, i, v, prev);
		SORT_SETDATA(data, i, v, prev);
		SORT_SETDATA(data, i, v, prev);
		SORT_SETDATA(data, i, v, prev);
		SORT_SETDATA(data, i, v, prev);
	}
}

void OPTIMIZE3 stress_sort_data_int32_shuffle(int32_t *data, const size_t n)
{
	register uint32_t const a = 16843009;
        register uint32_t const c = 826366247;
        register uint32_t seed = stress_mwc32();
	register uint32_t *data32 = (uint32_t *)data;
	register size_t i;

	register size_t mask = n - 1;

	/* Powers of two can use a bit mask for modulo n */
	if ((n & mask) == 0) {
PRAGMA_UNROLL_N(8)
		for (i = 0; i < n; i++) {
			register const uint32_t j = seed & mask;
			register const uint32_t tmp = data32[i];

			data32[i] = data32[j];
			seed = (a * seed + c);
			data32[j] = tmp;
		}
	} else {
PRAGMA_UNROLL_N(8)
		for (i = 0; i < n; i++) {
			register const uint32_t j = seed % n;
			register const uint32_t tmp = data32[i];

			data32[i] = data32[j];
			seed = (a * seed + c);
			data32[j] = tmp;
		}
	}
}

static void OPTIMIZE3 sort_swap8(void *p1, void *p2, register const size_t size)
{
	register uint64_t tmp64;

	(void)size;

	tmp64 = *(uint64_t *)p1;
	*(uint64_t *)p1 = *(uint64_t *)p2;
	*(uint64_t *)p2 = tmp64;
}

static void OPTIMIZE3 sort_swap4(void *p1, void *p2, register const size_t size)
{
	register uint32_t tmp32;

	(void)size;

	tmp32 = *(uint32_t *)p1;
	*(uint32_t *)p1 = *(uint32_t *)p2;
	*(uint32_t *)p2 = tmp32;
}

static void OPTIMIZE3 sort_swap2(void *p1, void *p2, register const size_t size)
{
	register uint16_t tmp16;

	(void)size;

	tmp16 = *(uint16_t *)p1;
	*(uint16_t *)p1 = *(uint16_t *)p2;
	*(uint16_t *)p2 = tmp16;
}

static void OPTIMIZE3 sort_swap1(void *p1, void *p2, register const size_t size)
{
	register uint8_t tmp8;

	(void)size;

	tmp8 = *(uint8_t *)p1;
	*(uint8_t *)p1 = *(uint8_t *)p2;
	*(uint8_t *)p2 = tmp8;
}

static void OPTIMIZE3 sort_swap(void *p1, void *p2, register size_t size)
{
	register uint8_t *u8p1 = (uint8_t *)p1;
	register uint8_t *u8p2 = (uint8_t *)p2;

	do {
		register uint8_t tmp;

		tmp = *(u8p1);
		*(u8p1++) = *(u8p2);
		*(u8p2++) = tmp;
	} while (--size);
}

static void OPTIMIZE3 sort_copy8(void *p1, void *p2, register const size_t size)
{
	(void)size;

	*(uint64_t *)p1 = *(uint64_t *)p2;
}

static void OPTIMIZE3 sort_copy4(void *p1, void *p2, register const size_t size)
{
	(void)size;

	*(uint32_t *)p1 = *(uint32_t *)p2;
}

static void OPTIMIZE3 sort_copy2(void *p1, void *p2, register const size_t size)
{
	(void)size;

	*(uint16_t *)p1 = *(uint16_t *)p2;
}

static void OPTIMIZE3 sort_copy1(void *p1, void *p2, register const size_t size)
{
	(void)size;

	*(uint8_t *)p1 = *(uint8_t *)p2;
}

static void OPTIMIZE3 sort_copy(void *p1, void *p2, register size_t size)
{
	register uint8_t *u8p1, *u8p2;

	u8p1 = (uint8_t *)p1;
	u8p2 = (uint8_t *)p2;

	do {
		*(u8p1++) = *(u8p2++);
	} while (--size);
}

stress_sort_swap_func_t stress_sort_swap_func(const size_t size)
{
	switch (size) {
	case 8:
		return sort_swap8;
	case 4:
		return sort_swap4;
	case 2:
		return sort_swap2;
	case 1:
		return sort_swap1;
	default:
		break;
	}
	return sort_swap;
}

stress_sort_copy_func_t stress_sort_copy_func(const size_t size)
{
	switch (size) {
	case 8:
		return sort_copy8;
	case 4:
		return sort_copy4;
	case 2:
		return sort_copy2;
	case 1:
		return sort_copy1;
	default:
		break;
	}
	return sort_copy;
}

static inline size_t qsort_bm_minimum(const size_t x, const size_t y)
{
	return x <= y ? x : y;
}

static inline uint8_t ALWAYS_INLINE *qsort_bm_med3(
	uint8_t *a,
	uint8_t *b,
	uint8_t *c,
	int (*cmp)(const void *, const void*))
{
	return (cmp(a, b) < 0) ?
		((cmp(b, c) < 0) ? b : (cmp(a, c) < 0) ? c : a) :
		((cmp(b, c) > 0) ? b : (cmp(a, c) > 0) ? c : a);
}

static inline void ALWAYS_INLINE qsort_bm_swap(
	uint8_t *a,
	uint8_t *b,
	size_t n)
{
	register uint8_t * RESTRICT pi = (uint8_t *)a;
	register uint8_t * RESTRICT pj = (uint8_t *)b;

PRAGMA_UNROLL_N(4)
	do {
		register uint8_t tmp;

		tmp = *pi;
		*pi++ = *pj;
		*pj++ = tmp;
	} while ((n -= sizeof(uint8_t)) > 0);
}

/*
 *  Bentley and MacIlroy’s quicksort, v2
 *  https://web.ecs.syr.edu/~royer/cis675/slides/07engSort.pdf
 */
void TARGET_CLONES OPTIMIZE3 qsort_bm(
	void *base,
	const size_t n,
	const size_t es,
	int (*cmp)(const void *, const void*))
{
	register uint8_t *a = (uint8_t *)base;
	uint8_t *pa, *pb, *pc, *pd, *pm, *pn, *pv;
	size_t s;

	if (n < THRESH) {
		for (pm = a + es; pm < a + (n * es); pm += es) {
			register uint8_t *p;

			for (p = pm; (p > a) && (cmp(p - es, p) > 0); p -= es) {
				qsort_bm_swap(p, p - es, es);
			}
		}
		return;
	}
	pm = a + (n >> 1) * es;
	if (n > THRESH) {
		register uint8_t *p = a;

		pn = a + (n - 1) * es;
		if (n > 63) {
			s = (n >> 3) * es;
			p = qsort_bm_med3(p, p + s, p + (s << 1), cmp);
			pm = qsort_bm_med3(pm - s, pm, pm + s, cmp);
			pn = qsort_bm_med3(pn - (s << 1), pn - s, pn, cmp);
		}
		pm = qsort_bm_med3(p, pm, pn, cmp);
	}

	pv = a;
	qsort_bm_swap(pv, pm, es);

	pa = pb = a;
	pc = pd = a + (n - 1) * es;
	for (;;) {
		int r;

		while ((pb <= pc) && (r = cmp(pb, pv)) <= 0) {
			if (r == 0) {
				qsort_bm_swap(pa, pb, es);
				pa += es;
			}
			pb += es;
		}
		while ((pb <= pc) && (r = cmp(pc, pv)) >= 0) {
			if (r == 0) {
				qsort_bm_swap(pc, pd, es);
				pd -= es;
			}
			pc -= es;
		}
		if (pb > pc)
			break;
		qsort_bm_swap(pb, pc, es);
		pb += es;
		pc -= es;
	}
	pn = a + (n * es);
	s = qsort_bm_minimum(pa - a, pb - pa);
	if (s > 0)
		qsort_bm_swap(a, pb - s, s);
	s = qsort_bm_minimum(pd - pc, pn - pd - es);
	if (s > 0)
		qsort_bm_swap(pb, pn-s, s);
	s = pb - pa;
	if (s > es)
		qsort_bm(a, s / es, es, cmp);
	s = pd - pc;
	if (s > es)
		qsort_bm(pn - s, s / es, es, cmp);
}
