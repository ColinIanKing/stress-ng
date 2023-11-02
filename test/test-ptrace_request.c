// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2022-2023 Colin Ian King
 *
 */
#define _GNU_SOURCE

#include <sys/ptrace.h>

int main(void)
{
	enum __ptrace_request request = 0;

	return (int)request;
}
