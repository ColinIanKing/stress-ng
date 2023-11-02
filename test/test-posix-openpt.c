// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2013-2021 Canonical, Ltd.
 * Copyright (C) 2022-2023 Colin Ian King
 *
 */
#if !defined(_XOPEN_SOURCE)
#define _XOPEN_SOURCE 600
#endif

#include <stdlib.h>
#include <fcntl.h>

int main(void)
{
	int ret;

	ret = posix_openpt(0);
	(void)ret;
#if defined(O_RDWR)
	ret = posix_openpt(O_RDWR);
	(void)ret;
#endif
#if defined(O_RDWR)
	ret = posix_openpt(O_NOCTTY);
	(void)ret;
#endif
	return 0;
}
