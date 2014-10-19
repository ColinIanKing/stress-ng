/*
 * Copyright (C) 2013-2014 Canonical, Ltd.
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
#include <string.h>
#include <unistd.h>
#include <poll.h>
#include <errno.h>

#include "stress-ng.h"

/*
 *  stress_poll()
 *	stress system by rapid polling system calls
 */
int stress_poll(
	uint64_t *const counter,
	const uint32_t instance,
	const uint64_t max_ops,
	const char *name)
{
	(void)instance;
	(void)name;

	do {
		struct timeval tv;
		int ret;

		ret = poll(NULL, 0, 0);
		if ((opt_flags & OPT_FLAGS_VERIFY) &&
                    (ret < 0) && (errno != EINTR)) {
			pr_fail(stderr, "poll failed with error: %d (%s)\n",
				errno, strerror(errno));
		}

		tv.tv_sec = 0;
		tv.tv_usec = 0;
		ret = select(0, NULL, NULL, NULL, &tv);
		if ((opt_flags & OPT_FLAGS_VERIFY) &&
                    (ret < 0) && (errno != EINTR)) {
			pr_fail(stderr, "select failed with error: %d (%s)\n",
				errno, strerror(errno));
		}
		if (!opt_do_run)
			break;
		(void)sleep(0);
		(*counter)++;
	} while (opt_do_run && (!max_ops || *counter < max_ops));

	return EXIT_SUCCESS;
}
