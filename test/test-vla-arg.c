// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2017-2021 Canonical, Ltd.
 * Copyright (C) 2022-2023 Colin Ian King.
 *
 */

void vla_arg_func(int n, int array[n])
{
	(void)n;
	(void)array;
}

int main(void)
{
	int data[32];

	vla_arg_func(sizeof(data) / sizeof(data[0]), data);
	return 0;
}
