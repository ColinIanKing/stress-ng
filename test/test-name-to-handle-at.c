// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2013-2021 Canonical, Ltd.
 * Copyright (C) 2022-2023 Colin Ian King
 *
 */
#define _GNU_SOURCE

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

int main(void)
{
	int dirfd = 0;
	struct file_handle handle;
	int mount_id = 0;
	int flags = 0;

	return name_to_handle_at(dirfd, "/some/path/name", &handle, &mount_id, flags);
}
