/*
 * Copyright (C) 2013-2021 Canonical, Ltd.
 * Copyright (C)      2022 Colin Ian King.
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

#if defined(HAVE_LINUX_OPENAT2_H)
#include <linux/openat2.h>
#endif

#if defined(HAVE_UTIME_H)
#include <utime.h>
#endif

typedef int (*stress_open_func_t)(void);

static const stress_help_t help[] = {
	{ "o N", "open N",	"start N workers exercising open/close" },
	{ NULL,	"open-ops N",	"stop after N open/close bogo operations" },
	{ NULL, "open-fd",	"open files in /proc/$pid/fd" },
	{ NULL,	NULL,		NULL }
};

static int open_count, *open_perms;

static int open_flags[] = {
#if defined(O_APPEND)
	O_APPEND,
#endif
#if defined(O_ASYNC)
	O_ASYNC,
#endif
#if defined(O_CLOEXEC)
	O_CLOEXEC,
#endif
#if defined(O_CREAT)
	O_CREAT,
#endif
#if defined(O_DIRECT)
	O_DIRECT,
#endif
#if defined(O_DIRECTORY)
	O_DIRECTORY,
#endif
#if defined(O_DSYNC)
	O_DSYNC,
#endif
#if defined(O_EXCL)
	O_EXCL,
#endif
#if defined(O_LARGEFILE)
	O_LARGEFILE,
#endif
#if defined(O_NOATIME)
	O_NOATIME,
#endif
#if defined(O_NOCTTY)
	O_NOCTTY,
#endif
#if defined(O_NOFOLLOW)
	O_NOFOLLOW,
#endif
#if defined(O_NONBLOCK)
	O_NONBLOCK,
#endif
#if defined(O_NDELAY)
	O_NDELAY,
#endif
#if defined(O_PATH)
	O_PATH,
#endif
#if defined(O_SYNC)
	O_SYNC,
#endif
#if defined(O_TMPFILE)
	O_TMPFILE,
#endif
#if defined(O_TRUNC)
	O_TRUNC,
#endif
/*
 *  FreeBSD extras
 */
#if defined(O_SEARCH)
	O_SEARCH,
#endif
#if defined(O_SHLOCK)
	O_SHLOCK,
#endif
#if defined(O_VERIFY)
	O_VERIFY,
#endif
#if defined(O_RESOLVE_BENEATH)
	O_RESOLVE_BENEATH,
#endif
/*
 *  OpenBSD extras
 */
#if defined(O_EXLOCK)
	O_EXLOCK,
#endif
/*
 *  NetBSD extras
 */
#if defined(O_NOSIGPIPE)
	O_NOSIGPIPE,
#endif
#if defined(O_RSYNC)
	O_RSYNC,
#endif
#if defined(O_ALT_IO)
	O_ALT_IO,
#endif
#if defined(O_REGULAR)
	O_REGULAR,
#endif
/*
 *  Oracle Solaris
 */
#if defined(O_NOLINKS)
	O_NOLINKS,
#endif
#if defined(O_TTY_INIT)
	O_TTY_INIT,
#endif
#if defined(O_XATTR)
	O_XATTR,
#endif
};

static int stress_set_open_fd(const char *opt)
{
	bool open_fd = true;

        (void)opt;
        return stress_set_setting("open-fd", TYPE_ID_BOOL, &open_fd);
}

static const stress_opt_set_func_t opt_set_funcs[] = {
	{ OPT_open_fd,	stress_set_open_fd, },
	{ 0,		NULL }
};

#if defined(HAVE_OPENAT) &&	\
    defined(AT_FDCWD)
/*
 *  obsolete_futimesat()
 *	modern libc maps the obsolete futimesat to utimesat
 */
static inline int obsolete_futimesat(
	const int dir_fd,
	const char *pathname,
	const struct timeval tv[2])
{
	int ret;

#if defined(__NR_futimesat)
	/* Try direct system call first */
	ret = (int)syscall(__NR_futimesat, dir_fd, pathname, tv);
	if ((ret == 0) || (errno != ENOSYS))
		return ret;
#endif
#if defined(HAVE_FUTIMESAT)
	/* Try libc variant next */
	ret = (int)futimesat(dir_fd, pathname, tv);
	if ((ret == 0) || (errno != ENOSYS))
		return ret;
#endif
	/* Not available */
	(void)dir_fd;
	(void)pathname;
	(void)tv;

	errno = ENOSYS;
	ret = -1;

	return ret;
}
#endif

/*
 *  obsolete_futimes()
 *	modern libc maps the obsolete futimes to utimes
 */
static inline int obsolete_futimes(const int fd, const struct timeval tv[2])
{
	int ret;

#if defined(__NR_futimes)
	/* Try direct system call first */
	ret = (int)syscall(__NR_futimes, fd, tv);
	if ((ret == 0) || (errno != ENOSYS))
		return ret;
#endif
#if defined(HAVE_FUTIMES)
	/* Try libc variant next */
	ret = (int)futimes(fd, tv);
	if ((ret == 0) || (errno != ENOSYS))
		return ret;
#endif
	/* Not available */
	(void)fd;
	(void)tv;

	errno = ENOSYS;
	ret = -1;

	return ret;
}

static inline int open_arg2(const char *pathname, const int flags)
{
	int fd;

#if defined(__NR_open)
	fd = (int)syscall(__NR_open, pathname, flags);
#else
	fd = open(pathname, flags);
#endif
	if (fd >= 0)
		(void)obsolete_futimes(fd, NULL);

	return fd;
}

static inline int open_arg3(const char *pathname, const int flags, const mode_t mode)
{
	int fd;

#if defined(__NR_open)
	fd = (int)syscall(__NR_open, pathname, flags, mode);
#else
	fd = open(pathname, flags, mode);
#endif

	if (fd >= 0) {
		struct timeval tv[2];

		/* Exercise illegal futimes, usec too small */
		tv[0].tv_usec = -1;
		tv[0].tv_sec = -1;
		tv[1].tv_usec = -1;
		tv[1].tv_sec = -1;
		(void)obsolete_futimes(fd, tv);

		/* Exercise illegal futimes, usec too large */
		tv[0].tv_usec = 1000000;
		tv[0].tv_sec = 0;
		tv[1].tv_usec = 1000000;
		tv[1].tv_sec = 0;
		(void)obsolete_futimes(fd, tv);

		/* Exercise illegal futimes, usec too small */
		tv[0].tv_usec = -1;
		tv[0].tv_sec = 0;
		tv[1].tv_usec = -1;
		tv[1].tv_sec = 0;
		(void)obsolete_futimes(fd, tv);
	}
	return fd;
}

static int open_flag_perm(void)
{
	static int index = 0;
	int fd;
	const int mode = S_IRUSR | S_IWUSR;
	const int flags = open_perms[index];
	char filename[PATH_MAX];

	(void)snprintf(filename, sizeof(filename), "stress-open-%d-%" PRIu32,
		(int)getpid(), stress_mwc32());

	if (UNLIKELY((open_count == 0) || (!open_perms))) {
		fd = open_arg3(filename, O_CREAT | O_RDWR, mode);
		(void)shim_unlink(filename);
		return fd;
	}

#if defined(O_CREATE)
	if (!(flags & O_CREATE)) {
#if defined(O_DIRECTORY)
		if (flags & O_DIRECTORY) {
			(void)mkdir(filename);
		} else
			fd = open_arg3(filename, O_CREAT | O_RDWR, mode);
			if (fd >= 0)
				(void)close(fd);
		}
	}
#else
	fd = open_arg3(filename, O_CREAT | O_RDWR, mode);
	if (fd >= 0)
		void)close(fd);
#endif
#endif
	fd = open_arg3(filename, flags, mode);
#if defined(O_DIRECTORY)
	if (flags & O_DIRECTORY)
		(void)shim_rmdir(filename);
#endif
	(void)shim_unlink(filename);
	index++;
	index %= open_count;

	return fd;
}

static int open_dev_zero_rd(void)
{
	int flags = 0;
#if defined(O_ASYNC)
	flags |= (stress_mwc32() & O_ASYNC);
#endif
#if defined(O_CLOEXEC)
	flags |= (stress_mwc32() & O_CLOEXEC);
#endif
#if defined(O_LARGEFILE)
	flags |= (stress_mwc32() & O_LARGEFILE);
#endif
#if defined(O_NOFOLLOW)
	flags |= (stress_mwc32() & O_NOFOLLOW);
#endif
#if defined(O_NONBLOCK)
	flags |= (stress_mwc32() & O_NONBLOCK);
#endif
#if defined(O_NDELAY)
	flags |= (stress_mwc32() & O_NDELAY);
#endif

	return open_arg2("/dev/zero", O_RDONLY | flags);
}

static int open_dev_null_wr(void)
{
	int flags = 0;
#if defined(O_ASYNC)
	flags |= (stress_mwc32() & O_ASYNC);
#endif
#if defined(O_CLOEXEC)
	flags |= (stress_mwc32() & O_CLOEXEC);
#endif
#if defined(O_LARGEFILE)
	flags |= (stress_mwc32() & O_LARGEFILE);
#endif
#if defined(O_NOFOLLOW)
	flags |= (stress_mwc32() & O_NOFOLLOW);
#endif
#if defined(O_NONBLOCK)
	flags |= (stress_mwc32() & O_NONBLOCK);
#endif
#if defined(O_DSYNC)
	flags |= (stress_mwc32() & O_DSYNC);
#endif
#if defined(O_SYNC)
	flags |= (stress_mwc32() & O_SYNC);
#endif

	return open_arg2("/dev/null", O_WRONLY | flags);
}

#if defined(O_TMPFILE)
static int open_tmp_rdwr(void)
{
	int flags = 0;
#if defined(O_TRUNC)
	flags |= (stress_mwc32() & O_TRUNC);
#endif
#if defined(O_APPEND)
	flags |= (stress_mwc32() & O_APPEND);
#endif
#if defined(O_NOATIME)
	flags |= (stress_mwc32() & O_NOATIME);
#endif
#if defined(O_DIRECT)
	flags |= (stress_mwc32() & O_DIRECT);
#endif
	return open_arg3("/tmp", O_TMPFILE | flags | O_RDWR, S_IRUSR | S_IWUSR);
}
#endif

#if defined(O_TMPFILE)
static int open_tmpfile_no_rdwr(void)
{
	/* Force -EINVAL, need O_WRONLY or O_RDWR to succeed */
	return open_arg3("/tmp", O_TMPFILE, S_IRUSR | S_IWUSR);
}
#endif

#if defined(HAVE_POSIX_OPENPT) &&	\
    defined(O_RDWR) &&			\
    defined(N_NOCTTY)
static int open_pt(void)
{
	return posix_openpt(O_RDWR | O_NOCTTY);
}
#endif

#if defined(O_TMPFILE) &&	\
    defined(O_EXCL)
static int open_tmp_rdwr_excl(void)
{
	return open_arg3("/tmp", O_TMPFILE | O_EXCL | O_RDWR, S_IRUSR | S_IWUSR);
}
#endif

#if defined(O_DIRECTORY)
static int open_dir(void)
{
	return open_arg2(".", O_DIRECTORY | O_RDONLY);
}
#endif

#if defined(O_PATH)
static int open_path(void)
{
	return open_arg2(".", O_DIRECTORY | O_PATH);
}
#endif

#if defined(O_CREAT)
static int open_create_eisdir(void)
{
	return open_arg3(".", O_CREAT, S_IRUSR | S_IWUSR);
}
#endif

#if defined(HAVE_OPENAT) &&	\
    defined(AT_FDCWD)
static int open_with_openat_cwd(void)
{
	char cwd[PATH_MAX];
	char filename[PATH_MAX];
	const char *temp_path = stress_get_temp_path();
	int fd, ret;

	if (!temp_path)
		return -1;
	if (!getcwd(cwd, sizeof(cwd)))
		return -1;
	if (chdir(temp_path) < 0)
		return -1;

	(void)snprintf(filename, sizeof(filename), "stress-open-%d-%" PRIu32,
		(int)getpid(), stress_mwc32());

	fd = openat(AT_FDCWD, filename, O_CREAT | O_RDWR, S_IRUSR | S_IWUSR);
	if (fd >= 0) {
		struct timeval tv[2];

		(void)obsolete_futimesat(AT_FDCWD, filename, NULL);

		/* Exercise illegal times */
		tv[0].tv_usec = 1000001;
		tv[0].tv_sec = 0;
		tv[1].tv_usec = 1000001;
		tv[1].tv_sec = 0;
		(void)obsolete_futimesat(AT_FDCWD, filename, tv);

		(void)shim_force_unlink(filename);
	}

	ret = chdir(cwd);
	(void)ret;
	return fd;
}
#endif

#if defined(HAVE_OPENAT) &&	\
    defined(AT_FDCWD) &&	\
    defined(O_PATH) &&		\
    defined(O_DIRECTORY)
static int open_with_openat_dir_fd(void)
{
	char filename[PATH_MAX];
	int fd, dir_fd;

	(void)snprintf(filename, sizeof(filename), "stress-open-%d-%" PRIu32,
		(int)getpid(), stress_mwc32());

	dir_fd = open_arg2(".", O_DIRECTORY | O_PATH);
	if (dir_fd < 0)
		return -1;

	fd = openat(dir_fd, filename, O_CREAT | O_RDWR, S_IRUSR | S_IWUSR);
	if (fd >= 0) {
		(void)obsolete_futimesat(dir_fd, filename, NULL);
		(void)shim_unlink(filename);
	}
	(void)close(dir_fd);
	return fd;
}
#endif

#if defined(HAVE_OPENAT2) &&		\
    defined(HAVE_LINUX_OPENAT2_H) &&	\
    defined(RESOLVE_NO_SYMLINKS) &&	\
    defined(__NR_openat2)
static int open_with_openat2_cwd(void)
{
	static const unsigned int resolve_flags[] = {
#if defined(RESOLVE_BENEATH)
		RESOLVE_BENEATH,
#endif
#if defined(RESOLVE_IN_ROOT)
		RESOLVE_IN_ROOT,
#endif
#if defined(RESOLVE_NO_MAGICLINKS)
		RESOLVE_NO_MAGICLINKS,
#endif
#if defined(RESOLVE_NO_SYMLINKS)
		RESOLVE_NO_SYMLINKS,
#endif
#if defined(RESOLVE_NO_XDEV)
		RESOLVE_NO_XDEV,
#endif
#if defined(RESOLVE_CACHED)
		RESOLVE_CACHED,
#endif
		0,
		(unsigned int)~0,	/* Intentionally illegal */
	};

	char cwd[PATH_MAX];
	char filename[PATH_MAX];
	const char *temp_path = stress_get_temp_path();
	int fd = -1, ret;
	size_t i = 0;
	static size_t j;
	struct open_how how;

	if (!temp_path)
		return -1;
	if (!getcwd(cwd, sizeof(cwd)))
		return -1;
	if (chdir(temp_path) < 0)
		return -1;
	(void)snprintf(filename, sizeof(filename), "stress-open-%d-%" PRIu32,
		(int)getpid(), stress_mwc32());

	/*
	 *  Work through resolve flags to find one that can
	 *  open the file successfully
	 */
	for (i = 0; i < SIZEOF_ARRAY(resolve_flags); i++) {
		(void)memset(&how, 0, sizeof(how));
		how.flags = O_CREAT | O_RDWR;
		how.mode = S_IRUSR | S_IWUSR;
		how.resolve = resolve_flags[j++];

		if (j >= SIZEOF_ARRAY(resolve_flags))
			j = 0;

		/* Exercise illegal usize field */
		fd = (int)syscall(__NR_openat2, AT_FDCWD, filename, &how, 0);
		if (fd >= 0) {
			/* Unxexpected, but handle it anyhow */
			(void)shim_unlink(filename);
			break;
		}

		fd = (int)syscall(__NR_openat2, AT_FDCWD, filename, &how, sizeof(how));
		if (fd >= 0) {
			(void)shim_unlink(filename);
			break;
		}
	}

	ret = chdir(cwd);
	(void)ret;

	return fd;
}
#endif

static stress_open_func_t open_funcs[] = {
	open_flag_perm,
	open_dev_zero_rd,
	open_dev_null_wr,
#if defined(O_TMPFILE)
	open_tmp_rdwr,
#endif
#if defined(O_TMPFILE) &&	\
    defined(O_EXCL)
	open_tmp_rdwr_excl,
#endif
#if defined(O_TMPFILE)
	open_tmpfile_no_rdwr,
#endif
#if defined(O_DIRECTORY)
	open_dir,
#endif
#if defined(O_PATH)
	open_path,
#endif
#if defined(HAVE_POSIX_OPENPT) &&	\
    defined(O_RDWR) &&			\
    defined(N_NOCTTY)
	open_pt,
#endif
#if defined(O_CREAT)
	open_create_eisdir,
#endif
#if defined(HAVE_OPENAT) &&		\
    defined(AT_FDCWD)
	open_with_openat_cwd,
#endif
#if defined(HAVE_OPENAT) &&	\
    defined(AT_FDCWD) &&	\
    defined(O_PATH) &&		\
    defined(O_DIRECTORY)
	open_with_openat_dir_fd,
#endif
#if defined(HAVE_OPENAT2) &&		\
    defined(HAVE_LINUX_OPENAT2_H) &&	\
    defined(RESOLVE_NO_SYMLINKS) &&	\
    defined(__NR_openat2)
	open_with_openat2_cwd,
#endif
};

/*
 *  stress_fd_dir()
 *	try to open the files the the stressor parent has open
 *	via the file names provided in /proc/$PID/fd. This
 *	ignores opens that fail.
 */
static void stress_fd_dir(const char *path)
{
	for (;;) {
		struct dirent *de;
		DIR *dir;

		dir = opendir(path);
		if (!dir)
			return;

		while ((de = readdir(dir)) != NULL) {
			char name[PATH_MAX];
			int fd;

			stress_mk_filename(name, sizeof(name), path, de->d_name);
			fd = open_arg2(name, O_RDONLY);
			if (fd >= 0)
				(void)close(fd);
		}
		(void)closedir(dir);
	}
}

/*
 *  stress_open()
 *	stress system by rapid open/close calls
 */
static int stress_open(const stress_args_t *args)
{
	int *fds;
	char path[PATH_MAX];
	size_t max_fds = stress_get_max_file_limit();
	size_t i, sz;
	pid_t pid = -1;
	const pid_t mypid = getpid();
	struct stat statbuf;
	bool open_fd = false;
	int all_open_flags;

	/*
	 *  32 bit systems may OOM if we have too many open fds, so
	 *  try to constrain the max limit as a workaround.
	 */
	if (sizeof(void *) == 4)
		max_fds = STRESS_MINIMUM(max_fds, 65536);

	sz = max_fds * sizeof(*fds);
	fds = (int *)mmap(NULL, sz, PROT_READ | PROT_WRITE,
		MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
	if (fds == MAP_FAILED) {
		max_fds = STRESS_FD_MAX;
		sz = max_fds * sizeof(*fds);
		fds = (int *)mmap(NULL, sz, PROT_READ | PROT_WRITE,
			MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
		if (fds == MAP_FAILED) {
			pr_inf("%s: cannot allocate file descriptors\n", args->name);
			return EXIT_NO_RESOURCE;
		}
	}

	(void)stress_get_setting("open-fd", &open_fd);
	if (open_fd) {
		(void)snprintf(path, sizeof(path), "/proc/%" PRIdMAX "/fd", (intmax_t)mypid);
		if ((stat(path, &statbuf) == 0) &&
		    ((statbuf.st_mode & S_IFMT) == S_IFDIR)) {
			pid = fork();

			if (pid == 0) {
				stress_fd_dir(path);
				_exit(0);
			}
		}
	}

	for (all_open_flags = 0, i = 0; i < SIZEOF_ARRAY(open_flags); i++)
		all_open_flags |= open_flags[i];
	open_count = stress_flag_permutation(all_open_flags, &open_perms);

	stress_set_proc_state(args->name, STRESS_STATE_RUN);

	do {
		size_t n;
		int ret;
		unsigned int min_fd = UINT_MAX, max_fd = 0;

		for (i = 0; i < max_fds; i++) {
			for (;;) {
				int idx;

				if (!keep_stressing(args)) {
					if (pid > 1)
						(void)kill(pid, SIGKILL);
					goto close_all;
				}

				idx = stress_mwc32() % SIZEOF_ARRAY(open_funcs);
				fds[i] = open_funcs[idx]();

				if (fds[i] >= 0)
					break;

				/* Check if we hit the open file limit */
				if ((errno == EMFILE) || (errno == ENFILE)) {
					if (pid > 1)
						(void)kill(pid, SIGKILL);
					goto close_all;
				}

				/* Other error occurred, retry */
			}
			if (fds[i] >= 0) {
				if (fds[i] > (int)max_fd)
					max_fd = (unsigned int)fds[i];
				if (fds[i] < (int)min_fd)
					min_fd = (unsigned int)fds[i];
			}

			stress_read_fdinfo(mypid, fds[i]);

			inc_counter(args);
		}
close_all:
		n = i;

		/*
		 *  try fast close of a range, fall back to
		 *  normal close if ENOSYS
		 */
		ret = shim_close_range(min_fd, max_fd, 0);
		if ((ret < 1) && (errno == ENOSYS)) {
			for (i = 0; i < n; i++)
				(void)close(fds[i]);
		}
	} while (keep_stressing(args));

	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);

	(void)munmap((void *)fds, sz);
	if (open_perms)
		free(open_perms);

	if (pid > 1) {
		int status;

		(void)kill(pid, SIGKILL);
		(void)waitpid(pid, &status, 0);
	}

	return EXIT_SUCCESS;
}

stressor_info_t stress_open_info = {
	.stressor = stress_open,
	.class = CLASS_FILESYSTEM | CLASS_OS,
	.opt_set_funcs = opt_set_funcs,
	.help = help
};
