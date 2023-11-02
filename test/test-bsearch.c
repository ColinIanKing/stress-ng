// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2022-2023 Colin Ian King
 *
 */
#include <search.h>
#include <stdlib.h>

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
	int data[] = { 1, 2, 4, 8 };
	int val = 2;
	void *ptr;

	ptr = bsearch((void *)&val, (void *)data, 4, sizeof(data[0]), cmp);

	return ptr == &data[1];
}
