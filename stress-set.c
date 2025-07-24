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

#include <time.h>

#if defined(HAVE_SYS_FSUID_H)
#include <sys/fsuid.h>
#else
UNEXPECTED
#endif

#if defined(HAVE_GRP_H)
#include <grp.h>
#else
UNEXPECTED
#endif

#if !defined(_DEFAULT_SOURCE)
#define _DEFAULT_SOURCE 1
#endif
#if !defined(_BSD_SOURCE)
#define _BSD_SOURCE 1
#endif

#define GIDS_MAX 	(1024)

typedef struct {
	int	ret;
	struct rlimit rlim;
} stress_rlimit_info_t;

static const shim_rlimit_resource_t rlimit_resources[] = {
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
	RLIMIT_STACK,
#endif
};

static stress_rlimit_info_t rlimits[SIZEOF_ARRAY(rlimit_resources)];

static const stress_help_t help[] = {
	{ NULL,	"set N",	"start N workers exercising the set*() system calls" },
	{ NULL,	"set-ops N",	"stop after N set bogo operations" },
	{ NULL,	NULL,		NULL }
};

/*
 *  stress on set*() calls
 *	stress system by rapid get*() system calls
 */
static int stress_set(stress_args_t *args)
{
	size_t i;
	int ret_hostname;
	const size_t hostname_len = stress_get_hostname_length();
	const size_t longname_len = hostname_len << 1;
	char *hostname;
	char *longname;
#if defined(HAVE_GETPGID) && 	\
    defined(HAVE_SETPGID)
	const pid_t mypid = getpid();
#endif
	const bool cap_sys_resource = stress_check_capability(SHIM_CAP_SYS_RESOURCE);
#if defined(HAVE_SETREUID)
	const bool cap_setuid = stress_check_capability(SHIM_CAP_SETUID);
	int bad_uid_count = 0;
#endif
#if defined(HAVE_GETPGID) &&    \
    defined(HAVE_SETPGID)
	const bool cap_root = stress_check_capability(0);
#endif

	for (i = 0; i < SIZEOF_ARRAY(rlimits); i++) {
		rlimits[i].ret = getrlimit(rlimit_resources[i], &rlimits[i].rlim);
	}

	hostname = (char *)calloc(hostname_len, sizeof(*hostname));
	if (!hostname) {
		pr_inf_skip("%s: cannot allocate hostname array of %zu bytes%s, skipping stressor\n",
			args->name, hostname_len, stress_get_memfree_str());
		return EXIT_NO_RESOURCE;
	}
	longname = (char *)calloc(longname_len, sizeof(*hostname));
	if (!longname) {
		pr_inf_skip("%s: cannot allocate longname array of %zu bytes%s, skipping stressor\n",
			args->name, longname_len, stress_get_memfree_str());
		free(hostname);
		return EXIT_NO_RESOURCE;
	}
	ret_hostname = gethostname(hostname, hostname_len - 1);
	if (ret_hostname == 0) {
		hostname[hostname_len - 1] = '\0';
		(void)shim_strscpy(longname, hostname, longname_len);
	}

	stress_set_proc_state(args->name, STRESS_STATE_SYNC_WAIT);
	stress_sync_start_wait(args);
	stress_set_proc_state(args->name, STRESS_STATE_RUN);

	do {
		int ret;
		gid_t gid;
		uid_t uid;
#if defined(HAVE_SETREUID)
		uid_t bad_uid;
#else
		UNEXPECTED
#endif
		struct rlimit rlim;

		/* setsid will fail, ignore return */
		VOID_RET(pid_t, setsid());
		if (UNLIKELY(!stress_continue(args)))
			break;

		/* getgid always succeeds */
		gid = getgid();
		VOID_RET(int, setgid(gid));
		if (UNLIKELY(!stress_continue(args)))
			break;

		if (*longname) {
			VOID_RET(int, sethostname(longname, longname_len));
			VOID_RET(int, sethostname(hostname, strlen(hostname)));
		}

#if defined(HAVE_GETPGID) &&	\
    defined(HAVE_SETPGID)
		{
			pid_t pid;

			pid = getpgid(mypid);
			if (pid != -1) {
				if (!cap_root) {
					const pid_t bad_pid = stress_get_unused_pid_racy(false);

					/* Exercise invalid pgid */
					VOID_RET(int, setpgid(mypid, bad_pid));

					/* Exercise invalid pid */
					VOID_RET(int, setpgid(bad_pid, pid));

					/* Exercise invalid pid and pgid */
					VOID_RET(int, setpgid(bad_pid, bad_pid));
				}
				VOID_RET(int, setpgid(mypid, pid));
				if (UNLIKELY(!stress_continue(args)))
					break;
			}
		}
#else
		UNEXPECTED
#endif

#if defined(HAVE_SETTIMEOFDAY) &&	\
    defined(HAVE_GETTIMEOFDAY)
		if (!stress_check_capability(SHIM_CAP_SYS_TIME)) {
			struct timeval tv;
			shim_timezone_t tz;

			/* We should not be able to set the time of day */
			ret = gettimeofday(&tv, &tz);
			if (ret == 0) {
				ret = settimeofday(&tv, &tz);
				if (ret < 0) {
					if (UNLIKELY((errno != EPERM) &&
					             (errno != EINVAL) &&
						     (errno != ENOBUFS))) {
						pr_fail("%s: settimeofday failed, could not "
							"set time, errno=%d (%s)\n",
							args->name, errno, strerror(errno));
					}
				}
				/*
				 *  exercise bogus times west tz, possibly undefined behaviour but
				 *  on Linux anything +/- 15*60 is -EINVAL
				 */
				tz.tz_minuteswest = 36 * 60;
				VOID_RET(int, settimeofday(&tv, &tz));
			}
		}
#else
		UNEXPECTED
#endif

#if defined(HAVE_GETPGRP) &&	\
    defined(HAVE_SETPGRP)
		{
			pid_t pid;

			/* getpgrp always succeeds */
			pid = getpgrp();
			if (pid != -1) {
				VOID_RET(int, setpgrp());
				if (UNLIKELY(!stress_continue(args)))
					break;
			}
		}
#else
		UNEXPECTED
#endif

		/* getuid always succeeds */
		uid = getuid();
		VOID_RET(int, setuid(uid));
		if (UNLIKELY(!stress_continue(args)))
			break;

#if defined(HAVE_GRP_H) &&	\
    defined(HAVE_GETGROUPS) &&	\
    defined(HAVE_SETGROUPS)
		ret = getgroups(0, NULL);
		if (ret > 0) {
			gid_t groups[GIDS_MAX];
			int n;

			(void)shim_memset(groups, 0, sizeof(groups));
			ret = STRESS_MINIMUM(ret, (int)SIZEOF_ARRAY(groups));
			n = getgroups(ret, groups);
			if (n > 0) {
				const gid_t bad_groups[1] = { (gid_t)-1 };

				/* Exercise invalid groups */
				VOID_RET(int, shim_setgroups(INT_MIN, groups));
				VOID_RET(int, shim_setgroups(0, groups));
				VOID_RET(int, shim_setgroups(1, bad_groups));
				VOID_RET(int, shim_setgroups(n, groups));
			}
		}
#else
		UNEXPECTED
#endif

#if defined(HAVE_SETREUID)
		VOID_RET(int, setreuid((uid_t)-1, (uid_t)-1));

		bad_uid_count++;
		if (bad_uid_count > 1024) {
			bad_uid_count = 0;

			/*
			 *  Validate setreuid syscalls exercised to increase the current
			 *  ruid and euid without CAP_SETUID capability cannot succeed
			 */
			if ((!cap_setuid) && (stress_get_unused_uid(&bad_uid) >= 0)) {
				if (setreuid(bad_uid, bad_uid) == 0) {
					pr_fail("%s: setreuid failed, did not have privilege to set "
						"ruid and euid, expected -EPERM, instead got errno=%d (%s)\n",
						args->name, errno, strerror(errno));
				}
			}
		}
#else
		UNEXPECTED
#endif

#if defined(HAVE_SETREGID)
#if defined(HAVE_GETRESGID)
		{
			gid_t rgid = (gid_t)-1;
			gid_t egid = (gid_t)-1;
			gid_t sgid = (gid_t)-1;

			ret = getresgid(&rgid, &egid, &sgid);
			if (ret == 0) {
				VOID_RET(int, setregid(rgid, egid));
				VOID_RET(int, setregid((gid_t)-1, egid));
				VOID_RET(int, setregid(rgid, (gid_t)-1));

				if (geteuid() != 0) {
					VOID_RET(int, setregid((gid_t)-2, egid));
					VOID_RET(int, setregid(rgid, (gid_t)-2));
				}
			}
		}
#else
		UNEXPECTED
#endif
		VOID_RET(int, setregid((gid_t)-1, (gid_t)-1));
#else
		UNEXPECTED
#endif

#if defined(HAVE_SETRESUID)
#if defined(HAVE_GETRESUID)
		{
			uid_t ruid = (uid_t)-1;
			uid_t euid = (uid_t)-1;
			uid_t suid = (uid_t)-1;

			ret = getresuid(&ruid, &euid, &suid);
			if (ret == 0) {
				VOID_RET(int, setresuid(ruid, euid, suid));
				VOID_RET(int, setresuid(ruid, euid, (uid_t)-1));
				VOID_RET(int, setresuid(ruid, (uid_t)-1, suid));
				VOID_RET(int, setresuid(ruid, (uid_t)-1, (uid_t)-1));
				VOID_RET(int, setresuid((uid_t)-1, euid, suid));
				VOID_RET(int, setresuid((uid_t)-1, euid, (uid_t)-1));
				VOID_RET(int, setresuid((uid_t)-1, (uid_t)-1, suid));

				if (geteuid() != 0) {
					VOID_RET(int, setresuid((uid_t)-2, euid, suid));
					VOID_RET(int, setresuid(ruid, (uid_t)-2, suid));
					VOID_RET(int, setresuid(ruid, euid, (uid_t)-2));
				}
				VOID_RET(int, setresuid(ruid, euid, suid));
			}
		}
#else
		UNEXPECTED
#endif
		VOID_RET(int, setresuid((uid_t)-1, (uid_t)-1, (uid_t)-1));
#else
		UNEXPECTED
#endif

#if defined(HAVE_SETRESGID)
#if defined(HAVE_GETRESGID)
		{
			gid_t rgid = (gid_t)-1;
			gid_t egid = (gid_t)-1;
			gid_t sgid = (gid_t)-1;

			ret = getresgid(&rgid, &egid, &sgid);
			if (ret == 0) {
				VOID_RET(int, setresgid(rgid, egid, sgid));
				VOID_RET(int, setresgid(rgid, egid, (gid_t)-1));
				VOID_RET(int, setresgid(rgid, (gid_t)-1, sgid));
				VOID_RET(int, setresgid(rgid, (gid_t)-1, (gid_t)-1));
				VOID_RET(int, setresgid((gid_t)-1, egid, sgid));
				VOID_RET(int, setresgid((gid_t)-1, egid, (gid_t)-1));
				VOID_RET(int, setresgid((gid_t)-1, (gid_t)-1, sgid));
				if (geteuid() != 0) {
					VOID_RET(int, setresgid((gid_t)-2, egid, sgid));
					VOID_RET(int, setresgid(rgid, (gid_t)-2, sgid));
					VOID_RET(int, setresgid(rgid, egid, (gid_t)-2));
				}
				VOID_RET(int, setresgid(rgid, egid, sgid));
			}
		}
#else
		UNEXPECTED
#endif
		VOID_RET(int, setresgid((gid_t)-1, (gid_t)-1, (gid_t)-1));
#else
		UNEXPECTED
#endif

#if defined(HAVE_SETFSGID) && 	\
    defined(HAVE_SYS_FSUID_H)
		{
			int fsgid;

			/* Passing -1 will return the current fsgid */
			fsgid = setfsgid((uid_t)-1);
			if (fsgid >= 0) {
				/* Set the current fsgid, should work */
				ret = setfsgid((uid_t)fsgid);
				if (ret == fsgid) {
					/*
					 * we can try changing it to
					 * something else knowing it can
					 * be restored successfully
					 */
					VOID_RET(int, setfsgid(gid));
					VOID_RET(int, setfsgid((uid_t)getegid()));

					/* And restore */
					VOID_RET(int, setfsgid((uid_t)fsgid));
				}
			}
		}
#else
		UNEXPECTED
#endif

#if defined(HAVE_SETFSUID) && 	\
    defined(HAVE_SYS_FSUID_H)
		{
			int fsuid;

			/* Passing -1 will return the current fsuid */
			fsuid = setfsuid((uid_t)-1);
			if (fsuid >= 0) {
				/* Set the current fsuid, should work */
				ret = setfsuid((uid_t)fsuid);
				if (ret == fsuid) {
					/*
					 * we can try changing it to
					 * something else knowing it can
					 * be restored successfully
					 */
					VOID_RET(int, setfsuid(uid));
					VOID_RET(int, setfsuid((uid_t)geteuid()));

					/* And restore */
					VOID_RET(int, setfsuid((uid_t)fsuid));
				}
			}
		}
#else
		UNEXPECTED
#endif

#if defined(__NR_sgetmask) &&	\
    defined(__NR_ssetmask)
		{
			long int mask;

			mask = shim_sgetmask();
			shim_ssetmask(mask);
		}
#endif

#if defined(HAVE_GETDOMAINNAME) &&	\
    defined(HAVE_SETDOMAINNAME)
		{
			char name[2048];

			ret = shim_getdomainname(name, sizeof(name));
			if (ret == 0) {
				/* Exercise zero length name (OK-ish) */
				VOID_RET(int, shim_setdomainname(name, 0));

				/* Exercise long name (-EINVAL) */
				VOID_RET(int, shim_setdomainname(name, sizeof(name)));

				/* Set name back */
				VOID_RET(int, shim_setdomainname(name, strlen(name)));
			}
			if (UNLIKELY(!stress_continue(args)))
				break;
		}
#else
		UNEXPECTED
#endif
		/*
		 *  Invalid setrlimit syscall with invalid
		 *  resource attribute resulting in EINVAL error
		 */
		(void)shim_memset(&rlim, 0, sizeof(rlim));
		if ((getrlimit((shim_rlimit_resource_t)INT_MAX, &rlim) < 0) && (errno == EINVAL)) {
			(void)setrlimit((shim_rlimit_resource_t)INT_MAX, &rlim);
		}

		for (i = 0; i < SIZEOF_ARRAY(rlimits); i++) {
			if (rlimits[i].ret == 0) {
				/* Bogus setrlimit syscall with invalid rlim attribute */
				rlim.rlim_cur = rlimits[i].rlim.rlim_cur;
				if (rlim.rlim_cur > 1) {
					rlim.rlim_max = rlim.rlim_cur - 1;
					(void)setrlimit(rlimit_resources[i], &rlim);
				}

				/* Valid setrlimit syscall and ignoring failure */
				(void)setrlimit(rlimit_resources[i], &rlimits[i].rlim);
			}
		}

		/*
		 *  Invalid setrlimit syscalls exercised
		 *  to increase the current hard limit
		 *  without CAP_SYS_RESOURCE capability
		 */
		if (!cap_sys_resource) {
			for (i = 0; i < SIZEOF_ARRAY(rlimits); i++) {
				if ((rlimits[i].ret == 0) && (rlimits[i].rlim.rlim_max < RLIM_INFINITY)) {
					rlim.rlim_cur = rlimits[i].rlim.rlim_cur;
					rlim.rlim_max = RLIM_INFINITY;
					ret = setrlimit(rlimit_resources[i], &rlim);
					/*
					 *  Cygwin can return -EINVAL as it's not supported in
					 *  due to limited functionality in windows, so don't throw
					 *  a failure for this specific failure.
					 */
					if ((ret != -EPERM) && (ret != -EINVAL)) {
						pr_fail("%s: setrlimit failed, did not have privilege to set "
							"hard limit, expected -EPERM, instead got errno=%d (%s)\n",
							args->name, errno, strerror(errno));
					}
					if (ret == 0) {
						(void)setrlimit(rlimit_resources[i], &rlimits[i].rlim);
					}
				}
			}
		}

		{
			/*
			 *  Exercise stime if it is available and
			 *  ignore CAP_SYS_TIME requirement to call
			 *  it.  Exercise this just once as constantly
			 *  setting the time may cause excessive
			 *  time drift.
			 */
			time_t t;
			static bool test_stime = true;

			if (test_stime && (time(&t) != ((time_t)-1))) {
				VOID_RET(int, shim_stime(&t));
				test_stime = false;
			}
		}
		stress_bogo_inc(args);
	} while (stress_continue(args));

	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);

	free(longname);
	free(hostname);

	return EXIT_SUCCESS;
}

const stressor_info_t stress_set_info = {
	.stressor = stress_set,
	.classifier = CLASS_OS,
	.verify = VERIFY_ALWAYS,
	.help = help
};
