/*
 * Copyright (C) 2024-2025 Colin Ian King.
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
#include "core-capabilities.h"
#include "core-killpid.h"

#if defined(HAVE_SYS_MOUNT_H)
#include <sys/mount.h>
#endif

#define STRESS_UMOUNT_PROCS	(3)

static const stress_help_t help[] = {
	{ NULL,	"umount N",	 "start N workers exercising umount races" },
	{ NULL,	"umount-ops N",	 "stop after N bogo umount operations" },
	{ NULL,	NULL,		 NULL }
};

/*
 *  stress_umount_supported()
 *      check if we can run this with SHIM_CAP_SYS_ADMIN capability
 */
static int stress_umount_supported(const char *name)
{
	if (!stress_check_capability(SHIM_CAP_SYS_ADMIN)) {
		pr_inf_skip("%s stressor will be skipped, "
			"need to be running with CAP_SYS_ADMIN "
			"rights for this stressor\n", name);
		return -1;
	}
	return 0;
}

#if defined(__linux__)
/*
 *  stress_umount_umount()
 *	umount a path with retries.
 */
static int stress_umount_umount(stress_args_t *args, const char *path, const uint64_t ns_delay)
{
	int i, ret, rc = EXIT_SUCCESS;

	/*
	 *  umount is attempted at least twice, the first successful mount
	 *  and then a retry. In theory the EINVAL should be returned
	 *  on a umount of a path that has already been umounted, so we
	 *  know that umount been successful and can then return.
	 */
	for (i = 0; i < 100; i++) {
		static bool warned = false;

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
				(void)shim_nanosleep_uint64(ns_delay);
			}
			continue;
		}
		switch (errno) {
		case EPERM:
			if (!warned) {
				warned = true;
				pr_inf_skip("%s: umount failed, no permission, skipping stresor\n",
					args->name);
			}
			return EXIT_NO_RESOURCE;
		case EAGAIN:
		case EBUSY:
		case ENOMEM:
			/* Wait and then re-try */
			(void)shim_nanosleep_uint64(ns_delay);
			break;
		case EINVAL:
		case ENOENT:
			/*
			 *  EINVAL if it's either invalid path or
			 *  it can't be umounted.  We now assume it
			 *  has been successfully umounted
			 */
			return rc;
		default:
			/* Unexpected, so report it */
			pr_inf("%s: umount failed %s, errno=%d %s\n", args->name,
				path, errno, strerror(errno));
			return EXIT_FAILURE;
		}
	}
	return rc;
}

/*
 *  stress_umount_read_proc_mounts()
 *	exercise reading of proc mounts
 */
static void stress_umount_read_proc_mounts(stress_args_t *args, const char *path)
{
	(void)path;

	do {
		int fd;
		char buffer[4096];
		ssize_t ret;

		fd = open("/proc/mounts", O_RDONLY);
		if (UNLIKELY(fd < 0))
			break;
		do {
			ret = read(fd, buffer, sizeof(buffer));
		} while (ret > 0);
		(void)close(fd);

		(void)shim_nanosleep_uint64(stress_mwc64modn(1000000));
	} while (stress_continue(args));

	_exit(0);
}

/*
 *  stress_umount_umounter()
 *	racy unmount, hammer time!
 */
static void stress_umount_umounter(stress_args_t *args, const char *path)
{
	int rc;
	stress_parent_died_alarm();
	(void)sched_settings_apply(true);

	do {
		rc = stress_umount_umount(args, path, 10000);
		(void)shim_nanosleep_uint64(stress_mwc64modn(10000));
	} while (stress_continue(args));

	_exit(rc);
}

/*
 *  stress_umount_mounter()
 *	aggressively perform ramfs mounts, this can force out of memory
 *	conditions
 */
static void stress_umount_mounter(stress_args_t *args, const char *path)
{
	const uint64_t ramfs_size = 64 * KB;
	int i = 0, rc = EXIT_SUCCESS;

	stress_parent_died_alarm();
	(void)sched_settings_apply(true);

	do {
		int ret;
		char opt[32];
		const char *fs = (i++ & 1) ? "ramfs" : "tmpfs";

		(void)snprintf(opt, sizeof(opt), "size=%" PRIu64, ramfs_size);
		ret = mount("", path, fs, 0, opt);
		if (ret < 0) {
			if (errno == EPERM) {
				static bool warned = false;

				if (UNLIKELY(!warned)) {
					warned = true;
					pr_inf_skip("%s: mount failed, no permission, "
						"skipping stressor\n", args->name);
				}
				rc = EXIT_NO_RESOURCE;
			} else if (UNLIKELY((errno != ENOSPC) &&
					    (errno != ENOMEM) &&
					    (errno != ENODEV))) {
				pr_fail("%s: mount failed, errno=%d (%s)\n",
					args->name, errno, strerror(errno));
				rc = EXIT_FAILURE;
			}
			/* Just in case, force umount */
			goto cleanup;
		} else {
			stress_bogo_inc(args);
		}
		(void)stress_umount_umount(args, path, 1000000);
	} while (stress_continue(args));

cleanup:
	(void)stress_umount_umount(args, path, 100000000);
	_exit(rc);
}

/*
 *  stress_umount_spawn()
 *	spawn off child processes
 */
static pid_t stress_umount_spawn(
	stress_args_t *args,
	const char *path,
	void (*func)(stress_args_t *args, const char *path),
	stress_pid_t **s_pid_head,
	stress_pid_t *s_pid)
{
again:
	s_pid->pid = fork();
	if (s_pid->pid < 0) {
		if (stress_redo_fork(args, errno))
			goto again;
		if (UNLIKELY(!stress_continue(args)))
			return 0;
		pr_inf("%s: fork failed, errno=%d (%s), skipping stressor\n",
			args->name, errno, strerror(errno));
		return -1;
	} else if (s_pid->pid == 0) {
		s_pid->pid = getpid();

		stress_parent_died_alarm();
		(void)sched_settings_apply(true);

		stress_sync_start_wait_s_pid(s_pid);

		stress_set_proc_state(args->name, STRESS_STATE_RUN);
		func(args, path);
		stress_set_proc_state(args->name, STRESS_STATE_WAIT);

		_exit(EXIT_SUCCESS);
	} else {
		stress_sync_start_s_pid_list_add(s_pid_head, s_pid);
	}
	return s_pid->pid;
}


/*
 *  stress_umount()
 *      stress unmounting
 */
static int stress_umount(stress_args_t *args)
{
	stress_pid_t *s_pids, *s_pids_head = NULL;
	int ret = EXIT_NO_RESOURCE;
	char pathname[PATH_MAX], realpathname[PATH_MAX];

	s_pids = stress_sync_s_pids_mmap(STRESS_UMOUNT_PROCS);
	if (s_pids == MAP_FAILED) {
		pr_inf_skip("%s: failed to mmap %d PIDs%s, skipping stressor\n",
			args->name, STRESS_UMOUNT_PROCS, stress_get_memfree_str());
		return EXIT_NO_RESOURCE;
	}

	stress_sync_start_init(&s_pids[0]);
	stress_sync_start_init(&s_pids[1]);
	stress_sync_start_init(&s_pids[2]);

	if (stress_sigchld_set_handler(args) < 0) {
		(void)stress_sync_s_pids_munmap(s_pids, STRESS_UMOUNT_PROCS);
		return EXIT_NO_RESOURCE;
	}

	stress_temp_dir(pathname, sizeof(pathname), args->name,
		args->pid, args->instance);
	if (mkdir(pathname, S_IRGRP | S_IWGRP) < 0) {
		pr_fail("%s: cannot mkdir %s, errno=%d (%s)\n",
			args->name, pathname, errno, strerror(errno));
		(void)stress_sync_s_pids_munmap(s_pids, STRESS_UMOUNT_PROCS);
		return EXIT_FAILURE;
	}
	if (!realpath(pathname, realpathname)) {
		pr_fail("%s: cannot realpath %s, errno=%d (%s)\n",
			args->name, pathname, errno, strerror(errno));
		(void)stress_temp_dir_rm_args(args);
		(void)stress_sync_s_pids_munmap(s_pids, STRESS_UMOUNT_PROCS);
		return EXIT_FAILURE;
	}

	if (stress_umount_spawn(args, realpathname, stress_umount_mounter, &s_pids_head, &s_pids[0]) < 0)
		goto reap;
	if (stress_umount_spawn(args, realpathname, stress_umount_umounter, &s_pids_head, &s_pids[1]) < 0)
		goto reap;
	if (stress_umount_spawn(args, realpathname, stress_umount_read_proc_mounts, &s_pids_head, &s_pids[2]) < 0)
		goto reap;

	stress_set_proc_state(args->name, STRESS_STATE_SYNC_WAIT);
	stress_sync_start_wait(args);
	stress_sync_start_cont_list(s_pids_head);
	stress_set_proc_state(args->name, STRESS_STATE_RUN);

	/* Wait for SIGALARMs */
	do {
		(void)shim_pause();
	} while (stress_continue(args));

	ret = EXIT_SUCCESS;
reap:
	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);
	stress_kill_and_wait_many(args, s_pids, STRESS_UMOUNT_PROCS, SIGALRM, true);
	(void)stress_temp_dir_rm_args(args);
	(void)stress_sync_s_pids_munmap(s_pids, STRESS_UMOUNT_PROCS);

	return ret;
}

const stressor_info_t stress_umount_info = {
	.stressor = stress_umount,
	.classifier = CLASS_OS,
	.supported = stress_umount_supported,
	.verify = VERIFY_ALWAYS,
	.help = help
};
#else
const stressor_info_t stress_umount_info = {
	.stressor = stress_unimplemented,
	.classifier = CLASS_OS,
	.supported = stress_umount_supported,
	.verify = VERIFY_ALWAYS,
	.help = help,
	.unimplemented_reason = "only supported on Linux"
};
#endif
