/*
 * Copyright (C) 2013-2019 Canonical, Ltd.
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
 */
#include "stress-ng.h"

static const help_t help[] = {
	{ NULL,	"ramfs N",	 "start N workers exercising ramfs mounts" },
	{ NULL,	"ramfs-ops N",	 "stop after N bogo ramfs mount operations" },
	{ NULL, "ramfs-bytes N", "set the ramfs size in bytes, e.g. 2M is 2MB" },
	{ NULL,	NULL,		 NULL }
};

/*
 *  stress_ramfs_supported()
 *      check if we can run this as root
 */
static int stress_ramfs_supported(void)
{
	if (!stress_check_capability(SHIM_CAP_SYS_ADMIN)) {
		pr_inf("ramfs stressor will be skipped, "
			"need to be running with CAP_SYS_ADMIN "
			"rights for this stressor\n");
		return -1;
	}
	return 0;
}

#if defined(__linux__) && \
    defined(HAVE_CLONE) && \
    defined(CLONE_NEWUSER) && \
    defined(CLONE_NEWNS) && \
    defined(CLONE_VM)

#define CLONE_STACK_SIZE	(128*1024)

static volatile bool keep_mounting = true;

static int stress_set_ramfs_size(const char *opt)
{
        uint64_t ramfs_size;
	const uint64_t page_size = (uint64_t)stress_get_pagesize();
	const uint64_t page_mask = ~(page_size - 1);

        ramfs_size = get_uint64_byte(opt);
        check_range_bytes("ramfs-size", ramfs_size,
                1 * MB, 1 * GB);
	if (ramfs_size & (page_size - 1)) {
		ramfs_size &= page_mask;
		pr_inf("ramfs: rounding ramfs-size to %" PRIu64 " x %" PRId64 "K pages\n",
			ramfs_size / page_size, page_size >> 10);
	}
        return set_setting("ramfs-size", TYPE_ID_UINT64, &ramfs_size);
}

static void stress_ramfs_child_handler(int signum)
{
	(void)signum;

	keep_mounting = false;
}

/*
 *  stress_ramfs_umount()
 *	umount a path with retries.
 */
static void stress_ramfs_umount(const args_t *args, const char *path)
{
	int i;
	static const uint64_t ns = 100000000;	/* 1/10th second */

	/*
	 *  umount is attempted at least twice, the first successfull mount
	 *  and then a retry. In theory the EINVAL should be returned
	 *  on a umount of a path that has already been umounted, so we
	 *  know that umount been successful and can then return.
	 */
	for (i = 0; i < 100; i++) {
		int ret;

		ret = umount(path);
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
			return;
			break;
		default:
			/* Unexpected, so report it */
			pr_inf("%s: umount failed %s: %d %s\n", args->name,
				path, errno, strerror(errno));
			break;
		}
	}
}

/*
 *  stress_ramfs_child()
 *	aggressively perform ramfs mounts, this can force out of memory
 *	situations
 */
static int stress_ramfs_child(const args_t *args)
{
	char pathname[PATH_MAX], realpathname[PATH_MAX];
	uint64_t ramfs_size = 2 * MB;

	if (stress_sighandler(args->name, SIGALRM,
	    stress_ramfs_child_handler, NULL) < 0) {
		pr_fail_err("sighandler SIGALRM");
		return EXIT_FAILURE;
	}
	if (stress_sighandler(args->name, SIGSEGV,
	    stress_ramfs_child_handler, NULL) < 0) {
		pr_fail_err("sighandler SIGSEGV");
		return EXIT_FAILURE;
	}
	(void)setpgid(0, g_pgrp);
	stress_parent_died_alarm();

	(void)get_setting("ramfs-size", &ramfs_size);

	stress_temp_dir(pathname, sizeof(pathname), args->name, args->pid, args->instance);
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
		int rc;
		char opt[32];
#if defined(__NR_fsopen) &&	\
    defined(__NR_fsmount) &&	\
    defined(__NR_fsconfig) &&	\
    defined(__NR_move_mount)
		int fd, mfd;
#endif

		(void)snprintf(opt, sizeof(opt), "size=%" PRIu64, ramfs_size);
		rc = mount("", realpathname, "tmpfs", 0, opt);
		if (rc < 0) {
			if ((errno != ENOSPC) && (errno != ENOMEM))
				pr_fail_err("mount");

			/* Just in case, force umount */
			goto cleanup;
		}
		stress_ramfs_umount(args, realpathname);

#if defined(__NR_fsopen) &&	\
    defined(__NR_fsmount) &&	\
    defined(__NR_fsconfig) &&	\
    defined(__NR_move_mount)
		/*
		 *  Use the new Linux 5.2 mount system calls
		 */
		fd = shim_fsopen("tmpfs", 0);
		if (fd < 0) {
			if (errno == ENOSYS)
				goto skip_fsopen;
			pr_fail("%s: fsopen failed: errno=%d (%s)\n",
				args->name, errno, strerror(errno));
		}
		(void)snprintf(opt, sizeof(opt), "%" PRIu64, ramfs_size);
		if (shim_fsconfig(fd, FSCONFIG_SET_STRING, "size", opt, 0) < 0) {
			if (errno == ENOSYS)
				goto cleanup_fd;
			pr_fail("%s: fsconfig failed: errno=%d (%s)\n",
				args->name, errno, strerror(errno));
			goto cleanup_fd;
		}
		if (shim_fsconfig(fd, FSCONFIG_CMD_CREATE, NULL, NULL, 0) < 0) {
			if (errno == ENOSYS)
				goto cleanup_fd;
			pr_fail("%s: fsconfig failed: errno=%d (%s)\n",
				args->name, errno, strerror(errno));
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
			goto cleanup_fd;
		}
		if (shim_move_mount(mfd, "", AT_FDCWD, realpathname, MOVE_MOUNT_F_EMPTY_PATH) < 0) {
			if (errno == ENOSYS)
				goto cleanup_mfd;
			pr_fail("%s: move_mount failed: errno=%d (%s)\n",
				args->name, errno, strerror(errno));
			goto cleanup_mfd;

		}
cleanup_mfd:
		(void)close(mfd);
cleanup_fd:
		(void)close(fd);
		stress_ramfs_umount(args, realpathname);
skip_fsopen:

#endif
		inc_counter(args);
	} while (keep_mounting && g_keep_stressing_flag &&
		 (!args->max_ops || get_counter(args) < args->max_ops));

cleanup:
	stress_ramfs_umount(args, realpathname);
	(void)stress_temp_dir_rm_args(args);

	return 0;
}

/*
 *  stress_ramfs_mount()
 *      stress ramfs mounting
 */
static int stress_ramfs_mount(const args_t *args)
{
	int pid = 0;

	do {
again:
		if (!g_keep_stressing_flag)
			break;

		pid = fork();
		if (pid < 0) {
			if (errno == EAGAIN)
				goto again;
			pr_err("%s: fork failed: errno=%d (%s)\n",
				args->name, errno, strerror(errno));
		} else if (pid > 0) {
			int status, waitret;

			/* Parent, wait for child */
			(void)setpgid(pid, g_pgrp);
			waitret = shim_waitpid(pid, &status, 0);
			if (waitret < 0) {
				if (errno != EINTR) {
					pr_dbg("%s: waitpid(): errno=%d (%s)\n",
						args->name, errno, strerror(errno));
					(void)kill(pid, SIGTERM);
					(void)kill(pid, SIGKILL);
				}
				(void)shim_waitpid(pid, &status, 0);
			} else if (WIFSIGNALED(status)) {
				pr_dbg("%s: child died: %s (instance %d)\n",
					args->name, stress_strsignal(WTERMSIG(status)),
					args->instance);
				/* If we got killed by OOM killer, re-start */
				if (WTERMSIG(status) == SIGKILL) {
					log_system_mem_info();
					pr_dbg("%s: assuming killed by OOM killer, "
						"restarting again (instance %d)\n",
						args->name, args->instance);
					goto again;
				}
			}
		} else if (pid == 0) {
			stress_ramfs_child(args);
			_exit(EXIT_SUCCESS);
		}
	} while (keep_stressing());

	return EXIT_SUCCESS;
}

static const opt_set_func_t opt_set_funcs[] = {
        { OPT_ramfs_size,	stress_set_ramfs_size},
        { 0,                    NULL }
};

stressor_info_t stress_ramfs_info = {
	.stressor = stress_ramfs_mount,
	.class = CLASS_OS,
	.opt_set_funcs = opt_set_funcs,
	.supported = stress_ramfs_supported,
	.help = help
};
#else
stressor_info_t stress_ramfs_info = {
	.stressor = stress_not_implemented,
	.class = CLASS_OS,
	.supported = stress_ramfs_supported,
	.help = help
};
#endif
