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

#include "stress-ng.h"

#if defined(STRESS_KCMP)

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/syscall.h>

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

/*
 *  sys_kcmp()
 *	kcmp syscall
 */
static inline long sys_kcmp(int pid1, int pid2, int type, int fd1, int fd2)
{
#if defined(__NR_kcmp)
	errno = 0;
	return syscall(__NR_kcmp, pid1, pid2, type, fd1, fd2);
#else
	(void)pid1;
	(void)pid2;
	(void)type;
	(void)fd1;
	(void)fd2;
	errno = ENOSYS;
	return -1;
#endif
}

#define KCMP(pid1, pid2, type, idx1, idx2)		\
{							\
	int rc = sys_kcmp(pid1, pid2, type, idx1, idx2);\
							\
	if (rc < 0) {	 				\
		if (errno == EPERM) {			\
			pr_inf(stderr, capfail, name);	\
			break;				\
		}					\
		pr_fail_err(name, "kcmp: " # type);	\
	}						\
	if (!opt_do_run)				\
		break;					\
}

#define KCMP_VERIFY(pid1, pid2, type, idx1, idx2, res)	\
{							\
	int rc = sys_kcmp(pid1, pid2, type, idx1, idx2);\
							\
	if (rc != res) {				\
		if (rc < 0) {				\
			if (errno == EPERM) {		\
				pr_inf(stderr, capfail, name); \
				break;			\
			}				\
			pr_fail_err(name, "kcmp: " # type);\
		} else {				\
			pr_fail(stderr, "%s: kcmp " # type \
			" returned %d, expected: %d\n",	\
			name, rc, ret);			\
		}					\
	}						\
	if (!opt_do_run)				\
		break;					\
}

/*
 *  stress_kcmp
 *	stress sys_kcmp
 */
int stress_kcmp(
	uint64_t *const counter,
	const uint32_t instance,
	const uint64_t max_ops,
	const char *name)
{
	pid_t pid1;
	int fd1;
	int ret = EXIT_SUCCESS;

	static const char *capfail =
		"%s: need CAP_SYS_PTRACE capability to run kcmp stressor, "
		"aborting stress test\n";

	(void)instance;

	if ((fd1 = open("/dev/null", O_WRONLY)) < 0) {
		pr_fail_err(name, "open");
		return EXIT_FAILURE;
	}

again:
	pid1 = fork();
	if (pid1 < 0) {
		if (opt_do_run && (errno == EAGAIN))
			goto again;

		pr_fail_dbg(name, "fork");
		(void)close(fd1);
		return EXIT_FAILURE;
	} else if (pid1 == 0) {
		setpgid(0, pgrp);
		stress_parent_died_alarm();

		/* Child */
		while (opt_do_run)
			pause();

		/* will never get here */
		(void)close(fd1);
		exit(EXIT_SUCCESS);
	} else {
		/* Parent */
		int fd2, status, pid2;

		setpgid(pid1, pgrp);
		pid2 = getpid();
		if ((fd2 = open("/dev/null", O_WRONLY)) < 0) {
			pr_fail_err(name, "open");
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
			if (opt_flags & OPT_FLAGS_VERIFY) {
				KCMP_VERIFY(pid1, pid1, KCMP_FILE, fd1, fd1, 0);
				KCMP_VERIFY(pid1, pid1, KCMP_FILES, 0, 0, 0);
				KCMP_VERIFY(pid1, pid1, KCMP_FS, 0, 0, 0);
				KCMP_VERIFY(pid1, pid1, KCMP_IO, 0, 0, 0);
				KCMP_VERIFY(pid1, pid1, KCMP_SIGHAND, 0, 0, 0);
				KCMP_VERIFY(pid1, pid1, KCMP_SYSVSEM, 0, 0, 0);
				KCMP_VERIFY(pid1, pid1, KCMP_VM, 0, 0, 0);
				KCMP_VERIFY(pid1, pid2, KCMP_SYSVSEM, 0, 0, 0);
			}
			(*counter)++;
		} while (opt_do_run && (!max_ops || *counter < max_ops));
reap:
		if (fd2 >= 0)
			(void)close(fd2);
		(void)kill(pid1, SIGKILL);
		(void)waitpid(pid1, &status, 0);
		(void)close(fd1);
	}
	return ret;
}

#endif
