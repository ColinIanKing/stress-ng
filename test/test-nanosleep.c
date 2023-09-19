// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2013-2021 Canonical, Ltd.
 * Copyright (C) 2022-2023 Colin Ian King
 *
 */

#include <time.h>

int main(void)
{
	struct timespec req, rem;

	req.tv_sec = 0;
	req.tv_nsec = 100000;

	rem.tv_sec = 0;
	rem.tv_nsec = 0;

	return nanosleep(&req, &rem);
}
