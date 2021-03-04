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
	{ NULL,	"full N",	"start N workers exercising /dev/full" },
	{ NULL, "full-ops N",	"stop after N /dev/full bogo I/O operations" },
	{ NULL,	NULL,		NULL }
};

#if defined(__linux__)

typedef struct {
	const char *name;
	const int whence;
} stress_whences_t;

static const stress_whences_t whences[] = {
	{ "SEEK_SET",	SEEK_SET },
	{ "SEEK_CUR",	SEEK_CUR },
	{ "SEEK_END",	SEEK_END }
};

/*
 *  stress_full
 *	stress /dev/full
 */
static int stress_full(const stress_args_t *args)
{
	stress_set_proc_state(args->name, STRESS_STATE_RUN);

	do {
		ssize_t ret;
		int fd, w;
		ssize_t i;
		off_t offset;
		char buffer[4096];
		char *ptr;

		if ((fd = open("/dev/full", O_RDWR)) < 0) {
			if (errno == ENOENT) {
				pr_inf("%s: /dev/full not available, skipping stress test\n",
					args->name);
				return EXIT_NOT_IMPLEMENTED;
			}
			pr_fail("%s: open /dev/full failed, errno=%d (%s)\n",
				args->name, errno, strerror(errno));
			return EXIT_FAILURE;
		}

		/*
		 *  Writes should always return -ENOSPC
		 */
		(void)memset(buffer, 0, sizeof(buffer));
		ret = write(fd, buffer, sizeof(buffer));
		if (ret != -1) {
			pr_fail("%s: write to /dev/null should fail "
				"with errno ENOSPC but it didn't\n",
				args->name);
			(void)close(fd);
			return EXIT_FAILURE;
		}
		if ((errno == EAGAIN) || (errno == EINTR))
			goto try_read;
		if (errno != ENOSPC) {
			pr_fail("%s: write failed, errno=%d (%s)\n",
				args->name, errno, strerror(errno));
			(void)close(fd);
			return EXIT_FAILURE;
		}

		/*
		 *  Reads should always work
		 */
try_read:
		ret = read(fd, buffer, sizeof(buffer));
		if (ret < 0) {
			pr_fail("%s: read failed, errno=%d (%s)\n",
				args->name, errno, strerror(errno));
			(void)close(fd);
			return EXIT_FAILURE;
		}
		for (i = 0; i < ret; i++) {
			if (buffer[i] != 0) {
				pr_fail("%s: buffer does not contain all zeros\n",
					args->name);
				(void)close(fd);
				return EXIT_FAILURE;
			}
		}

		/*
		 *  Try mmap'ing and msync on fd
		 */
		ptr = (char *)mmap(NULL, args->page_size, PROT_READ,
			MAP_ANONYMOUS | MAP_PRIVATE, fd, 0);
		if (ptr != MAP_FAILED) {
			stress_uint8_put(*ptr);
#if defined(MS_SYNC)
			(void)msync(ptr, args->page_size, MS_SYNC);
#endif
			(void)munmap((void *)ptr, args->page_size);
		}
		ptr = (char *)mmap(NULL, args->page_size, PROT_WRITE,
			MAP_ANONYMOUS | MAP_PRIVATE, fd, 0);
		if (ptr != MAP_FAILED) {
			*ptr = 0;
			(void)munmap((void *)ptr, args->page_size);
		}


		/*
		 *  Seeks will always succeed
		 */
		w = stress_mwc32() % 3;
		offset = (off_t)stress_mwc64();
		ret = lseek(fd, offset, whences[w].whence);
		if (ret < 0) {
			pr_fail("%s: lseek(fd, %jd, %s)\n",
				args->name, (intmax_t)offset, whences[w].name);
			(void)close(fd);
			return EXIT_FAILURE;
		}
		(void)close(fd);
		inc_counter(args);
	} while (keep_stressing(args));

	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);

	return EXIT_SUCCESS;
}

stressor_info_t stress_full_info = {
	.stressor = stress_full,
	.class = CLASS_DEV | CLASS_MEMORY | CLASS_OS,
	.help = help
};
#else
stressor_info_t stress_full_info = {
	.stressor = stress_not_implemented,
	.class = CLASS_DEV | CLASS_MEMORY | CLASS_OS,
	.help = help
};
#endif
