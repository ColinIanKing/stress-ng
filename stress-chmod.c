// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2013-2021 Canonical, Ltd.
 * Copyright (C) 2022-2023 Colin Ian King.
 *
 */
#include "stress-ng.h"

#if defined(HAVE_LIBGEN_H)
#include <libgen.h>
#endif

static const stress_help_t help[] = {
	{ NULL,	"chmod N",	"start N workers thrashing chmod file mode bits " },
	{ NULL,	"chmod-ops N",	"stop chmod workers after N bogo operations" },
	{ NULL,	NULL,		NULL }
};

static const mode_t modes[] = {
#if defined(S_ISUID)
	S_ISUID,
#endif
#if defined(S_ISGID)
	S_ISGID,
#endif
#if defined(S_ISVTX)
	S_ISVTX,
#endif
#if defined(S_IRUSR)
	S_IRUSR,
#endif
#if defined(S_IWUSR)
	S_IWUSR,
#endif
#if defined(S_IXUSR)
	S_IXUSR,
#endif
#if defined(S_IRGRP)
	S_IRGRP,
#endif
#if defined(S_IWGRP)
	S_IWGRP,
#endif
#if defined(S_IXGRP)
	S_IXGRP,
#endif
#if defined(S_IROTH)
	S_IROTH,
#endif
#if defined(S_IWOTH)
	S_IWOTH,
#endif
#if defined( S_IXOTH)
	S_IXOTH,
#endif
	0
};

static int OPTIMIZE3 stress_chmod_check(const int ret)
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
#if defined(EFTYPE)
		EFTYPE,
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
 *  do_fchmod()
 *	fchmod the 4 different masks from a mode flag, namely:
 *		mode flag
 *		all mode flags or'd together
 *		inverse mode flag
 *		inverse all mode flags or'd together
 */
static int do_fchmod(
	const int fd,
	const int bad_fd,
	const size_t i,
	const mode_t mask,
	const mode_t all_mask)
{
	stress_chmod_check(fchmod(fd, modes[i]) < 0);
	stress_chmod_check(fchmod(fd, mask) < 0);
	stress_chmod_check(fchmod(fd, modes[i] ^ all_mask) < 0);
	stress_chmod_check(fchmod(fd, mask ^ all_mask) < 0);

	/*
	 *  Exercise bad fchmod, ignore failure
	 */
	VOID_RET(int, fchmod(bad_fd, modes[i]));

	return 0;
}

/*
 *  do_chmod()
 *	chmod the 4 different masks from a mode flag, namely:
 *		mode flag
 *		all mode flags or'd together
 *		inverse mode flag
 *		inverse all mode flags or'd together
 */
static int do_chmod(
	const int dfd,
	const int bad_fd,
	const char *filebase,
	const char *filename,
	const char *longpath,
	const size_t i,
	const mode_t mask,
	const mode_t all_mask,
	const size_t mode_count,
	const int *mode_perms)
{
	static size_t index;

	if (!mode_count)
		return 0;

	(void)chmod(filename, (mode_t)mode_perms[index]);
	index++;
	if (index >= mode_count)
		index = 0;

	stress_chmod_check(chmod(filename, modes[i]) < 0);
	stress_chmod_check(chmod(filename, mask) < 0);
	stress_chmod_check(chmod(filename, modes[i] ^ all_mask) < 0);
	stress_chmod_check(chmod(filename, mask ^ all_mask) < 0);

#if defined(HAVE_FCHMODAT)
	if (dfd >= 0) {
		stress_chmod_check(fchmodat(dfd, filebase, modes[i], 0) < 0);
		stress_chmod_check(fchmodat(dfd, filebase, mask, 0) < 0);
		stress_chmod_check(fchmodat(dfd, filebase, modes[i] ^ all_mask, 0) < 0);
		stress_chmod_check(fchmodat(dfd, filebase, mask ^ all_mask, 0) < 0);

		/*
		 *  Exercise bad fchmodat, ignore failure
		 */
		VOID_RET(int, fchmodat(bad_fd, filebase, modes[i], 0));
	}
#else
	(void)dfd;
	(void)bad_fd;
	(void)filebase;
#endif

	/*
	 *  Exercise illegal filename
	 */
	VOID_RET(int, chmod("", modes[i]));

	/*
	 *  Exercise illegal overly long pathname
	 */
	VOID_RET(int, chmod(longpath, modes[i]));

	return 0;
}

/*
 *  stress_chmod
 *	stress chmod
 */
static int stress_chmod(const stress_args_t *args)
{
	const pid_t ppid = getppid();
	int fd = -1, rc = EXIT_FAILURE, retries = 0, dfd = -1;
	size_t i;
	const int bad_fd = stress_get_bad_fd();
	mode_t all_mask = 0;
	char filename[PATH_MAX], pathname[PATH_MAX], longpath[PATH_MAX + 16];
	char tmp[PATH_MAX], *filebase;
	int *mode_perms = NULL;
	size_t mode_count;

	for (i = 0; modes[i]; i++)
		all_mask |= modes[i];

	mode_count = stress_flag_permutation((int)all_mask, &mode_perms);

	/*
	 *  Allow for multiple workers to chmod the *same* file
	 */
	stress_temp_dir(pathname, sizeof(pathname), args->name, ppid, 0);
	if (mkdir(pathname, S_IRUSR | S_IRWXU) < 0) {
		if (errno != EEXIST) {
			rc = stress_exit_status(errno);
			pr_fail("%s: mkdir %s failed, errno=%d (%s)\n",
				args->name, pathname, errno, strerror(errno));
			free(mode_perms);
			return rc;
		}
	}
#if defined(O_DIRECTORY)
	dfd = open(pathname, O_DIRECTORY | O_RDONLY);
#else
	UNEXPECTED
#endif

	stress_rndstr(longpath, sizeof(longpath));
	longpath[0] = '/';

	(void)stress_temp_filename(filename, sizeof(filename),
		args->name, ppid, 0, 0);
	(void)shim_strlcpy(tmp, filename, sizeof(tmp));
	filebase = basename(tmp);

	if (args->instance == 0) {
		if ((fd = creat(filename, S_IRUSR | S_IWUSR)) < 0) {
			rc = stress_exit_status(errno);
			pr_fail("%s: create %s failed, errno=%d (%s)\n",
				args->name, filename, errno, strerror(errno));
			goto tidy;
		}
	} else {
		/* Other instances must try to open the file */
		for (;;) {
			if ((fd = open(filename, O_RDWR, S_IRUSR | S_IWUSR)) > - 1)
				break;

#if defined(__NetBSD__)
			/* For some reason usleep blocks */
			(void)shim_sched_yield();
#else
			(void)shim_usleep(100000);
#endif
			/* Timed out, then give up */
			if (!stress_continue_flag()) {
				rc = EXIT_SUCCESS;
				goto tidy;
			}
			/* Too many retries? */
			if (++retries >= 10000) {
				pr_err("%s: chmod: file %s took %d "
					"retries to open and gave up "
					"(instance %" PRIu32 ")%s\n",
					args->name, filename, retries, args->instance,
					stress_get_fs_type(filename));
				goto tidy;
			}
		}
	}

	stress_set_proc_state(args->name, STRESS_STATE_RUN);

	do {
		mode_t mask = 0;

		for (i = 0; modes[i]; i++) {
			mask |= modes[i];
			if (do_fchmod(fd, bad_fd, i, mask, all_mask) < 0) {
				pr_fail("%s: fchmod failed, errno=%d (%s)%s\n",
					args->name, errno, strerror(errno),
					stress_get_fs_type(filename));
			}
			if (do_chmod(dfd, bad_fd, filebase, filename, longpath, i,
				     mask, all_mask, mode_count, mode_perms) < 0) {
				if ((errno == ENOENT) || (errno == ENOTDIR)) {
					/*
					 * File was removed during test by
					 * another worker
					 */
					rc = EXIT_SUCCESS;
					goto tidy;
				}
				pr_fail("%s: chmod %s failed, errno=%d (%s)%s\n",
					args->name, filename, errno, strerror(errno),
					stress_get_fs_type(filename));
			}
		}
		shim_fsync(fd);
		stress_bogo_inc(args);
	} while (stress_continue(args));

	rc = EXIT_SUCCESS;
tidy:
	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);

#if defined(O_DIRECTORY)
	if (dfd >= 0)
		(void)close(dfd);
#else
	UNEXPECTED
#endif
	if (fd >= 0) {
		(void)fchmod(fd, 0666);
		(void)close(fd);
	}
	(void)shim_unlink(filename);
	(void)shim_rmdir(pathname);
	free(mode_perms);

	return rc;
}

stressor_info_t stress_chmod_info = {
	.stressor = stress_chmod,
	.class = CLASS_FILESYSTEM | CLASS_OS,
	.verify = VERIFY_ALWAYS,
	.help = help
};
