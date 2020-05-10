/*
 * Copyright (C) 2013-2020 Canonical, Ltd.
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

static const stress_help_t help[] = {
	{ NULL,	"pidfd N",	"start N workers exercising pidfd system call" },
	{ NULL,	"pidfd-ops N",	"stop after N pidfd bogo operations" },
	{ NULL,	NULL,		NULL }
};

#if defined(HAVE_PIDFD_SEND_SIGNAL)

static int stress_pidfd_open_fd(pid_t pid)
{
	int fd = -1;

	/* Randomly try pidfd_open first */
	if (stress_mwc1()) {
		fd = shim_pidfd_open(pid, 0);
	}
	if (fd < 0) {
		char buffer[1024];

		/* ..or fallback to open on /proc/$PID */
		(void)snprintf(buffer, sizeof(buffer), "/proc/%d", pid);
		fd = open(buffer, O_DIRECTORY | O_CLOEXEC);
	}
	return fd;
}

static int stress_pidfd_supported(const char *name)
{
	int pidfd, ret;
	const pid_t pid = getpid();

	pidfd = stress_pidfd_open_fd(pid);
	if (pidfd < 0) {
		pr_inf("%s stressor will be skipped, cannot open proc entry on procfs\n",
			name);
		return -1;
	}
	ret = shim_pidfd_send_signal(pidfd, 0, NULL, 0);
	if (ret < 0) {
		if (errno == ENOSYS) {
			pr_inf("pidfd stressor will be skipped, system call not implemented\n");
			(void)close(pidfd);
			return -1;
		}
		/* Something went wrong, but let stressor fail on that */
	}
	(void)close(pidfd);
	return 0;
}

static void stress_pidfd_reap(pid_t pid, int pidfd)
{
	int status;

	if (pidfd >= 0)
		(void)close(pidfd);
	if (pid) {
		(void)kill(pid, SIGKILL);
		(void)shim_waitpid(pid, &status, 0);
	}
}

/*
 *  stress_pidfd
 *	stress signalfd reads
 */
static int stress_pidfd(const stress_args_t *args)
{
	while (keep_stressing()) {
		pid_t pid;

		pid = fork();
		if (pid < 0) {
			if (keep_stressing_flag() && (errno == EAGAIN))
				continue;
			pr_fail("%s: fork failed, errno=%d (%s)\n",
				args->name, errno, strerror(errno));
			return EXIT_FAILURE;
		} else if (pid == 0) {
			(void)setpgid(0, g_pgrp);
			(void)pause();
			_exit(0);
		} else {
			/* Parent */
			int pidfd, ret;

			pidfd = stress_pidfd_open_fd(pid);
			if (pidfd < 0) {
				/* Process not found, try again */
				stress_pidfd_reap(pid, pidfd);
				continue;
			}
			/* Try to get fd 0 on child pid */
			ret = shim_pidfd_getfd(pidfd, 0, 0);
			/* Ignore failures for now, need to sanity check this */
			if (ret >= 0)
				(void)close(ret);

			ret = shim_pidfd_send_signal(pidfd, 0, NULL, 0);
			if (ret != 0) {
				if (errno == ENOSYS) {
					pr_inf("%s: skipping stress test, system call is not implemented\n",
						args->name);
					stress_pidfd_reap(pid, pidfd);
					return EXIT_NOT_IMPLEMENTED;
				}
				pr_err("%s: pidfd_send_signal failed: errno=%d (%s)\n",
					args->name, errno, strerror(errno));
				stress_pidfd_reap(pid, pidfd);
				break;
			}
			ret = shim_pidfd_send_signal(pidfd, SIGSTOP, NULL, 0);
			if (ret != 0) {
				pr_err("%s: pidfd_send_signal (SIGSTOP), failed: errno=%d (%s)\n",
					args->name, errno, strerror(errno));
			}
			ret = shim_pidfd_send_signal(pidfd, SIGCONT, NULL, 0);
			if (ret != 0) {
				pr_err("%s: pidfd_send_signal (SIGCONT), failed: errno=%d (%s)\n",
					args->name, errno, strerror(errno));
			}
			stress_pidfd_reap(pid, pidfd);
		}
		inc_counter(args);
	}
	return EXIT_SUCCESS;
}

stressor_info_t stress_pidfd_info = {
	.stressor = stress_pidfd,
	.class = CLASS_INTERRUPT | CLASS_OS,
	.supported = stress_pidfd_supported,
	.help = help
};
#else

static int stress_pidfd_supported(const char *name)
{
	pr_inf("%s: stressor will be skipped, system call not supported at build time\n", name);
	return -1;
}

stressor_info_t stress_pidfd_info = {
	.stressor = stress_not_implemented,
	.class = CLASS_INTERRUPT | CLASS_OS,
	.supported = stress_pidfd_supported,
	.help = help
};
#endif
