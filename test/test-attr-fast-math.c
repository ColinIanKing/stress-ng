// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2023      Colin Ian King
 *
 */
#define OPTIMIZE_FAST_MATH __attribute__((optimize("fast-math")))

static double OPTIMIZE_FAST_MATH do_math(double x, double y)
{
	return x * y + (x / y) / x;
}

int main(int argc, char **argv)
{
	return (int)do_math(10, 20.0 * (double)argc);
}
