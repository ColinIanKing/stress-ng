// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2013-2021 Canonical, Ltd.
 * Copyright (C) 2022-2023 Colin Ian King
 *
 */
#include <sys/file.h>

int main(void)
{
	int ret;

	ret = flock(1, LOCK_EX);
	(void)ret;
	ret = flock(1, LOCK_UN);
	(void)ret;
	ret = flock(1, LOCK_SH);
	(void)ret;

	return 0;
}
