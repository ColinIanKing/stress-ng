/*
 * Copyright (C) 2012-2021 Canonical, Ltd.
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

#include <sys/ioctl.h>

#if defined(HAVE_SYS_EPOLL_H)
#include <sys/epoll.h>
#endif

#if defined(HAVE_SYS_INOTIFY_H)
#include <sys/inotify.h>
#endif

#if !defined(INOTIFY_IOC_SETNEXTWD) &&	\
    defined(_IOW)
#define INOTIFY_IOC_SETNEXTWD	_IOW('I', 0, int32_t)
#endif

static const stress_help_t help[] = {
	{ NULL,	"inotify N",	 "start N workers exercising inotify events" },
	{ NULL,	"inotify-ops N", "stop inotify workers after N bogo operations" },
	{ NULL, NULL,		 NULL }
};

#if defined(HAVE_INOTIFY) &&		\
    defined(HAVE_INOTIFY1) &&		\
    defined(HAVE_SYS_INOTIFY_H) &&	\
    defined(HAVE_SYS_SELECT_H) && 	\
    defined(HAVE_SELECT) &&		\
    NEED_GLIBC(2,9,0)

#define DIR_FLAGS	(S_IRWXU | S_IRWXG)
#define FILE_FLAGS	(S_IRUSR | S_IWUSR)

#define TIME_OUT	(10)	/* Secs for inotify to report back */
#define BUF_SIZE	(4096)

typedef int (*stress_inotify_helper)(stress_args_t *args, const char *path, void *private);
typedef int (*stress_inotify_func)(stress_args_t *args, const char *path, const int bad_fd);

typedef struct {
	const stress_inotify_func func;
	const char*	description;
} stress_inotify_t;

/*
 * exercise_inotify1()
 * 	exercise inotify1 with all valid and invalid flags
 */
static void exercise_inotify1(void)
{
#if defined(IN_NONBLOCK) &&	\
    defined(IN_CLOEXEC)
	int fd;

	fd = inotify_init1(IN_NONBLOCK);
	if (fd >= 0)
		(void)close(fd);

	fd = inotify_init1(IN_CLOEXEC);
	if (fd >= 0)
		(void)close(fd);

	/* Exercise inotify1 with invalid flag */
	fd = inotify_init1(~0);
	if (fd >= 0)
		(void)close(fd);
#else
	UNEXPECTED
#endif
}

/*
 * exercise_inotify_add_watch()
 * 	exercise inotify_add_watch with all valid and invalid mask
 */
static void exercise_inotify_add_watch(
	const char *watchname,
	const int bad_fd)
{
	int fd, wd;
#if defined(IN_MASK_CREATE) &&	\
    defined(IN_MASK_ADD)
	int wd2;
#endif
	(void)bad_fd;

	fd = inotify_init();
	if (fd < 0)
		return;

	/* Exercise inotify_add_watch on invalid mask */
	wd = inotify_add_watch(fd, watchname, 0);
	if (wd >= 0)
		(void)inotify_rm_watch(fd, wd);

#if defined(INOTIFY_IOC_SETNEXTWD)
	/*
	 *  Exercise INOTIFY_IOC_SETNEXTWD
	 */
	VOID_RET(int, ioctl(fd, INOTIFY_IOC_SETNEXTWD, 8192));
#endif
	wd = inotify_add_watch(fd, watchname, ~0U);
	if (wd >= 0)
		(void)inotify_rm_watch(fd, wd);

#if defined(IN_MASK_CREATE) &&	\
    defined(IN_MASK_ADD)
	/* Exercise inotify_add_watch with two operations */
	wd = inotify_add_watch(fd, watchname, IN_MASK_CREATE | IN_MASK_ADD);
	if (wd >= 0)
		(void)inotify_rm_watch(fd, wd);
#else
	UNEXPECTED
#endif

#if defined(IN_MASK_CREATE) &&	\
    defined(IN_MASK_ADD)
	/*
	 * Exercise invalid inotify_add_watch by passing IN_MASK_CREATE in mask
	 * and pathname refers to a file already being watched by the same fd
	 */
	wd = inotify_add_watch(fd, watchname, IN_MASK_ADD);
	wd2 = inotify_add_watch(fd, watchname, IN_MASK_CREATE);
	if (wd >= 0)
		(void)inotify_rm_watch(fd, wd);
	if (wd2 >= 0)
		(void)inotify_rm_watch(fd, wd2);
#else
	UNEXPECTED
#endif

#if defined(IN_MASK_ADD)
	/* Exercise inotify_add_watch on bad_fd */
	wd = inotify_add_watch(bad_fd, watchname, IN_MASK_ADD);
	if (wd >= 0)
		(void)inotify_rm_watch(fd, wd);
#else
	UNEXPECTED
#endif

	(void)close(fd);
}

/*
 * exercise_inotify_rm_watch()
 * 	exercise inotify_rm_watch with all valid and invalid mask
 */
static void exercise_inotify_rm_watch(const int bad_fd)
{
	int fd;

	(void)bad_fd;

	fd = inotify_init();
	if (fd < 0)
		return;

	/* Exercise inotify_rm_watch on bad fd */
	VOID_RET(int, inotify_rm_watch(bad_fd, -1));

	/* Exercise inotify_rm_watch on invalid wd */
	VOID_RET(int, inotify_rm_watch(fd, 1));

	/* Close inotify file descriptor */
	(void)close(fd);

	/* Exercise inotify_rm_watch on non inotify fd */
#if defined(HAVE_EPOLL_CREATE1)
	fd = epoll_create1(0);
	if (fd < 0)
		return;
	VOID_RET(int, inotify_rm_watch(fd, 1));
#else
	UNEXPECTED
#endif

	(void)close(fd);
}

/*
 *  inotify_exercise()
 *	run a given test helper function 'func' and see if this triggers the
 *	required inotify event flags 'flags'.
 */
static int inotify_exercise(
	stress_args_t *args,	/* Stressor args */
	const char *filename,	/* Filename in test */
	const char *watchname,	/* File/directory to watch using inotify */
	const char *matchname,	/* Filename for inotify event to report */
	const stress_inotify_helper func, /* Helper func */
	const uint32_t flags,	/* IN_* flags to watch for */
	void *private,		/* Helper func private data */
	const int bad_fd)	/* A bad file descriptor */
{
	int fd, wd, n = 0, rc = EXIT_SUCCESS;
	uint32_t check_flags = flags;
	char buffer[1024];

	exercise_inotify1();
	exercise_inotify_add_watch(watchname, bad_fd);
	exercise_inotify_rm_watch(bad_fd);

retry:
	n++;
	if ((fd = inotify_init()) < 0) {
		if (UNLIKELY(!stress_continue(args)))
			return EXIT_SUCCESS;

		/* This is just so wrong... */
		if (errno == EMFILE) {
			/*
			 * inotify cleanup may be still running from a previous
			 * iteration, in which case we've run out of resources
			 * temporarily, so sleep a short while and retry.
			 */
			(void)shim_usleep(100000);
			goto retry;
		}
		/* Nope, give up, not necessarily a test failure, we maybe low on fds */
		pr_warn("%s: inotify_init failed, errno=%d (%s) after %" PRIu32 " calls\n",
			args->name, errno, strerror(errno), n);
		return EXIT_SUCCESS;
	}


	if ((wd = inotify_add_watch(fd, watchname, flags)) < 0) {
		(void)close(fd);
		pr_fail("%s: inotify_add_watch failed, errno=%d (%s)\n",
			args->name, errno, strerror(errno));
		return EXIT_FAILURE;
	}

	if (func(args, filename, private) < 0)
		goto cleanup;

	while (check_flags) {
		ssize_t len, i = 0;
		struct timeval tv;
		fd_set rfds;
		int err, nbytes;

		/* We give inotify TIME_OUT seconds to report back */
		tv.tv_sec = TIME_OUT;
		tv.tv_usec = 0;

		FD_ZERO(&rfds);
		FD_SET(fd, &rfds);

		/* Wait for an inotify event ... */
		err = select(fd + 1, &rfds, NULL, NULL, &tv);
		if (err == -1) {
			if (errno != EINTR)
				pr_err("%s: select error, errno=%d (%s)\n",
					args->name, errno, strerror(errno));
			break;
		} else if (err == 0) {
			if (g_opt_flags & OPT_FLAGS_VERIFY) {
				pr_fail("%s: timed out waiting for event flags 0x%x\n", args->name, flags);
				rc = EXIT_FAILURE;
			}
			break;
		}

redo:
		if (UNLIKELY(!stress_continue(args)))
			break;
		/*
		 *  Exercise FIOREAD to get inotify code coverage up
		 */
		if (ioctl(fd, FIONREAD, &nbytes) < 0) {
			if (g_opt_flags & OPT_FLAGS_VERIFY) {
				pr_fail("%s: data is ready, but ioctl FIONREAD failed\n", args->name);
				rc = EXIT_FAILURE;
				break;
			}
		}
		if (nbytes <= 0) {
			pr_fail("%s: data is ready, but ioctl FIONREAD "
				"reported %d bytes available\n", args->name, nbytes);
			rc = EXIT_FAILURE;
			break;
		}

		len = read(fd, buffer, sizeof(buffer));

		if ((len <= 0) || (len > (ssize_t)sizeof(buffer))) {
			if ((errno == EAGAIN) || (errno == EINTR))
				goto redo;
			if (errno) {
				pr_fail("%s: inotify fd read, errno=%d (%s)\n",
					args->name, errno, strerror(errno));
				rc = EXIT_FAILURE;
				break;
			}
		}

		/* Scan through inotify events */
		while ((i >= 0) && (i <= len - (ssize_t)sizeof(struct inotify_event))) {
			struct inotify_event *event =
				(struct inotify_event *)&buffer[i];
			uint32_t f = event->mask & (IN_DELETE_SELF |
						    IN_MOVE_SELF |
						    IN_MOVED_TO |
						    IN_MOVED_FROM |
						    IN_ATTRIB);
			if (event->len &&
			    strcmp(event->name, matchname) == 0 &&
			    flags & event->mask)
				check_flags &= ~(flags & event->mask);
			else if (flags & f)
				check_flags &= ~(flags & event->mask);

			i += sizeof(struct inotify_event) + event->len;
		}
	}

cleanup:
	(void)inotify_rm_watch(fd, wd);
	if (close(fd) < 0) {
		pr_fail("%s: close error, errno=%d (%s)\n",
			args->name, errno, strerror(errno));
		rc = EXIT_FAILURE;
	}
	return rc;
}

/*
 *  rm_file()
 *	remove a file
 */
static int rm_file(stress_args_t *args, const char *path)
{
	if ((shim_unlink(path) < 0) && (errno != ENOENT)) {
		pr_err("%s: cannot remove file %s, errno=%d (%s)\n",
			args->name, path, errno, strerror(errno));
		return -1;
	}
	return 0;
}

/*
 *  rm_dir()
 *	clean files in directory and directory
 */
static int rm_dir(stress_args_t *args, const char *path)
{
	DIR *dp;
	int ret;

	dp = opendir(path);
	if (dp != NULL) {
		struct dirent *d;

		while ((d = readdir(dp)) != NULL) {
			char filename[PATH_MAX];

			if (stress_is_dot_filename(d->d_name))
				continue;
			(void)stress_mk_filename(filename, sizeof(filename),
				path, d->d_name);
			(void)rm_file(args, filename);
		}
		(void)closedir(dp);
	}
	ret = shim_rmdir(path);
	if ((ret < 0) && (errno != ENOENT))
		pr_err("%s: cannot remove directory %s, errno=%d (%s)\n",
			args->name, path, errno, strerror(errno));
	return ret;
}

/*
 *  mk_dir()
 *	make a directory
 */
static int mk_dir(stress_args_t *args, const char *path)
{
	if (mkdir(path, DIR_FLAGS) < 0) {
		if ((errno == ENOMEM) || (errno == ENOSPC))
			return -1;
		pr_err("%s: cannot mkdir %s, errno=%d (%s)\n",
			args->name, path, errno, strerror(errno));
		return -1;
	}
	return 0;
}

/*
 *  mk_file()
 *	create file of length len bytes
 */
static int mk_file(stress_args_t *args, const char *filename, const size_t len)
{
	int fd;
	size_t sz = len;

	char buffer[BUF_SIZE];

	(void)rm_file(args, filename);
	if ((fd = open(filename, O_CREAT | O_RDWR, FILE_FLAGS)) < 0) {
		if ((errno == ENFILE) || (errno == ENOMEM) || (errno == ENOSPC))
			return -1;
		pr_err("%s: cannot create file %s, errno=%d (%s)\n",
			args->name, filename, errno, strerror(errno));
		return -1;
	}

	(void)shim_memset(buffer, 'x', BUF_SIZE);
	while (LIKELY(stress_continue(args) && (sz > 0))) {
		size_t n = (sz > BUF_SIZE) ? BUF_SIZE : sz;
		ssize_t ret;

		ret = write(fd, buffer, n);
		if (ret < 0) {
			if (errno == ENOSPC)
				break;
			pr_err("%s: error writing to file %s, errno=%d (%s)\n",
				args->name, filename, errno, strerror(errno));
			(void)close(fd);
			return -1;
		}
		sz -= (size_t)ret;
	}

	if (close(fd) < 0) {
		pr_err("%s: cannot close file %s, errno=%d (%s)\n",
			args->name, filename, errno, strerror(errno));
		return -1;
	}
	return 0;
}

#if defined(IN_ATTRIB)
static int inotify_attrib_helper(
	stress_args_t *args,
	const char *path,
	void *signum)
{
	(void)signum;
	if (chmod(path, S_IRUSR | S_IWUSR) < 0) {
		pr_err("%s: cannot chmod file %s, errno=%d (%s)\n",
			args->name, path, errno, strerror(errno));
		return -1;
	}
	return 0;
}

static int inotify_attrib_file(
	stress_args_t *args,
	const char *path,
	const int bad_fd)
{
	char filepath[PATH_MAX];
	int rc;

	stress_mk_filename(filepath, sizeof(filepath), path, "inotify_file");
	if (mk_file(args, filepath, 4096) < 0)
		return EXIT_SUCCESS;

	rc = inotify_exercise(args, filepath, path, "inotify_file",
		inotify_attrib_helper, IN_ATTRIB, NULL, bad_fd);
	(void)rm_file(args, filepath);

	return rc;
}
#else
	UNEXPECTED
#endif

#if defined(IN_ACCESS)
static int inotify_access_helper(
	stress_args_t *args,
	const char *path,
	void *signum)
{
	int fd;
	char buffer[1];
	int rc = 0;

	(void)signum;
	if ((fd = open(path, O_RDONLY)) < 0) {
		pr_err("%s: cannot open file %s, errno=%d (%s)\n",
			args->name, path, errno, strerror(errno));
		return -1;
	}

	/* Just want to force an access */
do_access:
	if (stress_continue(args) && (read(fd, buffer, 1) < 0)) {
		if ((errno == EAGAIN) || (errno == EINTR))
			goto do_access;
		pr_err("%s: cannot read file %s, errno=%d (%s)\n",
			args->name, path, errno, strerror(errno));
		rc = -1;
	}
	(void)close(fd);
	return rc;
}

static int inotify_access_file(
	stress_args_t *args,
	const char *path,
	const int bad_fd)
{
	char filepath[PATH_MAX];
	int rc;

	stress_mk_filename(filepath, sizeof(filepath), path, "inotify_file");
	if (mk_file(args, filepath, 4096) < 0)
		return EXIT_SUCCESS;

	rc = inotify_exercise(args, filepath, path, "inotify_file",
		inotify_access_helper, IN_ACCESS, NULL, bad_fd);
	(void)rm_file(args, filepath);

	return rc;
}
#else
	UNEXPECTED
#endif

#if defined(IN_MODIFY)
static int inotify_modify_helper(
	stress_args_t *args,
	const char *path,
	void *signum)
{
	int fd, rc = 0;
	char buffer[1] = { 0 };

	(void)signum;
	if (mk_file(args, path, 4096) < 0)
		return -1;
	if ((fd = open(path, O_RDWR)) < 0) {
		pr_err("%s: cannot open file %s, errno=%d (%s)\n",
			args->name, path, errno, strerror(errno));
		rc = -1;
		goto remove;
	}
do_modify:
	if (stress_continue(args) && (write(fd, buffer, 1) < 0)) {
		if ((errno == EAGAIN) || (errno == EINTR))
			goto do_modify;
		pr_err("%s: cannot write to file %s, errno=%d (%s)\n",
			args->name, path, errno, strerror(errno));
		rc = -1;
	}
	(void)close(fd);
remove:
	(void)rm_file(args, path);

	return rc;
}

static int inotify_modify_file(
	stress_args_t *args,
	const char *path,
	const int bad_fd)
{
	char filepath[PATH_MAX];

	stress_mk_filename(filepath, sizeof(filepath), path, "inotify_file");
	return inotify_exercise(args, filepath, path, "inotify_file",
		inotify_modify_helper, IN_MODIFY, NULL, bad_fd);
}
#else
	UNEXPECTED
#endif

#if defined(IN_CREATE)
static int inotify_creat_helper(
	stress_args_t *args,
	const char *path,
	void *signum)
{
	int fd;

	(void)signum;

	if ((fd = creat(path, FILE_FLAGS)) < 0) {
		pr_err("%s: cannot create file %s, errno=%d (%s)\n",
			args->name, path, errno, strerror(errno));
		return -1;
	}
	(void)close(fd);
	return 0;
}

static int inotify_creat_file(
	stress_args_t *args,
	const char *path,
	const int bad_fd)
{
	char filepath[PATH_MAX];
	int rc;

	stress_mk_filename(filepath, sizeof(filepath), path, "inotify_file");
	rc = inotify_exercise(args, filepath, path, "inotify_file",
		inotify_creat_helper, IN_CREATE, NULL, bad_fd);
	(void)rm_file(args, filepath);

	return rc;
}
#else
	UNEXPECTED
#endif

#if defined(IN_OPEN)
static int inotify_open_helper(
	stress_args_t *args,
	const char *path,
	void *signum)
{
	int fd;

	(void)signum;
	if ((fd = open(path, O_RDONLY)) < 0) {
		pr_err("%s: cannot open file %s, errno=%d (%s)\n",
			args->name, path, errno, strerror(errno));
		return -1;
	}
	(void)close(fd);
	return 0;
}

static int inotify_open_file(
	stress_args_t *args,
	const char *path,
	const int bad_fd)
{
	char filepath[PATH_MAX];
	int rc;

	stress_mk_filename(filepath, sizeof(filepath), path, "inotify_file");
	if (mk_file(args, filepath, 4096) < 0)
		return EXIT_SUCCESS;
	rc = inotify_exercise(args, filepath, path, "inotify_file",
		inotify_open_helper, IN_OPEN, NULL, bad_fd);
	(void)rm_file(args, filepath);

	return rc;
}
#else
	UNEXPECTED
#endif

#if defined(IN_DELETE)
static int inotify_delete_helper(
	stress_args_t *args,
	const char *path,
	void *signum)
{
	(void)signum;

	return rm_file(args, path);
}

static int inotify_delete_file(
	stress_args_t *args,
	const char *path,
	const int bad_fd)
{
	char filepath[PATH_MAX];
	int rc;

	stress_mk_filename(filepath, sizeof(filepath), path, "inotify_file");
	if (mk_file(args, filepath, 4096) < 0)
		return EXIT_SUCCESS;
	rc = inotify_exercise(args, filepath, path, "inotify_file",
		inotify_delete_helper, IN_DELETE, NULL, bad_fd);
	/* We remove (again) it just in case the test failed */
	(void)rm_file(args, filepath);

	return rc;
}
#else
	UNEXPECTED
#endif

#if defined(IN_DELETE_SELF)
static int inotify_delete_self_helper(
	stress_args_t *args,
	const char *path,
	void *signum)
{
	(void)signum;

	return rm_dir(args, path);
}

static int inotify_delete_self(
	stress_args_t *args,
	const char *path,
	const int bad_fd)
{
	char filepath[PATH_MAX];
	int rc;

	stress_mk_filename(filepath, sizeof(filepath), path, "inotify_dir");
	if (mk_dir(args, filepath) < 0)
		return EXIT_SUCCESS;
	rc = inotify_exercise(args, filepath, filepath, "inotify_dir",
		inotify_delete_self_helper, IN_DELETE_SELF, NULL, bad_fd);
	/* We remove (again) in case the test failed */
	(void)rm_dir(args, filepath);

	return rc;
}
#else
	UNEXPECTED
#endif

#if defined(IN_MOVE_SELF)
static int inotify_move_self_helper(
	stress_args_t *args,
	const char *oldpath,
	void *private)
{
	const char *newpath = (const char *)private;

	if (rename(oldpath, newpath) < 0) {
		pr_err("%s: cannot rename %s to %s, errno=%d (%s)\n",
			args->name, oldpath, newpath, errno, strerror(errno));
		return -1;
	}
	return 0;
}
#else
	UNEXPECTED
#endif

static int inotify_move_self(
	stress_args_t *args,
	const char *path,
	const int bad_fd)
{
	char filepath[PATH_MAX], newpath[PATH_MAX];
	int rc;

	stress_mk_filename(filepath, sizeof(filepath), path, "inotify_dir");
	if (mk_dir(args, filepath) < 0)
		return EXIT_SUCCESS;
	stress_mk_filename(newpath, sizeof(newpath), path, "renamed_dir");

	rc = inotify_exercise(args, filepath, filepath, "inotify_dir",
		inotify_move_self_helper, IN_MOVE_SELF, newpath, bad_fd);
	(void)rm_dir(args, newpath);
	(void)rm_dir(args, filepath);	/* In case rename failed */

	return rc;
}

#if defined(IN_MOVED_TO)
static int inotify_moved_to_helper(
	stress_args_t *args,
	const char *newpath,
	void *private)
{
	const char *oldpath = (const char *)private;

	if (rename(oldpath, newpath) < 0) {
		pr_err("%s: cannot rename %s to %s, errno=%d (%s)\n",
			args->name, oldpath, newpath, errno, strerror(errno));
		return -1;
	}
	return 0;
}

static int inotify_moved_to(
	stress_args_t *args,
	const char *path,
	const int bad_fd)
{
	char olddir[PATH_MAX - 16], oldfile[PATH_MAX], newfile[PATH_MAX];
	int rc;

	stress_mk_filename(olddir, sizeof(olddir), path, "new_dir");
	(void)rm_dir(args, olddir);
	if (mk_dir(args, olddir) < 0)
		return EXIT_SUCCESS;
	stress_mk_filename(oldfile, sizeof(oldfile), olddir, "inotify_file");
	if (mk_file(args, oldfile, 4096) < 0)
		return EXIT_SUCCESS;

	stress_mk_filename(newfile, sizeof(newfile), path, "inotify_file");
	rc = inotify_exercise(args, newfile, path, "inotify_dir",
		inotify_moved_to_helper, IN_MOVED_TO, oldfile, bad_fd);
	(void)rm_file(args, newfile);
	(void)rm_dir(args, olddir);

	return rc;
}
#else
	UNEXPECTED
#endif

#if defined(IN_MOVED_FROM)
static int inotify_moved_from_helper(
	stress_args_t *args,
	const char *oldpath,
	void *private)
{
	const char *newpath = (const char *)private;

	if (rename(oldpath, newpath) < 0) {
		pr_err("%s: cannot rename %s to %s, errno=%d (%s)\n",
			args->name, oldpath, newpath, errno, strerror(errno));
		return -1;
	}
	return 0;
}

static int inotify_moved_from(
	stress_args_t *args,
	const char *path,
	const int bad_fd)
{
	char oldfile[PATH_MAX], newdir[PATH_MAX - 16], newfile[PATH_MAX];
	int rc;

	stress_mk_filename(oldfile, sizeof(oldfile), path, "inotify_file");
	if (mk_file(args, oldfile, 4096) < 0)
		return EXIT_SUCCESS;
	stress_mk_filename(newdir, sizeof(newdir), path, "new_dir");
	(void)rm_dir(args, newdir);
	if (mk_dir(args, newdir) < 0)
		return EXIT_SUCCESS;
	stress_mk_filename(newfile, sizeof(newfile), newdir, "inotify_file");
	rc = inotify_exercise(args, oldfile, path, "inotify_dir",
		inotify_moved_from_helper, IN_MOVED_FROM, newfile, bad_fd);
	(void)rm_file(args, newfile);
	(void)rm_file(args, oldfile);	/* In case rename failed */
	(void)rm_dir(args, newdir);

	return rc;
}
#else
	UNEXPECTED
#endif

#if defined(IN_CLOSE_WRITE)
static int inotify_close_write_helper(
	stress_args_t *args,
	const char *path,
	void *ptr)
{
	int *fdptr = (int *)ptr;

	(void)args;
	(void)path;

	(void)close(*fdptr);
	*fdptr = -1;
	return 0;
}

static int inotify_close_write_file(
	stress_args_t *args,
	const char *path,
	const int bad_fd)
{
	char filepath[PATH_MAX];
	int fd, rc;

	stress_mk_filename(filepath, sizeof(filepath), path, "inotify_file");
	if (mk_file(args, filepath, 4096) < 0)
		return EXIT_SUCCESS;

	if ((fd = open(filepath, O_RDWR)) < 0) {
		pr_err("%s: cannot re-open %s, errno=%d (%s)\n",
			args->name, filepath, errno, strerror(errno));
		return EXIT_FAILURE;
	}

	rc = inotify_exercise(args, filepath, path, "inotify_file",
		inotify_close_write_helper, IN_CLOSE_WRITE, (void*)&fd, bad_fd);
	(void)rm_file(args, filepath);
	if (fd != -1)
		(void)close(fd);

	return rc;
}
#else
	UNEXPECTED
#endif

#if defined(IN_CLOSE_NOWRITE)
static int inotify_close_nowrite_helper(
	stress_args_t *args,
	const char *path,
	void *ptr)
{
	int *fdptr = (int *)ptr;

	(void)args;
	(void)path;

	(void)close(*fdptr);
	*fdptr = -1;
	return 0;
}

static int inotify_close_nowrite_file(
	stress_args_t *args,
	const char *path,
	const int bad_fd)
{
	char filepath[PATH_MAX];
	int fd, rc;

	stress_mk_filename(filepath, sizeof(filepath), path, "inotify_file");
	if (mk_file(args, filepath, 4096) < 0)
		return EXIT_SUCCESS;

	if ((fd = open(filepath, O_RDONLY)) < 0) {
		pr_err("%s: cannot re-open %s, errno=%d (%s)\n",
			args->name, filepath, errno, strerror(errno));
		(void)rm_file(args, filepath);
		return EXIT_FAILURE;
	}

	rc = inotify_exercise(args, filepath, path, "inotify_file",
		inotify_close_nowrite_helper, IN_CLOSE_NOWRITE, (void*)&fd, bad_fd);
	(void)rm_file(args, filepath);
	if (fd != -1)
		(void)close(fd);
	return rc;
}
#else
	UNEXPECTED
#endif

static const stress_inotify_t inotify_stressors[] = {
#if defined(IN_ACCESS)
	{ inotify_access_file,		"IN_ACCESS" },
#endif
#if defined(IN_MODIFY)
	{ inotify_modify_file,		"IN_MODIFY" },
#endif
#if defined(IN_ATTRIB)
	{ inotify_attrib_file,		"IN_ATTRIB" },
#endif
#if defined(IN_CLOSE_WRITE)
	{ inotify_close_write_file,	"IN_CLOSE_WRITE" },
#endif
#if defined(IN_CLOSE_NOWRITE)
	{ inotify_close_nowrite_file,	"IN_CLOSE_NOWRITE" },
#endif
#if defined(IN_OPEN)
	{ inotify_open_file,		"IN_OPEN" },
#endif
#if defined(IN_MOVED_FROM)
	{ inotify_moved_from,		"IN_MOVED_FROM" },
#endif
#if defined(IN_MOVED_TO)
	{ inotify_moved_to,		"IN_MOVED_TO" },
#endif
#if defined(IN_CREATE)
	{ inotify_creat_file,		"IN_CREATE" },
#endif
#if defined(IN_DELETE)
	{ inotify_delete_file,		"IN_DELETE" },
#endif
#if defined(IN_DELETE_SELF)
	{ inotify_delete_self,		"IN_DELETE_SELF" },
#endif
#if defined(IN_MOVE_SELF)
	{ inotify_move_self,		"IN_MOVE_SELF" },
#endif
	{ NULL,				NULL }
};

/*
 *  stress_inotify()
 *	stress inotify
 */
static int stress_inotify(stress_args_t *args)
{
	char pathname[PATH_MAX - 16];
	int ret, i, rc = EXIT_SUCCESS;
	const int bad_fd = stress_get_bad_fd();

	stress_temp_dir_args(args, pathname, sizeof(pathname));
	ret = stress_temp_dir_mk_args(args);
	if (ret < 0)
		return stress_exit_status(-ret);

	stress_set_proc_state(args->name, STRESS_STATE_SYNC_WAIT);
	stress_sync_start_wait(args);
	stress_set_proc_state(args->name, STRESS_STATE_RUN);

	do {
		for (i = 0; LIKELY(stress_continue(args) && inotify_stressors[i].func); i++) {
			if (inotify_stressors[i].func(args, pathname, bad_fd) == EXIT_FAILURE) {
				rc = EXIT_FAILURE;
				break;
			}
		}
		stress_bogo_inc(args);
	} while (stress_continue(args));

	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);
	(void)stress_temp_dir_rm_args(args);

	return rc;
}

const stressor_info_t stress_inotify_info = {
	.stressor = stress_inotify,
	.classifier = CLASS_FILESYSTEM | CLASS_OS,
	.verify = VERIFY_OPTIONAL,
	.help = help
};
#else
const stressor_info_t stress_inotify_info = {
	.stressor = stress_unimplemented,
	.classifier = CLASS_FILESYSTEM | CLASS_OS,
	.verify = VERIFY_OPTIONAL,
	.help = help,
	.unimplemented_reason = "built without sys/epoll.h, sys/inotify.h, inotify(), inotify1() or select() support"
};
#endif
