// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2013-2021 Canonical, Ltd.
 * Copyright (C) 2022-2023 Colin Ian King
 *
 */

#include <stddef.h>
#include <spawn.h>

int main(void)
{
	pid_t pid = 0;
	static char *argv_new[] = { NULL };
	static char *env_new[] = { NULL };

	return posix_spawn(&pid, "/tmp/nowhere", NULL, NULL, argv_new, env_new);
}
