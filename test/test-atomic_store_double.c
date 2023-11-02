// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2021-2023 Colin Ian King
 *
 */

int main(int argc, char **argv)
{
	double val = 1.0, var;

	__atomic_store(&var, &val, __ATOMIC_RELEASE);

	return 0;
}

