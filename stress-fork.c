/*
 * Copyright (C) 2013-2021 Canonical, Ltd.
 * Copyright (C) 2022-2023 Colin Ian King.
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
#include "core-madvise.h"
#include "core-out-of-memory.h"
#include "core-pragma.h"
#include "core-thrash.h"

#define MIN_FORKS		(1)
#define MAX_FORKS		(16000)
#define DEFAULT_FORKS		(1)

#define MIN_VFORKS		(1)
#define MAX_VFORKS		(16000)
#define DEFAULT_VFORKS		(1)

static const stress_help_t fork_help[] = {
	{ "f N","fork N",	"start N workers spinning on fork() and exit()" },
	{ NULL,	"fork-max P",	"create P workers per iteration, default is 1" },
	{ NULL,	"fork-ops N",	"stop after N fork bogo operations" },
	{ NULL, "fork-vm",	"enable extra virtual memory pressure" },
	{ NULL,	NULL,		NULL }
};

static const stress_help_t vfork_help[] = {
	{ NULL,	"vfork N",	"start N workers spinning on vfork() and exit()" },
	{ NULL,	"vfork-ops N",	"stop after N vfork bogo operations" },
	{ NULL,	"vfork-max P",	"create P processes per iteration, default is 1" },
	{ NULL,	NULL,		NULL }
};

#define STRESS_FORK	(0)
#define STRESS_VFORK	(1)

/*
 *  stress_set_fork_max()
 *	set maximum number of forks allowed
 */
static int stress_set_fork_max(const char *opt)
{
	uint32_t fork_max;

	fork_max = stress_get_uint32(opt);
	stress_check_range("fork-max", (uint64_t)fork_max,
		MIN_FORKS, MAX_FORKS);
	return stress_set_setting("fork-max", TYPE_ID_UINT32, &fork_max);
}

/*
 *  stress_set_fork_vm()
 *	set fork-vm flag on
 */
static int stress_set_fork_vm(const char *opt)
{
	return stress_set_setting_true("fork-vm", opt);
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
	const int which,
	const uint32_t fork_max,
	const bool vm)
{
	static fork_info_t info[MAX_FORKS];

	stress_set_oom_adjustment(args, true);

	/* Explicitly drop capabilities, makes it more OOM-able */
	VOID_RET(int, stress_drop_capabilities(args->name));

	do {
		NOCLOBBER uint32_t i, n;
		NOCLOBBER char *fork_fn_name;

		(void)shim_memset(info, 0, sizeof(info));

		for (n = 0; n < fork_max; n++) {
			pid_t pid;
			const uint8_t rnd = stress_mwc8();

			switch (which) {
			case STRESS_FORK:
				fork_fn_name = "fork";
				pid = fork();
				break;
			case STRESS_VFORK:
				fork_fn_name = "vfork";
				pid = shim_vfork();
				if (pid == 0)
					_exit(0);
				break;
			default:
				/* This should not happen */
				fork_fn_name = "unknown";
				pid = -1;
				pr_fail("%s: bad fork/vfork function, aborting\n", args->name);
				errno = ENOSYS;
				break;
			}

			if (pid == 0) {
				/*
				 *  50% of forks are short lived exiting processes
				 */
				if (n & 1)
					goto fast_exit;

				/*
				 *  With new session and capabilities
				 *  dropped vhangup will always fail
				 *  but it's useful to exercise this
				 *  to get more kernel coverage
				 */
				if (setsid() != (pid_t) -1)
					shim_vhangup();
				if (vm) {
					int flags = 0;

					switch (rnd & 7) {
					case 0:
#if defined(MADV_MERGEABLE)
						flags |= MADV_MERGEABLE;
#endif
#if defined(MADV_WILLNEED)
						flags |= MADV_WILLNEED;
#endif
#if defined(MADV_HUGEPAGE)
						flags |= MADV_HUGEPAGE;
#endif
#if defined(MADV_RANDOM)
						flags |= MADV_RANDOM;
#endif
						break;
					case 1:
#if defined(MADV_COLD)
						flags |= MADV_COLD;
#endif
						break;
					case 2:
#if defined(MADV_PAGEOUT)
						flags |= MADV_PAGEOUT;
#endif
						break;
					case 3:
#if defined(MADV_WILLNEED)
						flags |= MADV_WILLNEED;
#endif
						break;
					case 4:
#if defined(MADV_NOHUGEPAGE)
						flags |= MADV_NOHUGEPAGE;
#endif
						break;
					case 5:
						stress_ksm_memory_merge(1);
						break;
					/* cases 6..7 */
					default:
						break;
					}
					if (flags) {
						stress_madvise_pid_all_pages(getpid(), flags);
						stress_pagein_self(args->name);
					}
				}

				/* exercise some setpgid calls before we die */
				VOID_RET(int, setpgid(0, 0));
#if defined(HAVE_GETPGID)
				{
					const pid_t my_pid = getpid();
					const pid_t my_pgid = getpgid(my_pid);

					VOID_RET(int, setpgid(my_pid, my_pgid));
				}
#else
				UNEXPECTED
#endif

				/* -ve pgid is EINVAL */
				VOID_RET(int, setpgid(0, -1));
				/* -ve pid is EINVAL */
				VOID_RET(int, setpgid(-1, 0));
fast_exit:
				(void)shim_sched_yield();
				stress_set_proc_state(args->name, STRESS_STATE_ZOMBIE);
				_exit(0);
			} else if (pid < 0) {
				info[n].err = errno;
			}
			info[n].pid  = pid;
			if (!stress_continue(args))
				break;
		}
		for (i = 0; i < n; i++) {
			if (info[i].pid > 0) {
				int status;
				/* Parent, kill and then wait for child */
				(void)shim_waitpid(info[i].pid, &status, 0);
				stress_bogo_inc(args);
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
		if ((which == STRESS_VFORK) && (stress_time_now() > args->time_end))
			break;
#endif
	} while (stress_continue(args));

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
	bool vm = false;

	(void)stress_get_setting("fork-vm", &vm);

	if (!stress_get_setting("fork-max", &fork_max)) {
		if (g_opt_flags & OPT_FLAGS_MAXIMIZE)
			fork_max = MAX_FORKS;
		if (g_opt_flags & OPT_FLAGS_MINIMIZE)
			fork_max = MIN_FORKS;
	}

	stress_set_proc_state(args->name, STRESS_STATE_RUN);
	rc = stress_fork_fn(args, STRESS_FORK, fork_max, vm);
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

	if (!stress_get_setting("vfork-max", &vfork_max)) {
		if (g_opt_flags & OPT_FLAGS_MAXIMIZE)
			vfork_max = MAX_VFORKS;
		if (g_opt_flags & OPT_FLAGS_MINIMIZE)
			vfork_max = MIN_VFORKS;
	}

	stress_set_proc_state(args->name, STRESS_STATE_RUN);
	rc = stress_fork_fn(args, STRESS_VFORK, vfork_max, false);
	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);

	return rc;
}
STRESS_PRAGMA_POP

static const stress_opt_set_func_t fork_opt_set_funcs[] = {
	{ OPT_fork_max,		stress_set_fork_max },
	{ OPT_fork_vm,		stress_set_fork_vm },
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
	.verify = VERIFY_OPTIONAL,
	.help = fork_help
};

stressor_info_t stress_vfork_info = {
	.stressor = stress_vfork,
	.class = CLASS_SCHEDULER | CLASS_OS,
	.opt_set_funcs = vfork_opt_set_funcs,
	.verify = VERIFY_OPTIONAL,
	.help = vfork_help
};
