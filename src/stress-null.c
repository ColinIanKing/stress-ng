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
	{ NULL,	"null N",	"start N workers writing to /dev/null" },
	{ NULL,	"null-ops N",	"stop after N /dev/null bogo write operations" },
	{ NULL,	NULL,		NULL }
};

/*
 *  stress_null
 *	stress writing to /dev/null
 */
static int stress_null(const stress_args_t *args)
{
	int fd;
	char buffer[4096];

	if ((fd = open("/dev/null", O_RDWR)) < 0) {
		pr_fail("%s: open /dev/null failed, errno=%d (%s)\n",
			args->name, errno, strerror(errno));
		return EXIT_FAILURE;
	}

	(void)memset(buffer, 0xff, sizeof(buffer));

	stress_set_proc_state(args->name, STRESS_STATE_RUN);

	do {
		ssize_t ret;
#if defined(__linux__)
		void *ptr;
		const size_t page_size = args->page_size;
#endif

		ret = write(fd, buffer, sizeof(buffer));
		if (ret <= 0) {
			if ((errno == EAGAIN) || (errno == EINTR))
				continue;
			if (errno) {
				pr_fail("%s: write failed, errno=%d (%s)\n",
					args->name, errno, strerror(errno));
				(void)close(fd);
				return EXIT_FAILURE;
			}
			continue;
		}

#if defined(__linux__)
		ptr = mmap(NULL, page_size, PROT_WRITE,
			MAP_PRIVATE | MAP_ANONYMOUS, fd, stress_mwc64() & ~(page_size - 1));
		if (ptr != MAP_FAILED) {
			(void)memset(ptr, stress_mwc8(), page_size);
			(void)shim_msync(ptr, page_size, MS_SYNC);
			(void)munmap(ptr, page_size);
		}
#endif

		inc_counter(args);
	} while (keep_stressing(args));
	(void)close(fd);

	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);

	return EXIT_SUCCESS;
}

stressor_info_t stress_null_info = {
	.stressor = stress_null,
	.class = CLASS_DEV | CLASS_MEMORY | CLASS_OS,
	.help = help
};
