// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2013-2021 Canonical, Ltd.
 * Copyright (C) 2022-2023 Colin Ian King.
 *
 */

#include <features.h>
#include "../core-version.h"

/*
 *  For now, only x86-64 systems with GNUC > 5.5 are known
 *  to support this attribute reliably.
 */
#if (defined(__GNUC__) &&	\
     defined(__GLIBC__) &&	\
     NEED_GNUC(5,5,0)) ||	\
    (defined(__clang__) &&	\
     NEED_CLANG(14,0,0))

#if defined(__x86_64__) ||	\
    defined(__x86_64) ||	\
    defined(__amd64__) ||	\
    defined(__amd64) || 	\
    defined(__PPC64__)
#else
#error arch not supported
#endif

#define TARGET_CLONES	__attribute__((target_clones(TARGET_CLONE)))

static int TARGET_CLONES have_target_clones(void)
{
	return 0;
}

int main(void)
{
	return have_target_clones();
}

#else
#error target clones attribute not supported
#endif
