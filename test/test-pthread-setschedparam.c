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
	struct sched_param param;

	(void)memset(&pthread, 0, sizeof(pthread));
	(void)memset(&param, 0, sizeof(param));

	return pthread_setschedparam(pthread, 0, &param);
}
