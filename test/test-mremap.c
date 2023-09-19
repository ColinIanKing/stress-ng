// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2013-2021 Canonical, Ltd.
 * Copyright (C) 2022-2023 Colin Ian King
 *
 */

#define _GNU_SOURCE
#include <sys/mman.h>
#include <stddef.h>

int main(void)
{
	void *newbuf, *newaddr = NULL;
	size_t sz = 4096;

	/* semantically not correct, but this is a build check */
	newbuf = mremap(NULL, sz, sz,
			MREMAP_FIXED | MREMAP_MAYMOVE, (void *)newaddr);
	(void)newbuf;

	return 0;
}
