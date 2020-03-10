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
	{ NULL,	"getdent N",	 "start N workers reading directories using getdents" },
	{ NULL,	"getdent-ops N", "stop after N getdents bogo operations" },
	{ NULL,	NULL,		 NULL }
};

#if defined(HAVE_GETDENTS64) || defined(HAVE_GETDENTS)

#define BUF_SIZE	(256 * 1024)

typedef int (stress_getdents_func)(
	const stress_args_t *args,
	const char *path,
	const bool recurse,
	const int depth,
	const size_t page_size);

#if defined(HAVE_GETDENTS)
static stress_getdents_func stress_getdents_dir;
#endif
#if defined(HAVE_GETDENTS64)
static stress_getdents_func stress_getdents64_dir;
#endif

static stress_getdents_func * getdents_funcs[] = {
#if defined(HAVE_GETDENTS)
	stress_getdents_dir,
#endif
#if defined(HAVE_GETDENTS64)
	stress_getdents64_dir,
#endif
};

static inline int stress_getdents_rand(
	const stress_args_t *args,
	const char *path,
	const bool recurse,
	const int depth,
	const size_t page_size)
{
	int ret = -ENOSYS;
	const size_t n = SIZEOF_ARRAY(getdents_funcs);
	size_t i, j = stress_mwc32() % n;

	for (i = 0; i < n; i++) {
		stress_getdents_func *func = getdents_funcs[j];

		if (func) {
			ret = func(args, path, recurse, depth, page_size);
			if (ret == -ENOSYS)
				getdents_funcs[j] = NULL;
			else
				return ret;
		}
		j++;
		j = j % n;
	}
	pr_fail("%s: getdents: errno=%d (%s)\n",
		args->name, -ret, strerror(-ret));

	return ret;
}

#if defined(HAVE_GETDENTS)
/*
 *  stress_getdents_dir()
 *	read directory via the old 32 bit interface
 */
static int stress_getdents_dir(
	const stress_args_t *args,
	const char *path,
	const bool recurse,
	const int depth,
	const size_t page_size)
{
	int fd, rc = 0;
	char *buf;
	size_t buf_sz;

	if (!keep_stressing())
		return 0;

	fd = open(path, O_RDONLY | O_DIRECTORY);
	if (fd < 0)
		return 0;

	buf_sz = ((stress_mwc32() % BUF_SIZE) + page_size) & ~(page_size - 1);
	buf = malloc(buf_sz);
	if (!buf)
		goto exit_close;

	do {
		int nread;
		char *ptr = buf;

		nread = shim_getdents(fd, (struct shim_linux_dirent *)buf, buf_sz);
		if (nread < 0) {
			rc = -errno;
			goto exit_free;
		}
		if (nread == 0)
			break;

		inc_counter(args);

		if (!recurse || depth < 1)
			continue;

		while (ptr < buf + nread) {
			struct shim_linux_dirent *d = (struct shim_linux_dirent *)ptr;
			unsigned char d_type = *(ptr + d->d_reclen - 1);

			if (d_type == DT_DIR &&
			    stress_is_dot_filename(d->d_name)) {
				char newpath[PATH_MAX];

				(void)snprintf(newpath, sizeof(newpath), "%s/%s", path, d->d_name);
				rc = stress_getdents_rand(args, newpath, recurse, depth - 1, page_size);
				if (rc < 0)
					goto exit_free;
			}
			ptr += d->d_reclen;
		}
	} while (keep_stressing());
exit_free:
	free(buf);
exit_close:
	(void)close(fd);

	return rc;
}
#endif

#if defined(HAVE_GETDENTS64)
/*
 *  stress_getdents64_dir()
 *	read directory via the 64 bit interface
 */
static int stress_getdents64_dir(
	const stress_args_t *args,
	const char *path,
	const bool recurse,
	const int depth,
	const size_t page_size)
{
	int fd, rc = 0;
	char *buf;
	size_t buf_sz;

	if (!keep_stressing())
		return 0;

	fd = open(path, O_RDONLY | O_DIRECTORY);
	if (fd < 0)
		return 0;

	buf_sz = ((stress_mwc32() % BUF_SIZE) + page_size) & ~(page_size - 1);
	buf = malloc(buf_sz);
	if (!buf)
		goto exit_close;

	do {
		int nread;
		char *ptr = buf;

		nread = shim_getdents64(fd, (struct shim_linux_dirent64 *)buf, buf_sz);
		if (nread < 0) {
			rc = -errno;
			goto exit_free;
		}
		if (nread == 0)
			break;

		inc_counter(args);

		if (!recurse || depth < 1)
			continue;

		while (ptr < buf + nread) {
			struct shim_linux_dirent64 *d = (struct shim_linux_dirent64 *)ptr;

			if (d->d_type == DT_DIR &&
			    stress_is_dot_filename(d->d_name)) {
				char newpath[PATH_MAX];

				(void)snprintf(newpath, sizeof(newpath), "%s/%s", path, d->d_name);
				rc = stress_getdents_rand(args, newpath, recurse, depth - 1, page_size);
				if (rc < 0)
					goto exit_free;
			}
			ptr += d->d_reclen;
		}
	} while (keep_stressing());
exit_free:
	free(buf);
exit_close:
	(void)close(fd);

	return rc;
}
#endif

/*
 *  stress_getdent
 *	stress reading directories
 */
static int stress_getdent(const stress_args_t *args)
{
	const size_t page_size = args->page_size;

	do {
		int ret;

		ret = stress_getdents_rand(args, "/proc", true, 8, page_size);
		if (ret == -ENOSYS)
			break;
		ret = stress_getdents_rand(args, "/dev", true, 1, page_size);
		if (ret == -ENOSYS)
			break;
		ret = stress_getdents_rand(args, "/tmp", true, 4, page_size);
		if (ret == -ENOSYS)
			break;
		ret = stress_getdents_rand(args, "/sys", true, 8, page_size);
		if (ret == -ENOSYS)
			break;
		ret = stress_getdents_rand(args, "/run", true, 2, page_size);
		if (ret == -ENOSYS)
			break;
	} while (keep_stressing());

	return EXIT_SUCCESS;
}

stressor_info_t stress_getdent_info = {
	.stressor = stress_getdent,
	.class = CLASS_FILESYSTEM | CLASS_OS,
	.help = help
};
#else
stressor_info_t stress_getdent_info = {
	.stressor = stress_not_implemented,
	.class = CLASS_FILESYSTEM | CLASS_OS,
	.help = help
};
#endif
