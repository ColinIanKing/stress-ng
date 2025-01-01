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
#include <stdio.h>
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
	size_t i;

	for (i = 0; i < sizeof(pthread_funcs) / sizeof(pthread_funcs[0]); i++)
		printf("%p\n", pthread_funcs[i]);

	return 0;
}
