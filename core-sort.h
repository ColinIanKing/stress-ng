/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copyright (C) 2022-2023 Colin Ian King.
 *
 */
#ifndef CORE_SORT_H
#define CORE_SORT_H

#include <inttypes.h>

extern void stress_sort_data_int32_init(int32_t *data, const size_t n);
extern void stress_sort_data_int32_shuffle(int32_t *data, const size_t n);
extern void stress_sort_data_int32_mangle(int32_t *data, const size_t n);
extern void stress_sort_compare_reset(void);
extern uint64_t stress_sort_compare_get(void);
extern uint64_t stress_sort_compares ALIGN64;

static inline int stress_sort_cmp_str(const void *p1, const void *p2)
{
	return strcmp(*(const char * const *)p1, *(const char * const *)p2);
}

#if 1
#define STRESS_SORT_CMP_FWD(name, type)				\
static inline int OPTIMIZE3 stress_sort_cmp_fwd_ ## name(const void *p1, const void *p2) \
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
static inline int OPTIMIZE3 stress_sort_cmp_fwd_ ## name(const void *p1, const void *p2) \
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
static inline int OPTIMIZE3 stress_sort_cmp_rev_ ## name(const void *p1, const void *p2)\
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
static inline int OPTIMIZE3 stress_sort_cmp_rev_ ## name(const void *p1, const void *p2)\
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
