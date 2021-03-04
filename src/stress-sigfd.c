/*
 * Copyright (C) 2013-2021 Canonical, Ltd.
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
	{ NULL,	"sigfd N",	"start N workers reading signals via signalfd reads " },
	{ NULL,	"sigfd-ops N",	"stop after N bogo signalfd reads" },
	{ NULL,	NULL,		NULL }
};

#if defined(HAVE_SYS_SIGNALFD_H) && 	\
    defined(HAVE_SIGNALFD) &&		\
    NEED_GLIBC(2,8,0) && 		\
    defined(HAVE_SIGQUEUE)

/*
 *  stress_sigfd
 *	stress signalfd reads
 */
static int stress_sigfd(const stress_args_t *args)
{
	pid_t pid, ppid = args->pid;
	int sfd;
	const int bad_fd = stress_get_bad_fd();
	sigset_t mask;

	(void)sigemptyset(&mask);
	(void)sigaddset(&mask, SIGRTMIN);
	if (sigprocmask(SIG_BLOCK, &mask, NULL) < 0) {
		pr_fail("%s: sigprocmask failed, errno=%d (%s)\n",
			args->name, errno, strerror(errno));
		return EXIT_FAILURE;
	}

	/*
	 *  Exercise with a bad fd
	 */
	sfd = signalfd(bad_fd, &mask, 0);
	if (sfd >= 0)
		(void)close(sfd);
	/*
	 *  Exercise with bad flags
	 */
	sfd = signalfd(-1, &mask, ~0);
	if (sfd >= 0)
		(void)close(sfd);
	/*
	 *  Exercise with invalid fd (stdout)
	 */
	sfd = signalfd(fileno(stdout), &mask, 0);
	if (sfd >= 0)
		(void)close(sfd);

	sfd = signalfd(-1, &mask, 0);
	if (sfd < 0) {
		pr_fail("%s: signalfd failed, errno=%d (%s)\n",
			args->name, errno, strerror(errno));
		return EXIT_FAILURE;
	}

	stress_set_proc_state(args->name, STRESS_STATE_RUN);
again:
	pid = fork();
	if (pid < 0) {
		if (keep_stressing_flag() && (errno == EAGAIN))
			goto again;
		pr_fail("%s: fork failed, errno=%d (%s)\n",
			args->name, errno, strerror(errno));
		return EXIT_FAILURE;
	} else if (pid == 0) {
		int val = 0;

		(void)setpgid(0, g_pgrp);
		stress_parent_died_alarm();
		(void)sched_settings_apply(true);

		while (keep_stressing_flag()) {
			union sigval s;
			int ret;

			(void)memset(&s, 0, sizeof(s));
			s.sival_int = val++;
			ret = sigqueue(ppid, SIGRTMIN, s);
			if ((ret < 0) && (errno != EAGAIN)) {
				break;
			}

		}
		(void)close(sfd);
		_exit(0);
	} else {
		/* Parent */
		int status;
		const pid_t self = getpid();

		(void)setpgid(pid, g_pgrp);

		do {
			int ret;
			struct signalfd_siginfo fdsi;

			ret = read(sfd, &fdsi, sizeof(fdsi));
			if ((ret < 0) || (ret != sizeof(fdsi))) {
				if ((errno == EAGAIN) || (errno == EINTR))
					continue;
				if (errno) {
					pr_fail("%s: read failed, errno=%d (%s)\n",
						args->name, errno, strerror(errno));
					(void)close(sfd);
					_exit(EXIT_FAILURE);
				}
				continue;
			}
			if (ret == 0) 
				break;
			if (g_opt_flags & OPT_FLAGS_VERIFY) {
				if (fdsi.ssi_signo != (uint32_t)SIGRTMIN) {
					pr_fail("%s: unexpected signal %d\n",
						args->name, fdsi.ssi_signo);
					break;
				}
			}
			/*
			 *  periodically exercise the /proc info for
			 *  the signal fd to exercise the sigmask setting
			 *  for this specific kind of fd info.
			 */
			if ((fdsi.ssi_int & 0xffff) == 0)
				(void)stress_read_fdinfo(self, sfd);

			inc_counter(args);
		} while (keep_stressing(args));

		/* terminal child */
		(void)kill(pid, SIGKILL);
		(void)shim_waitpid(pid, &status, 0);
	}

	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);

	return EXIT_SUCCESS;
}

stressor_info_t stress_sigfd_info = {
	.stressor = stress_sigfd,
	.class = CLASS_INTERRUPT | CLASS_OS,
	.help = help
};
#else
stressor_info_t stress_sigfd_info = {
	.stressor = stress_not_implemented,
	.class = CLASS_INTERRUPT | CLASS_OS,
	.help = help
};
#endif
