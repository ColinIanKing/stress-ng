// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2013-2021 Canonical, Ltd.
 * Copyright (C) 2022-2023 Colin Ian King
 *
 */

#include <sys/ioctl.h>
#include <net/if.h>

int main(void)
{
	struct ifconf ifc = { };

	(void)ifc;

	return sizeof(ifc);
}
