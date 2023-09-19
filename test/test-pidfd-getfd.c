// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2013-2021 Canonical, Ltd.
 * Copyright (C) 2022-2023 Colin Ian King
 *
 */

#define _GNU_SOURCE

#if defined(__has_include) && __has_include(<sys/pidfd.h>)
#include <sys/pidfd.h>
#endif

int main(void)
{
	/* We don't care about the args, we just want to see if it links */
	return pidfd_getfd(-1, 0, 0);
}
