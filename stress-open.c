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

#if defined(HAVE_LINUX_OPENAT2_H)
#include <linux/openat2.h>
#endif

#if defined(HAVE_UTIME_H)
#include <utime.h>
#endif

typedef int (*stress_open_func_t)(stress_args_t *args, const char *temp_dir, const pid_t pid, double *duration, double *count);

static const stress_help_t help[] = {
	{ "o N", "open N",		"start N workers exercising open/close" },
	{ NULL, "open-fd",		"open files in /proc/$pid/fd" },
	{ NULL,	"open-max N",		"specficify maximum number of files to open" },
	{ NULL,	"open-ops N",		"stop after N open/close bogo operations" },
	{ NULL,	NULL,			NULL }
};

static size_t open_count;
static int *open_perms;

static const int open_flags[] = {
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

static size_t stress_get_max_fds(void)
{
	const size_t max_size = (size_t)-1;
	size_t max_fds = 0;

#if defined(RLIMIT_NOFILE)
	struct rlimit rlim;

	if (getrlimit(RLIMIT_NOFILE, &rlim) == 0) {
		struct rlimit new_rlim = rlim;

		new_rlim.rlim_cur = new_rlim.rlim_max;
		if (setrlimit(RLIMIT_NOFILE, &new_rlim) == 0) {
			max_fds = stress_get_max_file_limit();
			(void)setrlimit(RLIMIT_NOFILE, &rlim);
		}
	}
#endif

	if (max_fds == 0)
		max_fds = stress_get_max_file_limit();
	if (max_fds > max_size)
		max_fds = max_size;

	return max_fds;
}

static void stress_open_max(const char *opt_name, const char *opt_arg, stress_type_id_t *type_id, void *value)
{
	size_t *open_max = (size_t *)value;
	const size_t max_fds = stress_get_max_fds();

	(void)opt_name;

	*type_id = TYPE_ID_SIZE_T;
	*open_max = (size_t)stress_get_uint64_percent(opt_arg, 1, (uint64_t)max_fds, NULL,
                        "cannot determine maximum number of file descriptors");
}

static const stress_opt_t opts[] = {
	{ OPT_open_fd,  "open-fd",  TYPE_ID_BOOL, 0, 1, NULL },
	{ OPT_open_max,	"open-max", TYPE_ID_CALLBACK, 0, 1, stress_open_max },
	END_OPT,
};

#if defined(HAVE_OPENAT) &&	\
    defined(AT_FDCWD)
/*
 *  obsolete_futimesat()
 *	modern libc maps the obsolete futimesat to utimesat
 */
static int obsolete_futimesat(
	const int dir_fd,
	const char *pathname,
	const struct timeval tv[2])
{
	int ret;

#if defined(__NR_futimesat) &&	\
    defined(HAVE_SYSCALL)
	/* Try direct system call first */
	ret = (int)syscall(__NR_futimesat, dir_fd, pathname, tv);
	if (LIKELY((ret == 0) || (errno != ENOSYS)))
		return ret;
#endif
#if defined(HAVE_FUTIMESAT_DEPRECATED)
	/* Try libc variant next */
	ret = (int)futimesat(dir_fd, pathname, tv);
	if (LIKELY((ret == 0) || (errno != ENOSYS)))
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
static int obsolete_futimes(const int fd, const struct timeval tv[2])
{
	int ret;

#if defined(__NR_futimes) &&	\
    defined(HAVE_SYSCALL)
	/* Try direct system call first */
	ret = (int)syscall(__NR_futimes, fd, tv);
	if (LIKELY((ret == 0) || (errno != ENOSYS)))
		return ret;
#endif
#if defined(HAVE_FUTIMES)
	/* Try libc variant next */
	ret = (int)futimes(fd, tv);
	if (LIKELY((ret == 0) || (errno != ENOSYS)))
		return ret;
#endif
	/* Not available */
	(void)fd;
	(void)tv;

	errno = ENOSYS;
	ret = -1;

	return ret;
}

static inline int open_arg2(
	const char *pathname,
	const int flags,
	double *duration,
	double *count)
{
	int fd;
	double t;

	t = stress_time_now();
#if defined(__NR_open) &&	\
    defined(HAVE_SYSCALL)
	fd = (int)syscall(__NR_open, pathname, flags);
#else
	fd = open(pathname, flags);
#endif
	if (LIKELY(fd >= 0)) {
		(*duration) += stress_time_now() - t;
		(*count) += 1.0;
		(void)obsolete_futimes(fd, NULL);
	}

	return fd;
}

static inline int open_arg3(
	const char *pathname,
	const int flags,
	const int mode,
	double *duration,
	double *count)
{
	int fd;
	double t;

	t = stress_time_now();
#if defined(__NR_open) &&	\
    defined(HAVE_SYSCALL)
	fd = (int)syscall(__NR_open, pathname, flags, mode);
#else
	fd = open(pathname, flags, mode);
#endif

	if (LIKELY(fd >= 0)) {
		struct timeval tv[2];

		(*duration) += stress_time_now() - t;
		(*count) += 1.0;

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

#if defined(O_CREAT)
static int open_flag_perm(
	stress_args_t *args,
	const char *temp_dir,
	const pid_t pid,
	double *duration,
	double *count)
{
	static size_t idx = 0;
	int fd;
	const int mode = S_IRUSR | S_IWUSR;
	const int flags = open_perms[idx];
	char filename[PATH_MAX];

	(void)args;

	(void)snprintf(filename, sizeof(filename), "%s/stress-open-%" PRIdMAX "-%" PRIu32,
		temp_dir, (intmax_t)pid, stress_mwc32());

	if (UNLIKELY((open_count == 0) || (!open_perms))) {
		fd = open_arg3(filename, O_CREAT | O_RDWR, mode, duration, count);
		(void)shim_unlink(filename);
		return fd;
	}

#if defined(O_DIRECTORY)
	if (!(flags & O_CREAT)) {
		if (flags & O_DIRECTORY) {
			(void)mkdir(filename, mode);
		} else {
			fd = open_arg3(filename, O_CREAT | O_RDWR, mode, duration, count);
			if (LIKELY(fd >= 0))
				(void)close(fd);
		}
	}
#else
	fd = open_arg3(filename, O_CREAT | O_RDWR, mode, duration, count);
	if (LIKELY(fd >= 0))
		(void)close(fd);
#endif
	fd = open_arg3(filename, flags, mode, duration, count);
#if defined(O_DIRECTORY)
	if (LIKELY(flags & O_DIRECTORY))
		(void)shim_rmdir(filename);
#endif
	(void)shim_unlink(filename);
	idx++;
	if (UNLIKELY(idx >= open_count))
		idx = 0;

	return fd;
}
#endif

static int open_dev_zero_rd(
	stress_args_t *args,
	const char *temp_dir,
	const pid_t pid,
	double *duration,
	double *count)
{
	int flags = 0;

	(void)args;
	(void)temp_dir,
	(void)pid;

#if defined(O_ASYNC)
	flags |= O_ASYNC;
#endif
#if defined(O_CLOEXEC)
	flags |= O_CLOEXEC;
#endif
#if defined(O_LARGEFILE)
	flags |= O_LARGEFILE;
#endif
#if defined(O_NOFOLLOW)
	flags |= O_NOFOLLOW;
#endif
#if defined(O_NONBLOCK)
	flags |= O_NONBLOCK;
#endif
#if defined(O_NDELAY)
	flags |= O_NDELAY;
#endif
	flags &= stress_mwc32();
	flags |= O_RDONLY;

	return open_arg2("/dev/zero", flags, duration, count);
}

static int open_dev_null_wr(
	stress_args_t *args,
	const char *temp_dir,
	const pid_t pid,
	double *duration,
	double *count)
{
	int flags = 0;

	(void)args;
	(void)temp_dir;
	(void)pid;

#if defined(O_ASYNC)
	flags |= O_ASYNC;
#endif
#if defined(O_CLOEXEC)
	flags |= O_CLOEXEC;
#endif
#if defined(O_LARGEFILE)
	flags |= O_LARGEFILE;
#endif
#if defined(O_NOFOLLOW)
	flags |= O_NOFOLLOW;
#endif
#if defined(O_NONBLOCK)
	flags |= O_NONBLOCK;
#endif
#if defined(O_DSYNC)
	flags |= O_DSYNC;
#endif
#if defined(O_SYNC)
	flags |= O_SYNC;
#endif
	flags &= stress_mwc32();
	flags |= O_WRONLY;

	return open_arg2("/dev/null", flags, duration, count);
}

#if defined(O_TMPFILE)
static int open_tmp_rdwr(
	stress_args_t *args,
	const char *temp_dir,
	const pid_t pid,
	double *duration,
	double *count)
{
	int fd, flags = O_TMPFILE;

	(void)args;
	(void)temp_dir;
	(void)pid;

#if defined(O_TRUNC)
	flags |= O_TRUNC;
#endif
#if defined(O_APPEND)
	flags |= O_APPEND;
#endif
#if defined(O_NOATIME)
	flags |= O_NOATIME;
#endif
#if defined(O_DIRECT)
	flags |= O_DIRECT;
#endif
	flags &= stress_mwc32();
	flags |= O_TMPFILE | O_RDWR;

	/* try temp_dir first, some filesystems do not support this */
	fd = open_arg3(temp_dir, flags, S_IRUSR | S_IWUSR, duration, count);
	if (fd == -1)
		fd = open_arg3("/tmp", flags, S_IRUSR | S_IWUSR, duration, count);

	return fd;
}
#endif

#if defined(O_TMPFILE)
static int open_tmpfile_no_rdwr(
	stress_args_t *args,
	const char *temp_dir,
	const pid_t pid,
	double *duration,
	double *count)
{
	(void)args;
	(void)temp_dir;
	(void)pid;

	/* Force -EINVAL, need O_WRONLY or O_RDWR to succeed */
	return open_arg3("/tmp", O_TMPFILE, S_IRUSR | S_IWUSR, duration, count);
}
#endif

#if defined(HAVE_POSIX_OPENPT) &&	\
    defined(O_RDWR) &&			\
    defined(N_NOCTTY)
static int open_pt(
	stress_args_t *args,
	const char *temp_dir,
	const pid_t pid,
	double *duration,
	double *count)
{
	(void)args;
	(void)temp_dir;
	(void)pid;

	return posix_openpt(O_RDWR | O_NOCTTY);
}
#endif

#if defined(O_TMPFILE) &&	\
    defined(O_EXCL)
static int open_tmp_rdwr_excl(
	stress_args_t *args,
	const char *temp_dir,
	const pid_t pid,
	double *duration,
	double *count)
{
	(void)args;
	(void)temp_dir;
	(void)pid;

	return open_arg3("/tmp", O_TMPFILE | O_EXCL | O_RDWR, S_IRUSR | S_IWUSR, duration, count);
}
#endif

#if defined(O_DIRECTORY)
static int open_dir(
	stress_args_t *args,
	const char *temp_dir,
	const pid_t pid,
	double *duration,
	double *count)
{
	(void)args;
	(void)pid;

	return open_arg2(temp_dir, O_DIRECTORY | O_RDONLY, duration, count);
}
#endif

#if defined(O_DIRECTORY) &&	\
    defined(__linux__)
static int open_dir_proc_self_fd(
	stress_args_t *args,
	const char *temp_dir,
	const pid_t pid,
	double *duration,
	double *count)
{
	(void)args;
	(void)temp_dir;
	(void)pid;

	return open_arg2("/proc/self/fd", O_DIRECTORY | O_RDONLY, duration, count);
}
#endif

#if defined(O_PATH)
static int open_path(
	stress_args_t *args,
	const char *temp_dir,
	const pid_t pid,
	double *duration,
	double *count)
{
	(void)args;
	(void)pid;

	return open_arg2(temp_dir, O_DIRECTORY | O_PATH, duration, count);
}
#endif

#if defined(O_CREAT)
static int open_create_eisdir(
	stress_args_t *args,
	const char *temp_dir,
	const pid_t pid,
	double *duration,
	double *count)
{
	(void)args;
	(void)pid;

	return open_arg3(temp_dir, O_CREAT, S_IRUSR | S_IWUSR, duration, count);
}
#endif

#if defined(O_DIRECT) &&	\
    defined(O_CREAT)
static int open_direct(
	stress_args_t *args,
	const char *temp_dir,
	const pid_t pid,
	double *duration,
	double *count)
{
	char filename[PATH_MAX];
	int fd;
	double t;

	(void)snprintf(filename, sizeof(filename), "%s/stress-open-%" PRIdMAX "-%" PRIu32,
		temp_dir, (intmax_t)pid, stress_mwc32());

	t = stress_time_now();
	fd = open(filename, O_RDWR | O_CREAT | O_TRUNC | O_DIRECT, S_IRUSR | S_IWUSR);
	if (LIKELY(fd >= 0)) {
		(*duration) += stress_time_now() - t;
		(*count) += 1.0;
	} else {
		int ret;
		struct stat statbuf;

		ret = shim_stat(filename, &statbuf);
		if (UNLIKELY(ret == 0)) {
			pr_inf("%s: open with O_DIRECT failed but file '%s' was created%s\n",
				args->name, filename, stress_get_fs_type(filename));
		}
	}
	(void)shim_unlink(filename);
	return fd;
}
#endif

#if defined(HAVE_OPENAT) &&	\
    defined(AT_FDCWD) &&	\
    defined(O_CREAT)
static int open_with_openat_cwd(
	stress_args_t *args,
	const char *temp_dir,
	const pid_t pid,
	double *duration,
	double *count)
{
	char cwd[PATH_MAX];
	char filename[PATH_MAX];
	const char *temp_path = stress_get_temp_path();
	int fd;
	double t;

	(void)args;

	if (!temp_path)
		return -1;
	if (!getcwd(cwd, sizeof(cwd)))
		return -1;
	if (chdir(temp_path) < 0)
		return -1;

	(void)snprintf(filename, sizeof(filename), "%s/stress-open-%" PRIdMAX "-%" PRIu32,
		temp_dir, (intmax_t)pid, stress_mwc32());

	t = stress_time_now();
	fd = openat(AT_FDCWD, filename, O_CREAT | O_RDWR, S_IRUSR | S_IWUSR);
	if (LIKELY(fd >= 0)) {
		struct timeval tv[2];

		(*duration) += stress_time_now() - t;
		(*count) += 1.0;

		(void)obsolete_futimesat(AT_FDCWD, filename, NULL);

		/* Exercise illegal times */
		tv[0].tv_usec = 1000001;
		tv[0].tv_sec = 0;
		tv[1].tv_usec = 0;
		tv[1].tv_sec = 1;
		(void)obsolete_futimesat(AT_FDCWD, filename, tv);
		tv[0].tv_usec = 0;
		tv[0].tv_sec = 1;
		tv[1].tv_usec = 1000001;
		tv[1].tv_sec = 0;
		(void)obsolete_futimesat(AT_FDCWD, filename, tv);

		(void)shim_force_unlink(filename);
	}

	VOID_RET(int, chdir(cwd));
	return fd;
}
#endif

#if defined(HAVE_OPENAT) &&	\
    defined(AT_FDCWD) &&	\
    defined(O_PATH) &&		\
    defined(O_DIRECTORY) &&	\
    defined(O_CREAT)
static int open_with_openat_dir_fd(
	stress_args_t *args,
	const char *temp_dir,
	const pid_t pid,
	double *duration,
	double *count)
{
	char filename[PATH_MAX];
	int fd, dir_fd;
	const uint32_t rnd32 = stress_mwc32();

	(void)args;

	(void)snprintf(filename, sizeof(filename), "stress-open-%" PRIdMAX "-%" PRIu32,
		(intmax_t)pid, rnd32);

	dir_fd = open_arg2(temp_dir, O_DIRECTORY | O_PATH, duration, count);
	if (dir_fd < 0)
		return -1;

	fd = openat(dir_fd, filename, O_CREAT | O_RDWR, S_IRUSR | S_IWUSR);
	if (LIKELY(fd >= 0)) {
		(void)obsolete_futimesat(dir_fd, filename, NULL);
		(void)snprintf(filename, sizeof(filename), "%s/stress-open-%" PRIdMAX "-%" PRIu32,
			temp_dir, (intmax_t)pid, rnd32);
		(void)shim_unlink(filename);
	}
	(void)close(dir_fd);
	return fd;
}
#endif

#if defined(HAVE_OPENAT2) &&		\
    defined(HAVE_LINUX_OPENAT2_H) &&	\
    defined(RESOLVE_NO_SYMLINKS) &&	\
    defined(__NR_openat2) &&		\
    defined(HAVE_SYSCALL) &&		\
    defined(O_CREAT)
static int open_with_openat2_cwd(
	stress_args_t *args,
	const char *temp_dir,
	const pid_t pid,
	double *duration,
	double *count)
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
	int fd = -1;
	size_t i = 0;
	static size_t j;
	struct open_how how;

	(void)args;

	if (!getcwd(cwd, sizeof(cwd)))
		return -1;
	if (chdir(temp_dir) < 0)
		return -1;
	(void)snprintf(filename, sizeof(filename), "stress-open-%" PRIdMAX "-%" PRIu32,
		(intmax_t)pid, stress_mwc32());

	/*
	 *  Work through resolve flags to find one that can
	 *  open the file successfully
	 */
	for (i = 0; i < SIZEOF_ARRAY(resolve_flags); i++) {
		double t;

		(void)shim_memset(&how, 0, sizeof(how));
		how.flags = O_CREAT | O_RDWR;
		how.mode = S_IRUSR | S_IWUSR;
		how.resolve = resolve_flags[j++];

		if (UNLIKELY(j >= SIZEOF_ARRAY(resolve_flags)))
			j = 0;

		/* Exercise illegal usize field */
		fd = (int)syscall(__NR_openat2, AT_FDCWD, filename, &how, 0);
		if (UNLIKELY(fd >= 0)) {
			/* Unxexpected, but handle it anyhow */
			(void)shim_unlink(filename);
			break;
		}

		t = stress_time_now();
		fd = (int)syscall(__NR_openat2, AT_FDCWD, filename, &how, sizeof(how));
		if (LIKELY(fd >= 0)) {
			(*duration) += stress_time_now() - t;
			(*count) += 1.0;
			(void)shim_unlink(filename);
			break;
		}
	}

	VOID_RET(int, chdir(cwd));
	return fd;
}
#endif

#if defined(__linux__)
static int open_with_open_proc_self_fd(
	stress_args_t *args,
	const char *temp_dir,
	const pid_t pid,
	double *duration,
	double *count)
{
	int fd;
	double t;

	(void)args;
	(void)temp_dir;
	(void)pid;

	t = stress_time_now();
	fd = open("/proc/self/fd/0", O_RDONLY);
	if (LIKELY(fd >= 0)) {
		(*duration) += stress_time_now() - t;
		(*count) += 1.0;
	}
	return fd;
}
#endif

static int open_dup(
	stress_args_t *args,
	const char *temp_dir,
	const pid_t pid,
	double *duration,
	double *count)
{
	int fd;
	double t;

	(void)args;
	(void)temp_dir;
	(void)pid;

	t = stress_time_now();
	fd = dup(STDOUT_FILENO);
	if (LIKELY(fd >= 0)) {
		(*duration) += stress_time_now() - t;
		(*count) += 1.0;
	}
	return fd;
}

#if defined(O_CREAT) &&	\
    defined(O_TRUNC)
static int open_rdonly_trunc(
	stress_args_t *args,
	const char *temp_dir,
	const pid_t pid,
	double *duration,
	double *count)
{
	char filename[PATH_MAX];
	int fd;

	(void)args;

	(void)snprintf(filename, sizeof(filename), "%s/stress-open-%" PRIdMAX "-%" PRIu32,
		temp_dir, (intmax_t)pid, stress_mwc32());

	fd = open_arg3(filename, O_CREAT | O_RDWR, S_IRUSR | S_IWUSR, duration, count);
	if (fd < 0)
		return fd;

	/* undefined behaviour, will open, may truncate on some systems */
	fd = open_arg2(filename, O_RDONLY | O_TRUNC, duration, count);
	(void)fd;
	(void)shim_unlink(filename);

	return fd;
}
#endif

#if defined(O_CREAT)
static int open_modes(
	stress_args_t *args,
	const char *temp_dir,
	const pid_t pid,
	double *duration,
	double *count)
{
	char filename[PATH_MAX];
	int fd;
	int mode_mask = 0777;
	static int mode = 0;

#if defined(S_ISVTX)
	mode_mask |= S_ISVTX;
#endif
#if defined(S_ISGID)
	mode_mask |= S_ISGID;
#endif
#if defined(S_ISUID)
	mode_mask |= S_ISUID;
#endif

	(void)args;

	(void)snprintf(filename, sizeof(filename), "%s/stress-open-%" PRIdMAX "-%" PRIu32,
		temp_dir, (intmax_t)pid, stress_mwc32());

	fd = open_arg3(filename, O_CREAT | O_RDWR, mode, duration, count);
	mode++;
	mode &= mode_mask;
	if (UNLIKELY(fd < 0))
		return fd;

	(void)shim_unlink(filename);
	return fd;
}
#endif

static const stress_open_func_t open_funcs[] = {
#if defined(O_CREAT)
	open_flag_perm,
#endif
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
    defined(AT_FDCWD) &&		\
    defined(O_CREAT)
	open_with_openat_cwd,
#endif
#if defined(HAVE_OPENAT) &&	\
    defined(AT_FDCWD) &&	\
    defined(O_PATH) &&		\
    defined(O_DIRECTORY) &&	\
    defined(O_CREAT)
	open_with_openat_dir_fd,
#endif
#if defined(HAVE_OPENAT2) &&		\
    defined(HAVE_LINUX_OPENAT2_H) &&	\
    defined(RESOLVE_NO_SYMLINKS) &&	\
    defined(__NR_openat2) &&		\
    defined(O_CREAT)
	open_with_openat2_cwd,
#endif
#if defined(__linux__)
	open_with_open_proc_self_fd,
#endif
	open_dup,
#if defined(O_DIRECT) &&        \
    defined(O_CREAT)
	open_direct,
#endif
#if defined(O_DIRECTORY) &&	\
    defined(__linux__)
	open_dir_proc_self_fd,
#endif
#if defined(O_CREAT) &&	\
    defined(O_TRUNC)
	open_rdonly_trunc,
#endif
#if defined(O_CREAT)
	open_modes,
#endif
};

/*
 *  stress_fd_dir()
 *	try to open the files the stressor parent has open
 *	via the file names provided in /proc/$PID/fd. This
 *	ignores opens that fail.
 */
static void stress_fd_dir(const char *path, double *duration, double *count)
{
	for (;;) {
		const struct dirent *de;
		DIR *dir;

		dir = opendir(path);
		if (!dir)
			return;

		while ((de = readdir(dir)) != NULL) {
			char name[PATH_MAX];
			int fd;

			stress_mk_filename(name, sizeof(name), path, de->d_name);
			fd = open_arg2(name, O_RDONLY, duration, count);
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
static int stress_open(stress_args_t *args)
{
	int *fds, ret;
	char path[PATH_MAX], temp_dir[PATH_MAX];
	const size_t max_size = (size_t)-1;
	size_t open_max = stress_get_max_file_limit();
	size_t i, sz;
	pid_t pid = -1;
	const pid_t mypid = getpid();
	struct stat statbuf;
	bool open_fd = false;
	int all_open_flags;
	double duration = 0.0, count = 0.0, rate;

	/*
	 *  32 bit systems may OOM if we have too many open fds, so
	 *  try to constrain the default max limit as a workaround.
	 */
	if (sizeof(void *) == 4)
		open_max = STRESS_MINIMUM(open_max, 65536);

	ret = stress_temp_dir_mk_args(args);
	if (ret < 0)
		return stress_exit_status(-ret);
	(void)stress_temp_dir_args(args, temp_dir, sizeof(temp_dir));

	if (!stress_get_setting("open-max", &open_max)) {
		if (g_opt_flags & OPT_FLAGS_MINIMIZE)
			open_max = 1;
	}
	(void)stress_get_setting("open-fd", &open_fd);

	/* Limit to maximum size_t allocation size */
	if (open_max > (max_size - 1) / sizeof(*fds))
		open_max = (max_size -1) / sizeof(*fds);
	/* Limit to max int (fd value) */
	if (open_max > INT_MAX)
		open_max = INT_MAX;
	if (open_max < 1)
		open_max = 1;

	sz = open_max * sizeof(*fds);
	fds = (int *)mmap(NULL, sz, PROT_READ | PROT_WRITE,
			MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
	if (fds == MAP_FAILED) {
		/* shrink */
		open_max = 1024 * 1024;
		sz = open_max * sizeof(*fds);
		fds = (int *)mmap(NULL, sz, PROT_READ | PROT_WRITE,
				MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
		if (fds == MAP_FAILED) {
			pr_inf_skip("%s: cannot mmap %zu file descriptors%s, "
				"errno=%d (%s), skipping stressor\n",
				args->name, open_max, stress_get_memfree_str(),
				errno, strerror(errno));
			return EXIT_NO_RESOURCE;
		}
	}
	if (!args->instance)
		pr_inf("%s: using a maximum of %zd file descriptors\n", args->name, open_max);
	stress_set_vma_anon_name(fds, sz, "fds");

	if (open_fd) {
		(void)snprintf(path, sizeof(path), "/proc/%" PRIdMAX "/fd", (intmax_t)mypid);
		if ((shim_stat(path, &statbuf) == 0) &&
		    ((statbuf.st_mode & S_IFMT) == S_IFDIR)) {
			pid = fork();

			if (pid == 0) {
				stress_set_proc_state(args->name, STRESS_STATE_RUN);
				stress_fd_dir(path, &duration, &count);
				_exit(0);
			}
		}
	}

	for (all_open_flags = 0, i = 0; i < SIZEOF_ARRAY(open_flags); i++)
		all_open_flags |= open_flags[i];
	open_count = stress_flag_permutation(all_open_flags, &open_perms);

	stress_set_proc_state(args->name, STRESS_STATE_SYNC_WAIT);
	stress_sync_start_wait(args);
	stress_set_proc_state(args->name, STRESS_STATE_RUN);

	do {
		size_t n;
		unsigned int min_fd = UINT_MAX, max_fd = 0;

		for (i = 0; i < open_max; i++) {
			for (;;) {
				int idx;

				if (UNLIKELY(!stress_continue(args))) {
					if (pid > 1)
						(void)stress_kill_pid(pid);
					goto close_all;
				}

				idx = stress_mwc32modn(SIZEOF_ARRAY(open_funcs));
				fds[i] = open_funcs[idx](args, temp_dir, mypid, &duration, &count);

				if (fds[i] >= 0)
					break;

				/* Check if we hit the open file limit */
				if (UNLIKELY((errno == EMFILE) || (errno == ENFILE))) {
					if (pid > 1)
						(void)stress_kill_pid(pid);
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

			if (UNLIKELY((i & 8191) == 8191))
				shim_sync();

			stress_bogo_inc(args);
		}
close_all:
		n = i;

		/*
		 *  try fast close of a range, fall back to
		 *  normal close if ENOSYS
		 */
		shim_sync();
		errno = 0;
		ret = shim_close_range(min_fd, max_fd, 0);
		if (ret < 0) {
			for (i = 0; i < n; i++)
				(void)close(fds[i]);
		}
	} while (stress_continue(args));

	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);

	(void)stress_temp_dir_rm_args(args);
	(void)munmap((void *)fds, sz);
	if (open_perms)
		free(open_perms);

	if (pid > 1)
		(void)stress_kill_pid_wait(pid, NULL);

	rate = (count > 0.0) ? duration / count: 0.0;
	stress_metrics_set(args, 0, "nanosecs per open",
		rate * 1000000000.0, STRESS_METRIC_HARMONIC_MEAN);

	return EXIT_SUCCESS;
}

const stressor_info_t stress_open_info = {
	.stressor = stress_open,
	.classifier = CLASS_FILESYSTEM | CLASS_OS,
	.opts = opts,
	.help = help
};
