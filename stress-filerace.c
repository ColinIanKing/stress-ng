/*
 * Copyright (C) 2025      Colin Ian King.
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
#include "core-killpid.h"
#include "core-out-of-memory.h"

#include <sys/types.h>
#include <dirent.h>
#include <sys/file.h>
#include <sys/ioctl.h>

#if defined(HAVE_LINUX_FS_H)
#include <linux/fs.h>
#endif

#define MAX_FILERACE_PROCS	(7)
#define MAX_FDS			(64)

#define OFFSET_MASK		~((off_t)511ULL)

typedef void (*stress_filerace_fops_t)(const int fd, const char *filename);

static uid_t uid;
static gid_t gid;

static const stress_help_t help[] = {
	{ NULL,	"filerace N",		"start N workers that attemp to race file system calls" },
	{ NULL,	"filerace-ops N",	"stop after N filerace bogo operations" },
	{ NULL,	NULL,			NULL }
};

#if defined(SIGIO)
static void MLOCKED_TEXT stress_sigio_handler(int signum)
{
	(void)signum;
}
#endif

static void stress_filerace_write_random_uint32(const int fd)
{
	uint32_t val = stress_mwc32();

	if (lseek(fd, (off_t)val, SEEK_SET) >= 0)
		VOID_RET(ssize_t, write(fd, &val, sizeof(val)));
}

static void stress_filerace_read_random_uint32(const int fd)
{
	const uint32_t val = stress_mwc32();

	if (lseek(fd, (off_t)val, SEEK_SET) >= 0) {
		uint32_t tmp;

		VOID_RET(ssize_t, write(fd, &tmp, sizeof(tmp)));
	}
}


/*
 *  stress_filerace_tidy()
 *	clean up residual files
 */
static void stress_filerace_tidy(const char *path)
{
	DIR *dir;

	dir = opendir(path);
	if (dir) {
		const struct dirent *d;

		while ((d = readdir(dir)) != NULL) {
			char filename[PATH_MAX];

			if (stress_is_dot_filename(d->d_name))
				continue;
			(void)stress_mk_filename(filename, sizeof(filename),
				path, d->d_name);
			(void)shim_unlink(filename);
			(void)shim_rmdir(filename);
		}
		(void)closedir(dir);
	}
	(void)shim_rmdir(path);
}

static void stress_filerace_fstat(const int fd, const char *filename)
{
	struct stat buf;

	(void)filename;
	VOID_RET(int, fstat(fd, &buf));
}

static void stress_filerace_lseek_set(const int fd, const char *filename)
{
	(void)filename;
	VOID_RET(off_t, lseek(fd, (off_t)stress_mwc32(), SEEK_SET));
}

static void stress_filerace_lseek_end(const int fd, const char *filename)
{
	(void)filename;
	VOID_RET(off_t, lseek(fd, 0, SEEK_END));
}

static void stress_filerace_fchmod(const int fd, const char *filename)
{
	(void)filename;
	VOID_RET(int, fchmod(fd, S_IRUSR | S_IWUSR));
}

static void stress_filerace_fchown(const int fd, const char *filename)
{
	(void)filename;
	VOID_RET(int, fchown(fd, uid, gid));
}

#if defined(F_GETFL)
static void stress_filerace_fcntl(const int fd, const char *filename)
{
	(void)filename;
	VOID_RET(int, fcntl(fd, F_GETFL));
}
#endif

#if defined(HAVE_FSYNC)
static void stress_filerace_fsync(const int fd, const char *filename)
{
	(void)filename;
	VOID_RET(int, fsync(fd));
}
#endif

#if defined(HAVE_FDATASYNC)
static void stress_filerace_fdatasync(const int fd, const char *filename)
{
	(void)filename;
	VOID_RET(int, fdatasync(fd));
}
#endif

static void stress_filerace_write(const int fd, const char *filename)
{
	(void)filename;
	if (stress_mwc1()) {
		uint32_t data = stress_mwc32();

		VOID_RET(ssize_t, write(fd, &data, sizeof(data)));
	} else {
		uint8_t data[512];

		shim_memset(data, stress_mwc8(), sizeof(data));
		VOID_RET(ssize_t, write(fd, &data, sizeof(data)));
	}
}

static void stress_filerace_read(const int fd, const char *filename)
{
	(void)filename;
	if (stress_mwc1()) {
		uint32_t data;

		VOID_RET(ssize_t, read(fd, &data, sizeof(data)));
	} else {
		uint8_t data[512];

		VOID_RET(ssize_t, read(fd, &data, sizeof(data)));
	}
}

#if defined(HAVE_PWRITE)
static void stress_filerace_pwrite(const int fd, const char *filename)
{
	const off_t offset = ((off_t)stress_mwc32()) & OFFSET_MASK;

	(void)filename;
	if (stress_mwc1()) {
		uint32_t data = stress_mwc32();

		VOID_RET(ssize_t, pwrite(fd, &data, sizeof(data), offset));
	} else {
		uint8_t data[512];

		shim_memset(data, stress_mwc8(), sizeof(data));
		VOID_RET(ssize_t, pwrite(fd, &data, sizeof(data), offset));
	}
}
#endif

#if defined(HAVE_PREAD)
static void stress_filerace_pread(const int fd, const char *filename)
{
	const off_t offset = ((off_t)stress_mwc32()) & OFFSET_MASK;

	(void)filename;
	if (stress_mwc1()) {
		uint32_t data;

		VOID_RET(ssize_t, pread(fd, &data, sizeof(data), offset));
	} else {
		uint8_t data[512];

		VOID_RET(ssize_t, pread(fd, &data, sizeof(data), offset));
	}
}
#endif

#if defined(HAVE_FALLOCATE) &&		\
    defined(FALLOC_FL_PUNCH_HOLE) &&	\
    defined(FALLOC_FL_KEEP_SIZE)
static void stress_filerace_fallocate_punch_hole(const int fd, const char *filename)
{
	const off_t offset = ((off_t)stress_mwc32()) & OFFSET_MASK;
	const off_t len = ((off_t)stress_mwc16()) & OFFSET_MASK;

	(void)filename;
	VOID_RET(int, fallocate(fd, FALLOC_FL_PUNCH_HOLE | FALLOC_FL_KEEP_SIZE, offset, len));
}
#endif

#if defined(HAVE_FALLOCATE) &&		\
    defined(FALLOC_FL_COLLAPSE_RANGE)
static void stress_filerace_fallocate_collapse_range(const int fd, const char *filename)
{
	const off_t offset = ((off_t)stress_mwc32()) & OFFSET_MASK;
	const off_t len = ((off_t)stress_mwc16()) & OFFSET_MASK;

	(void)filename;
	VOID_RET(int, fallocate(fd, FALLOC_FL_COLLAPSE_RANGE, offset, len));
}
#endif

#if defined(HAVE_FALLOCATE) &&		\
    defined(FALLOC_FL_ZERO_RANGE)
static void stress_filerace_fallocate_zero_range(const int fd, const char *filename)
{
	const off_t offset = ((off_t)stress_mwc32()) & OFFSET_MASK;
	const off_t len = ((off_t)stress_mwc16()) & OFFSET_MASK;

	(void)filename;
	VOID_RET(int, fallocate(fd, FALLOC_FL_ZERO_RANGE, offset, len));
}
#endif

#if defined(HAVE_FALLOCATE) &&		\
    defined(FALLOC_FL_INSERT_RANGE)
static void stress_filerace_fallocate_insert_range(const int fd, const char *filename)
{
	const off_t offset = ((off_t)stress_mwc32()) & OFFSET_MASK;
	const off_t len = ((off_t)stress_mwc16()) & OFFSET_MASK;

	(void)filename;
	VOID_RET(int, fallocate(fd, FALLOC_FL_INSERT_RANGE, offset, len));
}
#endif

static void stress_filerace_ftruncate(const int fd, const char *filename)
{
	const off_t offset = (off_t)stress_mwc16();

	(void)filename;
	VOID_RET(int, ftruncate(fd, offset));
}

static void stress_filerace_utimes(const int fd, const char *filename)
{
	struct timeval times[2];

	(void)fd;
	times[0].tv_sec = stress_mwc32();
	times[0].tv_usec = stress_mwc32modn(1000000);
	times[1].tv_sec = stress_mwc32();
	times[1].tv_usec = stress_mwc32modn(1000000);

	errno = 0;
	VOID_RET(int, utimes(filename, times));
}

#if defined(HAVE_FUTIMES)
static void stress_filerace_futimes(const int fd, const char *filename)
{
	struct timeval tv[2];

	(void)filename;

	tv[0].tv_sec = (time_t)stress_mwc64() & 0x3ffffffffULL;
	tv[0].tv_usec = (time_t)stress_mwc32modn(1000000);

	tv[1].tv_sec = (time_t)stress_mwc64() & 0x3ffffffffULL;
	tv[1].tv_usec = (time_t)stress_mwc32modn(1000000);

	VOID_RET(int, futimes(fd, tv));
}
#endif

#if defined(HAVE_FLOCK) &&	\
    defined(LOCK_EX) &&		\
    defined(LOCK_UN)
static void stress_filerace_flock_ex(const int fd, const char *filename)
{
	(void)filename;
	if (flock(fd, LOCK_EX) == 0) {
		stress_filerace_write_random_uint32(fd);
		stress_random_small_sleep();
		VOID_RET(int, flock(fd, LOCK_UN));
	}
}
#endif

#if defined(HAVE_FLOCK) &&	\
    defined(LOCK_EX) &&		\
    defined(LOCK_SH)
static void stress_filerace_flock_sh(const int fd, const char *filename)
{
	(void)filename;
	if (flock(fd, LOCK_SH) == 0) {
		stress_filerace_write_random_uint32(fd);
		stress_random_small_sleep();
		VOID_RET(int, flock(fd, LOCK_UN));
	}
}
#endif

#if defined(FIBMAP)
static void stress_filerace_fibmap(const int fd, const char *filename)
{
	int block;

	(void)filename;
	block = 0;
	VOID_RET(int, ioctl(fd, FIBMAP, &block));
	block = stress_mwc32();
	VOID_RET(int, ioctl(fd, FIBMAP, &block));
}
#endif

#if defined(HAVE_POSIX_FADVISE) && 	\
    defined(POSIX_FADV_DONTNEED)
static void stress_filerace_posix_fadvise_dontneed_all(const int fd, const char *filename)
{
	struct stat buf;

	(void)filename;
	if (fstat(fd, &buf) < 0)
		return;
	VOID_RET(int, posix_fadvise(fd, 0, (off_t)buf.st_size, POSIX_FADV_DONTNEED));
}
#endif

#if defined(HAVE_POSIX_FADVISE) && 	\
    defined(POSIX_FADV_NORMAL) &&	\
    defined(POSIX_FADV_SEQUENTIAL) &&	\
    defined(POSIX_FADV_RANDOM) &&	\
    defined(POSIX_FADV_NOREUSE) &&	\
    defined(POSIX_FADV_WILLNEED) &&	\
    defined(POSIX_FADV_DONTNEED)
static void stress_filerace_posix_fadvise(const int fd, const char *filename)
{
	static const int advice[] = {
#if defined(POSIX_FADV_NORMAL)
		POSIX_FADV_NORMAL,
#endif
#if defined(POSIX_FADV_SEQUENTIAL)
		POSIX_FADV_SEQUENTIAL,
#endif
#if defined(POSIX_FADV_RANDOM)
		POSIX_FADV_RANDOM,
#endif
#if defined(POSIX_FADV_NOREUSE)
		POSIX_FADV_NOREUSE,
#endif
#if defined(POSIX_FADV_WILLNEED)
		POSIX_FADV_WILLNEED,
#endif
#if defined(POSIX_FADV_DONTNEED)
		POSIX_FADV_DONTNEED,
#endif
	};
	const off_t offset = ((off_t)stress_mwc32()) & OFFSET_MASK;
	const off_t len = ((off_t)stress_mwc16()) & OFFSET_MASK;
	const int new_advice = advice[stress_mwc8modn((uint8_t)SIZEOF_ARRAY(advice))];

	(void)filename;
	VOID_RET(int, posix_fadvise(fd, offset, len, new_advice));
}
#endif

#if defined(POSIX_FALLOCATE)
static void stress_filerace_posix_fallocate(const int fd, const char *filename)
{
	const off_t offset = ((off_t)stress_mwc32()) & OFFSET_MASK;
	const off_t len = ((off_t)stress_mwc16()) & OFFSET_MASK;

	(void)filename;
	VOID_RET(int, posix_fallocate(fd, offset, len));
}
#endif

#if defined(HAVE_READAHEAD)
static void stress_filerace_readahead(const int fd, const char *filename)
{
	const off_t offset = ((off_t)stress_mwc32()) & OFFSET_MASK;
	const size_t len = ((off_t)stress_mwc16()) & OFFSET_MASK;

	(void)filename;
	VOID_RET(int, readahead(fd, offset, len));
}
#endif

static void stress_filerace_chmod(const int fd, const char *filename)
{
	(void)fd;
	VOID_RET(int, chmod(filename, S_IRUSR | S_IWUSR));
}

static void stress_filerace_chown(const int fd, const char *filename)
{
	(void)fd;
	VOID_RET(int, chown(filename, uid, gid));
}

static void stress_filerace_open(const int fd, const char *filename)
{
	int new_fd;

	(void)fd;
	new_fd = open(filename, O_RDONLY);
	if (new_fd > -1)
		(void)close(new_fd);
}

static void stress_filerace_stat(const int fd, const char *filename)
{
	struct stat buf;

	(void)fd;
	VOID_RET(int, stat(filename, &buf));
}

static void stress_filerace_statx_fd(const int fd, const char *filename)
{
	shim_statx_t bufx;

	(void)filename;
	VOID_RET(int, shim_statx(fd, NULL, AT_EMPTY_PATH, SHIM_STATX_ALL, &bufx));
}

static void stress_filerace_statx_filename(const int fd, const char *filename)
{
	shim_statx_t bufx;

	(void)fd;
	VOID_RET(int, shim_statx((*filename == '.') ? AT_FDCWD : 0,
				 filename, 0, SHIM_STATX_ALL, &bufx));
}

static void stress_filerace_truncate(const int fd, const char *filename)
{
	const off_t offset = (off_t)stress_mwc16();

	(void)fd;
	VOID_RET(int, truncate(filename, offset));
}

static void stress_filerace_readlink(const int fd, const char *filename)
{
	char buf[PATH_MAX];

	(void)fd;
	/* will always fail */
	VOID_RET(int, readlink(filename, buf, sizeof(buf)));
}

static void stress_filerace_openmany(const int fd, const char *filename)
{
	size_t i;

	int fds[MAX_FDS];

	for (i = 0; i < SIZEOF_ARRAY(fds); i++) {
		fds[i] = open(filename, O_CREAT | O_RDWR | O_APPEND, S_IRUSR | S_IWUSR);
		if (fds[i] > -1)
			stress_filerace_write_random_uint32(fd);
	}
	for (i = 0; i < SIZEOF_ARRAY(fds); i++) {
		if (fds[i] > -1)
			(void)close(fds[i]);
	}
}

#if defined(F_SETLEASE) &&      \
    defined(F_WRLCK) &&         \
    defined(F_UNLCK)
static void stress_filerace_lease_wrlck(const int fd, const char *filename)
{
	(void)filename;
	if (fcntl(fd, F_SETLEASE, F_WRLCK) == 0) {
		stress_filerace_write_random_uint32(fd);
		stress_random_small_sleep();
		VOID_RET(int, fcntl(fd, F_SETLEASE, F_UNLCK));
	}
}
#endif

#if defined(F_SETLEASE) &&      \
    defined(F_RDLCK) &&         \
    defined(F_UNLCK)
static void stress_filerace_lease_rdlck(const int fd, const char *filename)
{
	(void)filename;
	if (fcntl(fd, F_SETLEASE, F_RDLCK) == 0) {
		stress_filerace_read_random_uint32(fd);
		stress_random_small_sleep();
		VOID_RET(int, fcntl(fd, F_SETLEASE, F_UNLCK));
	}
}
#endif

#if defined(HAVE_LOCKF) &&	\
    defined(F_LOCK) &&		\
    defined(F_ULOCK)
static void stress_filerace_lockf_lock(const int fd, const char *filename)
{
	uint32_t val = stress_mwc32();

	(void)filename;
	if (lockf(fd, F_LOCK, sizeof(val)) == 0) {
		if (lseek(fd, (off_t)val, SEEK_SET) >= 0)
			VOID_RET(ssize_t, write(fd, &val, sizeof(val)));
		stress_random_small_sleep();
		VOID_RET(int, lockf(fd, F_ULOCK, sizeof(val)));
	}
}
#endif

#if defined(HAVE_LOCKF) &&	\
    defined(F_TLOCK) &&		\
    defined(F_ULOCK)
static void stress_filerace_lockf_tlock(const int fd, const char *filename)
{
	uint32_t val = stress_mwc32();

	(void)filename;
	if (lockf(fd, F_TLOCK, sizeof(val)) == 0) {
		if (lseek(fd, (off_t)val, SEEK_SET) >= 0)
			VOID_RET(ssize_t, write(fd, &val, sizeof(val)));
		stress_random_small_sleep();
		VOID_RET(int, lockf(fd, F_ULOCK, sizeof(val)));
	}
}
#endif

#if defined(F_OFD_SETLK) &&     \
    defined(F_WRLCK) &&		\
    defined(F_UNLCK)
static void stress_filerace_lockofd_wr(const int fd, const char *filename)
{
	uint32_t val = stress_mwc32();

	(void)filename;

	if (lseek(fd, (off_t)val, SEEK_SET) >= 0) {
		if (write(fd, &val, sizeof(val)) > 0) {
			struct flock f;

			f.l_type = F_WRLCK;
			f.l_whence = SEEK_SET;
			f.l_start = (off_t)val;
			f.l_len = sizeof(val);
			f.l_pid = 0;
			VOID_RET(int, fcntl(fd, F_OFD_SETLK, &f));

			if (lseek(fd, (off_t)val, SEEK_SET) >= 0)
				VOID_RET(ssize_t, write(fd, &val, sizeof(val)));
			stress_random_small_sleep();

			f.l_type = F_UNLCK;
			f.l_whence = SEEK_SET;
			f.l_start = (off_t)val;
			f.l_len = sizeof(val);
			f.l_pid = 0;
			VOID_RET(int, fcntl(fd, F_OFD_SETLK, &f));
		}
	}
}
#endif

#if defined(F_OFD_SETLK) &&     \
    defined(F_RDLCK) &&		\
    defined(F_UNLCK)
static void stress_filerace_lockofd_rd(const int fd, const char *filename)
{
	uint32_t val = stress_mwc32();

	(void)filename;

	if (lseek(fd, (off_t)val, SEEK_SET) >= 0) {
		struct flock f;

		f.l_type = F_RDLCK;
		f.l_whence = SEEK_SET;
		f.l_start = (off_t)val;
		f.l_len = sizeof(val);
		f.l_pid = 0;
		VOID_RET(int, fcntl(fd, F_OFD_SETLK, &f));

		if (lseek(fd, (off_t)val, SEEK_SET) >= 0) {
			uint32_t tmp;

			VOID_RET(ssize_t, read(fd, &tmp, sizeof(tmp)));
		}
		stress_random_small_sleep();

		f.l_type = F_UNLCK;
		f.l_whence = SEEK_SET;
		f.l_start = (off_t)val;
		f.l_len = sizeof(val);
		f.l_pid = 0;
		VOID_RET(int, fcntl(fd, F_OFD_SETLK, &f));
	}
}
#endif

static void stress_filerace_chdir(const int fd, const char *filename)
{
	char cwdpath[PATH_MAX];

	(void)fd;
	if (getcwd(cwdpath, sizeof(cwdpath)) == NULL)
		return;
	if (chdir(filename) < 0)
		return;
	VOID_RET(int, chdir(cwdpath));
}

static void stress_filerace_fchdir(const int fd, const char *filename)
{
	char cwdpath[PATH_MAX];

	(void)filename;
	if (getcwd(cwdpath, sizeof(cwdpath)) == NULL)
		return;
	if (fchdir(fd) < 0)
		return;
	VOID_RET(int, chdir(cwdpath));
}

#if defined(MS_ASYNC) &&	\
    defined(MS_SYNC) &&		\
    defined(HAVE_FALLOCATE) &&	\
    defined(FALLOC_FL_ZERO_RANGE)
static sigjmp_buf mmap_jmpbuf;

static void NORETURN MLOCKED_TEXT stress_filerace_mmap_sigbus_handler(int sig)
{
	(void)sig;

	siglongjmp(mmap_jmpbuf, 1);
}

static void stress_filerace_mmap(const int fd, const char *filename)
{
	NOCLOBBER void *ptr;
	NOCLOBBER size_t mmap_size;
	off_t offset;
	struct sigaction new_action, old_action;

	mmap_size = stress_get_page_size() * (1 + (stress_mwc8() & 0xf));
	offset = ((off_t)stress_mwc32()) & ~(off_t)(mmap_size - 1);

	(void)filename;

	(void)shim_memset(&new_action, 0, sizeof(new_action));
	new_action.sa_handler = stress_filerace_mmap_sigbus_handler;
	(void)sigemptyset(&new_action.sa_mask);
	new_action.sa_flags = SA_NOCLDSTOP;

	/*
	 *  a SIGBUS handler is required because the file may
	 *  be truncated or a hole punched in the file after the
	 *  fallocate causing the mmap'd region to be no longer
	 *  backed by the file
	 */
	if (sigaction(SIGBUS, &new_action, &old_action) < 0)
		return;
	if (fallocate(fd, FALLOC_FL_ZERO_RANGE, offset, mmap_size) < 0)
		return;
	ptr = mmap(NULL, mmap_size, PROT_READ | PROT_WRITE,
		   MAP_SHARED, fd, offset);
	if (ptr == MAP_FAILED)
		return;
	if (sigsetjmp(mmap_jmpbuf, 1) != 0)
		goto unmap;
	(void)shim_memset(ptr, stress_mwc8(), mmap_size);
	VOID_RET(int, msync(ptr, mmap_size, stress_mwc1() ? MS_ASYNC : MS_SYNC));
unmap:
	(void)munmap(ptr, mmap_size);
	(void)sigaction(SIGBUS, &old_action, NULL);
}
#endif

static stress_filerace_fops_t stress_filerace_fops[] = {
	stress_filerace_fstat,
	stress_filerace_lseek_set,
	stress_filerace_lseek_end,
	stress_filerace_fchmod,
	stress_filerace_fchown,
#if defined(F_GETFL)
	stress_filerace_fcntl,
#endif
#if defined(HAVE_FSYNC)
	stress_filerace_fsync,
#endif
#if defined(HAVE_FDATASYNC)
	stress_filerace_fdatasync,
#endif
	stress_filerace_write,
	stress_filerace_read,
#if defined(HAVE_PWRITE)
	stress_filerace_pwrite,
#endif
#if defined(HAVE_PREAD)
	stress_filerace_pread,
#endif


#if defined(HAVE_FALLOCATE) &&		\
    defined(FALLOC_FL_PUNCH_HOLE) &&	\
    defined(FALLOC_FL_KEEP_SIZE)
	stress_filerace_fallocate_punch_hole,
#endif
#if defined(HAVE_FALLOCATE) &&		\
    defined(FALLOC_FL_COLLAPSE_RANGE)
	stress_filerace_fallocate_collapse_range,
#endif
#if defined(HAVE_FALLOCATE) &&		\
    defined(FALLOC_FL_ZERO_RANGE)
	stress_filerace_fallocate_zero_range,
#endif
#if defined(HAVE_FALLOCATE) &&		\
    defined(FALLOC_FL_INSERT_RANGE)
	stress_filerace_fallocate_insert_range,
#endif
	stress_filerace_ftruncate,
	stress_filerace_utimes,
#if defined(HAVE_FUTIMES)
	stress_filerace_futimes,
#endif
#if defined(HAVE_FLOCK) &&	\
    defined(LOCK_EX) &&		\
    defined(LOCK_UN)
	stress_filerace_flock_ex,
#endif
#if defined(HAVE_FLOCK) &&	\
    defined(LOCK_EX) &&		\
    defined(LOCK_SH)
	stress_filerace_flock_sh,
#endif
#if defined(FIBMAP)
	stress_filerace_fibmap,
#endif
#if defined(HAVE_POSIX_FADVISE) && 	\
    defined(POSIX_FADV_DONTNEED)
	stress_filerace_posix_fadvise_dontneed_all,
#endif
#if defined(HAVE_POSIX_FADVISE) && 	\
    defined(POSIX_FADV_NORMAL) &&	\
    defined(POSIX_FADV_SEQUENTIAL) &&	\
    defined(POSIX_FADV_RANDOM) &&	\
    defined(POSIX_FADV_NOREUSE) &&	\
    defined(POSIX_FADV_WILLNEED) &&	\
    defined(POSIX_FADV_DONTNEED)
	stress_filerace_posix_fadvise,
#endif
#if defined(POSIX_FALLOCATE)
	stress_filerace_posix_fallocate,
#endif
#if defined(HAVE_READAHEAD)
	stress_filerace_readahead,
#endif
	stress_filerace_chmod,
	stress_filerace_chown,
	stress_filerace_open,
	stress_filerace_stat,
	stress_filerace_statx_fd,
	stress_filerace_statx_filename,
	stress_filerace_truncate,
	stress_filerace_readlink,
	stress_filerace_openmany,
#if defined(F_SETLEASE) &&      \
    defined(F_WRLCK) &&         \
    defined(F_UNLCK)
	stress_filerace_lease_wrlck,
#endif
#if defined(F_SETLEASE) &&      \
    defined(F_RDLCK) &&         \
    defined(F_UNLCK)
	stress_filerace_lease_rdlck,
#endif
#if defined(HAVE_LOCKF) &&	\
    defined(F_LOCK) &&		\
    defined(F_ULOCK)
	stress_filerace_lockf_lock,
#endif
#if defined(HAVE_LOCKF) &&	\
    defined(F_TLOCK) &&		\
    defined(F_ULOCK)
	stress_filerace_lockf_tlock,
#endif
#if defined(F_OFD_SETLK) &&     \
    defined(F_WRLCK)
	stress_filerace_lockofd_wr,
#endif
#if defined(F_OFD_SETLK) &&     \
    defined(F_RDLCK)
	stress_filerace_lockofd_rd,
#endif
	stress_filerace_chdir,
	stress_filerace_fchdir,
#if defined(MS_ASYNC) &&	\
    defined(MS_SYNC) &&		\
    defined(HAVE_FALLOCATE) &&	\
    defined(FALLOC_FL_ZERO_RANGE)
	stress_filerace_mmap,
#endif
};

static void stress_filerace_file(const int fd, const char *filename)
{
	int i;

	for (i = 0; i < stress_mwc8modn(32); i++) {
		size_t idx = stress_mwc8modn((uint8_t)SIZEOF_ARRAY(stress_filerace_fops));

		stress_filerace_fops[idx](fd, filename);
	}
}

static void stress_filerace_filename(const char *pathname, char *filename, const size_t filename_len)
{
	const uint8_t rnd = stress_mwc8() & 0x3f;

	(void)snprintf(filename, filename_len, "%s/%2.2" PRIx8, pathname, rnd);
}

static void stress_filerace_child(stress_args_t *args, const char *pathname, const bool parent)
{
	int fds[MAX_FDS];
	size_t i;
	size_t fd_idx = 0;

	static const int flags[] = {
		0,
#if defined(O_DIRECT)
		O_DIRECT,
#endif
#if defined(O_DSYNC)
		O_DSYNC,
#endif
#if defined(O_EXCL)
		O_EXCL,
#endif
#if defined(O_NOATIME)
		O_NOATIME,
#endif
#if defined(O_NOFOLLOW)
		O_NOFOLLOW,
#endif
#if defined(O_NONBLOCK)
		O_NONBLOCK,
#endif
#if defined(O_SYNC)
		O_SYNC,
#endif
#if defined(O_TRUNC)
		O_TRUNC,
#endif
	};

	for (i = 0; i < SIZEOF_ARRAY(fds); i++)
		fds[i] = -1;

	do {
		char filename[PATH_MAX];
		char filename2[PATH_MAX];
		int flag;
		int which = stress_mwc8modn(10);
		int fd;
		DIR *dir;
		struct dirent *d;
		struct stat buf;
		uint8_t n;

		switch (which) {
		default:
		case 0:
			stress_filerace_filename(pathname, filename, sizeof(filename));
			(void)shim_unlink(filename);
			(void)shim_rmdir(filename);
			fds[fd_idx] = creat(filename, S_IRUSR | S_IWUSR);
			if (fds[fd_idx] > 0) {
				stress_filerace_file(fds[fd_idx], filename);
				fd_idx++;
			}
			break;
		case 1:
			stress_filerace_filename(pathname, filename, sizeof(filename));
			(void)shim_unlink(filename);
			(void)shim_rmdir(filename);
			break;
		case 2:
			stress_filerace_filename(pathname, filename, sizeof(filename));
			flag = flags[stress_mwc8modn((uint8_t)SIZEOF_ARRAY(flags))];
			fds[fd_idx] = open(filename, O_CREAT | O_RDWR | O_APPEND | flag, S_IRUSR | S_IWUSR);
			if (fds[fd_idx] > 0) {
				stress_filerace_file(fds[fd_idx], filename);
				fd_idx++;
			}
			break;
		case 3:
			stress_filerace_filename(pathname, filename, sizeof(filename));
			flag = flags[stress_mwc8modn((uint8_t)SIZEOF_ARRAY(flags))];
			fds[fd_idx] = open(filename, O_CREAT | O_RDWR | flag, S_IRUSR | S_IWUSR);
			if (fds[fd_idx] > 0) {
				stress_filerace_file(fds[fd_idx], filename);
				fd_idx++;
			}
			break;
		case 4:
			stress_filerace_filename(pathname, filename, sizeof(filename));
			stress_filerace_filename(pathname, filename2, sizeof(filename2));
			(void)rename(filename2, filename);
			break;
		case 5:
			if (stress_mwc8() < 8) {
				dir = opendir(pathname);
				if (dir) {
					while ((d = readdir(dir)) != NULL) {
					}
					(void)closedir(dir);
				}
				break;
			} else {
#if defined(O_DIRECTORY)
				fd = open(pathname, O_DIRECTORY);
					(void)close(fd);
#endif
				VOID_RET(int, stat(pathname, &buf));
			}
			break;
		case 6:
			stress_filerace_filename(pathname, filename, sizeof(filename));
			VOID_RET(int, stat(filename, &buf));
			VOID_RET(int, lstat(filename, &buf));
			VOID_RET(int, stat(pathname, &buf));
			VOID_RET(int, lstat(pathname, &buf));
			break;
		case 7:
			stress_filerace_filename(pathname, filename, sizeof(filename));
			stress_filerace_filename(pathname, filename2, sizeof(filename2));
			VOID_RET(int, unlink(filename));
			if (stress_mwc1())
				VOID_RET(int, link(filename2, filename));
			else
				VOID_RET(int, symlink(filename2, filename));
			VOID_RET(int, lchown(filename, uid, gid));
			VOID_RET(int, lchown(filename2, uid, gid));
			VOID_RET(int, lstat(filename, &buf));
			VOID_RET(int, lstat(filename2, &buf));
			break;
		case 8:
			stress_filerace_filename(pathname, filename, sizeof(filename));
			(void)shim_unlink(filename);
			(void)shim_rmdir(filename);
			VOID_RET(int, mkdir(filename, S_IRUSR | S_IWUSR | S_IXUSR));
#if defined(O_DIRECTORY)
			fd = open(filename, O_DIRECTORY);
			if (fd >= 0) {
				stress_filerace_file(fd, filename);
				(void)close(fd);
			}
#endif
			break;
		case 9:
			for (n = 0; n < 64; n++) {
				int fd;

				(void)snprintf(filename, sizeof(filename), "%s/%2.2" PRIx8, pathname, n);
				fd = open(filename, O_CREAT | O_RDWR | O_APPEND, S_IRUSR | S_IWUSR);
				if (fd > -1)
					(void)close(fd);
			}
		}

		if (fd_idx >= SIZEOF_ARRAY(fds)) {
			for (i = 0; i < SIZEOF_ARRAY(fds); i++) {
				if (fds[i] != -1) {
					(void)close(fds[i]);
					fds[i] = -1;
				}
			}
			fd_idx = 0;
		}
		if (parent)
			stress_bogo_inc(args);
	} while (stress_continue(args));

	for (i = 0; i < SIZEOF_ARRAY(fds); i++) {
		if (fds[i] != -1)
			(void)close(fds[i]);
	}
}

/*
 *  stress_filerace()
 *	stress filename sizes etc
 */
static int stress_filerace(stress_args_t *args)
{
	int rc = EXIT_SUCCESS;
	char pathname[PATH_MAX - 256];
	pid_t pids[MAX_FILERACE_PROCS];
	size_t i, children = 0;

#if defined(SIGIO)
	if (stress_sighandler(args->name, SIGIO, stress_sigio_handler, NULL) < 0)
		return EXIT_FAILURE;
#endif

	uid = getuid();
	gid = getgid();

	stress_temp_dir_args(args, pathname, sizeof(pathname));
	if (mkdir(pathname, S_IRWXU) < 0) {
		if (errno != EEXIST) {
			pr_fail("%s: mkdir %s failed, errno=%d (%s)\n",
				args->name, pathname, errno, strerror(errno));
			return EXIT_FAILURE;
		}
	}

	stress_set_proc_state(args->name, STRESS_STATE_SYNC_WAIT);
	stress_sync_start_wait(args);
	stress_set_proc_state(args->name, STRESS_STATE_RUN);

	for (i = 0; i < MAX_FILERACE_PROCS; i++) {
		pids[i] = fork();

		if (pids[i] < 0) {
			continue;
		} else if (pids[i] == 0) {
			stress_filerace_child(args, pathname, false);
			_exit(EXIT_SUCCESS);
		} else {
			children++;
		}
	}

	if (children == 0) {
		pr_inf_skip("%s: failed to create %d child processes, skipping stressor\n",
			args->name, MAX_FILERACE_PROCS);
		rc = EXIT_FAILURE;
		goto tidy_dir;
	}

	stress_filerace_child(args, pathname, true);

	for (i = 0; i < MAX_FILERACE_PROCS; i++) {
		if (pids[i] > 0)
			stress_kill_and_wait(args, pids[i], SIGKILL, true);
	}

tidy_dir:
	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);
	(void)stress_filerace_tidy(pathname);

	return rc;
}

const stressor_info_t stress_filerace_info = {
	.stressor = stress_filerace,
	.class = CLASS_FILESYSTEM | CLASS_OS,
	.verify = VERIFY_NONE,
	.help = help
};
