/*
 * Copyright (C) 2017 Canonical, Ltd.
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

#define STACK_SIZE      (16384)


/*
 *  stress_vforkmany()
 *	stress by vfork'ing as many processes as possible.
 *	vfork has interesting semantics, the parent blocks
 *	until the child has exited, plus child processes
 *	share the same address space. So we need to be
 *	careful not to overrite shared variables across
 *	all the processes.
 */
int stress_vforkmany(const args_t *args)
{
	static int status;
	static pid_t mypid;
	static double start;
	static uint8_t stack_sig[SIGSTKSZ + SIGSTKSZ];

	/* We should use an alterative signal stack */
	memset(stack_sig, 0, sizeof(stack_sig));
	if (stress_sigaltstack(stack_sig, SIGSTKSZ) < 0)
		return EXIT_FAILURE;

	start = time_now();
	mypid = getpid();
	(void)setpgid(0, g_pgrp);

	do {
		/*
		 *  Force pid to be a register, if it's
		 *  stashed on the stack or as a global
		 *  then waitpid will pick up the one
		 *  shared by all the vfork children
		 *  which is problematic on the wait
		 */
		register pid_t pid;
again:
		/*
		 * SIGALRM is not inherited over vfork so
		 * instead poll the run time and break out
		 * of the loop if we've run out of run time
		 */
		if ((time_now() - start) > (double)g_opt_timeout)
			g_keep_stressing_flag = false;

		inc_counter(args);
		if (getpid() == mypid)
			pid = fork();
		else
			pid = vfork();
		if (pid < 0) {
			/* failed, only exit of not the top parent */
			if (getpid() != mypid)
				_exit(0);
		} else if (pid == 0) {
			/* child, parent is blocked, spawn new child */
			if (!args->max_ops || *args->counter < args->max_ops)
				goto again;
			_exit(0);
		}
		/* parent, wait for child, and exit if not top parent */
		(void)waitpid(pid, &status, 0);
		if (getpid() != mypid)
			_exit(0);
	} while (keep_stressing());

	return EXIT_SUCCESS;
}
