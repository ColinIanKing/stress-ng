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
	{ NULL,	"unshare N",	 "start N workers exercising resource unsharing" },
	{ NULL,	"unshare-ops N", "stop after N bogo unshare operations" },
	{ NULL,	NULL,		 NULL }
};

#if defined(HAVE_UNSHARE)

#define MAX_PIDS	(32)

#if defined(CLONE_FS)		|| \
    defined(CLONE_FILES)	|| \
    defined(CLONE_NEWCGROUP)	|| \
    defined(CLONE_NEWIPC)	|| \
    defined(CLONE_NEWNET)	|| \
    defined(CLONE_NEWNS)	|| \
    defined(CLONE_NEWPID)	|| \
    defined(CLONE_NEWUSER)	|| \
    defined(CLONE_NEWUTS)	|| \
    defined(CLONE_SYSVSEM)	|| \
    defined(CLONE_THREAD)	|| \
    defined(CLONE_SIGHAND)	|| \
    defined(CLONE_VM)

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
#endif

/*
 *  enough_memory()
 *	returns true if we have enough memory, ensures
 *	we don't throttle back the unsharing because we
 *	get into deep swappiness
 */
static inline bool enough_memory(void)
{
	size_t shmall, freemem, totalmem, freeswap;
	bool enough;

	stress_get_memlimits(&shmall, &freemem, &totalmem, &freeswap);

	enough = (freemem == 0) ? true : freemem > (8 * MB);

	return enough;
}

/*
 *  stress_unshare()
 *	stress resource unsharing
 */
static int stress_unshare(const args_t *args)
{
	pid_t pids[MAX_PIDS];
#if defined(CLONE_NEWNET)
	const uid_t euid = geteuid();
#endif

	do {
		size_t i, n;

		(void)memset(pids, 0, sizeof(pids));

		for (n = 0; n < MAX_PIDS; n++) {
			if (!g_keep_stressing_flag)
				break;
			if (!enough_memory()) {
				/* memory too low, back off */
				(void)sleep(1);
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
#if defined(CLONE_NEWCGROUP)
				UNSHARE(CLONE_NEWCGROUP);
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
					if (shim_waitpid(pids[i], &status, 0) < 0) {
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

stressor_info_t stress_unshare_info = {
	.stressor = stress_unshare,
	.class = CLASS_OS,
	.help = help
};
#else
stressor_info_t stress_unshare_info = {
	.stressor = stress_not_implemented,
	.class = CLASS_OS,
	.help = help
};
#endif
