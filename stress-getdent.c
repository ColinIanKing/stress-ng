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

#include "stress-ng.h"

#if defined(STRESS_GETDENT)

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <unistd.h>
#include <fcntl.h>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/types.h>

#define BUF_SIZE	(256 * 1024)

struct linux_dirent {
	unsigned long  d_ino;     	/* Inode number */
	unsigned long  d_off;     	/* Offset to next linux_dirent */
	unsigned short d_reclen;  	/* Length of this linux_dirent */
	char           d_name[];  	/* Filename (null-terminated) */
};

struct linux_dirent64 {
	ino64_t        d_ino;		/* 64-bit inode number */
	off64_t        d_off;		/* 64-bit offset to next structure */
	unsigned short d_reclen;	/* Size of this dirent */
	unsigned char  d_type;		/* File type */
	char           d_name[];	/* Filename (null-terminated) */
};

typedef int (getdents_func)(
	const char *name, const char *path,
	const bool recurse, const int depth,
	uint64_t *counter, uint64_t max_ops,
	const size_t page_size);

#if defined(__NR_getdents)
static getdents_func stress_getdents_dir;
#endif
#if defined(__NR_getdents64)
static getdents_func stress_getdents64_dir;
#endif

static getdents_func * getdents_funcs[] = {
#if defined(__NR_getdents)
	stress_getdents_dir,
#endif
#if defined(__NR_getdents64)
	stress_getdents64_dir,
#endif
};

static inline int stress_getdents_rand(
	const char *name,
	const char *path,
	const bool recurse,
	const int depth,
	uint64_t *counter,
	uint64_t max_ops,
	const size_t page_size)
{
	int ret = -ENOSYS;
	const size_t n = SIZEOF_ARRAY(getdents_funcs);
	size_t i, j = mwc32() % n;

	for (i = 0; i < n; i++) {
		getdents_func *func = getdents_funcs[j];

		if (func) {
			ret = func(name, path, recurse, depth, counter, max_ops, page_size);
			if (ret == -ENOSYS)
				getdents_funcs[j] = NULL;
			else
				return ret;
		}
		j++;
		j = j % n;
	}
	pr_fail(stderr, "%s: getdents: errno=%d (%s)\n",
		name, -ret, strerror(-ret));

	return ret;
}

/*
 *  getdents syscall
 */
static inline int sys_getdents(
	unsigned int fd,
	struct linux_dirent *dirp,
	unsigned int count)
{
#if defined(__NR_getdents)
        return syscall(__NR_getdents, fd, dirp, count);
#else
	(void)fd;
	(void)dirp;
	(void)count;

	errno = ENOSYS;
	return -1;
#endif
}

/*
 *  getdents64 syscall
 */
static inline int sys_getdents64(
	unsigned int fd,
	struct linux_dirent64 *dirp,
	unsigned int count)
{
#if defined(__NR_getdents64)
        return syscall(__NR_getdents64, fd, dirp, count);
#else
	(void)fd;
	(void)dirp;
	(void)count;

	errno = ENOSYS;
	return -1;
#endif
}

#if defined(__NR_getdents)
/*
 *  stress_getdents_dir()
 *	read directory via the old 32 bit interface
 */
static int stress_getdents_dir(
	const char *name,
	const char *path,
	const bool recurse,
	const int depth,
	uint64_t *counter,
	uint64_t max_ops,
	const size_t page_size)
{
	int fd, rc = 0;
	char *buf;
	size_t buf_sz;

	if (!opt_do_run || (max_ops && *counter >= max_ops))
		return 0;

	fd = open(path, O_RDONLY | O_DIRECTORY);
	if (fd < 0)
		return 0;

	buf_sz = ((mwc32() % BUF_SIZE) + page_size) & ~(page_size - 1);
	buf = malloc(buf_sz);
	if (!buf)
		goto exit_close;

	do {
		int nread;
		char *ptr = buf;

		nread = sys_getdents(fd, (struct linux_dirent *)buf, buf_sz);
		if (nread < 0) {
			rc = -errno;
			goto exit_free;
		}
		if (nread == 0)
			break;

		(*counter)++;

		if (!recurse || depth < 1)
			continue;

		while (ptr < buf + nread) {
			struct linux_dirent *d = (struct linux_dirent *)ptr;
			unsigned char d_type = *(ptr + d->d_reclen - 1);

			if (d_type == DT_DIR &&
			    strcmp(d->d_name, ".") &&
			    strcmp(d->d_name, "..")) {
				char newpath[PATH_MAX];

				snprintf(newpath, sizeof(newpath), "%s/%s", path, d->d_name);
				rc = stress_getdents_rand(name, newpath, recurse, depth - 1,
					counter, max_ops, page_size);
				if (rc < 0)
					goto exit_free;
			}
			ptr += d->d_reclen;
		}
	} while (opt_do_run && (!max_ops || *counter < max_ops));
exit_free:
	free(buf);
exit_close:
	(void)close(fd);

	return rc;
}
#endif

#if defined(__NR_getdents64)
/*
 *  stress_getdents64_dir()
 *	read directory via the 64 bit interface
 */
static int stress_getdents64_dir(
	const char *name,
	const char *path,
	const bool recurse,
	const int depth,
	uint64_t *counter,
	uint64_t max_ops,
	const size_t page_size)
{
	int fd, rc = 0;
	char *buf;
	size_t buf_sz;

	if (!opt_do_run || (max_ops && *counter >= max_ops))
		return 0;

	fd = open(path, O_RDONLY | O_DIRECTORY);
	if (fd < 0)
		return 0;

	buf_sz = ((mwc32() % BUF_SIZE) + page_size) & ~(page_size - 1);
	buf = malloc(buf_sz);
	if (!buf)
		goto exit_close;

	do {
		int nread;
		char *ptr = buf;

		nread = sys_getdents64(fd, (struct linux_dirent64 *)buf, buf_sz);
		if (nread < 0) {
			rc = -errno;
			goto exit_free;
		}
		if (nread == 0)
			break;

		(*counter)++;

		if (!recurse || depth < 1)
			continue;

		while (ptr < buf + nread) {
			struct linux_dirent64 *d = (struct linux_dirent64 *)ptr;

			if (d->d_type == DT_DIR &&
			    strcmp(d->d_name, ".") &&
			    strcmp(d->d_name, "..")) {
				char newpath[PATH_MAX];

				snprintf(newpath, sizeof(newpath), "%s/%s", path, d->d_name);
				rc = stress_getdents_rand(name, newpath, recurse, depth - 1,
					counter, max_ops, page_size);
				if (rc < 0)
					goto exit_free;
			}
			ptr += d->d_reclen;
		}
	} while (opt_do_run && (!max_ops || *counter < max_ops));
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
int stress_getdent(
	uint64_t *const counter,
	const uint32_t instance,
	const uint64_t max_ops,
	const char *name)
{
	(void)instance;
	(void)name;

	size_t page_size = stress_get_pagesize();

	do {
		int ret;

		ret = stress_getdents_rand(name, "/proc", true, 8, counter, max_ops, page_size);
		if (ret == -ENOSYS)
			break;
		ret = stress_getdents_rand(name, "/dev", true, 1, counter, max_ops, page_size);
		if (ret == -ENOSYS)
			break;
		ret = stress_getdents_rand(name, "/tmp", true, 4, counter, max_ops, page_size);
		if (ret == -ENOSYS)
			break;
		ret = stress_getdents_rand(name, "/sys", true, 8, counter, max_ops, page_size);
		if (ret == -ENOSYS)
			break;
		ret = stress_getdents_rand(name, "/run", true, 2, counter, max_ops, page_size);
		if (ret == -ENOSYS)
			break;
	} while (opt_do_run && (!max_ops || *counter < max_ops));

	return EXIT_SUCCESS;
}
#endif
