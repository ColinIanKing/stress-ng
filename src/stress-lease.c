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

#if defined(F_SETLEASE) && defined(F_WRLCK) && defined(F_UNLCK)
static uint64_t lease_sigio;
#endif

static const stress_help_t help[] = {
	{ NULL,	"lease N",	    "start N workers holding and breaking a lease" },
	{ NULL,	"lease-ops N",	    "stop after N lease bogo operations" },
	{ NULL,	"lease-breakers N", "number of lease breaking workers to start" },
	{ NULL, NULL,		    NULL }
};

static int stress_set_lease_breakers(const char *opt)
{
	uint64_t lease_breakers;

	lease_breakers = stress_get_uint64(opt);
	stress_check_range("lease-breakers", lease_breakers,
		MIN_LEASE_BREAKERS, MAX_LEASE_BREAKERS);
	return stress_set_setting("lease-breakers", TYPE_ID_UINT64, &lease_breakers);
}

static const stress_opt_set_func_t opt_set_funcs[] = {
	{ OPT_lease_breakers,	stress_set_lease_breakers },
	{ 0,			NULL }
};

#if defined(F_SETLEASE) &&	\
    defined(F_WRLCK) &&		\
    defined(F_UNLCK)

/*
 *  stress_lease_handler()
 *	lease signal handler
 */
static void MLOCKED_TEXT stress_lease_handler(int signum)
{
	(void)signum;

	lease_sigio++;
}

/*
 *  stress_get_lease()
 *	exercise getting the lease on fd
 */
static int stress_get_lease(const int fd)
{
	return fcntl(fd, F_GETLEASE);
}

/*
 *  stress_lease_spawn()
 *	spawn a process
 */
static pid_t stress_lease_spawn(
	const stress_args_t *args,
	const char *filename)
{
	pid_t pid;
	int count = 0;

again:
	pid = fork();
	if (pid < 0) {
		if (keep_stressing_flag() &&
		    ((errno == EAGAIN) || (errno == ENOMEM)))
			goto again;
		return -1;
	}
	if (pid == 0) {
		(void)setpgid(0, g_pgrp);
		stress_parent_died_alarm();
		(void)sched_settings_apply(true);

		do {
			int fd;

			errno = 0;
			fd = open(filename, O_NONBLOCK | O_WRONLY, S_IRUSR | S_IWUSR);
			if (fd < 0) {
				if ((errno != EWOULDBLOCK) &&
                                    (errno != EACCES)) {
					pr_dbg("%s: open failed (child): errno=%d: (%s)\n",
						args->name, errno, strerror(errno));
					if (count++ > 3)
						break;
				}
				continue;
			}
			(void)stress_get_lease(fd);
			(void)close(fd);
		} while (keep_stressing(args));
		_exit(EXIT_SUCCESS);
	}
	(void)setpgid(pid, g_pgrp);
	return pid;
}

/*
 *  stress_try_lease()
 *	try and get a lease with lock type 'lock'
 */
static int stress_try_lease(
	const stress_args_t *args,
	const char *filename,
	const int flags,
	const int lock)
{
	int fd;

	fd = open(filename, flags, S_IRUSR | S_IWUSR);
	if (fd < 0) {
		int ret;

		ret = exit_status(errno);
		pr_err("%s: open failed (parent): errno=%d: (%s)\n",
			args->name, errno, strerror(errno));
		return ret;
	}

	/*
	 *  attempt a lease lock
	 */
	while (fcntl(fd, F_SETLEASE, lock) < 0) {
		if (!keep_stressing_flag())
			goto tidy;
	}
	(void)stress_get_lease(fd);

	inc_counter(args);
	(void)shim_sched_yield();

	/*
	 *  attempt a lease unlock
	 */
	while (fcntl(fd, F_SETLEASE, F_UNLCK) < 0) {
		if (!keep_stressing_flag())
			break;
		if (errno != EAGAIN) {
			pr_err("%s: fcntl failed: errno=%d: (%s)\n",
				args->name, errno, strerror(errno));
			break;
		}
	}
tidy:
	(void)close(fd);

	return EXIT_SUCCESS;
}

/*
 *  stress_lease
 *	stress fcntl lease activity
 */
static int stress_lease(const stress_args_t *args)
{
	char filename[PATH_MAX];
	int ret, fd, status;
	pid_t l_pids[MAX_LEASE_BREAKERS];
	uint64_t i, lease_breakers = DEFAULT_LEASE_BREAKERS;

	if (!stress_get_setting("lease-breakers", &lease_breakers)) {
		if (g_opt_flags & OPT_FLAGS_MAXIMIZE)
			lease_breakers = MAX_LEASE_BREAKERS;
		if (g_opt_flags & OPT_FLAGS_MINIMIZE)
			lease_breakers = MIN_LEASE_BREAKERS;
	}

	(void)memset(l_pids, 0, sizeof(l_pids));

	if (stress_sighandler(args->name, SIGIO, stress_lease_handler, NULL) < 0)
		return EXIT_FAILURE;

	ret = stress_temp_dir_mk_args(args);
	if (ret < 0)
		return exit_status(-ret);
	(void)stress_temp_filename_args(args,
		filename, sizeof(filename), stress_mwc32());

	fd = creat(filename, S_IRUSR | S_IWUSR);
	if (fd < 0) {
		ret = exit_status(errno);
		pr_err("%s: creat failed: errno=%d: (%s)\n",
			args->name, errno, strerror(errno));
		(void)stress_temp_dir_rm_args(args);
		return ret;
	}
	(void)close(fd);

	/*
	 *  start lease breaker child processes
	 */
	for (i = 0; i < lease_breakers; i++) {
		l_pids[i] = stress_lease_spawn(args, filename);
		if (l_pids[i] < 0) {
			pr_err("%s: failed to start all the lease breaker processes\n", args->name);
			goto reap;
		}
	}

	stress_set_proc_state(args->name, STRESS_STATE_RUN);

	do {
		ret = stress_try_lease(args, filename, O_WRONLY | O_APPEND, F_WRLCK);
		if (ret != EXIT_SUCCESS)
			break;
		ret = stress_try_lease(args, filename, O_RDONLY, F_RDLCK);
		if (ret != EXIT_SUCCESS)
			break;
	} while (keep_stressing(args));

reap:
	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);

	for (i = 0; i < lease_breakers; i++) {
		if (l_pids[i]) {
			(void)kill(l_pids[i], SIGKILL);
			(void)shim_waitpid(l_pids[i], &status, 0);
		}
	}

	(void)unlink(filename);
	(void)stress_temp_dir_rm_args(args);

	pr_dbg("%s: %" PRIu64 " lease sigio interrupts caught\n", args->name, lease_sigio);

	return ret;
}

stressor_info_t stress_lease_info = {
	.stressor = stress_lease,
	.class = CLASS_FILESYSTEM | CLASS_OS,
	.opt_set_funcs = opt_set_funcs,
	.help = help
};
#else
stressor_info_t stress_lease_info = {
	.stressor = stress_not_implemented,
	.class = CLASS_FILESYSTEM | CLASS_OS,
	.opt_set_funcs = opt_set_funcs,
	.help = help
};
#endif
