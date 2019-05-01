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

typedef int (*open_func_t)(void);

static const help_t help[] = {
	{ "o N", "open N",	"start N workers exercising open/close" },
	{ NULL,	"open-ops N",	"stop after N open/close bogo operations" },
	{ NULL,	NULL,		NULL }
};

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

#if defined(HAVE_POSIX_OPENPT) && defined(O_RDWR) && defined(N_NOCTTY)
static int open_pt(void)
{
	return posix_openpt(O_RDWR | O_NOCTTY);
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
	open_path,
#endif
#if defined(HAVE_POSIX_OPENPT) && defined(O_RDWR) && defined(N_NOCTTY)
	open_pt
#endif
};

/*
 *  stress_open()
 *	stress system by rapid open/close calls
 */
static int stress_open(const args_t *args)
{
	static int fds[STRESS_FD_MAX];
	size_t max_fd = stress_get_file_limit();

	if (max_fd > SIZEOF_ARRAY(fds))
		max_fd = SIZEOF_ARRAY(fds);

	do {
		size_t i, n;

		for (i = 0; i < max_fd; i++) {
			int idx = mwc32() % SIZEOF_ARRAY(open_funcs);

			fds[i] = open_funcs[idx]();

			if (fds[i] < 0)
				break;
			if (!keep_stressing())
				break;
			inc_counter(args);
		}
		n = i;

		for (i = 0; i < n; i++) {
			if (fds[i] < 0)
				break;
			(void)close(fds[i]);
		}
	} while (keep_stressing());

	return EXIT_SUCCESS;
}

stressor_info_t stress_open_info = {
	.stressor = stress_open,
	.class = CLASS_FILESYSTEM | CLASS_OS,
	.help = help
};
