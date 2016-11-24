/*
 * Copyright (C) 2014-2016 Canonical, Ltd.
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
	loff_t *off_in,
	int fd_out,
	loff_t *off_out,
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

int shim_fallocate(int fd, int mode, off_t offset, off_t len)
{
#if defined(__linux__) && defined(__NR_fallocate)
	return fallocate(fd, mode, offset, len);
#else
	(void)mode;

	return posix_fallocate(fd, offset, len);
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
int shim_getdents64(
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

int shim_getrandom(void *buff, size_t buflen, unsigned int flags)
{
#if defined(__NR_getrandom)
	return syscall(__NR_getrandom, buff, buflen, flags);
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
#if defined(__NR_kcmp)
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
#if defined(__NR_syslog)
        return syscall(__NR_syslog, type, bufp, len);
#else
	(void)type;
	(void)bufp;
	(void)len;

	errno = ENOSYS;
	return -1;
#endif
}    
