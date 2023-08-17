// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2013-2021 Canonical, Ltd.
 * Copyright (C) 2022-2023 Colin Ian King
 *
 */
#include <linux/landlock.h>

int main(void)
{
	const enum landlock_rule_type type = LANDLOCK_RULE_PATH_BENEATH;

	(void)type;

	return (int)type;
}
