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
