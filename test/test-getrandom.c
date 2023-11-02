// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2021-2023 Colin Ian King.
 *
 */
#include <sys/random.h>

int main(void)
{
	char buf[10];

	return (int)getrandom(buf, sizeof(buf), 0);
}
