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
	struct file_handle handle = { 0 };
	int mount_fd = 0;
	int flags = 0;

	return open_by_handle_at(mount_fd, &handle, flags);
}
