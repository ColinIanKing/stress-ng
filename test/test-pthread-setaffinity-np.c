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
	pthread_t pthread;
	cpu_set_t cpuset;

	(void)memset(&pthread, 0, sizeof(pthread));
	CPU_ZERO(&cpuset);
	CPU_SET(1, &cpuset);

	return pthread_setaffinity_np(pthread, sizeof(cpuset), &cpuset);
}
