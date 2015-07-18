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

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <signal.h>

#include "stress-ng.h"

/*
 *  check_return()
 *	sanity check fcntl() return for errors
 */
static void check_return(const int ret, const char *name, const char *cmd)
{
	if (ret < 0) {
		pr_fail(stderr, "%s: name: fcntl %s failed: "
			"errno=%d (%s)\n",
			name, cmd, errno, strerror(errno));
	}
}

/*
 *  do_fcntl()
 */
static int do_fcntl(const int fd, const char *name)
{
#if defined(F_DUPFD)
	{
		int ret;

		ret = fcntl(fd, F_DUPFD, mwc8());
		check_return(ret, name, "F_DUPFD");
		if (ret > -1)
			(void)close(ret);
	}
#endif

#if defined(F_DUPFD_CLOEXEC)
	{
		int ret;

		ret = fcntl(fd, F_DUPFD_CLOEXEC, mwc8());
		check_return(ret, name, "F_DUPFD_CLOEXEC");
		if (ret > -1)
			(void)close(ret);
	}
#endif

#if defined(F_GETFD)
	{
		int old_flags;

		old_flags = fcntl(fd, F_GETFD);
		check_return(old_flags, name, "F_GETFD");

#if defined(F_SETFD) && defined(O_CLOEXEC)
		if (old_flags > -1) {
			int new_flags, ret;

			new_flags = old_flags |= O_CLOEXEC;
			ret = fcntl(fd, F_SETFD, new_flags);
			check_return(ret, name, "F_SETFD");

			new_flags &= ~O_CLOEXEC;
			ret = fcntl(fd, F_SETFD, new_flags);
			check_return(ret, name, "F_SETFD");
		}
#endif
	}
#endif

#if defined(F_GETFL)
	{
		int old_flags;

		old_flags = fcntl(fd, F_GETFL);
		check_return(old_flags, name, "F_GETFL");

#if defined(F_SETFL) && defined(O_APPEND)
		if (old_flags > -1) {
			int new_flags, ret;

			new_flags = old_flags |= O_APPEND;
			ret = fcntl(fd, F_SETFL, new_flags);
			check_return(ret, name, "F_SETFL");

			new_flags &= ~O_APPEND;
			ret = fcntl(fd, F_SETFL, new_flags);
			check_return(ret, name, "F_SETFL");
		}
#endif
	}
#endif

#if defined(F_SETOWN)
	{
		int ret;

		ret = fcntl(fd, F_SETOWN, getpid());
		check_return(ret, name, "F_SETOWN");
	}
#endif

#if defined(F_GETOWN)
	{
		int ret;

		ret = fcntl(fd, F_GETOWN);
		check_return(ret, name, "F_GETOWN");
	}
#endif

#if defined(F_SETOWN_EX)
	{
		int ret;
		struct f_owner_ex owner;
	
		owner.type = F_OWNER_PID;
		owner.pid = getpid();
		ret = fcntl(fd, F_SETOWN_EX, &owner);
		check_return(ret, name, "F_SETOWN_EX");
	}
#endif

#if defined(F_GETOWN_EX)
	{
		int ret;
		struct f_owner_ex owner;

		owner.type = F_OWNER_PID;
		ret = fcntl(fd, F_GETOWN_EX, &owner);
		check_return(ret, name, "F_GETOWN_EX");
	}
#endif

#if defined(F_SETSIG)
	{
		int ret;

		ret = fcntl(fd, F_SETSIG, SIGKILL);
		check_return(ret, name, "F_SETSIG");
		ret = fcntl(fd, F_SETSIG, 0);
		check_return(ret, name, "F_SETSIG");
		ret = fcntl(fd, F_SETSIG, SIGIO);
		check_return(ret, name, "F_SETSIG");
	}
#endif

#if defined(F_GETSIG)
	{
		int ret;

		ret = fcntl(fd, F_GETSIG);
		check_return(ret, name, "F_GETSIG");
	}
#endif
	return 0;
}

/*
 *  stress_fcntl
 *	stress various fcntl calls
 */
int stress_fcntl(
	uint64_t *const counter,
	const uint32_t instance,
	const uint64_t max_ops,
	const char *name)
{
	const pid_t ppid = getppid();
	int fd = -1, rc = EXIT_FAILURE, retries = 0;
	char filename[PATH_MAX], dirname[PATH_MAX];

	/*
	 *  Allow for multiple workers to chmod the *same* file
	 */
	stress_temp_dir(dirname, sizeof(dirname), name, ppid, 0);
        if (mkdir(dirname, S_IRWXU) < 0) {
		if (errno != EEXIST) {
			pr_failed_err(name, "mkdir");
			return EXIT_FAILURE;
		}
	}
	(void)stress_temp_filename(filename, sizeof(filename),
		name, ppid, 0, 0);

	do {
		errno = 0;
		/*
		 *  Try and open the file, it may be impossible
		 *  momentarily because other fcntl stressors have
		 *  already created it
		 */
		if ((fd = creat(filename, S_IRUSR | S_IWUSR)) < 0) {
			if (errno == EPERM || errno == EACCES) {
				(void)usleep(100000);
				continue;
			}
			pr_failed_err(name, "open");
			goto tidy;
		}
		break;
	} while (opt_do_run && ++retries < 100);

	if (retries >= 100) {
		pr_err(stderr, "%s: chmod: file %s took %d "
			"retries to create (instance %" PRIu32 ")\n",
			name, filename, retries, instance);
		goto tidy;
	}

	do {
		do_fcntl(fd, name);
		(*counter)++;
	} while (opt_do_run && (!max_ops || *counter < max_ops));

	rc = EXIT_SUCCESS;
tidy:
	if (fd >= 0)
		(void)close(fd);
	(void)unlink(filename);
	(void)rmdir(dirname);

	return rc;
}
