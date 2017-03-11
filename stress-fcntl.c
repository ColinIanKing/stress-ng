/*
 * Copyright (C) 2013-2017 Canonical, Ltd.
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

#if !defined(__minix__)
/*
 *  fd_available()
 *	return true if a fd is not being used
 */
static inline bool fd_available(const int fd)
{
	const int ret  = fcntl(fd, F_GETFD);

	return ((ret == -1) && (errno == EBADF));
}

/*
 *  fd_get()
 *	find a free fd to dup onto
 */
static int fd_get(void)
{
	int i;

	for (i = 0; i < 256; i++) {
		int fd = mwc32() & 1023;

		if (fd_available(fd))
			return fd;
	}
	return -1;	/* unlikely */
}
#endif

/*
 *  check_return()
 *	sanity check fcntl() return for errors
 */
static void check_return(const args_t *args, const int ret, const char *cmd)
{
	if (ret < 0) {
		pr_fail("%s: fcntl %s failed: "
			"errno=%d (%s)\n",
			args->name, cmd, errno, strerror(errno));
	}
}

/*
 *  do_fcntl()
 */
static int do_fcntl(const args_t *args, const int fd)
{
#if defined(F_DUPFD) && !defined(__minix__)
	{
		int ret, fd2 = fd_get();

		if (fd2 != -1) {
			ret = fcntl(fd, F_DUPFD, fd2);
			check_return(args, ret, "F_DUPFD");
			if (ret > -1)
				(void)close(ret);
		}
	}
#endif

#if defined(F_DUPFD_CLOEXEC) && !defined(__minix__)
	{
		int ret, fd2 = fd_get();

		if (fd2 != -1) {
			ret = fcntl(fd, F_DUPFD_CLOEXEC, mwc8());
			check_return(args, ret, "F_DUPFD_CLOEXEC");
			if (ret > -1)
				(void)close(ret);
		}
	}
#endif

#if defined(F_GETFD)
	{
		int old_flags;

		old_flags = fcntl(fd, F_GETFD);
		check_return(args, old_flags, "F_GETFD");

#if defined(F_SETFD) && defined(O_CLOEXEC)
		if (old_flags > -1) {
			int new_flags, ret;

			new_flags = old_flags |= O_CLOEXEC;
			ret = fcntl(fd, F_SETFD, new_flags);
			check_return(args, ret, "F_SETFD");

			new_flags &= ~O_CLOEXEC;
			ret = fcntl(fd, F_SETFD, new_flags);
			check_return(args, ret, "F_SETFD");
		}
#endif
	}
#endif

#if defined(F_GETFL)
	{
		int old_flags;

		old_flags = fcntl(fd, F_GETFL);
		check_return(args, old_flags, "F_GETFL");

#if defined(F_SETFL) && defined(O_APPEND)
		if (old_flags > -1) {
			int new_flags, ret;

			new_flags = old_flags |= O_APPEND;
			ret = fcntl(fd, F_SETFL, new_flags);
			check_return(args, ret, "F_SETFL");

			new_flags &= ~O_APPEND;
			ret = fcntl(fd, F_SETFL, new_flags);
			check_return(args, ret, "F_SETFL");
		}
#endif
	}
#endif

#if defined(F_SETOWN) && defined(__linux__)
	{
		int ret;

		ret = fcntl(fd, F_SETOWN, args->pid);
		check_return(args, ret, "F_SETOWN");
	}
#endif

#if defined(F_GETOWN) && defined(__linux__)
	{
		int ret;

		ret = fcntl(fd, F_GETOWN);
		check_return(args, ret, "F_GETOWN");
	}
#endif

#if defined(F_SETOWN_EX)
	{
		int ret;
		struct f_owner_ex owner;

		owner.type = F_OWNER_PID;
		owner.pid = args->pid;
		ret = fcntl(fd, F_SETOWN_EX, &owner);
		check_return(args, ret, "F_SETOWN_EX, F_OWNER_PID");

		owner.type = F_OWNER_PGRP;
		owner.pid = getpgrp();
		ret = fcntl(fd, F_SETOWN_EX, &owner);
		check_return(args, ret, "F_SETOWN_EX, F_OWNER_PGRP");

#if defined(__linux__)
		owner.type = F_OWNER_TID;
		owner.pid = shim_gettid();
		ret = fcntl(fd, F_SETOWN_EX, &owner);
		check_return(args, ret, "F_SETOWN_EX, F_OWNER_TID");
#endif
	}
#endif

#if defined(F_GETOWN_EX)
	{
		int ret;
		struct f_owner_ex owner;

		owner.type = F_OWNER_PID;
		ret = fcntl(fd, F_GETOWN_EX, &owner);
		check_return(args, ret, "F_GETOWN_EX, F_OWNER_PID");

		owner.type = F_OWNER_PGRP;
		ret = fcntl(fd, F_GETOWN_EX, &owner);
		check_return(args, ret, "F_GETOWN_EX, F_OWNER_PGRP");

#if defined(__linux__)
		owner.type = F_OWNER_TID;
		ret = fcntl(fd, F_GETOWN_EX, &owner);
		check_return(args, ret, "F_GETOWN_EX, F_OWNER_TID");
#endif
	}
#endif

#if defined(F_SETSIG)
	{
		int ret;

		ret = fcntl(fd, F_SETSIG, SIGKILL);
		check_return(args, ret, "F_SETSIG");
		ret = fcntl(fd, F_SETSIG, 0);
		check_return(args, ret, "F_SETSIG");
		ret = fcntl(fd, F_SETSIG, SIGIO);
		check_return(args, ret, "F_SETSIG");
	}
#endif

#if defined(F_GETSIG)
	{
		int ret;

		ret = fcntl(fd, F_GETSIG);
		check_return(args, ret, "F_GETSIG");
	}
#endif

#if defined(F_GETOWNER_UIDS)
	{
		int ret;

		ret = fcntl(fd, F_GETOWNER_UIDS);
		check_return(args, ret, "F_GETOWNER_UIDS");
	}
#endif

#if defined(F_GETLEASE)
	{
		int ret;

		ret = fcntl(fd, F_GETLEASE);
		check_return(args, ret, "F_GETLEASE");
	}
#endif

#if defined(F_GETLK) && defined(F_SETLK) && defined(F_SETLKW) && \
    defined(F_WRLCK) && defined(F_UNLCK)
	{
#if defined (__linux__)
		struct flock64 f;
#else
		struct flock f;
#endif
		int ret;
		const off_t len = (mwc16() + 1) & 0x7fff;
		const off_t start = mwc16() & 0x7fff;

		if (ftruncate(fd, 65536) < 0) {
			pr_fail_err("ftruncate");
			goto lock_abort;
		}

		f.l_type = F_WRLCK;
		f.l_whence = SEEK_SET;
		f.l_start = start;
		f.l_len = len;
		f.l_pid = args->pid;

		ret = fcntl(fd, F_GETLK, &f);
		check_return(args, ret, "F_GETLK");

#if 0
		if (f.l_type != F_UNLCK) {
			pr_fail_err("fcntl F_GETLK");
			goto lock_abort;
		}
#endif

		f.l_type = F_WRLCK;
		f.l_whence = SEEK_SET;
		f.l_start = start;
		f.l_len = len;
		f.l_pid = args->pid;

		ret = fcntl(fd, F_SETLK, &f);
		if ((ret < 0) && (errno == EAGAIN))
			goto lock_abort;
		check_return(args, ret, "F_SETLK (F_WRLCK)");

		f.l_type = F_UNLCK;
		f.l_whence = SEEK_SET;
		f.l_start = start;
		f.l_len = len;
		f.l_pid = args->pid;

		ret = fcntl(fd, F_SETLK, &f);
		if ((ret < 0) && (errno == EAGAIN))
			goto lock_abort;

		check_return(args, ret, "F_SETLK (F_UNLCK)");

		f.l_type = F_WRLCK;
		f.l_whence = SEEK_SET;
		f.l_start = start;
		f.l_len = len;
		f.l_pid = args->pid;

		ret = fcntl(fd, F_SETLKW, &f);
		if ((ret < 0) && (errno == EAGAIN))
			goto lock_abort;
		check_return(args, ret, "F_SETLKW (F_WRLCK)");

		f.l_type = F_UNLCK;
		f.l_whence = SEEK_SET;
		f.l_start = start;
		f.l_len = len;
		f.l_pid = args->pid;

		ret = fcntl(fd, F_SETLK, &f);
		check_return(args, ret, "F_SETLK (F_UNLCK)");

lock_abort:	{ /* Nowt */ }
	}
#endif

#if defined(F_OFD_GETLK) && defined(F_OFD_SETLK) && defined(F_OFD_SETLKW) && \
    defined(F_WRLCK) && defined(F_UNLCK)
	{
#if defined (__linux__)
		struct flock64 f;
#else
		struct flock f;
#endif
		int ret;
		const off_t len = (mwc16() + 1) & 0x7fff;
		const off_t start = mwc16() & 0x7fff;

		if (ftruncate(fd, 65536) < 0) {
			pr_fail_err("ftruncate");
			goto ofd_lock_abort;
		}

		f.l_type = F_WRLCK;
		f.l_whence = SEEK_SET;
		f.l_start = start;
		f.l_len = len;
		f.l_pid = 0;

		ret = fcntl(fd, F_OFD_GETLK, &f);

		check_return(args, ret, "F_OFD_GETLK (F_WRLCK)");

		f.l_type = F_WRLCK;
		f.l_whence = SEEK_SET;
		f.l_start = start;
		f.l_len = len;
		f.l_pid = 0;

		ret = fcntl(fd, F_OFD_SETLK, &f);
		if ((ret < 0) && (errno == EAGAIN))
			goto ofd_lock_abort;
		check_return(args, ret, "F_OFD_SETLK (F_WRLCK)");

		f.l_type = F_UNLCK;
		f.l_whence = SEEK_SET;
		f.l_start = start;
		f.l_len = len;
		f.l_pid = 0;

		ret = fcntl(fd, F_OFD_SETLK, &f);
		if ((ret < 0) && (errno == EAGAIN))
			goto ofd_lock_abort;
		check_return(args, ret, "F_OFD_SETLK (F_UNLCK)");

		f.l_type = F_WRLCK;
		f.l_whence = SEEK_SET;
		f.l_start = start;
		f.l_len = len;
		f.l_pid = 0;

		ret = fcntl(fd, F_OFD_SETLKW, &f);
		if ((ret < 0) && (errno == EAGAIN))
			goto ofd_lock_abort;
		check_return(args, ret, "F_OFD_SETLKW (F_WRLCK)");

		f.l_type = F_UNLCK;
		f.l_whence = SEEK_SET;
		f.l_start = start;
		f.l_len = len;
		f.l_pid = 0;

		ret = fcntl(fd, F_OFD_SETLK, &f);
		if ((ret < 0) && (errno == EAGAIN))
			goto ofd_lock_abort;
		check_return(args, ret, "F_OFD_SETLK (F_UNLCK)");

ofd_lock_abort:	{ /* Nowt */ }
	}
#endif

	return 0;
}

/*
 *  stress_fcntl
 *	stress various fcntl calls
 */
int stress_fcntl(const args_t *args)
{
	const pid_t ppid = getppid();
	int fd = -1, rc = EXIT_FAILURE, retries = 0;
	char filename[PATH_MAX], dirname[PATH_MAX];

	/*
	 *  Allow for multiple workers to chmod the *same* file
	 */
	stress_temp_dir(dirname, sizeof(dirname), args->name, ppid, 0);
	if (mkdir(dirname, S_IRWXU) < 0) {
		if (errno != EEXIST) {
			pr_fail_err("mkdir");
			return exit_status(errno);
		}
	}
	(void)stress_temp_filename(filename, sizeof(filename),
		args->name, ppid, 0, 0);

	do {
		errno = 0;
		/*
		 *  Try and open the file, it may be impossible
		 *  momentarily because other fcntl stressors have
		 *  already created it
		 */
		if ((fd = creat(filename, S_IRUSR | S_IWUSR)) < 0) {
			if ((errno == EPERM) || (errno == EACCES) ||
			    (errno == ENOMEM) || (errno == ENOSPC)) {
				(void)shim_usleep(100000);
				continue;
			}
			pr_fail_err("open");
			goto tidy;
		}
		break;
	} while (g_keep_stressing_flag && ++retries < 100);

	if (retries >= 100) {
		pr_err("%s: chmod: file %s took %d "
			"retries to create (instance %" PRIu32 ")\n",
			args->name, filename, retries, args->instance);
		goto tidy;
	}

	do {
		do_fcntl(args, fd);
		inc_counter(args);
	} while (keep_stressing());

	rc = EXIT_SUCCESS;
tidy:
	if (fd >= 0)
		(void)close(fd);
	(void)unlink(filename);
	(void)rmdir(dirname);

	return rc;
}
