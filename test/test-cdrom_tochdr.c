// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2013-2021 Canonical, Ltd.
 * Copyright (C) 2022-2023 Colin Ian King
 *
 */
#include <linux/cdrom.h>
#include <string.h>

int main(void)
{
	struct cdrom_tochdr header;

	(void)memset(&header, 0, sizeof(header));
	(void)header;

	return sizeof(struct cdrom_tochdr);
}
