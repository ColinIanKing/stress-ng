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

#if defined(_POSIX_MEMLOCK_RANGE) && !defined(__minix__)

#define MLOCK_MAX	(256*1024)

#if defined(__NR_mlock2)

#ifndef MLOCK_ONFAULT
#define MLOCK_ONFAULT 1
#endif

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

			ret = shim_mlock2(addr, len, flags);
			if (!ret)
				return 0;
			if (errno != ENOSYS)
				return ret;

			/* mlock2 not supported... */
			use_mlock2 = false;
		}
	}

	/* Just do mlock */
	return mlock((const void *)addr, len);
}
#else
static inline int mlock_shim(const void *addr, size_t len)
{
	return mlock((const void *)addr, len);
}
#endif


/*
 *  stress_mlock()
 *	stress mlock with pages being locked/unlocked
 */
int stress_mlock(const args_t *args)
{
	const size_t page_size = args->page_size;
	pid_t pid;
	size_t max = sysconf(_SC_MAPPED_FILES);
	uint8_t **mappings;
	max = max > MLOCK_MAX ? MLOCK_MAX : max;

	if ((mappings = calloc(max, sizeof(uint8_t *))) == NULL) {
		pr_fail_dbg("malloc");
		return EXIT_NO_RESOURCE;
	}
again:
	pid = fork();
	if (pid < 0) {
		if (g_keep_stressing_flag && (errno == EAGAIN))
			goto again;
		pr_err("%s: fork failed: errno=%d: (%s)\n",
			args->name, errno, strerror(errno));
	} else if (pid > 0) {
		int status, ret;

		(void)setpgid(pid, g_pgrp);
		stress_parent_died_alarm();

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
			pr_dbg("%s: child died: %s (instance %d)\n",
				args->name, stress_strsignal(WTERMSIG(status)),
				args->instance);
			/* If we got killed by OOM killer, re-start */
			if (WTERMSIG(status) == SIGKILL) {
				log_system_mem_info();
				pr_dbg("%s: assuming killed by OOM "
					"killer, restarting again "
					"(instance %d)\n", args->name,
					args->instance);
				goto again;
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
		size_t i, n;

		(void)setpgid(0, g_pgrp);

		/* Make sure this is killable by OOM killer */
		set_oom_adjustment(args->name, true);

		do {
			for (n = 0; g_keep_stressing_flag && (n < max); n++) {
				int ret;
				if (!keep_stressing())
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
					pr_fail_err("mlock");
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
					inc_counter(args);
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
			for (n = 0; g_keep_stressing_flag && (n < max); n++) {
				if (!keep_stressing())
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
		} while (keep_stressing());
	}

	free(mappings);

	return EXIT_SUCCESS;
}
#else
int stress_mlock(const args_t *args)
{
	return stress_not_implemented(args);
}
#endif
