// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2013-2021 Canonical, Ltd.
 * Copyright (C) 2022-2023 Colin Ian King
 *
 */

#include <stdlib.h>

int main(void)
{
	char *name;

	name = getexecname();
	return name != NULL;
}
