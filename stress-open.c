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

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>

#include "stress-ng.h"

typedef int (*open_func_t)(void);

static int open_dev_zero_rd(void)
{
	int flags = 0;
#if defined(O_ASYNC)
	flags |= (mwc32() & O_ASYNC);
#endif
#if defined(O_CLOEXEC)
	flags |= (mwc32() & O_CLOEXEC);
#endif
#if defined(O_LARGEFILE)
	flags |= (mwc32() & O_LARGEFILE);
#endif
#if defined(O_NOFOLLOW)
	flags |= (mwc32() & O_NOFOLLOW);
#endif
#if defined(O_NONBLOCK)
	flags |= (mwc32() & O_NONBLOCK);
#endif

	return open("/dev/zero", O_RDONLY | flags);
}

static int open_dev_null_wr(void)
{
	int flags = 0;
#if defined(O_ASYNC)
	flags |= (mwc32() & O_ASYNC);
#endif
#if defined(O_CLOEXEC)
	flags |= (mwc32() & O_CLOEXEC);
#endif
#if defined(O_LARGEFILE)
	flags |= (mwc32() & O_LARGEFILE);
#endif
#if defined(O_NOFOLLOW)
	flags |= (mwc32() & O_NOFOLLOW);
#endif
#if defined(O_NONBLOCK)
	flags |= (mwc32() & O_NONBLOCK);
#endif
#if defined(O_DSYNC)
	flags |= (mwc32() & O_DSYNC);
#endif
#if defined(O_SYNC)
	flags |= (mwc32() & O_SYNC);
#endif

	return open("/dev/null", O_WRONLY | flags);
}

#if defined(O_TMPFILE)
static int open_tmp_rdwr(void)
{
	int flags = 0;
#if defined(O_TRUNC)
	flags |= (mwc32() & O_TRUNC);
#endif
#if defined(O_APPEND)
	flags |= (mwc32() & O_APPEND);
#endif
#if defined(O_NOATIME)
	flags |= (mwc32() & O_NOATIME);
#endif
#if defined(O_DIRECT)
	flags |= (mwc32() & O_DIRECT);
#endif
	return open("/tmp", O_TMPFILE | flags | O_RDWR, S_IRUSR | S_IWUSR);
}
#endif

#if defined(O_TMPFILE) && defined(O_EXCL)
static int open_tmp_rdwr_excl(void)
{
	return open("/tmp", O_TMPFILE | O_EXCL | O_RDWR, S_IRUSR | S_IWUSR);
}
#endif

#if defined(O_DIRECTORY)
static int open_dir(void)
{
	return open(".", O_DIRECTORY | O_RDONLY);
}
#endif

#if defined(O_PATH)
static int open_path(void)
{
	return open(".", O_DIRECTORY | O_PATH);
}
#endif


static open_func_t open_funcs[] = {
	open_dev_zero_rd,
	open_dev_null_wr,
#if defined(O_TMPFILE)
	open_tmp_rdwr,
#endif
#if defined(O_TMPFILE) && defined(O_EXCL)
	open_tmp_rdwr_excl,
#endif
#if defined(O_DIRECTORY)
	open_dir,
#endif
#if defined(O_PATH)
	open_path
#endif
};

/*
 *  stress_open()
 *	stress system by rapid open/close calls
 */
int stress_open(
	uint64_t *const counter,
	const uint32_t instance,
	const uint64_t max_ops,
	const char *name)
{
	int fds[STRESS_FD_MAX];
	const size_t max_fd = stress_get_file_limit();
	size_t i;

	(void)instance;
	(void)name;

	do {
		for (i = 0; i < max_fd; i++) {
			int idx = mwc32() % SIZEOF_ARRAY(open_funcs);

			fds[i] = open_funcs[idx]();

			if (fds[i] < 0)
				break;
			if (!opt_do_run)
				break;
			(*counter)++;
		}
		for (i = 0; i < max_fd; i++) {
			if (fds[i] < 0)
				break;
			if (!opt_do_run)
				break;
			(void)close(fds[i]);
		}
	} while (opt_do_run && (!max_ops || *counter < max_ops));

	return EXIT_SUCCESS;
}
