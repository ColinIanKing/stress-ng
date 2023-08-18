// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2013-2021 Canonical, Ltd.
 * Copyright (C) 2022-2023 Colin Ian King.
 *
 */
#include "stress-ng.h"
#include "core-builtin.h"
#include "core-capabilities.h"
#include "core-net.h"

#if defined(HAVE_SYS_EPOLL_H)
#include <sys/epoll.h>
#endif

#if defined(__NR_kcmp)
#define HAVE_KCMP
#endif

static const stress_help_t help[] = {
	{ NULL,	"kcmp N",	"start N workers exercising kcmp" },
	{ NULL,	"kcmp-ops N",	"stop after N kcmp bogo operations" },
	{ NULL, NULL,		NULL }
};

#if defined(HAVE_KCMP)

/* Urgh, should be from linux/kcmp.h */
enum {
	SHIM_KCMP_FILE,
	SHIM_KCMP_VM,
	SHIM_KCMP_FILES,
	SHIM_KCMP_FS,
	SHIM_KCMP_SIGHAND,
	SHIM_KCMP_IO,
	SHIM_KCMP_SYSVSEM,
	SHIM_KCMP_EPOLL_TFD,

	SHIM_KCMP_TYPES,
};

#if defined(HAVE_SYS_EPOLL_H) &&	\
    NEED_GLIBC(2,3,2)

/* Slot for KCMP_EPOLL_TFD */
struct shim_kcmp_epoll_slot {
	uint32_t efd;
	uint32_t tfd;
	uint32_t toff;
};
#endif

#define SHIM_KCMP(pid1, pid2, type, idx1, idx2)				\
	(int)shim_kcmp((pid_t)pid1, (pid_t)pid2, (int)type,		\
		  (unsigned long)idx1, (unsigned long)idx2)		\

#define KCMP(pid1, pid2, type, idx1, idx2)				\
do {									\
	int rc = SHIM_KCMP(pid1, pid2, type, idx1, idx2);		\
									\
	if (UNLIKELY(rc < 0)) {	 					\
		if (errno == EPERM) {					\
			pr_inf("%s: %s", capfail, args->name);		\
			goto reap;					\
		}							\
		if ((errno != EINVAL) &&				\
		    (errno != ENOSYS) &&				\
		    (errno != EBADF)) {					\
			pr_fail("%s: kcmp " # type " failed, "		\
				"errno=%d (%s)\n", args->name,		\
				errno, strerror(errno));		\
			ret = EXIT_FAILURE;				\
		}							\
	}								\
} while (0)

#define KCMP_VERIFY(pid1, pid2, type, idx1, idx2, res)			\
do {									\
	int rc = SHIM_KCMP(pid1, pid2, type, idx1, idx2);		\
									\
	if (UNLIKELY(rc != res)) {					\
		if (rc < 0) {						\
			if (errno == EPERM) {				\
				pr_inf("%s: %s", capfail, args->name);	\
				goto reap;				\
			}						\
			if ((errno != EINVAL) &&			\
			    (errno != ENOSYS) &&			\
			    (errno != EBADF)) {				\
				pr_fail("%s: kcmp " # type " failed, "	\
					"errno=%d (%s)\n", args->name,	\
					errno, strerror(errno));	\
				ret = EXIT_FAILURE;			\
			}						\
		} else {						\
			pr_fail( "%s: kcmp " # type			\
			" returned %d, expected: %d\n",			\
			args->name, rc, ret);				\
		}							\
	}								\
	if (!stress_continue_flag())					\
		goto reap;						\
} while (0)

/*
 *  stress_kcmp
 *	stress sys_kcmp
 */
static int stress_kcmp(const stress_args_t *args)
{
	pid_t pid1;
	int fd1;

#if defined(HAVE_SYS_EPOLL_H) &&	\
    NEED_GLIBC(2,3,2)
	int efd, sfd;
	int so_reuseaddr = 1;
	struct epoll_event ev;
	struct sockaddr *addr = NULL;
	socklen_t addr_len = 0;
	const pid_t mypid = getpid();
#endif
	int ret = EXIT_SUCCESS;
	const int bad_fd = stress_get_bad_fd();
	const bool is_root = stress_check_capability(SHIM_CAP_IS_ROOT);

	static const char *capfail =
		"need CAP_SYS_PTRACE capability to run kcmp stressor, "
		"aborting stress test\n";

	if ((fd1 = open("/dev/null", O_WRONLY)) < 0) {
		pr_fail("%s: open /dev/null failed, errno=%d (%s)\n",
			args->name, errno, strerror(errno));
		return EXIT_FAILURE;
	}

#if defined(HAVE_SYS_EPOLL_H) &&	\
    NEED_GLIBC(2,3,2)
	efd = -1;
	if ((sfd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
		sfd = -1;
		goto again;
	}
	if (setsockopt(sfd, SOL_SOCKET, SO_REUSEADDR,
			&so_reuseaddr, sizeof(so_reuseaddr)) < 0) {
		(void)close(sfd);
		sfd = -1;
		goto again;
	}
	if (stress_set_sockaddr(args->name, args->instance, mypid,
		AF_INET, 23000, &addr, &addr_len, NET_ADDR_ANY) < 0) {
		(void)close(sfd);
		sfd = -1;
		goto again;
	}

	if (bind(sfd, addr, addr_len) < 0) {
		(void)close(sfd);
		sfd = -1;
		goto again;
	}
	if (listen(sfd, SOMAXCONN) < 0) {
		(void)close(sfd);
		sfd = -1;
		goto again;
	}

	efd = epoll_create1(0);
	if (efd < 0) {
		(void)close(sfd);
		sfd = -1;
		efd = -1;
		goto again;
	}

	(void)shim_memset(&ev, 0, sizeof(ev));
	ev.data.fd = efd;
	ev.events = EPOLLIN | EPOLLET;
	if (epoll_ctl(efd, EPOLL_CTL_ADD, sfd, &ev) < 0) {
		(void)close(sfd);
		(void)close(efd);
		sfd = -1;
		efd = -1;
	}
#endif

	stress_set_proc_state(args->name, STRESS_STATE_RUN);
again:
	pid1 = fork();
	if (pid1 < 0) {
		if (stress_redo_fork(args, errno))
			goto again;
		(void)close(fd1);
		if (!stress_continue(args))
			goto finish;
		pr_fail("%s: fork failed, errno=%d (%s)\n",
			args->name, errno, strerror(errno));
#if defined(HAVE_SYS_EPOLL_H) &&	\
    NEED_GLIBC(2,3,2)
		if (sfd != -1)
			(void)close(sfd);
#endif
		return EXIT_FAILURE;
	} else if (pid1 == 0) {
		stress_parent_died_alarm();
		(void)sched_settings_apply(true);

		/* Child */
		while (stress_continue_flag())
			(void)pause();

		/* will never get here */
		(void)close(fd1);
#if defined(HAVE_SYS_EPOLL_H) &&	\
    NEED_GLIBC(2,3,2)
		if (efd != -1)
			(void)close(efd);
		if (sfd != -1)
			(void)close(sfd);
#endif
		_exit(EXIT_SUCCESS);
	} else {
		/* Parent */
		int fd2, status, pid2;
		const bool verify = !!(g_opt_flags & OPT_FLAGS_VERIFY);

		pid2 = getpid();
		if ((fd2 = open("/dev/null", O_WRONLY)) < 0) {
			pr_fail("%s: open /dev/null failed, errno=%d (%s)\n",
				args->name, errno, strerror(errno));
			ret = EXIT_FAILURE;
			goto reap;
		}

		do {
			KCMP(pid1, pid2, SHIM_KCMP_FILE, fd1, fd2);
			KCMP(pid1, pid1, SHIM_KCMP_FILE, fd1, fd1);
			KCMP(pid2, pid2, SHIM_KCMP_FILE, fd1, fd1);
			KCMP(pid2, pid2, SHIM_KCMP_FILE, fd2, fd2);

			KCMP(pid1, pid2, SHIM_KCMP_FILES, 0, 0);
			KCMP(pid1, pid1, SHIM_KCMP_FILES, 0, 0);
			KCMP(pid2, pid2, SHIM_KCMP_FILES, 0, 0);

			KCMP(pid1, pid2, SHIM_KCMP_FS, 0, 0);
			KCMP(pid1, pid1, SHIM_KCMP_FS, 0, 0);
			KCMP(pid2, pid2, SHIM_KCMP_FS, 0, 0);

			KCMP(pid1, pid2, SHIM_KCMP_IO, 0, 0);
			KCMP(pid1, pid1, SHIM_KCMP_IO, 0, 0);
			KCMP(pid2, pid2, SHIM_KCMP_IO, 0, 0);

			KCMP(pid1, pid2, SHIM_KCMP_SIGHAND, 0, 0);
			KCMP(pid1, pid1, SHIM_KCMP_SIGHAND, 0, 0);
			KCMP(pid2, pid2, SHIM_KCMP_SIGHAND, 0, 0);

			KCMP(pid1, pid2, SHIM_KCMP_SYSVSEM, 0, 0);
			KCMP(pid1, pid1, SHIM_KCMP_SYSVSEM, 0, 0);
			KCMP(pid2, pid2, SHIM_KCMP_SYSVSEM, 0, 0);

			KCMP(pid1, pid2, SHIM_KCMP_VM, 0, 0);
			KCMP(pid1, pid1, SHIM_KCMP_VM, 0, 0);
			KCMP(pid2, pid2, SHIM_KCMP_VM, 0, 0);

#if defined(HAVE_SYS_EPOLL_H) &&	\
    NEED_GLIBC(2,3,2)
			if ((sfd != -1) && (efd != -1)) {
				struct shim_kcmp_epoll_slot slot;

				slot.efd = (uint32_t)efd;
				slot.tfd = (uint32_t)sfd;
				slot.toff = (uint32_t)0;
				KCMP(pid1, pid2, SHIM_KCMP_EPOLL_TFD, efd, (unsigned long)&slot);
				KCMP(pid2, pid1, SHIM_KCMP_EPOLL_TFD, efd, (unsigned long)&slot);
				KCMP(pid2, pid2, SHIM_KCMP_EPOLL_TFD, efd, (unsigned long)&slot);
			}
#endif

			/* Same simple checks */
			if (verify) {
				KCMP_VERIFY(pid1, pid1, SHIM_KCMP_FILE, fd1, fd1, 0);
				KCMP_VERIFY(pid1, pid1, SHIM_KCMP_FILES, 0, 0, 0);
				KCMP_VERIFY(pid1, pid1, SHIM_KCMP_FS, 0, 0, 0);
				KCMP_VERIFY(pid1, pid1, SHIM_KCMP_IO, 0, 0, 0);
				KCMP_VERIFY(pid1, pid1, SHIM_KCMP_SIGHAND, 0, 0, 0);
				KCMP_VERIFY(pid1, pid1, SHIM_KCMP_SYSVSEM, 0, 0, 0);
				KCMP_VERIFY(pid1, pid1, SHIM_KCMP_VM, 0, 0, 0);
				KCMP_VERIFY(pid1, pid2, SHIM_KCMP_SYSVSEM, 0, 0, 0);
#if defined(HAVE_SYS_EPOLL_H) &&	\
    NEED_GLIBC(2,3,2)
				if ((sfd != -1) && (efd != -1)) {
					struct shim_kcmp_epoll_slot slot;

					slot.efd = (uint32_t)efd;
					slot.tfd = (uint32_t)sfd;
					slot.toff = (uint32_t)0;
					KCMP(pid1, pid2, SHIM_KCMP_EPOLL_TFD, efd, (unsigned long)&slot);
				}
#endif
			}
			/*
			 *  Exercise kcmp with some invalid calls to
			 *  get more kernel error handling coverage
			 */
			(void)SHIM_KCMP(pid1, pid2, 0x7fffffff, 0, 0);
			(void)SHIM_KCMP(pid1, pid2, SHIM_KCMP_FILE, bad_fd, fd1);
			(void)SHIM_KCMP(pid1, INT_MAX, SHIM_KCMP_FILE, fd1, fd2);
			(void)SHIM_KCMP(INT_MAX, pid2, SHIM_KCMP_FILE, fd1, fd2);
			if (!is_root)
				(void)SHIM_KCMP(1, pid2, SHIM_KCMP_SIGHAND, 0, 0);

			stress_bogo_inc(args);
		} while (stress_continue(args));
reap:
		if (fd2 >= 0)
			(void)close(fd2);
		(void)shim_kill(pid1, SIGKILL);
		(void)shim_waitpid(pid1, &status, 0);
		(void)close(fd1);
	}
finish:
	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);

#if defined(HAVE_SYS_EPOLL_H) &&	\
    NEED_GLIBC(2,3,2)
	if (efd != -1)
		(void)close(efd);
	if (sfd != -1)
		(void)close(sfd);
#endif
	return ret;
}

stressor_info_t stress_kcmp_info = {
	.stressor = stress_kcmp,
	.class = CLASS_OS,
	.verify = VERIFY_OPTIONAL,
	.help = help
};
#else
stressor_info_t stress_kcmp_info = {
	.stressor = stress_unimplemented,
	.class = CLASS_OS,
	.verify = VERIFY_OPTIONAL,
	.help = help,
	.unimplemented_reason = "built without kcmp() system call support"
};
#endif
