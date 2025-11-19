/*
 * Copyright (C) 2025      Colin Ian King.
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
#ifndef CORE_WORKLOAD_H
#define CORE_WORKLOAD_H

#define STRESS_WORKLOAD_METHOD_ALL	(0)
#define STRESS_WORKLOAD_METHOD_TIME	(1)
#define STRESS_WORKLOAD_METHOD_NOP	(2)
#define STRESS_WORKLOAD_METHOD_MEMSET	(3)
#define STRESS_WORKLOAD_METHOD_MEMMOVE	(4)
#define STRESS_WORKLOAD_METHOD_SQRT	(5)
#define STRESS_WORKLOAD_METHOD_INC64	(6)
#define STRESS_WORKLOAD_METHOD_MWC64	(7)
#define STRESS_WORKLOAD_METHOD_GETPID	(8)
#define STRESS_WORKLOAD_METHOD_MEMREAD	(9)
#define STRESS_WORKLOAD_METHOD_PAUSE	(10)
#define STRESS_WORKLOAD_METHOD_PROCNAME	(11)
#define STRESS_WORKLOAD_METHOD_FMA	(12)
#define STRESS_WORKLOAD_METHOD_RANDOM	(13)
#define STRESS_WORKLOAD_METHOD_VECFP	(14)
#define STRESS_WORKLOAD_METHOD_MAX	STRESS_WORKLOAD_METHOD_VECFP

typedef struct {
	const char *name;
	const int method;
} stress_workload_method_t;

extern const stress_workload_method_t workload_methods[];
extern const char *stress_workload_method(const size_t i);
extern void stress_workload_waste_time(const char *name,
	const int workload_method, const double run_duration_sec,
	uint8_t *buffer, const size_t buffer_len);

#endif
