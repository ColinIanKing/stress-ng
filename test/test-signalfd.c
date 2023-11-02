// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2013-2021 Canonical, Ltd.
 * Copyright (C) 2022-2023 Colin Ian King.
 *
 */

#include <sys/signalfd.h>
#include <string.h>

int main(void)
{
	sigset_t mask;

	(void)memset(&mask, 0, sizeof(mask));

	return signalfd(0, &mask, 0);
}
