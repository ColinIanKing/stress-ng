// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2023 Luis Chamberlain <mcgrof@kernel.org>
 *
 */
#include <linux/module.h>

int main(void)
{
	return delete_module("hello", 0);
}
