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
#ifndef CORE_MEMORY_H_H
#define CORE_MEMORY_H_H

#include "stress-ng.h"

extern size_t stress_get_page_size(void);
extern int stress_get_meminfo(size_t *freemem, size_t *totalmem,
        size_t *freeswap, size_t *totalswap);
extern void stress_get_memlimits(size_t *shmall, size_t *freemem,
	size_t *totalmem, size_t *freeswap, size_t *totalswap);
extern WARN_UNUSED char *stress_get_memfree_str(void);
extern void stress_ksm_memory_merge(const int flag);
extern WARN_UNUSED bool stress_low_memory(const size_t requested);
extern WARN_UNUSED uint64_t stress_get_phys_mem_size(void);
extern void stress_usage_bytes(stress_args_t *args,
	const size_t vm_per_instance, const size_t vm_total);
extern WARN_UNUSED void *stress_align_address(const void *addr, const size_t alignment);
extern void stress_set_vma_anon_name(const void *addr, const size_t size,
	const char *name);
extern int stress_munmap_force(void *addr, size_t length);
extern int stress_swapoff(const char *path);
extern bool stress_addr_readable(const void *addr, const size_t len);
extern WARN_UNUSED int stress_get_pid_memory_usage(const pid_t pid,
	size_t *total, size_t *resident, size_t *shared);

#endif
