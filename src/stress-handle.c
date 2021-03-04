/*
 * Copyright (C) 2013-2021 Canonical, Ltd.
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

static const stress_help_t help[] = {
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
	char	*mount_path;	/* mount full path, e.g. /boot */
	int	mount_id;	/* unique mount id */
} stress_mount_info_t;

static stress_mount_info_t mount_info[MAX_MOUNT_IDS];

/*
 *  free_mount_info()
 *	free allocated mount information
 */
static void free_mount_info(const int mounts)
{
	int i;

	for (i = 0; i < mounts; i++)
		free(mount_info[i].mount_path);
}

/*
 *  get_mount_info()
 *	parse mount information from /proc/self/mountinfo
 */
static int get_mount_info(const stress_args_t *args)
{
	FILE *fp;
	int mounts = 0;

	if ((fp = fopen("/proc/self/mountinfo", "r")) == NULL) {
		pr_dbg("%s: cannot open /proc/self/mountinfo\n", args->name);
		return -1;
	}

	(void)memset(&mount_info, 0, sizeof(mount_info));

	while (mounts < MAX_MOUNT_IDS) {
		char mount_path[PATH_MAX + 1];
		char *line = NULL;
		size_t line_len = 0;
		ssize_t nread;

		nread = getline(&line, &line_len, fp);
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
 *  stress_handle_child()
 *	exercise name_to_handle_at inside an oomble child
 *	wrapper because the allocations may cause memory
 *	failures on high memory pressure environments.
 */
static int stress_handle_child(const stress_args_t *args, void *context)
{
	const int mounts = *((int *)context);

	do {
		struct file_handle *fhp, *tmp;
		int mount_id, mount_fd, fd, i;
		char *ptr;

		if ((fhp = malloc(sizeof(*fhp))) == NULL)
			continue;

		fhp->handle_bytes = 0;
		if ((name_to_handle_at(AT_FDCWD, FILENAME, fhp, &mount_id, 0) != -1) &&
		    (errno != EOVERFLOW)) {
			pr_fail("%s: name_to_handle_at failed to get file handle size, errno=%d (%s)\n",
				args->name, errno, strerror(errno));
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
			pr_fail("%s: name_to_handle_at failed to get file handle, errno=%d (%s)\n",
				args->name, errno, strerror(errno));
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

		/* Exercise with large invalid size, EINVAL */
		fhp->handle_bytes = 4096;
		tmp = realloc(fhp, sizeof(struct file_handle) + fhp->handle_bytes);
		if (!tmp) {
			(void)close(mount_fd);
			free(fhp);
			continue;
		}
		fhp = tmp;
		(void)name_to_handle_at(AT_FDCWD, FILENAME, fhp, &mount_id, 0);

		/* Exercise with invalid flags, EINVAL */
		fhp->handle_bytes = 0;
		(void)name_to_handle_at(AT_FDCWD, FILENAME, fhp, &mount_id, ~0);

		/* Exercise with invalid filename, ENOENT */
		fhp->handle_bytes = 0;
		(void)name_to_handle_at(AT_FDCWD, "", fhp, &mount_id, 0);

		/* Exercise with invalid mount_fd */
		fhp->handle_bytes = 32;
		(void)name_to_handle_at(AT_FDCWD, FILENAME, fhp, &mount_id, 0);
		fd = open_by_handle_at(-1, fhp, O_RDONLY);
		if (fd >= 0)
			(void)close(fd);

		/*
		 *  Exercise with bad handle, unconstify f_handle,
		 *  fill it with random garbage and cause an ESTALE
		 *  failure
		 */
		ptr = (char *)shim_unconstify_ptr(&fhp->f_handle);
		stress_strnrnd(ptr, 32);
		fd = open_by_handle_at(mount_fd, fhp, O_RDONLY);
		if (fd >= 0)
			(void)close(fd);

		(void)close(mount_fd);
		free(fhp);
		inc_counter(args);
	} while (keep_stressing(args));

	return EXIT_SUCCESS;
}

/*
 *  stress_handle()
 *	stress system by rapid open/close calls via
 *	name_to_handle_at and open_by_handle_at
 */
static int stress_handle(const stress_args_t *args)
{
	int mounts, ret;

	mounts = get_mount_info(args);
	if (mounts < 0) {
		pr_fail("%s: failed to parse /proc/self/mountinfo\n", args->name);
		return EXIT_FAILURE;
	}

	stress_set_proc_state(args->name, STRESS_STATE_RUN);

	ret = stress_oomable_child(args, &mounts,
		stress_handle_child, STRESS_OOMABLE_NORMAL);

	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);

	free_mount_info(mounts);

	return ret;
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
