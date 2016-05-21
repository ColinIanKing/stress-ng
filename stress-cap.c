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

#if defined(STRESS_CAP)

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <ctype.h>
#include <sys/capability.h>
#include <sys/types.h>
#include <dirent.h>

static int stress_capgetset_pid(
	const char *name,
	const pid_t pid,
	const bool do_set,
	uint64_t *counter,
	const bool exists)
{
	int ret;
	struct __user_cap_header_struct uch;
	struct __user_cap_data_struct ucd[_LINUX_CAPABILITY_U32S_3];

	memset(&uch, 0, sizeof uch);
	memset(ucd, 0, sizeof ucd);

	uch.version = _LINUX_CAPABILITY_VERSION_3;
	uch.pid = pid;

	ret = capget(&uch, ucd);
	if (ret < 0) {
		if (((errno == ESRCH) && exists) ||
		    (errno != ESRCH)) {
			pr_fail(stderr, "%s: capget on pid %d failed: errno=%d (%s)\n",
				name, pid, errno, strerror(errno));
		}
	}

	if (do_set) {
		ret = capset(&uch, ucd);
		if (ret < 0) {
			if (((errno == ESRCH) && exists) ||
			    (errno != ESRCH)) {
				pr_fail(stderr, "%s: capget on pid %d failed: errno=%d (%s)\n",
					name, pid, errno, strerror(errno));
			}
		}
	}

	(*counter)++;

	return ret;
}

/*
 *  stress_cap
 *	stress capabilities (trivial)
 */
int stress_cap(
	uint64_t *const counter,
	const uint32_t instance,
	const uint64_t max_ops,
	const char *name)
{
	const pid_t pid = getpid();
	const pid_t ppid = getppid();

	(void)instance;

	do {
		DIR *dir;

		stress_capgetset_pid(name, 1, false, counter, true);
		if (!opt_do_run || (max_ops && *counter >= max_ops))
			break;
		stress_capgetset_pid(name, pid, true, counter, true);
		if (!opt_do_run || (max_ops && *counter >= max_ops))
			break;
		stress_capgetset_pid(name, ppid, false, counter, false);
		if (!opt_do_run || (max_ops && *counter >= max_ops))
			break;

		dir = opendir("/proc");
		if (dir) {
			struct dirent *d;

			while ((d = readdir(dir)) != NULL) {
				pid_t p;

				if (!isdigit(d->d_name[0]))
					continue;
				if (sscanf(d->d_name, "%u", &p) != 1)
					continue;
				stress_capgetset_pid(name, p, false, counter, false);
				if (!opt_do_run || (max_ops && *counter >= max_ops))
					break;
			}
			(void)closedir(dir);
		}
	} while (opt_do_run && (!max_ops || *counter < max_ops));

	return EXIT_SUCCESS;
}

#endif
