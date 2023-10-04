/*
 * Copyright (C) 2023      Colin Ian King.
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

#if defined(HAVE_SYS_MOUNT_H)
#include <sys/mount.h>
#endif

static const stress_help_t help[] = {
	{ NULL,	"cgroup N",	 "start N workers exercising cgroup mount/read/write/umounts" },
	{ NULL,	"cgroup-ops N",	 "stop after N iterations of cgroup actions" },
	{ NULL,	NULL,		 NULL }
};

/*
 *  stress_cgroup_supported()
 *      check if we can run this with SHIM_CAP_SYS_ADMIN capability
 */
static int stress_cgroup_supported(const char *name)
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

typedef struct {
	const char *name;
	const char *value;
} stress_cgroup_values_t;

static void stress_cgroup_remove_nl(char *str)
{
	char *ptr;

	ptr = strchr(str, '\n');
	if (ptr)
		*ptr = '\0';
}

/*
 *  stress_cgroup_umount()
 *	umount a path with retries.
 */
static void stress_cgroup_umount(const stress_args_t *args, const char *path)
{
	int i;
	int ret;
	static const uint64_t ns = 100000000;	/* 1/10th second */

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
			return;
		default:
			/* Unexpected, so report it */
			pr_inf("%s: umount failed %s: %d %s\n", args->name,
				path, errno, strerror(errno));
			break;
		}
	}
}

static void stress_cgroup_read(const char *path)
{
	int fd;
	char buf[1024];
	ssize_t ret;

	fd = open(path, O_RDONLY);
	if (fd < 0)
		return;
	do {
		ret = read(fd, buf, sizeof(buf));
	} while (ret > 0);
	(void)close(fd);
}

static void stress_cgroup_controllers(const char *realpathname)
{
	char path[PATH_MAX + 32];
	char controllers[512];
	char *ptr, *token;
	ssize_t ret;

	(void)snprintf(path, sizeof(path), "%s/%s", realpathname, "cgroup.subtree_control");
	ret = stress_system_read(path, controllers, sizeof(controllers));
	if (ret < 0)
		return;

	stress_cgroup_remove_nl(controllers);
	(void)snprintf(path, sizeof(path), "%s/%s", realpathname, "cgroup.subtree_control");

	/* Add existing controllers to already set subtree control, should be OK */
	for (ptr = controllers; (token = strtok(ptr, " ")) != NULL; ptr = NULL) {
		char controller[256];

		ret = (ssize_t)snprintf(controller, sizeof(controller), "+%s\n", token);
		stress_system_write(path, controller, ret);
	}
}

static void stress_cgroup_read_files(const char *realpathname)
{
	static const char *filenames[] = {
		"cgroup.type",
		"cgroup.procs",
		"cgroup.threads",
		"cgroup.controllers",
		"cgroup.subtree_control",
		"cgroup.events",
		"cgroup.max.descendants",
		"cgroup.max.depth",
		"cgroup.stat",
		"cgroup.freeze",
		"cgroup.kill",
		"cgroup.pressure",
		"irq.pressure",
	};

	size_t i;

	for (i = 0; i < SIZEOF_ARRAY(filenames); i++) {
		char path[PATH_MAX + 32];

		(void)snprintf(path, sizeof(path), "%s/%s", realpathname, filenames[i]);
		stress_cgroup_read(path);
	}
}

static void stress_cgroup_new_group(const char *realpathname, const pid_t pid)
{
	char path[PATH_MAX + 64], filename[PATH_MAX + 64], cmd[64];
	ssize_t len;
	size_t i;

	stress_cgroup_values_t values[] = {
		{ "cpu.stat",			NULL },
		{ "cpu.weight",			"90" },
		{ "cpu.weight.nice",		"-1" },
		{ "cpu.max",			NULL },
		{ "cpu.max.burst",		"0" },
		{ "cpu.pressure",		NULL },
		{ "cpu.uclamp.min",		"10.0" },
		{ "cpu.uclamp.max",		"90.0" },
		{ "memory.current",		NULL },
		{ "memory.min",			"0" },
		{ "memory.low",			"0" },
		{ "memory.high",		NULL },
		{ "memory.max",			NULL },
		{ "memory.reclaim",		"1M" },
		{ "memory.peak",		NULL },
		{ "memory.oom.group",		NULL },
		{ "memory.events",		NULL },
		{ "memory.events.local",	NULL },
		{ "memory.stat",		NULL },
		{ "memory.numa_stat",		NULL },
		{ "memory.swap.current",	NULL },
		{ "memory.swap.peak",		NULL },
		{ "memory.swap.max",		NULL },
		{ "memory.swap.events",		NULL },
		{ "memory.zswap.current",	NULL },
		{ "memory.zswap.max",		NULL },
		{ "memory.pressure",		NULL },
		{ "io.stat",			NULL },
		{ "io.cost.qos",		NULL },
		{ "io.cost.model",		NULL },
		{ "io.weight",			"default 90" },
		{ "io.max",			NULL },
		{ "io.pressure",		NULL },
		{ "io.latency",			NULL },
		{ "io.stat",			NULL },
		{ "pids.max",			"10000" },
		{ "pids.current",		NULL },
		{ "cpuset.cpus",		NULL },
		{ "cpuset.cpus.effective",	NULL },
		{ "cpuset.mems",		NULL },
		{ "cpuset.mems.effective",	NULL },
		{ "cpuset.cpus.partition",	NULL },
		{ "rdma.max",			NULL },
		{ "rdma.current",		NULL },
		{ "hugetlb.1GB.current",	NULL },
		{ "hugetlb.1GB.events",		NULL },
		{ "hugetlb.1GB.events.local",	NULL },
		{ "hugetlb.1GB.max",		NULL },
		{ "hugetlb.1GB.numa_stat",	NULL },
		{ "hugetlb.1GB.rsvd.current",	NULL },
		{ "hugetlb.1GB.rsvd.max",	NULL },
		{ "hugetlb.2GB.current",	NULL },
		{ "hugetlb.2GB.events",		NULL },
		{ "hugetlb.2GB.events.local",	NULL },
		{ "hugetlb.2GB.max",		NULL },
		{ "hugetlb.2GB.numa_stat",	NULL },
		{ "hugetlb.2GB.rsvd.current",	NULL },
		{ "hugetlb.2GB.rsvd.max",	NULL },
		{ "misc.capacity",		NULL },
		{ "misc.current",		NULL },
		{ "misc.max",			NULL },
		{ "misc.events",		NULL },
	};

	(void)snprintf(path, sizeof(path), "%s/stress-ng-%jd", realpathname, (intmax_t)pid);
	if (mkdir(path, S_IRUSR | S_IWUSR | S_IRUSR | S_IWUSR) < 0) {
		(void)rmdir(path);	/* just in case */
		return;
	}
	len = (ssize_t)snprintf(cmd, sizeof(cmd), "%jd\n", (intmax_t)pid);
	(void)snprintf(filename, sizeof(filename), "%s/stress-ng-%jd/cgroup.procs", realpathname, (intmax_t)pid);
	stress_system_write(filename, cmd, len);

	for (i = 0; i < SIZEOF_ARRAY(values); i++) {
		(void)snprintf(filename, sizeof(filename), "%s/stress-ng-%jd/%s", realpathname, (intmax_t)pid, values[i].name);
		stress_cgroup_read(filename);

		if (values[i].value) {
			stress_system_write(filename, values[i].value, strlen(values[i].value));
			stress_cgroup_read(filename);
		}
	}

	len = (ssize_t)snprintf(cmd, sizeof(cmd), "%jd\n", (intmax_t)pid);
	(void)snprintf(filename, sizeof(filename), "%s/cgroup.procs", realpathname);
	stress_system_write(filename, cmd, len);

	(void)rmdir(path);
}

/*
 *  stress_cgroup_child()
 *	aggressively perform cgroup mounts, this can force out of memory
 *	situations
 */
static int stress_cgroup_child(const stress_args_t *args)
{
	char pathname[PATH_MAX], realpathname[PATH_MAX];
	int rc = EXIT_SUCCESS;
	pid_t pid = getpid();

	stress_parent_died_alarm();
	(void)sched_settings_apply(true);

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
		int ret;

		ret = mount("none", realpathname, "cgroup2", 0, NULL);
		if (ret < 0) {
			if ((errno != ENOSPC) &&
			    (errno != ENOMEM) &&
			    (errno != ENODEV))
				pr_fail("%s: mount failed, errno=%d (%s)\n",
					args->name, errno, strerror(errno));
			/* Just in case, force umount */
			goto cleanup;
		}

		stress_cgroup_controllers(realpathname);
		stress_cgroup_read_files(realpathname);
		stress_cgroup_new_group(realpathname, pid);
		stress_cgroup_umount(args, realpathname);
		stress_bogo_inc(args);
	} while (stress_continue(args));

cleanup:
	stress_cgroup_umount(args, realpathname);
	(void)stress_temp_dir_rm_args(args);

	return rc;
}

/*
 *  stress_cgroup_mount()
 *      stress cgroup mounting
 */
static int stress_cgroup_mount(const stress_args_t *args)
{
	int pid;

	stress_set_proc_state(args->name, STRESS_STATE_RUN);

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
			_exit(stress_cgroup_child(args));
		}
	} while (stress_continue(args));

finish:
	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);

	return EXIT_SUCCESS;
}

stressor_info_t stress_cgroup_info = {
	.stressor = stress_cgroup_mount,
	.class = CLASS_OS,
	.supported = stress_cgroup_supported,
	.verify = VERIFY_ALWAYS,
	.help = help
};
#else
stressor_info_t stress_cgroup_info = {
	.stressor = stress_unimplemented,
	.class = CLASS_OS,
	.supported = stress_cgroup_supported,
	.verify = VERIFY_ALWAYS,
	.help = help,
	.unimplemented_reason = "built without clone() or only supported on Linux"
};
#endif
