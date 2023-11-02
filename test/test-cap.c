// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2013-2021 Canonical, Ltd.
 * Copyright (C) 2022-2023 Colin Ian King
 *
 */
#define _GNU_SOURCE

#include <unistd.h>
#include <sys/capability.h>

int main(void)
{
	struct __user_cap_header_struct uch;
	struct __user_cap_data_struct ucd;

	uch.version = _LINUX_CAPABILITY_VERSION_3;
	uch.pid = getpid();

	return capget(&uch, &ucd);
}
