// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2022-2023 Colin Ian King.
 *
 */
#include "stress-ng.h"
#include "core-killpid.h"
#include "core-lock.h"

#define TOUCH_PROCS	(4)

#define TOUCH_RANDOM	(0)
#define TOUCH_OPEN	(1)
#define TOUCH_CREAT	(2)

typedef struct {
	const char *opt;
	const int open_flag;
} touch_opts_t;

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

#if defined(O_TRUNC)
#define TOUCH_OPT_TRUNC		O_TRUNC
#else
#define TOUCH_OPT_TRUNC		(0)
#endif

static void *touch_lock;

#define TOUCH_OPT_ALL	\
	(TOUCH_OPT_DIRECT |	\
	 TOUCH_OPT_DSYNC |	\
	 TOUCH_OPT_EXCL |	\
	 TOUCH_OPT_NOATIME |	\
	 TOUCH_OPT_SYNC |	\
	 TOUCH_OPT_TRUNC )

static const stress_help_t help[] = {
	{ NULL,	"touch N",		"start N stressors that touch and remove files" },
	{ NULL, "touch-method M",	"specify method to touch tile file, open | create" },
	{ NULL,	"touch-ops N",		"stop after N touch bogo operations" },
	{ NULL, "touch-opts list",	"touch open options all, direct, dsync, excl, noatime, sync, trunc" },
	{ NULL,	NULL,		NULL }
};

static const touch_opts_t touch_opts[] = {
	{ "all",	TOUCH_OPT_ALL },
	{ "direct",	TOUCH_OPT_DIRECT },
	{ "dsync",	TOUCH_OPT_DSYNC },
	{ "excl",	TOUCH_OPT_EXCL },
	{ "noatime",	TOUCH_OPT_NOATIME },
	{ "sync",	TOUCH_OPT_SYNC },
	{ "trunc",	TOUCH_OPT_TRUNC },
};

static const touch_method_t touch_methods[] = {
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

	for (i = 0; i < SIZEOF_ARRAY(touch_methods); i++) {
		if (!strcmp(opts, touch_methods[i].method)) {
			stress_set_setting("touch-method", TYPE_ID_INT, &touch_methods[i].method_type);
			return 0;
		}
	}
	fprintf(stderr, "touch-method '%s' not known, methods are:", opts);
	for (i = 0; i < SIZEOF_ARRAY(touch_methods); i++)
			(void)fprintf(stderr, "%s %s",
				i == 0 ? "" : ",", touch_methods[i].method);
	(void)fprintf(stderr, "\n");
	return -1;
}

static const stress_opt_set_func_t opt_set_funcs[] = {
	{ OPT_touch_opts,	stress_set_touch_opts },
	{ OPT_touch_method,	stress_set_touch_method },
	{ 0,			NULL },
};

static void stress_touch_dir_clean(const stress_args_t *args)
{
	char tmp[PATH_MAX];
	DIR *dir;
	struct dirent *d;

	sync();
	stress_temp_dir(tmp, sizeof(tmp), args->name, args->pid, args->instance);
	dir = opendir(tmp);

	if (!dir)
		return;
	while ((d = readdir(dir)) != NULL) {
		char filename[PATH_MAX + sizeof(d->d_name) + 1];
		struct stat statbuf;

		(void)snprintf(filename, sizeof(filename), "%s/%s\n", tmp, d->d_name);
		if (stat(filename, &statbuf) < 0)
			continue;
		if ((statbuf.st_mode & S_IFMT) == S_IFREG)
			(void)shim_unlink(filename);
	}
	(void)closedir(dir);
}

static void stress_touch_loop(
	const stress_args_t *args,
	const int touch_method,
	const int open_flags)
{
	do {
		char filename[PATH_MAX];
		uint64_t counter;
		int fd, ret;

		ret = stress_lock_acquire(touch_lock);
		if (ret)
			break;
		counter = stress_bogo_get(args);
		stress_bogo_inc(args);
		ret = stress_lock_release(touch_lock);
		if (ret)
			break;
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
				pr_fail("%s: creat %s failed, errno=%d (%s)\n",
					args->name, filename, errno, strerror(errno));
				break;
			default:
				/* Silently ignore anything else */
				break;
			}
		} else {
			(void)close(fd);
		}

		(void)shim_unlink(filename);
	} while (stress_continue(args));
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
	pid_t pids[TOUCH_PROCS];
	size_t i;

	touch_lock = stress_lock_create();
	if (!touch_lock) {
		pr_inf_skip("%s: cannot create lock, skipping stressor\n", args->name);
		return EXIT_NO_RESOURCE;
	}

	(void)stress_get_setting("touch-opts", &open_flags);
	(void)stress_get_setting("touch-method", &touch_method);

	if ((args->instance == 0) &&
	    (touch_method == TOUCH_CREAT) &&
	    (open_flags != 0))
		pr_inf("%s: note: touch-opts are not used for creat touch method\n", args->name);

	ret = stress_temp_dir_mk_args(args);
	if (ret < 0)
		return stress_exit_status(-ret);

	stress_set_proc_state(args->name, STRESS_STATE_RUN);

	for (i = 0; i < TOUCH_PROCS; i++) {
		pids[i] = fork();

		if (pids[i] == 0) {
			stress_touch_loop(args, touch_method, open_flags);
			_exit(0);
		}
	}
	stress_touch_loop(args, touch_method, open_flags);

	stress_continue_set_flag(false);

	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);

	stress_kill_and_wait_many(args, pids, TOUCH_PROCS, SIGALRM, true);
	stress_touch_dir_clean(args);
	(void)stress_temp_dir_rm_args(args);
	(void)stress_lock_destroy(touch_lock);

	return EXIT_SUCCESS;
}

stressor_info_t stress_touch_info = {
	.stressor = stress_touch,
	.class = CLASS_FILESYSTEM | CLASS_OS,
	.opt_set_funcs = opt_set_funcs,
	.verify = VERIFY_ALWAYS,
	.help = help
};
