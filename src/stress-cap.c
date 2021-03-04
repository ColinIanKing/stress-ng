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
	{ NULL,	"cap N",	"start N workers exercising capget" },
	{ NULL,	"cap-ops N",	"stop cap workers after N bogo capget operations" },
	{ NULL,	NULL,		NULL }
};

#if defined(HAVE_SYS_CAPABILITY_H) &&	\
    defined(_LINUX_CAPABILITY_U32S_3)

static int stress_capgetset_pid(
	const stress_args_t *args,
	const pid_t pid,
	const bool do_set,
	const bool exists)
{
	int ret;
	struct __user_cap_header_struct uch;
	struct __user_cap_data_struct ucd[_LINUX_CAPABILITY_U32S_3];

	(void)memset(&uch, 0, sizeof uch);
	(void)memset(ucd, 0, sizeof ucd);

#if defined(_LINUX_CAPABILITY_VERSION_3)
	uch.version = _LINUX_CAPABILITY_VERSION_3;
	uch.pid = pid;

	ret = capget(&uch, ucd);
	if (ret < 0) {
		if (((errno == ESRCH) && exists) ||
		    (errno != ESRCH)) {
			pr_fail("%s: capget on pid %" PRIdMAX " failed: errno=%d (%s)\n",
				args->name, (intmax_t)pid, errno, strerror(errno));
		}
	}

	if (do_set) {
		ret = capset(&uch, ucd);
		if (ret < 0) {
			if (((errno == ESRCH) && exists) ||
			    (errno != ESRCH)) {
				pr_fail("%s: capget on pid %" PRIdMAX " failed: errno=%d (%s)\n",
					args->name, (intmax_t)pid, errno, strerror(errno));
			}
		}
	}
#endif

#if defined(_LINUX_CAPABILITY_VERSION_3)
	/*
	 *  Exercise invalid pid
	 */
	uch.version = _LINUX_CAPABILITY_VERSION_3;
	uch.pid = stress_get_unused_pid_racy(false);
	ret = capget(&uch, ucd);
#endif

	/*
	 *  Exercise older capability versions
	 */
#if defined(_LINUX_CAPABILITY_VERSION_2)
	uch.version = _LINUX_CAPABILITY_VERSION_2;
	uch.pid = pid;
	ret = capget(&uch, ucd);
#endif

#if defined(_LINUX_CAPABILITY_VERSION_1)
	uch.version = _LINUX_CAPABILITY_VERSION_1;
	uch.pid = pid;
	ret = capget(&uch, ucd);
#endif

	uch.version = ~0;
	uch.pid = pid;
	ret = capget(&uch, ucd);

	inc_counter(args);

	return ret;
}

/*
 *  stress_cap
 *	stress capabilities (trivial)
 */
static int stress_cap(const stress_args_t *args)
{
	stress_set_proc_state(args->name, STRESS_STATE_RUN);

	do {
		DIR *dir;

		stress_capgetset_pid(args, 1, false, true);
		if (!keep_stressing(args))
			break;
		stress_capgetset_pid(args, args->pid, true, true);
		if (!keep_stressing(args))
			break;
		stress_capgetset_pid(args, args->ppid, false, false);
		if (!keep_stressing(args))
			break;

		dir = opendir("/proc");
		if (dir) {
			struct dirent *d;

			while ((d = readdir(dir)) != NULL) {
				intmax_t p;

				if (!isdigit(d->d_name[0]))
					continue;
				if (sscanf(d->d_name, "%" SCNdMAX, &p) != 1)
					continue;
				stress_capgetset_pid(args, (pid_t)p, false, false);
				if (!keep_stressing(args))
					break;
			}
			(void)closedir(dir);
		}
	} while (keep_stressing(args));

	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);
	return EXIT_SUCCESS;
}

stressor_info_t stress_cap_info = {
	.stressor = stress_cap,
	.class = CLASS_OS,
	.help = help
};
#else
stressor_info_t stress_cap_info = {
	.stressor = stress_not_implemented,
	.class = CLASS_OS,
	.help = help
};
#endif
