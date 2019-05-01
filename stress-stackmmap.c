/*
 * Copyright (C) 2013-2019 Canonical, Ltd.
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

static const help_t help[] = {
	{ NULL,	"stackmmap N",	   "start N workers exercising a filebacked stack" },
	{ NULL,	"stackmmap-ops N", "stop after N bogo stackmmap operations" },
	{ NULL,	NULL,		   NULL }
};

#if defined(HAVE_SWAPCONTEXT) && 	\
    defined(HAVE_UCONTEXT_H)

#define MMAPSTACK_SIZE		(256 * KB)

static ucontext_t c_main, c_test;
static void *stack_mmap;			/* mmap'd stack */
static uintptr_t page_mask;
static size_t page_size;

/*
 *  Just terminate the child when SEGV occurs on the
 *  mmap'd stack.
 */
static void MLOCKED_TEXT stress_segvhandler(int signum)
{
	(void)signum;

	_exit(0);
}

/*
 *  push values onto file backed mmap'd stack and
 *  force msync on the map'd region if page boundary
 *  has changed
 */
static void stress_stackmmap_push_msync(void)
{
	void *addr = (void *)(((uintptr_t)&addr) & page_mask);
	static void *laddr;
	char waste[64];

	waste[0] = 0;
	waste[sizeof(waste) - 1] = 0;

	if (addr != laddr) {
		(void)shim_msync(addr, page_size,
			(mwc8() & 1) ? MS_ASYNC : MS_SYNC);
		laddr = addr;
	}
	if (g_keep_stressing_flag)
		stress_stackmmap_push_msync();
}

/*
 *  start the push here
 */
static void stress_stackmmap_push_start(void)
{
	/* stack for SEGV handler must not be on the stack */
	static uint8_t stack_sig[SIGSTKSZ + STACK_ALIGNMENT];
	struct sigaction new_action;

	/*
	 *  We need to handle SEGV signals when we
	 *  hit the end of the mmap'd stack; however
	 *  an alternative signal handling stack
	 *  is required because we ran out of stack
	 */
	(void)memset(&new_action, 0, sizeof new_action);
	new_action.sa_handler = stress_segvhandler;
	(void)sigemptyset(&new_action.sa_mask);
	new_action.sa_flags = SA_ONSTACK;
	if (sigaction(SIGSEGV, &new_action, NULL) < 0)
		return;

	/*
	 *  We need an alternative signal stack
	 *  to handle segfaults on an overrun
	 *  mmap'd stack
	 */
	(void)memset(stack_sig, 0, sizeof(stack_sig));
	if (stress_sigaltstack(stack_sig, SIGSTKSZ) < 0)
		return;

	stress_stackmmap_push_msync();
}

/*
 *  stress_stackmmap
 *	stress a file memory map'd stack
 */
static int stress_stackmmap(const args_t *args)
{
	int fd;
	volatile int rc = EXIT_FAILURE;		/* could be clobbered */
	char filename[PATH_MAX];

	page_size = args->page_size;
	page_mask = ~(page_size - 1);

	/* Create file back'd mmaping for the stack */
	if (stress_temp_dir_mk_args(args) < 0)
		return EXIT_FAILURE;
	(void)stress_temp_filename_args(args,
		filename, sizeof(filename), mwc32());

	fd = open(filename, O_SYNC | O_RDWR | O_CREAT, S_IRUSR | S_IWUSR);
	if (fd < 0) {
		pr_fail_err("mmap'd stack file open");
		goto tidy_dir;
	}
	(void)unlink(filename);
	if (ftruncate(fd, MMAPSTACK_SIZE) < 0) {
		pr_fail_err("ftruncate");
		(void)close(fd);
		goto tidy_dir;
	}
	stack_mmap = mmap(NULL, MMAPSTACK_SIZE, PROT_READ | PROT_WRITE,
		MAP_SHARED, fd, 0);
	if (stack_mmap == MAP_FAILED) {
		if (errno == ENXIO) {
			pr_inf("%s: skipping stressor, mmap not possible on file %s\n",
				args->name, filename);
			rc = EXIT_NO_RESOURCE;
			(void)close(fd);
			goto tidy_dir;
		}
		pr_fail_err("mmap");
		(void)close(fd);
		goto tidy_dir;
	}
	(void)close(fd);

	if (shim_madvise(stack_mmap, MMAPSTACK_SIZE, MADV_RANDOM) < 0) {
		pr_dbg("%s: madvise failed: errno=%d (%s)\n",
			args->name, errno, strerror(errno));
	}
	(void)memset(stack_mmap, 0, MMAPSTACK_SIZE);
	(void)memset(&c_test, 0, sizeof(c_test));
	if (getcontext(&c_test) < 0) {
		pr_fail_err("getcontext");
		goto tidy_mmap;
	}
	c_test.uc_stack.ss_sp = stack_mmap;
	c_test.uc_stack.ss_size = MMAPSTACK_SIZE;
	c_test.uc_link = &c_main;

	/*
	 *  set jmp handler to jmp back into the loop on a full
	 *  stack segfault.  Use swapcontext to jump into a
	 *  new context using the new mmap'd stack
	 */
	do {
		pid_t pid;
again:
		if (!g_keep_stressing_flag)
			break;
		pid = fork();
		if (pid < 0) {
			if ((errno == EAGAIN) || (errno == ENOMEM))
				goto again;
			pr_err("%s: fork failed: errno=%d (%s)\n",
				args->name, errno, strerror(errno));
		} else if (pid > 0) {
			int status, waitret;

			/* Parent, wait for child */
			(void)setpgid(pid, g_pgrp);
			waitret = shim_waitpid(pid, &status, 0);
			if (waitret < 0) {
				if (errno != EINTR)
					pr_dbg("%s: waitpid(): errno=%d (%s)\n",
						args->name, errno, strerror(errno));
				(void)kill(pid, SIGTERM);
				(void)kill(pid, SIGKILL);
				(void)shim_waitpid(pid, &status, 0);
			}
		} else if (pid == 0) {
			/* Child */

			(void)setpgid(0, g_pgrp);
			stress_parent_died_alarm();

			/* Make sure this is killable by OOM killer */
			set_oom_adjustment(args->name, true);

			(void)makecontext(&c_test, stress_stackmmap_push_start, 0);
			(void)swapcontext(&c_main, &c_test);

			_exit(0);
		}
		inc_counter(args);
	} while (keep_stressing());

	rc = EXIT_SUCCESS;

tidy_mmap:
	(void)munmap(stack_mmap, MMAPSTACK_SIZE);
tidy_dir:
	(void)stress_temp_dir_rm_args(args);
	return rc;
}

stressor_info_t stress_stackmmap_info = {
	.stressor = stress_stackmmap,
	.class = CLASS_VM | CLASS_MEMORY,
	.help = help
};
#else
stressor_info_t stress_stackmmap_info = {
	.stressor = stress_not_implemented,
	.class = CLASS_VM | CLASS_MEMORY,
	.help = help
};
#endif
