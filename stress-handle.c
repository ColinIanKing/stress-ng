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
#include "core-out-of-memory.h"

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
static int get_mount_info(stress_args_t *args)
{
	FILE *fp;
	int mounts = 0;

	if ((fp = fopen("/proc/self/mountinfo", "r")) == NULL) {
		pr_dbg("%s: cannot open /proc/self/mountinfo\n", args->name);
		return -1;
	}

	(void)shim_memset(&mount_info, 0, sizeof(mount_info));

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

		mount_info[mounts].mount_path = shim_strdup(mount_path);
		if (mount_info[mounts].mount_path == NULL) {
			pr_dbg("%s: cannot allocate mountinfo mount path%s\n",
				args->name, stress_get_memfree_str());
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
static int stress_handle_child(stress_args_t *args, void *context)
{
	const int mounts = *((int *)context);
	const int bad_fd = stress_get_bad_fd();
	int rc = EXIT_SUCCESS;

	do {
		struct file_handle *fhp, *tmp;
		int mount_id, mount_fd, fd, i;
		char *ptr;

		fhp = (struct file_handle *)malloc(sizeof(*fhp));
		if (UNLIKELY(!fhp))
			continue;

		fhp->handle_bytes = 0;
		if (UNLIKELY((name_to_handle_at(AT_FDCWD, FILENAME, fhp, &mount_id, 0) != -1) &&
		             (errno != EOVERFLOW))) {
			/* if ENOSYS bail out early */
			if (errno == ENOSYS) {
				pr_inf_skip("%s: name_to_handle_at system call not implemented, skipping stressor\n",
					args->name);
				rc = EXIT_NO_RESOURCE;
				free(fhp);
				break;
			}
			pr_fail("%s: name_to_handle_at failed to get file handle size, errno=%d (%s)\n",
				args->name, errno, strerror(errno));
			rc = EXIT_FAILURE;
			free(fhp);
			break;
		}
		tmp = realloc(fhp, sizeof(*tmp) + fhp->handle_bytes);
		if (UNLIKELY(!tmp)) {
			free(fhp);
			continue;
		}
		fhp = tmp;
		if (UNLIKELY(name_to_handle_at(AT_FDCWD, FILENAME, fhp, &mount_id, 0) < 0)) {
			/* if ENOSYS bail out early */
			if (errno == ENOSYS) {
				pr_inf_skip("%s: name_to_handle_at system call not implemented, skipping stressor\n",
					args->name);
				rc = EXIT_NO_RESOURCE;
				free(fhp);
				break;
			}
			pr_fail("%s: name_to_handle_at failed to get file handle, errno=%d (%s)\n",
				args->name, errno, strerror(errno));
			rc = EXIT_FAILURE;
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
		if (UNLIKELY(mount_fd == -2)) {
			pr_fail("%s: cannot find mount id %d\n", args->name, mount_id);
			rc = EXIT_FAILURE;
			free(fhp);
			break;
		}
		if (UNLIKELY(mount_fd < 0)) {
			pr_fail("%s: failed to open mount path '%s', errno=%d (%s)\n",
				args->name, mount_info[i].mount_path, errno, strerror(errno));
			rc = EXIT_FAILURE;
			free(fhp);
			break;
		}
		fd = open_by_handle_at(mount_fd, fhp, O_RDONLY);
		if (UNLIKELY(fd < 0)) {
			/* We don't abort if EPERM occurs, that's not a test failure */
			if (errno != EPERM) {
				pr_fail("%s: open_by_handle_at: failed to open, errno=%d (%s)\n",
					args->name, errno, strerror(errno));
				(void)close(mount_fd);
				rc = EXIT_FAILURE;
				free(fhp);
				break;
			}
		} else {
			(void)close(fd);
		}

		/* Exercise with large invalid size, EINVAL */
		fhp->handle_bytes = 4096;
		tmp = realloc(fhp, sizeof(*tmp) + fhp->handle_bytes);
		if (UNLIKELY(!tmp)) {
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

		/* Exercise with invalid handle_bytes, EOVERFLOW */
		fhp->handle_bytes = 1;
		(void)name_to_handle_at(AT_FDCWD, FILENAME, fhp, &mount_id, AT_EMPTY_PATH);

		/* Exercise with invalid mount_fd, part 1*/
		fhp->handle_bytes = 32;
		(void)name_to_handle_at(AT_FDCWD, FILENAME, fhp, &mount_id, 0);
		fd = open_by_handle_at(-1, fhp, O_RDONLY);
		if (UNLIKELY(fd >= 0))
			(void)close(fd);

		/* Exercise with invalid mount_fd, part 2 */
		fhp->handle_bytes = 32;
		(void)name_to_handle_at(AT_FDCWD, FILENAME, fhp, &mount_id, 0);
		fd = open_by_handle_at(bad_fd, fhp, O_RDONLY);
		if (UNLIKELY(fd >= 0))
			(void)close(fd);

		/*
		 *  Exercise with bad handle, unconstify f_handle,
		 *  fill it with random garbage and cause an ESTALE
		 *  failure
		 */
		ptr = (char *)shim_unconstify_ptr(&fhp->f_handle);
		stress_rndbuf(ptr, 32);
		fd = open_by_handle_at(mount_fd, fhp, O_RDONLY);
		if (UNLIKELY(fd >= 0))
			(void)close(fd);

		(void)close(mount_fd);
		free(fhp);
		stress_bogo_inc(args);
	} while (stress_continue(args));

	return rc;
}

/*
 *  stress_handle()
 *	stress system by rapid open/close calls via
 *	name_to_handle_at and open_by_handle_at
 */
static int stress_handle(stress_args_t *args)
{
	int mounts, ret;

	mounts = get_mount_info(args);
	if (mounts < 0) {
		pr_fail("%s: failed to parse /proc/self/mountinfo\n", args->name);
		return EXIT_FAILURE;
	}

	stress_set_proc_state(args->name, STRESS_STATE_SYNC_WAIT);
	stress_sync_start_wait(args);
	stress_set_proc_state(args->name, STRESS_STATE_RUN);

	ret = stress_oomable_child(args, &mounts,
		stress_handle_child, STRESS_OOMABLE_NORMAL);

	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);

	free_mount_info(mounts);

	return ret;
}

const stressor_info_t stress_handle_info = {
	.stressor = stress_handle,
	.classifier = CLASS_FILESYSTEM | CLASS_OS,
	.verify = VERIFY_ALWAYS,
	.help = help
};
#else
const stressor_info_t stress_handle_info = {
	.stressor = stress_unimplemented,
	.classifier = CLASS_FILESYSTEM | CLASS_OS,
	.verify = VERIFY_ALWAYS,
	.help = help,
	.unimplemented_reason = "built without name_to_handle_at(), open_by_handle_at() or AT_FDCWD"
};
#endif
