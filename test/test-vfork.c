// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2022-2023 Colin Ian King
 *
 */

#include <unistd.h>
#include <stdlib.h>

int main(void)
{
	pid_t pid = 0;

	pid = vfork();
	_exit((int)pid);
}
