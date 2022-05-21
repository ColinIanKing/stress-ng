/*
 * Copyright (C) 2013-2021 Canonical, Ltd.
 * Copyright (C)      2022 Colin Ian King.
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

typedef struct {
	const char *opt;
	const int open_flag;
} touch_opts_t;

#define TOUCH_RANDOM	(0)
#define TOUCH_OPEN	(1)
#define TOUCH_CREAT	(2)

typedef struct {
	const char *method;
	const int method_type;
} touch_method_t;

#if defined(O_DIRECT)
#define TOUCH_OPT_DIRECT	O_DIRECT
#else
#define TOUCH_OPT_DIRECT	(0)
#endif

#if defined(O_DSYNC)
#define TOUCH_OPT_DSYNC		O_DSYNC
#else
#define TOUCH_OPT_DSYNC		(0)
#endif

#if defined(O_EXCL)
#define TOUCH_OPT_EXCL		O_EXCL
#else
#define TOUCH_OPT_EXCL		(0)
#endif

#if defined(O_NOATIME)
#define TOUCH_OPT_NOATIME	O_NOATIME
#else
#define TOUCH_OPT_NOATIME	(0)
#endif

#if defined(O_SYNC)
#define TOUCH_OPT_SYNC		O_SYNC
#else
#define TOUCH_OPT_SYNC		(0)
#endif

static const stress_help_t help[] = {
	{ NULL,	"touch N",	"start N stressors that touch and remove files" },
	{ NULL,	"touch-ops N",	"stop after N touch bogo operations" },
	{ NULL, "touch-opts",	"touch open options direct,dsync,excl,noatime,sync" },
	{ NULL, "touch-method",	"specify method to touch tile file, open | create" },
	{ NULL,	NULL,		NULL }
};

static const touch_opts_t touch_opts[] = {
	{ "direct",	TOUCH_OPT_DIRECT },
	{ "dsync",	TOUCH_OPT_DSYNC },
	{ "excl",	TOUCH_OPT_EXCL },
	{ "noatime",	TOUCH_OPT_NOATIME },
	{ "sync",	TOUCH_OPT_SYNC },
};

static const touch_method_t touch_method[] = {
	{ "random",	TOUCH_RANDOM },
	{ "open",	TOUCH_OPEN },
	{ "creat",	TOUCH_CREAT },
};

/*
 *  stress_set_touch_opts
 *	parse --touch-opts option(s) list
 */
static int stress_set_touch_opts(const char *opts)
{
	char *str, *ptr, *token;
	int open_flags = 0;

	str = stress_const_optdup(opts);
	if (!str)
		return -1;

	for (ptr = str; (token = strtok(ptr, ",")) != NULL; ptr = NULL) {
		size_t i;
		bool opt_ok = false;

		for (i = 0; i < SIZEOF_ARRAY(touch_opts); i++) {
			if (!strcmp(token, touch_opts[i].opt)) {
				open_flags |= touch_opts[i].open_flag;
				opt_ok = true;
			}
		}
		if (!opt_ok) {
			(void)fprintf(stderr, "touch-opts option '%s' not known, options are:", token);
			for (i = 0; i < SIZEOF_ARRAY(touch_opts); i++)
				(void)fprintf(stderr, "%s %s",
					i == 0 ? "" : ",", touch_opts[i].opt);
			(void)fprintf(stderr, "\n");
			free(str);
			return -1;
		}
	}

	stress_set_setting("touch-opts", TYPE_ID_INT, &open_flags);
	free(str);

	return 0;
}

/*
 *  stress_set_touch_method
 *	set method to open the file to touch
 */
static int stress_set_touch_method(const char *opts)
{
	size_t i;

	for (i = 0; i < SIZEOF_ARRAY(touch_method); i++) {
		if (!strcmp(opts, touch_method[i].method)) {
			stress_set_setting("touch-method", TYPE_ID_INT, &touch_method[i].method_type);
			return 0;
		}
	}
	fprintf(stderr, "touch-method '%s' not known, methods are:", opts);
	for (i = 0; i < SIZEOF_ARRAY(touch_method); i++)
			(void)fprintf(stderr, "%s %s",
				i == 0 ? "" : ",", touch_method[i].method);
	(void)fprintf(stderr, "\n");
	return -1;
}

/*
 *  stress_touch
 *	stress file creation and removal
 */
static int stress_touch(const stress_args_t *args)
{
	int ret;
	int open_flags = 0;
	int touch_method = TOUCH_RANDOM;

	stress_get_setting("touch-opts", &open_flags);
	stress_get_setting("touch-method", &touch_method);

	if ((args->instance == 0) &&
	    (touch_method == TOUCH_CREAT) &&
	    (open_flags != 0))
		pr_inf("%s: note: touch-opts are not used for creat touch method\n", args->name);

	ret = stress_temp_dir_mk_args(args);
	if (ret < 0)
		return exit_status(-ret);

	stress_set_proc_state(args->name, STRESS_STATE_RUN);

	do {
		char filename[PATH_MAX];
		uint64_t counter = get_counter(args);
		int fd;

		(void)stress_temp_filename_args(args, filename,
			sizeof(filename), counter);

		switch (touch_method) {
		default:
		case TOUCH_RANDOM:
			if (stress_mwc1())
				fd = creat(filename, S_IRUSR | S_IWUSR);
			else
				fd = open(filename,  O_CREAT | O_WRONLY | open_flags, S_IRUSR | S_IWUSR);
			break;
		case TOUCH_OPEN:
			fd = open(filename,  O_CREAT | O_WRONLY | open_flags, S_IRUSR | S_IWUSR);
			break;
		case TOUCH_CREAT:
			fd = creat(filename, S_IRUSR | S_IWUSR);
			break;
		}

		if (fd < 0) {
			switch (errno) {
#if defined(EEXIST)
			case EEXIST:
#endif
#if defined(EFAULT)
			case EFAULT:
#endif
#if defined(EFBIG)
			case EFBIG:
#endif
#if defined(EINVAL)
			case EINVAL:
#endif
#if defined(EISDIR)
			case EISDIR:
#endif
#if defined(EMFILE)
			case EMFILE:
#endif
#if defined(ENOENT)
			case ENOENT:
#endif
#if defined(ENOTDIR)
			case ENOTDIR:
#endif
#if defined(ENXIO)
			case ENXIO:
#endif
#if defined(EOPNOTSUPP)
			case EOPNOTSUPP:
#endif
#if defined(ETXTBSY)
			case ETXTBSY:
#endif
#if defined(EWOULDBLOCK)
			case EWOULDBLOCK:
#endif
#if defined(EBADF)
			case EBADF:
#endif
			case -1:
				/* Unexpected failures, fail on these */
				pr_fail("%s: creat failed, errno=%d (%s)\n",
					args->name, errno, strerror(errno));
				break;
			default:
				/* Silently ignore anything else */
				break;
			}
		} else {
			(void)close(fd);
		}

		(void)unlink(filename);

		inc_counter(args);
	} while (keep_stressing(args));

	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);

	(void)stress_temp_dir_rm_args(args);

	return ret;
}

static const stress_opt_set_func_t opt_set_funcs[] = {
	{ OPT_touch_opts,	stress_set_touch_opts },
	{ OPT_touch_method,	stress_set_touch_method },
	{ 0,			NULL },
};

stressor_info_t stress_touch_info = {
	.stressor = stress_touch,
	.class = CLASS_FILESYSTEM | CLASS_OS,
	.opt_set_funcs = opt_set_funcs,
	.verify = VERIFY_ALWAYS,
	.help = help
};
