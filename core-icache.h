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
#ifndef CORE_ICACHE_H
#define CORE_ICACHE_H

#include "stress-ng.h"

#define SIZE_1K		(1024)
#define SIZE_4K		(4 * SIZE_1K)
#define SIZE_16K	(16 * SIZE_1K)
#define SIZE_64K	(64 * SIZE_1K)

/*
 *  STRESS_ICACHE_FUNC()
 *	generates a simple function that is page aligned in its own
 *	section so we can change the code mapping and make it
 *	modifiable to force I-cache refreshes by modifying the code
 */
#define STRESS_ICACHE_FUNC(func_name, page_size)			\
extern void SECTION(icache_callee) ALIGNED(page_size) func_name(void);							\

#if defined(HAVE_ALIGNED_64K)
STRESS_ICACHE_FUNC(stress_icache_func_64K, SIZE_64K)
#endif
STRESS_ICACHE_FUNC(stress_icache_func_16K, SIZE_16K)
STRESS_ICACHE_FUNC(stress_icache_func_4K, SIZE_4K)

#undef STRESS_ICACHE_FUNC

#endif
