// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2013-2021 Canonical, Ltd.
 * Copyright (C) 2022-2023 Colin Ian King
 *
 */
#include <inttypes.h>
#include "../core-version.h"

#if NEED_GNUC(4,0,0)
int main(void)
{
	const __uint128_t ui128 = 0;
	const __int128_t  i128 = 0;
}
#else
#error need GCC 4.0 or above
#endif

