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

static const stress_help_t help[] = {
	{ NULL,	"sigchld N",	 "start N workers that handle SIGCHLD" },
	{ NULL,	"sigchld-ops N", "stop after N bogo SIGCHLD signals" },
	{ NULL,	NULL,		 NULL }
};

static uint64_t counter;
static uint64_t cld_exited;
static uint64_t cld_killed;
static uint64_t cld_stopped;
static uint64_t cld_continued;

static void MLOCKED_TEXT stress_sigchld_handler(
	int sig,
	siginfo_t *info,
	void *ucontext)
{
	(void)ucontext;

	if (sig != SIGCHLD)
		return;

	switch (info->si_code) {
	case CLD_EXITED:
		cld_exited++;
		break;
	case CLD_KILLED:
		cld_killed++;
		break;
	case CLD_STOPPED:
		cld_stopped++;
		break;
	case CLD_CONTINUED:
		cld_continued++;
		break;
	default:
		break;
	}
	counter++;
}

/*
 *  stress_sigchld
 *	stress by generating SIGCHLD signals on exiting
 *	child processes.
 */
static int stress_sigchld(const stress_args_t *args)
{
	struct sigaction sa;

	counter = 0;
	cld_exited = 0;
	cld_killed = 0;
	cld_stopped = 0;
	cld_continued = 0;

	(void)shim_memset(&sa, 0, sizeof(sa));
	sa.sa_sigaction = stress_sigchld_handler;
#if defined(SA_SIGINFO)
	sa.sa_flags = SA_SIGINFO;
#endif
	if (sigaction(SIGCHLD, &sa, NULL) < 0) {
		pr_err("%s: cannot install SIGCHLD handler, errno=%d (%s)\n",
			args->name, errno, strerror(errno));
		return EXIT_FAILURE;
	}

	stress_set_proc_state(args->name, STRESS_STATE_RUN);

	do {
		pid_t pid;

again:
		pid = fork();
		if (pid < 0) {
			if (stress_redo_fork(errno))
				goto again;
			if (!keep_stressing(args))
				goto finish;
			pr_err("%s: fork failed: %d (%s)\n",
				args->name, errno, strerror(errno));
			return EXIT_FAILURE;
		} else if (pid == 0) {
			/* Child immediately exits */
			_exit(EXIT_FAILURE);
		} else {
			/* Parent wait and reap for child */
			int wstatus, ret;

			ret = kill(pid, SIGSTOP);
			if (ret == 0) {
				VOID_RET(int, kill(pid, SIGCONT));
			}
			VOID_RET(int, kill(pid, SIGKILL));
			VOID_RET(int, waitpid(pid, &wstatus, 0));
		}
		set_counter(args, counter);
	} while (keep_stressing(args));

finish:
	pr_dbg("%s: exit: %" PRIu64 ", kill: %" PRIu64
		", stop: %" PRIu64 ", continue: %" PRIu64 "\n",
		args->name,
		cld_exited, cld_killed,
		cld_stopped, cld_continued);

	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);

	return EXIT_SUCCESS;
}

stressor_info_t stress_sigchld_info = {
	.stressor = stress_sigchld,
	.class = CLASS_INTERRUPT | CLASS_OS,
	.help = help
};
