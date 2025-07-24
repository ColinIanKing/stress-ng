/*
 * Copyright (C) 2022-2025, Red Hat Inc, Dorinda Bassey <dbassey@redhat.com>
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
#include "stress-ng.h"
#include "core-out-of-memory.h"
#include "core-pthread.h"

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
	{ NULL,	"gpu N",		"start N GPU worker" },
	{ NULL,	"gpu-devnode name",	"specify CPU device node name" },
	{ NULL,	"gpu-frag N",		"specify shader core usage per pixel" },
	{ NULL,	"gpu-ops N",		"stop after N gpu render bogo operations" },
	{ NULL,	"gpu-tex-size N",	"specify upload texture NxN" },
	{ NULL,	"gpu-upload N",		"specify upload texture N times per frame" },
	{ NULL,	"gpu-xsize X",		"specify framebuffer size x" },
	{ NULL,	"gpu-ysize Y",		"specify framebuffer size y" },
	{ NULL,	NULL,			NULL }
};

static const stress_opt_t opts[] = {
	{ OPT_gpu_devnode, "gpu-devnode",  TYPE_ID_STR,    0, 0,        NULL },
	{ OPT_gpu_frag,    "gpu-frag",     TYPE_ID_INT32,  1, INT_MAX,  NULL },
	{ OPT_gpu_upload,  "gpu-upload",   TYPE_ID_INT32,  1, INT_MAX,  NULL },
	{ OPT_gpu_size,    "gpu-tex-size", TYPE_ID_INT32,  1, INT_MAX,  NULL },
	{ OPT_gpu_xsize,   "gpu-xsize",    TYPE_ID_UINT32, 1, UINT_MAX, NULL },
	{ OPT_gpu_ysize,   "gpu-ysize",    TYPE_ID_UINT32, 1, UINT_MAX, NULL },
	END_OPT,
};

#if defined(HAVE_LIB_EGL) &&		\
	defined(HAVE_EGL_H) &&		\
	defined(HAVE_EGL_EXT_H) &&	\
	defined(HAVE_LIB_GLES2) &&	\
	defined(HAVE_GLES2_H) &&	\
	defined(HAVE_LIB_GBM) &&	\
	defined(HAVE_GBM_H)

#if defined(HAVE_LIB_PTHREAD)
static volatile double gpu_freq_sum;
static volatile uint64_t gpu_freq_count;
#endif

static GLuint program;
static EGLDisplay display;
static EGLSurface surface;
static struct gbm_device *gbm;
static struct gbm_surface *gs;

static const char default_gpu_devnode[] = "/dev/dri/renderD128";
static int gpu_card = 0;
static GLubyte *teximage = NULL;

#if defined(HAVE_LIB_PTHREAD)
/*
 *  stress_get_gpu_freq_mhz()
 *	get GPU frequency in MHz, set to 0.0 if not readable
 */
static void stress_get_gpu_freq_mhz(double *gpu_freq)
{
	if (UNLIKELY(!gpu_freq))
		return;
#if defined(__linux__)
	{
		char buf[64];
		char filename[128];
		snprintf(filename, sizeof(filename), "/sys/class/drm/card%d/gt_cur_freq_mhz", gpu_card);

		if (stress_system_read(filename, buf, sizeof(buf)) > 0) {
			if (sscanf(buf, "%lf", gpu_freq) == 1)
				return;
		}
	}
#endif
	*gpu_freq = 0.0;
}
#endif

static void stress_gpu_trim_newline(char *str)
{
	char *ptr = strrchr(str, '\n');

	if (ptr)
		*ptr = '\0';
}

static const char vert_shader[] =
    "attribute vec4 pos;\n"
    "attribute vec4 color;\n"
    "varying vec4 v_color;\n"
    "\n"
    "void main()\n"
    "{\n"
    "    v_color = color;\n"
    "    gl_Position = pos;\n"
    "}\n";

static const char frag_shader[] =
    "precision mediump float;\n"
    "varying vec4 v_color;\n"
    "uniform int frag_n;\n"
    "\n"
    "void main()\n"
    "{\n"
    "    int i;\n"
    "    vec4 a = v_color;\n"
    "    for (i = 0; i < frag_n; i++) {\n"
    "        float f = float(i);\n"
    "        a = a / clamp(sin(f) * exp(f), 0.1, 0.9);\n"
    "    }\n"
    "    a = clamp(a, -1.0, 1.0);\n"
    "    gl_FragColor = v_color + 0.000001 * a;\n"
    "}\n";

static GLuint compile_shader(
	stress_args_t *args,
	const char *text,
	const int size,
	const GLenum type)
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
			char *infoLog;

			infoLog = (char *)malloc((size_t)infoLen);
			if (!infoLog) {
				pr_inf_skip("%s: failed to allocate infoLog%s, skipping stressor\n",
					args->name, stress_get_memfree_str());
				glDeleteShader(shader);
				return 0;
			}
			glGetShaderInfoLog(shader, infoLen, NULL, infoLog);
			stress_gpu_trim_newline(infoLog);
			pr_inf("%s: failed to compile shader: %s\n", args->name, infoLog);
			free(infoLog);
		}
		glDeleteShader(shader);
		return 0;
	}
	return shader;
}

static int load_shaders(stress_args_t *args)
{
	GLint linked;
	GLuint vertexShader;
	GLuint fragmentShader;
	const char *name = args->name;

	vertexShader = compile_shader(args, vert_shader, sizeof(vert_shader),
			    GL_VERTEX_SHADER);
	if (vertexShader == 0) {
		pr_inf_skip("%s: failed to compile vertex shader, skipping stressor\n", name);
		return EXIT_NO_RESOURCE;
	}

	fragmentShader = compile_shader(args, frag_shader, sizeof(frag_shader),
			    GL_FRAGMENT_SHADER);
	if (fragmentShader == 0) {
		pr_inf_skip("%s: failed to compile fragment shader, skipping stressor\n", name);
		return EXIT_NO_RESOURCE;
	}

	program = glCreateProgram();
	if (program == 0) {
		pr_inf("%s: failed to create the shader program\n", name);
		return EXIT_NO_RESOURCE;
	}

	glAttachShader(program, vertexShader);
	glAttachShader(program, fragmentShader);
	glLinkProgram(program);
	glGetProgramiv(program, GL_LINK_STATUS, &linked);
	if (!linked) {
		GLint infoLen = 0;

		glGetProgramiv(program, GL_INFO_LOG_LENGTH, &infoLen);
		if (infoLen > 1) {
			char *infoLog;

			infoLog = (char *)malloc((size_t)infoLen);
			if (!infoLog) {
				pr_inf_skip("%s: failed to allocate infoLog%s, skipping stressor\n",
					name, stress_get_memfree_str());
				glDeleteProgram(program);
				return EXIT_NO_RESOURCE;
			}
			glGetProgramInfoLog(program, infoLen, NULL, infoLog);
			stress_gpu_trim_newline(infoLog);
			pr_fail("%s: failed to link shader program: %s\n", name, infoLog);
			free(infoLog);
		}
		glDeleteProgram(program);
		return EXIT_FAILURE;
	}

	glUseProgram(program);

	return EXIT_SUCCESS;
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

static int gles2_init(
	stress_args_t *args,
	const uint32_t width,
	const uint32_t height,
	const int frag_n,
	const GLsizei texsize)
{
	int ret;
	GLint ufrag_n;
	GLuint apos, acolor;

	if (stress_instance_zero(args)) {
		pr_inf("%s: GL_VENDOR: %s\n", args->name, (const char *)glGetString(GL_VENDOR));
		pr_inf("%s: GL_VERSION: %s\n", args->name, (const char *)glGetString(GL_VERSION));
		pr_inf("%s: GL_RENDERER: %s\n", args->name, (const char *)glGetString(GL_RENDERER));
	}

	ret = load_shaders(args);
	if (ret != EXIT_SUCCESS)
		return ret;

	glClearColor(0, 0, 0, 0);
	glViewport(0, 0, (GLsizei)width, (GLsizei)height);

	ufrag_n = glGetUniformLocation(program, "frag_n");
	glUniform1i(ufrag_n, frag_n);
	if (glGetError() != GL_NO_ERROR) {
		pr_fail("%s: failed to get the storage location of %d\n",
		       args->name, frag_n);
		return EXIT_FAILURE;
	}

	apos = (GLuint)glGetAttribLocation(program, "pos");
	glEnableVertexAttribArray(apos);
	glVertexAttribPointer(apos, 4, GL_FLOAT, 0, 0, vertex);

	acolor = (GLuint)glGetAttribLocation(program, "color");
	glEnableVertexAttribArray(acolor);
	glVertexAttribPointer(acolor, 4, GL_FLOAT, 0, 0, color);

	if (texsize > 0) {
		GLint maxsize, bytesPerImage;
		GLuint texobj = 0;

		glGetIntegerv(GL_MAX_TEXTURE_SIZE, &maxsize);
		if (texsize > maxsize) {
			pr_inf("%s: image size %u exceeds maximum texture size %u\n",
				args->name, (unsigned int)texsize, (unsigned int)maxsize);
			return EXIT_FAILURE;
		}

		texobj = 0;
		glGenTextures(1, &texobj);
		glBindTexture(GL_TEXTURE_2D, texobj);

		bytesPerImage = texsize * texsize * 4;
		teximage = (GLubyte *)malloc((size_t)bytesPerImage);
		if (!teximage) {
			pr_inf_skip("%s: failed to allocate teximage%s, skipping stressor\n",
				args->name, stress_get_memfree_str());
			return EXIT_NO_RESOURCE;
		}
	}
	return EXIT_SUCCESS;
}

static void stress_gpu_run(const GLsizei texsize, const GLsizei uploads)
{
	if (texsize > 0) {
		int i;
		for (i = 0; LIKELY(stress_continue_flag() && (i < uploads)); i++) {
			glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, texsize,
				     texsize, 0, GL_RGBA, GL_UNSIGNED_BYTE,
				     teximage);
		}
	}
	glClear(GL_COLOR_BUFFER_BIT);
	glDrawArrays(GL_TRIANGLES, 0, 6);
	glFinish();
}

static int get_config(stress_args_t *args, EGLConfig *config)
{
	static const EGLint egl_config_attribs[] = {
		EGL_BUFFER_SIZE, 32,
		EGL_DEPTH_SIZE, EGL_DONT_CARE,
		EGL_STENCIL_SIZE, EGL_DONT_CARE,
		EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
		EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
		EGL_NONE,
	};

	int i;
	EGLint num_configs;
	EGLConfig *configs;

	if (eglGetConfigs(display, NULL, 0, &num_configs) == EGL_FALSE) {
		pr_inf_skip("%s: EGL: no EGL configs found, skipping stressor\n", args->name);
		return EXIT_NO_RESOURCE;
	}

	/* Use calloc to avoid multiplication overflow */
	configs = (EGLConfig *)calloc((size_t)num_configs, sizeof(EGLConfig));
	if (!configs) {
		pr_inf_skip("%s: EGL: EGL allocation failed%s, skipping stressor\n",
			args->name, stress_get_memfree_str());
		return EXIT_NO_RESOURCE;
	}
	if ((eglChooseConfig(display, egl_config_attribs,
			     configs, num_configs,
			     &num_configs) == EGL_FALSE) || (num_configs == 0)) {
		pr_inf_skip("%s: EGL: can't choose EGL config, skipping stressor\n", args->name);
		return EXIT_NO_RESOURCE;
	}

	for (i = 0; i < num_configs; ++i) {
		EGLint gbm_format;

		if (eglGetConfigAttrib(display, configs[i],
				       EGL_NATIVE_VISUAL_ID,
				       &gbm_format) == EGL_FALSE) {
			pr_inf_skip("%s: EGL: eglGetConfigAttrib failed, skipping stressor\n", args->name);
			return EXIT_NO_RESOURCE;
		}

		if (gbm_format != GBM_FORMAT_ARGB8888)
			continue;

		*config = configs[i];
		free(configs);

		return EXIT_SUCCESS;
	}

	pr_inf_skip("%s: EGL: cannot get configuration, skipping stressor\n", args->name);
	return EXIT_NO_RESOURCE;
}

static int egl_init(
	stress_args_t *args,
	const char *gpu_devnode,
	const uint32_t size_x,
	const uint32_t size_y)
{
	int ret, fd;
	EGLConfig config;
	EGLContext context;
	EGLint majorVersion;
	EGLint minorVersion;

	static const EGLint contextAttribs[] = {
		EGL_CONTEXT_CLIENT_VERSION, 2,
		EGL_NONE
	};

	fd = open(gpu_devnode, O_RDWR);
	if (fd < 0) {
		pr_inf_skip("%s: couldn't open device '%s', errno=%d (%s), skipping stressor\n",
			args->name, gpu_devnode, errno, strerror(errno));
		return EXIT_NO_RESOURCE;
	}

	gbm = gbm_create_device(fd);
	if (!gbm) {
		pr_inf_skip("%s: couldn't create gbm device, skipping stressor\n", args->name);
		return EXIT_NO_RESOURCE;
	}

	display = eglGetPlatformDisplay(EGL_PLATFORM_GBM_KHR, gbm, NULL);
	if (display == EGL_NO_DISPLAY) {
		pr_inf_skip("%s: EGL: eglGetPlatformDisplay failed with vendor, skipping stressor\n", args->name);
		return EXIT_NO_RESOURCE;
	}

	if (eglInitialize(display, &majorVersion, &minorVersion) == EGL_FALSE) {
		pr_inf_skip("%s: EGL: failed to initialize EGL, skipping stressor\n", args->name);
		return EXIT_NO_RESOURCE;
	}

	if (eglBindAPI(EGL_OPENGL_ES_API) == EGL_FALSE) {
		pr_inf("%s: EGL: Failed to bind OpenGL ES\n", args->name);
		return EXIT_NO_RESOURCE;
	}

	ret = get_config(args, &config);
	if (ret != EXIT_SUCCESS)
		return ret;

	gs = gbm_surface_create(gbm, size_x, size_y, GBM_BO_FORMAT_ARGB8888,
				GBM_BO_USE_LINEAR | GBM_BO_USE_SCANOUT |
				GBM_BO_USE_RENDERING);
	if (!gs) {
		pr_inf_skip("%s: could not create gbm surface, skipping stressor\n", args->name);
		return EXIT_NO_RESOURCE;
	}

	surface = eglCreatePlatformWindowSurface(display, config, gs, NULL);
	if (surface == EGL_NO_SURFACE) {
		pr_inf("%s: EGL: Failed to allocate surface%s\n",
			args->name, stress_get_memfree_str());
		return EXIT_NO_RESOURCE;
	}

	context = eglCreateContext(display, config, EGL_NO_CONTEXT, contextAttribs);
	if (context == EGL_NO_CONTEXT) {
		pr_inf("%s: EGL: Failed to create context\n", args->name);
		return EXIT_NO_RESOURCE;
	}

	if (eglMakeCurrent(display, surface, surface, context) == EGL_FALSE) {
		pr_inf("%s: EGL: Failed to make context current\n", args->name);
		return EXIT_NO_RESOURCE;
	}
	return EXIT_SUCCESS;
}

static int stress_gpu_supported(const char *name)
{
	const char *gpu_devnode = default_gpu_devnode;
	int fd;

	(void)stress_get_setting("gpu-devnode", &gpu_devnode);
	fd = open(gpu_devnode, O_RDWR);
	if (fd < 0) {
		pr_inf_skip("%s: cannot open GPU device '%s', errno=%d (%s), skipping stressor\n",
			name, gpu_devnode, errno, strerror(errno));
		return -1;
	}
	(void)close(fd);
	return 0;
}

#if defined(HAVE_LIB_PTHREAD)
/*
 *  stress_gpu_pthread()
 *	sample gpu frequency every 1/10th second, scaled by
 *	number of instances, so sample rate is always ~1/10th second
 *	across all instances.
 */
static void *stress_gpu_pthread(void *arg)
{
	stress_args_t *args = (stress_args_t *)arg;
	uint64_t sleep_usecs = 100000UL * (uint64_t)args->instances;
	uint64_t start_sleep_usecs = 100000UL * (uint64_t)args->instance;

	(void)shim_usleep(start_sleep_usecs);
	while (stress_continue(args)) {
		double freq_mhz;

		/* Do wait first, allow GPU to crank up load */
		stress_get_gpu_freq_mhz(&freq_mhz);
		if (freq_mhz > 0.0) {
			gpu_freq_sum += freq_mhz;
			gpu_freq_count++;
		}
		(void)shim_usleep(sleep_usecs);
	}
	return NULL;
}
#endif

static int stress_gpu_card(const char *gpu_devnode)
{
	int renderer;

	if (sscanf(gpu_devnode, "/dev/dri/renderD%d", &renderer) != 1)
		return -1;
	renderer -= 128;
	if (renderer < 0)
		return -1;

	return renderer;
}

static int stress_gpu_child(stress_args_t *args, void *context)
{
	int frag_n = 0;
	int ret = EXIT_SUCCESS, fd;
	uint32_t size_x = 256;
	uint32_t size_y = 256;
	GLsizei texsize = 4096;
	GLsizei uploads = 1;
	const char *gpu_devnode = default_gpu_devnode;
	sigset_t set;
#if defined(HAVE_LIB_PTHREAD)
	pthread_t pthread;
	int pret;
	double rate;

	gpu_freq_sum = 0.0;
	gpu_freq_count = 0;
#endif

	/*
	 *  Block SIGALRM, instead use sigpending
	 *  in pthread or this process to check if
	 *  SIGALRM has been sent.
	 */
	(void)sigemptyset(&set);
	(void)sigaddset(&set, SIGALRM);
	(void)sigprocmask(SIG_BLOCK, &set, NULL);

	/* save and close stderr */
	fd = dup(STDERR_FILENO);
	if (fd >= 0)
		(void)close(STDERR_FILENO);

	(void)context;

	(void)setenv("MESA_SHADER_CACHE_DISABLE", "true", 1);
	(void)setenv("MESA_LOG_FILE", "/dev/null", 1);

	(void)stress_get_setting("gpu-devnode", &gpu_devnode);
	(void)stress_get_setting("gpu-frag", &frag_n);
	(void)stress_get_setting("gpu-xsize", &size_x);
	(void)stress_get_setting("gpu-ysize", &size_y);
	(void)stress_get_setting("gpu-tex-size", &texsize);
	(void)stress_get_setting("gpu-upload", &uploads);

	gpu_card = stress_gpu_card(gpu_devnode);

	ret = egl_init(args, gpu_devnode, size_x, size_y);
	if (ret != EXIT_SUCCESS)
		goto deinit;

	ret = gles2_init(args, size_x, size_y, frag_n, texsize);
	if (ret != EXIT_SUCCESS)
		goto deinit;

#if defined(HAVE_LIB_PTHREAD)
	pret = pthread_create(&pthread, NULL, stress_gpu_pthread, (void *)args);
#endif

	stress_set_proc_state(args->name, STRESS_STATE_SYNC_WAIT);
	stress_sync_start_wait(args);
	stress_set_proc_state(args->name, STRESS_STATE_RUN);

	do {
		stress_gpu_run(texsize, uploads);
		if (glGetError() != GL_NO_ERROR) {
			ret = EXIT_NO_RESOURCE;
			break;
		}
		stress_bogo_inc(args);
	} while (!stress_sigalrm_pending() && stress_continue(args));

#if defined(HAVE_LIB_PTHREAD)
	if (pret == 0) {
		(void)pthread_cancel(pthread);
		(void)pthread_join(pthread, NULL);

		rate = (gpu_freq_count > 0) ? gpu_freq_sum / (double)gpu_freq_count : 0.0;
		if (rate > 0.0)
			stress_metrics_set(args, 0, "MHz GPU frequency",
					rate, STRESS_METRIC_HARMONIC_MEAN);
	}
#endif

deinit:
	if (teximage)
		free(teximage);

	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);

	if (fd >= 0) {
		/* and re-connect stderr */
		(void)dup2(fd, STDERR_FILENO);
		(void)close(fd);
	}

	return ret;
}

static int stress_gpu(stress_args_t *args)
{
	return stress_oomable_child(args, NULL, stress_gpu_child, STRESS_OOMABLE_NORMAL);
}

const stressor_info_t stress_gpu_info = {
	.stressor = stress_gpu,
	.classifier = CLASS_GPU,
	.opts = opts,
	.supported = stress_gpu_supported,
	.help = help
};
#else
const stressor_info_t stress_gpu_info = {
	.stressor = stress_unimplemented,
	.classifier = CLASS_GPU,
	.opts = opts,
	.help = help,
	.unimplemented_reason = "built without EGL/egl.h, EGL/eglext.h, GLES2/gl2.h or gbm.h"
};
#endif
