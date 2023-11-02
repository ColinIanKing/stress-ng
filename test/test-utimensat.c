// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2013-2021 Canonical, Ltd.
 * Copyright (C) 2022-2023 Colin Ian King.
 *
 */

#define _GNU_SOURCE

#include <string.h>
#include <fcntl.h>
#include <sys/stat.h>

int main(void)
{
	int ret;
	struct timespec times[2];

	(void)memset(times, 0, sizeof(times));

	ret = utimensat(0, "/tmp", times, 0);

	return ret;
}
