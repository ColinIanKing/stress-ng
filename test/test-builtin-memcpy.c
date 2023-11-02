// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2017-2021 Canonical, Ltd.
 * Copyright (C) 2022-2023 Colin Ian King
 *
 */

int main(int argc, char **argv)
{
	void *ptr;
	unsigned long dst;
	unsigned long src = ~0;

	ptr = __builtin_memcpy(&dst, &src, sizeof(dst));
	(void)ptr;

	return 0;
}

