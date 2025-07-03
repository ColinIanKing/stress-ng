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
#define _GNU_SOURCE
#include <unistd.h>
#include <fcntl.h>
#include <string.h>

int main(int argc, char **argv)
{
	static char * const exit_str = "--exec-exit";
	char *argv_new[] = { argv[0], exit_str, NULL };
	char *env_new[] = { NULL };
	int fd;

	if ((argc > 1) && (!strcmp(argv[1], exit_str)))
		return 0;

	fd = open("/proc/self/exe", O_RDONLY | O_CLOEXEC);
	return fexecve(fd, argv_new, env_new);
}
