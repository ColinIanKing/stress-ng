// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2013-2021 Canonical, Ltd.
 * Copyright (C) 2022-2023 Colin Ian King
 *
 */
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

int main(void)
{
	struct open_how how;

	(void)how;

	return sizeof(how);
}
