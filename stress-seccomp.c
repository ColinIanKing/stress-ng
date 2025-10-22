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

#if defined(HAVE_LINUX_AUDIT_H)
#include <linux/audit.h>
#else
UNEXPECTED
#endif

#if defined(HAVE_LINUX_FILTER_H)
#include <linux/filter.h>
#else
UNEXPECTED
#endif

#if defined(HAVE_LINUX_SECCOMP_H)
#include <linux/seccomp.h>
#else
UNEXPECTED
#endif

#if defined(HAVE_SYS_PRCTL_H)
#include <sys/prctl.h>
#else
UNEXPECTED
#endif

static const stress_help_t help[] = {
	{ NULL,	"seccomp N",	 "start N workers performing seccomp call filtering" },
	{ NULL,	"seccomp-ops N", "stop after N seccomp bogo operations" },
	{ NULL,	NULL,		 NULL }
};

#if defined(HAVE_LINUX_SECCOMP_H) &&	\
    defined(HAVE_LINUX_AUDIT_H) &&	\
    defined(HAVE_LINUX_FILTER_H) &&	\
    defined(HAVE_SYS_PRCTL_H) &&	\
    defined(PR_SET_SECCOMP) &&		\
    defined(SECCOMP_SET_MODE_FILTER)

#define EXIT_TRAPPED	(255)
#define SYSCALL_NR	(offsetof(struct seccomp_data, nr))

#define ALLOW_SYSCALL(syscall) \
	BPF_JUMP(BPF_JMP + BPF_JEQ + BPF_K, __NR_##syscall, 0, 1), \
	BPF_STMT(BPF_RET + BPF_K, SECCOMP_RET_ALLOW)

#if defined(__NR_seccomp)
static bool use_seccomp = true;
#endif

static struct sock_filter filter_allow_all[] = {
	BPF_STMT(BPF_RET + BPF_K, SECCOMP_RET_ALLOW)
};

static struct sock_filter filter_allow_write[] = {
	BPF_STMT(BPF_LD + BPF_W + BPF_ABS, SYSCALL_NR),
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
	BPF_STMT(BPF_RET + BPF_K, SECCOMP_RET_TRAP)
};

static struct sock_filter filter[] = {
	BPF_STMT(BPF_LD + BPF_W + BPF_ABS, SYSCALL_NR),
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
	BPF_STMT(BPF_RET + BPF_K, SECCOMP_RET_TRAP)
};

static struct sock_filter filter_random[64];

static struct sock_fprog prog_allow_all = {
	.len = (unsigned short int)SIZEOF_ARRAY(filter_allow_all),
	.filter = filter_allow_all
};

static struct sock_fprog prog_allow_write = {
	.len = (unsigned short int)SIZEOF_ARRAY(filter_allow_write),
	.filter = filter_allow_write
};

static struct sock_fprog prog = {
	.len = (unsigned short int)SIZEOF_ARRAY(filter),
	.filter = filter
};

static struct sock_fprog prog_random = {
	.len = (unsigned short int)SIZEOF_ARRAY(filter_random),
	.filter = filter_random
};

#if defined(SECCOMP_GET_ACTION_AVAIL)
static uint32_t seccomp_actions[] = {
#if defined(SECCOMP_RET_KILL_PROCESS)
	SECCOMP_RET_KILL_PROCESS,
#endif
#if defined(SECCOMP_RET_KILL_THREAD)
	SECCOMP_RET_KILL_THREAD,
#endif
#if defined(SECCOMP_RET_TRAP)
	SECCOMP_RET_TRAP,
#endif
#if defined(SECCOMP_RET_ERRNO)
	SECCOMP_RET_ERRNO,
#endif
#if defined(SECCOMP_RET_USER_NOTIF)
	SECCOMP_RET_USER_NOTIF,
#endif
#if defined(SECCOMP_RET_TRACE)
	SECCOMP_RET_TRACE,
#endif
#if defined(SECCOMP_RET_LOG)
	SECCOMP_RET_LOG,
#endif
#if defined(SECCOMP_RET_LOG)
	SECCOMP_RET_ALLOW,
#endif
	~0U,	/* Invalid */
};
#endif

static int stress_seccomp_supported(const char *name)
{
	pid_t pid;
	int status;

	pid = fork();
	if (pid < 0) {
		pr_inf_skip("%s stressor will be skipped, the check for seccomp "
			"failed, fork failed, errno=%d (%s)\n",
			name, errno, strerror(errno));
		return -1;
	}
	if (pid == 0) {
		if (shim_seccomp(SECCOMP_SET_MODE_FILTER, 0, &prog_allow_all) == 0) {
			_exit(0);
		}
		_exit(errno);
	}
	if (shim_waitpid(pid, &status, 0) < 0) {
		pr_inf_skip("%s stressor will be skipped, the check for seccomp "
			"failed, wait failed, errno=%d (%s)\n",
			name, errno, strerror(errno));
		return -1;
	}
	if (WIFEXITED(status) && (WEXITSTATUS(status) != 0)) {
		errno = WEXITSTATUS(status);
		if (errno == EACCES) {
			pr_inf_skip("%s stressor will be skipped, SECCOMP_SET_MODE_FILTER "
				"requires CAP_SYS_ADMIN capability\n", name);
			return -1;
		} else {
			pr_inf_skip("%s: seccomp stressor will be skipped, "
				"SECCOMP_SET_MODE_FILTER is not supported, errno=%d (%s)\n",
				name, errno, strerror(errno));
		}
		return -1;
	}
	return 0;
}

static void MLOCKED_TEXT NORETURN stress_sigsys(int signum)
{
	(void)signum;

	_exit(EXIT_TRAPPED);
}

/*
 *  stress_seccomp_set_huge_filter()
 *	set up a huge seccomp filter, see how large we can go
 */
static int stress_seccomp_set_huge_filter(stress_args_t *args)
{
	static struct sock_filter bpf_stmt =
		BPF_STMT(BPF_RET + BPF_K, SECCOMP_RET_ALLOW);
	struct sock_fprog huge_prog;
	const size_t bits = sizeof(huge_prog.len) * 8 > 31 ? 31 : sizeof(huge_prog.len) * 8;
	const size_t n_max = ((size_t)1 << bits) - 1;
	size_t i, j, n = 32, max = 1;

	(void)shim_memset(&huge_prog, 0, sizeof(huge_prog));
	if (prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0) < 0) {
		pr_fail("%s: prctl PR_SET_NEW_PRIVS failed, errno=%d (%s)\n",
			args->name, errno, strerror(errno));
		return -1;
	}

	for (j = 0; (n < n_max) && (n != max) && (j < 64); j++) {
		struct sock_filter *huge_filter;

		huge_filter = (struct sock_filter *)calloc(n, sizeof(*huge_filter));
		if (!huge_filter)
			return -1;

		for (i = 0; i < n; i++)
			huge_filter[i] = bpf_stmt;
		huge_prog.len = (unsigned short int)n;
		huge_prog.filter = huge_filter;

#if defined(__NR_seccomp)
		/*
		 *  Try using the newer seccomp syscall first
		 */
		if (use_seccomp) {
			if (shim_seccomp(SECCOMP_SET_MODE_FILTER, 0, &huge_prog) == 0) {
				max = n;
				n = n + n;
				goto next;
			}
		}
#endif
		if (prctl(PR_SET_SECCOMP, SECCOMP_MODE_FILTER, &huge_prog) == 0) {
			max = n;
			n = n + n;
			goto next;
		}
		n = max + ((n - max) >> 1);
next:
		free(huge_filter);
	}
	return 0;
}

/*
 *  stress_seccomp_set_filter()
 *	set up a seccomp filter, allow writes
 * 	if allow_write is true.
 */
static int stress_seccomp_set_filter(
	stress_args_t *args,
	const bool allow_write,
	bool do_random)
{
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
			struct sock_filter bpf_stmt = BPF_STMT(stress_mwc32(), SECCOMP_RET_KILL);
			filter_random[i] = bpf_stmt;
		}
	}

	if (prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0) < 0) {
		pr_fail("%s: prctl PR_SET_NO_NEW_PRIVS failed, errno=%d (%s)\n",
			args->name, errno, strerror(errno));
		return -1;
	}
#if defined(__NR_seccomp)
	/*
	 *  Try using the newer seccomp syscall first
	 */
	if (use_seccomp) {
#if defined(HAVE_SECCOMP_NOTIF_SIZES)
		{
			struct seccomp_notif_sizes sizes;

			/*
			 *  Exercise SECCOMP_GET_NOTIF_SIZES
			 */
			(void)shim_seccomp(SECCOMP_GET_NOTIF_SIZES, 0, &sizes);
			/* Invalid flags, EINVAL */
			(void)shim_seccomp(SECCOMP_GET_NOTIF_SIZES, ~0U, &sizes);
		}
#else
		UNEXPECTED
#endif
#if defined(SECCOMP_GET_ACTION_AVAIL)
		{
			for (i = 0; i < SIZEOF_ARRAY(seccomp_actions); i++) {
				/* Valid flags */
				(void)shim_seccomp(SECCOMP_GET_ACTION_AVAIL, 0, (void *)&seccomp_actions[i]);
				/* Invalid flags, EINVAL */
				(void)shim_seccomp(SECCOMP_GET_ACTION_AVAIL, ~0U, (void *)&seccomp_actions[i]);
			}
		}
#else
		UNEXPECTED
#endif
#if defined(SECCOMP_SET_MODE_STRICT)
		if (stress_mwc8() < 16) {
			(void)shim_seccomp(SECCOMP_SET_MODE_STRICT, 0, NULL);
			(void)shim_seccomp(SECCOMP_SET_MODE_STRICT, ~0U, NULL);
			(void)shim_seccomp(SECCOMP_SET_MODE_STRICT, 0, &i);
		}
#else
		UNEXPECTED
#endif
		/* Exercise invalid op */
		(void)shim_seccomp(~0U, 0, NULL);

redo_seccomp:
		if (shim_seccomp(SECCOMP_SET_MODE_FILTER, 0, p) == 0)
			return 0;

		if (errno != ENOSYS) {
			if (do_random) {
				do_random = false;
				p = &prog;
				goto redo_seccomp;
			}
			pr_fail("%s: prctl SECCOMP_SET_MODE_FILTER failed, errno=%d (%s)\n",
				args->name, errno, strerror(errno));
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
		pr_fail("%s: prctl PR_SET_SECCOMP SECCOMP_MODE_FILTER failed, errno=%d (%s)\n",
			args->name, errno, strerror(errno));
		return -1;
	}
	return 0;
}

/*
 *  stress_seccomp()
 *	stress seccomp
 */
static int stress_seccomp(stress_args_t *args)
{
	stress_set_proc_state(args->name, STRESS_STATE_SYNC_WAIT);
	stress_sync_start_wait(args);
	stress_set_proc_state(args->name, STRESS_STATE_RUN);

	do {
		pid_t pid;
		const bool allow_write = stress_mwc1() != 0;
		const bool do_random = stress_mwc32modn(20) != 0;

		pid = fork();
		if (pid == -1) {
			pr_fail("%s: fork failed, errno=%d (%s)\n",
				args->name, errno, strerror(errno));
			break;
		}
		if (pid == 0) {
			stress_set_proc_state(args->name, STRESS_STATE_RUN);

			/*
			 *  The child has a seccomp filter applied and
			 *  1 in 50 chance that write() is not allowed
			 *  causing seccomp to kill it and the parent
			 *  sees it die on a SIGSYS
			 */
			int fd, rc = EXIT_SUCCESS;

			stress_process_dumpable(false);
			VOID_RET(int, stress_sighandler(args->name, SIGSYS, stress_sigsys, NULL));

			(void)stress_seccomp_set_huge_filter(args);
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
			if (shim_waitpid(pid, &status, 0) < 0) {
				if (errno != EINTR)
					pr_dbg("%s: waitpid() on PID %" PRIdMAX " failed, errno=%d (%s)\n",
						args->name, (intmax_t)pid, errno, strerror(errno));
			} else {
				/* Did the child hit a weird error? */
				if (WIFEXITED(status) &&
				    (WEXITSTATUS(status) != EXIT_TRAPPED) &&
				    (WEXITSTATUS(status) != EXIT_SUCCESS)) {
					pr_fail("%s: aborting because of unexpected "
						"failure in child process\n", args->name);
					return EXIT_FAILURE;
				}
				/* ..exited OK but we expected trapped SIGSYS death? */
				if (WIFEXITED(status) && !allow_write &&
				    (WEXITSTATUS(status) != EXIT_TRAPPED)) {
					pr_fail("%s: expecting SIGSYS seccomp trap "
						"but got a successful exit which "
						"was not expected\n",
						args->name);
					return EXIT_FAILURE;
				}
				/* ..exited with a SIGSYS but we expected OK exit? */
				if (WIFSIGNALED(status) && allow_write) {
					if (WTERMSIG(status) == SIGSYS) {
						pr_fail("%s: expecting a successful exit "
							"but got a seccomp SIGSYS "
							"which was not expected\n",
							args->name);
						return EXIT_FAILURE;
					}
				}
			}
		}
		stress_bogo_inc(args);
	} while (stress_continue(args));

	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);

	return EXIT_SUCCESS;
}

const stressor_info_t stress_seccomp_info = {
	.stressor = stress_seccomp,
	.supported = stress_seccomp_supported,
	.classifier = CLASS_OS,
	.verify = VERIFY_ALWAYS,
	.help = help
};
#else
const stressor_info_t stress_seccomp_info = {
	.stressor = stress_unimplemented,
	.classifier = CLASS_OS,
	.verify = VERIFY_ALWAYS,
	.help = help,
	.unimplemented_reason = "built without linux/seccomp.h, linux/audit.h, linux/filter.h or prctl()"
};
#endif
