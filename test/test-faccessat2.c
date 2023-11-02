// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2013-2021 Canonical, Ltd.
 * Copyright (C) 2022-2023 Colin Ian King
 *
 */
#define _GNU_SOURCE

#include <fcntl.h>
#include <unistd.h>

int main(void)
{
	return faccessat2(AT_FDCWD, "dummytestfile", F_OK, AT_SYMLINK_NOFOLLOW);
}
