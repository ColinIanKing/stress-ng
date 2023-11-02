// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2022-2023 Colin Ian King
 *
 */

#include <malloc.h>

int main(void)
{
	return malloc_trim((size_t)0);
}
