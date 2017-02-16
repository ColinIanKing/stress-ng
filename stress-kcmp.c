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

#if defined(__linux__) && defined(__NR_kcmp)

/* Urgh, should be from linux/kcmp.h */
enum {
	KCMP_FILE,
	KCMP_VM,
	KCMP_FILES,
	KCMP_FS,
	KCMP_SIGHAND,
	KCMP_IO,
	KCMP_SYSVSEM,
	KCMP_TYPES,
};

#define KCMP(pid1, pid2, type, idx1, idx2)			\
{								\
	int rc = shim_kcmp(pid1, pid2, type, idx1, idx2);	\
								\
	if (rc < 0) {	 					\
		if (errno == EPERM) {				\
			pr_inf(capfail, args->name);		\
			break;					\
		}						\
		pr_fail_err("kcmp: " # type);			\
	}							\
	if (!g_keep_stressing_flag)				\
		break;						\
}

#define KCMP_VERIFY(pid1, pid2, type, idx1, idx2, res)		\
{								\
	int rc = shim_kcmp(pid1, pid2, type, idx1, idx2);	\
								\
	if (rc != res) {					\
		if (rc < 0) {					\
			if (errno == EPERM) {			\
				pr_inf(capfail, args->name); 	\
				break;				\
			}					\
			pr_fail_err("kcmp: " # type);		\
		} else {					\
			pr_fail( "%s: kcmp " # type		\
			" returned %d, expected: %d\n",		\
			args->name, rc, ret);			\
		}						\
	}							\
	if (!g_keep_stressing_flag)				\
		break;						\
}

/*
 *  stress_kcmp
 *	stress sys_kcmp
 */
int stress_kcmp(const args_t *args)
{
	pid_t pid1;
	int fd1;
	int ret = EXIT_SUCCESS;

	static const char *capfail =
		"%s: need CAP_SYS_PTRACE capability to run kcmp stressor, "
		"aborting stress test\n";

	if ((fd1 = open("/dev/null", O_WRONLY)) < 0) {
		pr_fail_err("open");
		return EXIT_FAILURE;
	}

again:
	pid1 = fork();
	if (pid1 < 0) {
		if (g_keep_stressing_flag && (errno == EAGAIN))
			goto again;

		pr_fail_dbg("fork");
		(void)close(fd1);
		return EXIT_FAILURE;
	} else if (pid1 == 0) {
		(void)setpgid(0, g_pgrp);
		stress_parent_died_alarm();

		/* Child */
		while (g_keep_stressing_flag)
			pause();

		/* will never get here */
		(void)close(fd1);
		exit(EXIT_SUCCESS);
	} else {
		/* Parent */
		int fd2, status, pid2;

		(void)setpgid(pid1, g_pgrp);
		pid2 = getpid();
		if ((fd2 = open("/dev/null", O_WRONLY)) < 0) {
			pr_fail_err("open");
			ret = EXIT_FAILURE;
			goto reap;
		}

		do {
			KCMP(pid1, pid2, KCMP_FILE, fd1, fd2);
			KCMP(pid1, pid1, KCMP_FILE, fd1, fd1);
			KCMP(pid2, pid2, KCMP_FILE, fd1, fd1);
			KCMP(pid2, pid2, KCMP_FILE, fd2, fd2);

			KCMP(pid1, pid2, KCMP_FILES, 0, 0);
			KCMP(pid1, pid1, KCMP_FILES, 0, 0);
			KCMP(pid2, pid2, KCMP_FILES, 0, 0);

			KCMP(pid1, pid2, KCMP_FS, 0, 0);
			KCMP(pid1, pid1, KCMP_FS, 0, 0);
			KCMP(pid2, pid2, KCMP_FS, 0, 0);

			KCMP(pid1, pid2, KCMP_IO, 0, 0);
			KCMP(pid1, pid1, KCMP_IO, 0, 0);
			KCMP(pid2, pid2, KCMP_IO, 0, 0);

			KCMP(pid1, pid2, KCMP_SIGHAND, 0, 0);
			KCMP(pid1, pid1, KCMP_SIGHAND, 0, 0);
			KCMP(pid2, pid2, KCMP_SIGHAND, 0, 0);

			KCMP(pid1, pid2, KCMP_SYSVSEM, 0, 0);
			KCMP(pid1, pid1, KCMP_SYSVSEM, 0, 0);
			KCMP(pid2, pid2, KCMP_SYSVSEM, 0, 0);

			KCMP(pid1, pid2, KCMP_VM, 0, 0);
			KCMP(pid1, pid1, KCMP_VM, 0, 0);
			KCMP(pid2, pid2, KCMP_VM, 0, 0);

			/* Same simple checks */
			if (g_opt_flags & OPT_FLAGS_VERIFY) {
				KCMP_VERIFY(pid1, pid1, KCMP_FILE, fd1, fd1, 0);
				KCMP_VERIFY(pid1, pid1, KCMP_FILES, 0, 0, 0);
				KCMP_VERIFY(pid1, pid1, KCMP_FS, 0, 0, 0);
				KCMP_VERIFY(pid1, pid1, KCMP_IO, 0, 0, 0);
				KCMP_VERIFY(pid1, pid1, KCMP_SIGHAND, 0, 0, 0);
				KCMP_VERIFY(pid1, pid1, KCMP_SYSVSEM, 0, 0, 0);
				KCMP_VERIFY(pid1, pid1, KCMP_VM, 0, 0, 0);
				KCMP_VERIFY(pid1, pid2, KCMP_SYSVSEM, 0, 0, 0);
			}
			inc_counter(args);
		} while (keep_stressing());
reap:
		if (fd2 >= 0)
			(void)close(fd2);
		(void)kill(pid1, SIGKILL);
		(void)waitpid(pid1, &status, 0);
		(void)close(fd1);
	}
	return ret;
}
#else
int stress_kcmp(const args_t *args)
{
	return stress_not_implemented(args);
}
#endif
