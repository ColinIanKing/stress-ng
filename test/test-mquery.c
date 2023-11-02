// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2013-2021 Canonical, Ltd.
 * Copyright (C) 2022-2023 Colin Ian King
 *
 */
#include <stdlib.h>
#include <sys/mman.h>

int main(void)
{
	return mquery(NULL, 4096, PROT_READ, MAP_FIXED, -1, 0);
}
