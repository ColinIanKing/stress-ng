// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2022-2023 Colin Ian King
 *
 */
#include <search.h>
#include <stdlib.h>

int main(void)
{
	ENTRY e;

	if (hcreate(128) == 0)
		return -1;

	e.key = "test";
	e.data = (void *)2;

	return (hsearch(e, ENTER) == NULL);
}
