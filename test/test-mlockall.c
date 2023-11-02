// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2013-2021 Canonical, Ltd.
 * Copyright (C) 2022-2023 Colin Ian King
 *
 */
#include <sys/mman.h>

int main(void)
{
	int ret;

	ret = mlockall(MCL_CURRENT);
	(void)ret;
	ret = mlockall(MCL_FUTURE);
	(void)ret;
	ret = mlockall(MCL_ONFAULT);
	(void)ret;

	return 0;
}
