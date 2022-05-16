/*
 * Copyright (C) 2022-2022 Colin Ian King.
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

int main(void)
{
	const uint32_t size_x = 16, size_y = 16;
	unsigned char gpu_size[size_x * size_y], *ptr;
	EGLDisplay display;
	EGLSurface surface;
	EGLint prioext = EGL_NONE;
	EGLint priolevel = EGL_NONE;
	EGLint numConfigs;
	EGLConfig config;

	(void)memset(gpu_size, 0, sizeof(gpu_size));

	EGLint major, minor;

	display = eglGetPlatformDisplay(EGL_PLATFORM_GBM_KHR, (void *) "invalid", NULL);
	if (display == EGL_NO_DISPLAY) {
		printf("Got an EGLDisplay for an invalid vendor name.\n");
        return 1;
	}

	EGLDisplay d = eglGetDisplay(EGL_DEFAULT_DISPLAY);
	if (!eglInitialize(d, &major, &minor)) {
		printf("eglInitialize failed\n");
		return 1;
	}

	if ( !eglGetConfigs(d, NULL, 0, &numConfigs) )
	{
		printf("There are no EGL configs \n");
		return 1;
	}

	if(eglBindAPI(EGL_OPENGL_ES_API) == EGL_FALSE){
		printf("EGL: Failed to bind OpenGL ES \n");
		return 1;
	}

	EGLContext context;
	const EGLint contextAttribs[] = {
		EGL_CONTEXT_CLIENT_VERSION, 2,
		prioext, priolevel,
		EGL_NONE
	};
	if((context = eglCreateContext(d, config, EGL_NO_CONTEXT, contextAttribs)) == EGL_NO_CONTEXT){
		printf("Failed to create context \n");
		return 1;
	}
	if(eglMakeCurrent(d, surface, surface, context) == EGL_FALSE){
		printf("Failed to make context current \n");
		return 1;
	}

	return 0;

}