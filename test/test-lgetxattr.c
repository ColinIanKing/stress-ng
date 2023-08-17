// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2013-2021 Canonical, Ltd.
 * Copyright (C) 2022-2023 Colin Ian King
 *
 */

#include <sys/types.h>

extern ssize_t lgetxattr(const char *path, const char *name, void *value, size_t size);

int main(void)
{
	char value[] = "valuestring";

	return lgetxattr("/some/path/to/somewhere", "name", value, sizeof(value));
}
