/*
 * Copyright (C) 2013-2016 Canonical, Ltd.
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
#define _GNU_SOURCE

#include "stress-ng.h"

#if defined(STRESS_HANDLE)

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/resource.h>

#define MAX_MOUNT_IDS	(1024)
#define FILENAME	"/dev/zero"

typedef struct {
	char	*mount_path;
	int	mount_id;
} mount_info_t;

static mount_info_t mount_info[MAX_MOUNT_IDS];

void free_mount_info(const int mounts)
{
	int i;

	for (i = 0; i < mounts; i++)
		free(mount_info[i].mount_path);
}

int get_mount_info(const char *name)
{
	FILE *fp;
	int mounts = 0;

	if ((fp = fopen("/proc/self/mountinfo", "r")) == NULL) {
		pr_dbg(stderr, "%s: cannot open /proc/self/mountinfo\n", name);
		return -1;
	}

	for (;;) {
		char mount_path[PATH_MAX];
		char *line = NULL;
		size_t line_len = 0;

		ssize_t nread = getline(&line, &line_len, fp);
		if (nread == -1)
			break;

		nread = sscanf(line, "%12d %*d %*s %*s %s",
			&mount_info[mounts].mount_id,
			mount_path);
		if (nread != 2)
			continue;

		mount_info[mounts].mount_path = strdup(mount_path);
		if (mount_info[mounts].mount_path == NULL) {
			pr_dbg(stderr, "%s: cannot allocate mountinfo mount path\n", name);
			free_mount_info(mounts);
			mounts = -1;
			break;
		}
		mounts++;
	}
	fclose(fp);
	return mounts;
}


/*
 *  stress_handle()
 *	stress system by rapid open/close calls via
 *	name_to_handle_at and open_by_handle_at
 */
int stress_handle(
	uint64_t *const counter,
	const uint32_t instance,
	const uint64_t max_ops,
	const char *name)
{
	int mounts;

	(void)instance;

	if ((mounts = get_mount_info(name)) < 0) {
		pr_fail(stderr, "%s: failed to parse /proc/self/mountinfo\n", name);
		return EXIT_FAILURE;
	}

	do {
		struct file_handle *fhp, *tmp;
		int mount_id, mount_fd, fd, i;

		if ((fhp = malloc(sizeof(*fhp))) == NULL)
			continue;

		fhp->handle_bytes = 0;
		if ((name_to_handle_at(AT_FDCWD, FILENAME, fhp, &mount_id, 0) != -1) &&
		    (errno != EOVERFLOW)) {
			pr_fail(stderr, "%s: name_to_handle_at: failed to get file handle size: errno=%d (%s)\n",
				name, errno, strerror(errno));
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
			pr_fail(stderr, "%s: name_to_handle_at: failed to get file handle: errno=%d (%s)\n",
				name, errno, strerror(errno));
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
			pr_fail(stderr, "%s: cannot find mount id %d\n", name, mount_id);
			free(fhp);
			break;
		}
		if (mount_fd < 0) {
			pr_fail(stderr, "%s: failed to open mount path '%s': errno=%d (%s)\n",
				name, mount_info[i].mount_path, errno, strerror(errno));
			free(fhp);
			break;
		}
		if ((fd = open_by_handle_at(mount_fd, fhp, O_RDONLY)) < 0) {
			/* We don't abort if EPERM occurs, that's not a test failure */
			if (errno != EPERM) {
				pr_fail(stderr, "%s: open_by_handle_at: failed to open: errno=%d (%s)\n",
					name, errno, strerror(errno));
				(void)close(mount_fd);
				free(fhp);
				break;
			}
		} else {
			(void)close(fd);
		}
		(void)close(mount_fd);
		free(fhp);
	} while (opt_do_run && (!max_ops || *counter < max_ops));

	free_mount_info(mounts);

	return EXIT_SUCCESS;
}
#endif
