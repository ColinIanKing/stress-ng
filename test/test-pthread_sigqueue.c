// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2013-2021 Canonical, Ltd.
 * Copyright (C) 2022-2023 Colin Ian King
 *
 */
#define _GNU_SOURCE

#include <signal.h>
#include <string.h>
#include <pthread.h>

int main(void)
{
	pthread_t thread;
	union sigval value;

	(void)memset(&thread, 0, sizeof(thread));
	(void)memset(&value, 0, sizeof(value));

	return pthread_sigqueue(thread, SIGKILL, value);
}
