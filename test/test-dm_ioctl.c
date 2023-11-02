// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2013-2021 Canonical, Ltd.
 * Copyright (C) 2022-2023 Colin Ian King
 *
 */
#include <linux/dm-ioctl.h>

int main(void)
{
	struct dm_ioctl d;

	(void)d;

	return sizeof(d);
}
