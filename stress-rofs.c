/*
 * Copyright (C) 2026      Colin Ian King.
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
#include "core-attribute.h"
#include "core-mmap.h"
#include "core-mounts.h"

#if defined(HAVE_LINUX_FS_H)
#include <linux/fs.h>
#endif

#include <sys/ioctl.h>

#if defined(HAVE_SYS_FILE_H)
#include <sys/file.h>
#endif

#if defined(HAVE_LINUX_MAGIC_H)
#include <linux/magic.h>
#endif

#if !defined(CONFIGFS_MAGIC)
#define CONFIGFS_MAGIC	0x62656570
#endif

#if defined(HAVE_SYS_VFS_H) &&  	\
    defined(HAVE_SYS_STATVFS_H) &&	\
    defined(HAVE_STATFS) &&     	\
    defined(__linux__)
#include <sys/vfs.h>
#include <sys/statvfs.h>
#define HAVE_ROFS_MOUNT
#endif

#if defined(HAVE_SYS_XATTR_H)
#include <sys/xattr.h>
#undef HAVE_ATTR_XATTR_H
#elif defined(HAVE_ATTR_XATTR_H)
#include <attr/xattr.h>
#endif
/*  Sanity check */
#if defined(HAVE_SYS_XATTR_H) &&        \
    defined(HAVE_ATTR_XATTR_H)
#error cannot have both HAVE_SYS_XATTR_H and HAVE_ATTR_XATTR_H
#endif

#define READ_BUF_SIZE	(512)
#define MOUNTS_MAX	(2048)

static const stress_help_t help[] = {
	{ NULL,	"rofs N",	  "start N workers exercising read-only filesystem" },
	{ NULL,	"rofs-dir path",  "specify mount point of read-only filesystem" },
	{ NULL,	"rofs-ops N",	  "stop after N rofs bogo operations" },
	{ NULL,	NULL,		  NULL }
};

typedef struct stress_rofs_info {
	struct stress_rofs_info *next;
	char d_name[256];
	struct stat statbuf;
	int stat_ret;
	bool writable;
} stress_rofs_info_t;

typedef int (*stress_rofs_file_func_t)(stress_args_t *args, const char *path, double *count, stress_rofs_info_t *info);

typedef struct stress_rofs_method {
	const char *name;
	stress_rofs_file_func_t func;
} stress_rofs_method_t;

/*
 *  stres_rofs_file_open()
 *	open a file, read-only
 */
static int stres_rofs_file_open(
	stress_args_t *args,
	const char *path,
	stress_rofs_info_t *info)
{
	int fd;

	fd = open(path, O_RDONLY);
	if (fd < 0) {
		switch (errno) {
		case EPERM:
		case EACCES:
		case ENOMEM:
			return -1;
		case ENOENT:
			if ((info->statbuf.st_mode & S_IFMT) == S_IFLNK)
				return -1;
			break;
		default:
			break;
		}
		pr_fail("%s: open on '%s' failed, errno=%d (%s)\n",
			args->name, path, errno, strerror(errno));
		return -1;
	}
	return fd;
}

/*
 *  stress_rofs_file_open
 *  	exercise lstat() on file
 */
static int stress_rofs_file_lstat(
	stress_args_t *args,
	const char *path,
	double *count,
	stress_rofs_info_t *info)
{
	struct stat *statbuf = &info->statbuf;

	if (lstat(path, statbuf) == 0)
		(*count) += 1.0;
	else
		pr_fail("%s: lstat on '%s' failed, errno=%d (%s)\n",
			args->name, path, errno, strerror(errno));
	return 0;
}

#if defined(AT_EMPTY_PATH) &&   \
    defined(AT_SYMLINK_NOFOLLOW)
/*
 *  stress_rofs_file_statx()
 *	exercise statx() on a file
 */
static int stress_rofs_file_statx(
	stress_args_t *args,
	const char *path,
	double *count,
	stress_rofs_info_t *info)
{
        shim_statx_t bufx;

	(void)info;

	if (shim_statx(AT_EMPTY_PATH, path, AT_SYMLINK_NOFOLLOW, SHIM_STATX_ALL, &bufx) == 0)
		(*count) += 1.0;
	else if (errno != ENOSYS)
		pr_fail("%s: statx on '%s' failed, errno=%d (%s)\n",
			args->name, path, errno, strerror(errno));
	return 0;
}
#endif

/*
 *  stress_rofs_file_access()
 *	exercise access() on a file
 */
static int stress_rofs_file_access(
	stress_args_t *args,
	const char *path,
	double *count,
	stress_rofs_info_t *info)
{
	(void)info;

	if (access(path, W_OK) == 0) {

		if ((info->statbuf.st_mode & S_IFMT) != S_IFLNK) {
			int fd;

			/*
			 *  Potential Time of check time of use
			 *  issue here, but we're now trying to
			 *  try and write-only open a read-only
			 *  file, so lets let that slide
			 */
			fd = open(path, O_WRONLY | O_APPEND);
			if (fd != -1) {
				(void)close(fd);
				info->writable = true;
				pr_fail("%s: access W_OK on '%s' unexpectedly succeeded\n",
					args->name, path);
				return -1;
			}
		}
	}
	(*count) += 1.0;
	return 0;
}

/*
 *  stress_rofs_file_mmap()
 *	exercise mmap on a file
 */
static int stress_rofs_file_mmap(
	stress_args_t *args,
	const char *path,
	double *count,
	stress_rofs_info_t *info)
{
	char *data;
	int fd;
	size_t i;
	const size_t page_size = args->page_size;
	const off_t size = (off_t)((info->statbuf.st_size > 0) ? info->statbuf.st_size : 0);
	const off_t mask = ~(off_t)(page_size - 1);
	size_t n_mmaps;

	/* try just regular files */
	if ((info->statbuf.st_mode & S_IFMT) != S_IFREG)
		return 0;

	if (size == 0)
		return 0;

	fd = stres_rofs_file_open(args, path, info);
	if (fd < 0)
		return -1;

	n_mmaps = (size >> 1) / page_size;
	if (n_mmaps < 1)
		n_mmaps = 1;
	if (n_mmaps > 8)
		n_mmaps = 8;

	/*
	 *  mmap file in page size hunks and read data
	 */
	for (i = 0; stress_continue(args) && (i < n_mmaps); i++) {
		const size_t rand_off = (off_t)((size > 0) ? stress_mwc64modn((uint64_t)size) : 0);

		data = (char *)mmap(NULL, page_size, PROT_READ, MAP_PRIVATE, fd, rand_off & mask);
		if (data != MAP_FAILED) {
			register const char *ptr_end = data + page_size;
			register volatile char *ptr;

			(*count) += 1.0;
			for (ptr = data; ptr < ptr_end; ptr++)
				(void)*ptr;

			(void)munmap((void *)data, page_size);
		}
	}

	/*
	 *  check that write mmapping fails on read-only file
	 */
	data = (char *)stress_mmap_populate(NULL, (size_t)size, PROT_WRITE, MAP_SHARED, fd, 0);
	if (data != MAP_FAILED) {
		pr_inf("%s: mmap on '%s' using PROT_WRITE unexpectedly succeeded\n",
			args->name, path);
		(void)munmap((void *)data, size);
		(void)close(fd);
		return -1;
	} else {
		(*count) += 1.0;
	}
	(void)close(fd);
	return 0;
}

typedef off_t (*stress_rofs_lseek_func_t)(const int fd, const off_t size,
					  const off_t curr_off, const off_t rand_off);

static off_t stress_rofs_lseek_set(
	const int fd,
	const off_t size,
	const off_t curr_off,
	const off_t rand_off)
{
	(void)size;
	(void)curr_off;

	return lseek(fd, rand_off, SEEK_SET);
}

static off_t stress_rofs_lseek_cur(
	const int fd,
	const off_t size,
	const off_t curr_off,
	const off_t rand_off)
{
	(void)size;

	return lseek(fd, rand_off - curr_off, SEEK_CUR);
}

static off_t stress_rofs_lseek_end(
	const int fd,
	const off_t size,
	const off_t curr_off,
	const off_t rand_off)
{
	(void)size;
	(void)curr_off;

	return lseek(fd, size - rand_off, SEEK_CUR);
}

#if defined(SEEK_HOLE)
static off_t stress_rofs_lseek_hole(
	const int fd,
	const off_t size,
	const off_t curr_off,
	const off_t rand_off)
{
	(void)size;
	(void)curr_off;

	return lseek(fd, rand_off, SEEK_HOLE);
}
#endif

#if defined(SEEK_DATA)
static off_t stress_rofs_lseek_data(
	const int fd,
	const off_t size,
	const off_t curr_off,
	const off_t rand_off)
{
	(void)size;
	(void)curr_off;

	return lseek(fd, rand_off, SEEK_DATA);
}
#endif

static const stress_rofs_lseek_func_t stress_rofs_lseek_funcs[] = {
	stress_rofs_lseek_set,
	stress_rofs_lseek_cur,
	stress_rofs_lseek_end,
#if defined(SEEK_HOLE)
	stress_rofs_lseek_hole,
#endif
#if defined(SEEK_DATA)
	stress_rofs_lseek_data,
#endif
};

static off_t stress_rofs_lseek(const int fd, stress_rofs_info_t *info)
{
	off_t size;
	off_t curr_off = lseek(fd, 0, SEEK_CUR);
	off_t rand_off;
	size_t rnd = stress_mwcsizemodn(SIZEOF_ARRAY(stress_rofs_lseek_funcs));

	size = (off_t)((info->statbuf.st_size > 0) ? info->statbuf.st_size : 0);
	curr_off = (curr_off > 0) ? curr_off : 0;
	rand_off = (off_t)((size > 0) ? stress_mwc64modn((uint64_t)size) : 0);

	return stress_rofs_lseek_funcs[rnd](fd, size, curr_off, rand_off);
}

/*
 *  stress_rofs_file_read()
 *	random positioned 512 byte reads
 */
static int stress_rofs_file_read(
	stress_args_t *args,
	const char *path,
	double *count,
	stress_rofs_info_t *info)
{
	int fd;
	int i;
	const int n = info->statbuf.st_size > 512 ? 16 : 1;
	char buffer[READ_BUF_SIZE] ALIGN64;

	fd = stres_rofs_file_open(args, path, info);
	if (fd < 0)
		return -1;

	for (i = 0; stress_continue(args) && (i < n); i++) {
		ssize_t ret;

		(void)stress_rofs_lseek(fd, info);

		ret = read(fd, buffer, READ_BUF_SIZE);
		if ((ret > 0) && (ret <= READ_BUF_SIZE))
			(*count) += 1.0;
	}
	(void)close(fd);
	return 0;
}

/*
 *  stress_rofs_file_lseek()
 *	lseeks
 */
static int stress_rofs_file_lseek(
	stress_args_t *args,
	const char *path,
	double *count,
	stress_rofs_info_t *info)
{
	int fd;
	int i;
	const int n = info->statbuf.st_size > 512 ? 16 : 1;

	fd = stres_rofs_file_open(args, path, info);
	if (fd < 0)
		return -1;

	for (i = 0; stress_continue(args) && (i < n); i++) {
		if (stress_rofs_lseek(fd, info) >= 0)
			(*count) += 1.0;
	}

	(void)close(fd);
	return 0;
}

#if (defined(HAVE_SYS_XATTR_H) ||	\
     defined(HAVE_ATTR_XATTR_H)) &&	\
    defined(HAVE_GETXATTR)
/*
 *  stress_rofs_file_listxattr()
 *	exercise xattribute list
 */
static int stress_rofs_file_listxattr(
	stress_args_t *args,
	const char *path,
	double *count,
	stress_rofs_info_t *info)
{
	char buffer[4086];

	(void)info;

	if (shim_listxattr(path, buffer, sizeof(buffer)) >= 0)
		(*count) += 1.0;
	else {
		switch (errno) {
		case ENOSYS:
		case EOPNOTSUPP:
			return 0;
		case ENOENT:
			if ((info->statbuf.st_mode & S_IFMT) == S_IFLNK)
				return 0;
			break;
		default:
			break;
		}
		pr_fail("%s: listxattr on '%s' failed, errno=%d (%s)\n",
			args->name, path, errno, strerror(errno));
		return -1;
	}
	return 0;
}
#endif

#if defined(HAVE_SYS_FILE_H) &&	\
    defined(HAVE_FLOCK) &&	\
    defined(LOCK_EX) &&		\
    defined(LOCK_UN)
/*
 *  stress_rofs_file_flock()
 *	exercise exclusive file lock
 */
static int stress_rofs_file_flock(
	stress_args_t *args,
	const char *path,
	double *count,
	stress_rofs_info_t *info)
{
	int fd;

	(void)info;
	(void)args;

	fd = stres_rofs_file_open(args, path, info);
	if (fd < 0)
		return -1;

	if (flock(fd, LOCK_EX) == 0)
		(*count) += 1.0;
	(void)flock(fd, LOCK_UN);

	(void)close(fd);
	return 0;
}
#endif

/*
 *  stress_rofs_file_valid_open_close()
 *	exercise valid_file open/close
 */
static int stress_rofs_file_valid_open_close(
	stress_args_t *args,
	const char *path,
	double *count,
	stress_rofs_info_t *info)
{
	int fd;

	(void)info;

	fd = stres_rofs_file_open(args, path, info);
	if (fd < 0)
		return -1;
	(*count) += 1.0;
	(void)close(fd);
	return 0;
}

/*
 *  stress_rofs_file_invalid_open_close()
 *	exercise invalid file open/close
 */
static int stress_rofs_file_invalid_open_close(
	stress_args_t *args,
	const char *path,
	double *count,
	stress_rofs_info_t *info)
{
	static const int flags[] = {
		O_CREAT | O_RDWR,
		O_RDWR,
		O_APPEND,
#if defined(O_SYNC)
		O_SYNC,
#endif
#if defined(O_DIRECT)
		O_DIRECT,
#endif
#if defined(O_DSYNC)
		O_DSYNC,
#endif
#if defined(O_EXCL)
		O_CREAT | O_RDWR | O_EXCL,
#endif
#if defined(O_TRUNC) && 0
		O_CREAT | O_RDWR | O_TRUNC,
#endif
	};
	int fd;
	int idx = stress_mwcsizemodn(SIZEOF_ARRAY(flags));

	if ((info->statbuf.st_mode & S_IFMT) == S_IFLNK)
		return 0;
	if ((info->statbuf.st_mode & S_IFMT) != S_IFREG)
		return 0;

	(*count) += 1.0;

	fd = open(path, flags[idx], 0007);
	if (fd >= 0) {
		ssize_t lret;
		char data[1];

		if (lseek(fd, 0, SEEK_SET) != 0) {
			(void)close(fd);
			return 0;
		}

		lret = read(fd, data, sizeof(data));
		if (lret != sizeof(data)) {
			(void)close(fd);
			return 0;
		}

		if (lseek(fd, 0, SEEK_SET) != 0) {
			(void)close(fd);
			return 0;
		}
		lret = write(fd, data, sizeof(data));
		if (lret != -1) {
			pr_inf("%s read-only file '%s' is writable\n",
				args->name, path);
			(void)close(fd);
			return -1;
		}
		(void)close(fd);
	}
	return 0;
}

/*
 *  stress_rofs_file_fsync()
 *	exercise (invalid) fsync
 */
static int stress_rofs_file_fsync(
	stress_args_t *args,
	const char *path,
	double *count,
	stress_rofs_info_t *info)
{
	int fd;

	(void)info;

	fd = stres_rofs_file_open(args, path, info);
	if (fd < 0)
		return -1;
	fsync(fd);

	(*count) += 1.0;
	(void)close(fd);
	return 0;
}

typedef int (*stress_rofs_file_ioctl_func_t)(const int fd);

#if defined(FS_IOC_GETVERSION)
static int stress_rofs_file_ioctl_ioc_get_version(const int fd)
{
	int version;

	return ioctl(fd, FS_IOC_GETVERSION, &version);
}
#endif

#if defined(FS_IOC_GETFSLABEL) &&	\
    defined(FSLABEL_MAX)
static int stress_rofs_file_ioctl_ioc_getfslabel(const int fd)
{
	char label[FSLABEL_MAX];

	return ioctl(fd, FS_IOC_GETFSLABEL, label);
}
#endif

#if defined(FS_IOC_GETFLAGS)
static int stress_rofs_file_ioctl_ioc_getflags(const int fd)
{
	int attr = 0;

	return ioctl(fd, FS_IOC_GETFLAGS, &attr);
}
#endif

#if defined(FIGETBSZ)
static int stress_rofs_file_ioctl_figetbsz(const int fd)
{
	int isz;

	return ioctl(fd, FIGETBSZ, &isz);
}
#endif

#if defined(FIONREAD)
static int stress_rofs_file_ioctl_fionread(const int fd)
{
	int isz;

	return ioctl(fd, FIONREAD, &isz);
}
#endif

#if defined(FIOQSIZE)
static int stress_rofs_file_ioctl_fioqsize(const int fd)
{
	shim_loff_t sz;

	return ioctl(fd, FIOQSIZE, &sz);
}
#endif

static const stress_rofs_file_ioctl_func_t stress_rofs_file_ioctl_funcs[] = {
#if defined(FS_IOC_GETVERSION)
	stress_rofs_file_ioctl_ioc_get_version,
#endif
#if defined(FS_IOC_GETFSLABEL) &&	\
    defined(FSLABEL_MAX)
	stress_rofs_file_ioctl_ioc_getfslabel,
#endif
#if defined(FS_IOC_GETFLAGS)
	stress_rofs_file_ioctl_ioc_getflags,
#endif
#if defined(FIGETBSZ)
	stress_rofs_file_ioctl_figetbsz,
#endif
#if defined(FIONREAD)
	stress_rofs_file_ioctl_fionread,
#endif
#if defined(FIOQSIZE)
	stress_rofs_file_ioctl_fioqsize,
#endif
};

/*
 *  stress_rofs_file_ioctl()
 *	exercise various randomly selected ioctl calls
 */
static int stress_rofs_file_ioctl(
	stress_args_t *args,
	const char *path,
	double *count,
	stress_rofs_info_t *info)
{
	int fd;
	const size_t n_ioctl_funcs = SIZEOF_ARRAY(stress_rofs_file_ioctl_funcs);
	size_t idx;

	if (n_ioctl_funcs < 1)
		return 0;

	fd = stres_rofs_file_open(args, path, info);
	if (fd < 0)
		return -1;

	idx = stress_mwcsizemodn(n_ioctl_funcs);
	if (stress_rofs_file_ioctl_funcs[idx](fd) == 0)
		(*count) += 1.0;

	(void)close(fd);
	return 0;
}

static const stress_rofs_method_t  stress_rofs_methods[] = {
	{ "lstat",		stress_rofs_file_lstat },
	{ "valid open/close",	stress_rofs_file_valid_open_close },
	{ "invalid open/close",	stress_rofs_file_invalid_open_close },
#if defined(AT_EMPTY_PATH) &&   \
    defined(AT_SYMLINK_NOFOLLOW)
	{ "statx",		stress_rofs_file_statx },
#endif
	{ "mmap read", 	 	stress_rofs_file_mmap },
	{ "512-byte read", 	stress_rofs_file_read },
	{ "lseek",      	stress_rofs_file_lseek },
	{ "access",		stress_rofs_file_access },
#if (defined(HAVE_SYS_XATTR_H) ||	\
     defined(HAVE_ATTR_XATTR_H)) &&	\
    defined(HAVE_GETXATTR)
	{ "listxattr",	stress_rofs_file_listxattr },
#endif
#if defined(HAVE_SYS_FILE_H) &&	\
    defined(HAVE_FLOCK) &&	\
    defined(LOCK_EX) &&		\
    defined(LOCK_UN)
	{ "flock",      stress_rofs_file_flock },
#endif
	{ "fsync",	stress_rofs_file_fsync },
	{ "ioctl",	stress_rofs_file_ioctl },
};

static stress_metrics_t stress_rofs_metrics[SIZEOF_ARRAY(stress_rofs_methods)];

static int stress_rofs_scandir(stress_args_t *args, const char *path, stress_rofs_info_t *dir_info)
{
	struct dirent *de;
	DIR *dp;
	int rc = 0;
	size_t i;
	stress_rofs_info_t *head = NULL;
	stress_rofs_info_t *info;
	double n;
	char new_path[PATH_MAX + 256];
	int write_count;

	if (!stress_continue(args))
		return rc;

	dp = opendir(path);
	if (!dp) {
		if (errno == EACCES)
			return 0;
		pr_fail("%s: path '%s' is not accessible, errno=%d (%s)\n",
			args->name, path, errno, strerror(errno));
		return -1;
	}

	if (access(path, W_OK) == 0) {
		if (dir_info)
			dir_info->writable = true;
		pr_fail("%s: path '%s' is writable\n", args->name, path);
		(void)closedir(dp);
		return -1;
	}

	while ((de = readdir(dp)) != NULL) {
		if (!de->d_name[0])
			continue;
		if (stress_fs_filename_dotty(de->d_name))
			continue;

		info = calloc(1, sizeof(*info));
		if (!info)
			break;
		(void)shim_strscpy(info->d_name, de->d_name, sizeof(info->d_name));
		info->next = head;
		head = info;
	}
	(void)closedir(dp);

	if (!head)
		return rc;

	for (i = 0; stress_continue(args) && (i < SIZEOF_ARRAY(stress_rofs_methods)); i++) {
		const stress_rofs_file_func_t func = stress_rofs_methods[i].func;
		double t;

		n = 0.0;
		t = stress_time_now();
		for (info = head; info; info = info->next, n = n + 1.0) {
			(void)snprintf(new_path, sizeof(new_path), "%s/%s", path, info->d_name);
			func(args, new_path, &n, info);
			stress_rofs_metrics[i].duration += stress_time_now() - t;
			stress_rofs_metrics[i].count += n;
		}
	}

	stress_bogo_inc(args);

	for (info = head; info; info = info->next) {
		if ((info->statbuf.st_mode & S_IFMT) == S_IFDIR) {
			(void)snprintf(new_path, sizeof(new_path), "%s/%s", path, info->d_name);
			if (stress_rofs_scandir(args, new_path, info) < 0)
				rc = -1;
		}
	}

	write_count = 0;
	info = head;
	while (info) {
		stress_rofs_info_t *next = info->next;

		if (info->writable)
			write_count++;

		free(info);
		info = next;
	}
	if (write_count > 0) {
		pr_fail("%s: %d files were writable in '%s'\n", args->name, write_count, path);
		return -1;
	}
	return rc;
}

/*
 *  stress_rofs()
 *	stress system with fstat
 */
static int stress_rofs(stress_args_t *args)
{
	NOCLOBBER int ret = EXIT_FAILURE;
	size_t i;
	int j;
	char *paths[MOUNTS_MAX];
#if defined(HAVE_ROFS_MOUNT)
	char *mnts[MOUNTS_MAX];
	int n_mnts = 0;
#endif
	int n_paths = 0;

	(void)memset(paths, 0, sizeof(paths));

	for (i = 0; i < SIZEOF_ARRAY(stress_rofs_metrics); i++) {
		stress_rofs_metrics[i].count = 0.0;
		stress_rofs_metrics[i].duration = 0.0;
	}

	if (stress_setting_get("rofs-dir", &paths[n_paths])) {
		/* rofs-dir provided, check if exists, readable, not-writable */
		if (access(paths[n_paths], F_OK) != 0) {
			pr_inf("%s: rofs-dir '%s' is not accessible, errno=%d (%s), skipping stressor\n",
				args->name, paths[n_paths], errno, strerror(errno));
			return EXIT_NO_RESOURCE;
		}
		if (access(paths[n_paths], R_OK) != 0) {
			pr_inf("%s: rofs-dir '%s' is not readable, errno=%d (%s), skipping stressor\n",
				args->name, paths[n_paths], errno, strerror(errno));
			return EXIT_NO_RESOURCE;
		}
		if (access(paths[n_paths], W_OK) == 0) {
			pr_inf("%s: rofs-dir '%s' is not read-only, skipping stressor\n",
				args->name, paths[n_paths]);
			return EXIT_NO_RESOURCE;
		}
		n_paths++;
	}

#if defined(HAVE_ROFS_MOUNT)
	if (n_paths == 0) {
		n_mnts = stress_mount_get(mnts, MOUNTS_MAX);
		for (j = 0; j < n_mnts; j++) {
			struct statfs statfsbuf;

			if (access(mnts[j], R_OK) != 0)
				continue;
			if (statfs(mnts[j], &statfsbuf) != 0)
				continue;
#if defined(CONFIGFS_MAGIC)
			if (statfsbuf.f_type == CONFIGFS_MAGIC)
				continue;
#endif
#if defined(CGROUP_SUPER_MAGIC)
			if (statfsbuf.f_type == CGROUP_SUPER_MAGIC)
				continue;
#endif
			if (strncmp(mnts[j], "/sys", 4) == 0)
				continue;

			if (statfsbuf.f_flags & ST_RDONLY)
				paths[n_paths++] = mnts[j];
		}
		if (n_paths == 0) {
			pr_inf("%s: specify read-only file systems using --rofs-dir "
			       "and/or mount one or more read-only file systems, "
			       "skipping stressor\n", args->name);
			stress_mount_free(mnts, n_mnts);
			return EXIT_NO_RESOURCE;
		}
	}
#else
	if (n_paths == 0) {
		pr_inf("%s: specify read-only file systems using --rofs-dir, "
		       "skipping stressor\n", args->name);
		return EXIT_NO_RESOURCE;
	}
#endif

	if (stress_instance_zero(args)) {
		size_t n = 0;
		char *str;

		for (j = 0; j < n_paths; j++)
			n += strlen(paths[j]) + 2;

		str = calloc(n, sizeof(*str));
		if (str) {
			for (j = 0; j < n_paths; j++)  {
				if (j > 0)
					shim_strlcat(str, ", ", n);
				shim_strlcat(str, paths[j], n);
			}
			pr_inf("%s: exercising %s\n", args->name, str);
			free(str);
		}
	}

	stress_proc_state_set(args->name, STRESS_STATE_SYNC_WAIT);
	stress_sync_start_wait(args);
	stress_proc_state_set(args->name, STRESS_STATE_RUN);

	j = args->instance % (int)n_paths;
	do {
		if (j >= n_paths)
			j = 0;
		if (stress_rofs_scandir(args, paths[j], NULL) < 0)
			break;
		j++;
	} while (stress_continue(args));

	ret = EXIT_SUCCESS;
	stress_proc_state_set(args->name, STRESS_STATE_DEINIT);

	for (i = 0; i < SIZEOF_ARRAY(stress_rofs_methods); i++) {
		char str[64];
		const double rate = (stress_rofs_metrics[i].duration > 0.0) ?
			stress_rofs_metrics[i].count / stress_rofs_metrics[i].duration : 0.0;

		(void)snprintf(str, sizeof(str), "%s ops per sec", stress_rofs_methods[i].name);
		stress_metrics_set(args, str, rate, STRESS_METRIC_HARMONIC_MEAN);

	}

#if defined(HAVE_ROFS_MOUNT)
	stress_mount_free(mnts, n_mnts);
#endif

	return ret;
}

static const stress_opt_t opts[] = {
	{ OPT_rofs_dir, "rofs-dir", TYPE_ID_STR, 0, 0, NULL },
	END_OPT,
};

const stressor_info_t stress_rofs_info = {
	.stressor = stress_rofs,
	.classifier = CLASS_FILESYSTEM | CLASS_OS,
	.opts = opts,
	.verify = VERIFY_ALWAYS,
	.help = help,
	.max_metrics_items = 12,
};
