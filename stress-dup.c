/*
 * Copyright (C) 2013-2020 Canonical, Ltd.
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
	{ NULL,	"dup N",	"start N workers exercising dup/close" },
	{ NULL,	"dup-ops N",	"stop after N dup/close bogo operations" },
	{ NULL,	NULL,		NULL }
};

/*
 *  stress_dup()
 *	stress system by rapid dup/close calls
 */
static int stress_dup(const stress_args_t *args)
{
	static int fds[STRESS_FD_MAX];
	size_t max_fd = stress_get_file_limit();
	size_t i;
#if defined(HAVE_DUP3)
	bool do_dup3 = true;
#endif
#if defined(RLIMIT_NOFILE)
	int max_rlimit_fd = -1;
	struct rlimit rlim;
#endif

	if (max_fd > SIZEOF_ARRAY(fds))
		max_fd =  SIZEOF_ARRAY(fds);

	fds[0] = open("/dev/zero", O_RDONLY);
	if (fds[0] < 0) {
		pr_dbg("%s: open failed on /dev/zero, errno=%d (%s)\n",
			args->name, errno, strerror(errno));
		return EXIT_NO_RESOURCE;
	}
#if defined(RLIMIT_NOFILE)
	(void)memset(&rlim, 0, sizeof(rlim));
	if (getrlimit(RLIMIT_NOFILE, &rlim) == 0) {
		if (rlim.rlim_cur < INT_MAX - 1)
			max_rlimit_fd = rlim.rlim_cur + 1;
	}
#endif

	do {
		size_t n;

		for (n = 1; n < max_fd; n++) {
			int tmp;

			fds[n] = dup(fds[0]);
			if (fds[n] < 0)
				break;
#if defined(RLIMIT_NOFILE)
			/* do an invalid dup on a large fd */
			tmp = dup(max_rlimit_fd);
			if (tmp >= 0)
				(void)close(tmp);
#endif

#if defined(HAVE_DUP3)
#if defined(RLIMIT_NOFILE)
			/* do an invalid dup3 on a large fd */
			tmp = shim_dup3(fds[0], max_rlimit_fd, O_CLOEXEC);
			if (tmp >= 0)
				(void)close(tmp);
#endif

			if (do_dup3 && stress_mwc1()) {
				int fd;

				fd = shim_dup3(fds[0], fds[n], O_CLOEXEC);
				/* No dup3 support? then fallback to dup2 */
				if ((fd < 0) && (errno == ENOSYS)) {
					fd = dup2(fds[0], fds[n]);
					do_dup3 = false;
				}
				fds[n] = fd;
			} else {
				fds[n] = dup2(fds[0], fds[n]);
			}
#else
			fds[n] = dup2(fds[0], fds[n]);
#endif
			if (fds[n] < 0)
				break;

			/* dup2 on the same fd should be a no-op */
			tmp = dup2(fds[n], fds[n]);
			if (tmp != fds[n]) {
				pr_fail("%s: dup2 failed with same fds, errno=%d (%s)\n",
					args->name, errno, strerror(errno));
				break;
			}
#if defined(RLIMIT_NOFILE)
			/* do an invalid dup2 on a large fd */
			tmp = dup2(fds[0], max_rlimit_fd);
			if (tmp >= 0)
				(void)close(tmp);
#endif

#if defined(F_DUPFD)
			/* POSIX.1-2001 fcntl() */

			(void)close(fds[n]);
			fds[n] = fcntl(fds[0], F_DUPFD, fds[0]);
			if (fds[n] < 0)
				break;
#endif

			if (!keep_stressing())
				break;
			inc_counter(args);
		}
		for (i = 1; i < n; i++) {
			if (fds[i] < 0)
				break;
			if (!keep_stressing_flag())
				break;
			(void)close(fds[i]);
		}
	} while (keep_stressing());
	(void)close(fds[0]);

	return EXIT_SUCCESS;
}

stressor_info_t stress_dup_info = {
	.stressor = stress_dup,
	.class = CLASS_FILESYSTEM | CLASS_OS,
	.help = help
};
