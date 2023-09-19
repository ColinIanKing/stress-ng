// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2022-2023 Colin Ian King.
 *
 */
#include <unistd.h>

int main(void)
{
	char buf[10];

	return getentropy(buf, sizeof(buf));
}
