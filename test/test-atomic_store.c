// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2021-2023 Colin Ian King
 *
 */

int main(int argc, char **argv)
{
	int val = 1, var;

	__atomic_store(&var, &val, 0);

	return 0;
}

