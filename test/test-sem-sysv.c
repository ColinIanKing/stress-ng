/*
 * Copyright (C) 2013-2021 Canonical, Ltd.
 * Copyright (C) 2022-2025 Colin Ian King
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
 */
#define  _GNU_SOURCE

#include <time.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/sem.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

#if defined(__gnu_hurd__)
#error semop, semget and semctl are not implemented
#endif

typedef union _semun {
	int              val;   /* Value for SETVAL */
	struct semid_ds *buf;   /* Buffer for IPC_STAT, IPC_SET */
	unsigned short int *array; /* Array for GETALL, SETALL */
	struct seminfo  *__buf; /* Buffer for IPC_INFO (Linux-specific) */
} semun_t;

int main(void)
{
	key_t key;
	int sem;
	semun_t arg;
	int ret;
	struct sembuf semwait, semsignal;
	struct timespec timeout;

	/*
	 * This is not meant to be functionally
	 * correct, it is just used to check we
	 * can build minimal POSIX semaphore
	 * based code
	 */

	key = (key_t)getpid();
	sem = semget(key, 1, IPC_CREAT | S_IRUSR | S_IWUSR);
	arg.val = 1;
	arg.buf = NULL;
	arg.array = NULL;
	arg.__buf = NULL;
	ret = semctl(sem, 0, SETVAL, arg);
	(void)ret;
	semwait.sem_num = 0;
	semwait.sem_op = -1;
	semwait.sem_flg = SEM_UNDO;
	(void)semwait;
	(void)clock_gettime(CLOCK_REALTIME, &timeout);
	timeout.tv_sec++;
#if defined(__linux__)
	ret = semtimedop(sem, &semwait, 1, &timeout);
	(void)ret;
#endif

	semsignal.sem_num = 0;
	semsignal.sem_op = 1;
	semsignal.sem_flg = SEM_UNDO;

	ret = semop(sem, &semsignal, 1);
	(void)ret;

#if defined(IPC_STAT)
	{
		struct semid_ds ds;
		semun_t s;

		s.buf = &ds;
		ret = semctl(sem, 0, IPC_STAT, &s);
		(void)ret;
	}
#endif
#if defined(SEM_STAT)
	{
		struct semid_ds ds;
		semun_t s;

		s.buf = &ds;
		ret = semctl(sem, 0, SEM_STAT, &s);
		(void)ret;
	}
#endif
#if defined(IPC_INFO) &&	\
    defined(__linux__)
	{
		struct seminfo si;
		semun_t s;

		s.__buf = &si;
		ret = semctl(sem, 0, IPC_INFO, &s);
		(void)ret;
	}
#endif
#if defined(SEM_INFO) &&	\
    defined(__linux__)
	{
		struct seminfo si;
		semun_t s;

		s.__buf = &si;
		ret = semctl(sem, 0, SEM_INFO, &s);
		(void)ret;
	}
#endif
#if defined(GETVAL)
	ret = semctl(sem, 0, GETVAL);
	(void)ret;
#endif
#if defined(GETPID)
	ret = semctl(sem, 0, GETPID);
	(void)ret;
#endif
#if defined(GETNCNT)
	ret = semctl(sem, 0, GETNCNT);
	(void)ret;
#endif
#if defined(GEZCNT)
	ret = semctl(sem, 0, GETZCNT);
	(void)ret;
#endif
	ret = semctl(sem, 0, IPC_RMID);
	(void)ret;

	return 0;
}
