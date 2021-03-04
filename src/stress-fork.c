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

static const stress_help_t fork_help[] = {
	{ "f N","fork N",	"start N workers spinning on fork() and exit()" },
	{ NULL,	"fork-ops N",	"stop after N fork bogo operations" },
	{ NULL,	"fork-max P",	"create P workers per iteration, default is 1" },
	{ NULL,	NULL,		NULL }
};

static const stress_help_t vfork_help[] = {
	{ NULL,	"vfork N",	"start N workers spinning on vfork() and exit()" },
	{ NULL,	"vfork-ops N",	"stop after N vfork bogo operations" },
	{ NULL,	"vfork-max P",	"create P processes per iteration, default is 1" },
	{ NULL,	NULL,		NULL }
};

/*
 *  stress_set_fork_max()
 *	set maximum number of forks allowed
 */
static int stress_set_fork_max(const char *opt)
{
	uint32_t fork_max;

	fork_max = stress_get_uint32(opt);
	stress_check_range("fork-max", fork_max,
		MIN_FORKS, MAX_FORKS);
	return stress_set_setting("fork-max", TYPE_ID_UINT32, &fork_max);
}

/*
 *  stress_set_vfork_max()
 *	set maximum number of vforks allowed
 */
static int stress_set_vfork_max(const char *opt)
{
	uint32_t vfork_max;

	vfork_max = stress_get_uint32(opt);
	stress_check_range("vfork-max", vfork_max,
		MIN_VFORKS, MAX_VFORKS);
	return stress_set_setting("vfork-max", TYPE_ID_UINT32, &vfork_max);
}

/*
 *  local_shim_fork()
 *	wrapper to fork. If native system call is available then
 *	use this 50% of the time and use the libc wrapped fork 50%
 *	of the time. Otherwise use the libc wrapper version 100%
 *	of the time. Linux implementations of libc fork map to
 *	using clone().
 */
static pid_t local_shim_fork(void)
{
#if defined(__NR_fork)
	static int which;

	return (++which & 1) ?
		syscall(__NR_fork) : fork();
#else
	return fork();
#endif
}

typedef struct {
	pid_t	pid;	/* Child PID */
	int	err;	/* Saved fork errno */
} fork_info_t;

/*
 *  stress_fork_fn()
 *	stress by forking and exiting using
 *	fork function fork_fn (fork or vfork)
 */
static int stress_fork_fn(
	const stress_args_t *args,
	pid_t (*fork_fn)(void),
	const char *fork_fn_name,
	const uint32_t fork_max)
{
	static fork_info_t info[MAX_FORKS];

	int ret;
#if defined(__APPLE__)
	double time_end = stress_time_now() + (double)g_opt_timeout;
#endif

	stress_set_oom_adjustment(args->name, true);

	/* Explicitly drop capabilities, makes it more OOM-able */
	ret = stress_drop_capabilities(args->name);
	(void)ret;

	do {
		NOCLOBBER uint32_t i, n;

		(void)memset(info, 0, sizeof(info));

		for (n = 0; n < fork_max; n++) {
			pid_t pid = fork_fn();

			if (pid == 0) {
				/*
				 *  With new session and capabilities
				 *  dropped vhangup will always fail
				 *  but it's useful to exercise this
				 *  to get more kernel coverage
				 */
				if (setsid() != (pid_t) -1)
					shim_vhangup();
				_exit(0);
			} else if (pid < 0) {
				info[n].err = errno;
			}
			if (pid > -1)
				(void)setpgid(pid, g_pgrp);
			info[n].pid  = pid;
			if (!keep_stressing(args))
				break;
		}
		for (i = 0; i < n; i++) {
			if (info[i].pid > 0) {
				int status;
				/* Parent, kill and then wait for child */
				(void)kill(info[i].pid, SIGKILL);
				(void)shim_waitpid(info[i].pid, &status, 0);
				inc_counter(args);
			}
		}

		for (i = 0; i < n; i++) {
			if ((info[i].pid < 0) && (g_opt_flags & OPT_FLAGS_VERIFY)) {
				switch (info[i].err) {
				case EAGAIN:
				case ENOMEM:
					break;
				default:
					pr_fail("%s: %s failed, errno=%d (%s)\n", args->name,
						fork_fn_name, info[i].err, strerror(info[i].err));
					break;
				}
			}
		}
#if defined(__APPLE__)
		/*
		 *  SIGALRMs don't get reliably delivered on OS X on
		 *  vfork so check the time in case SIGARLM was not
		 *  delivered.
		 */
		if ((fork_fn == vfork) && (stress_time_now() > time_end))
			break;
#endif
	} while (keep_stressing(args));

	return EXIT_SUCCESS;
}

/*
 *  stress_fork()
 *	stress by forking and exiting
 */
static int stress_fork(const stress_args_t *args)
{
	uint32_t fork_max = DEFAULT_FORKS;
	int rc;

	if (!stress_get_setting("fork-max", &fork_max)) {
		if (g_opt_flags & OPT_FLAGS_MAXIMIZE)
			fork_max = MAX_FORKS;
		if (g_opt_flags & OPT_FLAGS_MINIMIZE)
			fork_max = MIN_FORKS;
	}

	stress_set_proc_state(args->name, STRESS_STATE_RUN);

	rc = stress_fork_fn(args, local_shim_fork, "fork", fork_max);

	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);

	return rc;
}


/*
 *  stress_vfork()
 *	stress by vforking and exiting
 */
STRESS_PRAGMA_PUSH
STRESS_PRAGMA_WARN_OFF
static int stress_vfork(const stress_args_t *args)
{
	uint32_t vfork_max = DEFAULT_VFORKS;
	int rc;

	stress_set_proc_state(args->name, STRESS_STATE_RUN);

	if (!stress_get_setting("vfork-max", &vfork_max)) {
		if (g_opt_flags & OPT_FLAGS_MAXIMIZE)
			vfork_max = MAX_VFORKS;
		if (g_opt_flags & OPT_FLAGS_MINIMIZE)
			vfork_max = MIN_VFORKS;
	}

	rc = stress_fork_fn(args, vfork, "vfork", vfork_max);

	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);

	return rc;
}
STRESS_PRAGMA_POP

static const stress_opt_set_func_t fork_opt_set_funcs[] = {
	{ OPT_fork_max,		stress_set_fork_max },
	{ 0,			NULL }
};

static const stress_opt_set_func_t vfork_opt_set_funcs[] = {
	{ OPT_vfork_max,	stress_set_vfork_max },
	{ 0,			NULL }
};

stressor_info_t stress_fork_info = {
	.stressor = stress_fork,
	.class = CLASS_SCHEDULER | CLASS_OS,
	.opt_set_funcs = fork_opt_set_funcs,
	.help = fork_help
};

stressor_info_t stress_vfork_info = {
	.stressor = stress_vfork,
	.class = CLASS_SCHEDULER | CLASS_OS,
	.opt_set_funcs = vfork_opt_set_funcs,
	.help = vfork_help
};
