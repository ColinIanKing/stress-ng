/*
 * Copyright (C) 2013-2017 Canonical, Ltd.
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

#if defined(__linux__) && defined(HAVE_SYS_CAP_H)

#include <sys/capability.h>

static int stress_capgetset_pid(
	const args_t *args,
	const pid_t pid,
	const bool do_set,
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
			pr_fail("%s: capget on pid %d failed: "
				"errno=%d (%s)\n",
				args->name, pid, errno, strerror(errno));
		}
	}

	if (do_set) {
		ret = capset(&uch, ucd);
		if (ret < 0) {
			if (((errno == ESRCH) && exists) ||
			    (errno != ESRCH)) {
				pr_fail("%s: capget on pid %d failed: "
					"errno=%d (%s)\n",
					args->name, pid, errno, strerror(errno));
			}
		}
	}

	inc_counter(args);

	return ret;
}

/*
 *  stress_cap
 *	stress capabilities (trivial)
 */
int stress_cap(const args_t *args)
{
	do {
		DIR *dir;

		stress_capgetset_pid(args, 1, false, true);
		if (!keep_stressing())
			break;
		stress_capgetset_pid(args, args->pid, true, true);
		if (!keep_stressing())
			break;
		stress_capgetset_pid(args, args->ppid, false, false);
		if (!keep_stressing())
			break;

		dir = opendir("/proc");
		if (dir) {
			struct dirent *d;

			while ((d = readdir(dir)) != NULL) {
				pid_t p;

				if (!isdigit(d->d_name[0]))
					continue;
				if (sscanf(d->d_name, "%d", &p) != 1)
					continue;
				stress_capgetset_pid(args, p, false, false);
				if (!keep_stressing())
					break;
			}
			(void)closedir(dir);
		}
	} while (keep_stressing());

	return EXIT_SUCCESS;
}
#else
int stress_cap(const args_t *args)
{
	return stress_not_implemented(args);
}
#endif
