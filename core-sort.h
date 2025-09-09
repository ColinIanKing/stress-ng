/*
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
#ifndef CORE_SORT_H
#define CORE_SORT_H

#include <inttypes.h>
#include "core-attribute.h"

typedef void (*sort_swap_func_t)(void *p1, void *p2, register size_t size);
typedef void (*sort_copy_func_t)(void *p1, void *p2, register size_t size);

extern void stress_sort_data_int32_init(int32_t *data, const size_t n);
extern void stress_sort_data_int32_shuffle(int32_t *data, const size_t n);
extern void stress_sort_data_int32_mangle(int32_t *data, const size_t n);
extern void stress_sort_compare_reset(void);
extern uint64_t stress_sort_compare_get(void);
extern uint64_t stress_sort_compares ALIGN64;

extern sort_swap_func_t sort_swap_func(const size_t size);
extern sort_copy_func_t sort_copy_func(const size_t size);

static inline ALWAYS_INLINE CONST int stress_sort_cmp_str(const void *p1, const void *p2)
{
	return strcmp(*(const char * const *)p1, *(const char * const *)p2);
}

#if 1
#define STRESS_SORT_CMP_FWD(name, type)				\
static inline int CONST OPTIMIZE3 stress_sort_cmp_fwd_ ## name(const void *p1, const void *p2) \
{								\
	register const type v1 = *(const type *)p1;		\
	register const type v2 = *(const type *)p2;		\
								\
	stress_sort_compares++;					\
	if (v1 > v2)						\
		return 1;					\
	else if (v1 < v2)					\
		return -1;					\
	else							\
		return 0;					\
}
#else
#define STRESS_SORT_CMP_FWD(name, type)				\
static inline int CONST OPTIMIZE3 stress_sort_cmp_fwd_ ## name(const void *p1, const void *p2) \
{								\
	register const type v1 = *(const type *)p1;		\
	register const type v2 = *(const type *)p2;		\
								\
	stress_sort_compares++;					\
	return (v1 < v2) ? -(v1 != v2) : (v1 != v2);		\
}
#endif

#if 1
#define STRESS_SORT_CMP_REV(name, type)				\
static inline int CONST OPTIMIZE3 stress_sort_cmp_rev_ ## name(const void *p1, const void *p2)\
{								\
	register const type v1 = *(const type *)p1;		\
	register const type v2 = *(const type *)p2;		\
								\
	stress_sort_compares++;					\
	if (v1 < v2)						\
		return 1;					\
	else if (v1 > v2)					\
		return -1;					\
	else							\
		return 0;					\
}
#else
#define STRESS_SORT_CMP_REV(name, type)				\
static inline int CONST OPTIMIZE3 stress_sort_cmp_rev_ ## name(const void *p1, const void *p2)\
{								\
	register const type v1 = *(const type *)p1;		\
	register const type v2 = *(const type *)p2;		\
								\
	stress_sort_compares++;					\
	return (v1 > v2) ? -(v1 != v2) : (v1 != v2);		\
}
#endif

STRESS_SORT_CMP_FWD(int8,  int8_t)
STRESS_SORT_CMP_FWD(int16, int16_t)
STRESS_SORT_CMP_FWD(int32, int32_t)
STRESS_SORT_CMP_FWD(int64, int64_t)

STRESS_SORT_CMP_REV(int8,  int8_t)
STRESS_SORT_CMP_REV(int16, int16_t)
STRESS_SORT_CMP_REV(int32, int32_t)
STRESS_SORT_CMP_REV(int64, int64_t)

STRESS_SORT_CMP_FWD(int, int)
STRESS_SORT_CMP_REV(int, int)

#undef STRESS_SORT_CMP_FWD
#undef STRESS_SORT_CMP_REV

#endif
