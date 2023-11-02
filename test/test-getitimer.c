// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2022-2023 Colin Ian King
 *
 */
#define _GNU_SOURCE

#include <string.h>
#include <sys/time.h>

int main(void)
{
	struct itimerval old;

	(void)memset(&old, 0, sizeof(old));

	return getitimer(ITIMER_REAL, &old);
}
