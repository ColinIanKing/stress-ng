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

extern ssize_t listxattrat(int dfd, const char *path, unsigned int at_flags, char *list, size_t size);

int main(void)
{
	char list[1024];

	return listxattrat(AT_FDCWD, "/some/path/to/somewhere", 0, list, sizeof(list));
}
