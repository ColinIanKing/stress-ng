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

#if defined(STRESS_SECCOMP)

#include <stdio.h>
#include <stddef.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>

#include <sys/prctl.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/syscall.h>

#include <linux/audit.h>
#include <linux/filter.h>
#include <linux/seccomp.h>

#define SYSCALL_NR	(offsetof(struct seccomp_data, nr))

#define ALLOW_SYSCALL(syscall) \
	BPF_JUMP(BPF_JMP+BPF_JEQ+BPF_K, __NR_##syscall, 0, 1), \
	BPF_STMT(BPF_RET+BPF_K, SECCOMP_RET_ALLOW)

static struct sock_filter filter_allow_write[] = {
        BPF_STMT(BPF_LD+BPF_W+BPF_ABS, SYSCALL_NR),
#if defined(__NR_open)
	ALLOW_SYSCALL(open),
#endif
#if defined(__NR_write)
	ALLOW_SYSCALL(write),
#endif
#if defined(__NR_close)
	ALLOW_SYSCALL(close),
#endif
#if defined(__NR_exit_group)
	ALLOW_SYSCALL(exit_group),
#endif
#if defined(__NR_set_robust_list)
	ALLOW_SYSCALL(set_robust_list),
#endif
	BPF_STMT(BPF_RET+BPF_K, SECCOMP_RET_KILL)
};

static struct sock_filter filter[] = {
        BPF_STMT(BPF_LD+BPF_W+BPF_ABS, SYSCALL_NR),
#if defined(__NR_open)
	ALLOW_SYSCALL(open),
#endif
#if defined(__NR_close)
	ALLOW_SYSCALL(close),
#endif
#if defined(__NR_exit_group)
	ALLOW_SYSCALL(exit_group),
#endif
#if defined(__NR_set_robust_list)
	ALLOW_SYSCALL(set_robust_list),
#endif
	BPF_STMT(BPF_RET+BPF_K, SECCOMP_RET_KILL)
};

static struct sock_fprog prog_allow_write = {
	.len = (unsigned short)SIZEOF_ARRAY(filter_allow_write),
	.filter = filter_allow_write,
};

static struct sock_fprog prog = {
	.len = (unsigned short)SIZEOF_ARRAY(filter),
	.filter = filter
};

#if defined(__NR_seccomp)
static int sys_seccomp(unsigned int operation, unsigned int flags, void *args)
{
	return (int)syscall(__NR_seccomp, operation, flags, args);
}
#endif

/*
 *  stress_seccomp_set_filter()
 *	set up a seccomp filter, allow writes
 * 	if allow_write is true.
 */
static inline int stress_seccomp_set_filter(
	const char *name,
	const bool allow_write)
{
#if defined(__NR_seccomp)
	static bool use_seccomp = true;
#endif

	/*
	 *  Depending on allow_write we either use the
	 *  filter that allows writes or the filter
	 *  that does not allow writes
	 */
	struct sock_fprog *p = allow_write ?
		&prog_allow_write : &prog;

	if (prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0) < 0) {
		pr_fail(stderr, "%s: prctl PR_SET_NEW_PRIVS failed: %d (%s)\n",
			name, errno, strerror(errno));
		return -1;
	}
#if defined(__NR_seccomp)
	/*
	 *  Try using the newer seccomp syscall first
	 */
	if (use_seccomp) {
		if (sys_seccomp(SECCOMP_SET_MODE_FILTER, 0, p) == 0)
			return 0;

		if (errno != ENOSYS) {
			pr_fail(stderr, "%s: seccomp SECCOMP_SET_MODE_FILTER "
				"failed: %d (%s)\n",
				name, errno, strerror(errno));
			return -1;
		}
		use_seccomp = false;
	}
#endif

	if (prctl(PR_SET_SECCOMP, SECCOMP_MODE_FILTER, p) < 0) {
		pr_fail(stderr, "%s: prctl PR_SET_SECCOMP failed: %d (%s)\n",
			name, errno, strerror(errno));
		return -1;
	}
	return 0;
}


/*
 *  stress_seccomp()
 *	stress seccomp
 */
int stress_seccomp(
	uint64_t *const counter,
	const uint32_t instance,
	const uint64_t max_ops,
	const char *name)
{
	(void)instance;

	do {
		pid_t pid;
		int allow_write = (mwc32() % 50) != 0;

		pid = fork();
		if (pid == -1) {
			pr_fail(stderr, "%s: fork failed: %d (%s)\n",
				name, errno, strerror(errno));
			break;
		}
		if (pid == 0) {
			/*
			 *  The child has a seccomp filter applied and
			 *  1 in 50 chance that write() is not allowed
			 *  causing seccomp to kill it and the parent
			 *  sees it die on a SIGSYS
			 */
			int fd, rc = EXIT_SUCCESS;

			if (stress_seccomp_set_filter(name, allow_write) < 0)
				_exit(EXIT_FAILURE);
			if ((fd = open("/dev/null", O_WRONLY)) < 0) {
				pr_err(stderr, "%s: open failed on /dev/null, "
					"errno=%d (%s)\n",
					name, errno, strerror(errno));
				_exit(EXIT_FAILURE);
			}
			if (write(fd, "TEST\n", 5) < 0) {
				pr_err(stderr, "%s: write to /dev/null failed, "
					"errno=%d (%s)\n",
					name, errno, strerror(errno));
				rc = EXIT_FAILURE;
			}
			(void)close(fd);
			_exit(rc);
		}
		if (pid > 0) {
			int status;

			/* Wait for child to exit or get killed by seccomp */
			if (waitpid(pid, &status, 0) < 0) {
				if (errno != EINTR)
					pr_dbg(stderr, "%s: waitpid failed, errno = %d (%s)\n",
						name, errno, strerror(errno));
			} else {
				/* Did the child hit a weird error? */
				if (WIFEXITED(status) &&
				    (WEXITSTATUS(status) != EXIT_SUCCESS)) {
					pr_fail(stderr,
						"%s: aborting because of unexpected "
						"failure in child process\n", name);
					return EXIT_FAILURE;
				}
				/* ..exited OK but we expected SIGSYS death? */
				if (WIFEXITED(status) && !allow_write) {
					pr_fail_err(name,
						"expecting SIGSYS seccomp trap "
						"but got a successful exit which "
						"was not expected");
				}
				/* ..exited with a SIGSYS but we expexted OK exit? */
				if (WIFSIGNALED(status) && allow_write) {
					if (WTERMSIG(status) == SIGSYS) {
						pr_fail_err(name,
							"expecting a successful exit "
							"but got a seccomp SIGSYS "
							"which was not expected");
					}
				}
			}
		}
		(*counter)++;
	} while (opt_do_run && (!max_ops || *counter < max_ops));

	return EXIT_SUCCESS;
}

#endif
