/*
 * Copyright (C) 2013-2018 Canonical, Ltd.
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
#include "stress-ng.h"

#define ALIGN_SIZE	(64)

typedef struct {
	uint8_t buffer[STR_SHARED_SIZE + ALIGN_SIZE];
} buffer_t;

static NOINLINE void *__memcpy(void *dest, const void *src, size_t n)
{
	return memcpy(dest, src, n);
}

static NOINLINE void *__memmove(void *dest, const void *src, size_t n)
{
	return memmove(dest, src, n);
}

/*
 *  stress_memcpy()
 *	stress memory copies
 */
int stress_memcpy(const args_t *args)
{
	static buffer_t b;
	buffer_t *b_str = (buffer_t *)g_shared->str_shared;
	uint8_t *str_shared = g_shared->str_shared;
	uint8_t *aligned_buf = align_address(b.buffer, ALIGN_SIZE);

	stress_strnrnd((char *)aligned_buf, ALIGN_SIZE);

	do {
		(void)__memcpy(aligned_buf, str_shared, STR_SHARED_SIZE);
		(void)__memcpy(str_shared, aligned_buf, STR_SHARED_SIZE / 2);
		(void)__memmove(aligned_buf, aligned_buf + 64, STR_SHARED_SIZE - 64);
		*b_str = b;
		(void)__memmove(aligned_buf + 64, aligned_buf, STR_SHARED_SIZE - 64);
		b = *b_str;
		(void)__memmove(aligned_buf + 1, aligned_buf, STR_SHARED_SIZE - 1);
		(void)__memmove(aligned_buf, aligned_buf + 1, STR_SHARED_SIZE - 1);
		inc_counter(args);
	} while (keep_stressing());

	return EXIT_SUCCESS;
}
