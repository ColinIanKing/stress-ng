// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2013-2021 Canonical, Ltd.
 * Copyright (C) 2022-2023 Colin Ian King.
 *
 */
#define _GNU_SOURCE

#include <unistd.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/stat.h>
#include <fcntl.h>

int main(void)
{
	int shm_id, ret;
	key_t key;
	size_t sz = 64 * 1024;
	char *addr;

	/*
	 * This is not meant to be functionally
	 * correct, it is just used to check we
	 * can build minimal POSIX semaphore
	 * based code
	 */
	key = (key_t)getpid();

	shm_id = shmget(key, sz, IPC_CREAT | IPC_EXCL | S_IRUSR | S_IWUSR);
	if (shm_id < 0)
		return -1;
	addr = shmat(shm_id, NULL, 0);
	if (addr == (char *)-1)
		goto reap;

#if defined(IPC_STAT)
	{
		struct shmid_ds s;

		ret = shmctl(shm_id, IPC_INFO, (struct shmid_ds *)&s);
		(void)ret;
	}
#endif
#if defined(__linux__) &&	\
    defined(IPC_INFO)
	{
		struct shminfo s;

		ret = shmctl(shm_id, IPC_INFO, (struct shmid_ds *)&s);
		(void)ret;
	}
#endif
#if defined(__linux__) &&	\
    defined(SHM_INFO)
	{
		struct shm_info s;

		ret = shmctl(shm_id, SHM_INFO, (struct shmid_ds *)&s);
		(void)ret;
	}
#endif

	(void)shmdt(addr);
reap:
	ret = shmctl(shm_id, IPC_RMID, NULL);
	(void)ret;
	return 0;
}
