/*
 * Copyright (C) 2025      Colin Ian King
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

#include <sys/types.h>
#include <fcntl.h>
#include <stddef.h>
#include <linux/xattr.h>

extern ssize_t getxattrat(int dfd, const char *path, unsigned int at_flags,
	const char *name, struct xattr_args *args, size_t size);

int main(void)
{
	struct xattr_args args;

	return getxattrat(AT_FDCWD, "/path/to/somewhere", 0, "name", &args, sizeof(args));
}
