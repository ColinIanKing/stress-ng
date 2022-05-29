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
#include "stress-ng.h"

#if defined(HAVE_EGL_H)
#include <EGL/egl.h>
#endif
#if defined(HAVE_EGL_EXT_H)
#include <EGL/eglext.h>
#endif
#if defined(HAVE_GLES2_H)
#include <GLES2/gl2.h>
#endif

#if defined(HAVE_GBM_H)
#include <gbm.h>
#endif

static const stress_help_t help[] = {
	{NULL, "gpu N", "start N worker"},
	{NULL, "gpu-ops N", "stop after N gpu render bogo operations"},
	{NULL, "gpu-frag N", "specify shader core usage per pixel"},
	{NULL, "gpu-tex-size N", "specify upload texture NxN"},
	{NULL, "gpu-upload N", "specify upload texture N times per frame"},
	{NULL, "gpu-xsize X", "specify framebuffer size x"},
	{NULL, "gpu-ysize Y", "specify framebuffer size y"},
	{NULL, NULL, NULL}
};

static int stress_set_gpu(const char *opt, const char *name, const size_t max)
{
	int32_t gpu32;
	int64_t gpu64;

	gpu64 = stress_get_uint64(opt);
	stress_check_range(name, gpu64, 1, max);
	gpu32 = (uint32_t)gpu64;
	return stress_set_setting(name, TYPE_ID_INT32, &gpu32);
}

static int stress_set_gpu_frag(const char *opt)
{
	return stress_set_gpu(opt, "gpu-frag", INT_MAX);
}

static int stress_set_gpu_xsize(const char *opt)
{
	return stress_set_gpu(opt, "gpu-xsize", INT_MAX);
}

static int stress_set_gpu_ysize(const char *opt)
{
	return stress_set_gpu(opt, "gpu-ysize", INT_MAX);
}

static int stress_set_gpu_gl(const char *opt, const char *name, const size_t max)
{
	int gpu_val;
	gpu_val = stress_get_int32(opt);
	stress_check_range(name, gpu_val, 1, max);
	return stress_set_setting(name, TYPE_ID_INT32, &gpu_val);
}

static int stress_set_gpu_upload(const char *opt)
{
	return stress_set_gpu_gl(opt, "gpu-upload", INT_MAX);
}

static int stress_set_gpu_size(const char *opt)
{
	return stress_set_gpu_gl(opt, "gpu-tex-size", INT_MAX);
}

static const stress_opt_set_func_t opt_set_funcs[] = {
	{OPT_gpu_frag, stress_set_gpu_frag},
	{OPT_gpu_uploads, stress_set_gpu_upload},
	{OPT_gpu_size, stress_set_gpu_size},
	{OPT_gpu_xsize, stress_set_gpu_xsize},
	{OPT_gpu_ysize, stress_set_gpu_ysize},
	{0, NULL}
};

#if defined(HAVE_LIB_EGL) && \
	defined(HAVE_EGL_H) && \
	defined(HAVE_EGL_EXT_H) && \
	defined(HAVE_LIB_GLES2) && \
	defined(HAVE_GLES2_H) && \
	defined(HAVE_LIB_GBM) && \
	defined(HAVE_GBM_H)

static GLuint program;
static EGLDisplay display;
static EGLSurface surface;
static struct gbm_device *gbm;
static struct gbm_surface *gs;

static const char *devicenode = "/dev/dri/renderD128";
static GLubyte *teximage = NULL;

static const char vert_shader[] =
    "attribute vec4 pos;\n"
    "attribute vec4 color;\n"
    "varying vec4 v_color;\n"
    "void main()\n" "{\n" "v_color = color;\n" "gl_Position = pos;\n" "}\n";

static const char frag_shader[] =
    "precision mediump float;\n"
    "varying vec4 v_color;\n"
    "uniform int frag_n;\n"
    "void main()\n"
    "{\n"
    "int i;\n"
    "vec4 a = v_color;\n"
    "for (i = 0; i < frag_n; i++) {\n"
    "float f = float(i);\n"
    "a = a / clamp(sin(f) * exp(f), 0.1, 0.9);\n"
    "}\n"
    "a = clamp(a, -1.0, 1.0);\n"
    "gl_FragColor = v_color + 0.000001 * a;\n" "}\n";

static GLuint compile_shader(const char *text, const int size, const GLenum type)
{
	GLuint shader;
	GLint compiled;
	const GLchar *source[1] = { text };

	shader = glCreateShader(type);
	glShaderSource(shader, 1, source, &size);
	glCompileShader(shader);
	glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);
	if (!compiled) {
		GLint infoLen = 0;

		glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &infoLen);
		if (infoLen > 1) {
			char *infoLog = malloc(infoLen);
			glGetShaderInfoLog(shader, infoLen, NULL, infoLog);
			pr_inf("Error compiling shader:\n%s\n", infoLog);
			free(infoLog);
		}
		glDeleteShader(shader);
		return 0;
	}
	return shader;
}

void load_shaders(void)
{
	GLint linked;
	GLuint vertexShader;
	GLuint fragmentShader;

	if ((vertexShader =
	     compile_shader(vert_shader, sizeof(vert_shader),
			    GL_VERTEX_SHADER)) == 0)
		pr_inf("ERROR: Failed to compile vertex shader\n");
	if ((fragmentShader =
	     compile_shader(frag_shader, sizeof(frag_shader),
			    GL_FRAGMENT_SHADER)) == 0)
		pr_inf("ERROR: Failed to compile fragment shader\n");
	if ((program = glCreateProgram()) == 0)
		pr_inf("Error creating the shader program\n");
	glAttachShader(program, vertexShader);
	glAttachShader(program, fragmentShader);
	glLinkProgram(program);
	glGetProgramiv(program, GL_LINK_STATUS, &linked);
	if (!linked) {
		GLint infoLen = 0;
		glGetProgramiv(program, GL_INFO_LOG_LENGTH, &infoLen);
		if (infoLen > 1) {
			char *infoLog = malloc(infoLen);
			glGetProgramInfoLog(program, infoLen, NULL, infoLog);
			pr_inf("Error linking shader program:\n%s\n", infoLog);
			free(infoLog);
		}
		glDeleteProgram(program);
		exit(EXIT_FAILURE);
	}

	glUseProgram(program);
}

static const GLfloat vertex[] = {
	-1, -1, 0, 1,
	-1, 1, 0, 1,
	1, 1, 0, 1,

	-1, -1, 0, 1,
	1, 1, 0, 1,
	1, -1, 0, 1,
};

static const GLfloat color[] = {
	1, 0, 0, 1,
	0, 1, 0, 1,
	0, 0, 1, 1,

	1, 0, 0, 1,
	0, 0, 1, 1,
	1, 1, 0, 1,
};

void gles2_init(const uint32_t width, const uint32_t height, const int frag_n, const GLsizei texsize)
{
	pr_inf("GL_VENDOR: %s\n", (char *)glGetString(GL_VENDOR));
	pr_inf("GL_VERSION: %s\n", (char *)glGetString(GL_VERSION));
	pr_inf("GL_RENDERER: %s\n", (char *)glGetString(GL_RENDERER));

	load_shaders();

	glClearColor(0, 0, 0, 0);
	glViewport(0, 0, width, height);

	GLint ufrag_n = glGetUniformLocation(program, "frag_n");
	glUniform1i(ufrag_n, frag_n);
	if (glGetError() != GL_NO_ERROR)
		pr_inf("ERROR: Failed to get the storage location of %d\n",
		       frag_n);

	GLint apos = glGetAttribLocation(program, "pos");
	glEnableVertexAttribArray(apos);
	glVertexAttribPointer(apos, 4, GL_FLOAT, 0, 0, vertex);

	GLint acolor = glGetAttribLocation(program, "color");
	glEnableVertexAttribArray(acolor);
	glVertexAttribPointer(acolor, 4, GL_FLOAT, 0, 0, color);

	if (texsize > 0) {
		GLint maxsize;
		glGetIntegerv(GL_MAX_TEXTURE_SIZE, &maxsize);
		if (texsize > maxsize)
			pr_inf("ERROR: Image size exceeds max texture size \n");

		GLuint texobj = 0;
		glGenTextures(1, &texobj);
		glBindTexture(GL_TEXTURE_2D, texobj);

		GLint bytesPerImage = texsize * texsize * 4;
		teximage = malloc(bytesPerImage);
	}
}

static void stress_gpu_run(const GLsizei texsize, const GLsizei uploads)
{
	if (texsize > 0) {
		for (int i = 0; i < uploads; i++) {
			glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, texsize,
				     texsize, 0, GL_RGBA, GL_UNSIGNED_BYTE,
				     teximage);
		}
	}
	glClear(GL_COLOR_BUFFER_BIT);
	glDrawArrays(GL_TRIANGLES, 0, 6);
	glFinish();
}

EGLConfig get_config(void)
{
	EGLint egl_config_attribs[] = {
		EGL_BUFFER_SIZE, 32,
		EGL_DEPTH_SIZE, EGL_DONT_CARE,
		EGL_STENCIL_SIZE, EGL_DONT_CARE,
		EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
		EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
		EGL_NONE,
	};

	EGLint num_configs;
	if (eglGetConfigs(display, NULL, 0, &num_configs) == EGL_FALSE)
		pr_inf("EGL: There are no EGL configs \n");

	EGLConfig *configs = malloc(num_configs * sizeof(EGLConfig));
	if ((eglChooseConfig(display, egl_config_attribs,
			     configs, num_configs,
			     &num_configs) == EGL_FALSE) || (num_configs == 0))
		pr_inf("EGL: can't choose EGL config \n");

	for (int i = 0; i < num_configs; ++i) {
		EGLint gbm_format;

		if (eglGetConfigAttrib(display, configs[i],
				       EGL_NATIVE_VISUAL_ID,
				       &gbm_format) == EGL_FALSE)
			pr_inf("EGL: eglGetConfigAttrib failed \n");

		if (gbm_format != GBM_FORMAT_ARGB8888)
			continue;

		EGLConfig config = configs[i];
		free(configs);
		return config;
	}
	exit(EXIT_NO_RESOURCE);
}

static void egl_init(const uint32_t size_x, const uint32_t size_y)
{
	int fd = open(devicenode, O_RDWR);
	if (fd < 0)
		pr_inf("ERROR: couldn't open %s, skipping\n", devicenode);

	gbm = gbm_create_device(fd);
	if (gbm == NULL)
		pr_inf("ERROR: couldn't create gbm device \n");

	if ((display =
	     eglGetPlatformDisplay(EGL_PLATFORM_GBM_KHR, gbm,
				   NULL)) == EGL_NO_DISPLAY)
		pr_inf("EGL: eglGetPlatformDisplay failed with vendor \n");

	EGLint majorVersion;
	EGLint minorVersion;
	if (eglInitialize(display, &majorVersion, &minorVersion) == EGL_FALSE)
		pr_inf("EGL: Failed to initialize EGL \n");

	if (eglBindAPI(EGL_OPENGL_ES_API) == EGL_FALSE)
		pr_inf("EGL: Failed to bind OpenGL ES \n");

	EGLConfig config = get_config();

	gs = gbm_surface_create(gbm, size_x, size_y, GBM_BO_FORMAT_ARGB8888,
				GBM_BO_USE_LINEAR | GBM_BO_USE_SCANOUT |
				GBM_BO_USE_RENDERING);
	if (!gs)
		pr_inf("ERROR: Could not create gbm surface \n");

	if ((surface =
	     eglCreatePlatformWindowSurface(display, config, gs,
					    NULL)) == EGL_NO_SURFACE)
		pr_inf("EGL: Failed to allocate surface \n");

	EGLContext context;
	const EGLint contextAttribs[] = {
		EGL_CONTEXT_CLIENT_VERSION, 2,
		EGL_NONE
	};
	if ((context =
	     eglCreateContext(display, config, EGL_NO_CONTEXT,
			      contextAttribs)) == EGL_NO_CONTEXT)
		pr_inf("EGL: Failed to create context \n");

	if (eglMakeCurrent(display, surface, surface, context) == EGL_FALSE)
		pr_inf("EGL: Failed to make context current \n");
}

static int stress_gpu(const stress_args_t *args)
{
	int frag_n = 0;
	uint32_t size_x = 256;
	uint32_t size_y = 256;
	GLsizei texsize = 4096;
	GLsizei uploads = 1;

	(void)stress_get_setting("gpu-frag", &frag_n);
	(void)stress_get_setting("gpu-xsize", &size_x);
	(void)stress_get_setting("gpu-ysize", &size_y);
	(void)stress_get_setting("gpu-tex-size", &texsize);
	(void)stress_get_setting("gpu-upload", &uploads);

	egl_init(size_x, size_y);
	gles2_init(size_x, size_y, frag_n, texsize);

	stress_set_proc_state(args->name, STRESS_STATE_RUN);

	do {
		stress_gpu_run(texsize, uploads);
		if (glGetError() != GL_NO_ERROR)
			return EXIT_NO_RESOURCE;
		inc_counter(args);
	} while (keep_stressing(args));

	if (teximage)
		free(teximage);

	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);

	return EXIT_SUCCESS;
}

stressor_info_t stress_gpu_info = {
	.stressor = stress_gpu,
	.class = CLASS_GPU,
	.opt_set_funcs = opt_set_funcs,
	.help = help
};
#else
stressor_info_t stress_gpu_info = {
	.stressor = stress_not_implemented,
	.class = CLASS_GPU,
	.opt_set_funcs = opt_set_funcs,
	.help = help
};
#endif
