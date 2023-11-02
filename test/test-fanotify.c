// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2012-2021 Canonical, Ltd.
 * Copyright (C) 2021-2023 Colin Ian King.
 *
 */

#if !defined(__linux__)
#error requires linux to build
#endif

#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <mntent.h>
#include <sys/select.h>
#include <sys/fanotify.h>

#if !defined(FAN_MARK_ADD)
#error missing fnotify FAN_MARK_ADD flag
#endif

#if !defined(FAN_MARK_MOUNT)
#error missing fnotify FAN_MARK_MOUNT flag
#endif

#if !defined(FAN_ACCESS)
#error missing fnotify FAN_ACCESS flag
#endif

#if !defined(FAN_MODIFY)
#error missing fnotify FAN_MODIFY flag
#endif

#if !defined(FAN_OPEN)
#error missing fnotify FAN_OPEN flag
#endif

#if !defined(FAN_CLOSE)
#error missing fnotify FAN_CLOSE flag
#endif

#if !defined(FAN_ONDIR)
#error missing fnotify FAN_ONDIR flag
#endif

#if !defined(FAN_EVENT_ON_CHILD)
#error missing fnotify FAN_EVENT_ON_CHILD flag
#endif

#if !defined(FAN_EVENT_OK)
#error missing fnotify FAN_EVENT_OK macro
#endif

#if !defined(FAN_NOFD)
#error missing fnotify FAN_NOFD macro
#endif

#define BUFFER_SIZE	(4096)

int main(void)
{
	int fan_fd, ret;
	size_t len;
	struct fanotify_event_metadata *metadata;
	void *buffer;

	ret = posix_memalign(&buffer, BUFFER_SIZE, BUFFER_SIZE);
	if ((ret != 0) || (buffer == NULL))
		return -1;

	fan_fd = fanotify_init(0, 0);
	if (fan_fd < 0) {
		free(buffer);
		return -1;
	}

	ret = fanotify_mark(fan_fd, FAN_MARK_ADD | FAN_MARK_MOUNT,
		FAN_ACCESS| FAN_MODIFY | FAN_OPEN | FAN_CLOSE |
		FAN_ONDIR | FAN_EVENT_ON_CHILD, AT_FDCWD, "/");
	(void)ret;

	len = read(fan_fd, (void *)buffer, BUFFER_SIZE);
	metadata = (struct fanotify_event_metadata *)buffer;

	while (FAN_EVENT_OK(metadata, len)) {
		metadata = FAN_EVENT_NEXT(metadata, len);
	}

	free(buffer);
	(void)close(fan_fd);
	return 0;
}
