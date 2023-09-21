// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2013-2021 Canonical, Ltd.
 * Copyright (C) 2022-2023 Colin Ian King.
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
	stress_pthread_args_t *pargs = (stress_pthread_args_t *)parg;
	const stress_args_t *args = pargs->args;
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
		int rc, retries;
		DIR *dir;
		struct dirent *d;
		double t;

		t = stress_time_now();
		rc = mount("/", path, "", MS_BIND | MS_REC | MS_RDONLY, 0);
		if (rc < 0) {
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
		while ((d = readdir(dir)) != NULL) {
			char bindpath[PATH_MAX + sizeof(d->d_name) + 1];
			const char *name = d->d_name;
			struct stat statbuf;

			if (*name == '.')
				continue;

			(void)snprintf(bindpath, sizeof(bindpath), "%s/%s", path, name);
			rc = stat(bindpath, &statbuf);
			if (rc < 0) {
				pr_fail("%s: failed to stat bind mounted file %s, errno=%d (%s)\n",
					args->name, bindpath, errno, strerror(errno));
			}
		}
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
	} while (stress_continue_flag() &&
		 (!args->max_ops || (stress_bogo_get(args) < args->max_ops)));

	rate = (mount_count > 0.0) ? (double)mount_duration / mount_count : 0.0;
	stress_metrics_set(args, 0, "microsecs per mount", rate * STRESS_DBL_MICROSECOND);
	rate = (umount_count > 0.0) ? (double)umount_duration / umount_count : 0.0;
	stress_metrics_set(args, 1, "microsecs per umount", rate * STRESS_DBL_MICROSECOND);

	/* Remove path in child process just in case parent fails to reap it */
	(void)shim_rmdir(path);
	return 0;
}

/*
 *  stress_bind_mount()
 *      stress bind mounting
 */
static int stress_bind_mount(const stress_args_t *args)
{
	int status, ret;
	char path[PATH_MAX];
	stress_pthread_args_t pargs = { args, path, 0 };

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
		char *stack_top = (char *)stress_get_stack_top((void *)stack, CLONE_STACK_SIZE);

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
		VOID_RET(int, shim_waitpid(pid, &status, 0));
	} while (stress_continue(args));

	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);

	(void)shim_rmdir(path);

	return EXIT_SUCCESS;
}

stressor_info_t stress_bind_mount_info = {
	.stressor = stress_bind_mount,
	.class = CLASS_FILESYSTEM | CLASS_OS | CLASS_PATHOLOGICAL,
	.verify = VERIFY_ALWAYS,
	.help = help
};
#else
stressor_info_t stress_bind_mount_info = {
	.stressor = stress_unimplemented,
	.class = CLASS_FILESYSTEM | CLASS_OS | CLASS_PATHOLOGICAL,
	.verify = VERIFY_ALWAYS,
	.help = help,
	.unimplemented_reason = "built without Linux bind-mount options MS_BIND"
};
#endif
