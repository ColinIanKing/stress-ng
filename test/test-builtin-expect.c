// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2017-2021 Canonical, Ltd.
 * Copyright (C) 2022-2023 Colin Ian King
 *
 */
#include <math.h>

#define LIKELY(x)	__builtin_expect((x),1)
#define UNLIKELY(x)	__builtin_expect((x),0)

int main(int argc, char **argv)
{
	if (LIKELY(argc == 1))
		return 0;
	if (LIKELY(argc > 0))
		return 1;
	if (UNLIKELY(argc < 0))
		return -1;
	return 0;
}
