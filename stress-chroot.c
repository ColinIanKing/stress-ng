/*
 * Copyright (C) 2013-2021 Canonical, Ltd.
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
#include "core-builtin.h"
#include "core-capabilities.h"
#include "core-mmap.h"
#include "core-out-of-memory.h"

static const stress_help_t help[] = {
	{ NULL,	"chroot N",	"start N workers thrashing chroot" },
	{ NULL,	"chroot-ops N",	"stop chroot workers after N bogo operations" },
	{ NULL,	NULL,		NULL }
};

#if defined(HAVE_CHROOT)

typedef struct {
	stress_args_t *args;
	stress_metrics_t metrics;
	uint32_t	escape_flags;
	ino_t 		rootpath_inode;
	int		cwd_fd;
} chroot_shared_data_t;

typedef int (*stress_chroot_test_func)(chroot_shared_data_t *data);

static char temppath[PATH_MAX];
static char longpath[PATH_MAX + 32];
static char badpath[PATH_MAX];
static char filename[PATH_MAX];

#define CHROOT_ESCAPE_CHDIR		(0x0001)
#define CHROOT_ESCAPE_FD		(0x0002)

typedef struct {
	const char *name;
	const uint32_t mask;
} chroot_escape_t;

static const chroot_escape_t chroot_escapes[] = {
	{ "chdir",	CHROOT_ESCAPE_CHDIR },
	{ "fd",		CHROOT_ESCAPE_FD },
};

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

static ino_t chroot_inode(const char *path)
{
	struct stat statbuf;

	if (shim_stat(path, &statbuf) < 0)
		return (ino_t)-1;
	return statbuf.st_ino;
}

static int chroot_up(void)
{
	int i;
	ino_t previous;

	previous = chroot_inode(".");
	if (previous == (ino_t)-1)
		return -1;

	for (i = 0; i < PATH_MAX; i++) {
		ino_t current;

		if (chdir("..") < 0)
			return -1;
		current = chroot_inode(".");
		if (current == (ino_t)-1)
			return -1;
		if (current == previous)
			break;
		previous = current;
	}
	return 0;
}

/*
 *  do_chroot()
 *	helper to do a chroot followed by chdir
 */
static void do_chroot(
	chroot_shared_data_t *data,
	const char *path,
	void (*escape_func)(chroot_shared_data_t *data),
	int *ret1, int *ret2,
	int *errno1, int *errno2)
{
	double t1, t2;

	t1 = stress_time_now();
	*ret1 = chroot(path);
	*errno1 = errno;
	t2 = stress_time_now();
	if (*ret1 == 0) {
		data->metrics.duration += t2 - t1;
		data->metrics.count += 1.0;
	}

	if (escape_func)
		escape_func(data);

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
static int stress_chroot_test1(chroot_shared_data_t *data)
{
	char cwd[PATH_MAX];
	int ret1, ret2, errno1, errno2;

	do_chroot(data, temppath, NULL, &ret1, &ret2, &errno1, &errno2);
	/*
	 * We check for error, ENOENT can happen on termination
	 * so ignore this error
	 */
	if ((ret1 < 0) && (errno1 != ENOENT)) {
		pr_fail("%s: chroot(\"%s\"), errno=%d (%s)\n",
			data->args->name, temppath, errno1, strerror(errno1));
		return EXIT_FAILURE;
	}
	if (ret2 < 0) {
		pr_fail("%s: chdir(\"%s/\") failed, errno=%d (%s)\n",
			data->args->name, temppath, errno2, strerror(errno2));
		return EXIT_FAILURE;
	}
	if (!getcwd(cwd, sizeof(cwd))) {
		pr_fail("%s: getcwd failed, errno=%d (%s)\n",
			data->args->name, errno, strerror(errno));
		return EXIT_FAILURE;
	}
	if (strcmp(cwd, "/")) {
		pr_fail("%s: cwd in chroot is \"%s\" and not \"/\"\n", data->args->name, cwd);
		return EXIT_FAILURE;
	}
	return EXIT_SUCCESS;
}

/*
 *  stress_chroot_test2()
 *	check if path out of address space throws EFAULT error
 */
static int stress_chroot_test2(chroot_shared_data_t *data)
{
#if defined(__linux__)
	int ret1, ret2, errno1, errno2;

	do_chroot(data, (void *)1, NULL, &ret1, &ret2, &errno1, &errno2);
	if ((ret1 >= 0) || (errno1 != EFAULT)) {
		pr_fail("%s: chroot(\"(void *)1\"), expected EFAULT"
			", got instead errno=%d (%s)\n",
			data->args->name, errno1, strerror(errno1));
		return EXIT_FAILURE;
	}
#else
	(void)data;
#endif
	return EXIT_SUCCESS;
}

/*
 *  stress_chroot_test3()
 *	see if long path is handled correctly
 */
static int stress_chroot_test3(chroot_shared_data_t *data)
{
	int ret1, ret2, errno1, errno2;

	do_chroot(data, longpath, NULL, &ret1, &ret2, &errno1, &errno2);
#if defined(__HAIKU__)
	if ((ret1 >= 0) || ((errno1 != EINVAL) && (errno1 != ENAMETOOLONG))) {
#else
	if ((ret1 >= 0) || (errno1 != ENAMETOOLONG)) {
#endif
		pr_fail("%s: chroot(\"<very long path>\"), expected "
			"ENAMETOOLONG, got instead errno=%d (%s)\n",
			data->args->name, errno1, strerror(errno1));
		return EXIT_FAILURE;
	}
	return EXIT_SUCCESS;
}

/*
 *  stress_chroot_test4()
 *	check if chroot to a path that does not exist returns ENOENT
 */
static int stress_chroot_test4(chroot_shared_data_t *data)
{
	int ret1, ret2, errno1, errno2;

	do_chroot(data, badpath, NULL, &ret1, &ret2, &errno1, &errno2);
	if ((ret1 >= 0) || (errno1 != ENOENT)) {
		pr_fail("%s: chroot(\"%s\"), expected ENOENT"
			", got instead errno=%d (%s)\n",
			data->args->name, badpath, errno1, strerror(errno1));
		return EXIT_FAILURE;
	}
	return EXIT_SUCCESS;
}

/*
 *  stress_chroot_test5()
 *	check if chroot to a file returns ENOTDIR
 */
static int stress_chroot_test5(chroot_shared_data_t *data)
{
	int ret1, ret2, errno1, errno2;

	do_chroot(data, filename, NULL, &ret1, &ret2, &errno1, &errno2);
	/*
	 * We check for error, ENOENT can happen on termination
	 * so ignore this error
	 */
	if ((ret1 >= 0) || ((errno1 != ENOTDIR) &&
			    (errno1 != ENOENT) &&
			    (errno1 != EPERM))) {
		pr_fail("%s: chroot(\"%s\"), expected ENOTDIR"
			", got instead errno=%d (%s)\n",
			data->args->name, filename, errno1, strerror(errno1));
		return EXIT_FAILURE;
	}
	return EXIT_SUCCESS;
}

/*
 *  stress_chroot_test6()
 *	check if chroot to a device path fails with ENOTDIR
 */
static int stress_chroot_test6(chroot_shared_data_t *data)
{
	int ret1, ret2, errno1, errno2;
	static const char dev[] = "/dev/null";

	do_chroot(data, dev, NULL, &ret1, &ret2, &errno1, &errno2);
	if ((ret1 >= 0) || ((errno1 != ENOTDIR) &&
			    (errno1 != ENOENT) &&
			    (errno1 != EPERM))) {
		pr_fail("%s: chroot(\"%s\"), expected ENOTDIR"
			", got instead errno=%d (%s)\n",
			data->args->name, dev, errno1, strerror(errno1));
		return EXIT_FAILURE;
	}
	return EXIT_SUCCESS;
}

/*
 *  stress_chroot_test7()
 *	try with a stupidly long path
 */
static int stress_chroot_test7(chroot_shared_data_t *data)
{
	const size_t path_len = 256 * KB;
	int ret1, ret2, errno1, errno2;
	char *path;

	/* Don't throw a failure of we can't allocate large path */
	path = (char *)malloc(path_len);
	if (!path)
		return EXIT_SUCCESS;

	stress_rndstr(path, path_len);
	path[0] = '/';

	do_chroot(data, path, NULL, &ret1, &ret2, &errno1, &errno2);
	if ((ret1 >= 0) || ((errno1 != ENOTDIR) &&
			    (errno1 != ENAMETOOLONG) &&
			    (errno1 != ENOENT) &&
			    (errno1 != EPERM))) {
		pr_fail("%s: chroot(\"%-10.10s..\"), expected ENAMETOOLONG"
			", got instead errno=%d (%s)\n",
			data->args->name, path, errno1, strerror(errno1));
		free(path);
		return EXIT_FAILURE;
	}
	free(path);
	return EXIT_SUCCESS;
}

/*
 *  stress_chroot_test8()
 *	check if we can chroot to a valid directory and
 *	repeated chdir escape
 */
static int stress_chroot_test8(chroot_shared_data_t *data)
{
	const int rc = EXIT_SUCCESS;
	ino_t inode;

	if (chroot_up() < 0)
		return rc;
	if (chroot(".") < 0)
		return rc;
	inode = chroot_inode(".");
	if (inode == (ino_t)-1)
		return rc;
	if (inode == data->rootpath_inode)
		data->escape_flags |= CHROOT_ESCAPE_CHDIR;

	return EXIT_SUCCESS;
}

/*
 *  stress_chroot_test9()
 *	check if we can chroot to a valid directory and
 *	chdir on fd
 */
static int stress_chroot_test9(chroot_shared_data_t *data)
{
	const int rc = EXIT_SUCCESS;
	ino_t inode;

	if (chdir(temppath) < 0)
		return rc;
	if (chroot(".") < 0)
		return rc;
	if (fchdir(data->cwd_fd) < 0)
		return rc;
	if (chroot_up() < 0)
		return rc;
	if (chroot(".") < 0)
		return rc;
	inode = chroot_inode(".");
	if (inode == (ino_t)-1)
		return rc;
	if (inode == data->rootpath_inode)
		data->escape_flags |= CHROOT_ESCAPE_FD;

	return rc;
}


static const stress_chroot_test_func test_chroot_test_funcs[] = {
	stress_chroot_test1,
	stress_chroot_test2,
	stress_chroot_test3,
	stress_chroot_test4,
	stress_chroot_test5,
	stress_chroot_test6,
	stress_chroot_test7,
	stress_chroot_test8,
	stress_chroot_test9,
};

static void stress_chroot_report_escapes(
	stress_args_t *args,
	const chroot_shared_data_t *data)
{
	size_t i, j;
	char buf[1024];

	(void)shim_memset(buf, 0, sizeof(buf));
	for (i = 0, j = 0; i < SIZEOF_ARRAY(chroot_escapes); i++) {
		if (data->escape_flags & chroot_escapes[i].mask) {
			shim_strlcat(buf, " ", sizeof(buf));
			shim_strlcat(buf, chroot_escapes[i].name, sizeof(buf));
			j++;
		}
	}
	if (j) {
		pr_inf("%s: escaped chroot using method%s:%s\n",
			args->name, (j > 1) ? "s" : "", buf);
	}
}

/*
 *  stress_chroot()
 *	stress chroot system call
 */
static int stress_chroot(stress_args_t *args)
{
	size_t i = 0;
	int fd, ret = EXIT_FAILURE;
	double rate;
	chroot_shared_data_t *data;

	data = (chroot_shared_data_t*)stress_mmap_populate(NULL,
			sizeof(*data), PROT_READ | PROT_WRITE,
			MAP_SHARED | MAP_ANONYMOUS, -1, 0);
	if (data == MAP_FAILED) {
		pr_inf_skip("%s: cannot mmap metrics shared data%s, skipping stressor\n",
			args->name, stress_get_memfree_str());
		return EXIT_FAILURE;
	}
	stress_set_vma_anon_name(data, sizeof(*data), "metrics");
	stress_zero_metrics(&data->metrics, 1);
	data->args = args;
	data->rootpath_inode = chroot_inode("/");

	stress_rndstr(longpath, sizeof(longpath));
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
	data->cwd_fd = open(".", O_DIRECTORY | O_RDONLY);

	stress_set_proc_state(args->name, STRESS_STATE_SYNC_WAIT);
	stress_sync_start_wait(args);
	stress_set_proc_state(args->name, STRESS_STATE_RUN);

	do {
		pid_t pid;
again:
		pid = fork();
		if (pid < 0) {
			if (stress_redo_fork(args, errno))
				goto again;
		} else if (pid == 0) {
			stress_set_proc_state(args->name, STRESS_STATE_RUN);
			stress_set_oom_adjustment(args, true);
			(void)sched_settings_apply(true);

			ret = test_chroot_test_funcs[i](data);

			/* Children */
			_exit(ret);
		} else {
			/* Parent */
			int status;
			pid_t waitret;

			waitret = shim_waitpid(pid, &status, 0);
			if (waitret < 0) {
				if (errno == EINTR)
					break;
				pr_fail("%s: waitpid waiting on chroot child PID %" PRIdMAX " failed, errno=%d (%s)\n",
					args->name, (intmax_t)pid, errno, strerror(errno));
				goto tidy_all;
			}
			if (WEXITSTATUS(status) != EXIT_SUCCESS)
				goto tidy_all;

			stress_bogo_inc(args);
		}
		i++;
		if (i >= SIZEOF_ARRAY(test_chroot_test_funcs))
			i = 0;
	} while (stress_continue(args));

	if (stress_instance_zero(args))
		stress_chroot_report_escapes(args, data);
	rate = (data->metrics.duration > 0.0) ? data->metrics.count / data->metrics.duration : 0.0;
	stress_metrics_set(args, 0, "chroot calls per sec",
		rate, STRESS_METRIC_HARMONIC_MEAN);

	ret = EXIT_SUCCESS;

	if (data->cwd_fd != -1)
		(void)close(data->cwd_fd);
tidy_all:
	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);

	(void)shim_unlink(filename);
tidy_dir:
	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);

	(void)shim_rmdir(temppath);
tidy_ret:
	(void)munmap((void *)data, sizeof(*data));
	return ret;
}

const stressor_info_t stress_chroot_info = {
	.stressor = stress_chroot,
	.supported = stress_chroot_supported,
	.classifier = CLASS_OS,
	.verify = VERIFY_ALWAYS,
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

const stressor_info_t stress_chroot_info = {
	.stressor = stress_unimplemented,
	.supported = stress_chroot_supported,
	.classifier = CLASS_OS,
	.help = help,
	.unimplemented_reason = "built without chroot() support"
};
#endif
