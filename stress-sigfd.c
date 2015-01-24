/*
 * Copyright (C) 2013-2015 Canonical, Ltd.
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

#if defined(__linux__)

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>
#include <sys/signalfd.h>

#include "stress-ng.h"

/*
 *  stress_sigfd
 *	stress signalfd reads
 */
int stress_sigfd(
	uint64_t *const counter,
	const uint32_t instance,
	const uint64_t max_ops,
	const char *name)
{
	pid_t pid, ppid = getpid();
	int sfd;
	sigset_t mask;

	(void)instance;

	sigemptyset(&mask);
	sigaddset(&mask, SIGUSR1);
	if (sigprocmask(SIG_BLOCK, &mask, NULL) < 0) {
		pr_failed_dbg(name, "sigprocmask");
		_exit(EXIT_FAILURE);
	}
	sfd = signalfd(-1, &mask, 0);
	if (sfd < 0) {
		pr_failed_dbg(name, "signalfd");
		_exit(EXIT_FAILURE);
	}

	pid = fork();
	if (pid < 0) {
		pr_failed_dbg(name, "fork");
		return EXIT_FAILURE;
	} else if (pid == 0) {
		for (;;) {
			union sigval s;
			int ret;

			memset(&s, 0, sizeof(s));
			s.sival_int = 0;
			ret = sigqueue(ppid, SIGUSR1, s);
			if (ret < 0)
				break;
		}
		(void)close(sfd);
		_exit(0);
	} else {
		/* Parent */
		int status;

		do {
			int ret;
			struct signalfd_siginfo fdsi;

			ret = read(sfd, &fdsi, sizeof(fdsi));
			if (ret == 0)
				break;
			if (ret != sizeof(fdsi)) {
				pr_failed_dbg(name, "signalfd read");
				(void)close(sfd);
				_exit(EXIT_FAILURE);
			}
			if (opt_flags & OPT_FLAGS_VERIFY) {
				if (fdsi.ssi_signo != SIGUSR1) {
					pr_fail(stderr, "%s: unexpected signal %d",
						name, fdsi.ssi_signo);
					break;
				}
			}
			(*counter)++;
		} while (opt_do_run && (!max_ops || *counter < max_ops));

		/* terminal child */
		(void)kill(pid, SIGKILL);
		(void)waitpid(pid, &status, 0);
	}
	return EXIT_SUCCESS;
}
#endif
