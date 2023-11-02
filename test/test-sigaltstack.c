// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2013-2021 Canonical, Ltd.
 * Copyright (C) 2022-2023 Colin Ian King.
 *
 */

#include <stdio.h>
#include <signal.h>

#define SZ	65536

int main(void)
{
	stack_t ss;
	static char stack[SZ];

	ss.ss_sp = stack;
	ss.ss_size = SZ;
	ss.ss_flags = 0;

	return sigaltstack(&ss, NULL);
}
