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

#if !defined(__gnu_hurd__) && !defined(__minix__)
static uint8_t stack_sig[SIGSTKSZ + SIGSTKSZ]; /* ensure we have a sig stack */
static stack_t ss;
#endif

/*
 *  stress_vforkmany()
 *	stress by vfork'ing as many processes as possible.
 *	vfork has interesting semantics, the parent blocks
 *	until the child has exited, plus child processes
 *	share the same address space. So we need to be
 *	careful not to overrite shared variables across
 *	all the processes.
 */
int stress_vforkmany(
	uint64_t *const counter,
	const uint32_t instance,
	const uint64_t max_ops,
	const char *name)
{
	static int status;
	static pid_t mypid;
	static double start;

	(void)instance;
	(void)name;

#if !defined(__gnu_hurd__) && !defined(__minix__)
	/*
         *  We should use an alterative stack, for
         *  Linux we probably should use SS_AUTODISARM
	 *  if it is available
         */
	memset(stack_sig, 0, sizeof(stack_sig));
	ss.ss_sp = (void *)align_address(stack_sig, STACK_ALIGNMENT);
	ss.ss_size = SIGSTKSZ;
#if defined SS_AUTODISARM
	ss.ss_flags = SS_AUTODISARM;
#else
	ss.ss_flags = 0;
#endif
        if (sigaltstack(&ss, NULL) < 0) {
                pr_fail_err(name, "sigaltstack");
                return EXIT_FAILURE;
        }
#endif

	start = time_now();
	mypid = getpid();
	(void)setpgid(0, pgrp);

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
		if ((time_now() - start) > (double)opt_timeout)
			opt_do_run = false;

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
			(void)setpgid(0, pgrp);
			(*counter)++;
			if (!max_ops || *counter < max_ops)
				goto again;
			_exit(0);
		}
		/* parent, wait for child, and exit if not top parent */
		(void)waitpid(pid, &status, 0);
		if (getpid() != mypid)
			_exit(0);
	} while (opt_do_run && (!max_ops || *counter < max_ops));

	return EXIT_SUCCESS;
}
