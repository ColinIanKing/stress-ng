// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2013-2021 Canonical, Ltd.
 * Copyright (C) 2022-2023 Colin Ian King
 *
 */

#include <sys/types.h>

extern int fsetxattr(int fd, const char *name, const void *value, size_t size, int flags);

int main(void)
{
	int fd = 3;
	char value[] = "valuestring";

	return fsetxattr(fd, "name", value, sizeof(value), 0);
}
