// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2013-2021 Canonical, Ltd.
 * Copyright (C) 2022-2023 Colin Ian King.
 *
 */

#include <unistd.h>
#include <fcntl.h>
#include <unistd.h>

int main(void)
{
	return unlinkat(AT_FDCWD, "test", 0);
}
