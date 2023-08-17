// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2013-2021 Canonical, Ltd.
 * Copyright (C) 2022-2023 Colin Ian King
 *
 */

#include <stdio.h>
#include <mntent.h>

int main(void)
{
	FILE *mounts;
	struct mntent* mount;

	mounts = setmntent("/etc/mtab", "r");
	if (!mounts)
		return 1;
	while ((mount = getmntent(mounts)) != NULL)
		(void)printf("%s\n", mount->mnt_fsname);

	(void)endmntent(mounts);
	return 0;
}
