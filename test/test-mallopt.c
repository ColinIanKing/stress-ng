// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2013-2021 Canonical, Ltd.
 * Copyright (C) 2022-2023 Colin Ian King
 *
 */

#include <malloc.h>

int main(void)
{
	return mallopt(M_MMAP_THRESHOLD, 1024*1024);
}
