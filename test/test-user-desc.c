// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2013-2021 Canonical, Ltd.
 * Copyright (C) 2022-2023 Colin Ian King.
 *
 */
#define _GNU_SOURCE

#include <asm/ldt.h>
#include <string.h>

int main(void)
{
	struct user_desc u_info;

	(void)memset(&u_info, 0, sizeof(u_info));

	(void)u_info;

	return 0;
}
