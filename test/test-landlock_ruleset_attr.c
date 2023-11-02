// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2013-2021 Canonical, Ltd.
 * Copyright (C) 2022-2023 Colin Ian King
 *
 */
#include <string.h>
#include <linux/landlock.h>

int main(void)
{
	struct landlock_ruleset_attr attr;

	(void)memset(&attr, 0, sizeof(attr));
	(void)attr;

	return sizeof(attr);
}
