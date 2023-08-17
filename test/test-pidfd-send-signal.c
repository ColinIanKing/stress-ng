// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2013-2021 Canonical, Ltd.
 * Copyright (C) 2022-2023 Colin Ian King
 *
 */

#define _GNU_SOURCE

#include <sys/syscall.h>

#if !defined(__NR_pidfd_send_signal)
#error __NR_pidfd_send_signal not defined
#endif

int main(void)
{
	return 0;
}
