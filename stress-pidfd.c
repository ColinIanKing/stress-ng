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
	{ NULL,	"pidfd N",	"start N workers exercising pidfd system call" },
	{ NULL,	"pidfd-ops N",	"stop after N pidfd bogo operations" },
	{ NULL,	NULL,		NULL }
};

#if defined(HAVE_PIDFD_SEND_SIGNAL)

static int stress_pidfd_open_fd(pid_t pid)
{
	char buffer[1024];
			
	(void)snprintf(buffer, sizeof(buffer), "/proc/%d", pid);
	return open(buffer, O_DIRECTORY | O_CLOEXEC);
}

static int stress_pidfd_supported(void)
{
	int pidfd, ret;
	const pid_t pid = getpid();

	pidfd = stress_pidfd_open_fd(pid);
	if (pidfd < 0) {
		pr_inf("pidfd stressor will be skipped, cannot open proc entry on procfs\n");
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
static int stress_pidfd(const args_t *args)
{
	while (keep_stressing()) {
		pid_t pid;

		pid = fork();
		if (pid < 0) {
			if (g_keep_stressing_flag && (errno == EAGAIN))
				continue;
			pr_fail_dbg("fork");
			return EXIT_FAILURE;
		} else if (pid == 0) {
			(void)setpgid(0, g_pgrp);

			pause();
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

static int stress_pidfd_supported(void)
{
	pr_inf("pidfd stressor will be skipped, system call not supported at build time\n");
	return -1;
}
	
stressor_info_t stress_pidfd_info = {
	.stressor = stress_not_implemented,
	.class = CLASS_INTERRUPT | CLASS_OS,
	.supported = stress_pidfd_supported,
	.help = help
};
#endif
