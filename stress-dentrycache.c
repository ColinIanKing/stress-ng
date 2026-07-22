/*
 * Copyright (C) 2026      Colin Ian King.
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
#include "core-builtin.h"
#include "core-filesystem.h"

#include <sys/stat.h>

#if defined(HAVE_SYS_XATTR_H)
#include <sys/xattr.h>
#undef HAVE_ATTR_XATTR_H
#elif defined(HAVE_ATTR_XATTR_H)
#include <attr/xattr.h>
#endif

#if defined(HAVE_UTIME_H)
#include <utime.h>
#endif

#define YIELD_MASK	(0xffff)

typedef void (*direntrycache_func_t)(const char *filename);
typedef struct {
	const char *name;
	direntrycache_func_t dentrycache_func;
} dentrycache_method_t;

static const stress_help_t help[] = {
	{ NULL, "dentrycache N",     "start N dentry thrashing stressors" },
	{ NULL,	"dentrycache-ops N", "stop after N dentry bogo operations" },
	{ NULL,	NULL,                 NULL }
};

static void stress_dentrycache_access(const char *filename)
{
	VOID_RET(int, access(filename, R_OK));
}

#if (defined(HAVE_SYS_XATTR_H) ||	\
     defined(HAVE_ATTR_XATTR_H)) &&	\
    defined(HAVE_GETXATTR)
static void stress_dentrycache_getxattr(const char *filename)
{
	char attr[32];

	VOID_RET(int, getxattr(filename, "user.var", attr, sizeof(attr)));
}
#endif

static void stress_dentrycache_lstat(const char *filename)
{
	struct stat statbuf;

	VOID_RET(int, lstat(filename, &statbuf));
}

static void stress_dentrycache_open(const char *filename)
{
	int fd;

	fd = open(filename, O_RDONLY);
	if (UNLIKELY(fd != -1))
		(void)close(fd);
}

static void stress_dentrycache_readlink(const char *filename)
{
	char path[PATH_MAX];

	VOID_RET(ssize_t, readlink(filename, path, sizeof(path)));
}

static void stress_dentrycache_stat(const char *filename)
{
	struct stat statbuf;

	VOID_RET(int, stat(filename, &statbuf));
}

#if defined(HAVE_UTIME_H) &&    \
    defined(HAVE_UTIME) &&      \
    defined(HAVE_UTIMBUF)
static void stress_dentrycache_utime(const char *filename)
{
	static struct utimbuf times = {
		0,
		0,
	};

	VOID_RET(int, utime(filename, &times));
}
#endif

/*
 *  Note, destructive system calls such as unlink
 *  and rmdir should not be used to stop file
 *  removal hackery
 */
static dentrycache_method_t dentrycache_methods[] = {
	{ "all",	NULL },
	{ "access",	stress_dentrycache_access },
#if (defined(HAVE_SYS_XATTR_H) ||	\
     defined(HAVE_ATTR_XATTR_H)) &&	\
     defined(HAVE_GETXATTR)
	{ "getxattr",	stress_dentrycache_getxattr },
#endif
	{ "lstat",	stress_dentrycache_lstat },
	{ "open",	stress_dentrycache_open },
	{ "readlink",	stress_dentrycache_readlink },
	{ "stat",	stress_dentrycache_stat },
#if defined(HAVE_UTIME_H) &&    \
    defined(HAVE_UTIME) &&      \
    defined(HAVE_UTIMBUF)
	{ "utime",	stress_dentrycache_utime },
#endif
};

static const char *stress_dentrycache_method(const size_t i)
{
	return (i < SIZEOF_ARRAY(dentrycache_methods)) ? dentrycache_methods[i].name : NULL;
}

static const stress_opt_t opts[] = {
	{ OPT_dentrycache_method, "dentrycache-method", TYPE_ID_SIZE_T_METHOD, 0, 0, stress_dentrycache_method },
	END_OPT,
};

/*
 *  stress_dentrycache_filename()
 *	try to generate a unique path as fast as possible
 *	2^64 filenames are possible which is plenty for now
 */
static inline ALWAYS_INLINE OPTIMIZE3 void stress_dentrycache_filename(char *const filename, register uint64_t val)
{
	register int i;
	register char *ptr = filename;

	for (i = 0; val && (i < 16); i++) {
		*ptr++ = 'a' + (val & 0xf);
		val >>= 4;
	}
	for (; i < 16; i++)
		*ptr++ = 'a';
	*ptr = '\0';
}

/*
 *  stress_dentrycache
 *	stress dentries.  file names are based
 *	on a gray-coded value multiplied by two.
 *	Even numbered files exist, odd don't exist.
 */
static int OPTIMIZE3 stress_dentrycache(stress_args_t *args)
{
	int ret;
	int rc = EXIT_SUCCESS;
	char dir_path[PATH_MAX];
	char *filename;
	int64_t nr_dentries;
	int64_t nr_dentry1;
	int64_t nr_dentry2;
	size_t n;
	size_t dentrycache_method = 0;	/* default 'all' */
	register uint64_t count = 0;

	double t;
	double duration;
	double rate;
	char pid_str[32];

	ret = stress_fs_temp_dir_make_args(args);
	if (ret < 0)
		return stress_exit_status(-ret);

	(void)stress_fs_temp_dir(dir_path, sizeof(dir_path), args->name,
		args->pid, args->instance);

	(void)snprintf(pid_str, sizeof(pid_str), "/%" PRIdMAX "=", (intmax_t)getpid());
	n = strlcat(dir_path, pid_str, sizeof(dir_path));
	filename = dir_path + n ;

	stress_proc_state_set(args->name, STRESS_STATE_SYNC_WAIT);
	stress_sync_start_wait(args);
	stress_proc_state_set(args->name, STRESS_STATE_RUN);


	(void)stress_setting_get("dentrycache-method", &dentrycache_method);

	if (stress_instance_zero(args)) {
		pr_inf("%s: using method '%s' for negative dentry creation\n",
			args->name, dentrycache_methods[dentrycache_method].name);
	}

	stress_fs_dentry_state_get(&nr_dentry1);
	t = stress_time_now();
	if (dentrycache_method == 0) {
		do {
			register size_t i;

			for (i = 1; (i < SIZEOF_ARRAY(dentrycache_methods)) && stress_continue(args); i++) {
				register direntrycache_func_t dentrycache_func =
					dentrycache_methods[i].dentrycache_func;

				stress_dentrycache_filename(filename, count);
				count++;
				dentrycache_func(dir_path);
				if ((count & YIELD_MASK) == YIELD_MASK)
					shim_sched_yield();
				stress_bogo_inc(args);
			}
		} while (stress_continue(args));
	} else {
		register direntrycache_func_t dentrycache_func =
			dentrycache_methods[dentrycache_method].dentrycache_func;

		do {
			stress_dentrycache_filename(filename, count);
			count++;
			dentrycache_func(dir_path);
			if ((count & YIELD_MASK) == YIELD_MASK)
				shim_sched_yield();
			stress_bogo_inc(args);
		} while (stress_continue(args));
	}
	duration = stress_time_now() - t;
	stress_fs_dentry_state_get(&nr_dentry2);
	nr_dentries = (nr_dentry2 > nr_dentry1) ? nr_dentry2 - nr_dentry1 : 0LL;

	stress_proc_state_set(args->name, STRESS_STATE_DEINIT);

	rate = (count > 0.0) ? duration / (double)count : 0.0;
	stress_metrics_set(args, "nanosecs per negative dentry operationn",
		rate * STRESS_DBL_NANOSECOND, STRESS_METRIC_HARMONIC_MEAN);
	if (nr_dentries > 0)
		stress_metrics_set(args, "directory entries allocted", (double)nr_dentries, STRESS_METRIC_MAXIMUM);
	stress_metrics_set(args, "bogus (negative) directory entries accessed", (double)count, STRESS_METRIC_TOTAL);

	/* force unlink of all files */
	(void)stress_fs_temp_dir_rm_args(args);

	return rc;
}

static const stress_exercises_t exercises[] = {
	STRESS_EX_FEATURE("directory"),

	STRESS_EX_SYSCALL("access"),
#if (defined(HAVE_SYS_XATTR_H) ||	\
     defined(HAVE_ATTR_XATTR_H)) &&	\
     defined(HAVE_GETXATTR)
	STRESS_EX_SYSCALL("getxattr"),
#endif
	STRESS_EX_SYSCALL("lstat"),
	STRESS_EX_SYSCALL("open"),
	STRESS_EX_SYSCALL("readlink"),
	STRESS_EX_SYSCALL("stat"),
#if defined(HAVE_UTIME_H) &&    \
    defined(HAVE_UTIME) &&      \
    defined(HAVE_UTIMBUF)
	STRESS_EX_SYSCALL("utime"),
#endif
	STRESS_EX_END,
};

const stressor_info_t stress_dentrycache_info = {
	.stressor = stress_dentrycache,
	.classifier = CLASS_FILESYSTEM | CLASS_OS,
	.verify = VERIFY_NONE,
	.help = help,
	.opts = opts,
	.exercises = exercises,
};
