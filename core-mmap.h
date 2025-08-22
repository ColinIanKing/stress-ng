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
#ifndef CORE_MMAP_H
#define CORE_MMAP_H

extern void stress_mmap_set(uint8_t *buf, const size_t sz, const size_t page_size);
extern int stress_mmap_check(uint8_t *buf, const size_t sz, const size_t page_size);
extern void stress_mmap_set_light(uint8_t *buf, const size_t sz, const size_t page_size);
extern int stress_mmap_check_light(uint8_t *buf, const size_t sz, const size_t page_size);
extern WARN_UNUSED void *stress_mmap_populate(void *addr, size_t length, int prot,
	int flags, int fd, off_t offset);
extern WARN_UNUSED void *stress_mmap_anon_shared(size_t length, int prot);
extern int stress_munmap_anon_shared(void *addr, size_t length);

#endif
