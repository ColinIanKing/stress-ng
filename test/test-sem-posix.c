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
