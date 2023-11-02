// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2013-2021 Canonical, Ltd.
 * Copyright (C) 2022-2023 Colin Ian King
 *
 */
#define _GNU_SOURCE

#include <unistd.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <linux/openat2.h>

int main(void)
{
	struct open_how how;

	how.flags = O_RDWR;
	how.mode = O_CREAT;
	how.resolve = RESOLVE_NO_SYMLINKS;

	return syscall(__NR_openat2, AT_FDCWD, "test", &how, sizeof(how));
}
