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
#define _DEFAULT_SOURCE 1
#define _BSD_SOURCE 1

#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/resource.h>
#if defined(__linux__)
#include <sys/syscall.h>
#include <sys/utsname.h>
#include <sys/timex.h>
#endif
#include <time.h>

#include "stress-ng.h"

#define check_do_run()		\
	if (!opt_do_run)	\
		break;		\

#define GIDS_MAX 	(1024)

static const int rusages[] = {
#ifdef RUSAGE_SELF
	RUSAGE_SELF,
#endif
#ifdef RUSAGE_CHILDREN
	RUSAGE_CHILDREN,
#endif
#ifdef RUSAGE_THREAD
	RUSAGE_THREAD
#endif
};

static const int rlimits[] = {
#ifdef RLIMIT_AS
	RLIMIT_AS,
#endif
#ifdef RLIMIT_CORE
	RLIMIT_CORE,
#endif
#ifdef RLIMIT_CPU
	RLIMIT_CPU,
#endif
#ifdef RLIMIT_DATA
	RLIMIT_DATA,
#endif
#ifdef RLIMIT_FSIZE
	RLIMIT_FSIZE,
#endif
#ifdef RLIMIT_MEMLOCK
	RLIMIT_MEMLOCK,
#endif
#ifdef RLIMIT_MSGQUEUE
	RLIMIT_MSGQUEUE,
#endif
#ifdef RLIMIT_NICE
	RLIMIT_NICE,
#endif
#ifdef RLIMIT_NOFILE
	RLIMIT_NOFILE,
#endif
#ifdef RLIMIT_RSS
	RLIMIT_RSS,
#endif
#ifdef RLIMIT_RTPRIO
	RLIMIT_RTPRIO,
#endif
#ifdef RLIMIT_RTTIME
	RLIMIT_RTTIME,
#endif
#ifdef RLIMIT_SIGPENDING
	RLIMIT_SIGPENDING,
#endif
#ifdef RLIMIT_STACK
	RLIMIT_STACK
#endif
};

static const int priorities[] = {
#ifdef PRIO_PROCESS
	PRIO_PROCESS,
#endif
#ifdef PRIO_PGRP
	PRIO_PGRP,
#endif
#ifdef PRIO_USER
	PRIO_USER
#endif
};

#if defined(__linux__) && defined(__NR_gettid)
static inline int sys_gettid(void)
{
        return syscall(SYS_gettid);
}
#endif

#if defined(__linux__) && defined(__NR_getcpu)
static long sys_getcpu(
        unsigned *cpu,
        unsigned *node,
        void *tcache)
{
	return syscall(__NR_getcpu, cpu, node, tcache);
}
#endif

/*
 *  stress on get*() calls
 *	stress system by rapid get*() system calls
 */
int stress_get(
	uint64_t *const counter,
	const uint32_t instance,
	const uint64_t max_ops,
	const char *name)
{
	const bool verify = (opt_flags & OPT_FLAGS_VERIFY);

	(void)instance;

	do {
		char path[PATH_MAX];
		char *ptr;
		gid_t gids[GIDS_MAX];
#if defined(__linux__)
		gid_t rgid, egid, sgid;
		uid_t ruid, euid, suid;
		struct utsname utsbuf;
		struct timex timexbuf;
#endif
		const pid_t mypid = getpid();
		int ret;
		size_t i;
		struct timeval tv;
		time_t t;

		check_do_run();

		(void)getppid();
		check_do_run();

		ptr = getcwd(path, sizeof path);
		if (verify && !ptr)
			pr_fail(stderr, "%s: getcwd failed, errno=%d (%s)\n",
				name, errno, strerror(errno));
		check_do_run();

		(void)getgid();
		check_do_run();

		(void)getegid();
		check_do_run();

		(void)getuid();
		check_do_run();

		(void)geteuid();
		check_do_run();

		ret = getgroups(GIDS_MAX, gids);
		if (verify && (ret < 0))
			pr_fail(stderr, "%s: getgroups failed, errno=%d (%s)\n",
				name, errno, strerror(errno));
		check_do_run();

		(void)getpgrp();
		check_do_run();

		(void)getpgid(mypid);
		check_do_run();

		for (i = 0; i < SIZEOF_ARRAY(priorities); i++) {
			errno = 0;
			ret = getpriority(priorities[i], 0);
			if (verify && errno && (ret < 0))
				pr_fail(stderr, "%s: getpriority failed, errno=%d (%s)\n",
					name, errno, strerror(errno));
			check_do_run();
		}

#if defined(__linux__)
		ret = getresgid(&rgid, &egid, &sgid);
		if (verify && (ret < 0))
			pr_fail(stderr, "%s: getresgid failed, errno=%d (%s)\n",
				name, errno, strerror(errno));
		check_do_run();

		ret = getresuid(&ruid, &euid, &suid);
		if (verify && (ret < 0))
			pr_fail(stderr, "%s: getresuid failed, errno=%d (%s)\n",
				name, errno, strerror(errno));
		check_do_run();
#endif

		for (i = 0; i < SIZEOF_ARRAY(rlimits); i++) {
			struct rlimit rlim;

			ret = getrlimit(rlimits[i], &rlim);
			if (verify && (ret < 0))
				pr_fail(stderr, "%s: getrlimit(%zu, ..) failed, errno=%d (%s)\n",
					name, i, errno, strerror(errno));
			check_do_run();
		}

#if defined(__linux__) && NEED_GLIBC(2,13,0)
		for (i = 0; i < SIZEOF_ARRAY(rlimits); i++) {
			struct rlimit rlim[2];

			ret = prlimit(mypid, rlimits[i], NULL, &rlim[0]);
			if (verify && (ret < 0))
				pr_fail(stderr, "%s: prlimit(%d, %zu, ..) failed, errno=%d (%s)\n",
					name, mypid, i, errno, strerror(errno));
			if (!ret) {
				prlimit(mypid, rlimits[i], &rlim[0], NULL);
				if (verify && (ret < 0))
					pr_fail(stderr, "%s: prlimit(%d, %zu, ..) failed, errno=%d (%s)\n",
						name, mypid, i, errno, strerror(errno));
				prlimit(mypid, rlimits[i], &rlim[0], &rlim[1]);
				if (verify && (ret < 0))
					pr_fail(stderr, "%s: prlimit(%d, %zu, ..) failed, errno=%d (%s)\n",
						name, mypid, i, errno, strerror(errno));
			}
		
			check_do_run();
		}
#endif

		for (i = 0; i < SIZEOF_ARRAY(rusages); i++) {
			struct rusage usage;

			ret = getrusage(rusages[i], &usage);
			if (verify && (ret < 0))
				pr_fail(stderr, "%s: getrusage(%zu, ..) failed, errno=%d (%s)\n",
					name, i, errno, strerror(errno));
			check_do_run();
		}

#if _XOPEN_SOURCE >= 500 || _XOPEN_SOURCE && _XOPEN_SOURCE_EXTENDED
		ret = getsid(mypid);
		if (verify && (ret < 0))
			pr_fail(stderr, "%s: getsid failed, errno=%d (%s)\n",
				name, errno, strerror(errno));
		check_do_run();
#endif

#if defined(__linux__) && defined(__NR_gettid)
		(void)sys_gettid();
		check_do_run();
#endif

#if defined(__linux__) && defined(__NR_getcpu)
		{
			unsigned cpu, node;

			(void)sys_getcpu(&cpu, &node, NULL);
			(void)sys_getcpu(NULL, &node, NULL);
			(void)sys_getcpu(&cpu, NULL, NULL);
			(void)sys_getcpu(NULL, NULL, NULL);
		}
#endif

		t = time(NULL);
		if (verify && (t == (time_t)-1))
			pr_fail(stderr, "%s: time failed, errno=%d (%s)\n",
				name, errno, strerror(errno));

		ret = gettimeofday(&tv, NULL);
		if (verify && (ret < 0))
			pr_fail(stderr, "%s: gettimeval failed, errno=%d (%s)\n",
				name, errno, strerror(errno));
#if defined(__linux__)
		ret = uname(&utsbuf);
		if (verify && (ret < 0))
			pr_fail(stderr, "%s: uname failed, errno=%d (%s)\n",
				name, errno, strerror(errno));
#endif

#if defined(__linux__)
		timexbuf.modes = 0;
		ret = adjtimex(&timexbuf);
		if (verify && (ret < 0))
			pr_fail(stderr, "%s: adjtimex failed, errno=%d (%s)\n",
				name, errno, strerror(errno));
#endif
		ret = adjtime(NULL, &tv);
		if (verify && (ret < 0))
			pr_fail(stderr, "%s: adjtime failed, errno=%d (%s)\n",
				name, errno, strerror(errno));

		(*counter)++;
	} while (opt_do_run && (!max_ops || *counter < max_ops));

	return EXIT_SUCCESS;
}
