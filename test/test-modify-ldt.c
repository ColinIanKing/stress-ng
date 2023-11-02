// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2013-2021 Canonical, Ltd.
 * Copyright (C) 2022-2023 Colin Ian King
 *
 */
#define _GNU_SOURCE

#include <unistd.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <asm/ldt.h>
#include <string.h>

#if !defined(__NR_modify_ldt)
#error modify_ldt syscall not defined
#endif

/* Arch specific, x86 */
#if defined(__x86_64__) || defined(__x86_64) || \
    defined(__amd64__)  || defined(__amd64)  ||	\
    defined(__i386__)   || defined(__i386)
#else
#error modify_ldt syscall not applicable for non-x86 architectures
#endif

int main(void)
{
	struct user_desc ud;
	int ret;

	(void)memset(&ud, 0, sizeof(ud));
	ret = syscall(__NR_modify_ldt, 0, &ud, sizeof(ud));
	if (ret == 0)
		ret = syscall(__NR_modify_ldt, 1, &ud, sizeof(ud));

	return ret;
}
