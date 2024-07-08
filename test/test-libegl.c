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
#include <string.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>

/* The following functions from libegl are used by stress-ng */

static void *egl_funcs[] = {
	(void *)eglGetConfigs,
	(void *)eglChooseConfig,
	(void *)eglGetConfigAttrib,
	(void *)eglGetPlatformDisplay,
	(void *)eglInitialize,
	(void *)eglBindAPI,
	(void *)eglCreatePlatformWindowSurface,
	(void *)eglCreateContext,
	(void *)eglMakeCurrent,
};

/* This program does nothing as it is intended to be a compile-only check*/
int main(void)
{
	size_t i;

	for (i = 0; i < sizeof(egl_funcs) / sizeof(egl_funcs[0]); i++)
		printf("%p\n", egl_funcs[i]);
	return 0;
}
