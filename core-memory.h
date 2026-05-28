/*
 * Copyright (C) 2024-2026 Colin Ian King.
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
#ifndef CORE_MEMORY_H_H
#define CORE_MEMORY_H_H

#include "stress-ng.h"

extern size_t stress_memory_page_size_get(void);
extern int stress_memory_info_get(size_t *freemem, size_t *totalmem,
        size_t *freeswap, size_t *totalswap);
extern void stress_memory_limits_get(size_t *shmall, size_t *freemem,
	size_t *totalmem, size_t *freeswap, size_t *totalswap);
extern WARN_UNUSED char *stress_memory_free_get(void);
extern void stress_memory_ksm_merge(const int flag);
extern WARN_UNUSED bool stress_memory_low_check(const size_t requested);
extern WARN_UNUSED uint64_t stress_memory_phys_size_get(void);
extern void stress_memory_usage_get(stress_args_t *args,
	const size_t vm_per_instance, const size_t vm_total);
extern CONST WARN_UNUSED void *stress_memory_address_align(const void *addr, const size_t alignment);
extern void stress_memory_anon_name_set(const void *addr, const size_t size,
	const char *name);
extern int stress_memory_swap_off(const char *path);
extern bool stress_memory_readable(const void *addr, const size_t len);
extern WARN_UNUSED int stress_memory_usage_by_pid_get(const pid_t pid,
	size_t *total, size_t *resident, size_t *shared);
extern void stress_memory_compact(void);

#endif
