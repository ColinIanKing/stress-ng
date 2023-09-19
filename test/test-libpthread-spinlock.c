// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2013-2021 Canonical, Ltd.
 * Copyright (C) 2022-2023 Colin Ian King
 *
 */
#include <pthread.h>

/* The following functions from libpthread are used by stress-ng */

static void *pthread_funcs[] = {
	(void *)pthread_spin_lock,
	(void *)pthread_spin_unlock,
	(void *)pthread_spin_init,
	(void *)pthread_spin_destroy,
};

int main(void)
{
	return 0;
}
