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
#include "core-attribute.h"

static const stress_help_t help[] = {
	{ NULL,	"chown N",	"start N workers thrashing chown file ownership" },
	{ NULL,	"chown-ops N",	"stop chown workers after N bogo operations" },
	{ NULL,	NULL,		NULL }
};

static int CONST OPTIMIZE3 stress_chown_check(const int ret)
{
	static const int ignore_errors[] = {
#if defined(ENOENT)
		ENOENT,
#endif
#if defined(ENOTDIR)
		ENOTDIR,
#endif
#if defined(ENOSYS)
		ENOSYS,
#endif
#if defined(EPERM)
		EPERM,
#endif
	};

	size_t i;

	if (!ret)
		return 0;

	for (i = 0; i < SIZEOF_ARRAY(ignore_errors); i++) {
		if (errno == ignore_errors[i])
			return 0;
	}
	return -1;
}

/*
 *  do_fchown()
 *	set ownership different ways
 */
static int do_fchown(
	const int fd,
	const int bad_fd,
	const bool cap_chown,
	const uid_t uid,
	const gid_t gid)
{
	int tmp, ret;

	if (stress_chown_check(fchown(fd, uid, gid) < 0))
		return -errno;
	if (stress_chown_check(fchown(fd, (uid_t)-1, gid) < 0))
		return -errno;
	if (stress_chown_check(fchown(fd, uid, (gid_t)-1) < 0))
		return -errno;
	if (stress_chown_check(fchown(fd, (uid_t)-1, (gid_t)-1) < 0))
		return -errno;

	if (cap_chown)
		return 0;
	ret = fchown(fd, (uid_t)0, (gid_t)0);
	if (ret == 0)
		goto restore;
	if (stress_chown_check(ret) < 0)
		goto restore;

	ret = fchown(fd, (uid_t)-1, (gid_t)0);
	if (ret == 0)
		goto restore;
	if (stress_chown_check(ret) < 0)
		goto restore;
	ret = fchown(fd, (uid_t)0, (gid_t)-1);
	if (ret == 0)
		goto restore;
	if (stress_chown_check(ret) < 0)
		goto restore;

	/*
	 *  Exercise fchown with invalid fd
	 */
	VOID_RET(int, fchown(bad_fd, uid, gid));

	return 0;

restore:
	tmp = errno;
	VOID_RET(int, fchown(fd, uid, gid));

	return -tmp;
}

/*
 *  do_chown()
 *	set ownership different ways
 */
static int do_chown(
	int (*chown_func)(const char *pathname, uid_t owner, gid_t group),
	const char *filename,
	const bool cap_chown,
	const uid_t uid,
	const gid_t gid)
{
	int tmp, ret;

	if (stress_chown_check(chown_func(filename, uid, gid)) < 0)
		return -errno;
	if (stress_chown_check(chown_func(filename, (uid_t)-1, gid)) < 0)
		return -errno;
	if (stress_chown_check(chown_func(filename, uid, (gid_t)-1)) < 0)
		return -errno;
	if (stress_chown_check(chown_func(filename, (uid_t)-1, (gid_t)-1)) < 0)
		return -errno;

	if (cap_chown)
		return 0;
	ret = chown_func(filename, (uid_t)0, (gid_t)0);
	if (ret == 0)
		goto restore;
	if (stress_chown_check(ret))
		goto restore;
	ret = chown_func(filename, (uid_t)-1, (gid_t)0);
	if (ret == 0)
		goto restore;
	if (stress_chown_check(ret))
		goto restore;
	ret = chown_func(filename, (uid_t)0, (gid_t)-1);
	if (ret == 0)
		goto restore;
	if (stress_chown_check(ret))
		goto restore;
	return 0;

restore:
	tmp = errno;
	VOID_RET(int, chown_func(filename, uid, gid));

	return -tmp;
}

/*
 *  stress_chown
 *	stress chown
 */
static int stress_chown(stress_args_t *args)
{
	const pid_t ppid = getppid();
	int fd = -1, rc = EXIT_FAILURE, retries = 0;
	const int bad_fd = stress_get_bad_fd();
	char filename[PATH_MAX], pathname[PATH_MAX];
	const uid_t uid = getuid();
	const gid_t gid = getgid();
	bool cap_chown = false;
	int fsync_counter = 0;

	if (geteuid() == 0)
		cap_chown = true;

	/*
	 *  Allow for multiple workers to chown the *same* file
	 */
	stress_temp_dir(pathname, sizeof(pathname), args->name, ppid, 0);
	if (mkdir(pathname, S_IRUSR | S_IRWXU) < 0) {
		if (errno != EEXIST) {
			rc = stress_exit_status(errno);
			pr_fail("%s: mkdir %s failed, errno=%d (%s)\n",
				args->name, pathname, errno, strerror(errno));
			return rc;
		}
	}
	(void)stress_temp_filename(filename, sizeof(filename),
		args->name, ppid, 0, 0);

	if (stress_instance_zero(args)) {
		if ((fd = creat(filename, S_IRUSR | S_IWUSR)) < 0) {
			rc = stress_exit_status(errno);
			pr_fail("%s: creat %s failed, errno=%d (%s)\n",
				args->name, filename, errno, strerror(errno));
			goto tidy;
		}
	} else {
		/* Other instances must try to open the file */
		for (;;) {
			if ((fd = open(filename, O_RDWR)) > - 1)
				break;

#if defined(__NetBSD__)
			/* For some reason usleep blocks */
			(void)shim_sched_yield();
			retries = 0;
#else
			(void)shim_usleep(100000);
#endif
			/* Timed out, then give up */
			if (UNLIKELY(!stress_continue_flag())) {
				rc = EXIT_SUCCESS;
				goto tidy;
			}
			if (++retries >= 1000) {
				pr_inf("%s: chown: file %s took %d "
					"retries to open and gave up "
					"(instance %" PRIu32 ")%s\n",
					args->name, filename, retries, args->instance,
					stress_get_fs_type(filename));
				rc = EXIT_NO_RESOURCE;
				goto tidy;
			}
		}
	}

	stress_set_proc_state(args->name, STRESS_STATE_SYNC_WAIT);
	stress_sync_start_wait(args);
	stress_set_proc_state(args->name, STRESS_STATE_RUN);

#if defined(HAVE_PATHCONF) &&	\
    defined(_PC_CHOWN_RESTRICTED)
	VOID_RET(long int, pathconf(filename, _PC_CHOWN_RESTRICTED));
#endif

	rc = EXIT_SUCCESS;
	do {
		int ret;

		ret = do_fchown(fd, bad_fd, cap_chown, uid, gid);
		if ((ret < 0) && (ret != -EPERM)) {
			pr_fail("%s: fchown failed, errno=%d (%s)%s\n",
				args->name, errno, strerror(errno),
				stress_get_fs_type(filename));
			rc = EXIT_FAILURE;
			break;
		}

		ret = do_chown(chown, filename, cap_chown, uid, gid);
		if ((ret < 0) && (ret != -EPERM)) {
			pr_fail("%s: chown %s failed, errno=%d (%s)%s\n",
				args->name, filename, errno, strerror(errno),
				stress_get_fs_type(filename));
			rc = EXIT_FAILURE;
			break;
		}
		ret = do_chown(lchown, filename, cap_chown, uid, gid);
		if ((ret < 0) && (ret != -EPERM)) {
			pr_fail("%s: lchown %s failed, errno=%d (%s)%s\n",
				args->name, filename, errno, strerror(errno),
				stress_get_fs_type(filename));
			rc = EXIT_FAILURE;
			break;
		}
		if (fsync_counter++ >= 128) {
			fsync_counter = 0;
			(void)shim_fsync(fd);
		}
		stress_bogo_inc(args);
	} while (stress_continue(args));

tidy:
	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);

	if (fd >= 0)
		(void)close(fd);
	(void)shim_unlink(filename);
	(void)shim_rmdir(pathname);

	return rc;
}

const stressor_info_t stress_chown_info = {
	.stressor = stress_chown,
	.classifier = CLASS_FILESYSTEM | CLASS_OS,
	.verify = VERIFY_ALWAYS,
	.help = help
};
