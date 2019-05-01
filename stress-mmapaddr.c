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

static volatile bool page_fault = false;

static const help_t help[] = {
	{ NULL,	"mmapaddr N",	  "start N workers stressing mmap with random addresses" },
	{ NULL,	"mmapaddr-ops N", "stop after N mmapaddr bogo operations" },
	{ NULL,	NULL,		  NULL }
};

static void stress_fault_handler(int signum)
{
	(void)signum;

	page_fault = true;
}

/*
 *  stress_mmapaddr_check()
 *	perform some quick sanity checks to see if page is mapped OK
 */
static int stress_mmapaddr_check(const args_t *args, uint8_t *map_addr)
{
	unsigned char vec[1];
	volatile uint8_t val;
	int ret;

	page_fault = false;
	/* Should not fault! */
	val = *map_addr;
	(void)val;

	if (page_fault) {
		pr_err("%s: read of mmap'd address %p SEGFAULTed\n",
			args->name, map_addr);
		return -1;
	}

	vec[0] = 0;
	ret = shim_mincore(map_addr, args->page_size, vec);
	if (ret != 0) {
		pr_err("%s: mincore on address %p failed, errno=%d (%s)\n",
			args->name, map_addr, errno, strerror(errno));
		return -1;
	}
	if ((vec[0] & 1) == 0) {
		pr_inf("%s: mincore on address %p suggests page is not resident\n",
			args->name, map_addr);
		return -1;
	}
	return 0;
}

/*
 *  stress_mmapaddr_get_addr()
 *	try to find an unmapp'd address
 */
static void *stress_mmapaddr_get_addr(
	const args_t *args,
	const uintptr_t mask,
	const size_t page_size)
{
	unsigned char vec[1];
	void *addr = NULL;

	while (keep_stressing()) {
		int ret;

		vec[0] = 0;
		addr = (void *)(intptr_t)(mwc64() & mask);
		ret = shim_mincore(addr, page_size, vec);
		if (ret == 0) {
			addr = NULL;
			continue;	/* it's mapped already */
		} else if (ret <= 0) {
			if (errno == ENOSYS) {
				addr = NULL;
				break;
			}
			if (errno == ENOMEM) {
				break;
			}
		} else {
			addr = NULL;
			continue;
		}
	}
	return addr;
}

/*
 *  stress_mmapaddr()
 *	stress mmap with randomly chosen addresses
 */
static int stress_mmapaddr(const args_t *args)
{
	pid_t pid;

	if (stress_sighandler(args->name, SIGSEGV, stress_fault_handler, NULL) < 0)
		return EXIT_FAILURE;

again: 	pid = fork();
	if (pid < 0) {
		if (g_keep_stressing_flag &&
                    ((errno == EAGAIN) || (errno == ENOMEM)))
			goto again;
		pr_err("%s: fork failed: errno=%d: (%s)\n",
			args->name, errno, strerror(errno));
	} else if (pid > 0) {
		int status, ret;

		(void)setpgid(pid, g_pgrp);

		/* Parent, wait for child */
		ret = shim_waitpid(pid, &status, 0);
		if (ret < 0) {
			if (errno != EINTR)
				pr_dbg("%s: waitpid(): errno=%d (%s)\n",
					args->name, errno, strerror(errno));
			(void)kill(pid, SIGTERM);
			(void)kill(pid, SIGKILL);
			(void)shim_waitpid(pid, &status, 0);
		} else if (WIFSIGNALED(status)) {
			pr_dbg("%s: child died: %s (instance %d)\n",
				args->name, stress_strsignal(WTERMSIG(status)),
				args->instance);
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
					pr_dbg("%s: assuming killed by OOM "
						"killer, restarting again  "
						"(instance %d)\n", args->name,
						args->instance);
					goto again;
				}
			}
			/* If we got killed by sigsegv, re-start */
			if (WTERMSIG(status) == SIGSEGV) {
				pr_dbg("%s: killed by SIGSEGV, "
					"restarting again "
					"(instance %d)\n",
					args->name, args->instance);
				goto again;
			}
		}
	} else if (pid == 0) {
		const size_t page_size = args->page_size;
		const uintptr_t page_mask = ~(page_size - 1);
		const uintptr_t page_mask32 = page_mask & 0xffffffff;

		(void)setpgid(0, g_pgrp);
		stress_parent_died_alarm();

		/* Make sure this is killable by OOM killer */
		set_oom_adjustment(args->name, true);

		do {
			uint8_t *addr, *map_addr, *remap_addr;
			int flags;
			uint8_t rnd = mwc8();
#if defined(MAP_POPULATE)
			const int mmap_flags = MAP_POPULATE | MAP_PRIVATE | MAP_ANONYMOUS;
#else
			const int mmap_flags = MAP_PRIVATE | MAP_ANONYMOUS;
#endif
			/* Randomly chosen low or high address mask */
			const uintptr_t mask = (rnd & 0x80) ? page_mask : page_mask32;

			addr = stress_mmapaddr_get_addr(args, mask, page_size);
			if (!addr) {
				if (errno == ENOSYS)
					break;
				continue;
			}

			/* We get here if page is not already mapped */
#if defined(MAP_FIXED)
			flags = mmap_flags | ((rnd & 0x40) ? MAP_FIXED : 0);
#endif
#if defined(MAP_LOCKED)
			flags |= ((rnd & 0x20) ? MAP_LOCKED : 0);
#endif
			map_addr = (uint8_t *)mmap((void *)addr, page_size, PROT_READ, flags, -1, 0);
			if (!map_addr || (map_addr == MAP_FAILED))
				continue;

			if (stress_mmapaddr_check(args, map_addr) < 0)
				goto unmap;

			/* Now attempt to mmap the newly map'd page */
#if defined(MAP_32BIT)
			flags = mmap_flags;
			addr = map_addr;
			if (rnd & 0x10) {
				addr = NULL;
				flags |= MAP_32BIT;
			}
#endif
			remap_addr = (uint8_t *)mmap((void *)addr, page_size, PROT_READ, flags, -1, 0);
			if (!remap_addr || (remap_addr == MAP_FAILED))
				goto unmap;

			(void)stress_mmapaddr_check(args, remap_addr);
			(void)munmap((void *)remap_addr, page_size);

#if defined(HAVE_MREMAP) && NEED_GLIBC(2,4,0) && defined(MREMAP_FIXED) && defined(MREMAP_MAYMOVE)
			addr = stress_mmapaddr_get_addr(args, mask, page_size);
			if (!addr)
				goto unmap;

			/* Now try to remap with a new fixed address */
			remap_addr = mremap(map_addr, page_size, page_size, MREMAP_FIXED | MREMAP_MAYMOVE, addr);
			if (remap_addr && (remap_addr != MAP_FAILED))
				map_addr = remap_addr;
#endif
unmap:
			(void)munmap((void *)map_addr, page_size);
			inc_counter(args);
		} while (keep_stressing());
	}

	return EXIT_SUCCESS;
}

stressor_info_t stress_mmapaddr_info = {
	.stressor = stress_mmapaddr,
	.class = CLASS_VM | CLASS_OS,
	.help = help
};
