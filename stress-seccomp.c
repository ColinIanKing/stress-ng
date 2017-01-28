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

#if defined(HAVE_SECCOMP_H) && defined(__linux__) && defined(PR_SET_SECCOMP)

#include <sys/prctl.h>
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
#if defined(__NR_openat)
	ALLOW_SYSCALL(openat),
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
#if defined(__NR_openat)
	ALLOW_SYSCALL(openat),
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

static struct sock_filter filter_random[64];

static struct sock_fprog prog_allow_write = {
	.len = (unsigned short)SIZEOF_ARRAY(filter_allow_write),
	.filter = filter_allow_write
};

static struct sock_fprog prog = {
	.len = (unsigned short)SIZEOF_ARRAY(filter),
	.filter = filter
};

static struct sock_fprog prog_random = {
	.len = (unsigned short)SIZEOF_ARRAY(filter_random),
	.filter = filter_random
};

/*
 *  stress_seccomp_set_filter()
 *	set up a seccomp filter, allow writes
 * 	if allow_write is true.
 */
static inline int stress_seccomp_set_filter(
	const args_t *args,
	const bool allow_write,
	bool do_random)
{
#if defined(__NR_seccomp)
	static bool use_seccomp = true;
#endif

	/*
	 *  Depending on allow_write we either use the
	 *  filter that allows writes or the filter
	 *  that does not allow writes
	 */
	size_t i;
	struct sock_fprog *p = allow_write ?
		&prog_allow_write :
			do_random ? &prog_random : &prog;

	if (do_random) {
		for (i = 0; i < SIZEOF_ARRAY(filter_random); i++) {
			struct sock_filter bpf_stmt = BPF_STMT(mwc32(), SECCOMP_RET_KILL);
			filter_random[i] = bpf_stmt;
		}
	}

	if (prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0) < 0) {
		pr_fail_err("prctl PR_SET_NEW_PRIVS");
		return -1;
	}
#if defined(__NR_seccomp)
	/*
	 *  Try using the newer seccomp syscall first
	 */
	if (use_seccomp) {
redo_seccomp:
		if (shim_seccomp(SECCOMP_SET_MODE_FILTER, 0, p) == 0)
			return 0;

		if (errno != ENOSYS) {
			if (do_random) {
				do_random = false;
				p = &prog;
				goto redo_seccomp;
			}
			pr_fail_err("seccomp SECCOMP_SET_MODE_FILTER");
			return -1;
		}
		use_seccomp = false;
	}
#endif

redo_prctl:
	if (prctl(PR_SET_SECCOMP, SECCOMP_MODE_FILTER, p) < 0) {
		if (do_random) {
			do_random = false;
			p = &prog;
			goto redo_prctl;
		}
		pr_fail_err("prctl PR_SET_SECCOMP");
		return -1;
	}
	return 0;
}


/*
 *  stress_seccomp()
 *	stress seccomp
 */
int stress_seccomp(const args_t *args)
{
	do {
		pid_t pid;
		const bool allow_write = (mwc32() % 50) != 0;
		const bool do_random = (mwc32() % 20) != 0;

		pid = fork();
		if (pid == -1) {
			pr_fail_err("fork");
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

			if (stress_seccomp_set_filter(args, allow_write, do_random) < 0)
				_exit(EXIT_FAILURE);
			if ((fd = open("/dev/null", O_WRONLY)) < 0) {
				pr_err("%s: open failed on /dev/null, "
					"errno=%d (%s)\n",
					args->name, errno, strerror(errno));
				_exit(EXIT_FAILURE);
			}
			if (write(fd, "TEST\n", 5) < 0) {
				pr_err("%s: write to /dev/null failed, "
					"errno=%d (%s)\n",
					args->name, errno, strerror(errno));
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
					pr_dbg("%s: waitpid failed, errno = %d (%s)\n",
						args->name, errno, strerror(errno));
			} else {
				/* Did the child hit a weird error? */
				if (WIFEXITED(status) &&
				    (WEXITSTATUS(status) != EXIT_SUCCESS)) {
					pr_fail("%s: aborting because of unexpected "
						"failure in child process\n", args->name);
					return EXIT_FAILURE;
				}
				/* ..exited OK but we expected SIGSYS death? */
				if (WIFEXITED(status) && !allow_write) {
					pr_fail("%s: expecting SIGSYS seccomp trap "
						"but got a successful exit which "
						"was not expected\n",
						args->name);
				}
				/* ..exited with a SIGSYS but we expexted OK exit? */
				if (WIFSIGNALED(status) && allow_write) {
					if (WTERMSIG(status) == SIGSYS) {
						pr_fail("%s: expecting a successful exit "
							"but got a seccomp SIGSYS "
							"which was not expected\n",
							args->name);
					}
				}
			}
		}
		inc_counter(args);
	} while (keep_stressing());

	return EXIT_SUCCESS;
}
#else
int stress_seccomp(const args_t *args)
{
	return stress_not_implemented(args);
}
#endif
