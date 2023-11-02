// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2021-2023 Colin Ian King
 *
 */

int main(int argc, char **argv)
{
	double val = 0.0, var = 5.0;

	__atomic_load(&var, &val, __ATOMIC_CONSUME);

	return 0;
}
