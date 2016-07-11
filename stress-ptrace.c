/*
 * Copyright (C) 2013-2016 Canonical, Ltd.
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
#define _GNU_SOURCE

#include "stress-ng.h"

#if defined(STRESS_PTRACE)

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>

#include <sys/types.h>
#include <sys/ptrace.h>
#include <sys/wait.h>

/*
 *  main syscall ptrace loop
 */
static inline bool stress_syscall_wait(
	const char *name,
	const pid_t pid)
{
	while (opt_do_run) {
		int status;

		if (ptrace(PTRACE_SYSCALL, pid, 0, 0) < 0) {
			pr_fail_dbg(name, "ptrace");
			return true;
		}
		if (waitpid(pid, &status, 0) < 0) {
			if (errno != EINTR)
				pr_fail_dbg(name, "waitpid");
			return true;
		}

		if (WIFSTOPPED(status) &&
		    (WSTOPSIG(status) & 0x80))
			return false;
		if (WIFEXITED(status))
			return true;
	}
	return true;
}

/*
 *  stress_ptrace()
 *	stress ptracing
 */
int stress_ptrace(
	uint64_t *const counter,
	const uint32_t instance,
	const uint64_t max_ops,
	const char *name)
{
	pid_t pid;

	(void)instance;

	pid = fork();
	if (pid < 0) {
		pr_fail_dbg(name, "fork");
		return EXIT_FAILURE;
	} else if (pid == 0) {
		setpgid(0, pgrp);
		stress_parent_died_alarm();

		/*
		 * Child to be traced, we abort if we detect
		 * we are already being traced by someone else
		 * as this makes life way too complex
		 */
		if (ptrace(PTRACE_TRACEME) != 0) {
			pr_fail(stderr, "%s: ptrace child being traced "
				"already, aborting\n", name);
			_exit(0);
		}
		/* Wait for parent to start tracing me */
		kill(getpid(), SIGSTOP);

		/*
		 *  A simple mix of system calls
		 */
		while (opt_do_run) {
			(void)getppid();
			(void)getgid();
			(void)getegid();
			(void)getuid();
			(void)geteuid();
			(void)getpgrp();
			(void)time(NULL);
		}
		_exit(0);
	} else {
		/* Parent to do the tracing */
		int status;

		setpgid(pid, pgrp);

		if (waitpid(pid, &status, 0) < 0) {
			if (errno != EINTR) {
				pr_fail_dbg(name, "waitpid");
				return EXIT_FAILURE;
			}
			return EXIT_SUCCESS;
		}
		if (ptrace(PTRACE_SETOPTIONS, pid,
			0, PTRACE_O_TRACESYSGOOD) < 0) {
			pr_fail_dbg(name, "ptrace");
			return EXIT_FAILURE;
		}

		do {
			/*
			 *  We do two of the following per syscall,
			 *  one at the start, and one at the end to catch
			 *  the return.  In this stressor we don't really
			 *  care which is which, we just care about counting
			 *  them
			 */
			if (stress_syscall_wait(name, pid))
				break;
			(*counter)++;
		} while (opt_do_run && (!max_ops || *counter < max_ops));

		/* Terminate child */
		(void)kill(pid, SIGKILL);
		if (waitpid(pid, &status, 0) < 0)
			pr_fail_dbg(name, "waitpid");
	}
	return EXIT_SUCCESS;
}

#endif
