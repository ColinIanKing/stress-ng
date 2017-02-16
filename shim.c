/*
 * Copyright (C) 2014-2017 Canonical, Ltd.
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
#include <pwd.h>

/*
 *  Various shim abstraction wrappers around systems calls and
 *  GCC helper functions that may not be supported by some
 *  kernels or versions of different C libraries.
 */

int shim_sched_yield(void)
{
#if defined(_POSIX_PRIORITY_SCHEDULING) && !defined(__minix__)
	return sched_yield();
#else
	return 0;
#endif
}

int shim_cacheflush(char *addr, int nbytes, int cache)
{
#if defined(__linux__) && defined(__NR_cacheflush)
	return (int)syscall(__NR_cacheflush, addr, nbytes, cache);
#else
	(void)addr;
	(void)nbytes;
	(void)cache;

	errno = -ENOSYS;
	return -1;
#endif
}

ssize_t shim_copy_file_range(
	int fd_in,
	shim_loff_t *off_in,
	int fd_out,
	shim_loff_t *off_out,
	size_t len,
	unsigned int flags)
{
#if defined(__linux__) && defined(__NR_copy_file_range)
	return syscall(__NR_copy_file_range,
		fd_in, off_in, fd_out, off_out, len, flags);
#else
	(void)fd_in;
	(void)off_in;
	(void)fd_out;
	(void)off_out;
	(void)len;
	(void)flags;

	errno = -ENOSYS;
	return -1;
#endif
}

/*
 * shim_emulate_fallocate()
 *	emulate fallocate (very slow!)
 */
static int shim_emulate_fallocate(int fd, off_t offset, off_t len)
{
	const off_t buf_sz = 65536;
	char buffer[buf_sz];
	off_t n;

	n = lseek(fd, offset, SEEK_SET);
	if (n == (off_t)-1)
		return -1;

	memset(buffer, 0, buf_sz);
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
#if defined(__linux__) && defined(__NR_fallocate)
	int ret;

	ret = fallocate(fd, mode, offset, len);
	/* mode not supported? try with zero mode (dirty hack) */
	if ((ret < 0) && (errno == EOPNOTSUPP)) {
		ret = fallocate(fd, 0, offset, len);
		/* fallocate failed, try emulation mode */
		if ((ret < 0) && (errno == EOPNOTSUPP)) {
			ret = shim_emulate_fallocate(fd, offset, len);
		}
	}
	return ret;
#elif _POSIX_C_SOURCE >= 200112L
	int ret;

	(void)mode;

	ret = posix_fallocate(fd, offset, len);
	if ((ret < 0) && (errno == ENOSYS))
		ret = shim_emulate_fallocate(fd, offset, len);
	return ret;
#else
	(void)mode;

	return shim_emulate_fallocate(fd, offset, len);
#endif
}

int shim_gettid(void)
{
#if defined(__linux__) && defined(__NR_gettid)
	return syscall(__NR_gettid);
#else
	errno = -ENOSYS;
	return -1;
#endif
}

long shim_getcpu(
	unsigned *cpu,
	unsigned *node,
	void *tcache)
{
#if defined(__linux__) && defined(__NR_getcpu)
	return syscall(__NR_getcpu, cpu, node, tcache);
#else
	(void)cpu;
	(void)node;
	(void)tcache;

	errno = -ENOSYS;
	return -1;
#endif
}

int shim_getdents(
	unsigned int fd,
	struct shim_linux_dirent *dirp,
	unsigned int count)
{
#if defined(__linux__) && defined(__NR_getdents)
	return syscall(__NR_getdents, fd, dirp, count);
#else
	(void)fd;
	(void)dirp;
	(void)count;

	errno = ENOSYS;
	return -1;
#endif
}

int shim_getdents64(
	unsigned int fd,
	struct shim_linux_dirent64 *dirp,
	unsigned int count)
{
#if defined(__linux__) && defined(__NR_getdents64)
	return syscall(__NR_getdents64, fd, dirp, count);
#else
	(void)fd;
	(void)dirp;
	(void)count;

	errno = ENOSYS;
	return -1;
#endif
}

int shim_getrandom(void *buff, size_t buflen, unsigned int flags)
{
#if defined(__linux__) && defined(__NR_getrandom)
	return syscall(__NR_getrandom, buff, buflen, flags);
#elif defined(__OpenBSD__)
	(void)flags;

	return getentropy(buff, buflen);
#else
	(void)buff;
	(void)buflen;
	(void)flags;

	errno = ENOSYS;
	return -1;
#endif
}

void shim_clear_cache(char* begin, char *end)
{
#if defined(__GNUC__) && defined(STRESS_ARM)
	__clear_cache(begin, end);
#else
	(void)begin;
	(void)end;
#endif
}

long shim_kcmp(int pid1, int pid2, int type, int fd1, int fd2)
{
#if defined(__linux__) && defined(__NR_kcmp)
	errno = 0;
	return syscall(__NR_kcmp, pid1, pid2, type, fd1, fd2);
#else
	(void)pid1;
	(void)pid2;
	(void)type;
	(void)fd1;
	(void)fd2;

	errno = ENOSYS;
	return -1;
#endif
}

int shim_syslog(int type, char *bufp, int len)
{
#if defined(__linux__) && defined(__NR_syslog)
	return syscall(__NR_syslog, type, bufp, len);
#else
	(void)type;
	(void)bufp;
	(void)len;

	errno = ENOSYS;
	return -1;
#endif
}

int shim_membarrier(int cmd, int flags)
{
#if defined(__linux__) && defined(__NR_membarrier)
	return syscall(__NR_membarrier, cmd, flags);
#else
	(void)cmd;
	(void)flags;

	errno = ENOSYS;
	return -1;
#endif
}

int shim_memfd_create(const char *name, unsigned int flags)
{
#if defined(__linux__) && defined(__NR_memfd_create)
	return syscall(__NR_memfd_create, name, flags);
#else
	(void)name;
	(void)flags;

	errno = ENOSYS;
	return -1;
#endif
}

int shim_get_mempolicy(
	int *mode,
	unsigned long *nodemask,
	unsigned long maxnode,
	unsigned long addr,
	unsigned long flags)
{
#if defined(__linux__) && defined(__NR_get_mempolicy)
	return syscall(__NR_get_mempolicy,
		mode, nodemask, maxnode, addr, flags);
#else
	(void)mode;
	(void)nodemask;
	(void)maxnode;
	(void)addr;
	(void)flags;

	errno = ENOSYS;
	return -1;
#endif
}

int shim_set_mempolicy(
	int mode,
	unsigned long *nodemask,
	unsigned long maxnode)
{
#if defined(__linux__) && defined(__NR_set_mempolicy)
	return syscall(__NR_set_mempolicy,
		mode, nodemask, maxnode);
#else
	(void)mode;
	(void)nodemask;
	(void)maxnode;

	errno = ENOSYS;
	return -1;
#endif
}

long shim_mbind(
	void *addr,
	unsigned long len,
	int mode,
	const unsigned long *nodemask,
	unsigned long maxnode,
	unsigned flags)
{
#if defined(__linux__) && defined(__NR_mbind)
	return syscall(__NR_mbind,
		addr, len, mode, nodemask, maxnode, flags);
#else
	(void)addr;
	(void)len;
	(void)mode;
	(void)nodemask;
	(void)maxnode;
	(void)flags;

	errno = ENOSYS;
	return -1;
#endif
}

long shim_migrate_pages(
	int pid,
	unsigned long maxnode,
	const unsigned long *old_nodes,
	const unsigned long *new_nodes)
{
#if defined(__linux__) && defined(__NR_migrate_pages)
	return syscall(__NR_migrate_pages,
		pid, maxnode, old_nodes, new_nodes);
#else
	(void)pid;
	(void)maxnode;
	(void)old_nodes;
	(void)new_nodes;

	errno = ENOSYS;
	return -1;
#endif
}

long shim_move_pages(
	int pid,
	unsigned long count,
	void **pages,
	const int *nodes,
	int *status,
	int flags)
{
#if defined(__linux__) && defined(__NR_move_pages)
	return syscall(__NR_move_pages,
		pid, count, pages, nodes,
		status, flags);
#else
	(void)pid;
	(void)count;
	(void)pages;
	(void)nodes;
	(void)status;
	(void)flags;

	errno = ENOSYS;
	return -1;
#endif
}

int shim_userfaultfd(int flags)
{
#if defined(__linux__) && defined(__NR_userfaultfd)
	return syscall(__NR_userfaultfd, flags);
#else
	(void)flags;

	errno = ENOSYS;
	return -1;
#endif
}

int shim_seccomp(unsigned int operation, unsigned int flags, void *args)
{
#if defined(__linux__) && defined(__NR_seccomp)
	return (int)syscall(__NR_seccomp, operation, flags, args);
#else
	(void)operation;
	(void)flags;
	(void)args;

	errno = ENOSYS;
	return -1;
#endif
}

int shim_unshare(int flags)
{
#if defined(__linux__) && defined(__NR_unshare)
#if NEED_GLIBC(2,14,0)
	return unshare(flags);
#else
	return syscall(__NR_unshare, flags);
#endif
#else
	(void)flags;

	errno = ENOSYS;
	return -1;
#endif
}

int shim_sched_getattr(
	pid_t pid,
	struct shim_sched_attr *attr,
	unsigned int size,
	unsigned int flags)
{
#if defined(__linux__) && defined(__NR_sched_getattr)
	return syscall(__NR_sched_getattr, pid, attr, size, flags);
#else
	(void)pid;
	(void)attr;
	(void)size;
	(void)flags;

	errno = ENOSYS;
	return -1;
#endif
}

int shim_sched_setattr(
	pid_t pid,
	struct shim_sched_attr *attr,
	unsigned int flags)
{
#if defined(__linux__) && defined(__NR_sched_setattr)
	return syscall(__NR_sched_setattr, pid, attr, flags);
#else
	(void)pid;
	(void)attr;
	(void)flags;

	errno = ENOSYS;
	return -1;
#endif
}

int shim_mlock2(const void *addr, size_t len, int flags)
{
#if defined(__linux__) && defined(__NR_mlock2)
	return (int)syscall(__NR_mlock2, addr, len, flags);
#else
	(void)addr;
	(void)len;
	(void)flags;

	errno = ENOSYS;
	return -1;
#endif
}

/*
 *  shim_usleep()
 *	usleep is now deprecated, so
 *	emulate it with nanosleep
 */
int shim_usleep(uint64_t usec)
{
#if _POSIX_C_SOURCE >= 199309L
	struct timespec t, trem;

	t.tv_sec = usec / 1000000;
	t.tv_nsec = (usec - (t.tv_sec * 1000000)) * 1000;

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
	return 0;
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
	static char pw_name[256];
	struct passwd *pw;

	pw = getpwuid(geteuid());
	if (!pw)
		return NULL;

	strncpy(pw_name, pw->pw_name, sizeof(pw_name));
	pw_name[sizeof(pw_name) - 1 ] = '\0';

	return pw_name;
}


int shim_msync(void *addr, size_t length, int flags)
{
#if !defined(__gnu_hurd__) && !defined(__minix__)
	return msync(addr, length, flags);
#else
	(void)addr;
	(void)length;
	(void)flags;

	return 0;
#endif
}

int shim_sysfs(int option, ...)
{

#if defined(__linux__) && defined(__NR_sysfs)
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
	(void)option;

	errno = ENOSYS;
	return -1;
#endif
}
