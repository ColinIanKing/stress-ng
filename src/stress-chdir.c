/*
 * Copyright (C) 2013-2021 Canonical, Ltd.
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

static const stress_help_t help[] = {
	{ NULL, "chdir N",	"start N workers thrashing chdir on many paths" },
	{ NULL, "chdir-ops N",	"stop chdir workers after N bogo chdir operations" },
	{ NULL,	"chdir-dirs N",	"select number of directories to exercise chdir on" },
	{ NULL,	NULL,		NULL }
};

/*
 *  stress_set_chdir_dirs()
 *	set number of chdir directories from given option string
 */
static int stress_set_chdir_dirs(const char *opt)
{
	uint64_t chdir_dirs;

	chdir_dirs = stress_get_uint32(opt);
	stress_check_range("chdir-dirs", chdir_dirs,
		MIN_CHDIR_DIRS, MAX_CHDIR_DIRS);
	return stress_set_setting("chdir-dirs", TYPE_ID_UINT32, &chdir_dirs);
}

/*
 *  stress_chdir
 *	stress chdir calls
 */
static int stress_chdir(const stress_args_t *args)
{
	uint32_t i, chdir_dirs = DEFAULT_CHDIR_DIRS;
	char path[PATH_MAX], cwd[PATH_MAX], badpath[PATH_MAX], longpath[PATH_MAX + 16];
	int rc, ret = EXIT_FAILURE, *fds;
	char **paths;
	bool *mkdir_ok;
	struct stat statbuf;
	const bool is_root = stress_check_capability(SHIM_CAP_IS_ROOT);

	(void)stress_get_setting("chdir-dirs", &chdir_dirs);
	paths = calloc(chdir_dirs, sizeof(*paths));
	if (!paths) {
		pr_err("%s: out of memory allocating paths\n", args->name);
		return EXIT_NO_RESOURCE;
	}
	fds = calloc(chdir_dirs, sizeof(*fds));
	if (!fds) {
		pr_err("%s: out of memory allocating file descriptors\n", args->name);
		free(paths);
		return EXIT_NO_RESOURCE;
	}
	mkdir_ok = calloc(chdir_dirs, sizeof(*mkdir_ok));
	if (!mkdir_ok) {
		pr_err("%s: out of memory allocating file descriptors\n", args->name);
		free(fds);
		free(paths);
		return EXIT_NO_RESOURCE;
	}

	if (getcwd(cwd, sizeof(cwd)) == NULL) {
		pr_fail("%s: getcwd failed, errno=%d (%s)\n",
			args->name, errno, strerror(errno));
		goto err;
	}

	rc = stress_temp_dir_mk_args(args);
	if (rc < 0) {
		ret = exit_status(-rc);
		goto err;
	}

	for (i = 0; i < chdir_dirs; i++) {
		fds[i] = -1;
		paths[i] = NULL;
	}

	stress_strnrnd(longpath, sizeof(longpath));
	longpath[0] = '/';

	(void)stress_temp_filename_args(args, badpath, sizeof(badpath), ~0ULL);

	/* Populate */
	for (i = 0; i < chdir_dirs; i++) {
		uint64_t rnd = (uint64_t)stress_mwc32() << 32;
		uint32_t gray_code = (i >> 1) ^ i;
		int flags = O_RDONLY;

#if defined(O_DIRECTORY)
		flags |= O_DIRECTORY;
#endif
		(void)stress_temp_filename_args(args,
			path, sizeof(path), rnd | gray_code);
		paths[i] = strdup(path);
		if (paths[i] == NULL)
			goto abort;
		rc = mkdir(path, S_IRUSR | S_IWUSR | S_IXUSR);
		if (rc < 0) {
			if ((errno == ENOMEM) ||
			    (errno == ENOSPC) ||
			    (errno == EMLINK)) {
				mkdir_ok[i] = false;
				fds[i] = -1;
				continue;
			}
			ret = exit_status(errno);
			if (ret == EXIT_FAILURE)
				pr_fail("%s: mkdir %s failed, errno=%d (%s)\n",
					args->name, path, errno, strerror(errno));
			goto abort;
		}
		mkdir_ok[i] = true;
		fds[i] = open(paths[i], flags);
		if (!keep_stressing_flag())
			goto done;

		if ((i == 0) && (stat(path, &statbuf) < 0)) {
			pr_fail("%s: fstat on %s failed, errno=%d (%s)\n",
				args->name, path, errno, strerror(errno));
		}
	}

	stress_set_proc_state(args->name, STRESS_STATE_RUN);

	do {
		for (i = 0; i < chdir_dirs; i++) {
			uint32_t j = stress_mwc32() % chdir_dirs;
			const int fd = fds[j] >= 0 ? fds[j] : fds[0];

			if (!keep_stressing(args))
				goto done;
			if (mkdir_ok[i] && (chdir(paths[i]) < 0)) {
				if (errno != ENOMEM) {
					pr_fail("%s: chdir %s failed, errno=%d (%s)\n",
						args->name, paths[i], errno, strerror(errno));
					goto abort;
				}
			}

			if ((fd >= 0) && (fchdir(fd) < 0)) {
				if (errno != ENOMEM) {
					pr_fail("%s: fchdir failed, errno=%d (%s)\n",
						args->name, errno, strerror(errno));
					goto abort;
				}
			}

			/*
			 *  chdir to / should always work, surely?
			 */
			if (chdir("/") < 0) {
				if ((errno != ENOMEM) && (errno != EACCES)) {
					pr_fail("%s: chdir / failed, errno=%d (%s)\n",
						args->name, errno, strerror(errno));
					goto abort;
				}
			}

			/*
			 *  chdir to path that won't allow to access,
			 *  this is designed to exercise a failure. Don't
			 *  exercise this if root as this won't fail for
			 *  root.
			 */
			if (!is_root && (fchmod(fd, 0000) == 0)) {
				rc = fchdir(fd);
				(void)rc;
				rc = fchmod(fd, statbuf.st_mode & 0777);
				(void)rc;
			}

			while (keep_stressing(args)) {
				/* We need chdir to cwd to always succeed */
				if (chdir(cwd) == 0)
					break;
				/* Maybe low memory, force retry */
				if (errno != ENOMEM) {
					pr_fail("%s: chdir %s failed, errno=%d (%s)\n",
						args->name, cwd, errno, strerror(errno));
					goto tidy;
				}
			}
		}
		/*
		 *  chdir to a non-existent path
		 */
		rc = chdir(badpath);
		(void)rc;

		/*
		 *  chdir to an invalid non-directory
		 */
		rc = chdir("/dev/null");
		(void)rc;

		/*
		 *  fchdir to an invalid file descriptor
		 */
		rc = fchdir(-1);
		(void)rc;

		/*
		 *  chdir to a bad directory
		 */
		rc = chdir("");
		(void)rc;

		/*
		 *  chdir to an overly long directory name
		 */
		rc = chdir(longpath);
		(void)rc;

		inc_counter(args);
	} while (keep_stressing(args));
done:
	ret = EXIT_SUCCESS;
abort:
	if (chdir(cwd) < 0)
		pr_fail("%s: chdir %s failed, errno=%d (%s)\n",
			args->name, cwd, errno, strerror(errno));
tidy:
	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);
	/* force unlink of all files */
	pr_tidy("%s: removing %" PRIu32 " directories\n",
		args->name, chdir_dirs);

	for (i = 0; (i < chdir_dirs) && paths[i] ; i++) {
		if (fds[i] >= 0)
			(void)close(fds[i]);
		if (paths[i]) {
			(void)rmdir(paths[i]);
			free(paths[i]);
		}
	}
	(void)stress_temp_dir_rm_args(args);
err:
	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);
	free(mkdir_ok);
	free(fds);
	free(paths);

	return ret;
}

static const stress_opt_set_func_t opt_set_funcs[] = {
	{ OPT_chdir_dirs,	stress_set_chdir_dirs },
	{ 0,			NULL }
};

stressor_info_t stress_chdir_info = {
	.stressor = stress_chdir,
	.class = CLASS_FILESYSTEM | CLASS_OS,
	.opt_set_funcs = opt_set_funcs,
	.help = help
};
