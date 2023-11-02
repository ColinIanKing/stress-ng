// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2022-2023 Colin Ian King
 *
 */

#include <stdio.h>
#include <mntent.h>

int main(void)
{
	FILE *fp;

	fp = setmntent("/etc/mtab", "r");
	return (fp != NULL);
}
