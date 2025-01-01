/*
 * Copyright (C) 2012-2021 Canonical, Ltd.
 * Copyright (C) 2022-2025 Colin Ian King
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
#include <unistd.h>

#if !defined(__linux__)
#error requires linux to build
#endif

#include <sys/select.h>
#include <sys/inotify.h>

#if !defined(IN_DELETE_SELF)
#error missing inotify IN_DELETE_SELF flag
#endif

#if !defined(IN_MOVE_SELF)
#error missing inotify IN_MOVE_SELF flag
#endif

#if !defined(IN_MOVED_TO)
#error missing inotify IN_MOVED_TO flag
#endif

#if !defined(IN_MOVED_FROM)
#error missing inotify IN_MOVED_FROM flag
#endif

#if !defined(IN_ATTRIB)
#error missing inotify IN_ATTRIB flag
#endif

#if !defined(IN_ACCESS)
#error missing inotify IN_ACCESS flag
#endif

#if !defined(IN_MODIFY)
#error missing inotify IN_MODIFY flag
#endif

#if !defined(IN_CREATE)
#error missing inotify IN_CREATE flag
#endif

#if !defined(IN_OPEN)
#error missing inotify IN_OPEN flag
#endif

#if !defined(IN_CLOSE_WRITE)
#error missing inotify IN_CLOSE_WRITE flag
#endif

#if !defined(IN_CLOSE_NOWRITE)
#error missing inotify IN_CLOSE_NOWRITE flag
#endif

#if !defined(IN_DELETE)
#error missing inotify IN_DELETE flag
#endif

#if !defined(IN_DELETE_SELF)
#error missing inotify IN_DELETE_SELF flag
#endif

#define BUFFER_SIZE	(4096)

int main(void)
{
	int fd, wd;
	ssize_t len, i = 0;
	char buffer[1024];

	fd = inotify_init();
	if (fd < 0)
		return -1;

	wd = inotify_add_watch(fd, "/", IN_ACCESS);
	if (wd < 0)
		return -1;

	len = read(fd, buffer, sizeof(buffer));
	if (len < 0)
		return -1;

	while ((i >= 0) && (i <= len - (ssize_t)sizeof(struct inotify_event))) {
		const struct inotify_event *event = (struct inotify_event *)&buffer[i];

		if (event->len > sizeof(buffer))
			break;
		i += sizeof(struct inotify_event) + event->len;
	}

	(void)inotify_rm_watch(fd, wd);
	(void)close(fd);
	return 0;
}
