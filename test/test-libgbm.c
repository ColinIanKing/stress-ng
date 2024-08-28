/*
 * Copyright (C) 2022, Red Hat Inc, Dorinda Bassey <dbassey@redhat.com>
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
#include <fcntl.h>
#include <unistd.h>
#include <gbm.h>

/* The following functions from libgbm are used by stress-ng */

static void *gbm_funcs[] = {
	(void *)gbm_create_device,
	(void *)gbm_surface_create,
};

/* This program does nothing as it is intended to be a compile-only check*/
int main(void)
{
	size_t i;

	for (i = 0; i < sizeof(gbm_funcs) / sizeof(gbm_funcs[0]); i++)
		printf("%p\n", gbm_funcs[i]);

	return 0;
}
