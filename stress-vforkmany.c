/*
 * Copyright (C) 2017-2019 Canonical, Ltd.
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
#define WASTE_SIZE	(64 * MB)

static const help_t help[] = {
	{ NULL,	"vforkmany N",     "start N workers spawning many vfork children" },
	{ NULL,	"vforkmany-ops N", "stop after spawning N vfork children" },
	{ NULL,	NULL,		   NULL }
};

PRAGMA_PUSH
PRAGMA_WARN_OFF
static inline pid_t stress_shim_vfork(void)
{
#if defined(__NR_vfork)
	return (pid_t)syscall(__NR_vfork);
#else
	return vfork();
#endif
}
PRAGMA_POP

/*
 *  stress_vforkmany()
 *	stress by vfork'ing as many processes as possible.
 *	vfork has interesting semantics, the parent blocks
 *	until the child has exited, plus child processes
 *	share the same address space. So we need to be
 *	careful not to overrite shared variables across
 *	all the processes.
 */
static int stress_vforkmany(const args_t *args)
{
	static int status;
	static pid_t chpid;
	static volatile int instance = 0;
	static uint8_t stack_sig[SIGSTKSZ + SIGSTKSZ];
	static volatile bool *terminate;
	static bool *terminate_mmap;

	/* We should use an alterative signal stack */
	(void)memset(stack_sig, 0, sizeof(stack_sig));
	if (stress_sigaltstack(stack_sig, SIGSTKSZ) < 0)
		return EXIT_FAILURE;

	terminate = terminate_mmap =
		(bool *)mmap(NULL, args->page_size,
				PROT_READ | PROT_WRITE,
				MAP_SHARED | MAP_ANONYMOUS, -1, 0);
	if (terminate_mmap == MAP_FAILED) {
		pr_inf("%s: mmap failed: %d (%s)\n",
			args->name, errno, strerror(errno));
		return EXIT_NO_RESOURCE;
	}
	*terminate = false;

fork_again:
	if (!g_keep_stressing_flag)
		goto tidy;
	chpid = fork();
	if (chpid < 0) {
		if (errno == EAGAIN)
			goto fork_again;
		pr_err("%s: fork failed: errno=%d: (%s)\n",
			args->name, errno, strerror(errno));
		munmap((void *)terminate_mmap, args->page_size);
		return EXIT_FAILURE;
	} else if (chpid == 0) {
		static uint8_t *waste;
		static size_t waste_size = WASTE_SIZE;

		(void)setpgid(0, g_pgrp);

		/*
		 *  We want the children to be OOM'd if we
		 *  eat up too much memory
		 */
		set_oom_adjustment(args->name, true);
		stress_parent_died_alarm();

		/*
		 *  Allocate some wasted space so this child
		 *  scores more on the OOMable score than the
		 *  parent waiter so in theory it should be
		 *  OOM'd before the parent.
		 */
		do {
			waste = (uint8_t *)mmap(NULL, waste_size, PROT_READ | PROT_WRITE,
					MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
			if (waste != MAP_FAILED)
				break;

			if (!g_keep_stressing_flag)
				_exit(0);

			waste_size >>= 1;
		} while (waste_size > 4096);

		if (waste != MAP_FAILED)
			(void)mincore_touch_pages_interruptible(waste, WASTE_SIZE);
		do {
			/*
			 *  Force pid to be a register, if it's
			 *  stashed on the stack or as a global
			 *  then waitpid will pick up the one
			 *  shared by all the vfork children
			 *  which is problematic on the wait
			 */
			register pid_t pid;
			register bool first = (instance == 0);

vfork_again:
			/*
			 * SIGALRM is not inherited over vfork so
			 * instead poll the run time and break out
			 * of the loop if we've run out of run time
			 */
			if (*terminate) {
				g_keep_stressing_flag = false;
				break;
			}
			inc_counter(args);
			instance++;
			if (first) {
				pid = fork();
			} else {
				pid = stress_shim_vfork();
			}

			if (pid < 0) {
				/* failed, only exit of not the top parent */
				if (!first)
					_exit(0);
			} else if (pid == 0) {
				if (waste != MAP_FAILED)
					(void)mincore_touch_pages_interruptible(waste, WASTE_SIZE);

				/* child, parent is blocked, spawn new child */
				if (!args->max_ops || get_counter(args) < args->max_ops)
					goto vfork_again;
				_exit(0);
			}
			/* parent, wait for child, and exit if not top parent */
			(void)shim_waitpid(pid, &status, 0);
			if (!first)
				_exit(0);
		} while (keep_stressing());

		if (waste != MAP_FAILED)
			munmap((void *)waste, WASTE_SIZE);
		_exit(0);
	} else {
		/*
		 * Parent sleeps until timeout/SIGALRM and then
		 * flags terminate state that the vfork children
		 * see and will then exit.  We wait for the first
		 * one spawned to unblock and exit
		 */
		int chstatus;

		(void)setpgid(chpid, g_pgrp);
		g_opt_flags &= ~OPT_FLAGS_OOMABLE;
		set_oom_adjustment(args->name, false);

		(void)sleep(g_opt_timeout);
		*terminate = true;
		(void)kill(chpid, SIGALRM);

		(void)shim_waitpid(chpid, &chstatus, 0);
	}
tidy:
	(void)munmap((void *)terminate_mmap, args->page_size);
	return EXIT_SUCCESS;
}

stressor_info_t stress_vforkmany_info = {
	.stressor = stress_vforkmany,
	.class = CLASS_SCHEDULER | CLASS_OS,
	.help = help
};
