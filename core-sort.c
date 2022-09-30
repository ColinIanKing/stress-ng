/*
 * Copyright (C)      2022 Colin Ian King.
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

#define STRESS_SORT_CMP(name, type)				\
int stress_sort_cmp_ ## name(const void *p1, const void *p2)	\
{								\
	const type v1 = *(type *)p1;				\
	const type v2 = *(type *)p2;				\
								\
	if (v1 > v2)						\
		return 1;					\
	else if (v1 < v2)					\
		return -1;					\
	else							\
		return 0;					\
}

#define STRESS_SORT_CMP_REV(name, type)				\
int stress_sort_cmp_rev_ ## name(const void *p1, const void *p2)\
{								\
	const type v1 = *(type *)p1;				\
	const type v2 = *(type *)p2;				\
								\
	if (v1 < v2)						\
		return 1;					\
	else if (v1 > v2)					\
		return -1;					\
	else							\
		return 0;					\
}

STRESS_SORT_CMP(int8,  int8_t)
STRESS_SORT_CMP(int16, int16_t)
STRESS_SORT_CMP(int32, int32_t)
STRESS_SORT_CMP(int64, int64_t)

STRESS_SORT_CMP_REV(int8,  int8_t)
STRESS_SORT_CMP_REV(int16, int16_t)
STRESS_SORT_CMP_REV(int32, int32_t)
STRESS_SORT_CMP_REV(int64, int64_t)
