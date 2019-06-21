/*
 * Copyright (C) 2012-2019 Canonical, Ltd.
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
#include "stress-ng.h"

static const help_t help[] = {
	{ NULL,	"dnotify N",	 "start N workers exercising dnotify events" },
	{ NULL,	"dnotify-ops N", "stop dnotify workers after N bogo operations" },
	{ NULL,	NULL,		 NULL }
};

#if defined(F_NOTIFY) && \
    defined(HAVE_SYS_SELECT_H)

#define DIR_FLAGS	(S_IRWXU | S_IRWXG)
#define FILE_FLAGS	(S_IRUSR | S_IWUSR)

#define BUF_SIZE	(4096)

static volatile int dnotify_fd;

static int stress_dnotify_supported(void)
{
	char buf[64];
	const char path[] = "/proc/sys/fs/dir-notify-enable";
	int enabled;

	if (system_read(path, buf, sizeof(buf)) < 0) {
		pr_inf("dnotify stressor will be skipped, cannot "
			"open '%s', CONFIG_DNOTIFY is probably not set\n",
			path);
                return -1;
	}
	if (sscanf(buf, "%d", &enabled) != 1) {
		pr_inf("dnotify stressor will be skipped, cannot "
			"parse '%s'\n", path);
                return -1;
	}
	if (enabled != 1) {
		pr_inf("dnotify stressor will be skipped, dnotify is not enabled\n");
                return -1;
	}
	return 0;
}

static void dnotify_handler(int sig, siginfo_t *si, void *data)
{
	(void)sig;
	(void)data;

	dnotify_fd = si->si_fd;
}

typedef int (*dnotify_helper)(const args_t *args, const char *path, const void *private);
typedef void (*dnotify_func)(const args_t *args, const char *path);

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
	const args_t *args,	/* Stressor args */
	const char *filename,	/* Filename in test */
	const char *watchname,	/* File or directory to watch using dnotify */
	const dnotify_helper func,	/* Helper func */
	const int flags,	/* DN_* flags to watch for */
	void *private)		/* Helper func private data */
{
	int fd, i = 0;

	if ((fd = open(watchname, O_RDONLY)) < 0) {
		pr_fail_err("open");
		return;
	}
	if (fcntl(fd, F_SETSIG, SIGRTMIN + 1) < 0) {
		pr_fail_err("fcntl F_SETSIG");
		goto cleanup;
	}
	if (fcntl(fd, F_NOTIFY, flags) < 0) {
		pr_fail_err("fcntl F_NOTIFY");
		goto cleanup;
	}

	dnotify_fd = -1;
	if (func(args, filename, private) < 0)
		goto cleanup;

	/* Wait for up to 1 second for event */
	while ((i < 1000) && (dnotify_fd == -1)) {
		if (!g_keep_stressing_flag)
			goto cleanup;
		i++;
		(void)shim_usleep(1000);
	}

	if (dnotify_fd != fd) {
		pr_fail("%s: did not get expected dnotify "
			"file descriptor\n", args->name);
	}

cleanup:
	(void)close(fd);

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

	if ((fd = open(filename, O_CREAT | O_RDWR, FILE_FLAGS)) < 0) {
		pr_err("%s: cannot create file %s: errno=%d (%s)\n",
			args->name, filename, errno, strerror(errno));
		return -1;
	}

	(void)memset(buffer, 'x', BUF_SIZE);
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

static int dnotify_attrib_helper(
	const args_t *args,
	const char *path,
	const void *signum)
{
	(void)signum;
	if (chmod(path, S_IRUSR | S_IWUSR) < 0) {
		pr_err("%s: cannot chmod file %s: errno=%d (%s)\n",
			args->name, path, errno, strerror(errno));
		return -1;
	}
	return 0;
}

static void dnotify_attrib_file(const args_t *args, const char *path)
{
	char filepath[PATH_MAX];

	mk_filename(filepath, PATH_MAX, path, "dnotify_file");
	if (mk_file(args, filepath, 4096) < 0)
		return;

	dnotify_exercise(args, filepath, path,
		dnotify_attrib_helper, DN_ATTRIB, NULL);
	(void)rm_file(args, filepath);
}

static int dnotify_access_helper(
	const args_t *args,
	const char *path,
	const void *signum)
{
	int fd;
	char buffer[1];
	int rc = 0;

	(void)signum;
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

static void dnotify_access_file(const args_t *args, const char *path)
{
	char filepath[PATH_MAX];

	mk_filename(filepath, PATH_MAX, path, "dnotify_file");
	if (mk_file(args, filepath, 4096) < 0)
		return;

	dnotify_exercise(args, filepath, path,
		dnotify_access_helper, DN_ACCESS, NULL);
	(void)rm_file(args, filepath);
}

static int dnotify_modify_helper(
	const args_t *args,
	const char *path,
	const void *signum)
{
	int fd, rc = 0;
	char buffer[1] = { 0 };

	(void)signum;
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

static void dnotify_modify_file(const args_t *args, const char *path)
{
	char filepath[PATH_MAX];

	mk_filename(filepath, PATH_MAX, path, "dnotify_file");
	dnotify_exercise(args, filepath, path,
		dnotify_modify_helper, DN_MODIFY, NULL);
}

static int dnotify_creat_helper(
	const args_t *args,
	const char *path,
	const void *signum)
{
	(void)signum;
	int fd;
	if ((fd = creat(path, FILE_FLAGS)) < 0) {
		pr_err("%s: cannot create file %s: errno=%d (%s)\n",
			args->name, path, errno, strerror(errno));
		return -1;
	}
	(void)close(fd);
	return 0;
}

static void dnotify_creat_file(const args_t *args, const char *path)
{
	char filepath[PATH_MAX];

	mk_filename(filepath, PATH_MAX, path, "dnotify_file");
	dnotify_exercise(args, filepath, path,
		dnotify_creat_helper, DN_CREATE, NULL);
	(void)rm_file(args, filepath);
}

static int dnotify_delete_helper(
	const args_t *args,
	const char *path,
	const void *signum)
{
	(void)signum;

	return rm_file(args, path);
}

static void dnotify_delete_file(const args_t *args, const char *path)
{
	char filepath[PATH_MAX];

	mk_filename(filepath, PATH_MAX, path, "dnotify_file");
	if (mk_file(args, filepath, 4096) < 0)
		return;
	dnotify_exercise(args, filepath, path,
		dnotify_delete_helper, DN_DELETE, NULL);
	/* We remove (again) it just in case the test failed */
	(void)rm_file(args, filepath);
}

static int dnotify_rename_helper(
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

static void dnotify_rename_file(const args_t *args, const char *path)
{
	char oldfile[PATH_MAX], newfile[PATH_MAX];

	mk_filename(oldfile, PATH_MAX, path, "dnotify_file");
	mk_filename(newfile, PATH_MAX, path, "dnotify_file_renamed");

	if (mk_file(args, oldfile, 4096) < 0)
		return;

	dnotify_exercise(args, oldfile, path,
		dnotify_rename_helper, DN_RENAME, newfile);
	(void)rm_file(args, oldfile);	/* In case rename failed */
	(void)rm_file(args, newfile);
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
static int stress_dnotify(const args_t *args)
{
	char pathname[PATH_MAX];
	int ret, i;
	struct sigaction act;

	act.sa_sigaction = dnotify_handler;
	(void)sigemptyset(&act.sa_mask);
	act.sa_flags = SA_SIGINFO;
	if (sigaction(SIGRTMIN + 1, &act, NULL) < 0) {
		pr_err("%s: sigaction failed: errno=%d (%s)\n",
			args->name, errno, strerror(errno));
		return EXIT_NO_RESOURCE;
	}

	stress_temp_dir_args(args, pathname, sizeof(pathname));
	ret = stress_temp_dir_mk_args(args);
	if (ret < 0)
		return exit_status(-ret);
	do {
		for (i = 0; g_keep_stressing_flag && dnotify_stressors[i].func; i++)
			dnotify_stressors[i].func(args, pathname);
		inc_counter(args);
	} while (keep_stressing());
	(void)stress_temp_dir_rm_args(args);

	return EXIT_SUCCESS;
}
stressor_info_t stress_dnotify_info = {
	.stressor = stress_dnotify,
	.class = CLASS_FILESYSTEM | CLASS_SCHEDULER | CLASS_OS,
	.supported = stress_dnotify_supported,
	.help = help
};
#else
stressor_info_t stress_dnotify_info = {
	.stressor = stress_not_implemented,
	.class = CLASS_FILESYSTEM | CLASS_SCHEDULER | CLASS_OS,
	.help = help
};
#endif
