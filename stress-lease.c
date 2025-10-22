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
#include "core-killpid.h"

#define MIN_LEASE_BREAKERS	(1)
#define MAX_LEASE_BREAKERS	(64)
#define DEFAULT_LEASE_BREAKERS	(1)

#if defined(F_SETLEASE) && 	\
    defined(F_WRLCK) &&		\
    defined(F_UNLCK)
static uint64_t lease_sigio;
#endif

static const stress_help_t help[] = {
	{ NULL,	"lease N",	    "start N workers holding and breaking a lease" },
	{ NULL,	"lease-breakers N", "number of lease breaking workers to start" },
	{ NULL,	"lease-ops N",	    "stop after N lease bogo operations" },
	{ NULL, NULL,		    NULL }
};

static const stress_opt_t opts[] = {
	{ OPT_lease_breakers, "lease-breakers", TYPE_ID_UINT64, MIN_LEASE_BREAKERS, MAX_LEASE_BREAKERS, NULL },
	END_OPT,
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
	stress_args_t *args,
	const char *filename)
{
	pid_t pid;
	int count = 0;

again:
	pid = fork();
	if (pid < 0) {
		if (stress_redo_fork(args, errno))
			goto again;
		return -1;
	}
	if (pid == 0) {
		stress_set_proc_state(args->name, STRESS_STATE_RUN);
		stress_parent_died_alarm();
		(void)sched_settings_apply(true);

		do {
			int fd;

			errno = 0;
			fd = open(filename, O_NONBLOCK | O_WRONLY);
			if (fd < 0) {
				if ((errno != EWOULDBLOCK) &&
                                    (errno != EACCES)) {
					pr_dbg("%s: open failed (child), errno=%d: (%s)\n",
						args->name, errno, strerror(errno));
					if (count++ > 3)
						break;
				}
				continue;
			}
			(void)stress_get_lease(fd);
			(void)close(fd);
		} while (stress_continue(args));

		stress_set_proc_state(args->name, STRESS_STATE_WAIT);
		_exit(EXIT_SUCCESS);
	}
	return pid;
}

/*
 *  stress_try_lease()
 *	try and get a lease with lock type 'lock'
 */
static int stress_try_lease(
	stress_args_t *args,
	const char *filename,
	const int flags,
	const int lock)
{
	int fd, rc = EXIT_SUCCESS;

	fd = open(filename, flags);
	if (fd < 0) {
		int ret;

		ret = stress_exit_status(errno);
		pr_err("%s: open failed (parent), errno=%d: (%s)\n",
			args->name, errno, strerror(errno));
		return ret;
	}

	/*
	 *  attempt a lease lock
	 */
	while (fcntl(fd, F_SETLEASE, lock) < 0) {
		if (UNLIKELY(!stress_continue_flag()))
			goto tidy;
	}
	(void)stress_get_lease(fd);

	stress_bogo_inc(args);
	(void)shim_sched_yield();

	/*
	 *  attempt a lease unlock
	 */
	while (fcntl(fd, F_SETLEASE, F_UNLCK) < 0) {
		if (UNLIKELY(!stress_continue_flag()))
			break;
		if (errno != EAGAIN) {
			pr_fail("%s: fcntl failed, errno=%d: (%s)%s\n",
				args->name, errno, strerror(errno),
				stress_get_fs_type(filename));
			rc = EXIT_FAILURE;
			break;
		}
	}
tidy:
	(void)close(fd);

	return rc;
}

/*
 *  stress_lease
 *	stress fcntl lease activity
 */
static int stress_lease(stress_args_t *args)
{
	char filename[PATH_MAX];
	int ret, fd;
	pid_t l_pids[MAX_LEASE_BREAKERS];
	uint64_t i, lease_breakers = DEFAULT_LEASE_BREAKERS;
	double t1 = 0.0, t2 = 0.0, dt;

	if (!stress_get_setting("lease-breakers", &lease_breakers)) {
		if (g_opt_flags & OPT_FLAGS_MAXIMIZE)
			lease_breakers = MAX_LEASE_BREAKERS;
		if (g_opt_flags & OPT_FLAGS_MINIMIZE)
			lease_breakers = MIN_LEASE_BREAKERS;
	}

	(void)shim_memset(l_pids, 0, sizeof(l_pids));

	if (stress_sighandler(args->name, SIGIO, stress_lease_handler, NULL) < 0)
		return EXIT_FAILURE;

	ret = stress_temp_dir_mk_args(args);
	if (ret < 0)
		return stress_exit_status(-ret);
	(void)stress_temp_filename_args(args,
		filename, sizeof(filename), stress_mwc32());

	fd = creat(filename, S_IRUSR | S_IWUSR);
	if (fd < 0) {
		ret = stress_exit_status(errno);
		pr_err("%s: creat failed, errno=%d: (%s)\n",
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

	stress_set_proc_state(args->name, STRESS_STATE_SYNC_WAIT);
	stress_sync_start_wait(args);
	stress_set_proc_state(args->name, STRESS_STATE_RUN);

	t1 = stress_time_now();
	do {
		ret = stress_try_lease(args, filename, O_WRONLY | O_APPEND, F_WRLCK);
		if (ret != EXIT_SUCCESS)
			break;
		ret = stress_try_lease(args, filename, O_RDONLY, F_RDLCK);
		if (ret != EXIT_SUCCESS)
			break;
	} while (stress_continue(args));
	t2 = stress_time_now();

reap:
	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);

	for (i = 0; i < lease_breakers; i++) {
		if (l_pids[i])
			(void)stress_kill_pid_wait(l_pids[i], NULL);
	}

	(void)shim_unlink(filename);
	(void)stress_temp_dir_rm_args(args);

	pr_dbg("%s: %" PRIu64 " lease sigio interrupts caught\n", args->name, lease_sigio);
	dt = t2 - t1;
	if (dt > 0.0) {
		stress_metrics_set(args, 0, "lease sigio interrupts per sec",
			(double)lease_sigio / dt, STRESS_METRIC_HARMONIC_MEAN);
	}

	return ret;
}

const stressor_info_t stress_lease_info = {
	.stressor = stress_lease,
	.classifier = CLASS_FILESYSTEM | CLASS_OS,
	.opts = opts,
	.verify = VERIFY_ALWAYS,
	.help = help
};
#else
const stressor_info_t stress_lease_info = {
	.stressor = stress_unimplemented,
	.classifier = CLASS_FILESYSTEM | CLASS_OS,
	.opts = opts,
	.verify = VERIFY_ALWAYS,
	.help = help,
	.unimplemented_reason = "built without fcntl() F_SETLEASE, F_WRLCK or F_UNLCK commands"
};
#endif
