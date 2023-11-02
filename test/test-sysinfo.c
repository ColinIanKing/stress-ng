// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2013-2021 Canonical, Ltd.
 * Copyright (C) 2022-2023 Colin Ian King.
 *
 */

#include <sys/sysinfo.h>

#if defined(__sun__)
#error this is not the sysinfo you are looking for
#endif

int main(void)
{
	struct sysinfo info;

	return sysinfo(&info);
}
