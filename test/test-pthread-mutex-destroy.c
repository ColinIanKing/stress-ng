// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2022-2023 Colin Ian King
 *
 */
#define _GNU_SOURCE

#include <pthread.h>
#include <string.h>

int main(void)
{
	pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;

	return pthread_mutex_destroy(&mutex);

}
