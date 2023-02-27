/*
 * Copyright (C) 2022-2023 Colin Ian King.
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
#ifndef CORE_ARCH_H
#define CORE_ARCH_H

#include <inttypes.h>

extern void stress_sort_data_int32_init(int32_t *data, const size_t n);
extern void stress_sort_data_int32_shuffle(int32_t *data, const size_t n);
extern void stress_sort_compare_reset(void);
extern uint64_t stress_sort_compare_get(void);

#define STRESS_SORT_CMP_FWD(name, type)				\
extern int stress_sort_cmp_fwd_ ## name(const void *p1, const void *p2);
#define STRESS_SORT_CMP_REV(name, type)				\
extern int stress_sort_cmp_rev_ ## name(const void *p1, const void *p2);

STRESS_SORT_CMP_FWD(int8,  int8_t)
STRESS_SORT_CMP_FWD(int16, int16_t)
STRESS_SORT_CMP_FWD(int32, int32_t)
STRESS_SORT_CMP_FWD(int64, int64_t)

STRESS_SORT_CMP_REV(int8,  int8_t)
STRESS_SORT_CMP_REV(int16, int16_t)
STRESS_SORT_CMP_REV(int32, int32_t)
STRESS_SORT_CMP_REV(int64, int64_t)

#undef STRESS_SORT_CMP_FWD
#undef STRESS_SORT_CMP_REV

#endif
