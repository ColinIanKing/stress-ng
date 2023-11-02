// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2013-2021 Canonical, Ltd.
 * Copyright (C) 2022-2023 Colin Ian King
 *
 */
#define _GNU_SOURCE
#include <unistd.h>
#include <fcntl.h>
#include <sys/syscall.h>

int main(void)
{
	char *argv_new[] = { NULL, "--exec-exit", NULL };
	char *env_new[] = { NULL };

	/* One day this system call will land in glibc.. */
#if defined(__NR_execveat)
	return syscall(__NR_execveat, 0, "/proc/self/exe", argv_new, env_new, AT_EMPTY_PATH);
#else
#error no execveat
#endif
}
