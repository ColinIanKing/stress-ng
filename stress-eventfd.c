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
	{ NULL,	"eventfd N",	 "start N workers stressing eventfd read/writes" },
	{ NULL,	"eventfd-ops N", "stop eventfd workers after N bogo operations" },
	{ NULL, "eventfs-nonblock", "poll with non-blocking I/O on eventfd fd" },
	{ NULL,	NULL,		 NULL }
};

static int stress_set_eventfd_nonblock(const char *opt)
{
	bool eventfd_nonblock = true;

	(void)opt;
	return stress_set_setting("eventfd-nonblock", TYPE_ID_BOOL, &eventfd_nonblock);
}

static const stress_opt_set_func_t opt_set_funcs[] = {
	{ OPT_eventfd_nonblock,	stress_set_eventfd_nonblock },
        { 0,			NULL }
};

#if defined(HAVE_SYS_EVENTFD_H) && \
    defined(HAVE_EVENTFD) && \
    NEED_GLIBC(2,8,0)

/*
 *  stress_eventfd
 *	stress eventfd read/writes
 */
static int stress_eventfd(const stress_args_t *args)
{
	pid_t pid;
	int fd1, fd2, rc;
	int flags = 0;
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
		rc = exit_status(errno);
		pr_fail_dbg("eventfd");
		return rc;
	}
	fd2 = eventfd(0, flags);
	if (fd2 < 0) {
		rc = exit_status(errno);
		pr_fail_dbg("eventfd");
		(void)close(fd1);
		return rc;
	}

again:
	pid = fork();
	if (pid < 0) {
		if (keep_stressing_flag() &&
                    ((errno == EAGAIN) || (errno == ENOMEM)))
			goto again;
		pr_fail_dbg("fork");
		(void)close(fd1);
		(void)close(fd2);
		return EXIT_FAILURE;
	} else if (pid == 0) {
		(void)setpgid(0, g_pgrp);
		stress_parent_died_alarm();

		while (keep_stressing_flag()) {
			uint64_t val;
			ssize_t ret;

			for (;;) {
				if (!keep_stressing_flag())
					goto exit_child;
				ret = read(fd1, &val, sizeof(val));
				if (ret < 0) {
					if ((errno == EAGAIN) ||
					    (errno == EINTR))
						continue;
					pr_fail_dbg("child read");
					goto exit_child;
				}
				if (ret < (ssize_t)sizeof(val)) {
					pr_fail_dbg("child short read");
					goto exit_child;
				}
				break;
			}
			val = 1;

			for (;;) {
				if (!keep_stressing_flag())
					goto exit_child;
				ret = write(fd2, &val, sizeof(val));
				if (ret < 0) {
					if ((errno == EAGAIN) ||
					    (errno == EINTR))
						continue;
					pr_fail_dbg("child write");
					goto exit_child;
				}
				if (ret  < (ssize_t)sizeof(val)) {
					pr_fail_dbg("child short write");
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
		int status;

		do {
			uint64_t val = 1;
			int ret, procfd;
			char path[PATH_MAX];

			(void)snprintf(path, sizeof(path), "/proc/self/fdinfo/%d",
				stress_mwc1() ? fd1 : fd2);
			/*
			 *  Accessing /proc/self/fdinfo/[fd1|fd2] will exercise
			 *  eventfd-count and eventfd-id proc interfaces.
			 */
			procfd = open(path, O_RDONLY);
			if (procfd >= 0) {
				char buffer[4096];
				ssize_t n;

				n = read(procfd, buffer, sizeof(buffer));
				(void)n;
				(void)close(procfd);
			}

			for (;;) {
				if (!keep_stressing_flag())
					goto exit_parent;

				ret = write(fd1, &val, sizeof(val));
				if (ret < 0) {
					if ((errno == EAGAIN) ||
					    (errno == EINTR))
						continue;
					pr_fail_dbg("parent write");
					goto exit_parent;
				}
				if (ret < (ssize_t)sizeof(val)) {
					pr_fail_dbg("parent short write");
					goto exit_parent;
				}
				break;
			}

			for (;;) {
				if (!keep_stressing_flag())
					goto exit_parent;

				ret = read(fd2, &val, sizeof(val));
				if (ret < 0) {
					if ((errno == EAGAIN) ||
					    (errno == EINTR))
						continue;
					pr_fail_dbg("parent read");
					goto exit_parent;
				}
				if (ret < (ssize_t)sizeof(val)) {
					pr_fail_dbg("parent short read");
					goto exit_parent;
				}
				break;
			}
			inc_counter(args);
		} while (keep_stressing());
exit_parent:
		(void)kill(pid, SIGKILL);
		(void)shim_waitpid(pid, &status, 0);
		(void)close(fd1);
		(void)close(fd2);
	}
	return EXIT_SUCCESS;
}

stressor_info_t stress_eventfd_info = {
	.stressor = stress_eventfd,
	.class = CLASS_FILESYSTEM | CLASS_SCHEDULER | CLASS_OS,
	.opt_set_funcs = opt_set_funcs,
	.help = help
};
#else
stressor_info_t stress_eventfd_info = {
	.stressor = stress_not_implemented,
	.class = CLASS_FILESYSTEM | CLASS_SCHEDULER | CLASS_OS,
	.opt_set_funcs = opt_set_funcs,
	.help = help
};
#endif
