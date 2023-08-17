// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2013-2021 Canonical, Ltd.
 * Copyright (C) 2022-2023 Colin Ian King
 *
 */

#define STRESS_PRAGMA_PUSH      _Pragma("GCC diagnostic push")
#define STRESS_PRAGMA_POP       _Pragma("GCC diagnostic pop")

STRESS_PRAGMA_PUSH
static void test_pragma(void)
{
}
STRESS_PRAGMA_POP

int main(void)
{
	test_pragma();
	return 0;
}
