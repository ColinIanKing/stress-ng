// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2012-2021 Canonical, Ltd.
 * Copyright (C) 2022-2023 Colin Ian King
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
		struct inotify_event *event = (struct inotify_event *)&buffer[i];

		if (event->len > sizeof(buffer))
			break;
		i += sizeof(struct inotify_event) + event->len;
	}

	(void)inotify_rm_watch(fd, wd);
	(void)close(fd);
	return 0;
}
