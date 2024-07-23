/*
 * Copyright (C) 2013-2021 Canonical, Ltd.
 * Copyright (C) 2022-2024 Colin Ian King.
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
#include "core-capabilities.h"
#include "core-killpid.h"

#include <sched.h>

#if defined(HAVE_SYS_MOUNT_H)
#include <sys/mount.h>
#endif

static const stress_help_t help[] = {
	{ NULL,	"ramfs N",	 "start N workers exercising ramfs mounts" },
	{ NULL, "ramfs-size N",  "set the ramfs size in bytes, e.g. 2M is 2MB" },
	{ NULL,	"ramfs-fill",	 "attempt to fill ramfs" },
	{ NULL,	"ramfs-ops N",	 "stop after N bogo ramfs mount operations" },
	{ NULL,	NULL,		 NULL }
};

/*
 *  stress_ramfs_supported()
 *      check if we can run this with SHIM_CAP_SYS_ADMIN capability
 */
static int stress_ramfs_supported(const char *name)
{
	if (!stress_check_capability(SHIM_CAP_SYS_ADMIN)) {
		pr_inf_skip("%s stressor will be skipped, "
			"need to be running with CAP_SYS_ADMIN "
			"rights for this stressor\n", name);
		return -1;
	}
	return 0;
}

static const stress_opt_t opts[] = {
	{ OPT_ramfs_size, "ramfs-size", TYPE_ID_UINT64_BYTES, 1 * MB, 1 * GB, NULL },
	{ OPT_ramfs_fill, "ramfs-fill", TYPE_ID_BOOL, 0, 1, NULL },
	END_OPT,
};

#if defined(__linux__) && \
    defined(HAVE_CLONE) && \
    defined(CLONE_NEWUSER) && \
    defined(CLONE_NEWNS) && \
    defined(CLONE_VM)

static volatile bool keep_mounting = true;

static void stress_ramfs_child_handler(int signum)
{
	(void)signum;

	keep_mounting = false;
}

/*
 *  stress_ramfs_umount()
 *	umount a path with retries.
 */
static void stress_ramfs_umount(stress_args_t *args, const char *path)
{
	int i;
	int ret;
	static const uint64_t ns = 100000000;	/* 1/10th second */
	char hugepath[PATH_MAX + 16];

	/*
	 *  umount is attempted at least twice, the first successful mount
	 *  and then a retry. In theory the EINVAL should be returned
	 *  on a umount of a path that has already been umounted, so we
	 *  know that umount been successful and can then return.
	 */
	for (i = 0; i < 100; i++) {
#if defined(HAVE_UMOUNT2) &&	\
    defined(MNT_FORCE)
		if (stress_mwc1()) {
			ret = umount2(path, MNT_FORCE);
		} else {
			ret = umount(path);
		}
#else
		ret = umount(path);
#endif
		if (ret == 0) {
			if (i > 1) {
				shim_nanosleep_uint64(ns);
			}
			continue;
		}
		switch (errno) {
		case EAGAIN:
		case EBUSY:
		case ENOMEM:
			/* Wait and then re-try */
			shim_nanosleep_uint64(ns);
			break;
		case EINVAL:
			/*
			 *  EINVAL if it's either invalid path or
			 *  it can't be umounted.  We now assume it
			 *  has been successfully umounted
			 */
			goto misc_tests;
		default:
			/* Unexpected, so report it */
			pr_inf("%s: umount failed %s: %d %s\n", args->name,
				path, errno, strerror(errno));
			break;
		}
	}

misc_tests:
	/* Exercise umount again, EINVAL */
	VOID_RET(int, umount(path));

	/* Exercise umount of empty path, ENOENT */
	VOID_RET(int, umount(""));

	/* Exercise illegal flags */
#if defined(HAVE_UMOUNT2)
	VOID_RET(int, umount2(path, ~0));
#endif

	/* Exercise umount of hugepath, ENAMETOOLONG */
	stress_rndstr(hugepath, sizeof(hugepath));
	VOID_RET(int, umount(hugepath));
}

/*
 *  stress_ramfs_fs_ops()
 *	exercise the ram based file system;
 */
static int stress_ramfs_fs_ops(
	stress_args_t *args,
	const uint64_t ramfs_size,
	const bool ramfs_fill,
	const char *pathname)
{
	char filename[PATH_MAX + 5];
	char symlinkname[PATH_MAX + 5];
	struct stat statbuf;
	int fd, rc = EXIT_SUCCESS;

	(void)stress_mk_filename(filename, sizeof(filename), pathname, "mnt");
	(void)stress_mk_filename(symlinkname, sizeof(symlinkname), pathname, "lnk");

	fd = open(filename, O_CREAT | O_RDWR, S_IRUSR | S_IWUSR);
	if (fd < 0) {
		pr_fail("%s: cannot create file on ram based file system, errno=%d (%s)\n",
			args->name, errno, strerror(errno));
		rc = EXIT_FAILURE;
	} else {
		if (shim_fstat(fd, &statbuf) < 0) {
			pr_fail("%s: cannot fstat file on ram based file system, errno=%d (%s)\n",
				args->name, errno, strerror(errno));
			rc = EXIT_FAILURE;
		}
		if (ramfs_fill) {
			off_t offset = 0, scale;

			for (scale = 80; scale <= 100; scale++) {
				off_t end = scale * (ramfs_size / 100);

				errno = 0;
				if (shim_fallocate(fd, 0, offset, end - offset) < 0)
					break;
				offset = end;
			}
		}
		if (symlink(pathname, symlinkname) < 0) {
			pr_fail("%s: cannot create symbolic link on ram based file system, errno=%d (%s)\n",
				args->name, errno, strerror(errno));
			rc = EXIT_FAILURE;
		}
		if (shim_lstat(symlinkname, &statbuf) < 0) {
			pr_fail("%s: cannot lstat symbolic link on ram based file system, errno=%d (%s)\n",
				args->name, errno, strerror(errno));
			rc = EXIT_FAILURE;
		}
		if (shim_unlink(symlinkname) < 0) {
			pr_fail("%s: cannot unlink symbolic file on ram based file system, errno=%d (%s)\n",
				args->name, errno, strerror(errno));
			rc = EXIT_FAILURE;
		}
		if (shim_unlink(filename) < 0) {
			pr_fail("%s: cannot unlink file on ram based file system, errno=%d (%s)\n",
				args->name, errno, strerror(errno));
			rc = EXIT_FAILURE;
		}
		(void)close(fd);
	}

	if (mkdir(filename, S_IRUSR | S_IWUSR) < 0) {
		pr_fail("%s: cannot create directory on ram based file system, errno=%d (%s)\n",
			args->name, errno, strerror(errno));
		rc = EXIT_FAILURE;
	} else {
		if (shim_lstat(filename, &statbuf) < 0) {
			pr_fail("%s: cannot lstat directory on ram based file system, errno=%d (%s)\n",
				args->name, errno, strerror(errno));
			rc = EXIT_FAILURE;
		}
		if (shim_rmdir(filename) < 0) {
			pr_fail("%s: cannot remove directory on ram based file system, errno=%d (%s)\n",
				args->name, errno, strerror(errno));
			rc = EXIT_FAILURE;
		}
	}
	return rc;
}

/*
 *  stress_ramfs_child()
 *	aggressively perform ramfs mounts, this can force out of memory
 *	situations
 */
static int stress_ramfs_child(stress_args_t *args)
{
	char pathname[PATH_MAX], realpathname[PATH_MAX];
	uint64_t ramfs_size = 2 * MB;
	bool ramfs_fill = false;
	int i = 0;
	int rc = EXIT_SUCCESS;
	const uint64_t page_size = (uint64_t)stress_get_page_size();
	const uint64_t page_mask = ~(page_size - 1);

	if (stress_sighandler(args->name, SIGALRM,
	    stress_ramfs_child_handler, NULL) < 0) {
		pr_fail("%s: SIGALRM sighandler failed, errno=%d (%s)\n",
			args->name, errno, strerror(errno));
		return EXIT_FAILURE;
	}
	if (stress_sighandler(args->name, SIGSEGV,
	    stress_ramfs_child_handler, NULL) < 0) {
		pr_fail("%s: SIGSEGV sighandler failed, errno=%d (%s)\n",
			args->name, errno, strerror(errno));
		return EXIT_FAILURE;
	}
	stress_parent_died_alarm();
	(void)sched_settings_apply(true);

	(void)stress_get_setting("ramfs-size", &ramfs_size);
	(void)stress_get_setting("ramfs-fill", &ramfs_fill);

	if (ramfs_size & (page_size - 1)) {
		ramfs_size &= page_mask;
		pr_inf("ramfs: rounding ramfs-size to %" PRIu64 " x %" PRId64 "K pages\n",
			ramfs_size / page_size, page_size >> 10);
	}

	stress_temp_dir(pathname, sizeof(pathname), args->name,
		args->pid, args->instance);
	if (mkdir(pathname, S_IRGRP | S_IWGRP) < 0) {
		pr_fail("%s: cannot mkdir %s, errno=%d (%s)\n",
			args->name, pathname, errno, strerror(errno));
		return EXIT_FAILURE;
	}
	if (!realpath(pathname, realpathname)) {
		pr_fail("%s: cannot realpath %s, errno=%d (%s)\n",
			args->name, pathname, errno, strerror(errno));
		(void)stress_temp_dir_rm_args(args);
		return EXIT_FAILURE;
	}

	do {
		int ret;
		char opt[32];
#if defined(__NR_fsopen) &&		\
    defined(__NR_fsmount) &&		\
    defined(__NR_fsconfig) &&		\
    defined(__NR_move_mount) &&		\
    defined(FSCONFIG_SET_STRING) &&	\
    defined(FSCONFIG_CMD_CREATE) &&	\
    defined(MOVE_MOUNT_F_EMPTY_PATH)
		int fd, mfd;
#endif
		const char *fs = (i++ & 1) ? "ramfs" : "tmpfs";

		(void)snprintf(opt, sizeof(opt), "size=%" PRIu64, ramfs_size);
		ret = mount("", realpathname, fs, 0, opt);
		if (ret < 0) {
			if (errno == EPERM) {
				pr_inf_skip("%s: cannot mount, no permission, skipping stressor\n",
					args->name);
				rc = EXIT_NO_RESOURCE;
			} else if ((errno != ENOSPC) &&
			    (errno != ENOMEM) &&
			    (errno != ENODEV)) {
				pr_fail("%s: mount failed, errno=%d (%s)\n",
					args->name, errno, strerror(errno));
			}
			/* Just in case, force umount */
			goto cleanup;
		}
		if (stress_ramfs_fs_ops(args, ramfs_size, ramfs_fill, realpathname) == EXIT_FAILURE)
			rc = EXIT_FAILURE;
		stress_ramfs_umount(args, realpathname);

#if defined(__NR_fsopen) &&		\
    defined(__NR_fsmount) &&		\
    defined(__NR_fsconfig) &&		\
    defined(__NR_move_mount) &&		\
    defined(FSCONFIG_SET_STRING) &&	\
    defined(FSCONFIG_CMD_CREATE) &&	\
    defined(MOVE_MOUNT_F_EMPTY_PATH)
		/*
		 *  Use the new Linux 5.2 mount system calls
		 */
		fd = shim_fsopen(fs, 0);
		if (fd < 0) {
			if ((errno == ENOSYS) ||
			    (errno == ENODEV))
				goto skip_fsopen;
			pr_fail("%s: fsopen failed: errno=%d (%s)\n",
				args->name, errno, strerror(errno));
			goto skip_fsopen;
		}
		(void)snprintf(opt, sizeof(opt), "%" PRIu64, ramfs_size);
		if (shim_fsconfig(fd, FSCONFIG_SET_STRING, "size", opt, 0) < 0) {
			if (errno == ENOSYS)
				goto cleanup_fd;
			pr_fail("%s: fsconfig failed: errno=%d (%s)\n",
				args->name, errno, strerror(errno));
			rc = EXIT_FAILURE;
			goto cleanup_fd;
		}
		if (shim_fsconfig(fd, FSCONFIG_CMD_CREATE, NULL, NULL, 0) < 0) {
			if (errno == ENOSYS)
				goto cleanup_fd;
			pr_fail("%s: fsconfig failed: errno=%d (%s)\n",
				args->name, errno, strerror(errno));
			rc = EXIT_FAILURE;
			goto cleanup_fd;
		}
		mfd = shim_fsmount(fd, 0, 0);
		if (mfd < 0) {
			if (errno == ENOSYS)
				goto cleanup_fd;
			/*
			 * We may just have no memory for this, non-fatal
			 * and try again
			 */
			if ((errno == ENOSPC) || (errno == ENOMEM))
				goto cleanup_fd;
			pr_fail("%s: fsmount failed: errno=%d (%s)\n",
				args->name, errno, strerror(errno));
			rc = EXIT_FAILURE;
			goto cleanup_fd;
		}
		if (shim_move_mount(mfd, "", AT_FDCWD, realpathname, MOVE_MOUNT_F_EMPTY_PATH) < 0) {
			if (errno == ENOSYS)
				goto cleanup_mfd;
			pr_fail("%s: move_mount failed: errno=%d (%s)\n",
				args->name, errno, strerror(errno));
			rc = EXIT_FAILURE;
			goto cleanup_mfd;

		}
cleanup_mfd:
		(void)close(mfd);
cleanup_fd:
		(void)close(fd);
		if (stress_ramfs_fs_ops(args, ramfs_size, ramfs_fill, realpathname) == EXIT_FAILURE)
			rc = EXIT_FAILURE;
		stress_ramfs_umount(args, realpathname);
skip_fsopen:

#endif
		stress_bogo_inc(args);
	} while (keep_mounting && stress_continue(args) &&
		 (!args->max_ops || (stress_bogo_get(args) < args->max_ops)));

cleanup:
	stress_ramfs_umount(args, realpathname);
	(void)stress_temp_dir_rm_args(args);

	return rc;
}

/*
 *  stress_ramfs_mount()
 *      stress ramfs mounting
 */
static int stress_ramfs_mount(stress_args_t *args)
{
	int pid;

	stress_set_proc_state(args->name, STRESS_STATE_RUN);
	stress_sync_start_wait(args);

	do {
again:
		if (!stress_continue_flag())
			break;

		pid = fork();
		if (pid < 0) {
			if (stress_redo_fork(args, errno))
				goto again;
			if (!stress_continue(args))
				goto finish;
			pr_err("%s: fork failed: errno=%d (%s)\n",
				args->name, errno, strerror(errno));
		} else if (pid > 0) {
			int status, waitret;

			/* Parent, wait for child */
			waitret = shim_waitpid(pid, &status, 0);
			if (waitret < 0) {
				if (errno != EINTR) {
					pr_dbg("%s: waitpid(): errno=%d (%s)\n",
						args->name, errno, strerror(errno));
					(void)stress_kill_pid(pid);
				}
				(void)shim_waitpid(pid, &status, 0);
			} else if (WIFSIGNALED(status)) {
				pr_dbg("%s: child died: %s (instance %d)\n",
					args->name, stress_strsignal(WTERMSIG(status)),
					args->instance);
				/* If we got killed by OOM killer, re-start */
				if (WTERMSIG(status) == SIGKILL) {
					stress_log_system_mem_info();
					pr_dbg("%s: assuming killed by OOM killer, "
						"restarting again (instance %d)\n",
						args->name, args->instance);
					goto again;
				}
			} else if (WEXITSTATUS(status) == EXIT_FAILURE) {
				pr_fail("%s: child mount/umount failed\n", args->name);
				return EXIT_FAILURE;
			}
		} else {
			_exit(stress_ramfs_child(args));
		}
	} while (stress_continue(args));

finish:
	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);

	return EXIT_SUCCESS;
}

stressor_info_t stress_ramfs_info = {
	.stressor = stress_ramfs_mount,
	.class = CLASS_OS,
	.opts = opts,
	.supported = stress_ramfs_supported,
	.verify = VERIFY_ALWAYS,
	.help = help
};
#else
stressor_info_t stress_ramfs_info = {
	.stressor = stress_unimplemented,
	.class = CLASS_OS,
	.opts = opts,
	.supported = stress_ramfs_supported,
	.verify = VERIFY_ALWAYS,
	.help = help,
	.unimplemented_reason = "built without clone() or only supported on Linux"
};
#endif
