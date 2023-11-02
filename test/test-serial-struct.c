// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2013-2021 Canonical, Ltd.
 * Copyright (C) 2022-2023 Colin Ian King
 *
 */
#define _GNU_SOURCE

#include <linux/serial.h>
#include <string.h>

int main(void)
{
	struct serial_struct serial;

	(void)memset(&serial, 0, sizeof(serial));

	(void)serial;

	return 0;
}
