// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2013-2021 Canonical, Ltd.
 * Copyright (C) 2022-2023 Colin Ian King
 *
 */
#include <semaphore.h>
#include <pthread.h>

/* The following functions from libpthread are used by stress-ng */

static void *pthread_funcs[] = {
	(void *)pthread_create,
	(void *)pthread_join,
	(void *)pthread_mutex_lock,
	(void *)pthread_mutex_unlock,
	(void *)pthread_cond_wait,
	(void *)pthread_cond_broadcast,
	(void *)pthread_cond_destroy,
	(void *)pthread_mutex_destroy,
};

int main(void)
{
	return 0;
}
