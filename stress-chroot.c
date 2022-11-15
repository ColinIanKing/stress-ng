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
#include "core-capabilities.h"

static const stress_help_t help[] = {
	{ NULL,	"chroot N",	"start N workers thrashing chroot" },
	{ NULL,	"chroot-ops N",	"stop chroot workers after N bogo operations" },
	{ NULL,	NULL,		NULL }
};

#if defined(HAVE_CHROOT)

typedef struct {
	double duration;
	double count;
} chroot_metrics_t;

typedef int (*stress_chroot_test_func)(const stress_args_t *args,
				       chroot_metrics_t *metrics);

static char temppath[PATH_MAX];
static char longpath[PATH_MAX + 32];
static char badpath[PATH_MAX];
static char filename[PATH_MAX];

/*
 *  stress_chroot_supported()
 *      check if we can run this as root
 */
static int stress_chroot_supported(const char *name)
{
	if (!stress_check_capability(SHIM_CAP_SYS_ADMIN)) {
		pr_inf_skip("%s stressor will be skipped, "
			"need to be running with CAP_SYS_ADMIN "
			"rights for this stressor\n", name);
		return -1;
	}
	return 0;
}

/*
 *  do_chroot()
 *	helper to do a chroot followed by chdir
 */
static void do_chroot(
	const char *path,
	int *ret1, int *ret2,
	int *errno1, int *errno2,
	chroot_metrics_t *metrics)
{
	double t1, t2;

	t1 = stress_time_now();
	*ret1 = chroot(path);
	*errno1 = errno;
	t2 = stress_time_now();
	if (*ret1 == 0) {
		metrics->duration += t2 - t1;
		metrics->count += 1.0;
	}

	/*
	 * we must do chdir immediately after a chroot
	 * otherwise we have a security risk. Also
	 * CoverityScan will pick this up as an error.
	 */
	*ret2 = chdir("/");
	*errno2 = errno;
}

/*
 *  stress_chroot_test1()
 *	check if we can chroot to a valid directory
 */
static int stress_chroot_test1(
	const stress_args_t *args,
	chroot_metrics_t *metrics)
{
	char cwd[PATH_MAX];
	int ret1, ret2, errno1, errno2;

	do_chroot(temppath, &ret1, &ret2, &errno1, &errno2, metrics);

	/*
	 * We check for error, ENOENT can happen on termination
	 * so ignore this error
	 */
	if ((ret1 < 0) && (errno != ENOENT)) {
		pr_fail("%s: chroot(\"%s\"), errno=%d (%s)\n",
			args->name, temppath, errno1, strerror(errno1));
		return EXIT_FAILURE;
	}
	if (ret2 < 0) {
		pr_fail("%s: chdir(\"%s/\") failed, errno=%d (%s)\n",
			args->name, temppath, errno2, strerror(errno2));
		return EXIT_FAILURE;
	}
	if (!getcwd(cwd, sizeof(cwd))) {
		pr_fail("%s: getcwd failed, errno=%d (%s)\n",
			args->name, errno, strerror(errno));
		return EXIT_FAILURE;
	}
	if (strcmp(cwd, "/")) {
		pr_fail("%s: cwd in chroot is \"%s\" and not \"/\"\n", args->name, cwd);
		return EXIT_FAILURE;
	}
	return EXIT_SUCCESS;
}

/*
 *  stress_chroot_test2()
 *	check if path out of address space throws EFAULT error
 */
static int stress_chroot_test2(
	const stress_args_t *args,
	chroot_metrics_t *metrics)
{
#if defined(__linux__)
	int ret1, ret2, errno1, errno2;

	do_chroot((void *)1, &ret1, &ret2, &errno1, &errno2, metrics);

	if ((ret1 >= 0) || (errno1 != EFAULT)) {
		pr_fail("%s: chroot(\"(void *)1\"), expected EFAULT"
			", got instead errno=%d (%s)\n",
			args->name, errno1, strerror(errno1));
		return EXIT_FAILURE;
	}
#else
	(void)args;
	(void)metrics;
#endif
	return EXIT_SUCCESS;
}

/*
 *  stress_chroot_test3()
 *	see if long path is handled correctly
 */
static int stress_chroot_test3(
	const stress_args_t *args,
	chroot_metrics_t *metrics)
{
	int ret1, ret2, errno1, errno2;

	do_chroot(longpath, &ret1, &ret2, &errno1, &errno2, metrics);

#if defined(__HAIKU__)
	if ((ret1 >= 0) || (errno1 != EINVAL)) {
#else
	if ((ret1 >= 0) || (errno1 != ENAMETOOLONG)) {
#endif
		pr_fail("%s: chroot(\"<very long path>\"), expected "
			"ENAMETOOLONG, got instead errno=%d (%s)\n",
			args->name, errno1, strerror(errno1));
		return EXIT_FAILURE;
	}
	return EXIT_SUCCESS;
}

/*
 *  stress_chroot_test4()
 *	check if chroot to a path that does not exist returns ENOENT
 */
static int stress_chroot_test4(
	const stress_args_t *args,
	chroot_metrics_t *metrics)
{
	int ret1, ret2, errno1, errno2;

	do_chroot(badpath, &ret1, &ret2, &errno1, &errno2, metrics);

	if ((ret1 >= 0) || (errno1 != ENOENT)) {
		pr_fail("%s: chroot(\"%s\"), expected ENOENT"
			", got instead errno=%d (%s)\n",
			args->name, badpath, errno1, strerror(errno1));
		return EXIT_FAILURE;
	}
	return EXIT_SUCCESS;
}

/*
 *  stress_chroot_test5()
 *	check if chroot to a file returns ENOTDIR
 */
static int stress_chroot_test5(
	const stress_args_t *args,
	chroot_metrics_t *metrics)
{
	int ret1, ret2, errno1, errno2;

	do_chroot(filename, &ret1, &ret2, &errno1, &errno2, metrics);

	/*
	 * We check for error, ENOENT can happen on termination
	 * so ignore this error
	 */
	if ((ret1 >= 0) || ((errno1 != ENOTDIR) &&
			    (errno1 != ENOENT) &&
			    (errno1 != EPERM))) {
		pr_fail("%s: chroot(\"%s\"), expected ENOTDIR"
			", got instead errno=%d (%s)\n",
			args->name, filename, errno1, strerror(errno1));
		return EXIT_FAILURE;
	}
	return EXIT_SUCCESS;
}

/*
 *  stress_chroot_test6()
 *	check if chroot to a device path fails with ENOTDIR
 */
static int stress_chroot_test6(
	const stress_args_t *args,
	chroot_metrics_t *metrics)
{
	int ret1, ret2, errno1, errno2;
	static const char dev[] = "/dev/null";

	do_chroot(dev, &ret1, &ret2, &errno1, &errno2, metrics);

	if ((ret1 >= 0) || ((errno1 != ENOTDIR) &&
			    (errno1 != ENOENT) &&
			    (errno1 != EPERM))) {
		pr_fail("%s: chroot(\"%s\"), expected ENOTDIR"
			", got instead errno=%d (%s)\n",
			args->name, dev, errno1, strerror(errno1));
		return EXIT_FAILURE;
	}
	return EXIT_SUCCESS;
}

/*
 *  stress_chroot_test7()
 *	try with a stupidly long path
 */
static int stress_chroot_test7(
	const stress_args_t *args,
	chroot_metrics_t *metrics)
{
	const size_t path_len = 256 * KB;
	int ret1, ret2, errno1, errno2;
	char *path;
	
	/* Don't throw a failure of we can't allocate large path */
	path = malloc(path_len);
	if (!path)
		return EXIT_SUCCESS;

	stress_strnrnd(path, path_len);
	path[0] = '/';

	do_chroot(path, &ret1, &ret2, &errno1, &errno2, metrics);

	if ((ret1 >= 0) || ((errno1 != ENOTDIR) &&
			    (errno1 != ENAMETOOLONG) &&
			    (errno1 != ENOENT) &&
			    (errno1 != EPERM))) {
		pr_fail("%s: chroot(\"%-10.10s..\"), expected ENAMETOOLONG"
			", got instead errno=%d (%s)\n",
			args->name, path, errno1, strerror(errno1));
		free(path);
		return EXIT_FAILURE;
	}
	free(path);
	return EXIT_SUCCESS;
}

static const stress_chroot_test_func test_chroot_test_funcs[] = {
	stress_chroot_test1,
	stress_chroot_test2,
	stress_chroot_test3,
	stress_chroot_test4,
	stress_chroot_test5,
	stress_chroot_test6,
	stress_chroot_test7
};

/*
 *  stress_chroot()
 *	stress chroot system call
 */
static int stress_chroot(const stress_args_t *args)
{
	size_t i = 0;
	int fd, ret = EXIT_FAILURE;
	chroot_metrics_t *metrics;
	double rate;

	metrics = mmap(NULL, sizeof(*metrics), PROT_READ | PROT_WRITE,
			MAP_SHARED | MAP_ANONYMOUS, -1, 0);
	if (metrics == MAP_FAILED) {
		pr_inf_skip("%s: cannot mmap metrics shared data, skipping stressor\n",
			args->name);
		return EXIT_FAILURE;
	}

	stress_strnrnd(longpath, sizeof(longpath));
	(void)stress_temp_dir(badpath, sizeof(badpath), "badpath", args->pid, 0xbad);
	(void)stress_temp_dir_args(args, temppath, sizeof(temppath));
	(void)stress_temp_filename_args(args, filename, sizeof(filename), stress_mwc32());
	if (mkdir(temppath, S_IRWXU) < 0) {
		pr_fail("%s: mkdir %s failed, errno=%d (%s)\n",
			args->name, temppath, errno, strerror(errno));
		goto tidy_ret;
	}
	if ((fd = creat(filename, S_IRUSR | S_IWUSR)) < 0) {
		pr_fail("%s: create %s failed, errno=%d (%s)\n",
			args->name, filename, errno, strerror(errno));
		goto tidy_dir;
	}
	(void)close(fd);

	stress_set_proc_state(args->name, STRESS_STATE_RUN);

	do {
		pid_t pid;
again:
		pid = fork();
		if (pid < 0) {
			if (stress_redo_fork(errno))
				goto again;
		} else if (pid == 0) {
			stress_set_oom_adjustment(args->name, true);
			(void)sched_settings_apply(true);

			ret = test_chroot_test_funcs[i](args, metrics);

			/* Children */
			_exit(ret);
		} else {
			/* Parent */
			int status, waitret;

			waitret = shim_waitpid(pid, &status, 0);
			if (waitret < 0) {
				if (errno == EINTR)
					break;
				pr_fail("%s: waitpid waiting on chroot child failed, errno=%d (%s)\n",
					args->name, errno, strerror(errno));
				goto tidy_all;
			}
			if (WEXITSTATUS(status) != EXIT_SUCCESS)
				goto tidy_all;

			inc_counter(args);
		}
		i++;
		i %= SIZEOF_ARRAY(test_chroot_test_funcs);
	} while (keep_stressing(args));

	rate = (metrics->duration > 0.0) ? metrics->count / metrics->duration : 0.0;
	stress_misc_stats_set(args->misc_stats, 0, "chroot calls per sec", rate);

	ret = EXIT_SUCCESS;
tidy_all:
	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);

	(void)shim_unlink(filename);
tidy_dir:
	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);

	(void)shim_rmdir(temppath);
tidy_ret:
	(void)munmap((void *)metrics, sizeof(*metrics));
	return ret;
}

stressor_info_t stress_chroot_info = {
	.stressor = stress_chroot,
	.supported = stress_chroot_supported,
	.class = CLASS_OS,
	.help = help
};
#else

/*
 *  stress_chroot_supported()
 *      check if we can run this as root
 */
static int stress_chroot_supported(const char *name)
{
	pr_inf("%s: stressor is not supported on this system\n", name);
	return -1;
}

stressor_info_t stress_chroot_info = {
	.stressor = stress_not_implemented,
	.supported = stress_chroot_supported,
	.class = CLASS_OS,
	.verify = VERIFY_ALWAYS,
	.help = help,
	.unimplemented_reason = "built without chroot() support"
};
#endif
