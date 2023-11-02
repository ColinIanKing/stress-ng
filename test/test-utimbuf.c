// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2022-2023 Colin Ian King
 *
 */

#include <utime.h>
#include <string.h>

int main(void)
{
	struct utimbuf buf;

	(void)memset(&buf, 0, sizeof(buf));

	return sizeof(buf);
}
