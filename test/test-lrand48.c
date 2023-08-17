// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2022-2023 Colin Ian King
 *
 */
#include <stdlib.h>

int main(void)
{
	long r = lrand48();

	return (int)r;
}
