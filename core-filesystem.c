/*
 * Copyright (C) 2014-2021 Canonical, Ltd.
 * Copyright (C) 2022-2025 Colin Ian King.
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

#include "core-builtin.h"
#include "core-hash.h"
#include "core-sort.h"

#include <ctype.h>
#include <sys/ioctl.h>

#if defined(HAVE_LINUX_FIEMAP_H)
#include <linux/fiemap.h>
#endif

#if (defined(__FreeBSD__) || 	\
     defined(__OpenBSD__)) &&	\
     defined(HAVE_SYS_MOUNT_H)
#include <sys/mount.h>
#endif

#if defined(__FreeBSD__) &&	\
    defined(HAVE_SYS_PARAM_H)
#include <sys/param.h>
#endif

#if defined(HAVE_SYS_STATVFS_H)
#include <sys/statvfs.h>
#endif

#if defined(HAVE_SYS_SYSCTL_H) &&	\
    !defined(__linux__)
#include <sys/sysctl.h>
#endif

#if defined(HAVE_SYS_SYSMACROS_H)
#include <sys/sysmacros.h>
#endif

#if defined(HAVE_LINUX_FS_H)
#include <linux/fs.h>
#endif

#if defined(HAVE_SYS_VFS_H)
#include <sys/vfs.h>
#endif

#if defined(HAVE_LINUX_MAGIC_H)
#include <linux/magic.h>
#endif

typedef struct {
	const unsigned long int	fs_magic;
	const char *		fs_name;
} stress_fs_name_t;

#if defined(HAVE_LINUX_MAGIC_H) &&	\
    defined(HAVE_SYS_STATFS_H)
static const stress_fs_name_t stress_fs_names[] = {
#if defined(ADFS_SUPER_MAGIC)
	{ ADFS_SUPER_MAGIC,	"adfs" },
#endif
#if defined(AFFS_SUPER_MAGIC)
	{ AFFS_SUPER_MAGIC,	"affs" },
#endif
#if defined(AFS_SUPER_MAGIC)
	{ AFS_SUPER_MAGIC,	"afs" },
#endif
#if defined(AUTOFS_SUPER_MAGIC)
	{ AUTOFS_SUPER_MAGIC,	"autofs" },
#endif
#if defined(CEPH_SUPER_MAGIC)
	{ CEPH_SUPER_MAGIC,	"ceph" },
#endif
#if defined(CODA_SUPER_MAGIC)
	{ CODA_SUPER_MAGIC,	"coda" },
#endif
#if defined(CRAMFS_MAGIC)
	{ CRAMFS_MAGIC,		"cramfs" },
#endif
#if defined(CRAMFS_MAGIC_WEND)
	{ CRAMFS_MAGIC,		"cramfs" },
#endif
#if defined(DEBUGFS_MAGIC)
	{ DEBUGFS_MAGIC,	"debugfs" },
#endif
#if defined(SECURITYFS_MAGIC)
	{ SECURITYFS_MAGIC,	"securityfs" },
#endif
#if defined(SELINUX_MAGIC)
	{ SELINUX_MAGIC,	"selinux" },
#endif
#if defined(SMACK_MAGIC)
	{ SMACK_MAGIC,		"smack" },
#endif
#if defined(RAMFS_MAGIC)
	{ RAMFS_MAGIC,		"ramfs" },
#endif
#if defined(TMPFS_MAGIC)
	{ TMPFS_MAGIC,		"tmpfs" },
#endif
#if defined(HUGETLBFS_MAGIC)
	{ HUGETLBFS_MAGIC,	"hugetlbfs" },
#endif
#if defined(SQUASHFS_MAGIC)
	{ SQUASHFS_MAGIC,	"squashfs" },
#endif
#if defined(ECRYPTFS_SUPER_MAGIC)
	{ ECRYPTFS_SUPER_MAGIC,	"ecryptfs" },
#endif
#if defined(EFS_SUPER_MAGIC)
	{ EFS_SUPER_MAGIC,	"efs" },
#endif
#if defined(EROFS_SUPER_MAGIC_V1)
	{ EROFS_SUPER_MAGIC_V1,	"erofs" },
#endif
#if defined(EXT4_SUPER_MAGIC)
	{ EXT4_SUPER_MAGIC,	"ext4" },
#endif
#if defined(EXT3_SUPER_MAGIC)
	{ EXT3_SUPER_MAGIC,	"ext3" },
#endif
#if defined(EXT2_SUPER_MAGIC)
	{ EXT2_SUPER_MAGIC,	"ext2" },
#endif
#if defined(XENFS_SUPER_MAGIC)
	{ XENFS_SUPER_MAGIC,	"xenfs" },
#endif
#if defined(BTRFS_SUPER_MAGIC)
	{ BTRFS_SUPER_MAGIC,	"btrfs" },
#endif
#if defined(NILFS_SUPER_MAGIC)
	{ NILFS_SUPER_MAGIC,	"nilfs" },
#endif
#if defined(F2FS_SUPER_MAGIC)
	{ F2FS_SUPER_MAGIC,	"f2fs" },
#endif
#if defined(HPFS_SUPER_MAGIC)
	{ HPFS_SUPER_MAGIC,	"hpfs" },
#endif
#if defined(ISOFS_SUPER_MAGIC)
	{ ISOFS_SUPER_MAGIC,	"isofs" },
#endif
#if defined(JFFS2_SUPER_MAGIC)
	{ JFFS2_SUPER_MAGIC,	"jffs2" },
#endif
#if defined(XFS_SUPER_MAGIC)
	{ XFS_SUPER_MAGIC,	"xfs" },
#endif
#if defined(PSTOREFS_MAGIC)
	{ PSTOREFS_MAGIC,	"pstorefs" },
#endif
#if defined(EFIVARFS_MAGIC)
	{ EFIVARFS_MAGIC,	"efivars" },
#endif
#if defined(HOSTFS_SUPER_MAGIC)
	{ HOSTFS_SUPER_MAGIC,	"hostfs" },
#endif
#if defined(OVERLAYFS_SUPER_MAGIC)
	{ OVERLAYFS_SUPER_MAGIC, "overlayfs" },
#endif
#if defined(FUSE_SUPER_MAGIC)
	{ FUSE_SUPER_MAGIC,	"fuse" },
#endif
#if defined(BCACHEFS_STATFS_MAGIC)
	{ BCACHEFS_STATFS_MAGIC, "bcachefs" },
#else
	{ 0xca451a4e,		"bacachefs" },
#endif
#if defined(MINIX_SUPER_MAGIC)
	{ MINIX_SUPER_MAGIC,	"minix" },
#endif
#if defined(MINIX_SUPER_MAGIC2)
	{ MINIX_SUPER_MAGIC2,	"minix" },
#endif
#if defined(MINIX2_SUPER_MAGIC)
	{ MINIX2_SUPER_MAGIC,	"minix2" },
#endif
#if defined(MINIX2_SUPER_MAGIC2)
	{ MINIX2_SUPER_MAGIC2,	"minix2" },
#endif
#if defined(MINIX3_SUPER_MAGIC)
	{ MINIX3_SUPER_MAGIC,	"minix3" },
#endif
#if defined(MSDOS_SUPER_MAGIC)
	{ MSDOS_SUPER_MAGIC,	"msdos" },
#endif
#if defined(EXFAT_SUPER_MAGIC)
	{ EXFAT_SUPER_MAGIC,	"exfat" },
#endif
#if defined(NCP_SUPER_MAGIC)
	{ NCP_SUPER_MAGIC,	"ncp" },
#endif
#if defined(NFS_SUPER_MAGIC)
	{ NFS_SUPER_MAGIC,	"nfs" },
#endif
#if defined(OCFS2_SUPER_MAGIC)
	{ OCFS2_SUPER_MAGIC,	"ocfs2" },
#endif
#if defined(OPENPROM_SUPER_MAGIC)
	{ OPENPROM_SUPER_MAGIC,	"openprom" },
#endif
#if defined(QNX4_SUPER_MAGIC)
	{ QNX4_SUPER_MAGIC,	"qnx4" },
#endif
#if defined(QNX6_SUPER_MAGIC)
	{ QNX6_SUPER_MAGIC,	"qnx6" },
#endif
#if defined(AFS_FS_MAGIC)
	{ AFS_FS_MAGIC,		"afs" },
#endif
#if defined(REISERFS_SUPER_MAGIC)
	{ REISERFS_SUPER_MAGIC,	"reiserfs" },
#endif
#if defined(SMB_SUPER_MAGIC)
	{ SMB_SUPER_MAGIC,	"smb" },
#endif
#if defined(CIFS_SUPER_MAGIC)
	{ CIFS_SUPER_MAGIC,	"cifs" },
#endif
#if defined(SMB2_SUPER_MAGIC)
	{ SMB2_SUPER_MAGIC,	"smb2" },
#endif
#if defined(CGROUP_SUPER_MAGIC)
	{ CGROUP_SUPER_MAGIC,	"cgroup" },
#endif
#if defined(CGROUP2_SUPER_MAGIC)
	{ CGROUP2_SUPER_MAGIC,	"cgroup2" },
#endif
#if defined(RDTGROUP_SUPER_MAGIC)
	{ RDTGROUP_SUPER_MAGIC,	"rdtgroup" },
#endif
#if defined(TRACEFS_MAGIC)
	{ TRACEFS_MAGIC,	"tracefs" },
#endif
#if defined(V9FS_MAGIC)
	{ V9FS_MAGIC,		"v9fs" },
#endif
#if defined(BDEVFS_MAGIC)
	{ BDEVFS_MAGIC,		"bdevfs" },
#endif
#if defined(DAXFS_MAGIC)
	{ DAXFS_MAGIC,		"daxfs" },
#endif
#if defined(BINFMTFS_MAGIC)
	{ BINFMTFS_MAGIC,	"binfmtfs" },
#endif
#if defined(DEVPTS_SUPER_MAGIC)
	{ DEVPTS_SUPER_MAGIC,	"devpts" },
#endif
#if defined(BINDERFS_SUPER_MAGIC)
	{ BINDERFS_SUPER_MAGIC,	"binderfs" },
#endif
#if defined(FUTEXFS_SUPER_MAGIC)
	{ FUTEXFS_SUPER_MAGIC,	"futexfs" },
#endif
#if defined(PIPEFS_MAGIC)
	{ PIPEFS_MAGIC,		"pipefs" },
#endif
#if defined(PROC_SUPER_MAGIC)
	{ PROC_SUPER_MAGIC,	"proc" },
#endif
#if defined(SOCKFS_MAGIC)
	{ SOCKFS_MAGIC,		"sockfs" },
#endif
#if defined(SYSFS_MAGIC)
	{ SYSFS_MAGIC,		"sysfs" },
#endif
#if defined(USBDEVICE_SUPER_MAGIC)
	{ USBDEVICE_SUPER_MAGIC, "usbdev" },
#endif
#if defined(MTD_INODE_FS_MAGIC)
	{ MTD_INODE_FS_MAGIC,	"mtd" },
#endif
#if defined(ANON_INODE_FS_MAGIC)
	{ ANON_INODE_FS_MAGIC,	"anon" },
#endif
#if defined(BTRFS_TEST_MAGIC)
	{ BTRFS_TEST_MAGIC,	"btrfs" },
#endif
#if defined(NSFS_MAGIC)
	{ NSFS_MAGIC,		"nsfs" },
#endif
#if defined(BPF_FS_MAGIC)
	{ BPF_FS_MAGIC,		"bpf_fs" },
#endif
#if defined(AAFS_MAGIC)
	{ AAFS_MAGIC,		"aafs" },
#endif
#if defined(ZONEFS_MAGIC)
	{ ZONEFS_MAGIC,		"zonefs" },
#endif
#if defined(UDF_SUPER_MAGIC)
	{ UDF_SUPER_MAGIC,	"udf" },
#endif
#if defined(DMA_BUF_MAGIC)
	{ DMA_BUF_MAGIC,	"dmabuf" },
#endif
#if defined(DEVMEM_MAGIC)
	{ DEVMEM_MAGIC,		"devmem" },
#endif
#if defined(SECRETMEM_MAGIC)
	{ SECRETMEM_MAGIC,	"secretmem" },
#endif
#if defined(PID_FS_MAGIC)
	{ PID_FS_MAGIC,		"pidfs" },
#endif
#if defined(UBIFS_SUPER_MAGIC)
	{ UBIFS_SUPER_MAGIC,	"ubifs" },
#else
	{ 0x24051905,		"ubifs" },
#endif
	{ 0x1badface,		"bfs" },
#if defined(HFS_SUPER_MAGIC)
	{ HFS_SUPER_MAGIC,	"hfs" },
#else
	{ 0x4244,		"hfs" },
#endif
#if defined(HFSPLUS_SUPER_MAGIC)
	{ HFSPLUS_SUPER_MAGIC,	"hfsplus" },
#else
	{ 0x482b,		"hfsplus" },
#endif
#if defined(JFS_SUPER_MAGIC)
	{ JFS_SUPER_MAGIC,	"jfs" },
#else
	{ 0x3153464a,		"jfs" },
#endif
	{ 0x2fc12fc1,		"zfs" },
	{ 0x53464846,		"wsl" },
};
#endif

/*
 *  stress_get_temp_path()
 *	get temporary file path, return "." if null
 */
inline const char *stress_get_temp_path(void)
{
	char *path;

	if (!stress_get_setting("temp-path", &path))
		return ".";
	return path;
}

/*
 *  stress_check_temp_path()
 *	check if temp path is accessible
 */
int stress_check_temp_path(void)
{
	const char *path = stress_get_temp_path();

	if (UNLIKELY(access(path, R_OK | W_OK) < 0)) {
		(void)fprintf(stderr, "aborting: temp-path '%s' must be readable "
			"and writeable\n", path);
		return -1;
	}
	return 0;
}

/*
 *  stress_mk_filename()
 *	generate a full file name from a path and filename
 */
size_t stress_mk_filename(
	char *fullname,
	const size_t fullname_len,
	const char *pathname,
	const char *filename)
{
	/*
	 *  This may not be efficient, but it works. Do not
	 *  be tempted to optimize this, it is not used frequently
	 *  and is not a CPU bottleneck.
	 */
	(void)shim_strscpy(fullname, pathname, fullname_len);
	(void)shim_strlcat(fullname, "/", fullname_len);
	return shim_strlcat(fullname, filename, fullname_len);
}

/*
 *  stress_get_filesystem_size()
 *	get size of free space still available on the
 *	file system where stress temporary path is located,
 *	return 0 if failed
 */
uint64_t stress_get_filesystem_size(void)
{
#if defined(HAVE_SYS_STATVFS_H)
	int rc;
	struct statvfs buf;
	fsblkcnt_t blocks, max_blocks;
	const char *path = stress_get_temp_path();

	if (UNLIKELY(!path))
		return 0;

	(void)shim_memset(&buf, 0, sizeof(buf));
	rc = statvfs(path, &buf);
	if (UNLIKELY(rc < 0))
		return 0;

	max_blocks = (~(fsblkcnt_t)0) / buf.f_bsize;
	blocks = buf.f_bavail;

	if (blocks > max_blocks)
		blocks = max_blocks;

	return (uint64_t)buf.f_bsize * blocks;
#else
	UNEXPECTED
	return 0ULL;
#endif
}

/*
 *  stress_get_filesystem_available_inodes()
 *	get number of free available inodes on the current stress
 *	temporary path, return 0 if failed
 */
uint64_t stress_get_filesystem_available_inodes(void)
{
#if defined(HAVE_SYS_STATVFS_H)
	int rc;
	struct statvfs buf;
	const char *path = stress_get_temp_path();

	if (UNLIKELY(!path))
		return 0;

	(void)shim_memset(&buf, 0, sizeof(buf));
	rc = statvfs(path, &buf);
	if (UNLIKELY(rc < 0))
		return 0;

	return (uint64_t)buf.f_favail;
#else
	UNEXPECTED
	return 0ULL;
#endif
}

/*
 *  stress_fs_usage_bytes()
 *	report how much file sysytem is used per instance
 *	and in total compared to file system space available
 */
void stress_fs_usage_bytes(
	stress_args_t *args,
	const off_t fs_size_per_instance,
	const off_t fs_size_total)
{
	const off_t total_fs_size = (off_t)stress_get_filesystem_size();
	char s1[32], s2[32], s3[32];

	if (total_fs_size > 0) {
		pr_inf("%s: using %s file system space per stressor instance (total %s of %s available file system space)\n",
			args->name,
			stress_uint64_to_str(s1, sizeof(s1), (uint64_t)fs_size_per_instance, 2, true),
			stress_uint64_to_str(s2, sizeof(s2), (uint64_t)fs_size_total, 2, true),
			stress_uint64_to_str(s3, sizeof(s3), total_fs_size, 2, true));
	}
}


/*
 *  stress_set_nonblock()
 *	try to make fd non-blocking
 */
int stress_set_nonblock(const int fd)
{
	int flags;
#if defined(O_NONBLOCK)

	if ((flags = fcntl(fd, F_GETFL, 0)) < 0)
		flags = 0;
	return fcntl(fd, F_SETFL, O_NONBLOCK | flags);
#else
	UNEXPECTED
	flags = 1;
	return ioctl(fd, FIOBIO, &flags);
#endif
}

/*
 *  stress_base36_encode_uint64()
 *	encode 64 bit hash of filename into a unique base 36
 *	filename of up to 13 chars long + 1 char eos
 */
static inline void OPTIMIZE3 stress_base36_encode_uint64(char dst[14], uint64_t val)
{
	static const char b36[] = "abcdefghijklmnopqrstuvwxyz0123456789";
	const int b = 36;
	char *ptr = dst;

	while (val) {
		*ptr++ = b36[val % b];
		val /= b;
	}
	*ptr = '\0';
}

/*
 *  stress_temp_hash_truncate()
 *	filenames may be too long for the underlying filesystem
 *	so workaround this by hashing them into a 64 bit hex
 *	filename.
 */
static void stress_temp_hash_truncate(char *filename)
{
	size_t f_namemax = 16;
	size_t len = strlen(filename);
#if defined(HAVE_SYS_STATVFS_H)
	struct statvfs buf;

	(void)shim_memset(&buf, 0, sizeof(buf));
	if (statvfs(stress_get_temp_path(), &buf) == 0)
		f_namemax = buf.f_namemax;
#endif

	if (strlen(filename) > f_namemax) {
		const uint32_t upper = stress_hash_jenkin((uint8_t *)filename, len);
		const uint32_t lower = stress_hash_pjw(filename);
		const uint64_t val = ((uint64_t)upper << 32) | lower;

		stress_base36_encode_uint64(filename, val);
	}
}

/*
 *  stress_temp_filename()
 *      construct a temp filename
 */
int stress_temp_filename(
	char *path,
	const size_t len,
	const char *name,
	const pid_t pid,
	const uint32_t instance,
	const uint64_t magic)
{
	char directoryname[PATH_MAX];
	char filename[PATH_MAX];

	(void)snprintf(directoryname, sizeof(directoryname),
		"tmp-%s-%s-%d-%" PRIu32,
		g_app_name, name, (int)pid, instance);
	stress_temp_hash_truncate(directoryname);

	(void)snprintf(filename, sizeof(filename),
		"%s-%s-%d-%" PRIu32 "-%" PRIu64,
		g_app_name, name, (int)pid, instance, magic);
	stress_temp_hash_truncate(filename);

	return snprintf(path, len, "%s/%s/%s",
		stress_get_temp_path(), directoryname, filename);
}

/*
 *  stress_temp_filename_args()
 *      construct a temp filename using info from args
 */
int stress_temp_filename_args(
	stress_args_t *args,
	char *path,
	const size_t len,
	const uint64_t magic)
{
	return stress_temp_filename(path, len, args->name,
		args->pid, args->instance, magic);
}

/*
 *  stress_temp_dir()
 *	create a temporary directory name
 */
int stress_temp_dir(
	char *path,
	const size_t len,
	const char *name,
	const pid_t pid,
	const uint32_t instance)
{
	char directoryname[256];
	int l;

	(void)snprintf(directoryname, sizeof(directoryname),
		"tmp-%s-%s-%d-%" PRIu32,
		g_app_name, name, (int)pid, instance);
	stress_temp_hash_truncate(directoryname);

	l = snprintf(path, len, "%s/%s",
		stress_get_temp_path(), directoryname);
	return l;
}

/*
 *  stress_temp_dir_args()
 *	create a temporary directory name using info from args
 */
int stress_temp_dir_args(
	stress_args_t *args,
	char *path,
	const size_t len)
{
	return stress_temp_dir(path, len,
		args->name, args->pid, args->instance);
}

/*
 *   stress_temp_dir_mk()
 *	create a temporary directory
 */
int stress_temp_dir_mk(
	const char *name,
	const pid_t pid,
	const uint32_t instance)
{
	int ret;
	char tmp[PATH_MAX];

	stress_temp_dir(tmp, sizeof(tmp), name, pid, instance);
	ret = mkdir(tmp, S_IRWXU);
	if (UNLIKELY(ret < 0)) {
		ret = -errno;
		pr_fail("%s: mkdir '%s' failed, errno=%d (%s)\n",
			name, tmp, errno, strerror(errno));
		(void)shim_rmdir(tmp);
	}

	return ret;
}

/*
 *   stress_temp_dir_mk_args()
 *	create a temporary director using info from args
 */
int stress_temp_dir_mk_args(stress_args_t *args)
{
	return stress_temp_dir_mk(args->name, args->pid, args->instance);
}

/*
 *  stress_temp_dir_rm()
 *	remove a temporary directory
 */
int stress_temp_dir_rm(
	const char *name,
	const pid_t pid,
	const uint32_t instance)
{
	int ret;
	char tmp[PATH_MAX + 1];

	stress_temp_dir(tmp, sizeof(tmp), name, pid, instance);
	ret = shim_rmdir(tmp);
	if (UNLIKELY(ret < 0)) {
		ret = -errno;
		pr_fail("%s: rmdir '%s' failed, errno=%d (%s)\n",
			name, tmp, errno, strerror(errno));
	}

	return ret;
}

/*
 *  stress_temp_dir_rm_args()
 *	remove a temporary directory using info from args
 */
int stress_temp_dir_rm_args(stress_args_t *args)
{
	return stress_temp_dir_rm(args->name, args->pid, args->instance);
}

/*
 *  stress_system_write()
 *	write a buffer to a /sys or /proc entry
 */
ssize_t stress_system_write(
	const char *path,
	const char *buf,
	const size_t buf_len)
{
	int fd;
	ssize_t ret;

	if (UNLIKELY(!path || !buf))
		return -EINVAL;
	if (UNLIKELY(buf_len == 0))
		return -EINVAL;

	fd = open(path, O_WRONLY);
	if (UNLIKELY(fd < 0))
		return -errno;
	ret = write(fd, buf, buf_len);
	if (ret < (ssize_t)buf_len)
		ret = -errno;
	(void)close(fd);

	return ret;
}

/*
 *  stress_system_discard()
 *	read and discard contents of a given file
 */
ssize_t stress_system_discard(const char *path)
{
	int fd;
	ssize_t ret;

	if (UNLIKELY(!path))
		return -EINVAL;
	fd = open(path, O_RDONLY);
	if (UNLIKELY(fd < 0))
		return -errno;
	ret = stress_read_discard(fd);
	(void)close(fd);

	return ret;
}

/*
 *  stress_system_read()
 *	read a buffer from a /sys or /proc entry
 */
ssize_t stress_system_read(
	const char *path,
	char *buf,
	const size_t buf_len)
{
	int fd;
	ssize_t ret;

	if (UNLIKELY(!path || !buf))
		return -EINVAL;
	if (UNLIKELY(buf_len == 0))
		return -EINVAL;

	(void)shim_memset(buf, 0, buf_len);

	fd = open(path, O_RDONLY);
	if (UNLIKELY(fd < 0))
		return -errno;
	ret = read(fd, buf, buf_len);
	if (UNLIKELY(ret < 0)) {
		buf[0] = '\0';
		ret = -errno;
	}
	(void)close(fd);
	if ((ssize_t)buf_len == ret)
		buf[buf_len - 1] = '\0';

	return ret;
}

/*
 *  stress_get_max_file_limit()
 *	get max number of files that the current
 *	process can open not counting the files that
 *	may already been opened.
 */
size_t stress_get_max_file_limit(void)
{
#if defined(HAVE_GETDTABLESIZE)
	int tablesize;
#endif
#if defined(RLIMIT_NOFILE)
	struct rlimit rlim;
#endif
	size_t max_rlim = SIZE_MAX;
	size_t max_sysconf;

#if defined(HAVE_GETDTABLESIZE)
	/* try the simple way first */
	tablesize = getdtablesize();
	if (tablesize > 0)
		return (size_t)tablesize;
#endif
#if defined(RLIMIT_NOFILE)
	if (!getrlimit(RLIMIT_NOFILE, &rlim))
		max_rlim = (size_t)rlim.rlim_cur;
#endif
#if defined(_SC_OPEN_MAX)
	{
		const long int open_max = sysconf(_SC_OPEN_MAX);

		max_sysconf = (open_max > 0) ? (size_t)open_max : SIZE_MAX;
	}
#else
	max_sysconf = SIZE_MAX;
	UNEXPECTED
#endif
	/* return the lowest of these two */
	return STRESS_MINIMUM(max_rlim, max_sysconf);
}

/*
 *  stress_get_open_count(void)
 *	get number of open file descriptors
 */
static inline size_t stress_get_open_count(void)
{
#if defined(__linux__)
	DIR *dir;
	struct dirent *d;
	size_t n = 0;

	dir = opendir("/proc/self/fd");
	if (!dir)
		return (size_t)-1;

	while ((d = readdir(dir)) != NULL) {
		if (isdigit((unsigned char)d->d_name[0]))
			n++;
	}
	(void)closedir(dir);

	/*
	 * opendir used one extra fd that is now
	 * closed, so take that off the total
	 */
	return (n > 1) ? (n - 1) : n;
#else
	return 0;
#endif
}

/*
 *  stress_get_file_limit()
 *	get max number of files that the current
 *	process can open excluding currently opened
 *	files.
 */
size_t stress_get_file_limit(void)
{
	struct rlimit rlim;
	size_t last_opened, opened, max = 65536;	/* initial guess */

	if (!getrlimit(RLIMIT_NOFILE, &rlim))
		max = (size_t)rlim.rlim_cur;

	last_opened = 0;

	opened = stress_get_open_count();
	if (opened == 0) {
		size_t i;

		/* Determine max number of free file descriptors we have */
		for (i = 0; i < max; i++) {
			if (fcntl((int)i, F_GETFL) > -1) {
				opened++;
				last_opened = i;
			} else {
				/*
				 *  Hack: Over 250 contiguously closed files
				 *  most probably indicates we're at the point
				 *  were no more opened file descriptors are
				 *  going to be found, so bail out rather then
				 *  scanning for any more opened files
				 */
				if (i - last_opened > 250)
					break;
			}
		}
	}
	return max - opened;
}

/*
 *  stress_get_bad_fd()
 *	return a fd that will produce -EINVAL when using it
 *	either because it is not open or it is just out of range
 */
int stress_get_bad_fd(void)
{
#if defined(RLIMIT_NOFILE) &&	\
    defined(F_GETFL)
	struct rlimit rlim;

	(void)shim_memset(&rlim, 0, sizeof(rlim));

	if (getrlimit(RLIMIT_NOFILE, &rlim) == 0) {
		if (rlim.rlim_cur < INT_MAX - 1) {
			if (fcntl((int)rlim.rlim_cur, F_GETFL) == -1) {
				return (int)rlim.rlim_cur + 1;
			}
		}
	}
#elif defined(F_GETFL)
	int i;

	for (i = 2048; i > fileno(stdout); i--) {
		if (fcntl((int)i, F_GETFL) == -1)
			return i;
	}
#else
	UNEXPECTED
#endif
	return -1;
}

/*
 *  stress_is_a_pipe()
 *	return true if fd is a pipe
 */
bool stress_is_a_pipe(const int fd)
{
	struct stat statbuf;

	if (shim_fstat(fd, &statbuf) != 0)
		return false;
	if (S_ISFIFO(statbuf.st_mode))
		return true;
	return false;
}

#if defined(F_SETPIPE_SZ)
/*
 *  stress_check_max_pipe_size()
 *	check if the given pipe size is allowed
 */
static inline int stress_check_max_pipe_size(
	const size_t sz,
	const size_t page_size)
{
	int fds[2], rc = 0;

	if (UNLIKELY(sz < page_size))
		return -1;

	if (UNLIKELY(pipe(fds) < 0))
		return -1;

	if (fcntl(fds[0], F_SETPIPE_SZ, sz) < 0)
		rc = -1;

	(void)close(fds[0]);
	(void)close(fds[1]);
	return rc;
}
#endif

/*
 *  stress_probe_max_pipe_size()
 *	determine the maximum allowed pipe size
 */
size_t stress_probe_max_pipe_size(void)
{
	static size_t max_pipe_size;

#if defined(F_SETPIPE_SZ)
	ssize_t ret;
	size_t i, prev_sz, sz, min, max;
	char buf[64];
	size_t page_size;
#endif
	/* Already determined? returned cached size */
	if (max_pipe_size)
		return max_pipe_size;

#if defined(F_SETPIPE_SZ)
	page_size = stress_get_page_size();

	/*
	 *  Try and find maximum pipe size directly
	 */
	ret = stress_system_read("/proc/sys/fs/pipe-max-size", buf, sizeof(buf));
	if (ret > 0) {
		if (sscanf(buf, "%zu", &sz) == 1)
			if (!stress_check_max_pipe_size(sz, page_size))
				goto ret;
	}

	/*
	 *  Need to find size by binary chop probing
	 */
	min = page_size;
	max = INT_MAX;
	prev_sz = 0;
	sz = 0;
	for (i = 0; i < 64; i++) {
		sz = min + (max - min) / 2;
		if (prev_sz == sz)
			return sz;
		prev_sz = sz;
		if (stress_check_max_pipe_size(sz, page_size) == 0) {
			min = sz;
		} else {
			max = sz;
		}
	}
ret:
	max_pipe_size = sz;
#else
	max_pipe_size = stress_get_page_size();
#endif
	return max_pipe_size;
}

/*
 *  stress_dirent_list_free()
 *	free dirent list
 */
void stress_dirent_list_free(struct dirent **dlist, const int n)
{
	if (LIKELY(dlist != NULL)) {
		int i;

		for (i = 0; i < n; i++) {
			if (dlist[i])
				free(dlist[i]);
		}
		free(dlist);
	}
}

/*
 *  stress_dirent_list_prune()
 *	remove . and .. files from directory list
 */
int stress_dirent_list_prune(struct dirent **dlist, const int n)
{
	int i, j;

	if (UNLIKELY(!dlist))
		return -1;

	for (i = 0, j = 0; i < n; i++) {
		if (dlist[i]) {
			if (stress_is_dot_filename(dlist[i]->d_name)) {
				free(dlist[i]);
				dlist[i] = NULL;
			} else {
				dlist[j] = dlist[i];
				j++;
			}
		}
	}
	return j;
}

/*
 *  stress_read_discard(cont int fd)
 *	read and discard contents of file fd
 */
ssize_t stress_read_discard(const int fd)
{
	ssize_t rbytes = 0, ret;

	do {
		char buffer[4096];

		ret = read(fd, buffer, sizeof(buffer));
		if (ret > 0)
			rbytes += ret;
	} while (ret > 0);

	return rbytes;
}

/*
 *  stress_read_buffer()
 *	In addition to read() this function makes sure all bytes have been
 *	read. You're also able to ignore EINTR signals which could happen
 *	on alarm() in the parent process.
 */
ssize_t stress_read_buffer(
	const int fd,
	void *buffer,
	const ssize_t size,
	const bool ignore_sig_eintr)
{
	ssize_t rbytes = 0, ret;

	if (UNLIKELY(!buffer || (size < 1)))
		return -1;
	do {
		char *ptr = ((char *)buffer) + rbytes;
ignore_eintr:
		ret = read(fd, (void *)ptr, (size_t)(size - rbytes));
		if (ignore_sig_eintr && (ret < 0) && (errno == EINTR))
			goto ignore_eintr;
		if (ret > 0)
			rbytes += ret;
	} while ((ret > 0) && (rbytes != size));

	return (ret <= 0) ? ret : rbytes;
}

/*
 *  stress_write_buffer()
 *	In addition to write() this function makes sure all bytes have been
 *	written. You're also able to ignore EINTR interrupts which could happen
 *	on alarm() in the parent process.
 */
ssize_t stress_write_buffer(
	const int fd,
	const void *buffer,
	const ssize_t size,
	const bool ignore_sig_eintr)
{
	ssize_t wbytes = 0, ret;

	if (UNLIKELY(!buffer || (size < 1)))
		return -1;

	do {
		const void *ptr = (void *)((uintptr_t)buffer + wbytes);
ignore_eintr:
		ret = write(fd, ptr, (size_t)(size - wbytes));
		/* retry if interrupted */
		if (ignore_sig_eintr && (ret < 0) && (errno == EINTR))
			goto ignore_eintr;
		if (ret > 0)
			wbytes += ret;
	} while ((ret > 0) && (wbytes != size));

	return (ret <= 0) ? ret : wbytes;
}

/*
 *  stress_read_fdinfo()
 *	read the fdinfo for a specific pid's fd, Linux only
 */
int stress_read_fdinfo(const pid_t pid, const int fd)
{
#if defined(__linux__)
	char path[PATH_MAX];
	char buf[4096];

	(void)snprintf(path, sizeof(path), "/proc/%d/fdinfo/%d",
		(int)pid, fd);

	return (int)stress_system_read(path, buf, sizeof(buf));
#else
	(void)pid;
	(void)fd;

	return 0;
#endif
}

/*
 *  stress_get_extents()
 *	try to determine number extents in a file
 */
size_t stress_get_extents(const int fd)
{
#if defined(FS_IOC_FIEMAP) &&	\
    defined(HAVE_LINUX_FIEMAP_H)
	struct fiemap fiemap;

	(void)shim_memset(&fiemap, 0, sizeof(fiemap));
	fiemap.fm_length = ~0UL;

	/* Find out how many extents there are */
	if (ioctl(fd, FS_IOC_FIEMAP, &fiemap) < 0)
		return 0;

	return fiemap.fm_mapped_extents;
#else
	UNEXPECTED
	(void)fd;

	return 0;
#endif
}

#if defined(HAVE_LINUX_MAGIC_H) &&	\
    defined(HAVE_SYS_STATFS_H)
/*
 *  stress_fs_magic_to_name()
 *	return the human readable file system type based on fs type magic
 */
static const char *stress_fs_magic_to_name(const unsigned long int fs_magic)
{
	static char unknown[32];
	size_t i;

	for (i = 0; i < SIZEOF_ARRAY(stress_fs_names); i++) {
		if (stress_fs_names[i].fs_magic == fs_magic)
			return stress_fs_names[i].fs_name;
	}
	(void)snprintf(unknown, sizeof(unknown), "unknown 0x%lx", fs_magic);

	return unknown;
}
#endif

#if defined(HAVE_SYS_SYSMACROS_H) &&	\
    defined(__linux__)
/*
 *  stress_find_partition_dev()
 *	find major device name of device with major/minor number
 *	via the partition info
 */
static bool stress_find_partition_dev(
	const unsigned int devmajor,
	const unsigned int devminor,
	char *name,
	const size_t name_len)
{
	char buf[1024];
	FILE *fp;
	bool found = false;

	if (!name)
		return false;
	if (name_len < 1)
		return false;

	*name = '\0';
	fp = fopen("/proc/partitions", "r");
	if (!fp)
		return false;

	while (fgets(buf, sizeof(buf), fp) != NULL) {
		uint64_t blocks;
		char devname[name_len + 1];
		unsigned int pmajor, pminor;

		if (sscanf(buf, "%u %u %" SCNu64 " %128s", &pmajor, &pminor, &blocks, devname) == 4) {
			if ((devmajor == pmajor) && (devminor == pminor)) {
				(void)shim_strscpy(name, devname, name_len);
				found = true;
				break;
			}
		}
	}
	(void)fclose(fp);
	return found;
}

#endif

/*
 *  stress_get_fs_dev_model()
 *	file model name of device that the file is on
 */
static const char *stress_get_fs_dev_model(const char *filename)
{
#if defined(HAVE_SYS_SYSMACROS_H) &&	\
    defined(__linux__)
	struct stat statbuf;
	char dev[1024];
	static char buf[1024 + 16];
	char path[PATH_MAX];

	if (UNLIKELY(!filename))
		return NULL;
	if (UNLIKELY(shim_stat(filename, &statbuf) < 0))
		return NULL;

	if (!stress_find_partition_dev(major(statbuf.st_dev), 0, dev, sizeof(dev)))
		return NULL;

	(void)snprintf(path, sizeof(path), "/sys/block/%s/device/model", dev);
	if (stress_system_read(path, buf, sizeof(buf)) > 0) {
		char *ptr;

		for (ptr = buf; *ptr; ptr++) {
			if (*ptr == '\n') {
				*ptr = '\0';
				ptr--;
				break;
			}
		}
		while (ptr >= buf && *ptr == ' ') {
			*ptr = '\0';
			ptr--;
		}
		/* resolved to device model */
		return buf;
	}
	/* can't resolve, return device */
	(void)snprintf(buf, sizeof(buf), "/dev/%s", dev);
	(void)shim_strscpy(dev, buf, sizeof(dev));
	return buf;
#else
	(void)filename;
	return NULL;
#endif
}

/*
 *  stress_get_fs_info()
 *	for a given filename, determine the filesystem it is stored
 *	on and return filesystem type and number of blocks
 */
const char *stress_get_fs_info(const char *filename, uintmax_t *blocks)
{
#if defined(HAVE_LINUX_MAGIC_H) &&	\
    defined(HAVE_SYS_STATFS_H)
	struct statfs buf;

	*blocks = (intmax_t)0;
	if (UNLIKELY(!filename))
		return NULL;
	if (UNLIKELY(statfs(filename, &buf) != 0))
		return NULL;
	*blocks = (uintmax_t)buf.f_bavail;
	return stress_fs_magic_to_name((unsigned long int)buf.f_type);
#elif (defined(__FreeBSD__) &&		\
       defined(HAVE_SYS_MOUNT_H) &&	\
       defined(HAVE_SYS_PARAM_H)) ||	\
      (defined(__OpenBSD__) &&		\
       defined(HAVE_SYS_MOUNT_H))
	struct statfs buf;
	static char tmp[80];

	*blocks = (intmax_t)0;
	if (statfs(filename, &buf) != 0)
		return NULL;
	*blocks = (uintmax_t)buf.f_bavail;
	(void)shim_strscpy(tmp, buf.f_fstypename, sizeof(tmp));
	return tmp;
#else
	(void)filename;
	*blocks = (intmax_t)0;
	return NULL;
#endif
}

/*
 *  stress_get_fs_type()
 *	return the file system type that the given filename is in
 */
const char *stress_get_fs_type(const char *filename)
{
	uintmax_t blocks;
	const char *fs_name = stress_get_fs_info(filename, &blocks);
	const char *model = stress_get_fs_dev_model(filename);

	if (fs_name) {
		static char tmp[256];

		(void)snprintf(tmp, sizeof(tmp), ", filesystem type: %s (%" PRIuMAX" blocks available%s%s)",
			fs_name, blocks,
			model ? ", " : "",
			model ? model : "");
		return tmp;
	}
	return "";
}

/*
 *  stress_close_fds()
 *	close an array of file descriptors
 */
void stress_close_fds(int *fds, const size_t n)
{
	size_t i, j;

	if (UNLIKELY(!fds))
		return;
	if (UNLIKELY(n < 1))
		return;

	qsort(fds, n, sizeof(*fds), stress_sort_cmp_fwd_int);
	for (j = 0; j < n - 1; j++) {
		if (fds[j] >= 0)
			break;
	}
	for (i = j; i < n - 1; i++) {
		if (fds[i] + 1 != fds[i + 1])
			goto close_slow;
	}
	if (shim_close_range(fds[j], fds[n - 1], 0) == 0)
		return;

close_slow:
	for (i = j; i < n; i++)
		(void)close(fds[i]);
}

/*
 *  stress_file_rw_hint_short()
 *	hint that file data opened on fd has short lifetime
 */
void stress_file_rw_hint_short(const int fd)
{
#if defined(F_SET_FILE_RW_HINT) &&	\
    defined(RWH_WRITE_LIFE_SHORT)
	uint64_t hint = RWH_WRITE_LIFE_SHORT;

	VOID_RET(int, fcntl(fd, F_SET_FILE_RW_HINT, &hint));
#else
	(void)fd;
#endif
}

/*
 *  stress_unset_chattr_flags()
 *	disable all chattr flags including the immutable flas
 */
void stress_unset_chattr_flags(const char *pathname)
{
#if defined(__linux__) &&	\
    defined(_IOW)

#define SHIM_EXT2_IMMUTABLE_FL		0x00000010
#define SHIM_EXT2_IOC_SETFLAGS		_IOW('f', 2, long int)

	int fd;
	unsigned long int flags = 0;

	if (UNLIKELY(!pathname))
		return;
	fd = open(pathname, O_RDONLY);
	if (UNLIKELY(fd < 0))
		return;

	VOID_RET(int, ioctl(fd, SHIM_EXT2_IOC_SETFLAGS, &flags));
	(void)close(fd);

#undef SHIM_EXT2_IMMUTABLE_FL
#undef SHIM_EXT2_IOC_SETFLAGS

#else
	(void)pathname;
#endif
}

/*
 *  stress_dot_dirent_filter()
 *  	filter out dot files . and .. for scandir(), dirent and
 *  	d->d_name are valid
 */
static int CONST OPTIMIZE3 stress_dot_dirent_filter(const struct dirent *d)
{
	if (d->d_name[0] == '.') {
		if (d->d_name[1] == '\0')
			return 0;
		if ((d->d_name[1] == '.') && LIKELY((d->d_name[2] == '\0')))
			return 0;
	}
	return 1;
}

/*
 *  stress_is_dot_filename()
 *	is filename "." or "..", name maybe null
 */
bool CONST OPTIMIZE3 stress_is_dot_filename(const char *name)
{
	if (UNLIKELY(!name))
		return false;
	if (name[0] == '.') {
		if (name[1] == '\0')
			return true;
		if ((name[1] == '.') && LIKELY((name[2] == '\0')))
			return true;
	}
	return false;
}

/*
 *  stress_unset_inode_flags()
 *	unset the inode flag bits specified in flag
 */
static void stress_unset_inode_flags(const char *filename, const int flag)
{
#if defined(FS_IOC_SETFLAGS)
	int fd;
        const long int new_flag = 0;

	if (UNLIKELY(!filename))
		return;
	fd = open(filename, O_RDWR | flag);
	if (UNLIKELY(fd < 0))
		return;

        VOID_RET(int, ioctl(fd, FS_IOC_SETFLAGS, &new_flag));
	(void)close(fd);
#else
	(void)filename;
	(void)flag;
#endif
}

/*
 *  stress_clean_dir_files()
 *  	recursively delete files in directories
 */
static void stress_clean_dir_files(
	const char *temp_path,
	const size_t temp_path_len,
	char *path,
	const size_t path_posn)
{
	struct stat statbuf;
	char *ptr, *end;
	int n;
	struct dirent **names = NULL;

	if (UNLIKELY(!temp_path || !path))
		return;

	if (UNLIKELY(shim_stat(path, &statbuf) < 0)) {
		pr_dbg("stress-ng: failed to stat %s, errno=%d (%s)\n", path, errno, strerror(errno));
		return;
	}

	/* We don't follow symlinks */
	if (S_ISLNK(statbuf.st_mode))
		return;

	/* We don't remove paths with .. in */
	if (strstr(path, ".."))
		return;

	/* We don't remove paths that our out of the scope */
	if (strncmp(path, temp_path, temp_path_len))
		return;

	n = scandir(path, &names, stress_dot_dirent_filter, alphasort);
	if (n < 0) {
		(void)shim_rmdir(path);
		return;
	}

	ptr = path + path_posn;
	end = path + PATH_MAX;

	while (n--) {
		size_t name_len = strlen(names[n]->d_name) + 1;
#if !defined(DT_DIR) ||	\
    !defined(DT_LNK) ||	\
    !defined(DT_REG)
		int ret;
#endif

		/* No more space */
		if (ptr + name_len > end) {
			free(names[n]);
			continue;
		}

		(void)snprintf(ptr, (size_t)(end - ptr), "/%s", names[n]->d_name);
		name_len = strlen(ptr);

#if defined(DT_DIR) &&	\
    defined(DT_LNK) &&	\
    defined(DT_REG)
		/* Modern fast d_type method */
		switch (names[n]->d_type) {
		case DT_DIR:
			free(names[n]);
#if defined(O_DIRECTORY)
			stress_unset_inode_flags(temp_path, O_DIRECTORY);
#endif
			stress_unset_chattr_flags(path);
			stress_clean_dir_files(temp_path, temp_path_len, path, path_posn + name_len);
			(void)shim_rmdir(path);
			break;
		case DT_LNK:
		case DT_REG:
			free(names[n]);
			stress_unset_inode_flags(temp_path, 0);
			stress_unset_chattr_flags(path);
			if (strstr(path, "swap"))
				(void)stress_swapoff(path);
			(void)shim_unlink(path);
			break;
		default:
			free(names[n]);
			break;
		}
#else
		/* Slower stat method */
		free(names[n]);
		ret = shim_stat(path, &statbuf);
		if (ret < 0)
			continue;

		if ((statbuf.st_mode & S_IFMT) == S_IFDIR) {
#if defined(O_DIRECTORY)
			stress_unset_inode_flags(temp_path, O_DIRECTORY);
#endif
			stress_unset_chattr_flags(temp_path);
			stress_clean_dir_files(temp_path, temp_path_len, path, path_posn + name_len);
			(void)shim_rmdir(path);
		} else if (((statbuf.st_mode & S_IFMT) == S_IFLNK) ||
			   ((statbuf.st_mode & S_IFMT) == S_IFREG)) {
			stress_unset_inode_flags(temp_path, 0);
			stress_unset_chattr_flags(temp_path);
			(void)unlink(path);
		}
#endif
	}
	*ptr = '\0';
	free(names);
	(void)shim_rmdir(path);
}

/*
 *  stress_clean_dir()
 *	perform tidy up of any residual temp files; this
 *	happens if a stressor was terminated before it could
 *	tidy itself up, e.g. OOM'd or KILL'd
 */
void stress_clean_dir(
	const char *name,
	const pid_t pid,
	const uint32_t instance)
{
	const char *temp_path = stress_get_temp_path();
	const size_t temp_path_len = strlen(temp_path);

	if (LIKELY(name != NULL)) {
		char path[PATH_MAX];

		(void)stress_temp_dir(path, sizeof(path), name, pid, instance);
		if (access(path, F_OK) == 0) {
			pr_dbg("%s: removing temporary files in %s\n", name, path);
			stress_clean_dir_files(temp_path, temp_path_len, path, strlen(path));
		}
	}
}
