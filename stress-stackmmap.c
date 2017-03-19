/*
 * Copyright (C) 2013-2017 Canonical, Ltd.
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

#if defined(__linux__)

#include <ucontext.h>

#define MMAPSTACK_SIZE		(256 * KB)

static ucontext_t c_main, c_test;
static sigjmp_buf jmp_env;
static void *stack_mmap;			/* mmap'd stack */
static uintptr_t page_mask;
static size_t page_size;

static void stress_segvhandler(int dummy)
{
	(void)dummy;

	siglongjmp(jmp_env, 1);
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
	int ret;

	ret = sigsetjmp(jmp_env, 1);

	/* If we hit a segfault on the stack then swap back context */
	if (!ret)
		stress_stackmmap_push_msync();

	swapcontext(&c_test, &c_main);
}

/*
 *  stress_stackmmap
 *	stress a file memory map'd stack
 */
int stress_stackmmap(const args_t *args)
{
	int fd;
	volatile int rc = EXIT_FAILURE;		/* could be clobbered */
	struct sigaction new_action;
	char filename[PATH_MAX];

	/* stack for SEGV handler must not be on the stack */
	static uint8_t stack_sig[SIGSTKSZ + STACK_ALIGNMENT]; 

	page_size = args->page_size;
	page_mask = ~(page_size - 1);

	/*
	 *  We need to handle SEGV signals when we
	 *  hit the end of the mmap'd stack; however
	 *  an alternative signal handling stack
	 *  is required because we ran out of stack
	 */
	memset(&new_action, 0, sizeof new_action);
	new_action.sa_handler = stress_segvhandler;
	sigemptyset(&new_action.sa_mask);
	new_action.sa_flags = SA_ONSTACK;
	if (sigaction(SIGSEGV, &new_action, NULL) < 0) {
		pr_fail_err("sigaction");
		return EXIT_FAILURE;
	}

	/*
	 *  We need an alternative signal stack
	 *  to handle segfaults on an overrun
	 *  mmap'd stack
	 */
	memset(stack_sig, 0, sizeof(stack_sig));
	if (stress_sigaltstack(stack_sig, SIGSTKSZ) < 0)
		return EXIT_FAILURE;

	if (stress_temp_dir_mk_args(args) < 0)
		return EXIT_FAILURE;
	(void)stress_temp_filename_args(args,
		filename, sizeof(filename), mwc32());

	/* Create file back'd mmaping for the stack */
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
		pr_fail_err("mmap");
		(void)close(fd);
		goto tidy_dir;
	}
	(void)close(fd);

	if (shim_madvise(stack_mmap, MMAPSTACK_SIZE, MADV_RANDOM) < 0) {
		pr_dbg("%s: madvise failed: errno=%d (%s)\n",
			args->name, errno, strerror(errno));
	}
	memset(stack_mmap, 0, MMAPSTACK_SIZE);

	memset(&c_test, 0, sizeof(c_test));
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
		makecontext(&c_test, stress_stackmmap_push_start, 0);
		swapcontext(&c_main, &c_test);
		inc_counter(args);
	} while (keep_stressing());

	rc = EXIT_SUCCESS;

tidy_mmap:
	munmap(stack_mmap, MMAPSTACK_SIZE);
tidy_dir:
	(void)stress_temp_dir_rm_args(args);
	return rc;
}
#else
int stress_stackmmap(const args_t *args)
{
	return stress_not_implemented(args);
}
#endif
