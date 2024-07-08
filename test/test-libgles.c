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
#include <stdlib.h>
#include <GLES2/gl2.h>

/* The following functions from libGLESv2 are used by stress-ng */
static void *gl_funcs[] = {
	(void *)glCreateShader,
	(void *)glShaderSource,
	(void *)glCompileShader,
	(void *)glGetShaderiv,
	(void *)glGetShaderInfoLog,
	(void *)glDeleteShader,
	(void *)glCreateProgram,
	(void *)glAttachShader,
	(void *)glLinkProgram,
	(void *)glGetProgramiv,
	(void *)glGetProgramInfoLog,
	(void *)glDeleteProgram,
	(void *)glUseProgram,
	(void *)glGetString,
	(void *)glClearColor,
	(void *)glViewport,
	(void *)glGetUniformLocation,
	(void *)glUniform1i,
	(void *)glGetAttribLocation,
	(void *)glEnableVertexAttribArray,
	(void *)glVertexAttribPointer,
	(void *)glGetIntegerv,
	(void *)glGenTextures,
	(void *)glBindTexture,
	(void *)glTexImage2D,
	(void *)glClear,
	(void *)glDrawArrays,
	(void *)glFinish,
	(void *)glGetError,

};

/* This program does nothing as it is intended to be a compile-only check*/
int main(void)
{
	size_t i;

	for (i = 0; i < sizeof(gl_funcs) / sizeof(gl_funcs[0]); i++)
		printf("%p\n", gl_funcs[i]);
	return 0;
}
