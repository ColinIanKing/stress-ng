// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2022-2023 Colin Ian King
 *
 */
int main(int argc, char **argv)
{
	return __builtin_cpu_supports("avx");
}

