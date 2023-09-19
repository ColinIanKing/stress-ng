// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2021-2023 Colin Ian King
 *
 */
int main(int argc, char **argv)
{
	int var;

	__atomic_fetch_sub(&var, 1, 0);

	return 0;
}

