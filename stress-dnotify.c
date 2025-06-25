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

#if defined(HAVE_SYS_SELECT_H)
#include <sys/select.h>
#endif

static const stress_help_t help[] = {
	{ NULL,	"dnotify N",	 "start N workers exercising dnotify events" },
	{ NULL,	"dnotify-ops N", "stop dnotify workers after N bogo operations" },
	{ NULL,	NULL,		 NULL }
};

#if defined(F_NOTIFY) && \
    defined(HAVE_SYS_SELECT_H)

#define FILE_FLAGS	(S_IRUSR | S_IWUSR)

#define BUF_SIZE	(4096)

static volatile int dnotify_fd;

static int stress_dnotify_supported(const char *name)
{
	char buf[64];
	static const char path[] = "/proc/sys/fs/dir-notify-enable";
	int enabled;

	if (stress_system_read(path, buf, sizeof(buf)) < 0) {
		pr_inf_skip("%s stressor will be skipped, cannot "
			"open '%s', CONFIG_DNOTIFY is probably not set\n",
			name, path);
		return -1;
	}
	if (sscanf(buf, "%d", &enabled) != 1) {
		pr_inf_skip("%s stressor will be skipped, cannot "
			"parse '%s'\n", name, path);
		return -1;
	}
	if (enabled != 1) {
		pr_inf_skip("%s stressor will be skipped, dnotify is not enabled\n", name);
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

typedef int (*stress_dnotify_helper)(stress_args_t *args, const char *path, const void *private);
typedef int (*stress_dnotify_func)(stress_args_t *args, const char *path);

typedef struct {
	const stress_dnotify_func func;
	const char *description;
} stress_dnotify_stress_t;

/*
 *  dnotify_exercise()
 *	run a given test helper function 'func' and see if this triggers the
 *	required dnotify event flags 'flags'.
 */
static int dnotify_exercise(
	stress_args_t *args,	/* Stressor args */
	const char *filename,		/* Filename in test */
	const char *watchname,		/* File or directory to watch using dnotify */
	const stress_dnotify_helper func,/* Helper func */
	const unsigned long int flags,	/* DN_* flags to watch for */
	void *private)			/* Helper func private data */
{
	int fd, i = 0, rc = 0;
#if defined(DN_MULTISHOT)
	unsigned long int flags_ms = flags | DN_MULTISHOT;
#else
	unsigned long int flags_ms = flags;
#endif

	if ((fd = open(watchname, O_RDONLY)) < 0) {
		pr_fail("%s: open %s failed, errno=%d (%s)\n",
			args->name, watchname, errno, strerror(errno));
		return -1;
	}
	if (fcntl(fd, F_SETSIG, SIGRTMIN + 1) < 0) {
		pr_fail("%s: fcntl F_SETSIG failed, errno=%d (%s)\n",
			args->name, errno, strerror(errno));
		rc = -1;
		goto cleanup;
	}
	if (fcntl(fd, F_NOTIFY, flags_ms) < 0) {
		pr_fail("%s: fcntl F_NOTIFY failed, errno=%d (%s)\n",
			args->name, errno, strerror(errno));
		rc = -1;
		goto cleanup;
	}

	dnotify_fd = -1;
	if (func(args, filename, private) < 0) {
		rc = -1;
		goto cleanup;
	}

	/* Wait for up to 2 seconds for event */
	while ((i < 2000) && (dnotify_fd == -1)) {
		if (UNLIKELY(!stress_continue(args)))
			goto cleanup;
		i++;
		(void)shim_usleep(1000);
	}

	/*
	 *  If we didn't time out and we got an expected fd, report
	 *  a failure.
	 */
	if ((dnotify_fd != -1) && (dnotify_fd != fd)) {
		pr_fail("%s: did not get expected dnotify file descriptor %d, got %d instead\n",
			args->name, fd, dnotify_fd);
		rc = -1;
	}

cleanup:
	/* disable */
	if (fcntl(fd, F_NOTIFY, 0) < 0) {
		pr_fail("%s: fcntl F_NOTIFY failed, errno=%d (%s)\n",
			args->name, errno, strerror(errno));
		rc = -1;
	}
	(void)close(fd);

	return rc;
}

/*
 *  rm_file()
 *	remove a file
 */
static int rm_file(stress_args_t *args, const char *path)
{
	if ((shim_force_unlink(path) < 0) && (errno != ENOENT)) {
		pr_err("%s: cannot remove file %s, errno=%d (%s)\n",
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

	if ((fd = open(filename, O_CREAT | O_RDWR, FILE_FLAGS)) < 0) {
		if ((errno == ENFILE) || (errno == ENOMEM) || (errno == ENOSPC))
			return -1;
		pr_err("%s: cannot create file %s, errno=%d (%s)\n",
			args->name, filename, errno, strerror(errno));
		return -1;
	}

	(void)shim_memset(buffer, 'x', BUF_SIZE);
	while (LIKELY(stress_continue(args) && (sz > 0))) {
		const size_t n = (sz > BUF_SIZE) ? BUF_SIZE : sz;
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

static int dnotify_attrib_helper(
	stress_args_t *args,
	const char *path,
	const void *signum)
{
	(void)signum;
	if (chmod(path, S_IRUSR | S_IWUSR) < 0) {
		pr_err("%s: cannot chmod file %s, errno=%d (%s)\n",
			args->name, path, errno, strerror(errno));
		return -1;
	}
	return 0;
}

static int dnotify_attrib_file(stress_args_t *args, const char *path)
{
	char filepath[PATH_MAX];
	int rc;

	stress_mk_filename(filepath, sizeof(filepath), path, "dnotify_file");
	if (mk_file(args, filepath, 4096) < 0)
		return 0;

	rc = dnotify_exercise(args, filepath, path,
		dnotify_attrib_helper, DN_ATTRIB, NULL);
	(void)rm_file(args, filepath);

	return rc;
}

static int dnotify_access_helper(
	stress_args_t *args,
	const char *path,
	const void *signum)
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

static int dnotify_access_file(stress_args_t *args, const char *path)
{
	char filepath[PATH_MAX];
	int rc;

	stress_mk_filename(filepath, sizeof(filepath), path, "dnotify_file");
	if (mk_file(args, filepath, 4096) < 0)
		return 0;

	rc = dnotify_exercise(args, filepath, path,
		dnotify_access_helper, DN_ACCESS, NULL);
	(void)rm_file(args, filepath);
	return rc;
}

static int dnotify_modify_helper(
	stress_args_t *args,
	const char *path,
	const void *signum)
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
		if (errno != ENOSPC) {
			pr_err("%s: cannot write to file %s, errno=%d (%s)\n",
				args->name, path, errno, strerror(errno));
			rc = -1;
		}
	}
	(void)close(fd);
remove:
	(void)rm_file(args, path);
	return rc;
}

static int dnotify_modify_file(stress_args_t *args, const char *path)
{
	char filepath[PATH_MAX];

	stress_mk_filename(filepath, sizeof(filepath), path, "dnotify_file");
	return dnotify_exercise(args, filepath, path, dnotify_modify_helper,
				DN_MODIFY, NULL);

}

static int dnotify_creat_helper(
	stress_args_t *args,
	const char *path,
	const void *signum)
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

static int dnotify_creat_file(stress_args_t *args, const char *path)
{
	char filepath[PATH_MAX];
	int rc;

	stress_mk_filename(filepath, sizeof(filepath), path, "dnotify_file");
	rc = dnotify_exercise(args, filepath, path,
		dnotify_creat_helper, DN_CREATE, NULL);
	(void)rm_file(args, filepath);
	return rc;
}

static int dnotify_delete_helper(
	stress_args_t *args,
	const char *path,
	const void *signum)
{
	(void)signum;

	return rm_file(args, path);
}

static int dnotify_delete_file(stress_args_t *args, const char *path)
{
	char filepath[PATH_MAX];
	int rc;

	stress_mk_filename(filepath, sizeof(filepath), path, "dnotify_file");
	if (mk_file(args, filepath, 4096) < 0)
		return 0;
	rc = dnotify_exercise(args, filepath, path,
		dnotify_delete_helper, DN_DELETE, NULL);
	/* We remove (again) it just in case the test failed */
	(void)rm_file(args, filepath);
	return rc;
}

static int dnotify_rename_helper(
	stress_args_t *args,
	const char *oldpath,
	const void *private)
{
	const char *newpath = (const char *)private;

	if (rename(oldpath, newpath) < 0) {
		pr_err("%s: cannot rename %s to %s, errno=%d (%s)\n",
			args->name, oldpath, newpath, errno, strerror(errno));
		return -1;
	}
	return 0;
}

static int dnotify_rename_file(stress_args_t *args, const char *path)
{
	char oldfile[PATH_MAX], newfile[PATH_MAX];
	int rc;

	stress_mk_filename(oldfile, sizeof(oldfile), path, "dnotify_file");
	stress_mk_filename(newfile, sizeof(newfile), path, "dnotify_file_renamed");

	if (mk_file(args, oldfile, 4096) < 0)
		return 0;

	rc = dnotify_exercise(args, oldfile, path,
		dnotify_rename_helper, DN_RENAME, newfile);
	(void)rm_file(args, oldfile);	/* In case rename failed */
	(void)rm_file(args, newfile);
	return rc;
}

static const stress_dnotify_stress_t dnotify_stressors[] = {
	{ dnotify_access_file,		"DN_ACCESS" },
	{ dnotify_modify_file,		"DN_MODIFY" },
	{ dnotify_creat_file,		"DN_CREATE" },
	{ dnotify_delete_file,		"DN_DELETE" },
	{ dnotify_rename_file,		"DN_RENAME" },
	{ dnotify_attrib_file,		"DN_ATTRIB" },
};

/*
 *  stress_dnotify()
 *	stress dnotify
 */
static int stress_dnotify(stress_args_t *args)
{
	char pathname[PATH_MAX];
	int ret, rc = EXIT_SUCCESS;
	struct sigaction act;
	size_t i;

	act.sa_sigaction = dnotify_handler;
	(void)sigemptyset(&act.sa_mask);
	act.sa_flags = SA_SIGINFO;
	if (sigaction(SIGRTMIN + 1, &act, NULL) < 0) {
		pr_err("%s: sigaction failed, errno=%d (%s)\n",
			args->name, errno, strerror(errno));
		return EXIT_NO_RESOURCE;
	}
	/* Ignore stray SIGIOs */
	if (stress_sighandler(args->name, SIGIO, stress_sighandler_nop, NULL) < 0)
		return EXIT_NO_RESOURCE;

	stress_temp_dir_args(args, pathname, sizeof(pathname));
	ret = stress_temp_dir_mk_args(args);
	if (ret < 0)
		return stress_exit_status(-ret);

	stress_set_proc_state(args->name, STRESS_STATE_SYNC_WAIT);
	stress_sync_start_wait(args);
	stress_set_proc_state(args->name, STRESS_STATE_RUN);

	do {
		for (i = 0; LIKELY(stress_continue(args) && (i < SIZEOF_ARRAY(dnotify_stressors))); i++) {
			ret = dnotify_stressors[i].func(args, pathname);
			if (ret < 0) {
				rc = EXIT_FAILURE;
				goto tidy;
			}
		}
		stress_bogo_inc(args);
	} while (stress_continue(args));

tidy:
	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);
	(void)stress_temp_dir_rm_args(args);

	return rc;
}
const stressor_info_t stress_dnotify_info = {
	.stressor = stress_dnotify,
	.classifier = CLASS_FILESYSTEM | CLASS_SCHEDULER | CLASS_OS,
	.supported = stress_dnotify_supported,
	.verify = VERIFY_ALWAYS,
	.help = help
};
#else
const stressor_info_t stress_dnotify_info = {
	.stressor = stress_unimplemented,
	.classifier = CLASS_FILESYSTEM | CLASS_SCHEDULER | CLASS_OS,
	.verify = VERIFY_ALWAYS,
	.help = help,
	.unimplemented_reason = "built without dnotify support or sys/select.h"
};
#endif
