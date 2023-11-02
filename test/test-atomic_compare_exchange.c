// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2022-2023 Colin Ian King
 *
 */

int main(int argc, char **argv)
{
	int var = 0, zero = 0, one = 1;

	return __atomic_compare_exchange(&var, &zero, &one, 0, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
}

