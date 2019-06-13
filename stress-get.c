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

#define check_do_run()			\
	if (!g_keep_stressing_flag)	\
		break;			\

#define GIDS_MAX 	(1024)

static const help_t help[] = {
	{ NULL,	"get N",	"start N workers exercising the get*() system calls" },
	{ NULL,	"get-ops N",	"stop after N get bogo operations" },
	{ NULL, NULL,		NULL }
};

static const int rusages[] = {
#if defined(RUSAGE_SELF)
	RUSAGE_SELF,
#endif
#if defined(RUSAGE_CHILDREN)
	RUSAGE_CHILDREN,
#endif
#if defined(RUSAGE_THREAD)
	RUSAGE_THREAD
#endif
};

/*
 *  The following produce -EINVAL for Haiku, so
 *  disable them
 */
#if defined(__HAIKU__)
#undef RLIMIT_CPU
#undef RLIMIT_FSIZE
#endif

static const int rlimits[] = {
#if defined(RLIMIT_AS)
	RLIMIT_AS,
#endif
#if defined(RLIMIT_CORE)
	RLIMIT_CORE,
#endif
#if defined(RLIMIT_CPU)
	RLIMIT_CPU,
#endif
#if defined(RLIMIT_DATA)
	RLIMIT_DATA,
#endif
#if defined(RLIMIT_FSIZE)
	RLIMIT_FSIZE,
#endif
#if defined(RLIMIT_MEMLOCK)
	RLIMIT_MEMLOCK,
#endif
#if defined(RLIMIT_MSGQUEUE)
	RLIMIT_MSGQUEUE,
#endif
#if defined(RLIMIT_NICE)
	RLIMIT_NICE,
#endif
#if defined(RLIMIT_NOFILE)
	RLIMIT_NOFILE,
#endif
#if defined(RLIMIT_RSS)
	RLIMIT_RSS,
#endif
#if defined(RLIMIT_RTPRIO)
	RLIMIT_RTPRIO,
#endif
#if defined(RLIMIT_RTTIME)
	RLIMIT_RTTIME,
#endif
#if defined(RLIMIT_SIGPENDING)
	RLIMIT_SIGPENDING,
#endif
#if defined(RLIMIT_STACK)
	RLIMIT_STACK
#endif
};

#if defined(HAVE_GETPRIORITY)
static const int priorities[] = {
#if defined(PRIO_PROCESS)
	PRIO_PROCESS,
#endif
#if defined(PRIO_PGRP)
	PRIO_PGRP,
#endif
#if defined(PRIO_USER)
	PRIO_USER
#endif
};
#endif

/*
 *  stress on get*() calls
 *	stress system by rapid get*() system calls
 */
static int stress_get(const args_t *args)
{
	const bool verify = (g_opt_flags & OPT_FLAGS_VERIFY);
#if defined(HAVE_SYS_TIMEX_H)
#if defined(HAVE_ADJTIMEX) || defined(HAVE_ADJTIME)
	const bool cap_sys_time = stress_check_capability(SHIM_CAP_SYS_TIME);
#endif
#endif

	do {
		char path[PATH_MAX];
		char *ptr;
		gid_t gids[GIDS_MAX];
		unsigned cpu, node;
		const pid_t mypid = getpid();
		int ret, n, fs_index;
		size_t i;
#if defined(HAVE_SYS_TIMEX_H) && defined(HAVE_ADJTIME)
		struct timeval delta;
#endif
		struct timeval tv;
		time_t t;
		pid_t pid;
		gid_t gid;
		uid_t uid;

		(void)mypid;

		check_do_run();

		pid = getppid();
		(void)pid;
		check_do_run();

		ptr = getcwd(path, sizeof path);
		if (verify && !ptr)
			pr_fail_err("getcwd");
		check_do_run();

		gid = getgid();
		(void)gid;
		check_do_run();

		gid = getegid();
		(void)gid;
		check_do_run();

		uid = getuid();
		(void)uid;
		check_do_run();

		uid = geteuid();
		(void)uid;
		check_do_run();

		ret = getgroups(GIDS_MAX, gids);
		if (verify && (ret < 0) && (errno != EINVAL))
			pr_fail_err("getgroups");
		check_do_run();

#if defined(HAVE_GETPGRP)
		pid = getpgrp();
		(void)pid;
		check_do_run();
#endif

#if defined(HAVE_GETPGID)
		pid = getpgid(mypid);
		(void)pid;
		check_do_run();
#endif

#if defined(HAVE_GETPRIORITY)
		for (i = 0; i < SIZEOF_ARRAY(priorities); i++) {
			errno = 0;
			ret = getpriority(priorities[i], 0);
			if (verify && errno && (errno != EINVAL) && (ret < 0))
				pr_fail_err("getpriority");
			check_do_run();
		}
#endif

#if defined(HAVE_GETRESGID)
		{
			gid_t rgid, egid, sgid;

			ret = getresgid(&rgid, &egid, &sgid);
			if (verify && (ret < 0))
				pr_fail_err("getresgid");
			check_do_run();
		}
#endif

#if defined(HAVE_GETRESUID)
		{
			uid_t ruid, euid, suid;

			ret = getresuid(&ruid, &euid, &suid);
			if (verify && (ret < 0))
				pr_fail_err("getresuid");
			check_do_run();
		}
#endif

		for (i = 0; i < SIZEOF_ARRAY(rlimits); i++) {
			struct rlimit rlim;

			ret = getrlimit(rlimits[i], &rlim);
			if (verify && (ret < 0))
				pr_fail("%s: getrlimit(%zu, ..) failed, errno=%d (%s)\n",
					args->name, i, errno, strerror(errno));
			check_do_run();
		}

#if defined(HAVE_PRLIMIT) && NEED_GLIBC(2,13,0) && defined(EOVERFLOW)
		for (i = 0; i < SIZEOF_ARRAY(rlimits); i++) {
			struct rlimit rlim[2];

			ret = prlimit(mypid, rlimits[i], NULL, &rlim[0]);
			if (verify && (ret < 0) && (errno != EOVERFLOW))
				pr_fail("%s: prlimit(%d, %zu, ..) failed, errno=%d (%s)\n",
					args->name, mypid, i, errno, strerror(errno));
			if (!ret) {
				ret = prlimit(mypid, rlimits[i], &rlim[0], NULL);
				if (verify && (ret < 0) && (errno != EOVERFLOW))
					pr_fail("%s: prlimit(%d, %zu, ..) failed, errno=%d (%s)\n",
						args->name, mypid, i, errno, strerror(errno));
				ret = prlimit(mypid, rlimits[i], &rlim[0], &rlim[1]);
				if (verify && (ret < 0) && (errno != EOVERFLOW))
					pr_fail("%s: prlimit(%d, %zu, ..) failed, errno=%d (%s)\n",
						args->name, mypid, i, errno, strerror(errno));
			}
			check_do_run();
		}
#endif

#if defined(HAVE_LINUX_SYSCTL_H) &&	\
    defined(__NR__sysctl)
		{
			/*
			 *  _sysctl is a deprecated API, so it
			 *  probably will return -ENOSYS
			 */
			int name[] = { KERN_VERSION };
			char kern_version[64];
			size_t kern_version_len;
			struct __sysctl_args sysctl_args;

			memset(&sysctl_args, 0, sizeof(sysctl_args));
			sysctl_args.name = name;
			sysctl_args.nlen = SIZEOF_ARRAY(name);
			sysctl_args.oldval = kern_version;
			sysctl_args.oldlenp = &kern_version_len;
			sysctl_args.newval = NULL;
			sysctl_args.newlen = 0;

			ret = syscall(__NR__sysctl, &sysctl_args);
			(void)ret;
		}
#endif

		for (i = 0; i < SIZEOF_ARRAY(rusages); i++) {
			struct rusage usage;

			ret = getrusage(rusages[i], &usage);
			if (verify && (ret < 0))
				pr_fail("%s: getrusage(%zu, ..) failed, errno=%d (%s)\n",
					args->name, i, errno, strerror(errno));
			check_do_run();
		}

#if _XOPEN_SOURCE >= 500 || _XOPEN_SOURCE && _XOPEN_SOURCE_EXTENDED
		ret = getsid(mypid);
		if (verify && (ret < 0))
			pr_fail_err("getsid");
		check_do_run();
#endif

		(void)shim_gettid();
		check_do_run();

		(void)shim_getcpu(&cpu, &node, NULL);
		(void)shim_getcpu(NULL, &node, NULL);
		(void)shim_getcpu(&cpu, NULL, NULL);
		(void)shim_getcpu(NULL, NULL, NULL);
		check_do_run();

		t = time(NULL);
		if (verify && (t == (time_t)-1))
			pr_fail_err("time");

		ret = gettimeofday(&tv, NULL);
		if (verify && (ret < 0))
			pr_fail_err("gettimeval");
#if defined(HAVE_UNAME) && defined(HAVE_SYS_UTSNAME_H)
		{
			struct utsname utsbuf;

			ret = uname(&utsbuf);
			if (verify && (ret < 0))
				pr_fail_err("uname");
		}
#endif

#if defined(HAVE_GETPAGESISE)
		ret = getpagesize();
		(void)ret;
#endif

#if defined(HAVE_GETDTABLESIZE)
		ret = getdtablesize();
		(void)ret;
#endif

#if defined(HAVE_LOOKUP_DCOOKIE)
		{
			char buf[PATH_MAX];

			ret = syscall(__NR_lookup_dcookie, buf, sizeof(buf));
			(void)ret;
		}
#endif

#if defined(HAVE_SYS_TIMEX_H) && defined(HAVE_ADJTIMEX)
		{
			struct timex timexbuf;

			timexbuf.modes = 0;
			ret = adjtimex(&timexbuf);
			if (cap_sys_time && verify && (ret < 0))
				pr_fail_err("adjtimex");
		}
#endif

#if defined(HAVE_SYS_TIMEX_H) && defined(HAVE_ADJTIME)
		(void)memset(&delta, 0, sizeof(delta));
		ret = adjtime(&delta, &tv);
		if (cap_sys_time && verify && (ret < 0))
			pr_fail_err("adjtime");
#endif

		/* Get number of file system types */
		n = shim_sysfs(3);
		for (fs_index = 0; fs_index < n; fs_index++) {
			char buf[4096];

			ret = shim_sysfs(2, fs_index, buf);
			if (!ret) {
				ret = shim_sysfs(1, buf);
				if (verify && (ret != fs_index)) {
					pr_fail("%s: sysfs(1, %s) failed, errno=%d (%s)\n",
						args->name, buf, errno, strerror(errno));
				}
			} else {
				if (verify) {
					pr_fail("%s: sysfs(2, %d, buf) failed, errno=%d (%s)\n",
						args->name, fs_index, errno, strerror(errno));
				}
			}
		}
		inc_counter(args);
	} while (keep_stressing());

	return EXIT_SUCCESS;
}

stressor_info_t stress_get_info = {
	.stressor = stress_get,
	.class = CLASS_OS,
	.help = help
};
