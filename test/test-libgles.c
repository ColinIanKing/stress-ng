// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2022, Red Hat Inc, Dorinda Bassey <dbassey@redhat.com>
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
	return 0;
}
