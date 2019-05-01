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
	{ NULL,	"vm-segv N",	 "start N workers that unmap their address space" },
	{ NULL,	"vm-segv-ops N", "stop after N vm-segv unmap'd SEGV faults" },
	{ NULL,	NULL,		 NULL }
};

static NOINLINE void vm_unmap_child(const size_t page_size)
{
	size_t len = ~(size_t)0;
	void *addr = stress_align_address((void *)vm_unmap_child, page_size);

	len = len ^ (len >> 1);
	while (len > page_size) {
		munmap((void *)0, len - page_size);
		len >>= 1;
		clflush(addr);
		shim_clear_cache(addr, addr + 64);
	}
}

static NOINLINE void vm_unmap_self(const size_t page_size)
{
	void *addr = stress_align_address((void *)vm_unmap_self, page_size);

	munmap(addr, page_size);
	clflush(addr);
	shim_clear_cache(addr, addr + 64);
}

/*
 *  stress_vm_segv()
 *	stress vm segv by unmapping child's address space
 *	and generating a segv on return because child has
 *	no address space on return.
 */
static int stress_vm_segv(const args_t *args)
{
	set_oom_adjustment(args->name, true);

	do {
		pid_t pid;

again:
		if (!g_keep_stressing_flag)
			return EXIT_SUCCESS;
		pid = fork();
		if (pid < 0) {
			if ((errno == EAGAIN) || (errno == EINTR) || (errno == ENOMEM))
				goto again;
			pr_err("%s: fork failed: errno=%d: (%s)\n",
				args->name, errno, strerror(errno));
			return EXIT_NO_RESOURCE;
		} else if (pid > 0) {
			int status, ret;

			(void)setpgid(pid, g_pgrp);
			/* Parent, wait for child */

			ret = shim_waitpid(pid, &status, 0);
			if (ret < 0)
				goto kill_child;
#if !defined(HAVE_PTRACE)
			if (WTERMSIG(status) == SIGSEGV) {
				inc_counter(args);
				continue;
			}
#else

			(void)ptrace(PTRACE_SETOPTIONS, pid, 0, PTRACE_O_TRACESYSGOOD);

			while (keep_stressing()) {
				(void)ptrace(PTRACE_SYSCALL, pid, 0, 0);

				ret = shim_waitpid(pid, &status, 0);
				if (ret < 0)
					goto kill_child;
				if (WIFSTOPPED(status)) {
					int signum = WSTOPSIG(status);

					if ((signum & 0x7f) == SIGSEGV) {
						inc_counter(args);
						break;
					}
					if (signum & 0x80) 
						continue;
				}
				if (WIFEXITED(status)) {
					inc_counter(args);
					break;
				}
			}
#endif
kill_child:
			(void)kill(pid, SIGTERM);
			(void)kill(pid, SIGKILL);
			(void)shim_waitpid(pid, &status, 0);
		} else if (pid == 0) {
			/* Child */
			sigset_t set;
			const size_t page_size = args->page_size;

			set_oom_adjustment(args->name, true);
			stress_process_dumpable(false);

#if defined(HAVE_PTRACE)
			(void)ptrace(PTRACE_TRACEME);
			kill(getpid(), SIGSTOP);
#endif
			(void)sigemptyset(&set);
			(void)sigaddset(&set, SIGSEGV);
			(void)sigprocmask(SIG_BLOCK, &set, NULL);

			/*
			 *  Try to ummap the child's address space, should cause
			 *  a SIGSEGV at some point..
			 */
			vm_unmap_child(page_size);

			/*
			 *  That failed, so try unmapping this function
			 */
			vm_unmap_self(page_size);

			/* No luck, well that's unexpected.. */
			_exit(EXIT_FAILURE);
		}
	} while (keep_stressing());

	return EXIT_SUCCESS;
}

stressor_info_t stress_vm_segv_info = {
	.stressor = stress_vm_segv,
	.class = CLASS_VM | CLASS_MEMORY | CLASS_OS,
	.help = help
};
