// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2013-2021 Canonical, Ltd.
 * Copyright (C) 2022-2023 Colin Ian King.
 *
 */

#include <string.h>
#include <sys/time.h>

int main(void)
{
	struct timeval delta, tv;

	(void)memset(&delta, 0, sizeof(delta));

	return adjtime(&delta, &tv);
}
