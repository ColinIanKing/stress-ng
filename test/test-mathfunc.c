// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2013-2021 Canonical, Ltd.
 * Copyright (C) 2022-2023 Colin Ian King
 *
 */

#include <math.h>
#include <complex.h>
#include <stddef.h>

static void *funcs[] = {
	MATHFUNC,
};

int main(void)
{
	return (ptrdiff_t)&MATHFUNC + (funcs[0] == 0);
}
