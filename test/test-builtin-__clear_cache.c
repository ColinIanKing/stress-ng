// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2017-2021 Canonical, Ltd.
 * Copyright (C) 2022-2023 Colin Ian King
 *
 */

int main(int argc, char **argv)
{
	static char buffer[4096];

	__builtin___clear_cache(buffer, buffer + sizeof(buffer));

	return 0;
}

