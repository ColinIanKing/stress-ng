// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2013-2021 Canonical, Ltd.
 * Copyright (C) 2022-2023 Colin Ian King.
 *
 */

#include <sys/timex.h>

int main(void)
{
	struct timex buf;

	buf.modes = 0;
	return adjtimex(&buf);
}
