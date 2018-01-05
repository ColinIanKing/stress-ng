/*
 * Copyright (C) 2013-2018 Canonical, Ltd.
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

/*
 *  stress_exec_supported()
 *      check that we don't run this as root
 */
int stress_exec_supported(void)
{
	/*
	 *  Don't want to run this when running as root as
	 *  this could allow somebody to try and run another
	 *  executable as root.
	 */
        if (geteuid() == 0) {
		pr_inf("exec stressor must not run as root, skipping the stressor\n");
                return -1;
        }
        return 0;
}

/*
 *  stress_set_exec_max()
 *	set maximum number of forks allowed
 */
void stress_set_exec_max(const char *opt)
{
	uint64_t exec_max;

	exec_max = get_uint64(opt);
	check_range("exec-max", exec_max,
		MIN_EXECS, MAX_EXECS);
	set_setting("exec-max", TYPE_ID_INT64, &exec_max);
}

#if defined(__linux__)

/*
 *  stress_exec()
 *	stress by forking and exec'ing
 */
int stress_exec(const args_t *args)
{
	pid_t pids[MAX_FORKS];
	char path[PATH_MAX + 1];
	ssize_t len;
	uint64_t exec_fails = 0, exec_calls = 0;
	uint64_t exec_max = DEFAULT_EXECS;
	char *argv_new[] = { NULL, "--exec-exit", NULL };
	char *env_new[] = { NULL };

	(void)get_setting("exec-max", &exec_max);

	/*
	 *  Determine our own self as the executable, e.g. run stress-ng
	 */
	len = readlink("/proc/self/exe", path, sizeof(path));
	if (len < 0 || len > PATH_MAX) {
		pr_fail("%s: readlink on /proc/self/exe failed\n", args->name);
		return EXIT_FAILURE;
	}
	path[len] = '\0';
	argv_new[0] = path;

	do {
		unsigned int i;

		(void)memset(pids, 0, sizeof(pids));

		for (i = 0; i < exec_max; i++) {
			pids[i] = fork();

			if (pids[i] == 0) {
				int ret, fd_out, fd_in, rc;

				(void)setpgid(0, g_pgrp);
				stress_parent_died_alarm();

				if ((fd_out = open("/dev/null", O_WRONLY)) < 0) {
					pr_fail("%s: child open on "
						"/dev/null failed\n", args->name);
					_exit(EXIT_FAILURE);
				}
				if ((fd_in = open("/dev/zero", O_RDONLY)) < 0) {
					pr_fail("%s: child open on "
						"/dev/zero failed\n", args->name);
					(void)close(fd_out);
					_exit(EXIT_FAILURE);
				}
				(void)dup2(fd_out, STDOUT_FILENO);
				(void)dup2(fd_out, STDERR_FILENO);
				(void)dup2(fd_in, STDIN_FILENO);
				(void)close(fd_out);
				(void)close(fd_in);

				ret = execve(path, argv_new, env_new);
				rc = EXIT_SUCCESS;
				if (ret < 0) {
					switch (errno) {
					case ENOMEM:
						CASE_FALLTHROUGH;
					case EMFILE:
						rc = EXIT_NO_RESOURCE;
						break;
					case EAGAIN:
						/* Ignore as an error */
						rc = EXIT_SUCCESS;
						break;
					default:
						rc = EXIT_FAILURE;
						break;
					}
				}

				/* Child, immediately exit */
				_exit(rc);
			}
			if (pids[i] > -1)
				(void)setpgid(pids[i], g_pgrp);
			if (!g_keep_stressing_flag)
				break;
		}
		for (i = 0; i < exec_max; i++) {
			if (pids[i] > 0) {
				int status;
				/* Parent, wait for child */
				(void)waitpid(pids[i], &status, 0);
				exec_calls++;
				inc_counter(args);
				if (WEXITSTATUS(status) != EXIT_SUCCESS)
					exec_fails++;
			}
		}

		for (i = 0; i < exec_max; i++) {
			if ((pids[i] < 0) && (g_opt_flags & OPT_FLAGS_VERIFY)) {
				pr_fail("%s: fork failed\n", args->name);
			}
		}
	} while (keep_stressing());

	if ((exec_fails > 0) && (g_opt_flags & OPT_FLAGS_VERIFY)) {
		pr_fail("%s: %" PRIu64 " execs failed (%.2f%%)\n",
			args->name, exec_fails,
			(double)exec_fails * 100.0 / (double)(exec_calls));
	}

	return EXIT_SUCCESS;
}
#else
int stress_exec(const args_t *args)
{
	return stress_not_implemented(args);
}
#endif
