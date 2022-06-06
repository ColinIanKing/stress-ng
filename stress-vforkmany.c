/*
 * Copyright (C) 2017-2021 Canonical, Ltd.
 * Copyright (C)      2022 Colin Ian King.
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

#define WASTE_SIZE	(64 * MB)

static const stress_help_t help[] = {
	{ NULL,	"vforkmany N",     "start N workers spawning many vfork children" },
	{ NULL,	"vforkmany-ops N", "stop after spawning N vfork children" },
	{ NULL, "vforkmany-vm",	   "enable extra virtual memory pressure" },
	{ NULL,	NULL,		   NULL }
};

static int stress_set_vforkmany_vm(const char *opt)
{
	bool vm = true;

	(void)opt;

	return stress_set_setting("vforkmany-vm", TYPE_ID_BOOL, &vm);
}

/*
 *  vforkmany_wait()
 *	wait and then kill
 */
static void vforkmany_wait(const pid_t pid)
{
	for (;;) {
		int ret, status;

		errno = 0;
		ret = waitpid(pid, &status, 0);
		if ((ret >= 0) || (errno != EINTR))
			break;

		(void)kill(pid, SIGALRM);
	}
}

/*
 *  stress_vforkmany()
 *	stress by vfork'ing as many processes as possible.
 *	vfork has interesting semantics, the parent blocks
 *	until the child has exited, plus child processes
 *	share the same address space. So we need to be
 *	careful not to overwrite shared variables across
 *	all the processes.
 */
static int stress_vforkmany(const stress_args_t *args)
{
	static pid_t chpid;
	static uint8_t *stack_sig;
	static volatile bool *terminate;
	static bool *terminate_mmap;
	static bool vm = false;

	(void)stress_get_setting("vforkmany-vm", &vm);

	/* We should use an alternative signal stack */
	stack_sig = (uint8_t *)mmap(NULL, STRESS_SIGSTKSZ, PROT_READ | PROT_WRITE,
			MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
	if (stack_sig == MAP_FAILED) {
		pr_inf_skip("%s: skipping stressor, cannot allocate signal stack,"
			" errno=%d (%s)\n",
			args->name, errno, strerror(errno));
		return EXIT_NO_RESOURCE;
	}
	if (stress_sigaltstack(stack_sig, STRESS_SIGSTKSZ) < 0)
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

	stress_set_proc_state(args->name, STRESS_STATE_RUN);
fork_again:
	chpid = fork();
	if (chpid < 0) {
		if (stress_redo_fork(errno))
			goto fork_again;
		if (!keep_stressing(args))
			goto finish;
		pr_err("%s: fork failed: errno=%d: (%s)\n",
			args->name, errno, strerror(errno));
		(void)munmap((void *)terminate_mmap, args->page_size);
		return EXIT_FAILURE;
	} else if (chpid == 0) {
		static uint8_t *waste;
		static size_t waste_size = WASTE_SIZE;

		(void)setpgid(0, g_pgrp);

		/*
		 *  We want the children to be OOM'd if we
		 *  eat up too much memory
		 */
		stress_set_oom_adjustment(args->name, true);
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

			if (!keep_stressing_flag())
				_exit(0);

			waste_size >>= 1;
		} while (waste_size > 4096);

		if (waste != MAP_FAILED)
			(void)stress_mincore_touch_pages_interruptible(waste, WASTE_SIZE);
		if (!keep_stressing_flag())
			_exit(0);

		if (vm) {
			int flags = 0;

#if defined(MADV_NORMAL)
			flags |= MADV_NORMAL;
#endif
#if defined(MADV_HUGEPAGE)
			flags |= MADV_HUGEPAGE;
#endif
#if defined(MADV_SEQUENTIAL)
			flags |= MADV_SEQUENTIAL;
#endif
			if (flags)
				stress_madvise_pid_all_pages(getpid(), flags);
		}

		do {
			/*
			 *  Force pid to be a register, if it's
			 *  stashed on the stack or as a global
			 *  then waitpid will pick up the one
			 *  shared by all the vfork children
			 *  which is problematic on the wait
			 *  This is a dirty hack.
			 */
			register pid_t pid;
			static volatile pid_t start_pid = -1;

vfork_again:
			/*
			 * SIGALRM is not inherited over vfork so
			 * instead poll the run time and break out
			 * of the loop if we've run out of run time
			 */
			if (*terminate) {
				keep_stressing_set_flag(false);
				break;
			}
			if (start_pid == -1) {
				pid = fork();
				if (pid >= 0)
					start_pid = getpid();
			} else {
				pid = shim_vfork();
				inc_counter(args);
			}
			if (pid < 0) {
				/* failed */
				shim_sched_yield();
				_exit(0);
			} else if (pid == 0) {
				if (vm) {
					int flags = 0;

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
					if (flags)
						stress_madvise_pid_all_pages(getpid(), flags);
				}
				if (waste != MAP_FAILED)
					(void)stress_mincore_touch_pages_interruptible(waste, WASTE_SIZE);

				/* child, parent is blocked, spawn new child */
				if (!args->max_ops || (get_counter(args) < args->max_ops))
					goto vfork_again;
				_exit(0);
			} else {
				/* parent, wait for child, and exit if not first parent */
				if (pid >= 1)
					(void)vforkmany_wait(pid);
				shim_sched_yield();
				if (getpid() != start_pid)
					_exit(0);
			}
		} while (keep_stressing(args));

		if (waste != MAP_FAILED)
			(void)munmap((void *)waste, WASTE_SIZE);

		_exit(0);
	} else {
		/*
		 * Parent sleeps until timeout/SIGALRM and then
		 * flags terminate state that the vfork children
		 * see and will then exit.  We wait for the first
		 * one spawned to unblock and exit
		 */
		(void)setpgid(chpid, g_pgrp);
		g_opt_flags &= ~OPT_FLAGS_OOMABLE;
		stress_set_oom_adjustment(args->name, false);

		(void)sleep((unsigned int)g_opt_timeout);
		*terminate = true;

		for (;;) {
			int ret, chstatus;

			(void)kill(chpid, SIGALRM);
			errno = 0;
			ret = waitpid(chpid, &chstatus, 0);

			/* Reaped? - all done */
			if (ret >= 0)
				break;
			/* Interrupted? - retry */
			if (errno == EINTR)
				continue;
			/* Something went wrong, kill */
			(void)stress_killpid(chpid);
		}
	}
finish:
	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);

	(void)munmap((void *)terminate_mmap, args->page_size);
	(void)munmap((void *)stack_sig, STRESS_SIGSTKSZ);
	return EXIT_SUCCESS;
}

static const stress_opt_set_func_t opt_set_funcs[] = {
	{ OPT_vforkmany_vm,	stress_set_vforkmany_vm },
	{ 0,			NULL }
};

stressor_info_t stress_vforkmany_info = {
	.stressor = stress_vforkmany,
	.class = CLASS_SCHEDULER | CLASS_OS,
	.opt_set_funcs = opt_set_funcs,
	.help = help
};
