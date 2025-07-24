/*
 * Copyright (C) 2013-2021 Canonical, Ltd.
 * Copyright (C) 2022-2025 Colin Ian King.
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
 */
#include "stress-ng.h"
#include "core-builtin.h"
#include "core-capabilities.h"
#include "core-mounts.h"

#include <math.h>
#include <time.h>

#if defined(HAVE_LINUX_SYSCTL_H)
#include <linux/sysctl.h>
#endif

#if defined(HAVE_SYS_STATVFS_H)
#include <sys/statvfs.h>
#endif

#if defined(HAVE_SYS_TIMEX_H)
#include <sys/timex.h>
#endif

#if defined(HAVE_SYS_UTSNAME_H)
#include <sys/utsname.h>
#endif

#if defined(HAVE_SYS_VFS_H)
#include <sys/vfs.h>
#endif

#if defined(HAVE_UCONTEXT_H)
#include <ucontext.h>
#endif

#if !defined(_DEFAULT_SOURCE)
#define _DEFAULT_SOURCE 1
#endif
#if !defined(_BSD_SOURCE)
#define _BSD_SOURCE 1
#endif

#define GIDS_MAX 	(1024)
#define MOUNTS_MAX	(256)

typedef int (*stress_get_func_t)(stress_args_t *args);

typedef struct {
	const int who;		/* rusage who field */
	const char *name;	/* textual name of who value */
	const bool verify;	/* check for valid rusage return */
} stress_rusage_t;

static const stress_help_t help[] = {
	{ NULL,	"get N",		"start N workers exercising the get*() system calls" },
	{ NULL,	"get-ops N",		"stop after N get bogo operations" },
	{ NULL,	"get-slow-sync",	"synchronize get*() system amongst N workers" },
	{ NULL, NULL,			NULL }
};

static const stress_opt_t opts[] = {
	{ OPT_get_slow_sync, "get-slow-sync", TYPE_ID_BOOL, 0, 1, NULL },
	END_OPT,
};

#if defined(HAVE_SIGLONGJMP)

static pid_t mypid;
static bool verify;
#if defined(HAVE_SYS_TIMEX_H) &&	\
    (defined(HAVE_ADJTIMEX) || defined(HAVE_ADJTIME))
static bool cap_sys_time;
#endif
static int mounts_max;
static char *mnts[MOUNTS_MAX];

static const stress_rusage_t rusages[] = {
#if defined(RUSAGE_SELF)
	{ RUSAGE_SELF, 		"RUSAGE_SELF",		true },
#endif
#if defined(RUSAGE_CHILDREN)
	{ RUSAGE_CHILDREN, 	"RUSAGE_CHILDREN",	true },
#endif
#if defined(RUSAGE_THREAD)
	{ RUSAGE_THREAD,	"RUSAGE_THREAD",	true },
#endif
#if defined(RUSAGE_BOTH)
	{ RUSAGE_BOTH,		"RUSAGE_BOTH",		true },
#endif
	{ INT_MIN, 		"INT_MIN",		false },
	{ INT_MAX,		"INT_MAX",		false },
};

/*
 *  The following produce -EINVAL for Haiku, so
 *  disable them
 */
#if defined(__HAIKU__)
#undef RLIMIT_CPU
#undef RLIMIT_FSIZE
#endif

static const shim_rlimit_resource_t rlimits[] = {
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
static const shim_priority_which_t priorities[] = {
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

static sigjmp_buf jmp_env;

static void NORETURN MLOCKED_TEXT stress_segv_handler(int num)
{
	(void)num;

	siglongjmp(jmp_env, 1);
	stress_no_return();
}

static int stress_getppid(stress_args_t *args)
{
	VOID_RET(pid_t, getppid());
	(void)args;
	return EXIT_SUCCESS;
}

static int stress_getcontext(stress_args_t *args)
{
#if defined(HAVE_SWAPCONTEXT) &&        \
    defined(HAVE_UCONTEXT_H)
	ucontext_t context;

	VOID_RET(int, getcontext(&context));
#endif
	(void)args;

	return EXIT_SUCCESS;
}

static int stress_getdomainname(stress_args_t *args)
{
#if defined(HAVE_GETDOMAINNAME)
	char name[128];

	VOID_RET(int, shim_getdomainname(name, sizeof(name)));
#endif
	(void)args;

	return EXIT_SUCCESS;
}

static int stress_gethostid(stress_args_t *args)
{
#if defined(HAVE_GETHOSTID)
	VOID_RET(long int, gethostid());
#endif
	(void)args;

	return EXIT_SUCCESS;
}

static int stress_gethostname(stress_args_t *args)
{
#if defined(HAVE_GETHOSTNAME)
	char name[128];

	VOID_RET(int, gethostname(name, sizeof(name)));
#endif
	(void)args;

	return EXIT_SUCCESS;
}

static int stress_getlogin(stress_args_t *args)
{
#if defined(HAVE_GETLOGIN)
	VOID_RET(char *, getlogin());
#endif
	(void)args;

	return EXIT_SUCCESS;
}

static int stress_getcwd(stress_args_t *args)
{
	char *ptr;
	char path[PATH_MAX];

	ptr = getcwd(path, sizeof path);
	if (verify) {
		if (!ptr) {
			pr_fail("%s: getcwd %s failed, errno=%d (%s)%s\n",
				args->name, path, errno, strerror(errno),
					stress_get_fs_type(path));
			return EXIT_FAILURE;
		} else {
			/* getcwd returned a string: is it the same as path? */
			if (strncmp(ptr, path, sizeof(path))) {
				pr_fail("%s: getcwd returned a string that "
					"is different from the expected path\n",
					args->name);
				return EXIT_FAILURE;
			}
		}
	}
	return EXIT_SUCCESS;
}

static int stress_getgid(stress_args_t *args)
{
	VOID_RET(gid_t, getgid());
	(void)args;

	return EXIT_SUCCESS;
}

static int stress_getegid(stress_args_t *args)
{
	VOID_RET(gid_t, getegid());
	(void)args;

	return EXIT_SUCCESS;
}

static int stress_getuid(stress_args_t *args)
{
	VOID_RET(uid_t, getuid());
	(void)args;

	return EXIT_SUCCESS;
}

static int stress_geteuid(stress_args_t *args)
{
	VOID_RET(uid_t, geteuid());
	(void)args;

	return EXIT_SUCCESS;
}

static int stress_getgroups(stress_args_t *args)
{
#if defined(HAVE_GETGROUPS)
	int ret;
	gid_t gids[GIDS_MAX];

	/*
	 *  Zero size should return number of gids to fetch
	 */
	VOID_RET(int, getgroups(0, gids));

	/*
	 *  Try to get GIDS_MAX number of gids
	 */
	ret = getgroups(GIDS_MAX, gids);
	if (verify && (ret < 0) && (errno != EINVAL)) {
		pr_fail("%s: getgroups failed, errno=%d (%s)\n",
			args->name, errno, strerror(errno));
		return EXIT_FAILURE;
	}
	/*
	 *  Exercise invalid getgroups calls
	 */
	VOID_RET(int, getgroups(1, gids));
#if defined(__NR_getgroups) &&	\
    defined(HAVE_SYSCALL)
	/*
	 *  libc may detect a -ve gidsetsize argument and not call
	 *  the system call. Override this by directly calling the
	 *  system call if possible. Memset gids to zero to keep
	 *  valgrind happy.
	 */
	(void)shim_memset(gids, 0, sizeof(gids));
	VOID_RET(long int, syscall(__NR_getgroups, -1, gids));
#endif
#endif
	(void)args;

	return EXIT_SUCCESS;
}

static int stress_getpgrp(stress_args_t *args)
{
#if defined(HAVE_GETPGRP)
	VOID_RET(pid_t, getpgrp());
#endif
	(void)args;

	return EXIT_SUCCESS;
}

static int stress_getpgid(stress_args_t *args)
{
#if defined(HAVE_GETPGID)
	pid_t pid;

	VOID_RET(pid_t, getpgid(mypid));

	/*
	 *  Exercise with an possibly invalid pid
 	 */
	pid = stress_get_unused_pid_racy(false);
	VOID_RET(pid_t, getpgid(pid));
#endif
	(void)args;

	return EXIT_SUCCESS;
}

static int stress_getpriority(stress_args_t *args)
{
#if defined(HAVE_GETPRIORITY)
	pid_t pid;
	static size_t i = 0;

	if (i < SIZEOF_ARRAY(priorities)) {
		int ret;
		/*
		 *  Exercise getpriority calls that use illegal
		 *  arguments to get more kernel test coverage
		 */
		(void)getpriority((shim_priority_which_t)INT_MIN, 0);
		(void)getpriority((shim_priority_which_t)INT_MAX, 0);
		pid = stress_get_unused_pid_racy(false);
		(void)getpriority((shim_priority_which_t)0, (id_t)pid);

		errno = 0;
		ret = getpriority(priorities[i], 0);
		if (verify && errno && (errno != EINVAL) && (ret < 0)) {
			pr_fail("%s: getpriority failed, errno=%d (%s)\n",
				args->name, errno, strerror(errno));
			return EXIT_FAILURE;
		}

		/* Exercise getpriority calls using non-zero who argument */
		VOID_RET(int, getpriority(priorities[i], ~(id_t)0));

		i++;
		if (i >= SIZEOF_ARRAY(priorities))
			i = 0;
	}
#endif
	(void)args;

	return EXIT_SUCCESS;
}

static int stress_getresgid(stress_args_t *args)
{
#if defined(HAVE_GETRESGID)
	gid_t rgid, egid, sgid;
	int ret;

	ret = getresgid(&rgid, &egid, &sgid);
	if (verify && (ret < 0)) {
		pr_fail("%s: getresgid failed, errno=%d (%s)\n",
			args->name, errno, strerror(errno));
		return EXIT_FAILURE;
	}
#endif
	(void)args;

	return EXIT_SUCCESS;
}

static int stress_getresuid(stress_args_t *args)
{
#if defined(HAVE_GETRESUID)
	uid_t ruid, euid, suid;
	int ret;

	ret = getresuid(&ruid, &euid, &suid);
	if (verify && (ret < 0)) {
		pr_fail("%s: getresuid failed, errno=%d (%s)\n",
			args->name, errno, strerror(errno));
		return EXIT_FAILURE;
	}
#endif
	(void)args;

	return EXIT_SUCCESS;
}

static int stress_getrlimit(stress_args_t *args)
{
	static size_t i = 0;

	if (i < SIZEOF_ARRAY(rlimits)) {
		struct rlimit rlim;
		int ret;

		(void)getrlimit((shim_rlimit_resource_t)INT_MAX, &rlim);
		ret = getrlimit(rlimits[i], &rlim);
		if (verify && (ret < 0)) {
			pr_fail("%s: getrlimit(%zu, ..) failed, errno=%d (%s)\n",
				args->name, i, errno, strerror(errno));
			return EXIT_FAILURE;
		}
		i++;
		if (i >= SIZEOF_ARRAY(rlimits))
			i = 0;
	}
	(void)args;

	return EXIT_SUCCESS;
}

static int stress_ugetrlimit(stress_args_t *args)
{
#if defined(__NR_ugetrlimit) &&	\
    defined(HAVE_SYSCALL)
	struct rlimit rlim;
	static size_t i = 0;

	/*
	 * Legacy system call, most probably __NR_ugetrlimit
	 * is not defined, ignore return as it's most probably
	 * ENOSYS too
	 */
	(void)syscall(__NR_ugetrlimit, INT_MAX, &rlim);
	(void)syscall(__NR_ugetrlimit, INT_MIN, &rlim);
	(void)syscall(__NR_ugetrlimit, -1, &rlim);

	if (i < SIZEOF_ARRAY(rlimits)) {
		VOID_RET(int, (int)syscall(__NR_ugetrlimit, rlimits[i], &rlim));

		i++;
		if (i >= SIZEOF_ARRAY(rlimits))
			i = 0;
	}
#endif
	(void)args;

	return EXIT_SUCCESS;
}

static int stress_prlimit(stress_args_t *args)
{
#if defined(HAVE_PRLIMIT) &&	\
    NEED_GLIBC(2,13,0) &&	\
    defined(EOVERFLOW)
	struct rlimit rlim;
	pid_t pid;
	static size_t i = 0;

	/* Invalid prlimit syscall and ignoring failure */
	(void)prlimit(mypid, (shim_rlimit_resource_t)INT_MAX, NULL, &rlim);
	pid = stress_get_unused_pid_racy(false);
	(void)prlimit(pid, (shim_rlimit_resource_t)INT_MAX, NULL, &rlim);

	if (i < SIZEOF_ARRAY(rlimits)) {
		struct rlimit rlims[2];
		int ret;

		ret = prlimit(mypid, rlimits[i], NULL, &rlims[0]);
		if (verify && (ret < 0) && (errno != EOVERFLOW)) {
			pr_fail("%s: prlimit(%" PRIdMAX ", %zu, ..) failed, errno=%d (%s)\n",
				args->name, (intmax_t)mypid, i, errno, strerror(errno));
			return EXIT_FAILURE;
		}
		if (!ret) {
			ret = prlimit(mypid, rlimits[i], &rlims[0], NULL);
			if (verify && (ret < 0) && (errno != EOVERFLOW)) {
				pr_fail("%s: prlimit(%" PRIdMAX ", %zu, ..) failed, errno=%d (%s)\n",
					args->name, (intmax_t)mypid, i, errno, strerror(errno));
				return EXIT_FAILURE;
			}
			ret = prlimit(mypid, rlimits[i], &rlims[0], &rlims[1]);
			if (verify && (ret < 0) && (errno != EOVERFLOW)) {
				pr_fail("%s: prlimit(%" PRIdMAX", %zu, ..) failed, errno=%d (%s)\n",
					args->name, (intmax_t)mypid, i, errno, strerror(errno));
				return EXIT_FAILURE;
			}
		}

		/* Exercise invalid pids */
		(void)prlimit(pid, rlimits[i], NULL, &rlims[0]);

		i++;
		if (i >= SIZEOF_ARRAY(rlimits))
			i = 0;
	}
#endif
	(void)args;

	return EXIT_SUCCESS;
}

static int stress_sysctl(stress_args_t *args)
{
#if defined(HAVE_LINUX_SYSCTL_H) &&	\
    defined(__NR__sysctl) &&		\
    defined(HAVE_SYSCALL)
	/*
	 *  _sysctl is a deprecated API, so it
	 *  probably will return -ENOSYS
	 */
	int name[] = { KERN_VERSION };
	char kern_version[64];
	size_t kern_version_len = 0;
	struct __sysctl_args sysctl_args;

	(void)shim_memset(&sysctl_args, 0, sizeof(sysctl_args));
	sysctl_args.name = name;
	sysctl_args.nlen = SIZEOF_ARRAY(name);
	sysctl_args.oldval = kern_version;
	sysctl_args.oldlenp = &kern_version_len;
	sysctl_args.newval = NULL;
	sysctl_args.newlen = 0;

	VOID_RET(int, (int)syscall(__NR__sysctl, &sysctl_args));
#endif
	(void)args;

	return EXIT_SUCCESS;
}

static int stress_getrusage(stress_args_t *args)
{
	static size_t i = 0;

	if (i < SIZEOF_ARRAY(rusages)) {
		struct rusage usage;
		int ret;

		ret = shim_getrusage(rusages[i].who, &usage);
		if (rusages[i].verify && verify && (ret < 0) && (errno != ENOSYS)) {
			pr_fail("%s: getrusage(%s, ..) failed, errno=%d (%s)\n",
				args->name, rusages[i].name, errno, strerror(errno));
			return EXIT_FAILURE;
		}
		i++;
		if (i >= SIZEOF_ARRAY(rusages))
			i = 0;
	}
	(void)args;

	return EXIT_SUCCESS;
}

static int stress_getsid(stress_args_t *args)
{
#if defined(HAVE_GETSID)
	pid_t pid;
	int ret;

	ret = getsid(mypid);
	if (verify && (ret < 0)) {
		pr_fail("%s: getsid failed, errno=%d (%s)\n",
			args->name, errno, strerror(errno));
		return EXIT_FAILURE;
	}
	pid = stress_get_unused_pid_racy(false);
	VOID_RET(int, getsid(pid));
#endif
	(void)args;

	return EXIT_SUCCESS;
}

static int stress_gettid(stress_args_t *args)
{
	VOID_RET(pid_t, shim_gettid());
	(void)args;

	return EXIT_SUCCESS;
}

static int stress_getcpu(stress_args_t *args)
{
	unsigned cpu, node;

	VOID_RET(int, shim_getcpu(&cpu, &node, NULL));
	VOID_RET(int, shim_getcpu(NULL, &node, NULL));
	VOID_RET(int, shim_getcpu(&cpu, NULL, NULL));
	VOID_RET(int, shim_getcpu(NULL, NULL, NULL));

	(void)args;

	return EXIT_SUCCESS;
}

static int stress_time(stress_args_t *args)
{
	time_t t1, t2;

	t1 = time(NULL);
	if (t1 == (time_t)-1) {
		pr_fail("%s: time failed, errno=%d (%s)\n",
				args->name, errno, strerror(errno));
		return EXIT_FAILURE;
	}
	/*
	 *  Exercise time calls with a pointer to time_t
	 *  variable and check equality with returned value
	 *  with it to increase kernel test coverage
	 */
	t1 = time(&t2);
	if (shim_memcmp(&t1, &t2, sizeof(t1))) {
		pr_fail("%s: time failed, errno=%d (%s)\n",
			args->name, errno, strerror(errno));
		return EXIT_FAILURE;
	}
	/*
	 *  Exercise the time system call using the syscall()
	 *  function to increase kernel test coverage
	 */
	t1 = shim_time(NULL);
	if ((t1 == (time_t)-1) && (errno != ENOSYS)) {
		pr_fail("%s: time failed, errno=%d (%s)\n",
			args->name, errno, strerror(errno));
		return EXIT_FAILURE;
	}
	t1 = shim_time(&t2);
	if ((t1 == (time_t)-1) && (errno != ENOSYS)) {
		if (shim_memcmp(&t1, &t2, sizeof(t1))) {
			pr_fail("%s: time failed, errno=%d (%s)\n",
				args->name, errno, strerror(errno));
			return EXIT_FAILURE;
		}
	}

	(void)args;

	return EXIT_SUCCESS;
}

static int stress_gettimeofday(stress_args_t *args)
{
	int ret;
	struct timeval tv;
	shim_timezone_t tz;

#if defined(HAVE_GETTIMEOFDAY)
	/*
	 *  The following gettimeofday calls probably use the VDSO
	 *  on Linux
	 */
	ret = gettimeofday(&tv, NULL);
	if (ret < 0) {
		pr_fail("%s: gettimeofday failed, errno=%d (%s)\n",
			args->name, errno, strerror(errno));
		return EXIT_FAILURE;
	}
	ret = gettimeofday(&tv, &tz);
	if (ret < 0) {
		pr_fail("%s: gettimeofday failed, errno=%d (%s)\n",
			args->name, errno, strerror(errno));
		return EXIT_FAILURE;
	}
#endif
#if 0
	/*
	 * gettimeofday with NULL args works fine on x86-64
	 * linux, but on s390 will segfault. Disable this
	 * for now.
	 */
	ret = shim_gettimeofday(NULL, NULL);
	if ((ret < 0) && (errno != ENOSYS)) {
		pr_fail("%s: gettimeofday failed, errno=%d (%s)\n",
			args->name, errno, strerror(errno));
		return EXIT_FAILURE;
	}
#endif
	/*
	 *  Exercise the gettimeofday by force using the syscall
	 *  via the shim to force non-use of libc wrapper and/or
	 *  the vdso
	 */
	ret = shim_gettimeofday(&tv, NULL);
	if ((ret < 0) && (errno != ENOSYS)) {
		pr_fail("%s: gettimeofday failed, errno=%d (%s)\n",
			args->name, errno, strerror(errno));
		return EXIT_FAILURE;
	}
	ret = shim_gettimeofday(&tv, &tz);
	if ((ret < 0) && (errno != ENOSYS)) {
		pr_fail("%s: gettimeofday failed, errno=%d (%s)\n",
			args->name, errno, strerror(errno));
		return EXIT_FAILURE;
	}
#if 0
	ret = shim_gettimeofday(NULL, NULL);
	if ((ret < 0) && (errno != ENOSYS)) {
		pr_fail("%s: gettimeofday failed, errno=%d (%s)\n",
			args->name, errno, strerror(errno));
		return EXIT_FAILURE;
	}
#endif

	(void)args;

	return EXIT_SUCCESS;
}

static int stress_uname(stress_args_t *args)
{
#if defined(HAVE_UNAME) &&	\
    defined(HAVE_SYS_UTSNAME_H)
	struct utsname utsbuf;
	static bool uname_segv = false;

	if (!uname_segv) {
		int ret;

		ret = sigsetjmp(jmp_env, 1);
		if (ret != 0) {
			uname_segv = true;
		} else {
			ret = uname(args->mapped->page_none);
			if (ret >= 0) {
				pr_fail("%s: uname unexpectedly succeeded with read only utsbuf, "
					"expected -EFAULT, instead got errno=%d (%s)\n",
					args->name, errno, strerror(errno));
				return EXIT_FAILURE;
			}
		}

		ret = uname(&utsbuf);
		if (verify && (ret < 0)) {
			pr_fail("%s: uname failed, errno=%d (%s)\n",
				args->name, errno, strerror(errno));
			return EXIT_FAILURE;
		}
	}
#endif
	(void)args;

	return EXIT_SUCCESS;
}

static int stress_getpagesize(stress_args_t *args)
{
#if defined(HAVE_GETPAGESIZE)
	VOID_RET(int, getpagesize());
#endif
	(void)args;

	return EXIT_SUCCESS;
}

static int stress_getdtablesize(stress_args_t *args)
{
#if defined(HAVE_GETDTABLESIZE)
	VOID_RET(int, getdtablesize());
#endif
	(void)args;

	return EXIT_SUCCESS;
}

static int stress_lookup_dcookie(stress_args_t *args)
{
#if defined(HAVE_LOOKUP_DCOOKIE) &&	\
    defined(HAVE_SYSCALL)
	char path[PATH_MAX];

	/*
	 *  Exercise some random cookie lookups, really likely
	 *  to fail.
	 */
	VOID_RET(int, shim_lookup_dcookie(stress_mwc64(), path, sizeof(path)));
#endif
	(void)args;

	return EXIT_SUCCESS;
}

static int stress_adjtimex(stress_args_t *args)
{
#if defined(HAVE_SYS_TIMEX_H) &&	\
    defined(HAVE_ADJTIMEX)
	struct timex timexbuf;
	int ret;

	timexbuf.modes = 0;
	ret = adjtimex(&timexbuf);
	if (cap_sys_time && verify && (ret < 0) && (errno != EPERM)) {
		pr_fail("%s: adjtimex failed, errno=%d (%s)\n",
			args->name, errno, strerror(errno));
		return EXIT_FAILURE;
	}
#endif
	(void)args;

	return EXIT_SUCCESS;
}

static int stress_adjtime(stress_args_t *args)
{
#if defined(HAVE_SYS_TIMEX_H) &&	\
    defined(HAVE_ADJTIME)
	int ret;
	struct timeval delta;
	struct timeval tv;

	(void)shim_memset(&delta, 0, sizeof(delta));
	ret = adjtime(&delta, &tv);
	if (cap_sys_time && verify && (ret < 0) && (errno != EPERM)) {
		pr_fail("%s: adjtime failed, errno=%d (%s)\n",
			args->name, errno, strerror(errno));
		return EXIT_FAILURE;
	}
#endif
	(void)args;

	return EXIT_SUCCESS;
}

static int stress_sgetmask(stress_args_t *args)
{
#if defined(__NR_sgetmask)
	VOID_RET(int, shim_sgetmask());
#endif
	(void)args;

	return EXIT_SUCCESS;
}

static int stress_sysfs(stress_args_t *args)
{
	int n;
	static int fs_index = 0;

	/* Get number of file system types */
	n = shim_sysfs(3);

	if (fs_index < n) {
		char buf[4096];
		int ret;

		ret = shim_sysfs(2, fs_index, buf);
		if (!ret) {
			ret = shim_sysfs(1, buf);
			if (verify && (ret != fs_index)) {
				pr_fail("%s: sysfs(1, %s) failed, errno=%d (%s)\n",
					args->name, buf, errno, strerror(errno));
				return EXIT_FAILURE;
			}
		} else {
			if (verify) {
				pr_fail("%s: sysfs(2, %d, buf) failed, errno=%d (%s)\n",
					args->name, fs_index, errno, strerror(errno));
				return EXIT_FAILURE;
			}
		}
		fs_index++;
		if (fs_index >= n)
			fs_index = 0;
	}
	(void)args;

	return EXIT_SUCCESS;
}

static int stress_statfs(stress_args_t *args)
{
#if defined(HAVE_SYS_VFS_H) &&	\
    defined(HAVE_STATFS) &&	\
    defined(__linux__)
	static size_t i = 0;

	if (i < (size_t)mounts_max) {
		struct statfs buf;
		int fd;

		(void)statfs(mnts[i], &buf);

		fd = open(mnts[i], O_RDONLY);
		if (fd >= 0) {
			(void)fstatfs(fd, &buf);
			(void)close(fd);
		}
		i++;
		if (i >= (size_t)mounts_max)
			i = 0;
	}
#endif
	(void)args;

	return EXIT_SUCCESS;
}

static int stress_statvfs(stress_args_t *args)
{
#if defined(HAVE_SYS_STATVFS_H)
	static size_t i = 0;

	if (i < (size_t)mounts_max) {
		struct statvfs buf;

		(void)statvfs(mnts[i], &buf);
		i++;
		if (i >= (size_t)mounts_max)
			i = 0;
	}
#endif
	(void)args;

	return EXIT_SUCCESS;
}

static const stress_get_func_t stress_get_funcs[] = {
	stress_adjtime,
	stress_adjtimex,
	stress_getcontext,
	stress_getcpu,
	stress_getcwd,
	stress_getdomainname,
	stress_getdtablesize,
	stress_getegid,
	stress_geteuid,
	stress_getgid,
	stress_getgroups,
	stress_gethostid,
	stress_gethostname,
	stress_getpagesize,
	stress_getpgid,
	stress_getpgrp,
	stress_getppid,
	stress_getpriority,
	stress_getresgid,
	stress_getresuid,
	stress_getrlimit,
	stress_getrusage,
	stress_getsid,
	stress_gettid,
	stress_gettimeofday,
	stress_getuid,
	stress_lookup_dcookie,
	stress_prlimit,
	stress_sgetmask,
	stress_statfs,
	stress_statvfs,
	stress_sysctl,
	stress_sysfs,
	stress_time,
	stress_ugetrlimit,
	stress_uname
};

/*
 *  stress on get*() calls
 *	stress system by rapid get*() system calls
 */
static int stress_get(stress_args_t *args)
{
	NOCLOBBER int rc = EXIT_SUCCESS;
	bool get_slow_sync = false;
	size_t i = 0;

	(void)stress_get_setting("get-slow-sync", &get_slow_sync);

#if defined(HAVE_SYS_TIMEX_H) &&	\
    (defined(HAVE_ADJTIMEX) || defined(HAVE_ADJTIME))
	cap_sys_time = stress_check_capability(SHIM_CAP_SYS_TIME);
#endif
	if (stress_sighandler(args->name, SIGSEGV, stress_segv_handler, NULL) < 0)
		return EXIT_NO_RESOURCE;

	mounts_max = stress_mount_get(mnts, MOUNTS_MAX);
	mypid = getpid();
	verify = !!(g_opt_flags & OPT_FLAGS_VERIFY);

	stress_set_proc_state(args->name, STRESS_STATE_SYNC_WAIT);
	stress_sync_start_wait(args);
	stress_set_proc_state(args->name, STRESS_STATE_RUN);

	i = 0;


	do {
		if (get_slow_sync) {
			i = (size_t)(round(stress_time_now() * 10.0)) % SIZEOF_ARRAY(stress_get_funcs);
		} else {
			i++;
			if (i >= SIZEOF_ARRAY(stress_get_funcs))
				i = 0;
		}
		stress_get_funcs[i](args);
		stress_bogo_inc(args);
	} while (stress_continue(args));

	/* getlogin can re-set alarm(), so exercise it once at end */
	stress_getlogin(args);

	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);

	stress_mount_free(mnts, mounts_max);

	return rc;
}

const stressor_info_t stress_get_info = {
	.stressor = stress_get,
	.classifier = CLASS_OS,
	.verify = VERIFY_OPTIONAL,
	.opts = opts,
	.help = help
};

#else

const stressor_info_t stress_get_info = {
	.stressor = stress_unimplemented,
	.classifier = CLASS_OS,
	.verify = VERIFY_OPTIONAL,
	.opts = opts,
	.help = help,
	.unimplemented_reason = "built without siglongjmp support"
};

#endif
