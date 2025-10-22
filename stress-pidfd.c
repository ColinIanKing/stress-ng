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
#include "core-killpid.h"

static const stress_help_t help[] = {
	{ NULL,	"pidfd N",	"start N workers exercising pidfd system call" },
	{ NULL,	"pidfd-ops N",	"stop after N pidfd bogo operations" },
	{ NULL,	NULL,		NULL }
};

#if defined(HAVE_PIDFD_SEND_SIGNAL)

static int stress_pidfd_open(const pid_t pid, const unsigned int flag)
{
	int fd;
	const pid_t bad_pid = stress_get_unused_pid_racy(false);

	/* Exercise pidfd_open with non-existent PID */
	fd = shim_pidfd_open(bad_pid, 0);
	if (UNLIKELY(fd >= 0))
		(void)close(fd);

	/* Exercise pidfd_open with illegal flags */
	(void)shim_pidfd_open(pid, ~(1U));

	/* Exercise pidfd_open with illegal PID */
	(void)shim_pidfd_open((pid_t)-1, 0);

	fd = -1;
	/* Randomly try pidfd_open first */
	if (stress_mwc1()) {
		fd = shim_pidfd_open(pid, flag);
	}
	/* ..or fallback to open on /proc/$PID */
	if (fd < 0) {
		char buffer[1024];
		int o_flags = O_DIRECTORY | O_CLOEXEC;

		(void)snprintf(buffer, sizeof(buffer), "/proc/%" PRIdMAX, (intmax_t)pid);
#if defined(PIDFD_NONBLOCK)
		if (flag & PIDFD_NONBLOCK)
			o_flags |= O_NONBLOCK;
#endif
		fd = open(buffer, o_flags);
	}
	return fd;
}

static int stress_pidfd_supported(const char *name)
{
	int pidfd, ret;
	const pid_t pid = getpid();
	siginfo_t info;

	pidfd = stress_pidfd_open(pid, 0);
	if (pidfd < 0) {
		pr_inf_skip("%s stressor will be skipped, cannot open proc entry on procfs\n",
			name);
		return -1;
	}
	ret = shim_pidfd_send_signal(pidfd, 0, NULL, 0);
	if (ret < 0) {
		if (errno == ENOSYS) {
			pr_inf_skip("pidfd stressor will be skipped, system call not implemented\n");
			(void)close(pidfd);
			return -1;
		}
		/* Something went wrong, but don't let stressor fail on that */
	}

	/* initialized info to be safe */
	(void)shim_memset(&info, 0, sizeof(info));

	/*
	 * Exercise pidfd_send_signal with
	 * non-null pointer to info variable
	 */
	(void)shim_pidfd_send_signal(pidfd, 0, &info, 0);

	/* Exercise pidfd_send_signal with illegal flags */
	(void)shim_pidfd_send_signal(pidfd, 0, NULL, ~(1U));

	(void)close(pidfd);
	return 0;
}

static void stress_pidfd_reap(pid_t pid, int pidfd)
{
	if (pid)
		(void)stress_kill_pid_wait(pid, NULL);
	if (pidfd >= 0)
		(void)close(pidfd);
}

/*
 *  stress_pidfd
 *	stress signalfd reads
 */
static int stress_pidfd(stress_args_t *args)
{
	const int bad_fd = stress_get_bad_fd();
	int rc = EXIT_SUCCESS;

	stress_set_proc_state(args->name, STRESS_STATE_SYNC_WAIT);
	stress_sync_start_wait(args);
	stress_set_proc_state(args->name, STRESS_STATE_RUN);

	while ((rc == EXIT_SUCCESS) && stress_continue(args)) {
		pid_t pid;

again:
		pid = fork();
		if (pid < 0) {
			if (stress_redo_fork(args, errno))
				goto again;
			if (UNLIKELY(!stress_continue(args)))
				goto finish;
			pr_err("%s: fork failed, errno=%d (%s)\n",
				args->name, errno, strerror(errno));
			return EXIT_FAILURE;
		} else if (pid == 0) {
			stress_set_proc_state(args->name, STRESS_STATE_RUN);
			(void)shim_pause();
			_exit(0);
		} else {
			/* Parent */
			int pidfd, ret;
			struct stat statbuf;
			void *ptr;

#if defined(PIDFD_NONBLOCK)
			pidfd = stress_pidfd_open(pid, PIDFD_NONBLOCK);
			if (pidfd >= 0) {
#if defined(F_GETFL) && 0
				unsigned int flags;

				flags = fcntl(pidfd, F_GETFL, 0);
				if (UNLIKELY((flags & O_NONBLOCK) == 0)) {
					pr_fail("%s: pidfd_open opened using PIDFD_NONBLOCK "
						"but O_NONBLOCK is not set on the file\n",
						args->name);
					rc = EXIT_FAILURE;
				}
#endif
				(void)close(pidfd);
			}
#endif
			pidfd = stress_pidfd_open(pid, 0);
			if (pidfd < 0) {
				/* Process not found, try again */
				stress_pidfd_reap(pid, pidfd);
				continue;
			}

			/* Exercise fstat'ing the pidfd */
			VOID_RET(int, shim_fstat(pidfd, &statbuf));

			/* Exercise illegal mmap'ing the pidfd */
			ptr = mmap(NULL, args->page_size, PROT_READ,
				MAP_PRIVATE, pidfd, 0);
			if (UNLIKELY(ptr != MAP_FAILED))
				(void)munmap(ptr, args->page_size);

			/* Try to get fd 0 on child pid */
			ret = shim_pidfd_getfd(pidfd, 0, 0);
			if (ret >= 0)
				(void)close(ret);

			/* Exercise with invalid flags */
			ret = shim_pidfd_getfd(pidfd, 0, ~0U);
			if (UNLIKELY(ret >= 0))
				(void)close(ret);

			/* Exercise with bad_fd */
			ret = shim_pidfd_getfd(pidfd, bad_fd, 0);
			if (UNLIKELY(ret >= 0))
				(void)close(ret);

			ret = shim_pidfd_send_signal(pidfd, 0, NULL, 0);
			if (UNLIKELY(ret != 0)) {
				if (errno == ENOSYS) {
					if (stress_instance_zero(args))
						pr_inf_skip("%s: skipping stressor, system call is not implemented\n",
							args->name);
					stress_pidfd_reap(pid, pidfd);
					return EXIT_NOT_IMPLEMENTED;
				}
				pr_fail("%s: pidfd_send_signal failed, errno=%d (%s)\n",
					args->name, errno, strerror(errno));
				rc = EXIT_FAILURE;
				stress_pidfd_reap(pid, pidfd);
				break;
			}
			ret = shim_pidfd_send_signal(pidfd, SIGSTOP, NULL, 0);
			if (UNLIKELY(ret != 0)) {
				pr_fail("%s: pidfd_send_signal (SIGSTOP), failed, errno=%d (%s)\n",
					args->name, errno, strerror(errno));
				rc = EXIT_FAILURE;
			}
			ret = shim_pidfd_send_signal(pidfd, SIGCONT, NULL, 0);
			if (UNLIKELY(ret != 0)) {
				pr_fail("%s: pidfd_send_signal (SIGCONT), failed, errno=%d (%s)\n",
					args->name, errno, strerror(errno));
				rc = EXIT_FAILURE;
			}
#if defined(PIDFD_SELF_THREAD)
			(void)shim_pidfd_send_signal(PIDFD_SELF_THREAD, 0, NULL, 0);
#endif
#if defined(PIDFD_SELF_THREAD_GROUP)
			(void)shim_pidfd_send_signal(PIDFD_SELF_THREAD_GROUP, 0, NULL, 0);
#endif
#if defined(FD_PIDFS_ROOT)
			(void)shim_pidfd_send_signal(FD_PIDFS_ROOT, 0, NULL, 0);
#endif
#if defined(FD_INVALID)
			/* should return EBADF */
			(void)shim_pidfd_send_signal(FD_INVALID, 0, NULL, 0);
#endif
			stress_pidfd_reap(pid, pidfd);
		}
		stress_bogo_inc(args);
	}
finish:
	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);

	return rc;
}

const stressor_info_t stress_pidfd_info = {
	.stressor = stress_pidfd,
	.classifier = CLASS_INTERRUPT | CLASS_OS,
	.supported = stress_pidfd_supported,
	.verify = VERIFY_ALWAYS,
	.help = help
};
#else

static int stress_pidfd_supported(const char *name)
{
	pr_inf_skip("%s: stressor will be skipped, system call not supported at build time\n", name);
	return -1;
}

const stressor_info_t stress_pidfd_info = {
	.stressor = stress_unimplemented,
	.classifier = CLASS_INTERRUPT | CLASS_OS,
	.supported = stress_pidfd_supported,
	.verify = VERIFY_ALWAYS,
	.help = help,
	.unimplemented_reason = "built without pidfd_send_signal() system call"
};
#endif
