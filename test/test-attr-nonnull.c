/*
 * Copyright (C) 2024      Colin Ian King
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

#include <stdlib.h>
#include <string.h>

#define NONNULL(...) __attribute__((nonnull (__VA_ARGS__)))

static void nonnull_func(void *dst, void *src, size_t len) NONNULL(1, 2);

static void nonnull_func(void *dst, void *src, size_t len)
{
	(void)memcpy(dst, src, len);
}

int main(int argc, char **argv)
{
	char x[20];

	nonnull_func(x, "test", 5);
	/* nonnull_func(x, NULL, 5); */

	return 0;
}
