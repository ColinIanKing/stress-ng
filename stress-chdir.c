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

#define MIN_CHDIR_DIRS		(64)
#define MAX_CHDIR_DIRS		(65536)
#define DEFAULT_CHDIR_DIRS	(8192)

typedef struct {
	char *path;		/* path to chdir to */
	int fd;			/* fd of open dir */
	bool mkdir_ok;		/* true if mkdir succeeded */
} stress_chdir_info_t;


static const stress_help_t help[] = {
	{ NULL, "chdir N",	"start N workers thrashing chdir on many paths" },
	{ NULL,	"chdir-dirs N",	"select number of directories to exercise chdir on" },
	{ NULL, "chdir-ops N",	"stop chdir workers after N bogo chdir operations" },
	{ NULL,	NULL,		NULL }
};

static const stress_opt_t opts[] = {
	{ OPT_chdir_dirs, "chdir-dirs", TYPE_ID_UINT32, MIN_CHDIR_DIRS, MAX_CHDIR_DIRS, NULL },
        END_OPT,
};

/*
 *  stress_chdir
 *	stress chdir calls
 */
static int stress_chdir(stress_args_t *args)
{
	uint32_t i, chdir_dirs = DEFAULT_CHDIR_DIRS;
	stress_chdir_info_t *chdir_info;
	char path[PATH_MAX], cwd[PATH_MAX], badpath[PATH_MAX], longpath[PATH_MAX + 16];
	int rc, ret = EXIT_FAILURE;
	struct stat statbuf;
	const bool is_root = stress_check_capability(SHIM_CAP_IS_ROOT);
	bool got_statbuf = false;
	bool tidy_info = false;
	double count = 0.0, duration = 0.0, rate, start_time;

	if (!stress_get_setting("chdir-dirs", &chdir_dirs)) {
		if (g_opt_flags & OPT_FLAGS_MAXIMIZE)
			chdir_dirs = MAX_CHDIR_DIRS;
		if (g_opt_flags & OPT_FLAGS_MINIMIZE)
			chdir_dirs = MIN_CHDIR_DIRS;
	}
	chdir_info = (stress_chdir_info_t *)calloc(chdir_dirs, sizeof(*chdir_info));
	if (!chdir_info) {
		pr_inf_skip("%s: out of memory allocating %" PRIu32 " chdir structs, "
			    "skipping stressor\n", args->name, chdir_dirs);
		return EXIT_NO_RESOURCE;
	}

	if (getcwd(cwd, sizeof(cwd)) == NULL) {
		pr_fail("%s: getcwd failed, errno=%d (%s)\n",
			args->name, errno, strerror(errno));
		goto err;
	}

	rc = stress_temp_dir_mk_args(args);
	if (rc < 0) {
		ret = stress_exit_status(-rc);
		goto err;
	}

	for (i = 0; i < chdir_dirs; i++) {
		chdir_info[i].path = NULL;
		chdir_info[i].fd = -1;
		chdir_info[i].mkdir_ok = false;
	}

	stress_rndstr(longpath, sizeof(longpath));
	longpath[0] = '/';

	(void)stress_temp_filename_args(args, badpath, sizeof(badpath), ~0ULL);
	(void)shim_memset(&statbuf, 0, sizeof(statbuf));
	*path = '\0';	/* Keep static analysis tools happy */

	/* Populate */
	for (i = 0; LIKELY(stress_continue(args) && (i < chdir_dirs)); i++) {
		const uint64_t rnd = (uint64_t)stress_mwc32() << 32;
		const uint32_t gray_code = (i >> 1) ^ i;
		int flags = O_RDONLY;

#if defined(O_DIRECTORY)
		flags |= O_DIRECTORY;
#else
		UNEXPECTED
#endif
		(void)stress_temp_filename_args(args,
			path, sizeof(path), rnd | gray_code);
		chdir_info[i].path = shim_strdup(path);
		if (chdir_info[i].path == NULL)
			goto abort;
		rc = mkdir(path, S_IRUSR | S_IWUSR | S_IXUSR);
		if (rc < 0) {
			if ((errno == ENOMEM) ||
			    (errno == ENOSPC) ||
			    (errno == EMLINK)) {
				chdir_info[i].mkdir_ok = false;
				chdir_info[i].fd = -1;
				continue;
			}
			ret = stress_exit_status(errno);
			if (ret == EXIT_FAILURE)
				pr_fail("%s: mkdir %s failed, errno=%d (%s)\n",
					args->name, path, errno, strerror(errno));
			goto abort;
		}
		chdir_info[i].mkdir_ok = true;
		chdir_info[i].fd = open(chdir_info[i].path, flags);

		if (!got_statbuf) {
			if (shim_stat(path, &statbuf) == 0)
				got_statbuf = true;

		}
	}

	if (!got_statbuf && *path) {
		pr_fail("%s: fstat on %s failed, errno=%d (%s)%s\n",
			args->name, path, errno, strerror(errno),
			stress_get_fs_type(path));
		goto abort;
	}

	stress_set_proc_state(args->name, STRESS_STATE_SYNC_WAIT);
	stress_sync_start_wait(args);
	stress_set_proc_state(args->name, STRESS_STATE_RUN);

	do {
		for (i = 0; LIKELY(stress_continue(args) && (i < chdir_dirs)); i++) {
			const uint32_t j = stress_mwc32modn(chdir_dirs);
			const int fd = chdir_info[j].fd >= 0 ? chdir_info[j].fd : chdir_info[0].fd;
			double t;

			if (chdir_info[i].mkdir_ok && chdir_info[i].path) {
				t = stress_time_now();
				if (chdir(chdir_info[i].path) == 0) {
					duration += stress_time_now() - t;
					count += 1.0;
				} else {
					if (errno != ENOMEM) {
						pr_fail("%s: chdir %s failed, errno=%d (%s)%s\n",
							args->name, chdir_info[i].path,
							errno, strerror(errno),
							stress_get_fs_type(path));
						goto abort;
					}
				}
			}

			if ((fd >= 0) && (fchdir(fd) < 0)) {
				if (errno != ENOMEM) {
					pr_fail("%s: fchdir failed, errno=%d (%s)%s\n",
						args->name,
						errno, strerror(errno),
						stress_get_fs_type(chdir_info[i].path));
					goto abort;
				}
			}

			/*
			 *  chdir to / should always work, surely?
			 */
			t = stress_time_now();
			if (chdir("/") == 0) {
				duration += stress_time_now() - t;
				count += 1.0;
			} else {
				if ((errno != ENOMEM) && (errno != EACCES)) {
					pr_fail("%s: chdir / failed, errno=%d (%s)%s\n",
						args->name,
						errno, strerror(errno),
						stress_get_fs_type("/"));
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
				VOID_RET(int, fchdir(fd));
				VOID_RET(int, fchmod(fd, statbuf.st_mode & 0777));
			}

			while (stress_continue(args)) {
				/* We need chdir to cwd to always succeed */
				t = stress_time_now();
				if (chdir(cwd) == 0) {
					duration += stress_time_now() - t;
					count += 1.0;
					break;
				}
				/* Maybe low memory, force retry */
				if (errno != ENOMEM) {
					pr_fail("%s: chdir %s failed, errno=%d (%s)%s\n",
						args->name, cwd,
						errno, strerror(errno),
						stress_get_fs_type(cwd));
					goto tidy;
				}
			}
		}
		/*
		 *  chdir to a non-existent path
		 */
		VOID_RET(int, chdir(badpath));

		/*
		 *  chdir to an invalid non-directory
		 */
		VOID_RET(int, chdir("/dev/null"));

		/*
		 *  fchdir to an invalid file descriptor
		 */
		VOID_RET(int, fchdir(-1));

		/*
		 *  chdir to a bad directory
		 */
		VOID_RET(int, chdir(""));

		/*
		 *  chdir to an overly long directory name
		 */
		VOID_RET(int, chdir(longpath));

		stress_bogo_inc(args);
	} while (stress_continue(args));

	ret = EXIT_SUCCESS;
abort:
	if (chdir(cwd) < 0)
		pr_fail("%s: chdir %s failed, errno=%d (%s)%s\n",
			args->name, cwd, errno, strerror(errno),
			stress_get_fs_type(cwd));
tidy:
	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);

	/* force unlink of all files */
	start_time = stress_time_now();
	for (i = 0; (i < chdir_dirs) && chdir_info[i].path ; i++) {
		if (chdir_info[i].fd >= 0)
			(void)close(chdir_info[i].fd);
		if (chdir_info[i].path) {
			(void)shim_rmdir(chdir_info[i].path);
			free(chdir_info[i].path);
		}
		/* ..taking a while?, inform user */
		if (stress_instance_zero(args) && !tidy_info &&
		    (stress_time_now() > start_time + 0.5)) {
			tidy_info = true;
			pr_tidy("%s: removing %" PRIu32 " directories\n", args->name, chdir_dirs);
		}
	}
	(void)stress_temp_dir_rm_args(args);

	rate = (duration > 0.0) ? count / duration : 0.0;
	stress_metrics_set(args, 0, "chdir calls per sec",
		rate, STRESS_METRIC_HARMONIC_MEAN);
err:
	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);
	free(chdir_info);

	return ret;
}

const stressor_info_t stress_chdir_info = {
	.stressor = stress_chdir,
	.classifier = CLASS_FILESYSTEM | CLASS_OS,
	.opts = opts,
	.verify = VERIFY_ALWAYS,
	.help = help
};
