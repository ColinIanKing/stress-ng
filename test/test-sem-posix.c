// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2013-2021 Canonical, Ltd.
 * Copyright (C) 2022-2023 Colin Ian King
 *
 */

#include <time.h>
#include <semaphore.h>

#if defined(__FreeBSD_kernel__)
#error POSIX semaphores not yet implemented
#endif

int main(void)
{
	sem_t sem;
	int ret;
	struct timespec timeout;

	timeout.tv_sec = 0;
	timeout.tv_nsec = 1000000;

	/*
	 * This is not meant to be functionally
	 * correct, it is just used to check we
	 * can build minimal POSIX semaphore
	 * based code
	 */
	ret = sem_init(&sem, 1, 1);
	(void)ret;
	ret = sem_wait(&sem);
	(void)ret;
	ret = sem_post(&sem);
	(void)ret;
	ret = sem_trywait(&sem);
	(void)ret;
	ret = sem_timedwait(&sem, &timeout);
	(void)ret;
	ret = sem_destroy(&sem);
	(void)ret;

	return 0;
}
