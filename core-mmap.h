/*
 * Copyright (C) 2024      Colin Ian King.
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

#include "core-attribute.h"

extern void stress_mmap_set(uint8_t *buf, const size_t sz, const size_t page_size);
extern int stress_mmap_check(uint8_t *buf, const size_t sz, const size_t page_size);
extern void stress_mmap_set_light(uint8_t *buf, const size_t sz, const size_t page_size);
extern int stress_mmap_check_light(uint8_t *buf, const size_t sz, const size_t page_size);

#endif
