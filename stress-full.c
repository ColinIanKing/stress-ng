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

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <inttypes.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>

#include "stress-ng.h"

typedef struct {
	char *name;
	int whence;
} whences_t;

static const whences_t whences[] = {
	{ "SEEK_SET",	SEEK_SET },
	{ "SEEK_CUR",	SEEK_CUR },
	{ "SEEK_END",	SEEK_END }
};

/*
 *  stress_full
 *	stress /dev/full
 */
int stress_full(
	uint64_t *const counter,
	const uint32_t instance,
	const uint64_t max_ops,
	const char *name)
{
	(void)instance;

	do {
		ssize_t ret;
		int fd, w;
		ssize_t i;
		off_t offset;
		char buffer[4096];

		if ((fd = open("/dev/full", O_RDWR)) < 0) {
			pr_fail_err(name, "open");
			return EXIT_FAILURE;
		}

		/*
		 *  Writes should always return -ENOSPC
		 */
		memset(buffer, 0, sizeof(buffer));
		ret = write(fd, buffer, sizeof(buffer));
		if (ret != -1) {
			pr_fail(stderr, "%s: write to /dev/null should fail "
				"with errno ENOSPC but it didn't\n",
				name);
			(void)close(fd);
			return EXIT_FAILURE;
		}
		if ((errno == EAGAIN) || (errno == EINTR))
			goto try_read;
		if (errno != ENOSPC) {
			pr_fail_err(name, "write");
			(void)close(fd);
			return EXIT_FAILURE;
		}

		/*
		 *  Reads should always work
		 */
try_read:
		ret = read(fd, buffer, sizeof(buffer));
		if (ret < 0) {
			pr_fail_err(name, "read");
			(void)close(fd);
			return EXIT_FAILURE;
		}
		for (i = 0; i < ret; i++) {
			if (buffer[i] != 0) {
				pr_fail(stderr, "%s: buffer does not "
					"contain all zeros\n",
					name);
				(void)close(fd);
				return EXIT_FAILURE;
			}
		}

		/*
		 *  Seeks will always succeed
		 */
		w = mwc32() % 3;
		offset = (off_t)mwc64();
		ret = lseek(fd, offset, whences[w].whence);
		if (ret < 0) {
			pr_fail(stderr, "%s: lseek(fd, %jd, %s)\n",
				name, (intmax_t)offset, whences[w].name);
			(void)close(fd);
			return EXIT_FAILURE;
		}
		(void)close(fd);
		(*counter)++;
	} while (opt_do_run && (!max_ops || *counter < max_ops));

	return EXIT_SUCCESS;
}
