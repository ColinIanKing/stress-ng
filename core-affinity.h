/*
 * Copyright (C) 2024-2025 Colin Ian King.
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
#ifndef CORE_AFFINITY_H
#define CORE_AFFINITY_H

#include "config.h"

extern int stress_set_cpu_affinity(const char *arg);
extern int stress_change_cpu(stress_args_t *args, const int old_cpu);

#if defined(HAVE_CPU_SET_T)
extern int stress_parse_cpu_affinity(const char *arg, cpu_set_t *set, int *setbits);
#endif

extern WARN_UNUSED uint32_t stress_get_usable_cpus(uint32_t **cpus, const bool use_affinity);
extern void stress_free_usable_cpus(uint32_t **cpus);

#endif
