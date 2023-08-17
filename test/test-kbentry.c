// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2013-2021 Canonical, Ltd.
 * Copyright (C) 2022-2023 Colin Ian King
 *
 */
#include <linux/kd.h>

int main(void)
{
	struct kbentry k;

	(void)k;

	return sizeof(k);
}
