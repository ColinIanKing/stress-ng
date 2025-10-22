/*
 * Copyright (C) 2022-2025 Colin Ian King.
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
 *  stress_touch_opts
 *	parse --touch-opts option(s) list
 */
static void stress_touch_opts(const char *opt_name, const char *opt_arg, stress_type_id_t *type_id, void *value)
{
	char *str, *ptr;
	const char *token;
	int open_flags = 0;

	str = stress_const_optdup(opt_arg);
	if (!str) {
		(void)fprintf(stderr, "%s option: cannot dup string '%s'\n",
			opt_name, opt_arg);
		longjmp(g_error_env, 1);
		stress_no_return();
	}

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
			(void)fprintf(stderr, "%s option '%s' not known, options are:", opt_name, token);
			for (i = 0; i < SIZEOF_ARRAY(touch_opts); i++)
				(void)fprintf(stderr, " %s", touch_opts[i].opt);
			(void)fprintf(stderr, "\n");
			free(str);
			longjmp(g_error_env, 1);
			stress_no_return();
		}
	}
	*type_id = TYPE_ID_INT;
	*(int *)value = open_flags;
	free(str);
}

static const char *stress_touch_method(const size_t i)
{
	return (i < SIZEOF_ARRAY(touch_methods)) ? touch_methods[i].method : NULL;
}

static const stress_opt_t opts[] = {
	{ OPT_touch_opts,   "touch-opts",   TYPE_ID_CALLBACK, 0, 0, stress_touch_opts },
	{ OPT_touch_method, "touch-method", TYPE_ID_SIZE_T_METHOD, 0, 0, stress_touch_method },
	END_OPT,
};

static void stress_touch_dir_clean(stress_args_t *args)
{
	char tmp[PATH_MAX];
	DIR *dir;
	const struct dirent *d;

	shim_sync();
	stress_temp_dir(tmp, sizeof(tmp), args->name,
		args->pid, args->instance);
	dir = opendir(tmp);

	if (!dir)
		return;
	while ((d = readdir(dir)) != NULL) {
		/*
		 * One file name (with a path) and one NUL character (PATH_MAX),
		 * one slash (1), another file name without a path (NAME_MAX).
		 * This can produce a result that exceeds the OS limit, but the
		 * buffer size will be sufficient to join the strings safely
		 * without upsetting the compiler.
		 */
		char filename[PATH_MAX + 1 + NAME_MAX];
		struct stat statbuf;

		(void)snprintf(filename, sizeof(filename), "%s/%s", tmp, d->d_name);
		if (shim_stat(filename, &statbuf) < 0)
			continue;
		if ((statbuf.st_mode & S_IFMT) == S_IFREG)
			(void)shim_unlink(filename);
	}
	(void)closedir(dir);
}

static void stress_touch_loop(
	stress_args_t *args,
	const int touch_method_type,
	const int open_flags)
{
	do {
		char filename[PATH_MAX];
		uint64_t counter;
		int fd, ret;

		ret = stress_lock_acquire(touch_lock);
		if (UNLIKELY(ret))
			break;
		counter = stress_bogo_get(args);
		stress_bogo_inc(args);
		ret = stress_lock_release(touch_lock);
		if (UNLIKELY(ret))
			break;
		(void)stress_temp_filename_args(args, filename,
			sizeof(filename), counter);

		switch (touch_method_type) {
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

		if (UNLIKELY(fd < 0)) {
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
static int stress_touch(stress_args_t *args)
{
	int ret;
	int open_flags = 0;
	size_t touch_method = 0; /* TOUCH_RANDOM */
	int touch_method_type;
	stress_pid_t *s_pids, *s_pids_head = NULL;
	size_t i;

	s_pids = stress_sync_s_pids_mmap(TOUCH_PROCS);
	if (s_pids == MAP_FAILED) {
		pr_inf_skip("%s: failed to mmap %d PIDs%s, skipping stressor\n",
			args->name, TOUCH_PROCS, stress_get_memfree_str());
		return EXIT_NO_RESOURCE;
	}

	touch_lock = stress_lock_create("counter");
	if (!touch_lock) {
		pr_inf_skip("%s: cannot create lock, skipping stressor\n", args->name);
		(void)stress_sync_s_pids_munmap(s_pids, TOUCH_PROCS);
		return EXIT_NO_RESOURCE;
	}

	(void)stress_get_setting("touch-opts", &open_flags);
	(void)stress_get_setting("touch-method", &touch_method);

	touch_method_type = touch_methods[touch_method].method_type;

	if (stress_instance_zero(args) &&
	    (touch_method_type == TOUCH_CREAT) &&
	    (open_flags != 0))
		pr_inf("%s: note: touch-opts are not used for creat touch method\n", args->name);

	ret = stress_temp_dir_mk_args(args);
	if (ret < 0) {
		(void)stress_lock_destroy(touch_lock);
		(void)stress_sync_s_pids_munmap(s_pids, TOUCH_PROCS);
		return stress_exit_status(-ret);
	}

	for (i = 0; i < TOUCH_PROCS; i++) {
		stress_sync_start_init(&s_pids[i]);

		s_pids[i].pid = fork();
		if (s_pids[i].pid == 0) {
			stress_set_proc_state(args->name, STRESS_STATE_SYNC_WAIT);
			s_pids[i].pid = getpid();
			stress_sync_start_wait_s_pid(&s_pids[i]);
			stress_set_proc_state(args->name, STRESS_STATE_RUN);

			stress_touch_loop(args, touch_method_type, open_flags);
			_exit(0);
		} else if (s_pids[i].pid > 0) {
			stress_sync_start_s_pid_list_add(&s_pids_head, &s_pids[i]);
		}
	}

	stress_set_proc_state(args->name, STRESS_STATE_SYNC_WAIT);
	stress_sync_start_wait(args);
	stress_sync_start_cont_list(s_pids_head);
	stress_set_proc_state(args->name, STRESS_STATE_RUN);

	stress_touch_loop(args, touch_method_type, open_flags);

	stress_continue_set_flag(false);

	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);

	stress_kill_and_wait_many(args, s_pids, TOUCH_PROCS, SIGALRM, true);
	stress_touch_dir_clean(args);
	(void)stress_temp_dir_rm_args(args);
	(void)stress_lock_destroy(touch_lock);
	(void)stress_sync_s_pids_munmap(s_pids, TOUCH_PROCS);

	return EXIT_SUCCESS;
}

const stressor_info_t stress_touch_info = {
	.stressor = stress_touch,
	.classifier = CLASS_FILESYSTEM | CLASS_OS,
	.opts = opts,
	.verify = VERIFY_ALWAYS,
	.help = help
};
