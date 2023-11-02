// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2013-2021 Canonical, Ltd.
 * Copyright (C) 2022-2023 Colin Ian King
 *
 */
#define _GNU_SOURCE

#include <unistd.h>

#if !(defined(__APPLE__) || \
      defined(__DragonFly__) || \
      defined(__FreeBSD__) || \
      defined(__NetBSD__) || \
      defined(__OpenBSD__))
#include <bsd/unistd.h>
#endif

int main(int argc, char *argv[], char *envp[])
{
	setproctitle_init(argc, argv, envp);
	setproctitle("-%s", "this is a test");
	return 0;
}
