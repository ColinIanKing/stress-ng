/*
 * Copyright (C) 2013-2019 Canonical, Ltd.
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

static const help_t help[] = {
	{ "u N","urandom N",	 "start N workers reading /dev/urandom" },
	{ NULL, "urandom-ops N", "stop after N urandom bogo read operations" },
	{ NULL, NULL,		 NULL }
};

/*
 *  stress_urandom
 *	stress reading of /dev/urandom and /dev/random
 */
static int stress_urandom(const args_t *args)
{
	int fd_urnd, fd_rnd, rc = EXIT_FAILURE;

	if ((fd_urnd = open("/dev/urandom", O_RDONLY)) < 0) {
		if (errno != ENOENT) {
			pr_fail_err("open");
			return EXIT_FAILURE;
		}
	}
	if ((fd_rnd = open("/dev/random", O_RDONLY | O_NONBLOCK)) < 0) {
		if (errno != ENOENT) {
			pr_fail_err("open");
			(void)close(fd_urnd);
			return EXIT_FAILURE;
		}
	}

	if ((fd_urnd < 0) && (fd_rnd < 0)) {
		pr_inf("%s: random device(s) do not exist, skipping stressor\n",
			args->name);
		return EXIT_NOT_IMPLEMENTED;
	}

	do {
		char buffer[8192];
		ssize_t ret;

		if (fd_urnd >= 0) {
			ret = read(fd_urnd, buffer, sizeof(buffer));
			if (ret < 0) {
				if ((errno != EAGAIN) && (errno != EINTR)) {
					pr_fail_err("read");
					goto err;
				}
			}
		}

		/*
		 * Fetch entropy pool count, not considered fatal
		 * this fails, just skip this part of the stressor
		 */
		if (fd_rnd >= 0) {
#if defined(RNDGETENTCNT)
			unsigned long val;

			if (ioctl(fd_rnd, RNDGETENTCNT, &val) < 0)
				goto next;
			/* Try to avoid emptying entropy pool */
			if (val < 128)
				goto next;
#endif
			ret = read(fd_rnd, buffer, 1);
			if (ret < 0) {
				if ((errno != EAGAIN) && (errno != EINTR)) {
					pr_fail_err("read");
					goto err;
				}
			}
		}
#if defined(RNDGETENTCNT)
next:
#endif
		inc_counter(args);
	} while (keep_stressing());

	rc = EXIT_SUCCESS;
err:
	if (fd_urnd >= 0)
		(void)close(fd_urnd);
	if (fd_rnd >= 0)
		(void)close(fd_rnd);

	return rc;
}
stressor_info_t stress_urandom_info = {
	.stressor = stress_urandom,
	.class = CLASS_DEV | CLASS_OS,
	.help = help
};
