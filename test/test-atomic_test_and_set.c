// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2022-2023 Colin Ian King
 *
 */
#include <stdbool.h>

int main(int argc, char **argv)
{
	bool var = false;

	return (int)__atomic_test_and_set((void *)&var, __ATOMIC_ACQ_REL);
}
