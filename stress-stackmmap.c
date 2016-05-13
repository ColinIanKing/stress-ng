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

#if defined(STRESS_STACKMMAP)

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <ucontext.h>
#include <signal.h>
#include <setjmp.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>

#define MMAPSTACK_SIZE		(2 * MB)

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
		msync(addr, page_size, mwc8() >= 128 ? MS_SYNC : MS_ASYNC);
		laddr = addr;
	}
	if (opt_do_run)
		stress_stackmmap_push_msync();
}

/*
 *  stress_stackmmap
 *	stress a file memory map'd stack
 */
int stress_stackmmap(
	uint64_t *const counter,
	const uint32_t instance,
	const uint64_t max_ops,
	const char *name)
{
	int fd, ret;
	volatile int rc = EXIT_FAILURE;		/* could be clobbered */
	const pid_t pid = getpid();
        stack_t ss;
	struct sigaction new_action;
	char filename[PATH_MAX];
	uint8_t stack_sig[SIGSTKSZ] ALIGN64;	/* ensure we have a sig stack */

	page_size = stress_get_pagesize();
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
		pr_fail_err(name, "sigaction");
		return EXIT_FAILURE;
	}

	/*
	 *  We need an alternative signal stack
	 *  to handle segfaults on an overrun
	 *  mmap'd stack
	 */
        memset(stack_sig, 0, sizeof(stack_sig));
        ss.ss_sp = (void *)stack_sig;
        ss.ss_size = SIGSTKSZ;
        ss.ss_flags = 0;
        if (sigaltstack(&ss, NULL) < 0) {
		pr_fail_err(name, "sigaltstack");
		return EXIT_FAILURE;
	}

	if (stress_temp_dir_mk(name, pid, instance) < 0)
		return EXIT_FAILURE;
	(void)stress_temp_filename(filename, sizeof(filename),
		name, pid, instance, mwc32());

	/* Create file back'd mmaping for the stack */
	fd = open(filename, O_SYNC | O_RDWR | O_CREAT, S_IRUSR | S_IWUSR);
	if (fd < 0) {
		pr_fail_err(name, "mmap'd stack file open");
		goto tidy_dir;
	}
	(void)unlink(filename);
	if (ftruncate(fd, MMAPSTACK_SIZE) < 0) {
		pr_fail_err(name, "ftruncate");
		(void)close(fd);
		goto tidy_dir;
	}
	stack_mmap = mmap(NULL, MMAPSTACK_SIZE, PROT_READ | PROT_WRITE,
		MAP_SHARED, fd, 0);
	if (stack_mmap == MAP_FAILED) {
		pr_fail_err(name, "mmap");
		(void)close(fd);
		goto tidy_dir;
	}
	(void)close(fd);

	if (madvise(stack_mmap, MMAPSTACK_SIZE, MADV_RANDOM) < 0) {
		pr_dbg(stderr, "%s: madvise failed: errno=%d (%s)\n",
			name, errno, strerror(errno));
	}

	memset(&c_test, 0, sizeof(c_test));
	if (getcontext(&c_test) < 0) {
		pr_fail_err(name, "getcontext");
		goto tidy_mmap;
	}
	c_test.uc_stack.ss_sp = stack_mmap;
	c_test.uc_stack.ss_size = MMAPSTACK_SIZE;
	c_test.uc_link = &c_main;
	makecontext(&c_test, stress_stackmmap_push_msync, 0);

	/*
	 *  set jmp handler to jmp back into the loop on a full
	 *  stack segfault.  Use swapcontext to jump into a
	 *  new context using the new mmap'd stack
	 */
	do {
		ret = sigsetjmp(jmp_env, 1);
		if (!ret)
			swapcontext(&c_main, &c_test);
		(*counter)++;
	} while (opt_do_run && (!max_ops || *counter < max_ops));

	rc = EXIT_SUCCESS;

tidy_mmap:
	munmap(stack_mmap, MMAPSTACK_SIZE);
tidy_dir:
	(void)stress_temp_dir_rm(name, pid, instance);

	return rc;
}

#endif
