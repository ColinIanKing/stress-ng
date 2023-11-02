// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2016-2021 Canonical, Ltd.
 * Copyright (C) 2022-2023 Colin Ian King
 *
 */
#include <string.h>
#if defined(__APPLE__) || \
    defined(__DragonFly__) || \
    defined(__FreeBSD__) || \
    defined(__NetBSD__) || \
    defined(__OpenBSD__)
#include <stdlib.h>
#else
#include <bsd/stdlib.h>
#endif

static int intcmp(const void *p1, const void *p2)
{
        const int *i1 = (int *)p1;
        const int *i2 = (int *)p2;

	return *i1 - *i2;
}

int main(void)
{
	int data[64];
	int rc;

	(void)memset(data, 0, sizeof(data));

	rc = heapsort(data, 64, sizeof(*data), intcmp);
	(void)rc;
	rc = mergesort(data, 64, sizeof(*data), intcmp);
	(void)rc;
	rc = radixsort(NULL, 0, NULL, 0);
	(void)rc;

	return 0;
}
