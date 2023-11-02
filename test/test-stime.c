// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2013-2021 Canonical, Ltd.
 * Copyright (C) 2022-2023 Colin Ian King.
 *
 */

#include <time.h>
#include <string.h>

int main(void)
{
	time_t t;

	(void)memset(&t, 0, sizeof(t));

	return stime(&t);
}
