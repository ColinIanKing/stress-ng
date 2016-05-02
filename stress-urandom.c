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

#if defined(STRESS_URANDOM)

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#if defined(__linux__)
#include <linux/random.h>
#include <sys/ioctl.h>
#endif

#if defined(__linux__) && defined(RNDGETENTCNT)
#define DEV_RANDOM
#endif

/*
 *  stress_urandom
 *	stress reading of /dev/urandom
 */
int stress_urandom(
	uint64_t *const counter,
	const uint32_t instance,
	const uint64_t max_ops,
	const char *name)
{
	int fd_urnd, rc = EXIT_FAILURE;
#if defined(DEV_RANDOM)
	int fd_rnd;
#endif

	(void)instance;

	if ((fd_urnd = open("/dev/urandom", O_RDONLY)) < 0) {
		pr_fail_err(name, "open");
		return EXIT_FAILURE;
	}
#if defined(DEV_RANDOM)
	if ((fd_rnd = open("/dev/random", O_RDONLY | O_NONBLOCK)) < 0) {
		pr_fail_err(name, "open");
		(void)close(fd_urnd);
		return EXIT_FAILURE;
	}
#endif

	do {
		char buffer[8192];
		ssize_t ret;
#if defined(DEV_RANDOM)
		unsigned long val;
#endif

		ret = read(fd_urnd, buffer, sizeof(buffer));
		if (ret < 0) {
			if ((errno != EAGAIN) && (errno != EINTR)) {
				pr_fail_err(name, "read");
				goto err;
			}
		}
		(*counter)++;

#if defined(DEV_RANDOM)
		/*
		 * Fetch entropy pool count, not considered fatal 
		 * this fails, just skip this part of the stressor
		 */
		if (ioctl(fd_rnd, RNDGETENTCNT, &val) < 0)
			continue;
		/* Try to avoid emptying entropy pool */
		if (val < 128)
			continue;

		ret = read(fd_rnd, buffer, 1);
		if (ret < 0) {
			if ((errno != EAGAIN) && (errno != EINTR)) {
				pr_fail_err(name, "read");
				goto err;
			}
		}
#endif
		(*counter)++;
	} while (opt_do_run && (!max_ops || *counter < max_ops));

	rc = EXIT_SUCCESS;
err:
	(void)close(fd_urnd);
#if defined(DEV_RANDOM)
	(void)close(fd_rnd);
#endif

	return rc;
}
#endif
