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

#if defined(__linux__) &&	\
    defined(HAVE_SYS_MOUNT_H)

#define STRESS_CGROUP_MOUNTED	(1)
#define STRESS_CGROUP_UNMOUNTED	(2)
#define STRESS_CGROUP_UNKNOWN	(3)

typedef struct {
	const char *name;
	const char *value;
} stress_cgroup_values_t;

/*
 *  stress_cgroup_remove_nl()
 *	remove newline at end of string
 */
static inline void stress_cgroup_remove_nl(char *str)
{
	char *ptr;

	ptr = strchr(str, '\n');
	if (ptr)
		*ptr = '\0';
}

/*
 *  stress_cgroup_mounted_state()
 *	quick sanity check to see if path is mounted
 */
static int stress_cgroup_mounted_state(const char *path)
{
	FILE *fp;
	char buf[4096];
	int ret = STRESS_CGROUP_UNMOUNTED;

	fp = fopen("/proc/mounts", "r");
	if (!fp)
		return STRESS_CGROUP_UNKNOWN;

	while (fgets(buf, sizeof(buf), fp)) {
		const char *mnt, *type;
		char *ptr;

		ptr = buf;
		while (*ptr && (*ptr != ' '))
			ptr++;
		if (*ptr == '\0')
			break;
		*ptr = '\0';
		ptr++;
		mnt = ptr;

		while (*ptr && (*ptr != ' '))
			ptr++;
		if (*ptr == '\0')
			break;
		*ptr = '\0';
		ptr++;
		type = ptr;
		while (*ptr && (*ptr != ' '))
			ptr++;
		if (*ptr == '\0')
			break;
		*ptr = '\0';

		if ((strcmp(type, "cgroup2") == 0) &&
		    (strcmp(mnt, path) == 0)) {
			ret = STRESS_CGROUP_MOUNTED;
			break;
		}
	}
	(void)fclose(fp);
	return ret;
}

/*
 *  stress_group_sleep()
 *	small delay
 */
static void stress_group_sleep(uint64_t *counter)
{
	const uint64_t ns = stress_mwc64modn(100000000) + 50000000;

	(void)shim_nanosleep_uint64(ns);
	(*counter)++;
}

/*
 *  stress_cgroup_umount()
 *	umount a path with retries.
 */
static void stress_cgroup_umount(
	stress_args_t *args,
	const char *path,
	uint64_t *umount_retry)
{
	int i;
	int ret;

	/*
	 *  umount is attempted at least twice, the first successful mount
	 *  and then a retry. In theory the EINVAL should be returned
	 *  on a umount of a path that has already been umounted, so we
	 *  know that umount been successful and can then return.
	 */
	for (i = 0; i < 128; i++) {
		if (stress_cgroup_mounted_state(path) == STRESS_CGROUP_UNMOUNTED)
			return;

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
			if (i > 1)
				stress_group_sleep(umount_retry);
			continue;
		}

		switch (errno) {
		case EAGAIN:
		case EBUSY:
		case ENOMEM:
			/* Wait and then re-try */
			stress_group_sleep(umount_retry);
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
			pr_inf("%s: umount failed %s, errno=%d %s\n", args->name,
				path, errno, strerror(errno));
			break;
		}
	}
}

/*
 *  stress_cgroup_read()
 *	read/seek exercise of group file
 */
static void stress_cgroup_read(const char *path)
{
	int fd, i;
	char buf[1024];
	off_t len = 0;
	struct stat statbuf;

	fd = open(path, O_RDONLY);
	if (fd < 0)
		return;

	VOID_RET(int, shim_fstat(fd, &statbuf));

	for (;;) {
		ssize_t ret;

		ret = read(fd, buf, sizeof(buf));
		if (ret > 0)
			len += (off_t)ret;
		else
			break;
	}
	/* Add in a couple of random seek/reads for good measure */
	for (i = 0; (i < 2) && (i < len); i++) {
		const off_t offset = (off_t)stress_mwc32modn((uint32_t)len);

		if (lseek(fd, offset, SEEK_SET) >= 0)
			VOID_RET(int, read(fd, buf, sizeof(buf)));
	}

	(void)close(fd);
}

static void stress_cgroup_controllers(const char *realpathname)
{
	char path[PATH_MAX + 32];
	char controllers[512];
	const char *token;
	char *ptr;
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

/*
 *  stress_cgroup_read_files()
 *	read cgroup files
 */
static void stress_cgroup_read_files(const char *realpathname)
{
	static const char * const filenames[] = {
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

/*
 *  stress_cgroup_add_pid()
 *	add a PID to a process group
 */
static void stress_cgroup_add_pid(const char *realpathname, const pid_t pid)
{
	char filename[PATH_MAX + 64], cmd[64];
	ssize_t len;

	len = (ssize_t)snprintf(cmd, sizeof(cmd), "%" PRIdMAX "\n", (intmax_t)pid);
	(void)snprintf(filename, sizeof(filename), "%s/stress-ng-%" PRIdMAX "/cgroup.procs", realpathname, (intmax_t)pid);
	stress_system_write(filename, cmd, len);
}

/*
 *  stress_cgroup_del_pid()
 *	delete a PID from a process group
 */
static void stress_cgroup_del_pid(const char *realpathname, const pid_t pid)
{
	char filename[PATH_MAX + 64], cmd[64];
	ssize_t len;

	len = (ssize_t)snprintf(cmd, sizeof(cmd), "%" PRIdMAX "\n", (intmax_t)pid);
	(void)snprintf(filename, sizeof(filename), "%s/cgroup.procs", realpathname);
	stress_system_write(filename, cmd, len);
}

/*
 *  stress_cgroup_new_group()
 *	add a new groip
 */
static void stress_cgroup_new_group(stress_args_t *args, const char *realpathname)
{
	pid_t pid;

	pid = fork();
	if (pid == 0) {
		/* Child, perform some activity */
		stress_set_proc_state(args->name, STRESS_STATE_RUN);
		do {
			void *ptr;
			const size_t sz = MB;

			ptr = mmap(NULL, sz, PROT_READ | PROT_WRITE,
					MAP_ANONYMOUS | MAP_SHARED, -1, 0);
			(void)shim_sched_yield();
			if (ptr != MAP_FAILED)
				(void)munmap(ptr, sz);
			(void)shim_sched_yield();
		} while (stress_continue_flag());
		_exit(0);
	} else {
		int status;
		size_t i;
		char path[PATH_MAX + 64];

		static const stress_cgroup_values_t values[] = {
			{ "cpu.stat",			NULL },
			{ "cpu.stat.local",		NULL },
			{ "cpu.weight",			"90" },
			{ "cpu.weight.nice",		"-4" },
			{ "cpu.max",			NULL },
			{ "cpu.max.burst",		"50" },
			{ "cpu.pressure",		NULL },
			{ "cpu.uclamp.min",		"10.0" },
			{ "cpu.uclamp.max",		"95.0" },
			{ "cpu.idle",			"1" },
			{ "cpu.idle",			"0" },
			{ "memory.current",		NULL },
			{ "memory.min",			"1M" },
			{ "memory.low",			"2M" },
			{ "memory.high",		"32M" },
			{ "memory.max",			"128M" },
			{ "memory.reclaim",		"2M" },
			{ "memory.peak",		NULL },
			{ "memory.oom.group",		NULL },
			{ "memory.events",		NULL },
			{ "memory.events.local",	NULL },
			{ "memory.stat",		NULL },
			{ "memory.numa_stat",		NULL },
			{ "memory.swap.current",	NULL },
			{ "memory.swap.high",		NULL },
			{ "memory.swap.peak",		NULL },
			{ "memory.swap.max",		NULL },
			{ "memory.swap.events",		NULL },
			{ "memory.zswap.current",	NULL },
			{ "memory.zswap.max",		NULL },
			{ "memory.zswap.writeback",	"0" },
			{ "memory.zswap.writeback",	"1" },
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
			{ "pids.peak",			NULL },
			{ "pids.events",		NULL },
			{ "pids.events.local",		NULL },
			{ "cpuset.cpus",		"0" },	/* force child to cpu 0 */
			{ "cpuset.cpus.effective",	NULL },
			{ "cpuset.mems",		"0" },	/* force child to mem 0 */
			{ "cpuset.mems.effective",	NULL },
			{ "cpuset.cpus.exclusive",	NULL },
			{ "cpuset.cpus.exclusive.effective",	NULL },
			{ "cpuset.cpus.isolated",	NULL },
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
			{ "misc.peak",			NULL },
			{ "misc.max",			NULL },
			{ "misc.events",		NULL },
			{ "misc.events.local",		NULL },
			{ "cgroup.type",		NULL },
			{ "cgroup.procs",		NULL },
			{ "cgroup.threads",		NULL },
			{ "cgroup.controllers",		NULL },
			{ "cgroup.subtree_control",	NULL },
			{ "cgroup.events",		NULL },
			{ "cgroup.max.descendants",	NULL },
			{ "cgroup.max.depth",		NULL },
			{ "cgroup.stat",		NULL },
			{ "cgroup.pressure",		NULL },
			{ "cgroup.freeze",		"0" },	/* freeze child */
			{ "cgroup.freeze",		"1" },	/* unfreeze child */
		};

		/* Parent, exercise child in the cgroup */
		(void)snprintf(path, sizeof(path), "%s/stress-ng-%" PRIdMAX, realpathname, (intmax_t)pid);
		if (mkdir(path, S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP) < 0) {
			stress_kill_pid_wait(pid, &status);
			(void)rmdir(path);	/* just in case */
			return;
		}

		/*
		 *  Keep moving pid to/from cgroup while read and adjusting cgroup values
		 */
		for (i = 0; i < SIZEOF_ARRAY(values); i++) {
			char filename[PATH_MAX + 64];

			stress_cgroup_add_pid(realpathname, pid);
			(void)snprintf(filename, sizeof(filename), "%s/stress-ng-%" PRIdMAX "/%s", realpathname, (intmax_t)pid, values[i].name);
			stress_cgroup_read(filename);

			if (values[i].value) {
				(void)stress_system_write(filename, values[i].value, strlen(values[i].value));
				stress_cgroup_read(filename);
			}
			stress_cgroup_del_pid(realpathname, pid);
		}
		stress_kill_pid_wait(pid, &status);
		(void)rmdir(path);
	}
}

/*
 *  stress_cgroup_child()
 *	aggressively perform cgroup mounts, this can force out of memory
 *	situations
 */
static int stress_cgroup_child(stress_args_t *args)
{
	char pathname[PATH_MAX], realpathname[PATH_MAX];
	int rc = EXIT_SUCCESS;
	uint64_t mount_retry = 0, umount_retry = 0;

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
			if (errno == EBUSY) {
				/* Wait and retry */
				stress_group_sleep(&mount_retry);
				continue;
			} else if (errno == EPERM) {
				pr_inf_skip("%s: mount failed, no permission, skipping stressor\n",
					args->name);
				rc = EXIT_NO_RESOURCE;
				goto cleanup;
			} else if ((errno != ENOSPC) &&
				   (errno != ENOMEM) &&
				   (errno != ENODEV)) {
				pr_fail("%s: mount failed, errno=%d (%s)\n",
					args->name, errno, strerror(errno));
				rc = EXIT_FAILURE;
			}
			/* Just in case, force umount */
			goto cleanup;
		}

		stress_cgroup_controllers(realpathname);
		stress_cgroup_read_files(realpathname);
		stress_cgroup_new_group(args, realpathname);
		stress_cgroup_umount(args, realpathname, &umount_retry);
		stress_bogo_inc(args);
	} while (stress_continue(args));

cleanup:
	stress_cgroup_umount(args, realpathname, &umount_retry);
	if (stress_cgroup_mounted_state(realpathname) == STRESS_CGROUP_MOUNTED)
		pr_dbg("%s: could not unmount of %s\n", args->name, realpathname);
	(void)stress_temp_dir_rm_args(args);
	if ((mount_retry + umount_retry) > 0) {
		pr_dbg("%s: %" PRIu64 " mount retries, %" PRIu64 " umount retries\n",
			args->name, mount_retry, umount_retry);
	}

	return rc;
}

/*
 *  stress_cgroup_mount()
 *      stress cgroup mounting
 */
static int stress_cgroup_mount(stress_args_t *args)
{
	int pid, rc = EXIT_SUCCESS;

	stress_set_proc_state(args->name, STRESS_STATE_SYNC_WAIT);
	stress_sync_start_wait(args);
	stress_set_proc_state(args->name, STRESS_STATE_RUN);

	do {
again:
		if (UNLIKELY(!stress_continue_flag()))
			break;

		pid = fork();
		if (pid < 0) {
			if (stress_redo_fork(args, errno))
				goto again;
			if (UNLIKELY(!stress_continue(args)))
				goto finish;
			pr_err("%s: fork failed, errno=%d (%s)\n",
				args->name, errno, strerror(errno));
		} else if (pid > 0) {
			int status;
			pid_t waitret;

			/* Parent, wait for child */
			waitret = shim_waitpid(pid, &status, 0);
			if (waitret < 0) {
				if (errno != EINTR) {
					pr_dbg("%s: waitpid() on PID %" PRIdMAX " failed, errno=%d (%s)\n",
						args->name, (intmax_t)pid, errno, strerror(errno));
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
				rc = EXIT_FAILURE;
				break;
			} else if (WEXITSTATUS(status) == EXIT_NO_RESOURCE) {
				rc = EXIT_NO_RESOURCE;
				break;
			}
		} else {
			_exit(stress_cgroup_child(args));
		}
	} while (stress_continue(args));

finish:
	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);

	return rc;
}

const stressor_info_t stress_cgroup_info = {
	.stressor = stress_cgroup_mount,
	.classifier = CLASS_OS,
	.supported = stress_cgroup_supported,
	.verify = VERIFY_ALWAYS,
	.help = help
};
#else
const stressor_info_t stress_cgroup_info = {
	.stressor = stress_unimplemented,
	.classifier = CLASS_OS,
	.supported = stress_cgroup_supported,
	.verify = VERIFY_ALWAYS,
	.help = help,
	.unimplemented_reason = "only supported on Linux"
};
#endif
