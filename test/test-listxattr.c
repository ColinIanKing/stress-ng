// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2013-2021 Canonical, Ltd.
 * Copyright (C) 2022-2023 Colin Ian King
 *
 */

#include <sys/types.h>

extern ssize_t listxattr(const char *path, char *list, size_t size);

int main(void)
{
	char list[1024];

	return listxattr("/some/path/to/somewhere", list, sizeof(list));
}
