// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2013-2021 Canonical, Ltd.
 * Copyright (C) 2022-2023 Colin Ian King.
 *
 */
#include <ucontext.h>

void func(void)
{
	/* Just return */
}


int main(void)
{
	static unsigned char stack[63336];

	ucontext_t u1, u2;
	if (getcontext(&u2) < 0)
		return -1;
	u2.uc_stack.ss_sp = stack;
	u2.uc_stack.ss_size = sizeof(stack);
	u2.uc_link = &u1;

	makecontext(&u2, func, 0);

	if (swapcontext(&u1, &u2) < 0)
		return -1;
	return 0;
}
