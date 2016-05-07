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

#if defined(STRESS_IOPRIO)

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>


/*
 *  stress set/get io priorities
 *	stress system by rapid io priority changes
 */
int stress_ioprio(
	uint64_t *const counter,
	const uint32_t instance,
	const uint64_t max_ops,
	const char *name)
{
	const pid_t pid = getpid();
	const uid_t uid = getuid();
	const pid_t pgrp = getpgrp();

	(void)instance;

	do {
		int i;

		if (sys_ioprio_get(IOPRIO_WHO_PROCESS, pid) < 0) {
			pr_fail(stderr, "%s: ioprio_get(OPRIO_WHO_PROCESS, %d), "
				"errno = %d (%s)\n",
				name, pid, errno, strerror(errno));
			return EXIT_FAILURE;
		}
		if (sys_ioprio_get(IOPRIO_WHO_PROCESS, 0) < 0) {
			pr_fail(stderr, "%s: ioprio_get(OPRIO_WHO_PROCESS, 0), "
				"errno = %d (%s)\n",
				name, errno, strerror(errno));
			return EXIT_FAILURE;
		}
		if (sys_ioprio_get(IOPRIO_WHO_PGRP, pgrp) < 0) {
			pr_fail(stderr, "%s: ioprio_get(OPRIO_WHO_PGRP, %d), "
				"errno = %d (%s)\n",
				name, pgrp, errno, strerror(errno));
			return EXIT_FAILURE;
		}
		if (sys_ioprio_get(IOPRIO_WHO_PGRP, 0) < 0) {
			pr_fail(stderr, "%s: ioprio_get(OPRIO_WHO_PGRP, 0), "
				"errno = %d (%s)\n",
				name, errno, strerror(errno));
			return EXIT_FAILURE;
		}
		if (sys_ioprio_get(IOPRIO_WHO_USER, uid) < 0) {
			pr_fail(stderr, "%s: ioprio_get(OPRIO_WHO_USR, %d), "
				"errno = %d (%s)\n",
				name, uid, errno, strerror(errno));
			return EXIT_FAILURE;
		}
		if (sys_ioprio_set(IOPRIO_WHO_PROCESS, pid,
			IOPRIO_PRIO_VALUE(IOPRIO_CLASS_IDLE, 0)) < 0) {
			if (errno != EPERM) {
				pr_fail(stderr, "%s: ioprio_set("
					"IOPRIO_WHO_PROCESS, %d, "
					"(IOPRIO_CLASS_IDLE, 0)), "
					"errno = %d (%s)\n",
					name, pid, errno, strerror(errno));
				return EXIT_FAILURE;
			}
		}
		for (i = 0; i < 8; i++) {
			if (sys_ioprio_set(IOPRIO_WHO_PROCESS, pid,
				IOPRIO_PRIO_VALUE(IOPRIO_CLASS_BE, i)) < 0) {
				if (errno != EPERM) {
					pr_fail(stderr, "%s: ioprio_set("
						"IOPRIO_WHO_PROCESS, %d, "
						"(IOPRIO_CLASS_BE, %d)), "
						"errno = %d (%s)\n",
						name, pid, i, errno, strerror(errno));
					return EXIT_FAILURE;
				}
			}
		}
		for (i = 0; i < 8; i++) {
			if (sys_ioprio_set(IOPRIO_WHO_PROCESS, pid,
				IOPRIO_PRIO_VALUE(IOPRIO_CLASS_RT, i)) < 0) {
				if (errno != EPERM) {
					pr_fail(stderr, "%s: ioprio_set("
						"IOPRIO_WHO_PROCESS, %d, "
						"(IOPRIO_CLASS_RT, %d)), "
						"errno = %d (%s)\n",
						name, pid, i, errno, strerror(errno));
					return EXIT_FAILURE;
				}
			}
		}
		(*counter)++;
	} while (opt_do_run && (!max_ops || *counter < max_ops));

	return EXIT_SUCCESS;
}

#endif
