// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C)      2023 Colin Ian King
 *
 */

#define STRESS_PRAGMA_S390_NO_HARD_DFP _Pragma("GCC target (\"no-hard-dfp\")")

STRESS_PRAGMA_S390_NO_HARD_DFP

int main(void)
{
	_Decimal32 a = 1.2;
	_Decimal32 b = 3.8;
	_Decimal32 c = a + b;

	return (int)(double)c;
}
