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

static const stress_help_t help[] = {
	{ NULL,	"fcntl N",	"start N workers exercising fcntl commands" },
	{ NULL,	"fcntl-ops N",	"stop after N fcntl bogo operations" },
	{ NULL,	NULL,		NULL }
};

#if defined(HAVE_PID_TYPE)
#define shim_pid_type		enum __pid_type
#else
#define shim_pid_type		int
#endif

/* As of glibc6, this is not defined */
#if !defined(F_GETOWNER_UIDS) && defined(__linux__)
#define F_GETOWNER_UIDS  	(17)
#endif

#if defined(F_CREATED_QUERY) || \
    defined(F_DUPFD) || 	\
    defined(F_DUPFD_CLOEXEC) || \
    defined(F_GETFD) ||		\
    defined(F_SETFD) ||		\
    defined(F_GETFL) ||		\
    defined(F_SETFL) ||		\
    defined(F_GETOWN) ||	\
    defined(F_SETOWN) ||	\
    defined(F_GETOWN_EX) ||	\
    defined(F_SETOWN_EX) ||	\
    defined(F_GETSIG) ||	\
    defined(F_SETSIG) ||	\
    defined(F_GETOWNER_UIDS) ||	\
    defined(F_GETLEASE) ||	\
    (defined(F_GETLK) && defined(F_SETLK) && \
     defined(F_SETLKW) && defined(F_WRLCK) && \
     defined(F_UNLCK)) ||	\
    (defined(F_OFD_GETLK) && defined(F_OFD_SETLK) && \
     defined(F_OFD_SETLKW) && defined(F_WRLCK) && defined(F_UNLCK)) | \
    (defined(F_GET_FILE_RW_HINT) && defined(F_SET_FILE_RW_HINT)) | \
    (defined(F_GET_RW_HINT) && defined(F_SET_RW_HINT))

static size_t setfl_flag_count;
static int *setfl_flag_perms;

static const int all_setfl_flags =
#if defined(O_APPEND)
	O_APPEND |
#endif
#if defined(O_ASYNC)
	O_ASYNC |
#endif
#if defined(O_DIRECT)
	O_DIRECT |
#endif
#if defined(O_NOATIME)
	O_NOATIME |
#endif
#if defined(O_NONBLOCK)
	O_NONBLOCK |
#endif
	0;

/*
 *  check_return()
 *	sanity check fcntl() return for errors
 */
static void check_return(
	stress_args_t *args,
	const int ret,
	const char *cmd,
	int *rc)
{
	if (UNLIKELY(ret < 0)) {
		if ((errno != EINVAL) &&
		    (errno != EINTR) &&
		    (errno != EPERM)) {
			pr_fail("%s: fcntl %s failed, errno=%d (%s)\n",
				args->name, cmd, errno, strerror(errno));
			*rc = EXIT_FAILURE;
		}
	}
}
#endif

/*
 *  do_fcntl()
 */
static void do_fcntl(
	stress_args_t *args,
	const int fd,
	const int bad_fd,
	const int path_fd,
	int *rc)
{
#if defined(F_DUPFD)
	{
		int ret;

		ret = fcntl(fd, F_DUPFD, 0);
		check_return(args, ret, "F_DUPFD", rc);
		if (ret > -1)
			(void)close(ret);

		/* Exercise invalid fd */
		VOID_RET(int, fcntl(fd, F_DUPFD, bad_fd));
	}
#else
	UNEXPECTED
#endif

#if defined(F_DUPFD) &&		\
    defined(F_DUPFD_CLOEXEC)
	{
		int ret;

		ret = fcntl(fd, F_DUPFD, F_DUPFD_CLOEXEC);
		check_return(args, ret, "F_DUPFD_CLOEXEC", rc);
		if (ret > -1)
			(void)close(ret);
	}
#else
	UNEXPECTED
#endif

#if defined(F_GETFD)
	{
		int old_flags;

		old_flags = fcntl(fd, F_GETFD);
		check_return(args, old_flags, "F_GETFD", rc);

#if defined(F_SETFD) &&		\
    defined(O_CLOEXEC)
		if (old_flags > -1) {
			int new_flags, ret;

			new_flags = old_flags | O_CLOEXEC;
			ret = fcntl(fd, F_SETFD, new_flags);
			check_return(args, ret, "F_SETFD", rc);

			new_flags &= ~O_CLOEXEC;
			ret = fcntl(fd, F_SETFD, new_flags);
			check_return(args, ret, "F_SETFD", rc);
		}
#else
		UNEXPECTED
#endif
		/* Exercise invalid fd */
		VOID_RET(int, fcntl(bad_fd, F_GETFD));
	}
#else
	UNEXPECTED
#endif

#if defined(F_GETFL)
	{
		int old_flags;

		old_flags = fcntl(fd, F_GETFL);
		check_return(args, old_flags, "F_GETFL", rc);

#if defined(F_SETFL) &&		\
    defined(O_APPEND)
		if (old_flags > -1) {
			int new_flags, ret;

			/* Exercise all permutations of SETFL flags */
			if ((setfl_flag_count > 0) && (setfl_flag_perms)) {
				static size_t idx;

				VOID_RET(int, fcntl(fd, F_SETFL, setfl_flag_perms[idx]));

				idx++;
				if (UNLIKELY(idx >= setfl_flag_count))
					idx = 0;
			}

			new_flags = old_flags | O_APPEND;
			ret = fcntl(fd, F_SETFL, new_flags);
			check_return(args, ret, "F_SETFL", rc);

			new_flags &= ~O_APPEND;
			ret = fcntl(fd, F_SETFL, new_flags);
			check_return(args, ret, "F_SETFL", rc);
		}
#else
		UNEXPECTED
#endif
	}
#else
	UNEXPECTED
#endif

#if defined(F_SETOWN) &&	\
    defined(__linux__)
	{
		int ret;

#if defined(HAVE_GETPGRP)
		ret = fcntl(fd, F_SETOWN, -getpgrp());
		check_return(args, ret, "F_SETOWN", rc);
#else
		UNEXPECTED
#endif
		ret = fcntl(fd, F_SETOWN, args->pid);
		check_return(args, ret, "F_SETOWN", rc);

		/* This should return -EINVAL */
		VOID_RET(int, fcntl(fd, F_SETOWN, INT_MIN));

		/* This is intended to probably fail with -ESRCH */
		VOID_RET(int, fcntl(fd, F_SETOWN, stress_get_unused_pid_racy(false)));

		/* And set back to current pid */
		VOID_RET(int, fcntl(fd, F_SETOWN, args->pid));
	}
#else
	UNEXPECTED
#endif

#if defined(F_GETOWN) &&	\
    defined(__linux__)
	{
		int ret;

#if defined(__NR_fcntl) &&	\
    defined(HAVE_SYSCALL)
		/*
		 * glibc maps fcntl F_GETOWN to F_GETOWN_EX so
		 * so try to bypass the glibc altogether
		 */
		ret = (int)syscall(__NR_fcntl, fd, F_GETOWN);
#else
		ret = fcntl(fd, F_GETOWN);
#endif
		check_return(args, ret, "F_GETOWN", rc);
	}
#endif

/*
 *  These may not yet be defined in libc
 */
#if !defined(F_OWNER_TID)
#define F_OWNER_TID	0
#endif
#if !defined(F_OWNER_PID)
#define F_OWNER_PID     1
#endif
#if !defined(F_OWNER_PGRP)
#define F_OWNER_PGRP    2
#endif

#if defined(F_SETOWN_EX) &&	\
    (defined(F_OWNER_PID) ||	\
     defined(F_OWNER_GID) ||	\
     defined(F_OWNER_PGRP) ||	\
     (defined(F_OWNER_TID) && defined(__linux__)))
	{
		struct f_owner_ex owner;

#if defined(F_OWNER_PID)
		/* This is intended to probably fail with -ESRCH */
		owner.type = (shim_pid_type)F_OWNER_PID;
		owner.pid = stress_get_unused_pid_racy(false);
		VOID_RET(int, fcntl(fd, F_SETOWN_EX, &owner));

		/* set to stressor's pid */
		owner.type = (shim_pid_type)F_OWNER_PID;
		owner.pid = args->pid;
		VOID_RET(int, fcntl(fd, F_SETOWN_EX, &owner));

#endif
#if defined(HAVE_GETPGRP) &&	\
    defined(F_OWNER_PGRP)
		owner.type = (shim_pid_type)F_OWNER_PGRP;
		owner.pid = getpgrp();
		VOID_RET(int, fcntl(fd, F_SETOWN_EX, &owner));
#else
		UNEXPECTED
#endif
#if defined(F_OWNER_TID) &&	\
    defined(__linux__)
		owner.type = (shim_pid_type)F_OWNER_TID;
		owner.pid = shim_gettid();
		VOID_RET(int, fcntl(fd, F_SETOWN_EX, &owner));
#else
		UNEXPECTED
#endif
	}
#endif

#if defined(F_GETOWN_EX)
	{
		int ret;
		struct f_owner_ex owner;

		owner.type = (shim_pid_type)F_OWNER_PID;
		ret = fcntl(fd, F_GETOWN_EX, &owner);
		check_return(args, ret, "F_GETOWN_EX, F_OWNER_PID", rc);

#if defined(F_OWNER_PGRP)
		owner.type = (shim_pid_type)F_OWNER_PGRP;
		VOID_RET(int, fcntl(fd, F_GETOWN_EX, &owner));
#else
		UNEXPECTED
#endif
#if defined(F_OWNER_GID)
		/* deprecated, renamed to F_OWNER_PGRP */
		owner.type = (shim_pid_type)F_OWNER_GID;
		VOID_RET(int, fcntl(fd, F_GETOWN_EX, &owner));
#endif
#if defined(F_OWNER_TID) &&	\
    defined(__linux__)
		owner.type = (shim_pid_type)F_OWNER_TID;
		VOID_RET(int, fcntl(fd, F_GETOWN_EX, &owner));
#else
		UNEXPECTED
#endif
	}
#endif

#if defined(F_SETSIG)
	{
		int ret;

		ret = fcntl(fd, F_SETSIG, SIGKILL);
		check_return(args, ret, "F_SETSIG", rc);
		ret = fcntl(fd, F_SETSIG, 0);
		check_return(args, ret, "F_SETSIG", rc);
		ret = fcntl(fd, F_SETSIG, SIGIO);
		check_return(args, ret, "F_SETSIG", rc);

		/* Exercise illegal signal number */
		VOID_RET(int, fcntl(fd, F_SETSIG, ~0));
		/* Apparently zero restores default behaviour */
		VOID_RET(int, fcntl(fd, F_SETSIG, 0));
	}
#else
	UNEXPECTED
#endif

#if defined(F_GETSIG)
	{
		int ret;

		ret = fcntl(fd, F_GETSIG);
		check_return(args, ret, "F_GETSIG", rc);
	}
#else
	UNEXPECTED
#endif

#if defined(F_GETOWNER_UIDS)
	/* Not defined in libc fcntl.h */
	{
		int ret;
		uid_t uids[2];

		ret = fcntl(fd, F_GETOWNER_UIDS, uids);
		check_return(args, ret, "F_GETOWNER_UIDS", rc);
	}
#endif

#if defined(F_GETLEASE) &&	\
    !defined(__APPLE__)
	{
		int ret;

		ret = fcntl(fd, F_GETLEASE);
		check_return(args, ret, "F_GETLEASE", rc);
	}
#else
	UNEXPECTED
#endif

#if defined(F_GETLK) &&		\
    defined(F_SETLK) &&		\
    defined(F_SETLKW) &&	\
    defined(F_WRLCK) &&		\
    defined(F_UNLCK)
	{
#if defined (__linux__)
		struct flock64 f;
#else
		struct flock f;
#endif
		int ret;
		off_t lret;
		const off_t len = (stress_mwc16() + 1) & 0x7fff;
		const off_t start = stress_mwc16() & 0x7fff;

		if (UNLIKELY(ftruncate(fd, 65536) < 0)) {
			pr_fail("%s: ftruncate failed, errno=%d (%s)\n",
				args->name, errno, strerror(errno));
			goto lock_abort;
		}

		f.l_type = F_WRLCK;
		f.l_whence = SEEK_SET;
		f.l_start = start;
		f.l_len = len;
		f.l_pid = args->pid;

		ret = fcntl(fd, F_GETLK, &f);
		check_return(args, ret, "F_GETLK", rc);

#if 0
		if (UNLIKELY(f.l_type != F_UNLCK)) {
			pr_fail("%s: fcntl F_GETLCK failed, errno=%d (%s)\n",
				args->name, errno, strerror(errno));
			goto lock_abort;
		}
#endif

		/*
		 *  lock and unlock at SEEK_SET position
		 */
		f.l_type = F_WRLCK;
		f.l_whence = SEEK_SET;
		f.l_start = start;
		f.l_len = len;
		f.l_pid = args->pid;

		ret = fcntl(fd, F_SETLK, &f);
		/*
		 * According to POSIX.1 editions 2008, 2013, 2016 and 2018, in
		 * this context EACCES and EAGAIN mean the same, although
		 * Haiku seems to be the only supported OS to return EACCES.
		 */
		if (UNLIKELY((ret < 0) && (errno == EACCES || errno == EAGAIN)))
			goto lock_abort;
		check_return(args, ret, "F_SETLK (F_WRLCK)", rc);

		f.l_type = F_UNLCK;
		f.l_whence = SEEK_SET;
		f.l_start = start;
		f.l_len = len;
		f.l_pid = args->pid;

		ret = fcntl(fd, F_SETLK, &f);
		if (UNLIKELY((ret < 0) && (errno == EAGAIN)))
			goto lock_abort;

		check_return(args, ret, "F_SETLK (F_UNLCK)", rc);

		/*
		 *  lock and unlock at SEEK_SET position
		 */
		f.l_type = F_WRLCK;
		f.l_whence = SEEK_SET;
		f.l_start = start;
		f.l_len = len;
		f.l_pid = args->pid;

		ret = fcntl(fd, F_SETLKW, &f);
		if (UNLIKELY((ret < 0) && ((errno == EAGAIN) || (errno == EDEADLK))))
			goto lock_abort;
		check_return(args, ret, "F_SETLKW (F_WRLCK)", rc);

		f.l_type = F_UNLCK;
		f.l_whence = SEEK_SET;
		f.l_start = start;
		f.l_len = len;
		f.l_pid = args->pid;

		ret = fcntl(fd, F_SETLK, &f);
		check_return(args, ret, "F_SETLK (F_UNLCK)", rc);

		f.l_type = F_WRLCK;
		f.l_whence = SEEK_END;
		f.l_start = 0;
		f.l_len = 1;
		f.l_pid = args->pid;

		ret = fcntl(fd, F_SETLKW, &f);
		if (UNLIKELY((ret < 0) && ((errno == EAGAIN) || (errno == EDEADLK))))
			goto lock_abort;
		check_return(args, ret, "F_SETLKW (F_WRLCK)", rc);

		/*
		 *  lock and unlock at SEEK_END position
		 */
		f.l_type = F_UNLCK;
		f.l_whence = SEEK_END;
		f.l_start = 0;
		f.l_len = 1;
		f.l_pid = args->pid;

		ret = fcntl(fd, F_SETLK, &f);
		check_return(args, ret, "F_SETLK (F_UNLCK)", rc);

		lret = lseek(fd, start, SEEK_SET);
		if (UNLIKELY(lret == (off_t)-1))
			goto lock_abort;

		/*
		 *  lock and unlock at SEEK_CUR position
		 */
		f.l_type = F_WRLCK;
		f.l_whence = SEEK_CUR;
		f.l_start = 0;
		f.l_len = len;
		f.l_pid = args->pid;

		ret = fcntl(fd, F_SETLKW, &f);
		if (UNLIKELY((ret < 0) && ((errno == EAGAIN) || (errno == EDEADLK))))
			goto lock_abort;
		check_return(args, ret, "F_SETLKW (F_WRLCK)", rc);

		f.l_type = F_UNLCK;
		f.l_whence = SEEK_CUR;
		f.l_start = 0;
		f.l_len = len;
		f.l_pid = args->pid;

		ret = fcntl(fd, F_SETLK, &f);
		check_return(args, ret, "F_SETLK (F_UNLCK)", rc);

		/* Exercise various invalid locks */
		f.l_type = ~0;
		f.l_whence = SEEK_CUR;
		f.l_start = 0;
		f.l_len = len;
		f.l_pid = args->pid;
		VOID_RET(int, fcntl(fd, F_SETLK, &f));

		f.l_type = F_SETLK;
		f.l_whence = ~0;
		f.l_start = 0;
		f.l_len = len;
		f.l_pid = args->pid;
		VOID_RET(int, fcntl(fd, F_SETLK, &f));

		f.l_type = F_SETLK;
		f.l_whence = SEEK_SET;
		f.l_start = 0;
		f.l_len = 0;
		f.l_pid = 0;
		VOID_RET(int, fcntl(fd, F_SETLK, &f));

lock_abort:	{ /* Nowt */ }
	}
#endif

#if defined(F_OFD_GETLK) &&	\
    defined(F_OFD_SETLK) &&	\
    defined(F_OFD_SETLKW) && 	\
    defined(F_WRLCK) &&		\
    defined(F_UNLCK)
	{
#if defined (__linux__)
		struct flock64 f;
#else
		struct flock f;
#endif
		int ret;
		const off_t len = (stress_mwc16() + 1) & 0x7fff;
		const off_t start = stress_mwc16() & 0x7fff;

		if (UNLIKELY(ftruncate(fd, 65536) < 0)) {
			pr_fail("%s: ftruncate failed, errno=%d (%s)\n",
				args->name, errno, strerror(errno));
			goto ofd_lock_abort;
		}

		f.l_type = F_WRLCK;
		f.l_whence = SEEK_SET;
		f.l_start = start;
		f.l_len = len;
		f.l_pid = 0;

		ret = fcntl(fd, F_OFD_GETLK, &f);

		check_return(args, ret, "F_OFD_GETLK (F_WRLCK)", rc);

		f.l_type = F_WRLCK;
		f.l_whence = SEEK_SET;
		f.l_start = start;
		f.l_len = len;
		f.l_pid = 0;

		ret = fcntl(fd, F_OFD_SETLK, &f);
		if (UNLIKELY((ret < 0) && (errno == EAGAIN)))
			goto ofd_lock_abort;
		check_return(args, ret, "F_OFD_SETLK (F_WRLCK)", rc);

		f.l_type = F_UNLCK;
		f.l_whence = SEEK_SET;
		f.l_start = start;
		f.l_len = len;
		f.l_pid = 0;

		ret = fcntl(fd, F_OFD_SETLK, &f);
		if (UNLIKELY((ret < 0) && (errno == EAGAIN)))
			goto ofd_lock_abort;
		check_return(args, ret, "F_OFD_SETLK (F_UNLCK)", rc);

		f.l_type = F_WRLCK;
		f.l_whence = SEEK_SET;
		f.l_start = start;
		f.l_len = len;
		f.l_pid = 0;

		ret = fcntl(fd, F_OFD_SETLKW, &f);
		if (UNLIKELY((ret < 0) && ((errno == EAGAIN) || (errno == EDEADLK))))
			goto ofd_lock_abort;
		check_return(args, ret, "F_OFD_SETLKW (F_WRLCK)", rc);

		f.l_type = F_UNLCK;
		f.l_whence = SEEK_SET;
		f.l_start = start;
		f.l_len = len;
		f.l_pid = 0;

		ret = fcntl(fd, F_OFD_SETLK, &f);
		if (UNLIKELY((ret < 0) && (errno == EAGAIN)))
			goto ofd_lock_abort;
		check_return(args, ret, "F_OFD_SETLK (F_UNLCK)", rc);
ofd_lock_abort:	{ /* Nowt */ }
	}
#else
	UNEXPECTED
#endif

#if (defined(F_GET_FILE_RW_HINT) && defined(F_SET_FILE_RW_HINT)) | \
    (defined(F_GET_RW_HINT) && defined(F_SET_RW_HINT))
	{
		int ret;
		size_t i;
		unsigned long hint;
		static const unsigned long hints[] = {
#if defined(RWH_WRITE_LIFE_EXTREME)
			RWH_WRITE_LIFE_EXTREME,
#endif
#if defined(RWH_WRITE_LIFE_LONG)
			RWH_WRITE_LIFE_LONG,
#endif
#if defined(RWH_WRITE_LIFE_MEDIUM)
			RWH_WRITE_LIFE_MEDIUM,
#endif
#if defined(RWH_WRITE_LIFE_SHORT)
			RWH_WRITE_LIFE_SHORT,
#endif
#if defined(RWH_WRITE_LIFE_NONE)
			RWH_WRITE_LIFE_NONE,
#endif
#if defined(RWF_WRITE_LIFE_NOT_SET)
			RWF_WRITE_LIFE_NOT_SET,
#endif
			~0U,	/* Invalid */
		};

#if defined(F_GET_FILE_RW_HINT) &&	\
    defined(F_SET_FILE_RW_HINT)
		ret = fcntl(fd, F_GET_FILE_RW_HINT, &hint);
		if (ret == 0) {
			for (i = 0; i < SIZEOF_ARRAY(hints); i++) {
				hint = hints[i];
				VOID_RET(int, fcntl(fd, F_SET_FILE_RW_HINT, &hint));
			}
		}
		/* Exercise invalid hint type */
		hint = ~0UL;
		VOID_RET(int, fcntl(fd, F_SET_FILE_RW_HINT, &hint));
#else
		UNEXPECTED
#endif
#if defined(F_GET_RW_HINT) &&	\
    defined(F_SET_RW_HINT)
		ret = fcntl(fd, F_GET_RW_HINT, &hint);
		if (ret == 0) {
			for (i = 0; i < SIZEOF_ARRAY(hints); i++) {
				hint = hints[i];
				VOID_RET(int, fcntl(fd, F_SET_RW_HINT, &hint));
			}
		}
#else
		UNEXPECTED
#endif

	}
#endif

#if defined(F_GETFD)
	{
		/*
		 *  and exercise with an invalid fd
		 */
		VOID_RET(int, fcntl(bad_fd, F_GETFD, F_GETFD));
	}
#else
	(void)bad_fd;
#endif

#if defined(O_PATH)
#if defined(F_DUPFD)
	{
		int ret;

		/* Exercise allowed F_DUPFD on a path fd */
		ret = fcntl(path_fd, F_DUPFD, 0);
		if (ret > -1)
			(void)close(ret);
	}
#else
	UNEXPECTED
#endif
#if defined(F_GETOWN)
	{
		/* Exercise not allowed F_GETOWN on a path fd, EBADF */
		VOID_RET(int, fcntl(path_fd, F_GETOWN));
	}
#else
	UNEXPECTED
#endif
#else
	(void)path_fd;
#endif

#if defined(F_DUPFD_QUERY)
	{
		int ret, dupfd;

		ret = fcntl(path_fd, F_DUPFD_QUERY, path_fd);
		if (UNLIKELY((ret >= 0) && (ret != 1))) {
			pr_fail("%s: fcntl F_DUPFD_QUERY on same fd failed, returned %d\n",
				args->name, ret);
			*rc = EXIT_FAILURE;
		}
		ret = fcntl(path_fd, F_DUPFD_QUERY, fd);
		if (UNLIKELY((ret >= 0) && (ret != 0))) {
			pr_fail("%s: fcntl F_DUPFD_QUERY on different fd failed, returned %d\n",
				args->name, ret);
			*rc = EXIT_FAILURE;
		}
		dupfd = dup(fd);
		if (dupfd >= 0) {
			ret = fcntl(fd, F_DUPFD_QUERY, dupfd);
			if (UNLIKELY((ret >= 0) && (ret != 1))) {
				pr_fail("%s: fcntl F_DUPFD_QUERY on dup'd fd failed, returned %d\n",
					args->name, ret);
				*rc = EXIT_FAILURE;
			}
			(void)close(dupfd);
		}
	}
#endif

#if defined(F_CREATED_QUERY)
	{
		int ret;

		/* Linux 6.12 */
		ret = fcntl(fd, F_CREATED_QUERY, 0);
		check_return(args, ret, "F_CREATED_QUERY", rc);
	}
#endif
#if defined(F_READAHEAD)
	{
		/* FreeBSD */
		VOID_RET(int, fcntl(fd, F_READAHEAD, 1024));
		VOID_RET(int, fcntl(fd, F_READAHEAD, -1));
	}
#endif
#if defined(F_RDAHEAD)
	{
		/* FreeBSD and Darwin */
		VOID_RET(int, fcntl(fd, F_RDAHEAD, 1));
		VOID_RET(int, fcntl(fd, F_RDAHEAD, 0));
	}
#endif
#if defined(F_MAXFD)
	{
		/* NetBSD */
		VOID_RET(int, fcntl(fd, F_MAXFD, 0));
	}
#endif
#if defined(F_GETNOSIGPIPE)
	{
		/* NetBSD */
		VOID_RET(int, fcntl(fd, F_GETNOSIGPIPE, 0));
	}
#endif
#if defined(F_GETPATH)
	{
		/* NetBSD */
#if defined(MAXPATHLEN)
		char path[MAXPATHLEN];
#else
		char path[PATH_MAX];
#endif
		VOID_RET(int, fcntl(fd, F_GETPATH, path));
	}
#endif
}

/*
 *  stress_fcntl
 *	stress various fcntl calls
 */
static int stress_fcntl(stress_args_t *args)
{
	const pid_t ppid = getppid();
	int fd, rc = EXIT_FAILURE, retries = 0, path_fd;
	const int bad_fd = stress_get_bad_fd();
	char filename[PATH_MAX], pathname[PATH_MAX];

	setfl_flag_count = stress_flag_permutation(all_setfl_flags, &setfl_flag_perms);

	/*
	 *  Allow for multiple workers to chmod the *same* file
	 */
	stress_temp_dir(pathname, sizeof(pathname), args->name, ppid, 0);
	if (mkdir(pathname, S_IRWXU) < 0) {
		if (errno != EEXIST) {
			pr_fail("%s: mkdir %s failed, errno=%d (%s)\n",
				args->name, pathname, errno, strerror(errno));
			return stress_exit_status(errno);
		}
	}
	(void)stress_temp_filename(filename, sizeof(filename),
		args->name, ppid, 0, 0);

	stress_set_proc_state(args->name, STRESS_STATE_SYNC_WAIT);
	stress_sync_start_wait(args);
	stress_set_proc_state(args->name, STRESS_STATE_RUN);

#if defined(O_PATH)
	path_fd = open("/bin/true", O_PATH | O_RDONLY);
#else
	path_fd = -1;
#endif

	do {
		errno = 0;
		/*
		 *  Try and open the file, it may be impossible
		 *  momentarily because other fcntl stressors have
		 *  already created it
		 */
		fd = creat(filename, S_IRUSR | S_IWUSR);
		if (fd < 0) {
			if ((errno == EPERM) || (errno == EACCES) ||
			    (errno == ENOMEM) || (errno == ENOSPC)) {
				(void)shim_usleep(100000);
				continue;
			}
			pr_fail("%s: creat %s failed, errno=%d (%s)\n",
				args->name, filename, errno, strerror(errno));
			goto tidy;
		} else {
			break;
		}
	} while (stress_continue_flag() && ++retries < 100);

	if ((fd < 0) || (retries >= 100)) {
		pr_err("%s: creat: file %s took %d "
			"retries to create (instance %" PRIu32 ")\n",
			args->name, filename, retries, args->instance);
		goto tidy;
	}

	rc = EXIT_SUCCESS;
	do {
		do_fcntl(args, fd, bad_fd, path_fd, &rc);
		stress_bogo_inc(args);
	} while (stress_continue(args));

tidy:
	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);

	if (path_fd >= 0)
		(void)close(path_fd);
	if (fd >= 0)
		(void)close(fd);
	(void)shim_unlink(filename);
	(void)shim_rmdir(pathname);

	if (setfl_flag_perms)
		free(setfl_flag_perms);

	return rc;
}

const stressor_info_t stress_fcntl_info = {
	.stressor = stress_fcntl,
	.classifier = CLASS_FILESYSTEM | CLASS_OS,
	.verify = VERIFY_ALWAYS,
	.help = help
};
