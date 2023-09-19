// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2017-2021 Canonical, Ltd.
 * Copyright (C) 2022-2023 Colin Ian King
 *
 */
#include <math.h>

int main(int argc, char **argv)
{
	double x = 1.48734 + (double)argc;

	return (int)__builtin_sqrt(x);
}

