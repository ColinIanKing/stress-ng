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

static const help_t help[] = {
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

/* Slot for KCMP_EPOLL_TFD */
struct kcmp_epoll_slot {
	uint32_t efd;
	uint32_t tfd;
	uint32_t toff;
};

#define KCMP(pid1, pid2, type, idx1, idx2)			\
{								\
	int rc = shim_kcmp(pid1, pid2, type, idx1, idx2);	\
								\
	if (rc < 0) {	 					\
		if (errno == EPERM) {				\
			pr_inf("%s: %s", capfail, args->name);	\
			break;					\
		}						\
		if (errno != EINVAL)				\
			pr_fail_err("kcmp: " # type);		\
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
				pr_inf("%s: %s", capfail, args->name);	\
				break;				\
			}					\
			if (errno != EINVAL)			\
				pr_fail_err("kcmp: " # type);	\
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
static int stress_kcmp(const args_t *args)
{
	pid_t pid1;
	int fd1;

#if defined(HAVE_SYS_EPOLL_H) && NEED_GLIBC(2,3,2)
	int efd, sfd;
	int so_reuseaddr = 1;
	struct epoll_event ev;
	struct sockaddr *addr = NULL;
	socklen_t addr_len = 0;
#endif
	int ret = EXIT_SUCCESS;

	static const char *capfail =
		"need CAP_SYS_PTRACE capability to run kcmp stressor, "
		"aborting stress test\n";

	if ((fd1 = open("/dev/null", O_WRONLY)) < 0) {
		pr_fail_err("open");
		return EXIT_FAILURE;
	}

#if defined(HAVE_SYS_EPOLL_H) && NEED_GLIBC(2,3,2)
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
	stress_set_sockaddr(args->name, args->instance, args->ppid,
		AF_INET, 23000, &addr, &addr_len, NET_ADDR_ANY);

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

	(void)memset(&ev, 0, sizeof(ev));
	ev.data.fd = efd;
	ev.events = EPOLLIN | EPOLLET;
	if (epoll_ctl(efd, EPOLL_CTL_ADD, sfd, &ev) < 0) {
		(void)close(sfd);
		(void)close(efd);
		sfd = -1;
		efd = -1;
	}
#endif

again:
	pid1 = fork();
	if (pid1 < 0) {
		if (g_keep_stressing_flag &&
		    ((errno == EAGAIN) || (errno == ENOMEM)))
			goto again;

		pr_fail_dbg("fork");
		(void)close(fd1);
#if defined(HAVE_SYS_EPOLL_H) && NEED_GLIBC(2,3,2)
		if (sfd != -1)
			(void)close(sfd);
#endif
		return EXIT_FAILURE;
	} else if (pid1 == 0) {
		(void)setpgid(0, g_pgrp);
		stress_parent_died_alarm();

		/* Child */
		while (g_keep_stressing_flag)
			(void)pause();

		/* will never get here */
		(void)close(fd1);
#if defined(HAVE_SYS_EPOLL_H) && NEED_GLIBC(2,3,2)
		if (efd != -1)
			(void)close(efd);
		if (sfd != -1)
			(void)close(sfd);
#endif
		_exit(EXIT_SUCCESS);
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

#if defined(HAVE_SYS_EPOLL_H) && NEED_GLIBC(2,3,2)
			if (efd != -1) {
				struct kcmp_epoll_slot slot;

				slot.efd = efd;
				slot.tfd = sfd;
				slot.toff = 0;
				KCMP(pid1, pid2, SHIM_KCMP_EPOLL_TFD, efd, (unsigned long)&slot);
				KCMP(pid2, pid1, SHIM_KCMP_EPOLL_TFD, efd, (unsigned long)&slot);
				KCMP(pid2, pid2, SHIM_KCMP_EPOLL_TFD, efd, (unsigned long)&slot);
			}
#endif

			/* Same simple checks */
			if (g_opt_flags & OPT_FLAGS_VERIFY) {
				KCMP_VERIFY(pid1, pid1, SHIM_KCMP_FILE, fd1, fd1, 0);
				KCMP_VERIFY(pid1, pid1, SHIM_KCMP_FILES, 0, 0, 0);
				KCMP_VERIFY(pid1, pid1, SHIM_KCMP_FS, 0, 0, 0);
				KCMP_VERIFY(pid1, pid1, SHIM_KCMP_IO, 0, 0, 0);
				KCMP_VERIFY(pid1, pid1, SHIM_KCMP_SIGHAND, 0, 0, 0);
				KCMP_VERIFY(pid1, pid1, SHIM_KCMP_SYSVSEM, 0, 0, 0);
				KCMP_VERIFY(pid1, pid1, SHIM_KCMP_VM, 0, 0, 0);
				KCMP_VERIFY(pid1, pid2, SHIM_KCMP_SYSVSEM, 0, 0, 0);
#if defined(HAVE_SYS_EPOLL_H) && NEED_GLIBC(2,3,2)
				if (efd != -1) {
					struct kcmp_epoll_slot slot;

					slot.efd = efd;
					slot.tfd = sfd;
					slot.toff = 0;
					KCMP(pid1, pid2, SHIM_KCMP_EPOLL_TFD, efd, (unsigned long)&slot);
				}
#endif
			}
			inc_counter(args);
		} while (keep_stressing());
reap:
		if (fd2 >= 0)
			(void)close(fd2);
		(void)kill(pid1, SIGKILL);
		(void)shim_waitpid(pid1, &status, 0);
		(void)close(fd1);
	}
#if defined(HAVE_SYS_EPOLL_H) && NEED_GLIBC(2,3,2)
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
	.help = help
};
#else
stressor_info_t stress_kcmp_info = {
	.stressor = stress_not_implemented,
	.class = CLASS_OS,
	.help = help
};
#endif
