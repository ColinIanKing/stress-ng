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
	{ NULL,	"handle N",	"start N workers exercising name_to_handle_at" },
	{ NULL,	"handle-ops N",	"stop after N handle bogo operations" },
	{ NULL,	NULL,		NULL }
};

#if defined(HAVE_NAME_TO_HANDLE_AT) &&	\
    defined(HAVE_OPEN_BY_HANDLE_AT) && 	\
    defined(AT_FDCWD)

#define MAX_MOUNT_IDS	(1024)
#define FILENAME	"/dev/zero"

/* Stringification macros */
#define XSTR(s)	STR(s)
#define STR(s) #s

typedef struct {
	char	*mount_path;
	int	mount_id;
} mount_info_t;

static mount_info_t mount_info[MAX_MOUNT_IDS];

static void free_mount_info(const int mounts)
{
	int i;

	for (i = 0; i < mounts; i++)
		free(mount_info[i].mount_path);
}

static int get_mount_info(const args_t *args)
{
	FILE *fp;
	int mounts = 0;

	if ((fp = fopen("/proc/self/mountinfo", "r")) == NULL) {
		pr_dbg("%s: cannot open /proc/self/mountinfo\n", args->name);
		return -1;
	}

	for (;;) {
		char mount_path[PATH_MAX + 1];
		char *line = NULL;
		size_t line_len = 0;

		ssize_t nread = getline(&line, &line_len, fp);
		if (nread == -1) {
			free(line);
			break;
		}

		nread = sscanf(line, "%12d %*d %*s %*s %" XSTR(PATH_MAX) "s",
			&mount_info[mounts].mount_id,
			mount_path);
		free(line);
		if (nread != 2)
			continue;

		mount_info[mounts].mount_path = strdup(mount_path);
		if (mount_info[mounts].mount_path == NULL) {
			pr_dbg("%s: cannot allocate mountinfo mount path\n", args->name);
			free_mount_info(mounts);
			mounts = -1;
			break;
		}
		mounts++;
	}
	(void)fclose(fp);
	return mounts;
}


/*
 *  stress_handle()
 *	stress system by rapid open/close calls via
 *	name_to_handle_at and open_by_handle_at
 */
static int stress_handle(const args_t *args)
{
	int mounts;
	pid_t pid;

	if ((mounts = get_mount_info(args)) < 0) {
		pr_fail("%s: failed to parse /proc/self/mountinfo\n", args->name);
		return EXIT_FAILURE;
	}

again:
	if (!g_keep_stressing_flag)
		goto tidy;
	pid = fork();
	if (pid < 0) {
		if ((errno == EAGAIN) || (errno == ENOMEM))
			goto again;
		pr_err("%s: fork failed: errno=%d: (%s)\n",
			args->name, errno, strerror(errno));
	} else if (pid > 0) {
		int status, ret;

		(void)setpgid(pid, g_pgrp);
		/* Parent, wait for child */
		ret = shim_waitpid(pid, &status, 0);
		if (ret < 0) {
			if (errno != EINTR)
				pr_dbg("%s: waitpid(): errno=%d (%s)\n",
					args->name, errno, strerror(errno));
			(void)kill(pid, SIGTERM);
			(void)kill(pid, SIGKILL);
			(void)shim_waitpid(pid, &status, 0);
		} else if (WIFSIGNALED(status)) {
			pr_dbg("%s: child died: %s (instance %d)\n",
				args->name, stress_strsignal(WTERMSIG(status)),
				args->instance);
			/* If we got killed by OOM killer, re-start */
			if (WTERMSIG(status) == SIGKILL) {
				if (g_opt_flags & OPT_FLAGS_OOMABLE) {
					log_system_mem_info();
					pr_dbg("%s: assuming killed by OOM "
						"killer, bailing out "
						"(instance %d)\n",
						args->name, args->instance);
					_exit(0);
				} else {
					log_system_mem_info();
					pr_dbg("%s: assuming killed by OOM "
						"killer, restarting again "
						"(instance %d)\n",
						args->name, args->instance);
					goto again;
				}
			}
		}
	} else if (pid == 0) {
		(void)setpgid(0, g_pgrp);
		stress_parent_died_alarm();

		/* Make sure this is killable by OOM killer */
		set_oom_adjustment(args->name, true);

		do {
			struct file_handle *fhp, *tmp;
			int mount_id, mount_fd, fd, i;

			if ((fhp = malloc(sizeof(*fhp))) == NULL)
				continue;

			fhp->handle_bytes = 0;
			if ((name_to_handle_at(AT_FDCWD, FILENAME, fhp, &mount_id, 0) != -1) &&
			    (errno != EOVERFLOW)) {
				pr_fail_err("name_to_handle_at: failed to get file handle size");
				free(fhp);
				break;
			}
			tmp = realloc(fhp, sizeof(struct file_handle) + fhp->handle_bytes);
			if (tmp == NULL) {
				free(fhp);
				continue;
			}
			fhp = tmp;
			if (name_to_handle_at(AT_FDCWD, FILENAME, fhp, &mount_id, 0) < 0) {
				pr_fail_err("name_to_handle_at: failed to get file handle");
				free(fhp);
				break;
			}

			mount_fd = -2;
			for (i = 0; i < mounts; i++) {
				if (mount_info[i].mount_id == mount_id) {
					mount_fd = open(mount_info[i].mount_path, O_RDONLY);
					break;
				}
			}
			if (mount_fd == -2) {
				pr_fail("%s: cannot find mount id %d\n", args->name, mount_id);
				free(fhp);
				break;
			}
			if (mount_fd < 0) {
				pr_fail("%s: failed to open mount path '%s': errno=%d (%s)\n",
					args->name, mount_info[i].mount_path, errno, strerror(errno));
				free(fhp);
				break;
			}
			if ((fd = open_by_handle_at(mount_fd, fhp, O_RDONLY)) < 0) {
				/* We don't abort if EPERM occurs, that's not a test failure */
				if (errno != EPERM) {
					pr_fail("%s: open_by_handle_at: failed to open: errno=%d (%s)\n",
						args->name, errno, strerror(errno));
					(void)close(mount_fd);
					free(fhp);
					break;
				}
			} else {
				(void)close(fd);
			}
			(void)close(mount_fd);
			free(fhp);
			inc_counter(args);
		} while (keep_stressing());
		_exit(EXIT_SUCCESS);
	}
tidy:
	free_mount_info(mounts);

	return EXIT_SUCCESS;
}

stressor_info_t stress_handle_info = {
	.stressor = stress_handle,
	.class = CLASS_FILESYSTEM | CLASS_OS,
	.help = help
};
#else
stressor_info_t stress_handle_info = {
	.stressor = stress_not_implemented,
	.class = CLASS_FILESYSTEM | CLASS_OS,
	.help = help
};
#endif
