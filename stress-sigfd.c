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
#include "core-affinity.h"
#include "core-builtin.h"
#include "core-killpid.h"

#if defined(HAVE_SYS_SIGNALFD_H)
#include <sys/signalfd.h>
#else
UNEXPECTED
#endif

static const stress_help_t help[] = {
	{ NULL,	"sigfd N",	"start N workers reading signals via signalfd reads " },
	{ NULL,	"sigfd-ops N",	"stop after N bogo signalfd reads" },
	{ NULL,	NULL,		NULL }
};

#if defined(HAVE_SYS_SIGNALFD_H) && 	\
    defined(HAVE_SIGNALFD) &&		\
    defined(HAVE_SIGQUEUE)

#if defined(__NR_signalfd4) &&		\
    defined(__linux__) &&		\
    defined(HAVE_SYSCALL)
#define HAVE_SIGNALFD4
static inline int shim_signalfd4(
	int ufd,
	sigset_t *user_mask,
	size_t sizemask,
	int flags)
{
	return (int)syscall(__NR_signalfd4, ufd, user_mask, sizemask, flags);
}
#endif

/*
 *  stress_sigfd
 *	stress signalfd reads
 */
static int stress_sigfd(stress_args_t *args)
{
	pid_t pid, ppid = args->pid;
	int sfd, parent_cpu, rc = EXIT_SUCCESS;
	const int bad_fd = stress_get_bad_fd();
	sigset_t mask;

	(void)sigemptyset(&mask);
	(void)sigaddset(&mask, SIGRTMIN);
	if (sigprocmask(SIG_BLOCK, &mask, NULL) < 0) {
		pr_err("%s: sigprocmask failed, errno=%d (%s)\n",
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
	/*
	 *  Exercise with invalid sizemask
	 */
#if defined(HAVE_SIGNALFD4)
	sfd = shim_signalfd4(-1, &mask, sizeof(mask) + 1, 0);
	if (sfd >= 0)
		(void)close(sfd);
#endif

	sfd = signalfd(-1, &mask, 0);
	if (sfd < 0) {
		pr_fail("%s: signalfd failed, errno=%d (%s)\n",
			args->name, errno, strerror(errno));
		return EXIT_FAILURE;
	}

	stress_set_proc_state(args->name, STRESS_STATE_SYNC_WAIT);
	stress_sync_start_wait(args);
	stress_set_proc_state(args->name, STRESS_STATE_RUN);
again:
	parent_cpu = stress_get_cpu();
	pid = fork();
	if (pid < 0) {
		if (stress_redo_fork(args, errno))
			goto again;
		if (UNLIKELY(!stress_continue(args)))
			goto finish;
		pr_err("%s: fork failed, errno=%d (%s)\n",
			args->name, errno, strerror(errno));
		(void)close(sfd);
		return EXIT_FAILURE;
	} else if (pid == 0) {
		int val = 0;
		union sigval s ALIGN64;

		stress_set_proc_state(args->name, STRESS_STATE_RUN);
		(void)stress_change_cpu(args, parent_cpu);
		stress_parent_died_alarm();
		(void)sched_settings_apply(true);

		(void)shim_memset(&s, 0, sizeof(s));

		while (stress_continue_flag()) {
			int ret;

			s.sival_int = val++;
			ret = sigqueue(ppid, SIGRTMIN, s);
			if (UNLIKELY((ret < 0) && (errno != EAGAIN)))
				break;
		}
		(void)close(sfd);
		_exit(0);
	} else {
		/* Parent */
		const pid_t self = getpid();
		const bool verify = !!(g_opt_flags & OPT_FLAGS_VERIFY);

		do {
			ssize_t ret;
			struct signalfd_siginfo fdsi ALIGN64;

			ret = read(sfd, &fdsi, sizeof(fdsi));
			if (UNLIKELY((ret < 0) || (ret != sizeof(fdsi)))) {
				if ((errno == EAGAIN) || (errno == EINTR))
					continue;
				if (errno) {
					pr_fail("%s: read failed, errno=%d (%s)\n",
						args->name, errno, strerror(errno));
					rc = EXIT_FAILURE;
					break;
				}
				continue;
			}
			if (UNLIKELY(ret == 0))
				break;
			if (UNLIKELY(verify)) {
				if (UNLIKELY(fdsi.ssi_signo != (uint32_t)SIGRTMIN)) {
					pr_fail("%s: unexpected signal %d\n",
						args->name, fdsi.ssi_signo);
					rc = EXIT_FAILURE;
					break;
				}
			}
			/*
			 *  periodically exercise the /proc info for
			 *  the signal fd to exercise the sigmask setting
			 *  for this specific kind of fd info.
			 */
			if (UNLIKELY((fdsi.ssi_int & 0xffff) == 0))
				(void)stress_read_fdinfo(self, sfd);

			stress_bogo_inc(args);
		} while (stress_continue(args));

		/* terminate child */
		(void)stress_kill_pid_wait(pid, NULL);
	}

finish:
	(void)close(sfd);
	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);

	return rc;
}

const stressor_info_t stress_sigfd_info = {
	.stressor = stress_sigfd,
	.classifier = CLASS_SIGNAL | CLASS_OS,
	.verify = VERIFY_OPTIONAL,
	.help = help
};
#else
const stressor_info_t stress_sigfd_info = {
	.stressor = stress_unimplemented,
	.classifier = CLASS_SIGNAL | CLASS_OS,
	.verify = VERIFY_OPTIONAL,
	.help = help,
	.unimplemented_reason = "built without sys/signalfd.h, signalfd() or sigqueue() system calls"
};
#endif
