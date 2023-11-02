// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2021-2023 Colin Ian King
 *
 */

#define _GNU_SOURCE

#include <stdlib.h>

int main(void)
{
	double loadavg[3];

	return getloadavg(loadavg, 3);
}
