// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2013-2021 Canonical, Ltd.
 * Copyright (C) 2022-2023 Colin Ian King.
 *
 */

#include <unistd.h>
#include <sys/swap.h>

int main(void)
{
	static char swapfile[] = "test.swap";
	int ret;

	ret = swapon(swapfile, 0);
	(void)ret;
	ret = swapoff(swapfile);
	(void)ret;

	return 0;
}
