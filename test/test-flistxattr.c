// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2013-2021 Canonical, Ltd.
 * Copyright (C) 2022-2023 Colin Ian King
 *
 */

#include <sys/types.h>

extern ssize_t flistxattr(int fd, char *list, size_t size);

int main(void)
{
	int fd = 3;
	char list[1024];

	return flistxattr(fd, list, sizeof(list));
}
