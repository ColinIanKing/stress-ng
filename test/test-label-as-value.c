// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2013-2021 Canonical, Ltd.
 * Copyright (C) 2022-2023 Colin Ian King
 *
 */

int main(void)
{
	void *ptr;

label:
	ptr = &&label;	/* cppcheck-suppress internalAstError */
	goto *ptr;

	return 0;
}
