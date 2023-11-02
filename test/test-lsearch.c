// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2022-2023 Colin Ian King
 *
 */
#include <search.h>
#include <stdlib.h>
#include <string.h>

static int cmp(const void *p1, const void *p2)
{
	const int *i1 = (const int *)p1;
	const int *i2 = (const int *)p2;

	if (*i1 > *i2)
		return 1;
	else if (*i1 < *i2)
		return -1;
	else
		return 0;
}

int main(void)
{
	int val = 32, root[10];
	size_t n = 0;

	(void)memset(root, 0, sizeof(root));

	(void)lsearch((void *)&val, (void *)root, &n, sizeof(root[0]), cmp);
}
