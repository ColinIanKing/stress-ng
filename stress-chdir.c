/*
 * Copyright (C) 2013-2020 Canonical, Ltd.
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
	char path[PATH_MAX], cwd[PATH_MAX];
	int rc, ret = EXIT_FAILURE, *fds;
	char **paths;

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

	if (getcwd(cwd, sizeof(cwd)) == NULL) {
		pr_fail_err("getcwd");
		goto err;
	}

	rc = stress_temp_dir_mk_args(args);
	if (rc < 0) {
		ret = exit_status(-rc);
		goto err;
	}

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
			ret = exit_status(errno);
			pr_fail_err("mkdir");
			goto abort;
		}
		fds[i] = open(paths[i], flags);
		if (!keep_stressing_flag())
			goto done;
	}

	do {
		for (i = 0; i < chdir_dirs; i++) {
			uint32_t j = stress_mwc32() % chdir_dirs;
			const int fd = fds[j] >= 0 ? fds[j] : fds[0];

			if (!keep_stressing())
				goto done;
			if (chdir(paths[i]) < 0) {
				if (errno != ENOMEM) {
					pr_fail_err("chdir");
					goto abort;
				}
			}

			if ((fd >= 0) && (fchdir(fd) < 0)) {
				if (errno != ENOMEM) {
					pr_fail_err("fchdir");
					goto abort;
				}
			}

			/*
			 *  chdir to / should always work, surely?
			 */
			if (chdir("/") < 0) {
				if ((errno != ENOMEM) && (errno != EACCES)) {
					pr_fail_err("chdir");
					goto abort;
				}
			}
			while (keep_stressing()) {
				/* We need chdir to cwd to always succeed */
				if (chdir(cwd) == 0)
					break;
				/* Maybe low memory, force retry */
				if (errno != ENOMEM) {
					pr_fail_err("chdir");
					goto tidy;
				}
			}
		}
		inc_counter(args);
	} while (keep_stressing());
done:
	ret = EXIT_SUCCESS;
abort:
	if (chdir(cwd) < 0)
		pr_fail_err("chdir");
tidy:
	/* force unlink of all files */
	pr_tidy("%s: removing %" PRIu32 " directories\n",
		args->name, chdir_dirs);

	for (i = 0; (i < chdir_dirs) && paths[i] ; i++) {
		(void)close(fds[i]);
		(void)rmdir(paths[i]);
		free(paths[i]);
	}
	(void)stress_temp_dir_rm_args(args);
err:
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
