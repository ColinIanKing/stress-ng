// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2013-2021 Canonical, Ltd.
 * Copyright (C) 2022-2023 Colin Ian King
 *
 */
#define _GNU_SOURCE

#include <pthread.h>
#include <string.h>

int main(void)
{
	pthread_mutexattr_t mutex_attr;

	(void)memset(&mutex_attr, 0, sizeof(mutex_attr));

	return pthread_mutexattr_destroy(&mutex_attr);
}
