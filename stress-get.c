// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2013-2021 Canonical, Ltd.
 * Copyright (C) 2022-2023 Colin Ian King.
 *
 */
#include "stress-ng.h"
#include "core-builtin.h"
#include "core-capabilities.h"
#include "core-mounts.h"

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

static const stress_help_t help[] = {
	{ NULL,	"get N",	"start N workers exercising the get*() system calls" },
	{ NULL,	"get-ops N",	"stop after N get bogo operations" },
	{ NULL, NULL,		NULL }
};

typedef struct {
	const int who;		/* rusage who field */
	const char *name;	/* textual name of who value */
	const bool verify;	/* check for valid rusage return */
} stress_rusage_t;

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
}

/*
 *  stress on get*() calls
 *	stress system by rapid get*() system calls
 */
static int stress_get(const stress_args_t *args)
{
	char *mnts[MOUNTS_MAX];
	int mounts_max;
	const bool verify = !!(g_opt_flags & OPT_FLAGS_VERIFY);
#if defined(HAVE_SYS_TIMEX_H)
#if defined(HAVE_ADJTIMEX) || defined(HAVE_ADJTIME)
	const bool cap_sys_time = stress_check_capability(SHIM_CAP_SYS_TIME);
#endif
#endif

	if (stress_sighandler(args->name, SIGSEGV, stress_segv_handler, NULL) < 0)
		return EXIT_NO_RESOURCE;

	mounts_max = stress_mount_get(mnts, MOUNTS_MAX);

	stress_set_proc_state(args->name, STRESS_STATE_RUN);

	do {
		char path[PATH_MAX];
		char *ptr;
		gid_t gids[GIDS_MAX];
		unsigned cpu, node;
		const pid_t mypid = getpid();
		int ret, n, fs_index;
		size_t i;
#if defined(HAVE_SYS_TIMEX_H) &&	\
    defined(HAVE_ADJTIME)
		struct timeval delta;
#endif
		struct timeval tv;
		struct timezone tz;
		struct rlimit rlim;
		time_t t, t1, t2;

		(void)mypid;

		if (!stress_continue_flag())
			break;

		VOID_RET(pid_t, getppid());

		if (!stress_continue_flag())
			break;

#if defined(HAVE_SWAPCONTEXT) &&        \
    defined(HAVE_UCONTEXT_H)
		{
			ucontext_t context;

			VOID_RET(int, getcontext(&context));
			if (!stress_continue_flag())
				break;
		}
#else
		UNEXPECTED
#endif

#if defined(HAVE_GETDOMAINNAME)
		{
			char name[128];

			VOID_RET(int, shim_getdomainname(name, sizeof(name)));
			if (!stress_continue_flag())
				break;
		}
#else
		UNEXPECTED
#endif

#if defined(HAVE_GETHOSTID)
		{
			VOID_RET(long, gethostid());
			if (!stress_continue_flag())
				break;
		}
#else
		UNEXPECTED
#endif

#if defined(HAVE_GETHOSTNAME)
		{
			char name[128];

			VOID_RET(int, gethostname(name, sizeof(name)));
			if (!stress_continue_flag())
				break;
		}
#else
		UNEXPECTED
#endif

		ptr = getcwd(path, sizeof path);
		if (verify) {
			if (!ptr) {
				pr_fail("%s: getcwd %s failed, errno=%d (%s)%s\n",
					args->name, path, errno, strerror(errno),
					stress_get_fs_type(path));
			} else {
				/* getcwd returned a string: is it the same as path? */
				if (strncmp(ptr, path, sizeof(path))) {
					pr_fail("%s: getcwd returned a string that "
						"is different from the expected path\n",
						args->name);
				}
			}
		}
		if (!stress_continue_flag())
			break;

		VOID_RET(gid_t, getgid());
		VOID_RET(gid_t, getegid());
		VOID_RET(uid_t, getuid());
		VOID_RET(uid_t, geteuid());

		if (!stress_continue_flag())
			break;

		/*
		 *  Zero size should return number of gids to fetch
		 */
		VOID_RET(int, getgroups(0, gids));

		/*
		 *  Try to get GIDS_MAX number of gids
		 */
		ret = getgroups(GIDS_MAX, gids);
		if (verify && (ret < 0) && (errno != EINVAL))
			pr_fail("%s: getgroups failed, errno=%d (%s)\n",
				args->name, errno, strerror(errno));
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
		VOID_RET(long, syscall(__NR_getgroups, -1, gids));
#endif
		if (!stress_continue_flag())
			break;

#if defined(HAVE_GETPGRP)
		VOID_RET(pid_t, getpgrp());
#else
		UNEXPECTED
#endif

#if defined(HAVE_GETPGID)
		{
			pid_t pid;

			VOID_RET(pid_t, getpgid(mypid));

			/*
			 *  Exercise with an possibly invalid pid
			 */
			pid = stress_get_unused_pid_racy(false);
			VOID_RET(pid_t, getpgid(pid));
		}
		if (!stress_continue_flag())
			break;
#else
		UNEXPECTED
#endif

#if defined(HAVE_GETPRIORITY)
		{
			pid_t pid;
			/*
			 *  Exercise getpriority calls that use illegal
			 *  arguments to get more kernel test coverage
			 */
			(void)getpriority((shim_priority_which_t)INT_MIN, 0);
			(void)getpriority((shim_priority_which_t)INT_MAX, 0);
			pid = stress_get_unused_pid_racy(false);
			(void)getpriority((shim_priority_which_t)0, (id_t)pid);
		}

		for (i = 0; i < SIZEOF_ARRAY(priorities); i++) {
			errno = 0;
			ret = getpriority(priorities[i], 0);
			if (verify && errno && (errno != EINVAL) && (ret < 0))
				pr_fail("%s: getpriority failed, errno=%d (%s)\n",
					args->name, errno, strerror(errno));
			if (!stress_continue_flag())
				break;
		}
		/* Exercise getpriority calls using non-zero who argument */
		for (i = 0; i < SIZEOF_ARRAY(priorities); i++){
			VOID_RET(int, getpriority(priorities[i], ~(id_t)0));
		}
#else
		UNEXPECTED
#endif

#if defined(HAVE_GETRESGID)
		{
			gid_t rgid, egid, sgid;

			ret = getresgid(&rgid, &egid, &sgid);
			if (verify && (ret < 0))
				pr_fail("%s: getresgid failed, errno=%d (%s)\n",
					args->name, errno, strerror(errno));
			if (!stress_continue_flag())
				break;
		}
#else
		UNEXPECTED
#endif

#if defined(HAVE_GETRESUID)
		{
			uid_t ruid, euid, suid;

			ret = getresuid(&ruid, &euid, &suid);
			if (verify && (ret < 0))
				pr_fail("%s: getresuid failed, errno=%d (%s)\n",
					args->name, errno, strerror(errno));
			if (!stress_continue_flag())
				break;
		}
#else
		UNEXPECTED
#endif
		/* Invalid getrlimit syscall and ignoring failure */
		(void)getrlimit((shim_rlimit_resource_t)INT_MAX, &rlim);

		for (i = 0; i < SIZEOF_ARRAY(rlimits); i++) {
			ret = getrlimit(rlimits[i], &rlim);
			if (verify && (ret < 0))
				pr_fail("%s: getrlimit(%zu, ..) failed, errno=%d (%s)\n",
					args->name, i, errno, strerror(errno));
			if (!stress_continue_flag())
				break;
		}

#if defined(__NR_ugetrlimit) &&	\
    defined(HAVE_SYSCALL)
		/*
		 * Legacy system call, most probably __NR_ugetrlimit
		 * is not defined, ignore return as it's most probably
		 * ENOSYS too
		 */
		(void)syscall(__NR_ugetrlimit, INT_MAX, &rlim);
		(void)syscall(__NR_ugetrlimit, INT_MIN, &rlim);
		(void)syscall(__NR_ugetrlimit, -1, &rlim);

		for (i = 0; i < SIZEOF_ARRAY(rlimits); i++) {
			VOID_RET(int, (int)syscall(__NR_ugetrlimit, rlimits[i], &rlim));
		}
#endif

#if defined(HAVE_PRLIMIT) &&	\
    NEED_GLIBC(2,13,0) &&	\
    defined(EOVERFLOW)
		{
			pid_t pid;

			/* Invalid prlimit syscall and ignoring failure */
			(void)prlimit(mypid, (shim_rlimit_resource_t)INT_MAX, NULL, &rlim);
			pid = stress_get_unused_pid_racy(false);
			(void)prlimit(pid, (shim_rlimit_resource_t)INT_MAX, NULL, &rlim);

			for (i = 0; i < SIZEOF_ARRAY(rlimits); i++) {
				struct rlimit rlims[2];

				ret = prlimit(mypid, rlimits[i], NULL, &rlims[0]);
				if (verify && (ret < 0) && (errno != EOVERFLOW))
					pr_fail("%s: prlimit(%" PRIdMAX ", %zu, ..) failed, errno=%d (%s)\n",
						args->name, (intmax_t)mypid, i, errno, strerror(errno));
				if (!ret) {
					ret = prlimit(mypid, rlimits[i], &rlims[0], NULL);
					if (verify && (ret < 0) && (errno != EOVERFLOW))
						pr_fail("%s: prlimit(%" PRIdMAX ", %zu, ..) failed, errno=%d (%s)\n",
							args->name, (intmax_t)mypid, i, errno, strerror(errno));
					ret = prlimit(mypid, rlimits[i], &rlims[0], &rlims[1]);
					if (verify && (ret < 0) && (errno != EOVERFLOW))
						pr_fail("%s: prlimit(%" PRIdMAX", %zu, ..) failed, errno=%d (%s)\n",
							args->name, (intmax_t)mypid, i, errno, strerror(errno));
				}

				/* Exercise invalid pids */
				(void)prlimit(pid, rlimits[i], NULL, &rlims[0]);
			}
			if (!stress_continue_flag())
				break;
		}
#else
		UNEXPECTED
#endif

#if defined(HAVE_LINUX_SYSCTL_H) &&	\
    defined(__NR__sysctl) &&		\
    defined(HAVE_SYSCALL)
		{
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
		}
#endif

		for (i = 0; i < SIZEOF_ARRAY(rusages); i++) {
			struct rusage usage;

			ret = shim_getrusage(rusages[i].who, &usage);
			if (rusages[i].verify && verify && (ret < 0) && (errno != ENOSYS))
				pr_fail("%s: getrusage(%s, ..) failed, errno=%d (%s)\n",
					args->name, rusages[i].name, errno, strerror(errno));
			if (!stress_continue_flag())
				break;
		}

#if defined(HAVE_GETSID)
		{
			pid_t pid;

			ret = getsid(mypid);
			if (verify && (ret < 0))
				pr_fail("%s: getsid failed, errno=%d (%s)\n",
					args->name, errno, strerror(errno));
			pid = stress_get_unused_pid_racy(false);
			VOID_RET(int, getsid(pid));
		}
		if (!stress_continue_flag())
		break;
#else
		UNEXPECTED
#endif

		(void)shim_gettid();

		(void)shim_getcpu(&cpu, &node, NULL);
		(void)shim_getcpu(NULL, &node, NULL);
		(void)shim_getcpu(&cpu, NULL, NULL);
		(void)shim_getcpu(NULL, NULL, NULL);
		if (!stress_continue_flag())
			break;

		t = time(NULL);
		if (t == (time_t)-1) {
			pr_fail("%s: time failed, errno=%d (%s)\n",
				args->name, errno, strerror(errno));
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

		}
		/*
		 *  Exercise the time system call using the syscall()
		 *  function to increase kernel test coverage
		 */
		t = shim_time(NULL);
		if ((t == (time_t)-1) && (errno != ENOSYS))
			pr_fail("%s: time failed, errno=%d (%s)\n",
				args->name, errno, strerror(errno));

		t1 = shim_time(&t2);
		if ((t == (time_t)-1) && (errno != ENOSYS)) {
			if (shim_memcmp(&t1, &t2, sizeof(t1))) {
				pr_fail("%s: time failed, errno=%d (%s)\n",
					args->name, errno, strerror(errno));
			}
		}


		/*
		 *  The following gettimeofday calls probably use the VDSO
		 *  on Linux
		 */
		ret = gettimeofday(&tv, NULL);
		if (ret < 0) {
			pr_fail("%s: gettimeval failed, errno=%d (%s)\n",
				args->name, errno, strerror(errno));
		}
		ret = gettimeofday(&tv, &tz);
		if (ret < 0) {
			pr_fail("%s: gettimeval failed, errno=%d (%s)\n",
				args->name, errno, strerror(errno));
		}
#if 0
		/*
		 * gettimeofday with NULL args works fine on x86-64
		 * linux, but on s390 will segfault. Disable this
		 * for now.
		 */
		ret = gettimeofday(NULL, NULL);
		if ((ret < 0) && (errno != ENOSYS)) {
			pr_fail("%s: gettimeval failed, errno=%d (%s)\n",
				args->name, errno, strerror(errno));
		}
#endif

		/*
		 *  Exercise the gettimeofday by force using the syscall
		 *  via the shim to force non-use of libc wrapper and/or
		 *  the vdso
		 */
		ret = shim_gettimeofday(&tv, NULL);
		if ((ret < 0) && (errno != ENOSYS)) {
			pr_fail("%s: gettimeval failed, errno=%d (%s)\n",
				args->name, errno, strerror(errno));
		}
		ret = shim_gettimeofday(&tv, &tz);
		if ((ret < 0) && (errno != ENOSYS)) {
			pr_fail("%s: gettimeval failed, errno=%d (%s)\n",
				args->name, errno, strerror(errno));
		}
		ret = shim_gettimeofday(NULL, NULL);
		if ((ret < 0) && (errno != ENOSYS)) {
			pr_fail("%s: gettimeval failed, errno=%d (%s)\n",
				args->name, errno, strerror(errno));
		}

#if defined(HAVE_UNAME) &&	\
    defined(HAVE_SYS_UTSNAME_H)
		{
			struct utsname utsbuf;
			static bool uname_segv = false;

			if (!uname_segv) {
				ret = sigsetjmp(jmp_env, 1);
				if (!stress_continue(args))
					break;
				if (ret != 0) {
					uname_segv = true;
				} else {
					ret = uname(args->mapped->page_none);
					if (ret >= 0) {
						pr_fail("%s: uname unexpectedly succeeded with read only utsbuf, "
							"expected -EFAULT, instead got errno=%d (%s)\n",
							args->name, errno, strerror(errno));
					}
				}
			}

			ret = uname(&utsbuf);
			if (verify && (ret < 0))
				pr_fail("%s: uname failed, errno=%d (%s)\n",
					args->name, errno, strerror(errno));
		}
#else
		UNEXPECTED
#endif

#if defined(HAVE_GETPAGESIZE)
		VOID_RET(int, getpagesize());
#else
		UNEXPECTED
#endif

#if defined(HAVE_GETDTABLESIZE)
		VOID_RET(int, getdtablesize());
#else
		UNEXPECTED
#endif

#if defined(HAVE_LOOKUP_DCOOKIE) &&	\
    defined(HAVE_SYSCALL)
		{
			char buf[PATH_MAX];

			VOID_RET(int, (int)syscall(__NR_lookup_dcookie, buf, sizeof(buf)));
		}
#else
		UNEXPECTED
#endif

#if defined(HAVE_SYS_TIMEX_H) &&	\
    defined(HAVE_ADJTIMEX)
		{
			struct timex timexbuf;

			timexbuf.modes = 0;
			ret = adjtimex(&timexbuf);
			if (cap_sys_time && verify && (ret < 0) && (errno != EPERM))
				pr_fail("%s: adjtimex failed, errno=%d (%s)\n",
					args->name, errno, strerror(errno));
		}
#else
		UNEXPECTED
#endif

#if defined(HAVE_SYS_TIMEX_H) &&	\
    defined(HAVE_ADJTIME)
		(void)shim_memset(&delta, 0, sizeof(delta));
		ret = adjtime(&delta, &tv);
		if (cap_sys_time && verify && (ret < 0) && (errno != EPERM))
			pr_fail("%s: adjtime failed, errno=%d (%s)\n",
				args->name, errno, strerror(errno));
#else
		UNEXPECTED
#endif

#if defined(__NR_lookup_dcookie)
		/*
		 *  Exercise some random cookie lookups, really likely
		 *  to fail.
		 */
		VOID_RET(int, shim_lookup_dcookie(stress_mwc64(), path, sizeof(path)));
#endif

#if defined(__NR_sgetmask)
		VOID_RET(int, shim_sgetmask());
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
#if defined(HAVE_SYS_VFS_H) &&	\
    defined(HAVE_STATFS) &&	\
    defined(__linux__)
		for (i = 0; i < (size_t)mounts_max; i++) {
			struct statfs buf;
			int fd;

			(void)statfs(mnts[i], &buf);

			fd = open(mnts[i], O_RDONLY);
			if (fd >= 0) {
				(void)fstatfs(fd, &buf);
				(void)close(fd);
			}
		}
#endif

#if defined(HAVE_SYS_STATVFS_H)
		for (i = 0; i < (size_t)mounts_max; i++) {
			struct statvfs buf;

			statvfs(mnts[i], &buf);
		}
#else
		UNEXPECTED
#endif

		stress_bogo_inc(args);
	} while (stress_continue(args));

	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);

	stress_mount_free(mnts, mounts_max);

	return EXIT_SUCCESS;
}

stressor_info_t stress_get_info = {
	.stressor = stress_get,
	.class = CLASS_OS,
	.verify = VERIFY_OPTIONAL,
	.help = help
};
