/*
 * Copyright (C) 2012-2017 Canonical, Ltd.
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
#include "stress-ng.h"

#if defined(__linux__) && NEED_GLIBC(2,9,0)

#include <sys/select.h>
#include <sys/inotify.h>

#define DIR_FLAGS	(S_IRWXU | S_IRWXG)
#define FILE_FLAGS	(S_IRUSR | S_IWUSR)

#define TIME_OUT	(10)	/* Secs for inotify to report back */
#define BUF_SIZE	(4096)

typedef int (*inotify_helper)(const args_t *args, const char *path, const void *private);
typedef void (*inotify_func)(const args_t *args, const char *path);

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
	const args_t *args,	/* Stressor args */
	const char *filename,	/* Filename in test */
	const char *watchname,	/* File or directory to watch using inotify */
	const char *matchname,	/* Filename we expect inotify event to report */
	const inotify_helper func,	/* Helper func */
	const int flags,	/* IN_* flags to watch for */
	void *private)		/* Helper func private data */
{
	int fd, wd, check_flags = flags, n = 0;
	char buffer[1024];
retry:
	n++;
	if ((fd = inotify_init()) < 0) {
		if (!g_keep_stressing_flag)
			return;

		/* This is just so wrong... */
		if (n < 10000 && errno == EMFILE) {
			/*
			 * inotify cleanup may be still running from a previous
			 * iteration, in which case we've run out of resources
			 * temporarily, so sleep a short while and retry.
			 */
			(void)shim_usleep(10000);
			goto retry;
		}
		/* Nope, give up */
		pr_fail("%s: inotify_init failed: errno=%d (%s) after %" PRIu32 " calls\n",
			args->name, errno, strerror(errno), n);
		return;
	}

	if ((wd = inotify_add_watch(fd, watchname, flags)) < 0) {
		(void)close(fd);
		pr_fail_err("inotify_add_watch");
		return;
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
				pr_err("%s: select error: errno=%d (%s)\n",
					args->name, errno, strerror(errno));
			break;
		} else if (err == 0) {
			if (g_opt_flags & OPT_FLAGS_VERIFY)
				pr_fail("%s: timed waiting for event flags 0x%x\n", args->name, flags);
			break;
		}

redo:
		if (!g_keep_stressing_flag)
			break;
		/*
		 *  Exercise FIOREAD to get inotify code coverage up
		 */
		if (ioctl(fd, FIONREAD, &nbytes) < 0) {
			if (g_opt_flags & OPT_FLAGS_VERIFY) {
				pr_fail("%s: data is ready, but ioctl FIONREAD failed\n", args->name);
				break;
			}
		}
		if (nbytes <= 0) {
			pr_fail("%s: data is ready, but ioctl FIONREAD "
				"reported %d bytes available\n", args->name, nbytes);
			break;
		}

		len = read(fd, buffer, sizeof(buffer));

		if ((len <= 0) || (len > (ssize_t)sizeof(buffer))) {
			if ((errno == EAGAIN) || (errno == EINTR))
				goto redo;
			if (errno) {
				pr_fail_err("error reading inotify");
				break;
			}
		}

		/* Scan through inotify events */
		while ((i >= 0) && (i <= len - (ssize_t)sizeof(struct inotify_event))) {
			struct inotify_event *event =
				(struct inotify_event *)&buffer[i];
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
		}
	}

cleanup:
	(void)inotify_rm_watch(fd, wd);
	if (close(fd) < 0) {
		pr_err("%s: close error: errno=%d (%s)\n",
			args->name, errno, strerror(errno));
	}
}

/*
 *  rm_file()
 *	remove a file
 */
static int rm_file(const args_t *args, const char *path)
{
	if ((unlink(path) < 0) && errno != ENOENT) {
		pr_err("%s: cannot remove file %s: errno=%d (%s)\n",
			args->name, path, errno, strerror(errno));
		return -1;
	}
	return 0;
}

/*
 *  rm_dir()
 *	clean files in directory and directory
 */
static int rm_dir(const args_t *args, const char *path)
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

			(void)snprintf(filename, sizeof(filename), "%s/%s",
				path, d->d_name);
			(void)rm_file(args, filename);
		}
		(void)closedir(dp);
	}
	ret = rmdir(path);
	if (ret < 0 && errno != ENOENT)
		pr_err("%s: cannot remove directory %s: errno=%d (%s)\n",
			args->name, path, errno, strerror(errno));
	return ret;
}

/*
 *  mk_dir()
 *	make a directory
 */
static int mk_dir(const args_t *args, const char *path)
{
	if (mkdir(path, DIR_FLAGS) < 0) {
		pr_err("%s: cannot mkdir %s: errno=%d (%s)\n",
			args->name, path, errno, strerror(errno));
		return -1;
	}
	return 0;
}

/*
 *  mk_filename()
 *	simple helper to create a filename
 */
static inline void mk_filename(
	char *filename,
	const size_t len,
	const char *path,
	const char *name)
{
	(void)snprintf(filename, len, "%s/%s", path, name);
}

/*
 *  mk_file()
 *	create file of length len bytes
 */
static int mk_file(const args_t *args, const char *filename, const size_t len)
{
	int fd;
	size_t sz = len;

	char buffer[BUF_SIZE];

	(void)rm_file(args, filename);
	if ((fd = open(filename, O_CREAT | O_RDWR, FILE_FLAGS)) < 0) {
		pr_err("%s: cannot create file %s: errno=%d (%s)\n",
			args->name, filename, errno, strerror(errno));
		return -1;
	}

	memset(buffer, 'x', BUF_SIZE);
	while (sz > 0) {
		size_t n = (sz > BUF_SIZE) ? BUF_SIZE : sz;
		int ret;

		if ((ret = write(fd, buffer, n)) < 0) {
			pr_err("%s: error writing to file %s: errno=%d (%s)\n",
				args->name, filename, errno, strerror(errno));
			(void)close(fd);
			return -1;
		}
		sz -= ret;
	}

	if (close(fd) < 0) {
		pr_err("%s: cannot close file %s: errno=%d (%s)\n",
			args->name, filename, errno, strerror(errno));
		return -1;
	}
	return 0;
}


static int inotify_attrib_helper(
	const args_t *args,
	const char *path,
	const void *dummy)
{
	(void)dummy;
	if (chmod(path, S_IRUSR | S_IWUSR) < 0) {
		pr_err("%s: cannot chmod file %s: errno=%d (%s)\n",
			args->name, path, errno, strerror(errno));
		return -1;
	}
	return 0;
}

static void inotify_attrib_file(const args_t *args, const char *path)
{
	char filepath[PATH_MAX];

	mk_filename(filepath, PATH_MAX, path, "inotify_file");
	if (mk_file(args, filepath, 4096) < 0)
		return;

	inotify_exercise(args, filepath, path, "inotify_file",
		inotify_attrib_helper, IN_ATTRIB, NULL);
	(void)rm_file(args, filepath);
}

static int inotify_access_helper(
	const args_t *args,
	const char *path,
	const void *dummy)
{
	int fd;
	char buffer[1];
	int rc = 0;

	(void)dummy;
	if ((fd = open(path, O_RDONLY)) < 0) {
		pr_err("%s: cannot open file %s: errno=%d (%s)\n",
			args->name, path, errno, strerror(errno));
		return -1;
	}

	/* Just want to force an access */
do_access:
	if (g_keep_stressing_flag && (read(fd, buffer, 1) < 0)) {
		if ((errno == EAGAIN) || (errno == EINTR))
			goto do_access;
		pr_err("%s: cannot read file %s: errno=%d (%s)\n",
			args->name, path, errno, strerror(errno));
		rc = -1;
	}
	(void)close(fd);
	return rc;
}

static void inotify_access_file(const args_t *args, const char *path)
{
	char filepath[PATH_MAX];

	mk_filename(filepath, PATH_MAX, path, "inotify_file");
	if (mk_file(args, filepath, 4096) < 0)
		return;

	inotify_exercise(args, filepath, path, "inotify_file",
		inotify_access_helper, IN_ACCESS, NULL);
	(void)rm_file(args, filepath);
}

static int inotify_modify_helper(
	const args_t *args,
	const char *path,
	const void *dummy)
{
	int fd, rc = 0;
	char buffer[1] = { 0 };

	(void)dummy;
	if (mk_file(args, path, 4096) < 0)
		return -1;
	if ((fd = open(path, O_RDWR)) < 0) {
		pr_err("%s: cannot open file %s: errno=%d (%s)\n",
			args->name, path, errno, strerror(errno));
		rc = -1;
		goto remove;
	}
do_modify:
	if (g_keep_stressing_flag && (write(fd, buffer, 1) < 0)) {
		if ((errno == EAGAIN) || (errno == EINTR))
			goto do_modify;
		pr_err("%s: cannot write to file %s: errno=%d (%s)\n",
			args->name, path, errno, strerror(errno));
		rc = -1;
	}
	(void)close(fd);
remove:
	(void)rm_file(args, path);
	return rc;
}

static void inotify_modify_file(const args_t *args, const char *path)
{
	char filepath[PATH_MAX];

	mk_filename(filepath, PATH_MAX, path, "inotify_file");
	inotify_exercise(args, filepath, path, "inotify_file",
		inotify_modify_helper, IN_MODIFY, NULL);
}

static int inotify_creat_helper(
	const args_t *args,
	const char *path,
	const void *dummy)
{
	(void)dummy;
	int fd;
	if ((fd = creat(path, FILE_FLAGS)) < 0) {
		pr_err("%s: cannot create file %s: errno=%d (%s)\n",
			args->name, path, errno, strerror(errno));
		return -1;
	}
	(void)close(fd);
	return 0;
}

static void inotify_creat_file(const args_t *args, const char *path)
{
	char filepath[PATH_MAX];

	mk_filename(filepath, PATH_MAX, path, "inotify_file");
	inotify_exercise(args, filepath, path, "inotify_file",
		inotify_creat_helper, IN_CREATE, NULL);
	(void)rm_file(args, filepath);
}

static int inotify_open_helper(
	const args_t *args,
	const char *path,
	const void *dummy)
{
	int fd;

	(void)dummy;
	if ((fd = open(path, O_RDONLY)) < 0) {
		pr_err("%s: cannot open file %s: errno=%d (%s)\n",
			args->name, path, errno, strerror(errno));
		return -1;
	}
	(void)close(fd);
	return 0;
}

static void inotify_open_file(const args_t *args, const char *path)
{
	char filepath[PATH_MAX];

	mk_filename(filepath, PATH_MAX, path, "inotify_file");
	if (mk_file(args, filepath, 4096) < 0)
		return;
	inotify_exercise(args, filepath, path, "inotify_file",
		inotify_open_helper, IN_OPEN, NULL);
	(void)rm_file(args, filepath);
}

static int inotify_delete_helper(
	const args_t *args,
	const char *path,
	const void *dummy)
{
	(void)dummy;

	return rm_file(args, path);
}

static void inotify_delete_file(const args_t *args, const char *path)
{
	char filepath[PATH_MAX];

	mk_filename(filepath, PATH_MAX, path, "inotify_file");
	if (mk_file(args, filepath, 4096) < 0)
		return;
	inotify_exercise(args, filepath, path, "inotify_file",
		inotify_delete_helper, IN_DELETE, NULL);
	/* We remove (again) it just in case the test failed */
	(void)rm_file(args, filepath);
}

static int inotify_delete_self_helper(
	const args_t *args,
	const char *path,
	const void *dummy)
{
	(void)dummy;

	return rm_dir(args, path);
}

static void inotify_delete_self(const args_t *args, const char *path)
{
	char filepath[PATH_MAX];

	mk_filename(filepath, PATH_MAX, path, "inotify_dir");
	if (mk_dir(args, filepath) < 0)
		return;
	inotify_exercise(args, filepath, filepath, "inotify_dir",
		inotify_delete_self_helper, IN_DELETE_SELF, NULL);
	/* We remove (again) in case the test failed */
	(void)rm_dir(args, filepath);
}

static int inotify_move_self_helper(
	const args_t *args,
	const char *oldpath,
	const void *private)
{
	const char *newpath = (const char *)private;

	if (rename(oldpath, newpath) < 0) {
		pr_err("%s: cannot rename %s to %s: errno=%d (%s)\n",
			args->name, oldpath, newpath, errno, strerror(errno));
		return -1;
	}
	return 0;
}

static void inotify_move_self(const args_t *args, const char *path)
{
	char filepath[PATH_MAX], newpath[PATH_MAX];

	mk_filename(filepath, PATH_MAX, path, "inotify_dir");
	if (mk_dir(args, filepath) < 0)
		return;
	mk_filename(newpath, PATH_MAX, path, "renamed_dir");

	inotify_exercise(args, filepath, filepath, "inotify_dir",
		inotify_move_self_helper, IN_MOVE_SELF, newpath);
	(void)rm_dir(args, newpath);
	(void)rm_dir(args, filepath);	/* In case rename failed */
}

static int inotify_moved_to_helper(
	const args_t *args,
	const char *newpath,
	const void *private)
{
	const char *oldpath = (const char *)private;

	if (rename(oldpath, newpath) < 0) {
		pr_err("%s: cannot rename %s to %s: errno=%d (%s)\n",
			args->name, oldpath, newpath, errno, strerror(errno));
		return -1;
	}
	return 0;
}

static void inotify_moved_to(const args_t *args, const char *path)
{
	char olddir[PATH_MAX], oldfile[PATH_MAX], newfile[PATH_MAX];

	mk_filename(olddir, PATH_MAX, path, "new_dir");
	(void)rm_dir(args, olddir);
	if (mk_dir(args, olddir) < 0)
		return;
	mk_filename(oldfile, PATH_MAX, olddir, "inotify_file");
	if (mk_file(args, oldfile, 4096) < 0)
		return;

	mk_filename(newfile, PATH_MAX, path, "inotify_file");
	inotify_exercise(args, newfile, path, "inotify_dir",
		inotify_moved_to_helper, IN_MOVED_TO, oldfile);
	(void)rm_file(args, newfile);
	(void)rm_dir(args, olddir);
}

static int inotify_moved_from_helper(
	const args_t *args,
	const char *oldpath,
	const void *private)
{
	const char *newpath = (const char *)private;

	if (rename(oldpath, newpath) < 0) {
		pr_err("%s: cannot rename %s to %s: errno=%d (%s)\n",
			args->name, oldpath, newpath, errno, strerror(errno));
		return -1;
	}
	return 0;
}

static void inotify_moved_from(const args_t *args, const char *path)
{
	char oldfile[PATH_MAX], newdir[PATH_MAX], newfile[PATH_MAX];

	mk_filename(oldfile, PATH_MAX, path, "inotify_file");
	if (mk_file(args, oldfile, 4096) < 0)
		return;
	mk_filename(newdir, PATH_MAX, path, "new_dir");
	(void)rm_dir(args, newdir);
	if (mk_dir(args, newdir) < 0)
		return;
	mk_filename(newfile, PATH_MAX, newdir, "inotify_file");
	inotify_exercise(args, oldfile, path, "inotify_dir",
		inotify_moved_from_helper, IN_MOVED_FROM, newfile);
	(void)rm_file(args, newfile);
	(void)rm_file(args, oldfile);	/* In case rename failed */
	(void)rm_dir(args, newdir);
}

static int inotify_close_write_helper(
	const args_t *args,
	const char *path,
	const void *fdptr)
{
	(void)args;
	(void)path;

	(void)close(*(const int *)fdptr);
	return 0;
}

static void inotify_close_write_file(const args_t *args, const char *path)
{
	char filepath[PATH_MAX];
	int fd;

	mk_filename(filepath, PATH_MAX, path, "inotify_file");
	if (mk_file(args, filepath, 4096) < 0)
		return;

	if ((fd = open(filepath, O_RDWR)) < 0) {
		pr_err("%s: cannot re-open %s: errno=%d (%s)\n",
			args->name, filepath, errno, strerror(errno));
		return;
	}

	inotify_exercise(args, filepath, path, "inotify_file",
		inotify_close_write_helper, IN_CLOSE_WRITE, (void*)&fd);
	(void)rm_file(args, filepath);
	(void)close(fd);
}

static int inotify_close_nowrite_helper(
	const args_t *args,
	const char *path,
	const void *fdptr)
{
	(void)args;
	(void)path;

	(void)close(*(const int *)fdptr);
	return 0;
}

static void inotify_close_nowrite_file(const args_t *args, const char *path)
{
	char filepath[PATH_MAX];
	int fd;

	mk_filename(filepath, PATH_MAX, path, "inotify_file");
	if (mk_file(args, filepath, 4096) < 0)
		return;

	if ((fd = open(filepath, O_RDONLY)) < 0) {
		pr_err("%s: cannot re-open %s: errno=%d (%s)\n",
			args->name, filepath, errno, strerror(errno));
		(void)rm_file(args, filepath);
		return;
	}

	inotify_exercise(args, filepath, path, "inotify_file",
		inotify_close_nowrite_helper, IN_CLOSE_NOWRITE, (void*)&fd);
	(void)rm_file(args, filepath);
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
int stress_inotify(const args_t *args)
{
	char dirname[PATH_MAX];
	int ret, i;

	stress_temp_dir_args(args, dirname, sizeof(dirname));
	ret = stress_temp_dir_mk_args(args);
	if (ret < 0)
		return exit_status(-ret);
	do {
		for (i = 0; g_keep_stressing_flag && inotify_stressors[i].func; i++)
			inotify_stressors[i].func(args, dirname);
		inc_counter(args);
	} while (keep_stressing());
	(void)stress_temp_dir_rm_args(args);

	return EXIT_SUCCESS;
}
#else
int stress_inotify(const args_t *args)
{
	return stress_not_implemented(args);
}
#endif
