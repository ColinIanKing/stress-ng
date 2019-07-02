/*
 * Copyright (C) 2014-2019 Canonical, Ltd.
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

/*
 *  Various shim abstraction wrappers around systems calls and
 *  GCC helper functions that may not be supported by some
 *  kernels or versions of different C libraries.
 */

static inline int shim_enosys(long arg1, ...)
{
	(void)arg1;
	errno = ENOSYS;
	return -1;
}

/*
 *  shim_sched_yield()
 *  	wrapper for sched_yield(2) - yield the processor
 */
int shim_sched_yield(void)
{
#if defined(HAVE_SCHED_YIELD)
	return sched_yield();
#else
	return sleep(0);
#endif
}

/*
 *  shim_cacheflush()
 *	wrapper for cacheflush(2), flush contents of
 *	instruction and/or data cache
 */
int shim_cacheflush(char *addr, int nbytes, int cache)
{
#if defined(__NR_cacheflush)
	return (int)syscall(__NR_cacheflush, addr, nbytes, cache);
#else
	return shim_enosys(0, addr, nbytes, cache);
#endif
}

/*
 * shim_copy_file_range()
 *	wrapper for copy_file_range(2), copy range of data
 *	from one file to another
 */
ssize_t shim_copy_file_range(
	int fd_in,
	shim_loff_t *off_in,
	int fd_out,
	shim_loff_t *off_out,
	size_t len,
	unsigned int flags)
{
#if defined(HAVE_COPY_FILE_RANGE)
	return copy_file_range(fd_in, off_in, fd_out, off_out, len, flags);
#elif defined(__NR_copy_file_range)
	return syscall(__NR_copy_file_range,
		fd_in, off_in, fd_out, off_out, len, flags);
#else
	return shim_enosys(0, fd_in, off_in, fd_out, off_out, len, flags);
#endif
}

/*
 * shim_emulate_fallocate()
 *	emulate fallocate (very slow!)
 */
static int shim_emulate_fallocate(int fd, off_t offset, off_t len)
{
	const off_t buf_sz = 8192;
	char buffer[buf_sz];
	off_t n;

	n = lseek(fd, offset, SEEK_SET);
	if (n == (off_t)-1)
		return -1;

	(void)memset(buffer, 0, buf_sz);
	n = len;

	while (g_keep_stressing_flag && (n > 0)) {
		ssize_t ret;
		size_t count = (size_t)STRESS_MINIMUM(n, buf_sz);

		ret = write(fd, buffer, count);
		if (ret >= 0) {
			n -= ret;
		} else {
			return -1;
		}
	}
	return 0;
}

/*
 * shim_fallocate()
 *	shim wrapper for fallocate system call
 *	- falls back to posix_fallocate w/o mode
 * 	- falls back to direct writes
 */
int shim_fallocate(int fd, int mode, off_t offset, off_t len)
{
#if defined(HAVE_FALLOCATE)
	int ret;

	ret = fallocate(fd, mode, offset, len);
	/* mode not supported? try with zero mode (dirty hack) */
	if ((ret < 0) && (errno == EOPNOTSUPP)) {
		ret = syscall(__NR_fallocate, fd, 0, offset, len);
		/* fallocate failed, try emulation mode */
		if ((ret < 0) && (errno == EOPNOTSUPP)) {
			ret = shim_emulate_fallocate(fd, offset, len);
		}
	}
	return ret;
#elif defined(__NR_fallocate)
	int ret;

	ret = syscall(__NR_fallocate, fd, mode, offset, len);
	/* mode not supported? try with zero mode (dirty hack) */
	if ((ret < 0) && (errno == EOPNOTSUPP)) {
		ret = syscall(__NR_fallocate, fd, 0, offset, len);
		/* fallocate failed, try emulation mode */
		if ((ret < 0) && (errno == EOPNOTSUPP)) {
			ret = shim_emulate_fallocate(fd, offset, len);
		}
	}
	return ret;
#elif defined(HAVE_POSIX_FALLOCATE) && !defined(__FreeBSD_kernel__)
	/*
	 *  Even though FreeBSD kernels support this, large
	 *  allocations take forever to be interrupted and so
	 *  we don't use this for FreeBSD for now.
	 */
	int ret;

	(void)mode;

	/*
	 *  posix_fallocate returns 0 for success, > 0 as errno
	 */
	ret = posix_fallocate(fd, offset, len);
	errno = 0;
	if (ret != 0) {
		/* failed, so retry with slower emulated fallocate */
		ret = shim_emulate_fallocate(fd, offset, len);
	}
	return ret;
#else
	(void)mode;

	return shim_emulate_fallocate(fd, offset, len);
#endif
}

/*
 *  shim_gettid()
 *	wrapper for gettid(2), get thread identification
 */
int shim_gettid(void)
{
#if defined(__NR_gettid)
	return syscall(__NR_gettid);
#else
	return shim_enosys(0);
#endif
}

/*
 *  shim_getcpu()
 *	wrapper for getcpu(2) - get CPU and NUMA node of
 *	calling thread
 */
long shim_getcpu(
	unsigned *cpu,
	unsigned *node,
	void *tcache)
{
#if defined(__NR_getcpu)
	return syscall(__NR_getcpu, cpu, node, tcache);
#else
	return shim_enosys(0, cpu, node, tcache);
#endif
}

/*
 *  shim_getdents()
 *	wrapper for getdents(2) - get directory entries
 */
int shim_getdents(
	unsigned int fd,
	struct shim_linux_dirent *dirp,
	unsigned int count)
{
#if defined(__NR_getdents)
	return syscall(__NR_getdents, fd, dirp, count);
#else
	return shim_enosys(0, fd, dirp, count);
#endif
}

/*
 *  shim_getdent64()
 *	wrapper for getdents64(2) - get directory entries
 */
int shim_getdents64(
	unsigned int fd,
	struct shim_linux_dirent64 *dirp,
	unsigned int count)
{
#if defined(__NR_getdents64)
	return syscall(__NR_getdents64, fd, dirp, count);
#else
	return shim_enosys(0, fd, dirp, count);
#endif
}

/*
 *  shim_getrandom()
 *	wrapper for Linux getrandom(2) and BSD getentropy(2)
 */
int shim_getrandom(void *buff, size_t buflen, unsigned int flags)
{
#if defined(__NR_getrandom)
	return syscall(__NR_getrandom, buff, buflen, flags);
#elif defined(__OpenBSD__) || defined(__APPLE__)
	(void)flags;

	return getentropy(buff, buflen);
#else
	return shim_enosys(0, buff, buflen, flags);
#endif
}

/*
 *  shim_clear_cache()
 *	wrapper for ARM GNUC clear cache intrinsic
 */
void shim_clear_cache(char* begin, char *end)
{
#if defined(__GNUC__) && defined(STRESS_ARM)
	__clear_cache(begin, end);
#else
	(void)begin;
	(void)end;
#endif
}

/*
 *  shim_kcmp()
 *	wrapper for Linux kcmp(2) - compre two processes to
 *	see if they share a kernel resource.
 */
long shim_kcmp(pid_t pid1, pid_t pid2, int type, unsigned long idx1, unsigned long idx2)
{
#if defined(__NR_kcmp)
	errno = 0;
	return syscall(__NR_kcmp, pid1, pid2, type, idx1, idx2);
#else
	return shim_enosys(0, pid1, pid2, type, idx1, idx2);
#endif
}

/*
 *  shim_syslog()
 *	wrapper for syslog(2) (NOT syslog(3))
 */
int shim_syslog(int type, char *bufp, int len)
{
#if defined(__NR_syslog)
	return syscall(__NR_syslog, type, bufp, len);
#else
	return shim_enosys(0, type, bufp, len);
#endif
}

/*
 *  shim_membarrier()
 *	wrapper for membarrier(2) - issue memory barriers
 */
int shim_membarrier(int cmd, int flags)
{
#if defined(__NR_membarrier)
	return syscall(__NR_membarrier, cmd, flags);
#else
	return shim_enosys(0, cmd, flags);
#endif
}

/*
 *  shim_memfd_create()
 *	wrapper for memfd_create(2)
 */
int shim_memfd_create(const char *name, unsigned int flags)
{
#if defined(__NR_memfd_create)
	return syscall(__NR_memfd_create, name, flags);
#else
	return shim_enosys(0, name, flags);
#endif
}

/*
 *  shim_get_mempolicy()
 *	wrapper for get_mempolicy(2) - get NUMA memory policy
 */
int shim_get_mempolicy(
	int *mode,
	unsigned long *nodemask,
	unsigned long maxnode,
	unsigned long addr,
	unsigned long flags)
{
#if defined(__NR_get_mempolicy)
	return syscall(__NR_get_mempolicy,
		mode, nodemask, maxnode, addr, flags);
#else
	return shim_enosys(0, mode, nodemask, maxnode, addr, flags);
#endif
}

/*
 *  shim_set_mempolicy()
 *	wrapper for set_mempolicy(2) - set NUMA memory policy
 */
int shim_set_mempolicy(
	int mode,
	unsigned long *nodemask,
	unsigned long maxnode)
{
#if defined(__NR_set_mempolicy)
	return syscall(__NR_set_mempolicy,
		mode, nodemask, maxnode);
#else
	return shim_enosys(0, mode, nodemask, maxnode);
#endif
}

/*
 *  shim_mbind()
 *	wrapper for mbind(2) - set memory policy for a memory range
 */
long shim_mbind(
	void *addr,
	unsigned long len,
	int mode,
	const unsigned long *nodemask,
	unsigned long maxnode,
	unsigned flags)
{
#if defined(__NR_mbind)
	return syscall(__NR_mbind,
		addr, len, mode, nodemask, maxnode, flags);
#else
	return shim_enosys(0, addr, len, mode, nodemask, maxnode, flags);
#endif
}

/*
 *  shim_migrate_pages()
 *	wrapper for migrate_pages(2) - move all pages in a process to other nodes
 */
long shim_migrate_pages(
	int pid,
	unsigned long maxnode,
	const unsigned long *old_nodes,
	const unsigned long *new_nodes)
{
#if defined(__NR_migrate_pages)
	return syscall(__NR_migrate_pages,
		pid, maxnode, old_nodes, new_nodes);
#else
	return shim_enosys(0, pid, maxnode, old_nodes, new_nodes);
#endif
}

/*
 *  shim_move_pages()
 *	wrapper for move_pages(2) - move pages in a process to other nodes
 */
long shim_move_pages(
	int pid,
	unsigned long count,
	void **pages,
	const int *nodes,
	int *status,
	int flags)
{
#if defined(__NR_move_pages)
	return syscall(__NR_move_pages, pid, count, pages, nodes,
		status, flags);
#else
	return shim_enosys(0, pid, count, pages, nodes, status, flags);
#endif
}

/*
 *  shim_userfaultfd()
 *	wrapper for userfaultfd(2)
 */
int shim_userfaultfd(int flags)
{
#if defined(__NR_userfaultfd)
	return syscall(__NR_userfaultfd, flags);
#else
	return shim_enosys(0, flags);
#endif
}

/*
 *  shim_seccomp()
 *	wrapper for seccomp(2) - operate on Secure Computing state of process
 */
int shim_seccomp(unsigned int operation, unsigned int flags, void *args)
{
#if defined(__NR_seccomp)
	return (int)syscall(__NR_seccomp, operation, flags, args);
#else
	return shim_enosys(0, operation, flags, args);
#endif
}

/*
 *  shim_unshare()
 *	wrapper for unshare(2)
 */
int shim_unshare(int flags)
{
#if defined(__NR_unshare)
#if NEED_GLIBC(2,14,0)
	return unshare(flags);
#else
	return syscall(__NR_unshare, flags);
#endif
#else
	return shim_enosys(0, flags);
#endif
}

/*
 *  shim_shed_getattr()
 *	wrapper for shed_getattr(2)
 */
int shim_sched_getattr(
	pid_t pid,
	struct shim_sched_attr *attr,
	unsigned int size,
	unsigned int flags)
{
#if defined(__NR_sched_getattr)
	return syscall(__NR_sched_getattr, pid, attr, size, flags);
#else
	return shim_enosys(0, pid, attr, size, flags);
#endif
}

/*
 *  shim_shed_setattr()
 *	wrapper for shed_setattr(2)
 */
int shim_sched_setattr(
	pid_t pid,
	struct shim_sched_attr *attr,
	unsigned int flags)
{
#if defined(__NR_sched_setattr)
	return syscall(__NR_sched_setattr, pid, attr, flags);
#else
	return shim_enosys(0, pid, attr, flags);
#endif
}

/*
 *  shim_mlock()
 *	wrapper for mlock(2) - lock memory
 */
int shim_mlock(const void *addr, size_t len)
{
#if defined(HAVE_MLOCK)
	return mlock(addr, len);
#else
	return shim_enosys(0, addr, len);
#endif
}

/*
 *  shim_munlock()
 *	wrapper for munlock(2) - unlock memory
 */
int shim_munlock(const void *addr, size_t len)
{
#if defined(HAVE_MUNLOCK)
	return munlock(addr, len);
#else
	return shim_enosys(0, addr, len);
#endif
}

/*
 *  shim_mlock2()
 *	wrapper for mlock2(2) - lock memory
 */
int shim_mlock2(const void *addr, size_t len, int flags)
{
#if defined(__NR_mlock2)
	return (int)syscall(__NR_mlock2, addr, len, flags);
#else
	return shim_enosys(0, addr, len, flags);
#endif
}

/*
 *  shim_mlockall()
 * 	wrapper for mlockall() - lock all memmory
 */
int shim_mlockall(int flags)
{
#if defined(HAVE_MLOCKALL)
	return mlockall(flags);
#else
	return shim_enosys(0, flags);
#endif
}

/*
 *  shim_munlockall()
 * 	wrapper for munlockall() - unlock all memmory
 */
int shim_munlockall(void)
{
/* if HAVE_MLOCKALL defined we also have munlockall */
#if defined(HAVE_MLOCKALL)
	return munlockall();
#else
	return shim_enosys(0);
#endif
}

/*
 *  shim_nanosleep_uint64()
 *	nanosecond sleep that handles being interrupted but will
 *	be preempted if an ALARM signal has triggered a termination
 */
int shim_nanosleep_uint64(uint64_t nsec)
{
#if defined(HAVE_NANOSLEEP)
	struct timespec t, trem;

	t.tv_sec = nsec / 1000000000;
	t.tv_nsec = nsec % 1000000000;

	for (;;) {
		errno = 0;
		if (nanosleep(&t, &trem) < 0) {
			if (errno == EINTR) {
				t = trem;
				if (g_keep_stressing_flag)
					continue;
			} else {
				return -1;
			}
		}
		break;
	}
#else
	useconds_t usec = nsec / 1000;
	const double t_end = time_now() + ((double)usec) / 1000000.0;

	for (;;) {
		errno = 0;
		if (usleep(usec) < 0) {
			if (errno == EINTR) {
				double t_left = t_end - time_now();

				if (t_left < 0.0)
					return 0;
				usec = (useconds_t)(t_left * 1000000.0);
				if (usec == 0)
					return 0;
				if (g_keep_stressing_flag)
					continue;
			} else {
				return -1;
			}
		}
		break;
	}
#endif
	return 0;
}

/*
 *  shim_usleep()
 *	usleep is now deprecated, so
 *	emulate it with nanosleep
 */
int shim_usleep(uint64_t usec)
{
	return shim_nanosleep_uint64(usec * 1000);
}

/*
 *  shim_usleep_interruptible()
 *	interruptible usleep
 */
int shim_usleep_interruptible(uint64_t usec)
{
#if defined(HAVE_NANOSLEEP)
	struct timespec t, trem;

	t.tv_sec = usec / 1000000;
	t.tv_nsec = (usec - (t.tv_sec * 1000000)) * 1000;

	errno = 0;
	return nanosleep(&t, &trem);
#else
	return usleep((useconds_t)usec);
#endif
}


/*
 *  shim_getlogin
 *	a more secure abstracted version of getlogin
 *
 * According to flawfinder:
 * "It's often easy to fool getlogin. Sometimes it does not work at all,
 * because some program messed up the utmp file. Often, it gives only the
 * first 8 characters of the login name. The user currently logged in on the
 * controlling tty of our program need not be the user who started it. Avoid
 * getlogin() for security-related purposes (CWE-807). Use getpwuid(geteuid())
 * and extract the desired information instead."
 */
char *shim_getlogin(void)
{
#if defined(BUILD_STATIC)
	/*
	 *  static builds can't use getpwuid because of
	 *  dynamic linking issues. Ugh.
	 */
	return NULL;
#else
	static char pw_name[256];
	const struct passwd *pw = getpwuid(geteuid());
	if (!pw)
		return NULL;

	(void)shim_strlcpy(pw_name, pw->pw_name, sizeof(pw_name));
	pw_name[sizeof(pw_name) - 1 ] = '\0';

	return pw_name;
#endif
}

/*
 *  shim_msync()
 *	wrapper for msync(2) - synchronize a file with a memory map
 */
int shim_msync(void *addr, size_t length, int flags)
{
#if defined(HAVE_MSYNC)
	return msync(addr, length, flags);
#else
	return shim_enosys(0, addr, length, flags);
#endif
}

/*
 *  shim_sysfs()
 *	wrapper for sysfs(2) - get filesystem type information
 */
int shim_sysfs(int option, ...)
{
#if defined(__NR_sysfs)
	int ret;
	va_list ap;
	char *fsname;
	unsigned int fs_index;
	char *buf;

	va_start(ap, option);

	switch (option) {
	case 1:
		fsname = va_arg(ap, char *);
		ret = syscall(__NR_sysfs, option, fsname);
		break;
	case 2:
		fs_index = va_arg(ap, unsigned int);
		buf = va_arg(ap, char *);
		ret = syscall(__NR_sysfs, option, fs_index, buf);
		break;
	case 3:
		ret = syscall(__NR_sysfs, option);
		break;
	default:
		ret = -1;
		errno = EINVAL;
	}

	va_end(ap);

	return ret;
#else
	return shim_enosys(0, option);
#endif
}

/*
 *  shim_madvise()
 *	wrapper for madvise(2) - get filesystem type information
 */
int shim_madvise(void *addr, size_t length, int advice)
{
#if defined(HAVE_MADVISE)
	return madvise(addr, length, advice);
#elif defined(HAVE_POSIX_MADVISE)
	int posix_advice;

	switch (advice) {
#if defined(POSIX_MADV_NORMAL) && defined(MADV_NORMAL)
	case MADV_NORMAL:
		posix_advice = POSIX_MADV_NORMAL;
		break;
#endif
#if defined(POSIX_MADV_SEQUENTIAL) && defined(MADV_SEQUENTIAL)
	case MADV_SEQUENTIAL:
		posix_advice = POSIX_MADV_SEQUENTIAL;
		break;
#endif
#if defined(POSIX_MADV_RANDOM) && defined(MADV_RANDOM)
	case MADV_RANDOM:
		posix_advice = POSIX_MADV_RANDOM;
		break;
#endif
#if defined(POSIX_MADV_WILLNEED) && defined(MADV_WILLNEED)
	case MADV_WILLNEED:
		posix_advice = POSIX_MADV_WILLNEED;
		break;
#endif
#if defined(POSIX_MADV_DONTNEED) && defined(MADV_DONTNEED)
	case MADV_DONTNEED:
		posix_advice = POSIX_MADV_DONTNEED;
		break;
#endif
	default:
		posix_advice = POSIX_MADV_NORMAL;
		break;
	}
	return posix_madvise(addr, length, posix_advice);
#else
	return shim_enosys(0, addr, length, advice);
#endif
}

/*
 *  shim_mincore()
 *	wrapper for mincore(2) -  determine whether pages are resident in memory
 */
int shim_mincore(void *addr, size_t length, unsigned char *vec)
{
#if defined(HAVE_MINCORE) && NEED_GLIBC(2,2,0)
#if defined(__FreeBSD__) || defined(__OpenBSD__) || \
    defined(__NetBSD__) || defined(__sun__)
	return mincore(addr, length, (char *)vec);
#else
	return mincore(addr, length, vec);
#endif
#else
	return shim_enosys(0, addr, length, vec);
#endif
}

ssize_t shim_statx(
	int dfd,
	const char *filename,
	unsigned int flags,
	unsigned int mask,
	struct shim_statx *buffer)
{
#if defined(__NR_statx)
	return syscall(__NR_statx, dfd, filename, flags, mask, buffer);
#else
	return shim_enosys(0, dfd, filename, flags, mask, buffer);
#endif
}

/*
 *  futex wake()
 *	wake n waiters on futex
 */
int shim_futex_wake(const void *futex, const int n)
{
#if defined(__NR_futex)
	return syscall(__NR_futex, futex, FUTEX_WAKE, n, NULL, NULL, 0);
#else
	return shim_enosys(0, futex, 0, n, NULL, NULL, 0);
#endif
}

/*
 *  futex_wait()
 *	wait on futex with a timeout
 */
int shim_futex_wait(
	const void *futex,
	const int val,
	const struct timespec *timeout)
{
#if defined(__NR_futex)
	return syscall(__NR_futex, futex, FUTEX_WAIT, val, timeout, NULL, 0);
#else
	return shim_enosys(0, futex, 0, val, timeout, NULL, 0);
#endif
}

/*
 *  dup3()
 *	linux special dup
 */
int shim_dup3(int oldfd, int newfd, int flags)
{
#if defined(HAVE_DUP3)
	return dup3(oldfd, newfd, flags);
#else
	return shim_enosys(0, oldfd, newfd, flags);
#endif
}


int shim_sync_file_range(
	int fd,
	shim_off64_t offset,
	shim_off64_t nbytes,
	unsigned int flags)
{
#if defined(HAVE_SYNC_FILE_RANGE)
	return sync_file_range(fd, offset, nbytes, flags);
#elif defined(__NR_sync_file_range)
	return syscall(__NR_sync_file_range, fd, offset, nbytes, flags);
#elif defined(__NR_sync_file_range2)
	/*
	 * from sync_file_range(2):
	 * "Some architectures (e.g., PowerPC, ARM) need  64-bit  arguments
	 * to be aligned in a suitable pair of registers.  On such
	 * architectures, the call signature of sync_file_range() shown in
	 * the SYNOPSIS would force a register to be wasted as padding
	 * between the fd and offset arguments.  (See syscall(2) for details.)
	 * Therefore, these architectures define a different system call that
	 * orders the arguments suitably"
	 */
	return syscall(__NR_sync_file_range2, fd, flags, offset, nbytes);
#else
	return shim_enosys(0, fd, offset, nbytes, flags);
#endif
}

/*
 *  shim_ioprio_set()
 *	ioprio_set system call
 */
int shim_ioprio_set(int which, int who, int ioprio)
{
#if defined(__NR_ioprio_set)
	return syscall(__NR_ioprio_set, which, who, ioprio);
#else
	return shim_enosys(0, which, who, ioprio);
#endif
}

/*
 *  shim_ioprio_get()
 *	ioprio_get system call
 */
int shim_ioprio_get(int which, int who)
{
#if defined(__NR_ioprio_get)
	return syscall(__NR_ioprio_get, which, who);
#else
	return shim_enosys(0, which, who);
#endif
}

/*
 *   shim_brk()
 *	brk system call shim
 */
#if defined(__APPLE__)
PRAGMA_PUSH
PRAGMA_WARN_OFF
#endif
int shim_brk(void *addr)
{
#if defined(__APPLE__)
	return (int)brk(addr);
#else
	return brk(addr);
#endif
}
#if defined(__APPLE__)
PRAGMA_POP
#endif

/*
 *   shim_sbrk()
 *	sbrk system call shim
 */
#if defined(__APPLE__)
PRAGMA_PUSH
PRAGMA_WARN_OFF
#endif
void *shim_sbrk(intptr_t increment)
{
	return sbrk(increment);
}
#if defined(__APPLE__)
PRAGMA_POP
#endif

/*
 *   shim_strlcpy()
 *	wrapper / implementation of BSD strlcpy
 */
size_t shim_strlcpy(char *dst, const char *src, size_t len)
{
#if defined(HAVE_STRLCPY)
	return strlcpy(dst, src, len);
#else
	register char *d = dst;
	register const char *s = src;
	register size_t n = len;

	if (n) {
		while (--n) {
			register char c = *s++;

			*d++ = c;
			if (c == '\0')
				break;
		}
	}

	if (!n) {
		if (len)
			*d = '\0';
		while (*s)
			s++;
	}

	return (s - src - 1);
#endif
}

/*
 *   shim_strlcat()
 *	wrapper / implementation of BSD strlcat
 */
size_t shim_strlcat(char *dst, const char *src, size_t len)
{
#if defined(HAVE_STRLCAT)
	return strlcat(dst, src, len);
#else
	register char *d = dst;
	register const char *s = src;
	register size_t n = len, tmplen;

	while (n-- && *d != '\0') {
		d++;
	}

	tmplen = d - dst;
	n = len - tmplen;

	if (!n) {
		return strlen(s) + tmplen;
	}

	while (*s != '\0') {
		if (n != 1) {
			*d = *s;
			d++;
			n--;
		}
		s++;
	}
	*d = '\0';

	return (s - src) + tmplen;
#endif
}

/*
 *  shim_fsync
 *	wrapper for fsync
 */
int shim_fsync(int fd)
{
#if defined(__APPLE__) && defined(F_FULLFSYNC)
	int ret;

	/*
	 *  For APPLE Mac OS X try to use the full fsync fcntl
	 *  first and then fall back to a potential no-op'd fsync
	 *  implementation.
	 */
	ret = fcntl(fd, F_FULLFSYNC, NULL);
	if (ret == 0)
		return 0;
#endif
	return fsync(fd);
}

/*
 *  shim_fdatasync
 *	wrapper for fdatasync
 */
int shim_fdatasync(int fd)
{
#if defined(HAVE_FDATASYNC)
	/*
	 *  For some reason, fdatasync prototype may be missing
	 *  in some __APPLE__ systems.
	 */
#if defined(__APPLE__)
	extern int fdatasync(int fd);
#endif
	return fdatasync(fd);
#else
	return shim_enosys(0, fd);
#endif
}

/*
 *   shim_pkey_alloc()
 *	wrapper for pkey_alloc()
 */
int shim_pkey_alloc(unsigned long flags, unsigned long access_rights)
{
#if defined(__NR_pkey_alloc)
	return syscall(__NR_pkey_alloc, flags, access_rights);
#else
	return shim_enosys(0, flags, access_rights);
#endif
}

/*
 *   shim_pkey_free()
 *	wrapper for pkey_free()
 */
int shim_pkey_free(int pkey)
{
#if defined(__NR_pkey_free)
	return syscall(__NR_pkey_free, pkey);
#else
	return shim_enosys(0, pkey);
#endif
}

/*
 *   shim_pkey_mprotect()
 *	wrapper for pkey_mprotect()
 */
int shim_pkey_mprotect(void *addr, size_t len, int prot, int pkey)
{
#if defined(__NR_pkey_mprotect)
	return syscall(__NR_pkey_mprotect, addr, len, prot, pkey);
#else
	return shim_enosys(0, addr, len, prot, pkey);
#endif
}

/*
 *   shim_pkey_get()
 *	wrapper for pkey_get()
 */
int shim_pkey_get(int pkey)
{
#if defined(__NR_pkey_get)
	return syscall(__NR_pkey_get, pkey);
#else
	return shim_enosys(0, pkey);
#endif
}

/*
 *   shim_pkey_set()
 *	wrapper for pkey_set()
 */
int shim_pkey_set(int pkey, unsigned int rights)
{
#if defined(__NR_pkey_set)
	return syscall(__NR_pkey_set, pkey, rights);
#else
	return shim_enosys(0, pkey, rights);
#endif
}

/*
 *   shim_execveat()
 *	wrapper for execveat()
 */
int shim_execveat(
        int dir_fd,
        const char *pathname,
        char *const argv[],
        char *const envp[],
        int flags)
{
#if defined(__NR_execveat)
        return syscall(__NR_execveat, dir_fd, pathname, argv, envp, flags);
#else
        return shim_enosys(0, dir_fd, pathname, argv, envp, flags);
#endif
}

/*
 *   shim_getxattr
 *	wrapper for getxattr
 */
ssize_t shim_getxattr(
	const char *path,
	const char *name,
	void *value,
	size_t size)
{
#if defined(HAVE_GETXATTR)
#if defined(__APPLE__)
	return getxattr(path, name, value, size, 0, 0);
#else
	return getxattr(path, name, value, size);
#endif
#else
	return shim_enosys(0, path, name, value, size);
#endif
}

/*
 *   shim_waitpid()
 *	wrapper for waitpid with EINTR retry
 */
pid_t shim_waitpid(pid_t pid, int *wstatus, int options)
{
	pid_t ret;

	for (;;) {
		ret = waitpid(pid, wstatus, options);
		if (ret >= 0)
			break;
		if (errno != EINTR)
			break;
		if (!g_keep_stressing_flag)
			break;
	}
	return ret;
}

/*
 *   shim_pidfd_send_signal()
 *	wrapper for pidfd_send_signal added to Linux 5.1
 */
int shim_pidfd_send_signal(
	int pidfd,
	int sig,
	siginfo_t *info,
	unsigned int flags)
{
#if defined(__NR_pidfd_send_signal)
	return syscall(__NR_pidfd_send_signal, pidfd, sig, info, flags);
#else
	return shim_enosys(0, pidfd, sig, info, flags);
#endif
}

