// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2022-2023 Colin Ian King
 *
 */
#include <sys/syscall.h>      /* Definition of SYS_* constants */
#include <unistd.h>

int main(void)
{
#if defined(__NR_getpid)
	return syscall(__NR_getpid);
#else
	return syscall(0);
#endif
}
