/*
 * Copyright (C) 2013-2021 Canonical, Ltd.
 * Copyright (C)      2022 Colin Ian King.
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
 */
#include "stress-ng.h"

#if defined(__NR_getdents)
#define HAVE_GETDENTS
#endif

#if defined(__NR_getdents64)
#define HAVE_GETDENTS64
#endif

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
	const size_t page_size,
	const int bad_fd);

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
	const size_t page_size,
	const int bad_fd)
{
	int ret = -ENOSYS;
	const size_t n = SIZEOF_ARRAY(getdents_funcs);
	size_t i, j = stress_mwc32() % n;

	for (i = 0; i < n; i++) {
		stress_getdents_func *func = getdents_funcs[j];

		if (func) {
			ret = func(args, path, recurse, depth, page_size, bad_fd);
			if (ret == -ENOSYS)
				getdents_funcs[j] = NULL;
			else
				return ret;
		}
		j++;
		j = j % n;
	}
	pr_fail("%s: getdents: errno=%d (%s)%s\n",
		args->name, -ret, strerror(-ret), stress_fs_type(path));

	return ret;
}

/*
 *  stress_gendent_offset()
 *	increment ptr by offset
 */
static inline void *stress_gendent_offset(void *ptr, const int offset)
{
	register uintptr_t u = (uintptr_t)ptr;

	u += (uintptr_t)offset;
	return (void *)u;
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
	const size_t page_size,
	const int bad_fd)
{
	int fd, rc = 0, nread;
	struct shim_linux_dirent *buf;
	unsigned int buf_sz;

	if (!keep_stressing(args))
		return 0;

	fd = open(path, O_RDONLY | O_DIRECTORY);
	if (fd < 0)
		return 0;

	buf_sz = (unsigned int)((stress_mwc32() % BUF_SIZE) + page_size) & ~(page_size - 1);
	buf = (struct shim_linux_dirent *)malloc((size_t)buf_sz);
	if (!buf)
		goto exit_close;

	/*
	 *  exercise getdents on bad fd
	 */
	VOID_RET(int, shim_getdents((unsigned int)bad_fd, buf, buf_sz));

	/*
	 *  exercise getdents with illegal zero size
	 */
	VOID_RET(int, shim_getdents((unsigned int)fd, buf, 0));

	do {
		struct shim_linux_dirent *ptr = buf;
		struct shim_linux_dirent *end;

		nread = shim_getdents((unsigned int)fd, buf, buf_sz);
		if (nread < 0) {
			rc = -errno;
			goto exit_free;
		}
		if (nread == 0)
			break;

		inc_counter(args);

		if (!recurse || (depth < 1))
			continue;

		end = (struct shim_linux_dirent *)stress_gendent_offset((void *)buf, nread);
		while (ptr < end) {
			struct shim_linux_dirent *d = ptr;
			unsigned char d_type = (unsigned char)*((char *)ptr + d->d_reclen - 1);
			if (d_type == DT_DIR &&
			    !stress_is_dot_filename(d->d_name)) {
				char newpath[PATH_MAX];

				(void)stress_mk_filename(newpath, sizeof(newpath), path, d->d_name);
				rc = stress_getdents_rand(args, newpath, recurse, depth - 1, page_size, bad_fd);
				if (rc < 0)
					goto exit_free;
			}
			ptr = (struct shim_linux_dirent *)stress_gendent_offset((void *)ptr, d->d_reclen);
		}
	} while (keep_stressing(args));
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
	const size_t page_size,
	const int bad_fd)
{
	int fd, rc = 0;
	struct shim_linux_dirent64 *buf;
	unsigned int buf_sz;

	if (!keep_stressing(args))
		return 0;

	fd = open(path, O_RDONLY | O_DIRECTORY);
	if (fd < 0)
		return 0;

	buf_sz = (unsigned int)((stress_mwc32() % BUF_SIZE) + page_size) & ~(page_size - 1);
	buf = (struct shim_linux_dirent64 *)malloc((size_t)buf_sz);
	if (!buf)
		goto exit_close;

	/*
	 *  exercise getdents64 on bad fd
	 */
	VOID_RET(int, shim_getdents64((unsigned int)bad_fd, buf, buf_sz));

	do {
		struct shim_linux_dirent64 *ptr = (struct shim_linux_dirent64 *)buf;
		struct shim_linux_dirent64 *end;
		int nread;

		nread = shim_getdents64((unsigned int)fd, buf, buf_sz);
		if (nread < 0) {
			rc = -errno;
			goto exit_free;
		}
		if (nread == 0)
			break;

		inc_counter(args);

		if (!recurse || (depth < 1))
			continue;

		end = (struct shim_linux_dirent64 *)stress_gendent_offset((void *)buf, nread);
		while (ptr < end) {
			struct shim_linux_dirent64 *d = ptr;

			if (d->d_type == DT_DIR &&
			    !stress_is_dot_filename(d->d_name)) {
				char newpath[PATH_MAX];

				(void)stress_mk_filename(newpath, sizeof(newpath), path, d->d_name);
				rc = stress_getdents_rand(args, newpath, recurse, depth - 1, page_size, bad_fd);
				if (rc < 0)
					goto exit_free;
			}
			ptr = (struct shim_linux_dirent64 *)stress_gendent_offset((void *)ptr, d->d_reclen);
		}
	} while (keep_stressing(args));
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
	const int bad_fd = stress_get_bad_fd();

	stress_set_proc_state(args->name, STRESS_STATE_RUN);

	do {
		int ret;

		ret = stress_getdents_rand(args, "/proc", true, 8, page_size, bad_fd);
		if (ret == -ENOSYS)
			break;
		ret = stress_getdents_rand(args, "/dev", true, 1, page_size, bad_fd);
		if (ret == -ENOSYS)
			break;
		ret = stress_getdents_rand(args, "/tmp", true, 4, page_size, bad_fd);
		if (ret == -ENOSYS)
			break;
		ret = stress_getdents_rand(args, "/sys", true, 8, page_size, bad_fd);
		if (ret == -ENOSYS)
			break;
		ret = stress_getdents_rand(args, "/run", true, 2, page_size, bad_fd);
		if (ret == -ENOSYS)
			break;
	} while (keep_stressing(args));

	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);

	return EXIT_SUCCESS;
}

stressor_info_t stress_getdent_info = {
	.stressor = stress_getdent,
	.class = CLASS_FILESYSTEM | CLASS_OS,
	.verify = VERIFY_ALWAYS,
	.help = help
};
#else
stressor_info_t stress_getdent_info = {
	.stressor = stress_unimplemented,
	.class = CLASS_FILESYSTEM | CLASS_OS,
	.help = help,
	.unimplemented_reason = "built without getdents() or getdents64() support"
};
#endif
