// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2013-2021 Canonical, Ltd.
 * Copyright (C) 2022-2023 Colin Ian King
 *
 */

#include <sys/types.h>

extern int fremovexattr(int fd, const char *name);

int main(void)
{
	int fd = 3;

	return fremovexattr(fd, "name");
}
