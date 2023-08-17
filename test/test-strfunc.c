// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2013-2021 Canonical, Ltd.
 * Copyright (C) 2022-2023 Colin Ian King.
 *
 */
#define _GNU_SOURCE

#include <stddef.h>
#include <string.h>

#if !(defined(__APPLE__) || \
      defined(__DragonFly__) || \
      defined(__FreeBSD__) || \
      defined(__NetBSD__) || \
      defined(__OpenBSD__))
#include <bsd/string.h>
#endif

static void *funcs[] = {
	STRFUNC,
};

int main(void)
{
	return (ptrdiff_t)(funcs[0] == 0);
}
