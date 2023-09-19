// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2013-2021 Canonical, Ltd.
 * Copyright (C) 2022-2023 Colin Ian King
 *
 */

#include <sys/types.h>
#include <unistd.h>
#include <features.h>
#include "../core-version.h"

/*
 * Test for POSIX getpgrp, only
 * supported by glibc 2.19 onwards
 */
#if NEED_GLIBC(2,19,0)
int main(void)
{
	return getpgrp();
}
#else
#error need glib 2.19.0 or higher
#endif
