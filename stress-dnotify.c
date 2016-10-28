/*
 * Copyright (C) 2012-2016 Canonical, Ltd.
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
 */
#define _GNU_SOURCE

#include "stress-ng.h"

#if defined(STRESS_INOTIFY)

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

#define DIR_FLAGS	(S_IRWXU | S_IRWXG)
#define FILE_FLAGS	(S_IRUSR | S_IWUSR)

#define BUF_SIZE	(4096)

static volatile int dnotify_fd;

static void dnotify_handler(int sig, siginfo_t *si, void *data)
{
	(void)sig;
	(void)data;

	dnotify_fd = si->si_fd;
}

typedef int (*dnotify_helper)(const char *name, const char *path, const void *private);
typedef void (*dnotify_func)(const char *name, const char *path);

typedef struct {
	const dnotify_func func;
	const char*	description;
} dnotify_stress_t;

/*
 *  dnotify_exercise()
 *	run a given test helper function 'func' and see if this triggers the
 *	required dnotify event flags 'flags'.
 */
static void dnotify_exercise(
	const char *name,	/* Stressor name */
	const char *filename,	/* Filename in test */
	const char *watchname,	/* File or directory to watch using dnotify */
	const dnotify_helper func,	/* Helper func */
	const int flags,	/* DN_* flags to watch for */
	void *private)		/* Helper func private data */
{
	int fd, i = 0;

	if ((fd = open(watchname, O_RDONLY)) < 0) {
		pr_fail(stderr, "%s: open failed: errno=%d (%s)\n",
			name, errno, strerror(errno));
		return;
	}
	if (fcntl(fd, F_SETSIG, SIGRTMIN + 1) < 0) {
		pr_fail(stderr, "%s: fcntl F_SETSIG failed: errno=%d (%s)\n",
			name, errno, strerror(errno));
		goto cleanup;
	}
	if (fcntl(fd, F_NOTIFY, flags) < 0) {
		pr_fail(stderr, "%s: fcntl F_NOTIFY failed: errno=%d (%s)\n",
			name, errno, strerror(errno));
		goto cleanup;
	}

	dnotify_fd = -1;
	if (func(name, filename, private) < 0)
		goto cleanup;

	/* Wait for up to 1 second for event */
	while ((i < 1000) && (dnotify_fd == -1)) {
		i++;
		usleep(1000);
	}

	if (dnotify_fd != fd) {
		pr_fail(stderr, "%s: did not get expected dnotify "
			"file descriptor\n", name);
	}

cleanup:
	(void)close(fd);

}

/*
 *  rm_file()
 *	remove a file
 */
static int rm_file(const char *name, const char *path)
{
	if ((unlink(path) < 0) && errno != ENOENT) {
		pr_err(stderr, "%s: cannot remove file %s: errno=%d (%s)\n",
			name, path, errno, strerror(errno));
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
	snprintf(filename, len, "%s/%s", path, name);
}

/*
 *  mk_file()
 *	create file of length len bytes
 */
static int mk_file(const char *name, const char *filename, const size_t len)
{
	int fd;
	size_t sz = len;

	char buffer[BUF_SIZE];

	if ((fd = open(filename, O_CREAT | O_RDWR, FILE_FLAGS)) < 0) {
		pr_err(stderr, "%s: cannot create file %s: errno=%d (%s)\n",
			name, filename, errno, strerror(errno));
		return -1;
	}

	memset(buffer, 'x', BUF_SIZE);
	while (sz > 0) {
		size_t n = (sz > BUF_SIZE) ? BUF_SIZE : sz;
		int ret;

		if ((ret = write(fd, buffer, n)) < 0) {
			pr_err(stderr, "%s: error writing to file %s: errno=%d (%s)\n",
				name, filename, errno, strerror(errno));
			(void)close(fd);
			return -1;
		}
		sz -= ret;
	}

	if (close(fd) < 0) {
		pr_err(stderr, "%s: cannot close file %s: errno=%d (%s)\n",
			name, filename, errno, strerror(errno));
		return -1;
	}
	return 0;
}

static int dnotify_attrib_helper(
	const char *name,
	const char *path,
	const void *dummy)
{
	(void)dummy;
	if (chmod(path, S_IRUSR | S_IWUSR) < 0) {
		pr_err(stderr, "%s: cannot chmod file %s: errno=%d (%s)\n",
			name, path, errno, strerror(errno));
		return -1;
	}
	return 0;
}

void dnotify_attrib_file(const char *name, const char *path)
{
	char filepath[PATH_MAX];

	mk_filename(filepath, PATH_MAX, path, "dnotify_file");
	if (mk_file(name, filepath, 4096) < 0)
		return;

	dnotify_exercise(name, filepath, path,
		dnotify_attrib_helper, DN_ATTRIB, NULL);
	(void)rm_file(name, filepath);
}

static int dnotify_access_helper(
	const char *name,
	const char *path,
	const void *dummy)
{
	int fd;
	char buffer[1];
	int rc = 0;

	(void)dummy;
	if ((fd = open(path, O_RDONLY)) < 0) {
		pr_err(stderr, "%s: cannot open file %s: errno=%d (%s)\n",
			name, path, errno, strerror(errno));
		return -1;
	}

	/* Just want to force an access */
do_access:
	if (opt_do_run && (read(fd, buffer, 1) < 0)) {
		if ((errno == EAGAIN) || (errno == EINTR))
			goto do_access;
		pr_err(stderr, "%s: cannot read file %s: errno=%d (%s)\n",
			name, path, errno, strerror(errno));
		rc = -1;
	}
	(void)close(fd);
	return rc;
}

static void dnotify_access_file(const char *name, const char *path)
{
	char filepath[PATH_MAX];

	mk_filename(filepath, PATH_MAX, path, "dnotify_file");
	if (mk_file(name, filepath, 4096) < 0)
		return;

	dnotify_exercise(name, filepath, path,
		dnotify_access_helper, DN_ACCESS, NULL);
	(void)rm_file(name, filepath);
}

static int dnotify_modify_helper(
	const char *name,
	const char *path,
	const void *dummy)
{
	int fd, rc = 0;
	char buffer[1] = { 0 };

	(void)dummy;
	if (mk_file(name, path, 4096) < 0)
		return -1;
	if ((fd = open(path, O_RDWR)) < 0) {
		pr_err(stderr, "%s: cannot open file %s: errno=%d (%s)\n",
			name, path, errno, strerror(errno));
		rc = -1;
		goto remove;
	}
do_modify:
	if (opt_do_run && (write(fd, buffer, 1) < 0)) {
		if ((errno == EAGAIN) || (errno == EINTR))
			goto do_modify;
		pr_err(stderr, "%s: cannot write to file %s: errno=%d (%s)\n",
			name, path, errno, strerror(errno));
		rc = -1;
	}
	(void)close(fd);
remove:
	(void)rm_file(name, path);
	return rc;
}

static void dnotify_modify_file(const char *name, const char *path)
{
	char filepath[PATH_MAX];

	mk_filename(filepath, PATH_MAX, path, "dnotify_file");
	dnotify_exercise(name, filepath, path,
		dnotify_modify_helper, DN_MODIFY, NULL);
}

static int dnotify_creat_helper(
	const char *name,
	const char *path,
	const void *dummy)
{
	(void)dummy;
	int fd;
	if ((fd = creat(path, FILE_FLAGS)) < 0) {
		pr_err(stderr, "%s: cannot create file %s: errno=%d (%s)\n",
			name, path, errno, strerror(errno));
		return -1;
	}
	(void)close(fd);
	return 0;
}

static void dnotify_creat_file(const char *name, const char *path)
{
	char filepath[PATH_MAX];

	mk_filename(filepath, PATH_MAX, path, "dnotify_file");
	dnotify_exercise(name, filepath, path,
		dnotify_creat_helper, DN_CREATE, NULL);
	(void)rm_file(name, filepath);
}

static int dnotify_delete_helper(
	const char *name,
	const char *path,
	const void *dummy)
{
	(void)dummy;

	return rm_file(name, path);
}

static void dnotify_delete_file(const char *name, const char *path)
{
	char filepath[PATH_MAX];

	mk_filename(filepath, PATH_MAX, path, "dnotify_file");
	if (mk_file(name, filepath, 4096) < 0)
		return;
	dnotify_exercise(name, filepath, path,
		dnotify_delete_helper, DN_DELETE, NULL);
	/* We remove (again) it just in case the test failed */
	(void)rm_file(name, filepath);
}

static int dnotify_rename_helper(
	const char *name,
	const char *oldpath,
	const void *private)
{
	char *newpath = (char*)private;

	if (rename(oldpath, newpath) < 0) {
		pr_err(stderr, "%s: cannot rename %s to %s: errno=%d (%s)\n",
			name, oldpath, newpath, errno, strerror(errno));
		return -1;
	}
	return 0;
}

static void dnotify_rename_file(const char *name, const char *path)
{
	char oldfile[PATH_MAX], newfile[PATH_MAX];

	mk_filename(oldfile, PATH_MAX, path, "dnotify_file");
	mk_filename(newfile, PATH_MAX, path, "dnotify_file_renamed");

	if (mk_file(name, oldfile, 4096) < 0)
		return;

	dnotify_exercise(name, oldfile, path,
		dnotify_rename_helper, DN_RENAME, newfile);
	(void)rm_file(name, oldfile);	/* In case rename failed */
	(void)rm_file(name, newfile);
}

static const dnotify_stress_t dnotify_stressors[] = {
	{ dnotify_access_file,		"DN_ACCESS" },
	{ dnotify_modify_file,		"DN_MODIFY" },
	{ dnotify_creat_file,		"DN_CREATE" },
	{ dnotify_delete_file,		"DN_DELETE" },
	{ dnotify_rename_file,		"DN_RENAME" },
	{ dnotify_attrib_file,		"DN_ATTRIB" },
	{ NULL,				NULL }
};

/*
 *  stress_dnotify()
 *	stress dnotify
 */
int stress_dnotify(
	uint64_t *const counter,
	const uint32_t instance,
	const uint64_t max_ops,
	const char *name)
{
	char dirname[PATH_MAX];
	int ret, i;
	const pid_t pid = getpid();
	struct sigaction act;

	act.sa_sigaction = dnotify_handler;
	sigemptyset(&act.sa_mask);
	act.sa_flags = SA_SIGINFO;
	if (sigaction(SIGRTMIN + 1, &act, NULL) < 0) {
		pr_err(stderr, "%s: sigaction failed: errno=%d (%s)\n",
			name, errno, strerror(errno));
		return EXIT_NO_RESOURCE;
	}

	stress_temp_dir(dirname, sizeof(dirname), name, pid, instance);
	ret = stress_temp_dir_mk(name, pid, instance);
	if (ret < 0)
		return exit_status(-ret);
	do {
		for (i = 0; opt_do_run && dnotify_stressors[i].func; i++)
			dnotify_stressors[i].func(name, dirname);
		(*counter)++;
	} while (opt_do_run && (!max_ops || *counter < max_ops));
	(void)stress_temp_dir_rm(name, pid, instance);

	return EXIT_SUCCESS;
}

#endif
