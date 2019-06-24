/*
 * Copyright (C) 2013-2019 Canonical, Ltd.
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
 * This code is a complete clean re-write of the stress tool by
 * Colin Ian King <colin.king@canonical.com> and attempts to be
 * backwardly compatible with the stress tool by Amos Waterland
 * <apw@rossby.metr.ou.edu> but has more stress tests and more
 * functionality.
 *
 */
#include "stress-ng.h"

static const help_t help[] = {
	{ NULL,	"chroot N",	"start N workers thrashing chroot" },
	{ NULL,	"chroot-ops N",	"stop chhroot workers after N bogo operations" },
	{ NULL,	NULL,		NULL }
};

#if defined(HAVE_CHROOT)

typedef int (*stress_chroot_test_func)(const args_t *args);

static char temppath[PATH_MAX];
static char longpath[PATH_MAX + 32];
static char badpath[PATH_MAX];
static char filename[PATH_MAX];

/*
 *  stress_chroot_supported()
 *      check if we can run this as root
 */
static int stress_chroot_supported(void)
{
	if (!stress_check_capability(SHIM_CAP_SYS_ADMIN)) {
		pr_inf("chroot stressor will be skipped, "
			"need to be running with CAP_SYS_ADMIN "
			"rights for this stressor\n");
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
	int *errno1, int *errno2)
{
	*ret1 = chroot(path);
	*errno1 = errno;

	/*
	 * we must do chdir immediately after a chroot
	 * otherwise we have a security risk. Also
	 * CoverityScan will pick this up as an error.
	 */
	*ret2 = chdir("/");
	*errno2 = errno;
}

static int stress_chroot_test1(const args_t *args)
{
	char cwd[PATH_MAX];
	int ret1, ret2, errno1, errno2;

	do_chroot(temppath, &ret1, &ret2, &errno1, &errno2);

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
		pr_fail_errno("chdir(\"/\")\n", errno2);
		return EXIT_FAILURE;
	}
	if (!getcwd(cwd, sizeof(cwd))) {
		pr_fail_err("getcwd");
		return EXIT_FAILURE;
	}
	if (strcmp(cwd, "/")) {
		pr_fail("%s: cwd in chroot is \"%s\" and not \"/\"\n", args->name, cwd);
		return EXIT_FAILURE;
	}
	return EXIT_SUCCESS;
}

static int stress_chroot_test2(const args_t *args)
{
	int ret1, ret2, errno1, errno2;

	do_chroot((void *)1, &ret1, &ret2, &errno1, &errno2);

	if ((ret1 >= 0) || (errno1 != EFAULT))  {
		pr_fail("%s: chroot(\"%s\"), expected EFAULT"
			", got instead errno=%d (%s)\n",
			args->name, temppath, errno1, strerror(errno1));
		return EXIT_FAILURE;
	}
	if (ret2 < 0) {
		pr_fail_errno("chdir(\"/\")\n", errno2);
		return EXIT_FAILURE;
	}
	return EXIT_SUCCESS;
}

static int stress_chroot_test3(const args_t *args)
{
	int ret1, ret2, errno1, errno2;

	do_chroot(longpath, &ret1, &ret2, &errno1, &errno2);

#if defined(__HAIKU__)
	if ((ret1 >= 0) || (errno1 != EINVAL))  {
#else
	if ((ret1 >= 0) || (errno1 != ENAMETOOLONG))  {
#endif
		pr_fail("%s: chroot(\"<very long path>\"), expected "
			"ENAMETOOLONG, got instead errno=%d (%s)\n",
			args->name, errno1, strerror(errno1));
		return EXIT_FAILURE;
	}
	if (ret2 < 0) {
		pr_fail_errno("chdir(\"/\")\n", errno2);
		return EXIT_FAILURE;
	}
	return EXIT_SUCCESS;
}

static int stress_chroot_test4(const args_t *args)
{
	int ret1, ret2, errno1, errno2;

	do_chroot(badpath, &ret1, &ret2, &errno1, &errno2);

	if ((ret1 >= 0) || (errno1 != ENOENT))  {
		pr_fail("%s: chroot(\"%s\"), expected ENOENT"
			", got instead errno=%d (%s)\n",
			args->name, badpath, errno1, strerror(errno1));
		return EXIT_SUCCESS;
	}
	if (ret2 < 0) {
		pr_fail_errno("chdir(\"/\")\n", errno2);
		return EXIT_FAILURE;
	}
	return EXIT_SUCCESS;
}

static int stress_chroot_test5(const args_t *args)
{
	int ret1, ret2, errno1, errno2;

	do_chroot(filename, &ret1, &ret2, &errno1, &errno2);

	/*
	 * We check for error, ENOENT can happen on termination
	 * so ignore this error
	 */
	if ((ret1 >= 0) || ((errno1 != ENOTDIR) &&
			    (errno1 != ENOENT) && 
			    (errno1 != EPERM)))  {
		pr_fail("%s: chroot(\"%s\"), expected ENOTDIR"
			", got instead errno=%d (%s)\n",
			args->name, filename, errno1, strerror(errno1));
		return EXIT_SUCCESS;
	}
	if (ret2 < 0) {
		pr_fail_errno("chdir(\"/\")\n", errno2);
		return EXIT_FAILURE;
	}
	return EXIT_SUCCESS;
}

static const stress_chroot_test_func test_chroot_test_funcs[] =
{
	stress_chroot_test1,
	stress_chroot_test2,
	stress_chroot_test3,
	stress_chroot_test4,
	stress_chroot_test5
};

/*
 *  stress_chroot()
 *	stress chroot system call
 */
static int stress_chroot(const args_t *args)
{
	size_t i = 0;
	int fd, ret = EXIT_FAILURE;

	stress_strnrnd(longpath, sizeof(longpath));
	(void)stress_temp_dir(badpath, sizeof(badpath), "badpath", args->pid, 0xbad);
	(void)stress_temp_dir_args(args, temppath, sizeof(temppath));
	(void)stress_temp_filename_args(args, filename, sizeof(filename), mwc32());
	if (mkdir(temppath, S_IRWXU) < 0) {
		pr_fail_err("mkdir");
		goto tidy_ret;
	}
	if ((fd = creat(filename, S_IRUSR | S_IWUSR)) < 0) {
		pr_fail_err("creat");
		goto tidy_dir;
	}
	(void)close(fd);

	do {
		pid_t pid;
retry:
		if (!keep_stressing())
			break;

		pid = fork();
		if (pid < 0) {
			goto retry;
		} else if (pid == 0) {
			(void)setpgid(0, g_pgrp);
			set_oom_adjustment(args->name, true);

			ret = test_chroot_test_funcs[i](args);

			/* Children */
			_exit(ret);
		} else {
			/* Parent */
			int status, waitret;

			waitret = shim_waitpid(pid, &status, 0);
			if (waitret < 0) {
				if (errno == EINTR)
					break;
				pr_fail_err("waitpid waiting on chroot child");
				goto tidy_all;
			}
			if (WEXITSTATUS(status) != EXIT_SUCCESS)
				goto tidy_all;

			inc_counter(args);
		}
		i++;
		i %= SIZEOF_ARRAY(test_chroot_test_funcs);
	} while (keep_stressing());

	ret = EXIT_SUCCESS;
tidy_all:
	(void)unlink(filename);
tidy_dir:
	(void)rmdir(temppath);
tidy_ret:
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
static int stress_chroot_supported(void)
{
	pr_inf("chroot stressor is not supported on this system\n");
	return -1;
}

stressor_info_t stress_chroot_info = {
	.stressor = stress_not_implemented,
	.supported = stress_chroot_supported,
	.class = CLASS_OS,
	.help = help
};
#endif
