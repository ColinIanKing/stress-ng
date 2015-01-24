/*
 * Copyright (C) 2012-2015 Canonical, Ltd.
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
 * stress-inotify.c is derived from the eCryptfs inotify tests
 * that I authored in 2012.
 */
#define _GNU_SOURCE

#if defined(__linux__)

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdbool.h>
#include <inttypes.h>
#include <string.h>
#include <unistd.h>
#include <limits.h>
#include <errno.h>
#include <fcntl.h>
#include <dirent.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/select.h>
#include <sys/inotify.h>

#include "stress-ng.h"

#define DIR_FLAGS	(S_IRWXU | S_IRWXG)
#define FILE_FLAGS	(S_IRUSR | S_IWUSR)

#define TIME_OUT	(10)	/* Secs for inotify to report back */
#define BUF_SIZE	(4096)

typedef int (*inotify_helper)(const char *path, const void *private);
typedef void (*inotify_func)(const char *path);

typedef struct {
	const inotify_func func;
	const char*	description;
} inotify_stress_t;

/*
 *  inotify_exercise()
 *	run a given test helper function 'func' and see if this triggers the
 *	required inotify event flags 'flags'.
 */
static void inotify_exercise(
	const char *filename,	/* Filename in test */
	const char *watchname,	/* File or directory to watch using inotify */
	const char *matchname,	/* Filename we expect inotify event to report */
	const inotify_helper func,	/* Helper func */
	const int flags,	/* IN_* flags to watch for */
	void *private)		/* Helper func private data */
{
	int fd, wd, check_flags = flags;
	char buffer[1024];
	static uint32_t n = 0;

retry:
	n++;
	if ((fd = inotify_init()) < 0) {
		/* This is just so wrong... */
		if (n < 1000 && errno == EMFILE) {
			/*
			 * inotify cleanup may be still running from a previous
			 * iteration, in which case we've run out of resources
			 * temporarily, so sleep a short while and retry.
			 */
			usleep(10000);
			goto retry;
		}
		/* Nope, give up */
		pr_fail(stderr, "inotify_init failed: errno=%d (%s) after %" PRIu32 " calls\n",
			errno, strerror(errno), n);
		return;
	}

	if ((wd = inotify_add_watch(fd, watchname, flags)) < 0) {
		(void)close(fd);
		pr_fail(stderr, "inotify_add_watch failed: errno=%d (%s)",
			errno, strerror(errno));
		return;
	}

	if (func(filename, private) < 0)
		goto cleanup;

	while (check_flags) {
		ssize_t len, i = 0;
		struct timeval tv;
		fd_set rfds;
		int err;

		/* We give inotify TIME_OUT seconds to report back */
		tv.tv_sec = TIME_OUT;
		tv.tv_usec = 0;

		FD_ZERO(&rfds);
		FD_SET(fd, &rfds);

		/* Wait for an inotify event ... */
		err = select(fd + 1, &rfds, NULL, NULL, &tv);
		if (err == -1) {
			if (errno != EINTR)
				pr_err(stderr, "select error: errno=%d (%s)\n",
					errno, strerror(errno));
			break;
		} else if (err == 0) {
			if (opt_flags & OPT_FLAGS_VERIFY)
				pr_fail(stderr, "timed waiting for event flags 0x%x\n", flags);
			break;
		}

		len = read(fd, buffer, sizeof(buffer));

		if ((len < 0) || (len > (ssize_t)sizeof(buffer))) {
			pr_fail(stderr, "error reading inotify: errno=%d (%s)\n",
				errno, strerror(errno));
			break;
		}

		/* Scan through inotify events */
		do {
			struct inotify_event *event = (struct inotify_event *)&buffer[i];
			int f = event->mask & (IN_DELETE_SELF | IN_MOVE_SELF |
					       IN_MOVED_TO | IN_MOVED_FROM |
					       IN_ATTRIB);
			if (event->len &&
			    strcmp(event->name, matchname) == 0 &&
			    flags & event->mask)
				check_flags &= ~(flags & event->mask);
			else if (flags & f)
				check_flags &= ~(flags & event->mask);

			i += sizeof(struct inotify_event) + event->len;
		} while (i < len);
	}

cleanup:
	(void)inotify_rm_watch(fd, wd);
	if (close(fd) < 0) {
		pr_err(stderr, "close error: errno=%d (%s)\n",
			errno, strerror(errno));
	}
}

/*
 *  rm_file()
 *	remove a file
 */
static int rm_file(const char *path)
{
	if ((unlink(path) < 0) && errno != ENOENT) {
		pr_err(stderr, "cannot remove file %s: errno=%d (%s)\n",
			path, errno, strerror(errno));
		return -1;
	}
	return 0;
}

/*
 *  rm_dir()
 *	clean files in directory and directory
 */
static int rm_dir(const char *path)
{
	DIR *dp;
	int ret;

	dp = opendir(path);
	if (dp != NULL) {
		struct dirent *d;

		while ((d = readdir(dp)) != NULL) {
			char filename[PATH_MAX];

			if (!strcmp(d->d_name, ".") ||
			    !strcmp(d->d_name, ".."))
				continue;

			snprintf(filename, sizeof(filename), "%s/%s", path, d->d_name);
			(void)rm_file(filename);
		}
		(void)closedir(dp);
	}
	ret = rmdir(path);
	if (ret < 0 && errno != ENOENT)
		pr_err(stderr, "cannot remove directory %s: errno=%d (%s)\n",
			path, errno, strerror(errno));
	return ret;
}

/*
 *  mk_dir()
 *	make a directory
 */
static int mk_dir(const char *path)
{
	if (mkdir(path, DIR_FLAGS) < 0) {
		pr_err(stderr, "cannot mkdir %s: errno=%d (%s)\n",
			path, errno, strerror(errno));
		return -1;
	}
	return 0;
}

/*
 *  mk_filename()
 *	simple helper to create a filename
 */
static void mk_filename(
	char *filename,
	const size_t len,
	const char *path,
	const char *name)
{
	snprintf(filename, len, "%s/%s", path, name);
}

/*
 *  mk_file()
 *	create file of length len bytes
 */
static int mk_file(const char *filename, const size_t len)
{
	int fd;
	size_t sz = len;

	char buffer[BUF_SIZE];

	(void)rm_file(filename);
	if ((fd = open(filename, O_CREAT | O_RDWR, FILE_FLAGS)) < 0) {
		pr_err(stderr, "cannot create file %s: errno=%d (%s)\n",
			filename, errno, strerror(errno));
		return -1;
	}

	memset(buffer, 'x', BUF_SIZE);
	while (sz > 0) {
		size_t n = (sz > BUF_SIZE) ? BUF_SIZE : sz;
		int ret;

		if ((ret = write(fd, buffer, n)) < 0) {
			pr_err(stderr, "error writing to file %s: errno=%d (%s)\n",
				filename, errno, strerror(errno));
			(void)close(fd);
			return -1;
		}
		sz -= ret;
	}

	if (close(fd) < 0) {
		pr_err(stderr, "cannot close file %s: errno=%d (%s)\n",
			filename, errno, strerror(errno));
		return -1;
	}
	return 0;
}


static int inotify_attrib_helper(const char *path, const void *dummy)
{
	(void)dummy;
	if (chmod(path, S_IRUSR | S_IWUSR) < 0) {
		pr_err(stderr, "cannot chmod file %s: errno=%d (%s)\n",
			path, errno, strerror(errno));
		return -1;
	}
	return 0;
}

void inotify_attrib_file(const char *path)
{
	char filepath[PATH_MAX];

	mk_filename(filepath, PATH_MAX, path, "inotify_file");
	if (mk_file(filepath, 4096) < 0)
		return;

	inotify_exercise(filepath, path, "inotify_file", inotify_attrib_helper, IN_ATTRIB, NULL);
	(void)rm_file(filepath);
}

static int inotify_access_helper(const char *path, const void *dummy)
{
	int fd;
	char buffer[1];
	int rc = 0;

	(void)dummy;
	if ((fd = open(path, O_RDONLY)) < 0) {
		pr_err(stderr, "cannot open file %s: errno=%d (%s)\n",
			path, errno, strerror(errno));
		return -1;
	}

	/* Just want to force an access */
	if (read(fd, buffer, 1) < 0) {
		pr_err(stderr, "cannot read file %s: errno=%d (%s)\n",
			path, errno, strerror(errno));
		rc = -1;
	}
	(void)close(fd);
	return rc;
}

static void inotify_access_file(const char *path)
{
	char filepath[PATH_MAX];

	mk_filename(filepath, PATH_MAX, path, "inotify_file");
	if (mk_file(filepath, 4096) < 0)
		return;

	inotify_exercise(filepath, path, "inotify_file", inotify_access_helper, IN_ACCESS, NULL);
	(void)rm_file(filepath);
}

static int inotify_modify_helper(const char *path, const void *dummy)
{
	int fd, rc = 0;
	char buffer[1] = { 0 };

	(void)dummy;
	if (mk_file(path, 4096) < 0)
		return -1;
	if ((fd = open(path, O_RDWR)) < 0) {
		pr_err(stderr, "cannot open file %s: errno=%d (%s)\n",
			path, errno, strerror(errno));
		rc = -1;
		goto remove;
	}
	if (write(fd, buffer, 1) < 0) {
		pr_err(stderr, "cannot write to file %s: errno=%d (%s)\n",
			path, errno, strerror(errno));
		rc = -1;
	}
	(void)close(fd);
remove:
	(void)rm_file(path);
	return rc;
}

static void inotify_modify_file(const char *path)
{
	char filepath[PATH_MAX];

	mk_filename(filepath, PATH_MAX, path, "inotify_file");
	inotify_exercise(filepath, path, "inotify_file", inotify_modify_helper, IN_MODIFY, NULL);
}

static int inotify_creat_helper(const char *path, const void *dummy)
{
	(void)dummy;
	int fd;
	if ((fd = creat(path, FILE_FLAGS)) < 0) {
		pr_err(stderr, "cannot create file %s: errno=%d (%s)\n",
			path, errno, strerror(errno));
		return -1;
	}
	(void)close(fd);
	return 0;
}

static void inotify_creat_file(const char *path)
{
	char filepath[PATH_MAX];

	mk_filename(filepath, PATH_MAX, path, "inotify_file");
	inotify_exercise(filepath, path, "inotify_file", inotify_creat_helper, IN_CREATE, NULL);
	(void)rm_file(filepath);
}

static int inotify_open_helper(const char *path, const void *dummy)
{
	int fd;

	(void)dummy;
	if ((fd = open(path, O_RDONLY)) < 0) {
		pr_err(stderr, "cannot open file %s: errno=%d (%s)\n",
			path, errno, strerror(errno));
		return -1;
	}
	(void)close(fd);
	return 0;
}

static void inotify_open_file(const char *path)
{
	char filepath[PATH_MAX];

	mk_filename(filepath, PATH_MAX, path, "inotify_file");
	if (mk_file(filepath, 4096) < 0)
		return;
	inotify_exercise(filepath, path, "inotify_file", inotify_open_helper, IN_OPEN, NULL);
	(void)rm_file(filepath);
}

static int inotify_delete_helper(const char *path, const void *dummy)
{
	(void)dummy;

	return rm_file(path);
}

static void inotify_delete_file(const char *path)
{
	char filepath[PATH_MAX];

	mk_filename(filepath, PATH_MAX, path, "inotify_file");
	if (mk_file(filepath, 4096) < 0)
		return;
	inotify_exercise(filepath, path, "inotify_file", inotify_delete_helper, IN_DELETE, NULL);
	/* We remove (again) it just in case the test failed */
	(void)rm_file(filepath);
}

static int inotify_delete_self_helper(const char *path, const void *dummy)
{
	(void)dummy;

	return rm_dir(path);
}

static void inotify_delete_self(const char *path)
{
	char filepath[PATH_MAX];

	mk_filename(filepath, PATH_MAX, path, "inotify_dir");
	if (mk_dir(filepath) < 0)
		return;
	inotify_exercise(filepath, filepath, "inotify_dir", inotify_delete_self_helper, IN_DELETE_SELF, NULL);
	/* We remove (again) in case the test failed */
	(void)rm_dir(filepath);
}

static int inotify_move_self_helper(const char *oldpath, const void *private)
{
	char *newpath = (char*)private;

	if (rename(oldpath, newpath) < 0) {
		pr_err(stderr, "cannot rename %s to %s: errno=%d (%s)\n",
			oldpath, newpath, errno, strerror(errno));
		return -1;
	}
	return 0;
}

static void inotify_move_self(const char *path)
{
	char filepath[PATH_MAX], newpath[PATH_MAX];

	mk_filename(filepath, PATH_MAX, path, "inotify_dir");
	if (mk_dir(filepath) < 0)
		return;
	mk_filename(newpath, PATH_MAX, path, "renamed_dir");

	inotify_exercise(filepath, filepath, "inotify_dir", inotify_move_self_helper, IN_MOVE_SELF, newpath);
	(void)rm_dir(newpath);
	(void)rm_dir(filepath);	/* In case rename failed */
}

static int inotify_moved_to_helper(const char *newpath, const void *private)
{
	char *oldpath = (char*)private;

	if (rename(oldpath, newpath) < 0) {
		pr_err(stderr, "cannot rename %s to %s: errno=%d (%s)\n",
			oldpath, newpath, errno, strerror(errno));
		return -1;
	}
	return 0;
}

static void inotify_moved_to(const char *path)
{
	char olddir[PATH_MAX], oldfile[PATH_MAX], newfile[PATH_MAX];

	mk_filename(olddir, PATH_MAX, path, "new_dir");
	(void)rm_dir(olddir);
	if (mk_dir(olddir) < 0)
		return;
	mk_filename(oldfile, PATH_MAX, olddir, "inotify_file");
	if (mk_file(oldfile, 4096) < 0)
		return;

	mk_filename(newfile, PATH_MAX, path, "inotify_file");
	inotify_exercise(newfile, path, "inotify_dir", inotify_moved_to_helper, IN_MOVED_TO, oldfile);
	(void)rm_file(newfile);
	(void)rm_dir(olddir);
}

static int inotify_moved_from_helper(const char *oldpath, const void *private)
{
	char *newpath = (char*)private;

	if (rename(oldpath, newpath) < 0) {
		pr_err(stderr, "cannot rename %s to %s: errno=%d (%s)\n",
			oldpath, newpath, errno, strerror(errno));
		return -1;
	}
	return 0;
}

static void inotify_moved_from(const char *path)
{
	char oldfile[PATH_MAX], newdir[PATH_MAX], newfile[PATH_MAX];

	mk_filename(oldfile, PATH_MAX, path, "inotify_file");
	if (mk_file(oldfile, 4096) < 0)
		return;
	mk_filename(newdir, PATH_MAX, path, "new_dir");
	(void)rm_dir(newdir);
	if (mk_dir(newdir) < 0)
		return;
	mk_filename(newfile, PATH_MAX, newdir, "inotify_file");
	inotify_exercise(oldfile, path, "inotify_dir", inotify_moved_from_helper, IN_MOVED_FROM, newfile);
	(void)rm_file(newfile);
	(void)rm_file(oldfile);	/* In case rename failed */
	(void)rm_dir(newdir);
}

static int inotify_close_write_helper(const char *path, const void *fdptr)
{
	(void)path;
	(void)close(*(int *)fdptr);
	return 0;
}

static void inotify_close_write_file(const char *path)
{
	char filepath[PATH_MAX];
	int fd;

	mk_filename(filepath, PATH_MAX, path, "inotify_file");
	if (mk_file(filepath, 4096) < 0)
		return;

	if ((fd = open(filepath, O_RDWR)) < 0) {
		pr_err(stderr, "cannot re-open %s: errno=%d (%s)\n",
			filepath, errno, strerror(errno));
		return;
	}

	inotify_exercise(filepath, path, "inotify_file", inotify_close_write_helper, IN_CLOSE_WRITE, (void*)&fd);
	(void)rm_file(filepath);
	(void)close(fd);
}

static int inotify_close_nowrite_helper(const char *path, const void *fdptr)
{
	(void)path;
	(void)close(*(int *)fdptr);
	return 0;
}

static void inotify_close_nowrite_file(const char *path)
{
	char filepath[PATH_MAX];
	int fd;

	mk_filename(filepath, PATH_MAX, path, "inotify_file");
	if (mk_file(filepath, 4096) < 0)
		return;

	if ((fd = open(filepath, O_RDONLY)) < 0) {
		pr_err(stderr, "cannot re-open %s: errno=%d (%s)\n",
			filepath, errno, strerror(errno));
		(void)rm_file(filepath);
		return;
	}

	inotify_exercise(filepath, path, "inotify_file", inotify_close_nowrite_helper, IN_CLOSE_NOWRITE, (void*)&fd);
	(void)rm_file(filepath);
	(void)close(fd);
}

static const inotify_stress_t inotify_stressors[] = {
	{ inotify_access_file,		"IN_ACCESS" },
	{ inotify_modify_file,		"IN_MODIFY" },
	{ inotify_attrib_file,		"IN_ATTRIB" },
	{ inotify_close_write_file,	"IN_CLOSE_WRITE" },
	{ inotify_close_nowrite_file,	"IN_CLOSE_NOWRITE" },
	{ inotify_open_file,		"IN_OPEN" },
	{ inotify_moved_from,		"IN_MOVED_FROM" },
	{ inotify_moved_to,		"IN_MOVED_TO" },
	{ inotify_creat_file,		"IN_CREATE" },
	{ inotify_delete_file,		"IN_DELETE" },
	{ inotify_delete_self,		"IN_DELETE_SELF" },
	{ inotify_move_self,		"IN_MOVE_SELF" },
	{ NULL,				NULL }
};

/*
 *  stress_inotify()
 *	stress inotify
 */
int stress_inotify(
	uint64_t *const counter,
	const uint32_t instance,
	const uint64_t max_ops,
	const char *name)
{
	char dirname[PATH_MAX];
	int i;
	const pid_t pid = getpid();

	(void)counter;
	(void)max_ops;

	stress_temp_dir(dirname, sizeof(dirname), name, pid, instance);
	if (stress_temp_dir_mk(name, pid, instance) < 0)
		return EXIT_FAILURE;
	do {
		for (i = 0; opt_do_run && inotify_stressors[i].func; i++)
			inotify_stressors[i].func(dirname);
		(*counter)++;
	} while (opt_do_run && (!max_ops || *counter < max_ops));
	(void)stress_temp_dir_rm(name, pid, instance);

	return EXIT_SUCCESS;
}

#endif
