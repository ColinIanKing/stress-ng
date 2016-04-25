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

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>

#include <sys/mman.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <signal.h>

#include "stress-ng.h"

#define MLOCK_MAX	(256*1024)

#if defined(__NR_mlock2)

#ifndef MLOCK_ONFAULT
#define MLOCK_ONFAULT 1
#endif

static int sys_mlock2(const void *addr, size_t len, int flags)
{
        return (int)syscall(__NR_mlock2, addr, len, flags);
}

/*
 *  mlock_shim()
 *	if mlock2 is available, randonly exerise this
 *	or mlock.  If not available, just fallback to
 *	mlock.  Also, pick random mlock2 flags
 */
static int mlock_shim(const void *addr, size_t len)
{
	static bool use_mlock2 = true;

	if (use_mlock2) {
		uint32_t rnd = mwc32() >> 5;
		/* Randomly use mlock2 or mlock */
		if (rnd & 1) {
			const int flags = (rnd & 2) ?
				0 : MLOCK_ONFAULT;
			int ret;

			ret = sys_mlock2(addr, len, flags);
			if (!ret)
				return 0;
			if (errno != ENOSYS)
				return ret;

			/* mlock2 not supported... */
			use_mlock2 = false;
		}
	}

	/* Just do mlock */
	return mlock((void *)addr, len);
}
#else
static inline int mlock_shim(const void *addr, size_t len)
{
	return mlock((void *)addr, len);
}
#endif


/*
 *  stress_mlock()
 *	stress mlock with pages being locked/unlocked
 */
int stress_mlock(
	uint64_t *const counter,
	const uint32_t instance,
	const uint64_t max_ops,
	const char *name)
{
	const size_t page_size = stress_get_pagesize();
	pid_t pid;
	size_t max = sysconf(_SC_MAPPED_FILES);
	uint8_t **mappings;
	max = max > MLOCK_MAX ? MLOCK_MAX : max;

	if ((mappings = calloc(max, sizeof(uint8_t *))) == NULL) {
                pr_fail_dbg(name, "malloc");
                return EXIT_NO_RESOURCE;
	}
again:
	pid = fork();
	if (pid < 0) {
		if (opt_do_run && (errno == EAGAIN))
			goto again;
		pr_err(stderr, "%s: fork failed: errno=%d: (%s)\n",
			name, errno, strerror(errno));
	} else if (pid > 0) {
		int status, ret;

		setpgid(pid, pgrp);
		stress_parent_died_alarm();

		/* Parent, wait for child */
		ret = waitpid(pid, &status, 0);
		if (ret < 0) {
			if (errno != EINTR)
				pr_dbg(stderr, "%s: waitpid(): errno=%d (%s)\n",
					name, errno, strerror(errno));
			(void)kill(pid, SIGTERM);
			(void)kill(pid, SIGKILL);
			(void)waitpid(pid, &status, 0);
		} else if (WIFSIGNALED(status)) {
			pr_dbg(stderr, "%s: child died: %s (instance %d)\n",
				name, stress_strsignal(WTERMSIG(status)),
				instance);
			/* If we got killed by OOM killer, re-start */
			if (WTERMSIG(status) == SIGKILL) {
				log_system_mem_info();
				pr_dbg(stderr, "%s: assuming killed by OOM "
					"killer, restarting again "
					"(instance %d)\n", name, instance);
				goto again;
			}
			/* If we got killed by sigsegv, re-start */
			if (WTERMSIG(status) == SIGSEGV) {
				pr_dbg(stderr, "%s: killed by SIGSEGV, "
					"restarting again "
					"(instance %d)\n",
					name, instance);
				goto again;
			}
		}
	} else if (pid == 0) {
		size_t i, n;

		setpgid(0, pgrp);

		/* Make sure this is killable by OOM killer */
		set_oom_adjustment(name, true);

		do {
			for (n = 0; opt_do_run && (n < max); n++) {
				int ret;
				if (!opt_do_run || (max_ops && *counter >= max_ops))
					break;

				mappings[n] = (uint8_t *)mmap(NULL, page_size * 3,
					PROT_READ | PROT_WRITE,
					MAP_SHARED | MAP_ANONYMOUS, -1, 0);
				if (mappings[n] == MAP_FAILED)
					break;
				ret = mlock_shim((void *)(mappings[n] + page_size), page_size);
				if (ret < 0) {
					if (errno == EAGAIN)
						continue;
					if (errno == ENOMEM)
						break;
					pr_fail_err(name, "mlock");
					break;
				} else {
					/*
					 * Mappings are always page aligned so
					 * we can use the bottom bit to
					 * indicate if the page has been
					 * mlocked or not
				 	 */
					mappings[n] = (uint8_t *)
						((ptrdiff_t)mappings[n] | 1);
					(*counter)++;
				}
			}

			for (i = 0; i < n;  i++) {
				ptrdiff_t addr = (ptrdiff_t)mappings[i];
				ptrdiff_t mlocked = addr & 1;

				addr ^= mlocked;
				if (mlocked)
					(void)munlock((void *)((uint8_t *)addr + page_size), page_size);
				munmap((void *)addr, page_size * 3);
			}
#if !defined(__gnu_hurd__)
			(void)mlockall(MCL_CURRENT);
			(void)mlockall(MCL_FUTURE);
#if defined(MCL_ONFAULT)
			(void)mlockall(MCL_ONFAULT);
#endif
#endif
			for (n = 0; opt_do_run && (n < max); n++) {
				if (!opt_do_run || (max_ops && *counter >= max_ops))
					break;

				mappings[n] = (uint8_t *)mmap(NULL, page_size,
					PROT_READ | PROT_WRITE,
					MAP_SHARED | MAP_ANONYMOUS, -1, 0);
				if (mappings[n] == MAP_FAILED)
					break;
			}
#if !defined(__gnu_hurd__)
			(void)munlockall();
#endif
			for (i = 0; i < n;  i++)
				munmap((void *)mappings[i], page_size);
		} while (opt_do_run && (!max_ops || *counter < max_ops));
	}

	free(mappings);

	return EXIT_SUCCESS;
}
