// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2022-2023 Colin Ian King
 *
 */
#define _GNU_SOURCE

#include <fcntl.h>
#include <unistd.h>

int main(void)
{

	return symlinkat("target", -1, "test");
}
