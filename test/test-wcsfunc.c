// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2013-2021 Canonical, Ltd.
 * Copyright (C) 2022-2023 Colin Ian King.
 *
 */
#define _GNU_SOURCE

#if defined(__APPLE__) || \
    defined(__DragonFly__) || \
    defined(__FreeBSD__) || \
    defined(__NetBSD__) || \
    defined(__OpenBSD__)
#include <wchar.h>
#if !defined(__APPLE__)
#include <sys/tree.h>
#endif
#else
#include <bsd/stdlib.h>
#include <bsd/string.h>
#include <bsd/sys/tree.h>
#if !defined(__FreeBSD_kernel__)
/* GNU/kFreeBSD does not support this */
#include <bsd/wchar.h>
#endif
#endif

#include <stddef.h>
#include <wchar.h>

static void *funcs[] = {
	WCSFUNC,
};

int main(void)
{
	return (ptrdiff_t)(funcs[0] == 0);
}
