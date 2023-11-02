// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2013-2021 Canonical, Ltd.
 * Copyright (C) 2022-2023 Colin Ian King
 *
 */
#include <asm/mtrr.h>

int main(void)
{
	struct mtrr_gentry m;

	(void)m;

	return sizeof(m);
}
