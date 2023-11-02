// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2022, Red Hat Inc, Dorinda Bassey <dbassey@redhat.com>
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
	return 0;
}
