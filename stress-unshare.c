/*
 * Copyright (C) 2013-2018 Canonical, Ltd.
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

#if defined(__linux__) && defined(__NR_unshare)

#define MAX_PIDS	(32)

#define UNSHARE(flags)	\
	check_unshare(args, flags, #flags);

/*
 *  unshare with some error checking
 */
static void check_unshare(const args_t *args, int flags, const char *flags_name)
{
	int rc;
	rc = shim_unshare(flags);
	if ((rc < 0) && (errno != EPERM) && (errno != EINVAL)) {
		pr_fail("%s: unshare(%s) failed, "
			"errno=%d (%s)\n", args->name, flags_name,
			errno, strerror(errno));
	}
}

/*
 *  enough_memory()
 *	returns true if we have enough memory, ensures
 *	we don't throttle back the unsharing because we
 *	get into deep swappiness
 */
static inline bool enough_memory(void)
{
	size_t shmall, freemem, totalmem;
	bool enough;

	stress_get_memlimits(&shmall, &freemem, &totalmem);

	enough = (freemem == 0) ? true : freemem > (8 * MB);

	return enough;
}

/*
 *  stress_unshare()
 *	stress resource unsharing
 */
int stress_unshare(const args_t *args)
{
	pid_t pids[MAX_PIDS];
	const uid_t euid = geteuid();

	do {
		size_t i, n;

		(void)memset(pids, 0, sizeof(pids));

		for (n = 0; n < MAX_PIDS; n++) {
			if (!g_keep_stressing_flag)
				break;
			if (!enough_memory()) {
				/* memory too low, back off */
				sleep(1);
				break;
			}
			pids[n] = fork();
			if (pids[n] < 0) {
				/* Out of resources for fork */
				if (errno == EAGAIN)
					break;
			} else if (pids[n] == 0) {
				/* Child */
				(void)setpgid(0, g_pgrp);
				stress_parent_died_alarm();

				/* Make sure this is killable by OOM killer */
				set_oom_adjustment(args->name, true);

#if defined(CLONE_FS)
				UNSHARE(CLONE_FS);
#endif
#if defined(CLONE_FILES)
				UNSHARE(CLONE_FILES);
#endif
#if defined(CLONE_NEWIPC)
				UNSHARE(CLONE_NEWIPC);
#endif
#if defined(CLONE_NEWNET)
				/*
				 *  CLONE_NEWNET when running as root on
				 *  hundreds of processes can be stupidly
				 *  expensive on older kernels so limit
				 *  this to just one per stressor instance
				 *  and don't unshare of root
				 */
				if ((n == 0) && (euid != 0))
					UNSHARE(CLONE_NEWNET);
#endif
#if defined(CLONE_NEWNS)
				UNSHARE(CLONE_NEWNS);
#endif
#if defined(CLONE_NEWPID)
				UNSHARE(CLONE_NEWPID);
#endif
#if defined(CLONE_NEWUSER)
				UNSHARE(CLONE_NEWUSER);
#endif
#if defined(CLONE_NEWUTS)
				UNSHARE(CLONE_NEWUTS);
#endif
#if defined(CLONE_SYSVSEM)
				UNSHARE(CLONE_SYSVSEM);
#endif
#if defined(CLONE_THREAD)
				UNSHARE(CLONE_THREAD);
#endif
#if defined(CLONE_SIGHAND)
				UNSHARE(CLONE_SIGHAND);
#endif
#if defined(CLONE_VM)
				UNSHARE(CLONE_VM);
#endif
				_exit(0);
			} else {
				(void)setpgid(pids[n], g_pgrp);
			}
		}
		for (i = 0; i < n; i++) {
			int status;

			if (pids[i] > 0) {
				int ret;

				ret = kill(pids[i], SIGKILL);
				if (ret == 0) {
					if (waitpid(pids[i], &status, 0) < 0) {
						if (errno != EINTR)
							pr_err("%s: waitpid errno=%d (%s)\n",
								args->name, errno, strerror(errno));
					}
				}
			}
		}
		inc_counter(args);
	} while (keep_stressing());

	return EXIT_SUCCESS;
}
#else
int stress_unshare(const args_t *args)
{
	return stress_not_implemented(args);
}
#endif
