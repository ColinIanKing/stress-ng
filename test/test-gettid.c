// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2021-2023 Colin Ian King.
 *
 */
#define _GNU_SOURCE
#include <unistd.h>

int main(void)
{
	return gettid();
}
