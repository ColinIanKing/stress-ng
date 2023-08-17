// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2022-2023 Colin Ian King.
 *
 */
#include <linux/usbdevice_fs.h>

int main(void)
{
	struct usbdevfs_getdriver dr;

	return sizeof(dr);
}
