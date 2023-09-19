// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2013-2021 Canonical, Ltd.
 * Copyright (C) 2022-2023 Colin Ian King
 *
 */

#define _GNU_SOURCE

#include <sys/types.h>
#include <sys/uio.h>

int main(void)
{
	pid_t pid = 0;
	struct iovec local_iov = { 0 };
	struct iovec remote_iov = { 0 };
	unsigned long liovcnt = 1;
	unsigned long riovcnt = 1;
	unsigned long flags = 0;

	return process_vm_writev(pid, &local_iov, liovcnt, &remote_iov, riovcnt, flags);
}
