// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2017-2021 Canonical, Ltd.
 * Copyright (C) 2022-2023 Colin Ian King
 *
 */
#include <stdint.h>

int main(int argc, char **argv)
{
	uint64_t var;

	__atomic_fetch_add_8(&var, 1, 0);

	return 0;
}

