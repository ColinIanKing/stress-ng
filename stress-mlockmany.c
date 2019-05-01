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
	{ NULL,	"mlockmany N",	   "start N workers exercising many mlock/munlock processes" },
	{ NULL,	"mlockmany-ops N", "stop after N mlockmany bogo operations" },
	{ NULL,	NULL,		   NULL }
};

#if defined(HAVE_MLOCK)

#define MAX_MLOCK_PROCS		(1024)

/*
 *  stress_mlockmany()
 *	stress by forking and exiting
 */
static int stress_mlockmany(const args_t *args)
{
	pid_t pids[MAX_MLOCK_PROCS];
	int errnos[MAX_MLOCK_PROCS];
	int ret;
#if defined(RLIMIT_MEMLOCK)
	struct rlimit rlim;
#endif
	size_t mlock_size, max_mlock_procs;

	set_oom_adjustment(args->name, true);

	/* Explicitly drop capabilites, makes it more OOM-able */
	ret = stress_drop_capabilities(args->name);
	(void)ret;

	max_mlock_procs = args->num_instances > 0 ? MAX_MLOCK_PROCS / args->num_instances : 1;
	if (max_mlock_procs < 1)
		max_mlock_procs = 1;

#if defined(RLIMIT_MEMLOCK)
	ret = getrlimit(RLIMIT_MEMLOCK, &rlim);
	if (ret < 0) {
		mlock_size = 8 * MB;
	} else {
		mlock_size = rlim.rlim_cur;
	}
#else
	mlock_size = args->page_size * 1024;
#endif

	do {
		unsigned int i, n;
		size_t shmall, freemem, totalmem, freeswap, last_freeswap;

		(void)memset(pids, 0, sizeof(pids));
		(void)memset(errnos, 0, sizeof(errnos));

		stress_get_memlimits(&shmall, &freemem, &totalmem, &last_freeswap);

		for (n = 0; n < max_mlock_procs; n++) {
			pid_t pid;

			if (!keep_stressing())
				break;

			stress_get_memlimits(&shmall, &freemem, &totalmem, &freeswap);

			/* We detected swap being used, bail out */
			if (last_freeswap > freeswap)
				break;

			/* Keep track of expanding free swap space */
			if (freeswap > last_freeswap)
				last_freeswap = freeswap;

			pid = fork();
			if (pid == 0) {
				void *ptr = MAP_FAILED;
				size_t mmap_size = mlock_size;

				(void)setpgid(0, g_pgrp);
				stress_parent_died_alarm();
				set_oom_adjustment(args->name, true);

				shim_mlockall(0);
				stress_get_memlimits(&shmall, &freemem, &totalmem, &freeswap);
				/* We detected swap being used, bail out */
				if (last_freeswap > freeswap)
					_exit(0);

				while (mmap_size > args->page_size) {
					if (!keep_stressing())
						_exit(0);
					ptr = mmap(NULL, mmap_size, PROT_READ | PROT_WRITE,
						MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
					if (ptr != MAP_FAILED)
						break;
					mmap_size >>= 1;
				}
				if (ptr == MAP_FAILED)
					_exit(0);


				mincore_touch_pages(ptr, mmap_size);

				mlock_size = mmap_size;
				while (mlock_size > args->page_size) {
					if (!keep_stressing())
						_exit(0);
					ret = shim_mlock(ptr, mlock_size);
					if (ret == 0)
						break;
					mlock_size >>= 1;
				}

				for (;;) {
					if (!keep_stressing())
						goto unlock;
					(void)shim_munlock(ptr, mlock_size);
					if (!keep_stressing())
						goto unmap;
					(void)shim_mlock(ptr, mlock_size);
					if (!keep_stressing())
						goto unlock;
					/* Try invalid sizes */
					(void)shim_mlock(ptr, 0);
					(void)shim_munlock(ptr, 0);

					(void)shim_mlock(ptr, mlock_size << 1);
					(void)shim_munlock(ptr, mlock_size << 1);

					(void)shim_mlock(ptr, ~(size_t)0);
					(void)shim_munlock(ptr, ~(size_t)0);
					if (!keep_stressing())
						goto unlock;
					(void)shim_usleep_interruptible(10000);
				}
unlock:
				(void)shim_munlock(ptr, mlock_size);
unmap:
				(void)munmap(ptr, mmap_size);
				_exit(0);
			} else if (pid < 0) {
				errnos[n] = errno;
			}
			if (pid > -1)
				(void)setpgid(pids[n], g_pgrp);
			pids[n] = pid;
			if (!g_keep_stressing_flag)
				break;
		}
		for (i = 0; i < n; i++) {
			if (pids[i] > 0) {
				int status;
				/* Parent, wait for child */
				(void)kill(pids[i], SIGKILL);
				(void)shim_waitpid(pids[i], &status, 0);
				inc_counter(args);
			}
		}
	} while (keep_stressing());

	return EXIT_SUCCESS;
}

stressor_info_t stress_mlockmany_info = {
	.stressor = stress_mlockmany,
	.class = CLASS_VM | CLASS_OS | CLASS_PATHOLOGICAL,
	.help = help
};

#else

stressor_info_t stress_mlockmany_info = {
	.stressor = stress_not_implemented,
	.class = CLASS_VM | CLASS_OS | CLASS_PATHOLOGICAL,
	.help = help
};

#endif

