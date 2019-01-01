/*
 * Copyright (C) 2013-2019 Canonical, Ltd.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 *
 * This code is a complete clean re-write of the stress tool by
 * Colin Ian King <colin.king@canonical.com> and attempts to be
 * backwardly compatible with the stress tool by Amos Waterland
 * <apw@rossby.metr.ou.edu> but has more stress tests and more
 * functionality.
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
#if defined(__linux__) && defined(IPC_INFO)
	{
		struct shminfo s;

		ret = shmctl(shm_id, IPC_INFO, (struct shmid_ds *)&s);
		(void)ret;
	}
#endif
#if defined(__linux__) && defined(SHM_INFO)
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
