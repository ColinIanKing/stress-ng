/*
 * Copyright (C) 2013-2021 Canonical, Ltd.
 * Copyright (C) 2021-2025 Colin Ian King.
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

#if defined(HAVE_SYS_EVENTFD_H)
#include <sys/eventfd.h>
#endif

static const stress_help_t help[] = {
	{ NULL,	"eventfd N",		"start N workers stressing eventfd read/writes" },
	{ NULL, "eventfs-nonblock",	"poll with non-blocking I/O on eventfd fd" },
	{ NULL,	"eventfd-ops N",	"stop eventfd workers after N bogo operations" },
	{ NULL,	NULL,			NULL }
};

static const stress_opt_t opts[] = {
	{ OPT_eventfd_nonblock, "eventfd-nonblock", TYPE_ID_BOOL, 0, 1, NULL },
	END_OPT,
};

#if defined(HAVE_SYS_EVENTFD_H) && \
    defined(HAVE_EVENTFD) && \
    NEED_GLIBC(2,8,0)

/*
 *  stress_eventfd
 *	stress eventfd read/writes
 */
static int stress_eventfd(stress_args_t *args)
{
	pid_t pid;
	int fd1, fd2, test_fd, rc;
	int flags = 0, parent_cpu;
	bool eventfd_nonblock = false;

	(void)stress_get_setting("eventfd-nonblock", &eventfd_nonblock);

#if defined(EFD_CLOEXEC)
	flags |= EFD_CLOEXEC;
#endif
#if defined(EFD_SEMAPHORE)
	flags |= EFD_SEMAPHORE;
#endif
#if defined(EFD_NONBLOCK)
	if (eventfd_nonblock)
		flags |= EFD_NONBLOCK;
#endif

	fd1 = eventfd(0, flags);
	if (fd1 < 0) {
		rc = stress_exit_status(errno);
		pr_fail("%s: eventfd failed, errno=%d (%s)\n",
			args->name, errno, strerror(errno));
		return rc;
	}
	fd2 = eventfd(0, flags);
	if (fd2 < 0) {
		rc = stress_exit_status(errno);
		pr_fail("%s: eventfd failed, errno=%d (%s)\n",
			args->name, errno, strerror(errno));
		(void)close(fd1);
		return rc;
	}

	/* Exercise eventfd on invalid flags */
	test_fd = eventfd(0, ~0);
	if (test_fd >= 0)
		(void)close(test_fd);

	stress_set_proc_state(args->name, STRESS_STATE_SYNC_WAIT);
	stress_sync_start_wait(args);
	stress_set_proc_state(args->name, STRESS_STATE_RUN);
again:
	parent_cpu = stress_get_cpu();
	pid = fork();
	if (pid < 0) {
		if (stress_redo_fork(args, errno))
			goto again;

		(void)close(fd1);
		(void)close(fd2);

		if (UNLIKELY(!stress_continue(args)))
			return EXIT_SUCCESS;
		pr_fail("%s: fork failed, errno=%d (%s)\n",
			args->name, errno, strerror(errno));
		return EXIT_FAILURE;
	} else if (pid == 0) {
		int n = 0;

		stress_set_proc_state(args->name, STRESS_STATE_RUN);
		(void)stress_change_cpu(args, parent_cpu);
		stress_parent_died_alarm();
		(void)sched_settings_apply(true);

		while (stress_continue_flag()) {
			uint64_t val = 0;
			ssize_t ret;
			char re[7];

			/* Exercise read on small buffer */
			VOID_RET(ssize_t, read(fd1, re, sizeof(re)));

			for (;;) {
				if (UNLIKELY(!stress_continue_flag()))
					goto exit_child;
				ret = read(fd1, &val, sizeof(val));
				if (UNLIKELY(ret < 0)) {
					if ((errno == EAGAIN) ||
					    (errno == EINTR))
						continue;
					pr_fail("%s child read failed, errno=%d (%s)\n",
						args->name, errno, strerror(errno));
					goto exit_child;
				}
				if (UNLIKELY(ret < (ssize_t)sizeof(val))) {
					pr_fail("%s child short read, got %zd, expecting %zd bytes\n",
						args->name, ret, (ssize_t)sizeof(val));
					goto exit_child;
				}
				break;
			}

			/*
			 *  Periodically exercise with invalid writes
			 */
			if (UNLIKELY(n++ >= 64)) {
				n = 0;

				/* Exercise write using small buffer */
				(void)shim_memset(re, 0, sizeof(re));
				VOID_RET(ssize_t, write(fd1, re, sizeof(re)));

				/* Exercise write on buffer out of range */
				val = ~0UL;
				VOID_RET(ssize_t, write(fd1, &val, sizeof(val)));
			}

			val = 1;
			for (;;) {
				if (UNLIKELY(!stress_continue_flag()))
					goto exit_child;
				ret = write(fd2, &val, sizeof(val));
				if (UNLIKELY(ret < 0)) {
					if ((errno == EAGAIN) ||
					    (errno == EINTR))
						continue;
					pr_fail("%s: child write failed, errno=%d (%s)\n",
						args->name, errno, strerror(errno));
					goto exit_child;
				}
				if (UNLIKELY(ret < (ssize_t)sizeof(val))) {
					pr_fail("%s child short write, got %zd, expecting %zd bytes\n",
						args->name, ret, (ssize_t)sizeof(val));
					goto exit_child;
				}
				break;
			}
		}
exit_child:
		(void)close(fd1);
		(void)close(fd2);
		_exit(EXIT_SUCCESS);
	} else {
		const pid_t self = getpid();

		do {
			uint64_t val = 1;
			ssize_t ret;

			/*
			 *  Accessing /proc/self/fdinfo/[fd1|fd2] will exercise
			 *  eventfd-count and eventfd-id proc interfaces.
			 */
			(void)stress_read_fdinfo(self, stress_mwc1() ? fd1 : fd2);

			for (;;) {
				if (UNLIKELY(!stress_continue_flag()))
					goto exit_parent;

				ret = write(fd1, &val, sizeof(val));
				if (UNLIKELY(ret < 0)) {
					if ((errno == EAGAIN) ||
					    (errno == EINTR))
						continue;
					pr_fail("%s: parent write failed, errno=%d (%s)\n",
						args->name, errno, strerror(errno));
					goto exit_parent;
				}
				if (UNLIKELY(ret < (ssize_t)sizeof(val))) {
					pr_fail("%s parent short write, got %zd, expecting %zd bytes\n",
						args->name, ret, (ssize_t)sizeof(val));
					goto exit_parent;
				}
				break;
			}

			for (;;) {
				if (UNLIKELY(!stress_continue_flag()))
					goto exit_parent;

				ret = read(fd2, &val, sizeof(val));
				if (UNLIKELY(ret < 0)) {
					if ((errno == EAGAIN) ||
					    (errno == EINTR))
						continue;
					pr_fail("%s: parent read failed, errno=%d (%s)\n",
						args->name, errno, strerror(errno));
					goto exit_parent;
				}
				if (UNLIKELY(ret < (ssize_t)sizeof(val))) {
					pr_fail("%s parent short read, got %zd, expecting %zd bytes\n",
						args->name, ret, (ssize_t)sizeof(val));
					goto exit_parent;
				}
				break;
			}
			stress_bogo_inc(args);
		} while (stress_continue(args));
exit_parent:
		stress_set_proc_state(args->name, STRESS_STATE_DEINIT);

		(void)stress_kill_pid_wait(pid, NULL);
		(void)close(fd1);
		(void)close(fd2);
	}
	return EXIT_SUCCESS;
}

const stressor_info_t stress_eventfd_info = {
	.stressor = stress_eventfd,
	.classifier = CLASS_FILESYSTEM | CLASS_SCHEDULER | CLASS_OS,
	.opts = opts,
	.verify = VERIFY_ALWAYS,
	.help = help
};
#else
const stressor_info_t stress_eventfd_info = {
	.stressor = stress_unimplemented,
	.classifier = CLASS_FILESYSTEM | CLASS_SCHEDULER | CLASS_OS,
	.opts = opts,
	.verify = VERIFY_ALWAYS,
	.help = help,
	.unimplemented_reason = "built without sys/eventfd.h or eventfd() support"
};
#endif
