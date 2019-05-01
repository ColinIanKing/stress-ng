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
	{ NULL,	"flock N",	"start N workers locking a single file" },
	{ NULL,	"flock-ops N",	"stop after N flock bogo operations" },
	{ NULL,	NULL,		NULL }
};

#if defined(HAVE_FLOCK) &&	\
    defined(LOCK_EX) &&		\
    defined(LOCK_UN)

#define MAX_FLOCK_STRESSORS	(3)

static void stress_flock_child(const args_t *args, const int fd)
{
	bool cont;

	for (;;) {
		if (flock(fd, LOCK_EX) == 0) {
			cont = keep_stressing();
			if (cont)
				inc_counter(args);
			(void)flock(fd, LOCK_UN);
			if (!cont)
				break;
		}

#if defined(LOCK_NB)
		if (flock(fd, LOCK_EX | LOCK_NB) == 0) {
			cont = keep_stressing();
			if (cont)
				inc_counter(args);
			(void)flock(fd, LOCK_UN);
			if (!cont)
				break;
		}
#endif

#if defined(LOCK_SH)
		if (!keep_stressing())
			break;
		if (flock(fd, LOCK_SH) == 0) {
			cont = keep_stressing();
			if (cont)
				inc_counter(args);
			(void)flock(fd, LOCK_UN);
			if (!cont)
				break;
		}
#endif

#if defined(LOCK_SH) && defined(LOCK_NB)
		if (!keep_stressing())
			break;
		if (flock(fd, LOCK_SH | LOCK_NB) == 0) {
			cont = keep_stressing();
			if (cont)
				inc_counter(args);
			(void)flock(fd, LOCK_UN);
			if (!cont)
				break;
		}
#endif
	}
}

/*
 *  stress_flock
 *	stress file locking
 */
static int stress_flock(const args_t *args)
{
	int fd, ret, rc = EXIT_FAILURE;
	size_t i;
	pid_t pids[MAX_FLOCK_STRESSORS];
	char filename[PATH_MAX];

	ret = stress_temp_dir_mk_args(args);
	if (ret < 0)
		return exit_status(-ret);

	(void)stress_temp_filename_args(args,
		filename, sizeof(filename), mwc32());
	fd = open(filename, O_CREAT | O_RDWR, S_IRUSR | S_IWUSR);
	if (fd < 0) {
		pr_err("%s: failed to create %s: errno=%d (%s)\n",
			args->name, filename, errno, strerror(errno));
		goto err;
	}

	(void)memset(pids, 0, sizeof(pids));
	for (i = 0; i < MAX_FLOCK_STRESSORS; i++) {
		pids[i] = fork();
		if (pids[i] < 0) {
			goto reap;
		} else if (pids[i] == 0) {
			(void)setpgid(0, g_pgrp);
			stress_parent_died_alarm();

			stress_flock_child(args, fd);
			_exit(EXIT_SUCCESS);
		}
	}

	stress_flock_child(args, fd);
	rc = EXIT_SUCCESS;
reap:
	(void)close(fd);

	for (i = 0; i < MAX_FLOCK_STRESSORS; i++) {
		if (pids[i] > 0) {
			int status;

			(void)kill(pids[i], SIGKILL);
			(void)shim_waitpid(pids[i], &status, 0);
		}
	}

	(void)unlink(filename);
err:
	(void)stress_temp_dir_rm_args(args);

	return rc;
}

stressor_info_t stress_flock_info = {
	.stressor = stress_flock,
	.class = CLASS_FILESYSTEM | CLASS_OS,
	.help = help
};
#else
stressor_info_t stress_flock_info = {
	.stressor = stress_not_implemented,
	.class = CLASS_FILESYSTEM | CLASS_OS,
	.help = help
};
#endif
