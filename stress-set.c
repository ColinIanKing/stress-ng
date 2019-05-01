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

#define _DEFAULT_SOURCE 1
#define _BSD_SOURCE 1

#if defined(HOST_NAME_MAX)
#define STRESS_HOST_NAME_LEN	(HOST_NAME_MAX + 1)
#else
#define STRESS_HOST_NAME_LEN	(256)
#endif

#define check_do_run()			\
	if (!g_keep_stressing_flag)	\
		break;			\

#define GIDS_MAX 	(1024)

typedef struct {
	int	id;
	int	ret;
	struct rlimit rlim;
} rlimit_info_t;

static rlimit_info_t rlimits[] = {
#if defined(RLIMIT_AS)
	{ RLIMIT_AS, 0, { 0, 0 } },
#endif
#if defined(RLIMIT_CORE)
	{ RLIMIT_CORE, 0, { 0, 0 } },
#endif
#if defined(RLIMIT_CPU)
	{ RLIMIT_CPU, 0, { 0, 0 } },
#endif
#if defined(RLIMIT_DATA)
	{ RLIMIT_DATA, 0, { 0, 0 } },
#endif
#if defined(RLIMIT_FSIZE)
	{ RLIMIT_FSIZE, 0, { 0, 0 } },
#endif
#if defined(RLIMIT_MEMLOCK)
	{ RLIMIT_MEMLOCK, 0, { 0, 0 } },
#endif
#if defined(RLIMIT_MSGQUEUE)
	{ RLIMIT_MSGQUEUE, 0, { 0, 0 } },
#endif
#if defined(RLIMIT_NICE)
	{ RLIMIT_NICE, 0, { 0, 0 } },
#endif
#if defined(RLIMIT_NOFILE)
	{ RLIMIT_NOFILE, 0, { 0, 0 } },
#endif
#if defined(RLIMIT_RSS)
	{ RLIMIT_RSS, 0, { 0, 0 } },
#endif
#if defined(RLIMIT_RTPRIO)
	{ RLIMIT_RTPRIO, 0, { 0, 0 } },
#endif
#if defined(RLIMIT_RTTIME)
	{ RLIMIT_RTTIME, 0, { 0, 0 } },
#endif
#if defined(RLIMIT_SIGPENDING)
	{ RLIMIT_SIGPENDING, 0, { 0, 0 } },
#endif
#if defined(RLIMIT_STACK)
	{ RLIMIT_STACK, 0, { 0, 0 } },
#endif
};

static const help_t help[] = {
	{ NULL,	"set N",	"start N workers exercising the set*() system calls" },
	{ NULL,	"set-ops N",	"stop after N set bogo operations" },
	{ NULL,	NULL,		NULL }
};

/*
 *  stress on set*() calls
 *	stress system by rapid get*() system calls
 */
static int stress_set(const args_t *args)
{
	size_t i;
	int ret_hostname;
	char hostname[STRESS_HOST_NAME_LEN];
#if defined(HAVE_GETPGID) && defined(HAVE_SETPGID)
	const pid_t mypid = getpid();
#endif

	for (i = 0; i < SIZEOF_ARRAY(rlimits); i++) {
		rlimits[i].ret = getrlimit(rlimits[i].id, &rlimits[i].rlim);
	}

	(void)memset(hostname, 0, sizeof(hostname));
	ret_hostname = gethostname(hostname, sizeof(hostname) - 1);
	if (ret_hostname == 0)
		hostname[sizeof(hostname) - 1 ] = '\0';

	do {
		int ret;
		pid_t pid;
		gid_t gid;
		uid_t uid;

		/* setsid will fail, ignore return */
		pid = setsid();
		(void)pid;
		check_do_run();

		/* getgid always succeeds */
		gid = getgid();
		ret = setgid(gid);
		(void)ret;
		check_do_run();

		if (ret_hostname == 0) {
			ret = sethostname(hostname, sizeof(hostname) - 1);
			(void)ret;
		}

#if defined(HAVE_GETPGID) && defined(HAVE_SETPGID)
		pid = getpgid(mypid);
		if (pid != -1) {
			ret = setpgid(mypid, pid);
			(void)ret;
			check_do_run();
		}
#endif

#if defined(HAVE_GETPGRP) && defined(HAVE_SETPGRP)
		/* getpgrp always succeeds */
		pid = getpgrp();
		if (pid != -1) {
			ret = setpgrp();
			(void)ret;
			check_do_run();
		}
#endif

		/* getuid always succeeds */
		uid = getuid();
		ret = setuid(uid);
		(void)ret;
		check_do_run();

#if defined(HAVE_GRP_H)
		ret = getgroups(0, NULL);
		if (ret > 0) {
			gid_t groups[GIDS_MAX];

			ret = STRESS_MINIMUM(ret, (int)SIZEOF_ARRAY(groups));
			ret = getgroups(ret, groups);
			if (ret > 0) {
				ret = setgroups(ret, groups);
				(void)ret;
			}
		}
#endif

#if defined(HAVE_SETREUID)
		ret = setreuid(-1, -1);
		(void)ret;
#endif
#if defined(HAVE_SETREGID)
		ret = setregid(-1, -1);
		(void)ret;
#endif
#if defined(HAVE_SETRESUID)
		ret = setresuid(-1, -1, -1);
		(void)ret;
#endif
#if defined(HAVE_SETRESGID)
		ret = setresgid(-1, -1, -1);
		(void)ret;
#endif

		for (i = 0; i < SIZEOF_ARRAY(rlimits); i++) {
			if (rlimits[i].ret == 0) {
				ret = setrlimit(rlimits[i].id, &rlimits[i].rlim);
				(void)ret;
			}
		}
		inc_counter(args);
	} while (keep_stressing());

	return EXIT_SUCCESS;
}

stressor_info_t stress_set_info = {
	.stressor = stress_set,
	.class = CLASS_OS,
	.help = help
};
