// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2017-2021 Canonical, Ltd.
 * Copyright (C) 2022-2023 Colin Ian King
 *
 */

int main(int argc, char **argv)
{
	int var;

	__atomic_sub_fetch(&var, 1, 0);

	return 0;
}

