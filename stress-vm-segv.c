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

static NOINLINE void vm_unmap_child(const size_t page_size)
{
	size_t len = ~(size_t)0;
	void *addr = align_address((void *)vm_unmap_child, page_size);

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
	void *addr = align_address((void *)vm_unmap_self, page_size);

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
			ret = waitpid(pid, &status, 0);
			if (ret < 0) {
				if (errno != EINTR)
					pr_dbg("%s: waitpid(): errno=%d (%s)\n",
						args->name, errno, strerror(errno));
				(void)kill(pid, SIGTERM);
				(void)kill(pid, SIGKILL);
				(void)waitpid(pid, &status, 0);
			} else if (WIFSIGNALED(status)) {
				/* If we got killed by OOM killer, re-start */
				if (WTERMSIG(status) == SIGKILL) {
					if (g_opt_flags & OPT_FLAGS_OOMABLE) {
						log_system_mem_info();
						pr_dbg("%s: assuming killed by OOM "
							"killer, bailing out "
							"(instance %d)\n",
							args->name, args->instance);
						_exit(0);
					} else {
						log_system_mem_info();
						pr_dbg("%s: assuming killed by OOM "
							"killer, restarting again "
							"(instance %d)\n",
							args->name, args->instance);
						goto again;
					}
				}
				/* expected: child killed itself with SIGSEGV */
				if (WTERMSIG(status) == SIGSEGV) {
					inc_counter(args);
					continue;
				}
			}
		} else if (pid == 0) {
			/* Child */
			const size_t page_size = args->page_size;
			const struct rlimit lim = { RLIM_INFINITY, RLIM_INFINITY };

			setrlimit(RLIMIT_CORE, &lim);
			set_oom_adjustment(args->name, true);
			stress_process_dumpable(false);

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
	.class = CLASS_VM | CLASS_MEMORY | CLASS_OS
};
