// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2022-2023 Colin Ian King
 *
 */

#include <utime.h>

int main(void)
{
	struct utimbuf buf;

	return utime(".", &buf);
}
