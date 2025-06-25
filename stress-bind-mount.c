/*
 * Copyright (C) 2013-2021 Canonical, Ltd.
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
#include "core-pthread.h"

#if defined(HAVE_SYS_MOUNT_H)
#include <sys/mount.h>
#endif

static const stress_help_t help[] = {
	{ NULL,	"bind-mount N",	    "start N workers exercising bind mounts" },
	{ NULL,	"bind-mount-ops N", "stop after N bogo bind mount operations" },
	{ NULL,	NULL,		    NULL }
};

#if defined(__linux__) && \
    defined(MS_BIND) && \
    defined(MS_REC) && \
    defined(HAVE_CLONE) && \
    defined(CLONE_NEWUSER) && \
    defined(CLONE_NEWNS) && \
    defined(CLONE_VM)

#define CLONE_STACK_SIZE	(128*1024)

static void MLOCKED_TEXT stress_bind_mount_child_handler(int signum)
{
	if (signum == SIGALRM) {
		stress_continue_set_flag(false);
		return;
	}
	_exit(0);
}

/*
 *  stress_bind_mount_child()
 *	aggressively perform bind mounts, this can force out of memory
 *	situations
 */
static int stress_bind_mount_child(void *parg)
{
	const stress_pthread_args_t *pargs = (stress_pthread_args_t *)parg;
	stress_args_t *args = pargs->args;
	const char *path = (const char *)pargs->data;
	double mount_duration = 0.0, umount_duration = 0.0;
	double mount_count = 0.0, umount_count = 0.0;
	double rate;

	if (stress_sighandler(args->name, SIGALRM,
				stress_bind_mount_child_handler, NULL) < 0) {
		pr_fail("%s: SIGALRM sighandler failed, errno=%d (%s)\n",
			args->name, errno, strerror(errno));
		return EXIT_FAILURE;
	}
	if (stress_sighandler(args->name, SIGSEGV,
				stress_bind_mount_child_handler, NULL) < 0) {
		pr_fail("%s: SIGSEGV sighandler failed, errno=%d (%s)\n",
			args->name, errno, strerror(errno));
		return EXIT_FAILURE;
	}
	stress_parent_died_alarm();

	do {
		int rc, retries, stat_count, stat_okay;
		DIR *dir;
		const struct dirent *d;
		double t;

		t = stress_time_now();
		rc = mount("/", path, "", MS_BIND | MS_REC | MS_RDONLY, 0);
		if (rc < 0) {
			if ((errno == EACCES) || (errno == ENOENT)) {
				pr_inf_skip("%s: bind mount failed, skipping stressor\n", args->name);
				(void)shim_rmdir(path);
				return EXIT_NO_RESOURCE;
			}
			if (errno != ENOSPC)
				pr_fail("%s: bind mount failed, errno=%d (%s)\n",
					args->name, errno, strerror(errno));
			break;
		}
		mount_duration += stress_time_now() - t;
		mount_count += 1.0;

		/*
		 *  Check if we can stat the files in the bound mount path
		 */
		dir = opendir("/");
		if (!dir)
			goto bind_umount;

		stat_count = 0;
		stat_okay = 0;

		while ((d = readdir(dir)) != NULL) {
			char bindpath[PATH_MAX + sizeof(d->d_name) + 1];
			const char *name = d->d_name;
			struct stat statbuf;

			if (*name == '.')
				continue;

			stat_count++;
			(void)snprintf(bindpath, sizeof(bindpath), "%s/%s", path, name);
			/*
			 *  Note that not all files may succeed on being stat'd on
			 *  some systems
			 */
			rc = shim_stat(bindpath, &statbuf);
			if (rc == 0)
				stat_okay++;
		}
		/*
		 *  More than one file in directory and all failed to stat..? then
		 *  this looks like multiple failures, so report it
		 */
		if ((stat_okay == 0) && (stat_count > 0))
			pr_fail("%s: failed to stat %d bind mounted files\n",
				args->name, stat_count - stat_okay);
		(void)closedir(dir);

bind_umount:
		for (retries = 0; retries < 15; retries++) {
#if defined(HAVE_UMOUNT2) &&	\
    defined(MNT_DETACH)
			t = stress_time_now();
			rc = umount2(path, MNT_DETACH);
			if (rc == 0) {
				umount_duration += stress_time_now() - t;
				umount_count += 1.0;
				break;
			}
			(void)shim_usleep(50000);
#else
			/*
			 * The following fails with -EBUSY, but try it anyhow
			 *  just to make the kernel work harder
			 */
			t = stress_time_now();
			rc = umount(path);
			if (rc == 0) {
				umount_duration += stress_time_now() - t;
				umount_count += 1.0;
				break;
			}
#endif
		}
		stress_bogo_inc(args);
	} while (stress_continue(args));

	rate = (mount_count > 0.0) ? (double)mount_duration / mount_count : 0.0;
	stress_metrics_set(args, 0, "microsecs per mount",
		rate * STRESS_DBL_MICROSECOND, STRESS_METRIC_HARMONIC_MEAN);
	rate = (umount_count > 0.0) ? (double)umount_duration / umount_count : 0.0;
	stress_metrics_set(args, 1, "microsecs per umount",
		rate * STRESS_DBL_MICROSECOND, STRESS_METRIC_HARMONIC_MEAN);

	/* Remove path in child process just in case parent fails to reap it */
	(void)shim_rmdir(path);
	return EXIT_SUCCESS;
}

/*
 *  stress_bind_mount()
 *      stress bind mounting
 */
static int stress_bind_mount(stress_args_t *args)
{
	int status, ret, rc = EXIT_SUCCESS;
	char path[PATH_MAX];
	stress_pthread_args_t pargs = { args, path, 0 };

	stress_set_proc_state(args->name, STRESS_STATE_SYNC_WAIT);
	stress_sync_start_wait(args);
	stress_set_proc_state(args->name, STRESS_STATE_RUN);

	(void)stress_temp_dir(path, sizeof(path), args->name, getpid(), args->instance);
	ret = mkdir(path, S_IRUSR | S_IRGRP | S_IWGRP);
	if (ret < 0) {
		(void)shim_rmdir(path);
		pr_err("%s: mkdir %s failed, errno=%d (%s)\n",
			args->name, path, errno, strerror(errno));
		return EXIT_NO_RESOURCE;
	}

	do {
		pid_t pid;
		static char stack[CLONE_STACK_SIZE];
		char *const stack_top = (char *)stress_get_stack_top((void *)stack, CLONE_STACK_SIZE);

		(void)shim_memset(stack, 0, sizeof stack);

		pid = (pid_t)clone(stress_bind_mount_child,
			stress_align_stack(stack_top),
			CLONE_NEWUSER | CLONE_NEWNS | CLONE_VM | SIGCHLD,
			(void *)&pargs, 0);
		if (pid < 0) {
			switch (errno) {
			case ENOMEM:
			case ENOSPC:
			case EPERM:
				return EXIT_NO_RESOURCE;
			case ENOSYS:
				return EXIT_NOT_IMPLEMENTED;
			default:
				break;
			}
			pr_fail("%s: clone failed, errno=%d (%s)\n",
				args->name, errno, strerror(errno));
			return EXIT_FAILURE;
		}
		if (shim_waitpid(pid, &status, 0) < 0) {
			pr_inf("%s: waitpid on PID %" PRIdMAX " failed, errno=%d (%s)\n",
				args->name, (intmax_t)pid, errno, strerror(errno));
			break;
		}
		if (WIFEXITED(status)) {
			rc = WEXITSTATUS(status);
			if (rc != EXIT_SUCCESS)
				break;
		} else if (WIFSIGNALED(status)) {
			break;
		}
	} while (stress_continue(args));

	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);

	(void)shim_rmdir(path);

	return rc;
}

const stressor_info_t stress_bind_mount_info = {
	.stressor = stress_bind_mount,
	.classifier = CLASS_FILESYSTEM | CLASS_OS | CLASS_PATHOLOGICAL,
	.verify = VERIFY_ALWAYS,
	.help = help
};
#else
const stressor_info_t stress_bind_mount_info = {
	.stressor = stress_unimplemented,
	.classifier = CLASS_FILESYSTEM | CLASS_OS | CLASS_PATHOLOGICAL,
	.verify = VERIFY_ALWAYS,
	.help = help,
	.unimplemented_reason = "built without Linux bind-mount options MS_BIND"
};
#endif
