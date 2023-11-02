// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2022-2023 Colin Ian King
 *
 */

#include <linux/fs.h>

int main(void)
{
	struct fsxattr attr;

	return sizeof(attr);
}
