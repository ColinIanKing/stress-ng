// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2021-2023 Colin Ian King.
 *
 */
#define _GNU_SOURCE

#include <fcntl.h>
#include <sys/stat.h>

int main(void)
{
	struct statx statxbuf;

	return statx(-1, "/tmp", AT_STATX_SYNC_AS_STAT, STATX_BASIC_STATS, &statxbuf);
}
