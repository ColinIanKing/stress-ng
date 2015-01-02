/*
 * Copyright (C) 2013-2015 Canonical, Ltd.
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
 * This code is a complete clean re-write of the stress tool by
 * Colin Ian King <colin.king@canonical.com> and attempts to be
 * backwardly compatible with the stress tool by Amos Waterland
 * <apw@rossby.metr.ou.edu> but has more stress tests and more
 * functionality.
 *
 */
#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

#include "stress-ng.h"

static uint8_t buffer[MEM_CACHE_SIZE] ALIGN64;

/*
 *  stress_memcpy()
 *	stress memory copies
 */
int stress_memcpy(
	uint64_t *const counter,
	const uint32_t instance,
	const uint64_t max_ops,
	const char *name)
{
	(void)instance;
	(void)name;

	uint8_t *mem_cache = shared->mem_cache;
	
	do {
		memcpy(buffer, mem_cache, MEM_CACHE_SIZE);
		memcpy(mem_cache, buffer, MEM_CACHE_SIZE);
		memmove(buffer, buffer + 64, MEM_CACHE_SIZE - 64);
		memmove(buffer + 64, buffer, MEM_CACHE_SIZE - 64);
		memmove(buffer + 1, buffer, MEM_CACHE_SIZE - 1);
		(*counter)++;
	} while (opt_do_run && (!max_ops || *counter < max_ops));

	return EXIT_SUCCESS;
}
