// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2021-2023 Colin Ian King
 *
 */

int main(int argc, char **argv)
{
	int val = 0, var = 5;

	__atomic_load(&var, &val, 0);

	return 0;
}

