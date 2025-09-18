/*
 * Copyright (C) 2014-2021 Canonical, Ltd.
 * Copyright (C) 2021-2025 Colin Ian King
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
#define STRESS_CORE_SHIM

/*
 *  For ALT Linux gcc use at most _FORTIFY_SOURCE level 2
 *  https://github.com/ColinIanKing/stress-ng/issues/452
 */
#if defined(HAVE_ALT_LINUX_GCC) &&	\
    defined(_FORTIFY_SOURCE) &&		\
    _FORTIFY_SOURCE > 2
#undef _FORTIFY_SOURCE
#define _FORTIFY_SOURCE	2
#endif

#include "stress-ng.h"
#include "core-arch.h"
#include "core-attribute.h"
#include "core-asm-riscv.h"
#include "core-builtin.h"
#include "core-cpu-cache.h"
#include "core-killpid.h"
#include "core-pragma.h"
#include "core-shim.h"

#include <sched.h>
#include <stdarg.h>
#include <time.h>
#include <pwd.h>

#if defined(__NR_pkey_get)
#define HAVE_PKEY_GET
#endif

#if defined(__NR_pkey_set)
#define HAVE_PKEY_SET
#endif

#if defined(HAVE_GRP_H)
#include <grp.h>
#else
UNEXPECTED
#endif

#if defined(HAVE_USTAT_H)
#if defined(__sun__)
/* ustat and long file support on sun does not build */
#undef HAVE_USTAT_H
#else
#include <ustat.h>
#endif
#endif

#if defined(HAVE_LINUX_FUTEX_H)
#include <linux/futex.h>
#endif

#if defined(HAVE_ASM_CACHECTL_H)
#include <asm/cachectl.h>
#endif

#if defined(HAVE_MODIFY_LDT)
#include <asm/ldt.h>
#endif

#if defined(HAVE_SYS_PIDFD_H)
#include <sys/pidfd.h>
#endif

#if defined(HAVE_SYS_PRCTL_H)
#include <sys/prctl.h>
#endif

#if defined(HAVE_SYS_TIMEX_H)
#include <sys/timex.h>
#endif

#if defined(HAVE_SYS_RANDOM_H)
#include <sys/random.h>
#endif

#if defined(HAVE_SYS_SELECT_H)
#include <sys/select.h>
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

#if defined(HAVE_RUSAGE_WHO_T)
typedef __rusage_who_t	shim_rusage_who_t;
#else
typedef int		shim_rusage_who_t;
#endif

#if defined(HAVE_LINUX_MODULE_H)
#include <linux/module.h>
#endif

#if defined(__sun__)
#if defined(HAVE_GETDOMAINNAME)
extern int getdomainname(char *name, size_t len);
#endif
#if defined(HAVE_SETDOMAINNAME)
extern int setdomainname(const char *name, size_t len);
#endif
#endif

#define FALLOCATE_BUF_SIZE	(8192)

/*
 *  Various shim abstraction wrappers around systems calls and
 *  GCC helper functions that may not be supported by some
 *  kernels or versions of different C libraries.
 */

/*
 *  shim_enosys()
 *	simulate unimplemented system call. Ignores
 *	the sysnr argument and all following 1..N syscall
 *	arguments.  Returns -1 and sets errno to ENOSYS
 */
static inline long int shim_enosys(long sysnr, ...)
{
	(void)sysnr;
	errno = ENOSYS;
	return (long int)-1;
}

/*
 *  shim_sched_yield()
 *  	wrapper for sched_yield(2) - yield the processor
 */
int shim_sched_yield(void)
{
#if defined(HAVE_SCHED_YIELD)
	return sched_yield();
#elif defined(__NR_sched_yield) &&	\
      defined(HAVE_SYSCALL)
	return syscall(__NR_sched_yield);
#else
	UNEXPECTED
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
#if defined(HAVE_ASM_CACHECTL_H) &&	\
    defined(HAVE_CACHEFLUSH) && 	\
    defined(STRESS_ARCH_MIPS)
	extern int cacheflush(void *addr, int nbytes, int cache);

	return cacheflush((void *)addr, nbytes, cache);
#elif defined(STRESS_ARCH_RISCV)
#if defined(__NR_riscv_flush_icache)
	if (cache == SHIM_ICACHE) {
		if (syscall(__NR_riscv_flush_icache, (uintptr_t)addr,
			    ((uintptr_t)addr) + nbytes, 0) == 0) {
			return 0;
		}
	}
#endif
#if defined(HAVE_ASM_RISCV_FENCE_I)
	if (cache == SHIM_ICACHE)  {
		stress_asm_riscv_fence_i();
		return 0;
	}
#endif
#if defined(HAVE_ASM_RISCV_FENCE)
	if (cache == SHIM_ICACHE)  {
		stress_asm_riscv_fence();
		return 0;
	}
#endif
	return -1;
#elif defined(HAVE_BUILTIN___CLEAR_CACHE)
	/* More portable builtin */
	(void)cache;

	__builtin___clear_cache((void *)addr, (void *)(addr + nbytes));
	return 0;
#elif defined(__NR_cacheflush) &&	\
      defined(HAVE_SYSCALL)
	/* potentially incorrect args, needs per-arch fixing */
#if defined(STRESS_ARCH_M68K)
	return (int)syscall(__NR_cacheflush, addr, 1 /* cacheline */, cache, nbytes);
#elif defined(STRESS_ARCH_SH4)
	return (int)syscall(__NR_cacheflush, addr, nbytes, (cache == ICACHE) ? 0x4 : 0x3);
#else
	return (int)syscall(__NR_cacheflush, addr, nbytes, cache);
#endif
#else
	return (int)shim_enosys(0, addr, nbytes, cache);
#endif
}

/*
 * shim_copy_file_range()
 *	wrapper for copy_file_range(2), copy range of data
 *	from one file to another
 */
ssize_t shim_copy_file_range(
	int fd_in,
	shim_off64_t *off_in,
	int fd_out,
	shim_off64_t *off_out,
	size_t len,
	unsigned int flags)
{
#if defined(HAVE_COPY_FILE_RANGE)
	return copy_file_range(fd_in, off_in, fd_out, off_out, len, flags);
#elif defined(__NR_copy_file_range) &&	\
      defined(HAVE_SYSCALL)
	return (ssize_t)syscall(__NR_copy_file_range,
		fd_in, off_in, fd_out, off_out, len, flags);
#else
	return (ssize_t)shim_enosys(0, fd_in, off_in, fd_out, off_out, len, flags);
#endif
}

/*
 * shim_emulate_fallocate()
 *	emulate fallocate (very slow!)
 */
static int shim_emulate_fallocate(int fd, off_t offset, off_t len)
{
	char buffer[FALLOCATE_BUF_SIZE];
	off_t n;

	n = lseek(fd, offset, SEEK_SET);
	if (n == (off_t)-1)
		return -1;

	(void)shim_memset(buffer, 0, FALLOCATE_BUF_SIZE);
	n = len;

	while (LIKELY(stress_continue_flag() && (n > 0))) {
		ssize_t ret;
		const size_t count = (size_t)STRESS_MINIMUM(n, FALLOCATE_BUF_SIZE);

		ret = write(fd, buffer, count);
		if (LIKELY(ret >= 0)) {
			n -= STRESS_MINIMUM(ret, n);
		} else {
			return -1;
		}
	}
	return 0;
}

/*
 *  shim_posix_fallocate()
 *	emulation of posix_fallocate using chunks of allocations
 *	and add EINTR support (which is not POSIX, but this is
 *	an emulation wrapper so too bad).
 */
int shim_posix_fallocate(int fd, off_t offset, off_t len)
{
#if defined(HAVE_POSIX_FALLOCATE) &&	\
    !defined(__FreeBSD__) &&		\
    defined(EINVAL) &&			\
    defined(EOPNOTSUPP)
	const off_t chunk_len = (off_t)(1 * MB);
	static bool emulate = false;

	if (emulate) {
		errno = 0;
		if (shim_emulate_fallocate(fd, offset, len) < 0)
			return errno;
		return 0;
	}

	do {
		int ret;
		const off_t sz = (len > chunk_len) ? chunk_len : len;

		errno = 0;
		/* posix fallocate returns err in return and not via errno */
		ret = posix_fallocate(fd, offset, sz);
		if (ret != 0) {
			if ((ret == EINVAL) || (ret == EOPNOTSUPP)) {
				emulate = true;
				errno = 0;
				if (shim_emulate_fallocate(fd, offset, len) < 0)
					return errno;
				return 0;
			}
			return ret;
		}

		/*
		 *  stressor has been signaled to finish, fake
		 *  an EINTR return
		 */
		if (UNLIKELY(!stress_continue_flag()))
			return EINTR;
		offset += sz;
		len -= sz;
	} while (len > 0);

	return 0;
#else
	errno = 0;
	if (shim_emulate_fallocate(fd, offset, len) < 0)
		return errno;
	return 0;
#endif
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
#if defined(FALLOC_FL_PUNCH_HOLE)
		if (mode & FALLOC_FL_PUNCH_HOLE)
			return ret;
#endif
#if defined(FALLOC_FL_COLLAPSE_RANGE)
		if (mode & FALLOC_FL_COLLAPSE_RANGE)
			return ret;
#endif
#if defined(FALLOC_FL_WRITE_ZEROES)
		if (mode & FALLOC_FL_COLLAPSE_RANGE)
			return ret;
#endif
#if defined(__NR_fallocate) &&	\
    defined(HAVE_SYSCALL)
		ret = (int)syscall(__NR_fallocate, fd, 0, offset, len);
#else
		ret = -1;
		errno = ENOSYS;
#endif
		/* fallocate failed, try emulation mode */
		if ((ret < 0) && (errno == EOPNOTSUPP)) {
			ret = shim_emulate_fallocate(fd, offset, len);
		}
	}
	return ret;
#elif defined(__NR_fallocate) &&	\
      defined(HAVE_SYSCALL)
	int ret;

	ret = syscall(__NR_fallocate, fd, mode, offset, len);
	/* mode not supported? try with zero mode (dirty hack) */
	if ((ret < 0) && (errno == EOPNOTSUPP)) {
#if defined(FALLOC_FL_PUNCH_HOLE)
		if (mode & FALLOC_FL_PUNCH_HOLE)
			return ret;
#endif
#if defined(FALLOC_FL_COLLAPSE_RANGE)
		if (mode & FALLOC_FL_COLLAPSE_RANGE)
			return ret;
#endif
#if defined(FALLOC_FL_WRITE_ZEROES)
		if (mode & FALLOC_FL_WRITE_ZEROES)
			return ret;
#endif
		ret = syscall(__NR_fallocate, fd, 0, offset, len);
		/* fallocate failed, try emulation mode */
		if ((ret < 0) && (errno == EOPNOTSUPP)) {
			ret = shim_emulate_fallocate(fd, offset, len);
		}
	}
	return ret;
#elif defined(HAVE_POSIX_FALLOCATE) &&	\
      !defined(__FreeBSD_kernel__)
	/*
	 *  Even though FreeBSD kernels support this, large
	 *  allocations take forever to be interrupted and so
	 *  we don't use this for FreeBSD for now.
	 */
	int ret;

	(void)mode;
#if defined(FALLOC_FL_PUNCH_HOLE)
	if (mode & FALLOC_FL_PUNCH_HOLE)
		return 0;
#endif
#if defined(FALLOC_FL_COLLAPSE_RANGE)
	if (mode & FALLOC_FL_COLLAPSE_RANGE)
		return 0;
#endif
#if defined(FALLOC_FL_WRITE_ZEROES)
	if (mode & FALLOC_FL_WRITE_ZEROES)
		return 0;
#endif
	/*
	 *  posix_fallocate returns 0 for success, > 0 as errno
	 */
	ret = shim_posix_fallocate(fd, offset, len);
	errno = 0;
	if (ret != 0) {
		/* failed, so retry with slower emulated fallocate */
		ret = shim_emulate_fallocate(fd, offset, len);
	}
	return ret;
#else
	(void)mode;
#if defined(FALLOC_FL_PUNCH_HOLE)
	if (mode & FALLOC_FL_PUNCH_HOLE)
		return 0;
#endif
#if defined(FALLOC_FL_COLLAPSE_RANGE)
	if (mode & FALLOC_FL_COLLAPSE_RANGE)
		return 0;
#endif
#if defined(FALLOC_FL_WRITE_ZEROES)
	if (mode & FALLOC_FL_WRITE_ZEROES)
		return 0;
#endif
	return shim_emulate_fallocate(fd, offset, len);
#endif
}

/*
 *  shim_gettid()
 *	wrapper for gettid(2), get thread identification
 */
int shim_gettid(void)
{
#if defined(HAVE_GETTID)
	return gettid();
#elif defined(__NR_gettid) &&	\
      defined(HAVE_SYSCALL)
	return (int)syscall(__NR_gettid);
#else
	UNEXPECTED
	return (int)shim_enosys(0);
#endif
}

/*
 *  shim_getcpu()
 *	wrapper for getcpu(2) - get CPU and NUMA node of
 *	calling thread
 */
long int shim_getcpu(
	unsigned int *cpu,
	unsigned int *node,
	void *tcache)
{
#if defined(HAVE_GETCPU) && !defined(STRESS_ARCH_S390)
	(void)tcache;
	return (long int)getcpu(cpu, node);
#elif defined(__NR_getcpu) &&	\
      defined(HAVE_SYSCALL)
	return (long int)syscall(__NR_getcpu, cpu, node, tcache);
#else
	UNEXPECTED
	return (long int)shim_enosys(0, cpu, node, tcache);
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
#if defined(__NR_getdents) &&	\
    defined(HAVE_SYSCALL)
	return (int)syscall(__NR_getdents, fd, dirp, count);
#else
	return (int)shim_enosys(0, fd, dirp, count);
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
#if defined(__NR_getdents64) &&	\
    defined(HAVE_SYSCALL)
	return (int)syscall(__NR_getdents64, fd, dirp, count);
#else
	return (int)shim_enosys(0, fd, dirp, count);
#endif
}

/*
 *  shim_getrandom()
 *	wrapper for Linux getrandom(2) and BSD getentropy(2)
 */
int shim_getrandom(void *buff, size_t buflen, unsigned int flags)
{
#if defined(HAVE_SYS_RANDOM_H) &&	\
    defined(HAVE_GETRANDOM)
	return (int)getrandom(buff, buflen, flags);
#elif defined(__NR_getrandom) &&	\
      defined(HAVE_SYSCALL)
	return (int)syscall(__NR_getrandom, buff, buflen, flags);
#elif defined(__OpenBSD__) || defined(__APPLE__)
	(void)flags;

	return getentropy(buff, buflen);
#else
	UNEXPECTED
	return (int)shim_enosys(0, buff, buflen, flags);
#endif
}

/*
 *  shim_flush_icache()
 *	wrapper for RISC-V icache flush and ARM __clear_cache gcc intrinsic
 */
void shim_flush_icache(void *begin, void *end)
{
#if defined(HAVE_COMPILER_GCC_OR_MUSL) &&	\
    defined(STRESS_ARCH_ARM)
	__clear_cache(begin, end);
#elif defined(STRESS_ARCH_RISCV) &&		\
      defined(__NR_riscv_flush_icache) && 	\
      defined(HAVE_SYSCALL)
	(void)syscall(__NR_riscv_flush_icache, begin, end, 0);
#else
	(void)shim_enosys(0, begin, end);
#endif
}

/*
 *  shim_kcmp()
 *	wrapper for Linux kcmp(2) - compare two processes to
 *	see if they share a kernel resource.
 */
long int shim_kcmp(pid_t pid1, pid_t pid2, int type, unsigned long int idx1, unsigned long int idx2)
{
#if defined(__NR_kcmp) &&	\
    defined(HAVE_SYSCALL)
	errno = 0;
	return (long int)syscall(__NR_kcmp, pid1, pid2, type, idx1, idx2);
#else
	return (long int)shim_enosys(0, pid1, pid2, type, idx1, idx2);
#endif
}

/*
 *  shim_klogctl()
 *	wrapper for syslog(2) (NOT syslog(3))
 */
int shim_klogctl(int type, char *bufp, int len)
{
#if defined(__NR_syslog) &&	\
    defined(HAVE_SYSCALL)
	return (int)syscall(__NR_syslog, type, bufp, len);
#else
	return (int)shim_enosys(0, type, bufp, len);
#endif
}

/*
 *  shim_membarrier()
 *	wrapper for membarrier(2) - issue memory barriers
 */
int shim_membarrier(int cmd, int flags, int cpu_id)
{
#if defined(HAVE_MEMBARRIER)
	return membarrier(cmd, flags, cpu_id);
#elif defined(__NR_membarrier) &&	\
    defined(HAVE_SYSCALL)
	return (int)syscall(__NR_membarrier, cmd, flags, cpu_id);
#else
	return (int)shim_enosys(0, cmd, flags, cpu_id);
#endif
}

/*
 *  shim_memfd_create()
 *	wrapper for memfd_create(2)
 */
int shim_memfd_create(const char *name, unsigned int flags)
{
#if defined(HAVE_MEMFD_CREATE)
	return memfd_create(name, flags);
#elif defined(__NR_memfd_create) &&	\
      defined(HAVE_SYSCALL)
	return (int)syscall(__NR_memfd_create, name, flags);
#else
	return (int)shim_enosys(0, name, flags);
#endif
}

/*
 *  shim_get_mempolicy()
 *	wrapper for get_mempolicy(2) - get NUMA memory policy
 */
int shim_get_mempolicy(
	int *mode,
	unsigned long int *nodemask,
	unsigned long int maxnode,
	void *addr,
	unsigned long int flags)
{
#if defined(__NR_get_mempolicy) &&	\
    defined(HAVE_SYSCALL)
	return (int)syscall(__NR_get_mempolicy,
		mode, nodemask, maxnode, addr, flags);
#else
	return (int)shim_enosys(0, mode, nodemask, maxnode, addr, flags);
#endif
}

/*
 *  shim_set_mempolicy()
 *	wrapper for set_mempolicy(2) - set NUMA memory policy
 */
int shim_set_mempolicy(
	int mode,
	unsigned long int *nodemask,
	unsigned long int maxnode)
{
#if defined(__NR_set_mempolicy) &&	\
    defined(HAVE_SYSCALL)
	return (int)syscall(__NR_set_mempolicy,
		mode, nodemask, maxnode);
#else
	return (int)shim_enosys(0, mode, nodemask, maxnode);
#endif
}

/*
 *  shim_mbind()
 *	wrapper for mbind(2) - set memory policy for a memory range
 */
long int shim_mbind(
	void *addr,
	unsigned long int len,
	int mode,
	const unsigned long int *nodemask,
	unsigned long int maxnode,
	unsigned flags)
{
#if defined(__NR_mbind) &&	\
    defined(HAVE_SYSCALL)
	return (long int)syscall(__NR_mbind,
		addr, len, mode, nodemask, maxnode, flags);
#else
	return (long int)shim_enosys(0, addr, len, mode, nodemask, maxnode, flags);
#endif
}

/*
 *  shim_migrate_pages()
 *	wrapper for migrate_pages(2) - move all pages in a process to other nodes
 */
long int shim_migrate_pages(
	int pid,
	unsigned long int maxnode,
	const unsigned long int *old_nodes,
	const unsigned long int *new_nodes)
{
#if defined(__NR_migrate_pages) &&	\
    defined(HAVE_SYSCALL)
	return (long int)syscall(__NR_migrate_pages,
		pid, maxnode, old_nodes, new_nodes);
#else
	return (long int)shim_enosys(0, pid, maxnode, old_nodes, new_nodes);
#endif
}

/*
 *  shim_move_pages()
 *	wrapper for move_pages(2) - move pages in a process to other nodes
 */
long int shim_move_pages(
	int pid,
	unsigned long int count,
	void **pages,
	const int *nodes,
	int *status,
	int flags)
{
#if defined(__NR_move_pages) &&	\
    defined(HAVE_SYSCALL)
	return (long int)syscall(__NR_move_pages, pid, count, pages, nodes,
		status, flags);
#else
	return (long int)shim_enosys(0, pid, count, pages, nodes, status, flags);
#endif
}

/*
 *  shim_userfaultfd()
 *	wrapper for userfaultfd(2)
 */
int shim_userfaultfd(int flags)
{
#if defined(__NR_userfaultfd) &&	\
    defined(HAVE_SYSCALL)
	return (int)syscall(__NR_userfaultfd, flags);
#else
	return (int)shim_enosys(0, flags);
#endif
}

/*
 *  shim_seccomp()
 *	wrapper for seccomp(2) - operate on Secure Computing state of process
 */
int shim_seccomp(unsigned int operation, unsigned int flags, void *args)
{
#if defined(__NR_seccomp) &&	\
    defined(HAVE_SYSCALL)
	return (int)syscall(__NR_seccomp, operation, flags, args);
#else
	return (int)shim_enosys(0, operation, flags, args);
#endif
}

/*
 *  shim_unshare()
 *	wrapper for unshare(2)
 */
int shim_unshare(int flags)
{
#if defined(HAVE_UNSHARE)
	return unshare(flags);
#elif defined(__NR_unshare) &&	\
      defined(HAVE_SYSCALL)
	return (int)syscall(__NR_unshare, flags);
#else
	return (int)shim_enosys(0, flags);
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
#if defined(__NR_sched_getattr) &&	\
    defined(HAVE_SYSCALL)
	return (int)syscall(__NR_sched_getattr, pid, attr, size, flags);
#else
	return (int)shim_enosys(0, pid, attr, size, flags);
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
#if defined(__NR_sched_setattr) &&	\
    defined(HAVE_SYSCALL)
	return (int)syscall(__NR_sched_setattr, pid, attr, flags);
#else
	return (int)shim_enosys(0, pid, attr, flags);
#endif
}

/*
 *  shim_mlock()
 *	wrapper for mlock(2) - lock memory
 */
int shim_mlock(const void *addr, size_t len)
{
#if defined(HAVE_MLOCK)
	return mlock(shim_unconstify_ptr(addr), len);
#elif defined(__NR_mlock) &&	\
      defined(HAVE_SYSCALL)
	return (int)syscall(__NR_mlock, addr, len);
#else
	return (int)shim_enosys(0, addr, len);
#endif
}

/*
 *  shim_munlock()
 *	wrapper for munlock(2) - unlock memory
 */
int shim_munlock(const void *addr, size_t len)
{
#if defined(HAVE_MUNLOCK)
	return munlock(shim_unconstify_ptr(addr), len);
#elif defined(__NR_munlock) &&	\
      defined(HAVE_SYSCALL)
	return (int)syscall(__NR_munlock, addr, len);
#else
	return (int)shim_enosys(0, addr, len);
#endif
}

/*
 *  shim_mlock2()
 *	wrapper for mlock2(2) - lock memory; force call
 *	via syscall if possible as zero flags call goes
 *	via mlock() in libc or mlock2() for non-zero flags.
 */
int shim_mlock2(const void *addr, size_t len, int flags)
{
#if defined(__NR_mlock2) &&	\
    defined(HAVE_SYSCALL)
	return (int)syscall(__NR_mlock2, addr, len, flags);
#elif defined(HAVE_MLOCK2)
	return mlock2(addr, len, flags);
#else
	return (int)shim_enosys(0, addr, len, flags);
#endif
}

/*
 *  shim_mlockall()
 * 	wrapper for mlockall() - lock all memory
 */
int shim_mlockall(int flags)
{
#if defined(HAVE_MLOCKALL)
	return mlockall(flags);
#elif defined(__NR_mlockall) &&	\
      defined(HAVE_SYSCALL)
	return (int)syscall(__NR_mlockall, flags);
#else
	return (int)shim_enosys(0, flags);
#endif
}

/*
 *  shim_munlockall()
 * 	wrapper for munlockall() - unlock all memory
 */
int shim_munlockall(void)
{
#if defined(HAVE_MUNLOCKALL)
	return munlockall();
#elif defined(__NR_munlockall) &&	\
      defined(HAVE_SYSCALL)
	return (int)syscall(__NR_munlockall);
#else
	return (int)shim_enosys(0);
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

	t.tv_sec = nsec / STRESS_NANOSECOND;
	t.tv_nsec = nsec % STRESS_NANOSECOND;

	for (;;) {
		errno = 0;
		if (nanosleep(&t, &trem) < 0) {
			if (errno == EINTR) {
				t = trem;
				if (LIKELY(stress_continue_flag()))
					continue;
			} else {
				return -1;
			}
		}
		break;
	}
#else
	useconds_t usec = nsec / 1000;
	const double t_end = stress_time_now() + (((double)usec) * ONE_MILLIONTH);

	for (;;) {
		errno = 0;
		if (usleep(usec) < 0) {
			if (errno == EINTR) {
				const double t_left = t_end - stress_time_now();

				if (t_left < 0.0)
					return 0;
				usec = (useconds_t)(t_left * STRESS_DBL_MICROSECOND);
				if (usec == 0)
					return 0;
				if (LIKELY(stress_continue_flag()))
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

	t.tv_sec = (time_t)((double)usec * ONE_MILLIONTH);
	t.tv_nsec = ((long int)usec - (t.tv_sec * 1000000)) * 1000;

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

	(void)shim_strscpy(pw_name, pw->pw_name, sizeof(pw_name));
	pw_name[sizeof(pw_name) - 1] = '\0';

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
#elif defined(__NR_msync) &&	\
      defined(HAVE_SYSCALL)
	return (int)syscall(__NR_msync, addr, length, flags);
#else
	return (int)shim_enosys(0, addr, length, flags);
#endif
}

/*
 *  shim_sysfs()
 *	wrapper for sysfs(2) - get filesystem type information
 */
int shim_sysfs(int option, ...)
{
#if defined(__NR_sysfs) &&	\
    defined(HAVE_SYSCALL)
	int ret;
	va_list ap;
	char *fsname;
	unsigned int fs_index;
	char *buf;

	va_start(ap, option);

	switch (option) {
	case 1:
		fsname = va_arg(ap, char *);
		ret = (int)syscall(__NR_sysfs, option, fsname);
		break;
	case 2:
		fs_index = va_arg(ap, unsigned int);
		buf = va_arg(ap, char *);
		ret = (int)syscall(__NR_sysfs, option, fs_index, buf);
		break;
	case 3:
		ret = (int)syscall(__NR_sysfs, option);
		break;
	default:
		ret = -1;
		errno = EINVAL;
	}

	va_end(ap);

	return ret;
#else
	return (int)shim_enosys(0, option);
#endif
}

/*
 *  shim_madvise()
 *	wrapper for madvise(2) - get filesystem type information
 */
int shim_madvise(void *addr, size_t length, int advice)
{
#if defined(HAVE_MADVISE)
	int madvice;

	switch (advice) {
#if defined(POSIX_MADV_NORMAL) &&	\
    defined(MADV_NORMAL)
	case POSIX_MADV_NORMAL:
		madvice = MADV_NORMAL;
		break;
#endif
#if defined(POSIX_MADV_SEQUENTIAL) &&	\
    defined(MADV_SEQUENTIAL)
	case POSIX_MADV_SEQUENTIAL:
		madvice = MADV_SEQUENTIAL;
		break;
#endif
#if defined(POSIX_MADV_RANDOM) &&	\
    defined(MADV_RANDOM)
	case POSIX_MADV_RANDOM:
		madvice = MADV_RANDOM;
		break;
#endif
#if defined(POSIX_MADV_WILLNEED) &&	\
    defined(MADV_WILLNEED)
	case POSIX_MADV_WILLNEED:
		madvice = MADV_WILLNEED;
		break;
#endif
#if defined(POSIX_MADV_DONTNEED) &&	\
    defined(MADV_DONTNEED)
	case POSIX_MADV_DONTNEED:
		madvice = MADV_DONTNEED;
		break;
#endif
	default:
		madvice = advice;
		break;
	}
	return (int)madvise(addr, length, madvice);
#elif defined(__NR_madvise) &&	\
      defined(HAVE_SYSCALL)
	return (int)syscall(__NR_madvise, addr, length, advice);
#elif defined(HAVE_POSIX_MADVISE)
	int posix_advice;

	switch (advice) {
#if defined(POSIX_MADV_NORMAL) &&	\
    defined(MADV_NORMAL)
	case MADV_NORMAL:
		posix_advice = POSIX_MADV_NORMAL;
		break;
#endif
#if defined(POSIX_MADV_SEQUENTIAL) &&	\
    defined(MADV_SEQUENTIAL)
	case MADV_SEQUENTIAL:
		posix_advice = POSIX_MADV_SEQUENTIAL;
		break;
#endif
#if defined(POSIX_MADV_RANDOM) &&	\
    defined(MADV_RANDOM)
	case MADV_RANDOM:
		posix_advice = POSIX_MADV_RANDOM;
		break;
#endif
#if defined(POSIX_MADV_WILLNEED) &&	\
    defined(MADV_WILLNEED)
	case MADV_WILLNEED:
		posix_advice = POSIX_MADV_WILLNEED;
		break;
#endif
#if defined(POSIX_MADV_DONTNEED) &&	\
    defined(MADV_DONTNEED)
	case MADV_DONTNEED:
		posix_advice = POSIX_MADV_DONTNEED;
		break;
#endif
	default:
		posix_advice = POSIX_MADV_NORMAL;
		break;
	}
	return (int)posix_madvise(addr, length, posix_advice);
#else
	return (int)shim_enosys(0, addr, length, advice);
#endif
}

/*
 *  shim_mincore()
 *	wrapper for mincore(2) -  determine whether pages are resident in memory
 */
int shim_mincore(void *addr, size_t length, unsigned char *vec)
{
#if defined(HAVE_MINCORE) &&	\
    NEED_GLIBC(2,2,0)
#if defined(__FreeBSD__) || defined(__OpenBSD__) || \
    defined(__NetBSD__) || defined(__sun__)
	return mincore(addr, length, (char *)vec);
#else
	return mincore(addr, length, vec);
#endif
#elif defined(__NR_mincore) &&	\
      defined(HAVE_SYSCALL)
	return (int)syscall(__NR_mincore, addr, length, vec);
#else
	return (int)shim_enosys(0, addr, length, vec);
#endif
}

/*
 *  shim_statx()
 *	extended stat, Linux only variant
 */
int shim_statx(
	int dfd,
	const char *filename,
	int flags,
	unsigned int mask,
	shim_statx_t *buffer)
{
	(void)shim_memset(buffer, 0, sizeof(*buffer));
#if defined(HAVE_STATX)
	return statx(dfd, filename, flags, mask, (struct statx *)buffer);
#elif defined(__NR_statx) &&	\
      defined(HAVE_SYSCALL)
	return syscall(__NR_statx, dfd, filename, flags, mask, buffer);
#else
	return shim_enosys(0, dfd, filename, flags, mask, buffer);
#endif
}

#undef STATX_COPY
#undef STATX_COPY_TS

/*
 *  futex wake()
 *	wake n waiters on futex
 */
int shim_futex_wake(const void *futex, const int n)
{
#if defined(__NR_futex) &&	\
    defined(FUTEX_WAKE) &&	\
    defined(HAVE_SYSCALL)
	return (int)syscall(__NR_futex, futex, FUTEX_WAKE, n, NULL, NULL, 0);
#else
	return (int)shim_enosys(0, futex, 0, n, NULL, NULL, 0);
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
#if defined(__NR_futex) &&	\
    defined(FUTEX_WAKE) &&	\
    defined(HAVE_SYSCALL)
	return (int)syscall(__NR_futex, futex, FUTEX_WAIT, val, timeout, NULL, 0);
#else
	return (int)shim_enosys(0, futex, 0, val, timeout, NULL, 0);
#endif
}

/*
 *  shim_dup3()
 *	linux special dup3
 */
int shim_dup3(int oldfd, int newfd, int flags)
{
#if defined(HAVE_DUP3)
	return dup3(oldfd, newfd, flags);
#elif defined(__NR_dup3) &&	\
      defined(HAVE_SYSCALL)
	return (int)syscall(__NR_dup3, oldfd, newfd, flags);
#else
	return (int)shim_enosys(0, oldfd, newfd, flags);
#endif
}

/*
 *  shim_sync_file_range()
 *	sync data on a specified data range
 */
int shim_sync_file_range(
	int fd,
	shim_off64_t offset,
	shim_off64_t nbytes,
	unsigned int flags)
{
#if defined(HAVE_SYNC_FILE_RANGE)
	return sync_file_range(fd, offset, nbytes, flags);
#elif defined(__NR_sync_file_range) &&	\
      defined(HAVE_SYSCALL)
	return (int)syscall(__NR_sync_file_range, fd, offset, nbytes, flags);
#elif defined(__NR_sync_file_range2) &&	\
      defined(HAVE_SYSCALL)
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
	return (int)syscall(__NR_sync_file_range2, fd, flags, offset, nbytes);
#else
	return (int)shim_enosys(0, fd, offset, nbytes, flags);
#endif
}

/*
 *  shim_ioprio_set()
 *	ioprio_set system call
 */
int shim_ioprio_set(int which, int who, int ioprio)
{
#if defined(__NR_ioprio_set) &&	\
    defined(HAVE_SYSCALL)
	return (int)syscall(__NR_ioprio_set, which, who, ioprio);
#else
	return (int)shim_enosys(0, which, who, ioprio);
#endif
}

/*
 *  shim_ioprio_get()
 *	ioprio_get system call
 */
int shim_ioprio_get(int which, int who)
{
#if defined(__NR_ioprio_get) &&	\
    defined(HAVE_SYSCALL)
	return (int)syscall(__NR_ioprio_get, which, who);
#else
	return (int)shim_enosys(0, which, who);
#endif
}

/*
 *   shim_brk()
 *	brk system call shim
 */
#if defined(__APPLE__)
STRESS_PRAGMA_PUSH
STRESS_PRAGMA_WARN_OFF
#endif
int shim_brk(void *addr)
{
#if defined(__APPLE__)
	return (int)brk(addr);
#elif defined(__NR_brk) &&	\
      defined(HAVE_SYSCALL)
	VOID_RET(int, (int)syscall(__NR_brk, addr));
	return (errno == 0) ? 0 : ENOMEM;
#elif defined(HAVE_BRK)
	return brk(addr);
#else
	const uintptr_t brkaddr = (uintptr_t)shim_sbrk(0);
	intptr_t inc = brkaddr - (intptr_t)addr;
	const void *newbrk = shim_sbrk(inc);

	if (UNLIKELY(newbrk == (void *)-1)) {
		if (errno != ENOSYS)
			errno = ENOMEM;
		return -1;
	}
	return 0;
#endif
}
#if defined(__APPLE__)
STRESS_PRAGMA_POP
#endif

/*
 *   shim_sbrk()
 *	sbrk system call shim
 */
#if defined(__APPLE__)
STRESS_PRAGMA_PUSH
STRESS_PRAGMA_WARN_OFF
#endif
void *shim_sbrk(intptr_t increment)
{
#if defined(HAVE_SBRK)
	return sbrk(increment);
#elif defined(__NR_sbrk) &&	\
      defined(HAVE_SYSCALL)
	return (void *)syscall(__NR_sbrk, increment);
#else
	UNEXPECTED
	return (void *)(intptr_t)shim_enosys(0, increment);
#endif
}
#if defined(__APPLE__)
STRESS_PRAGMA_POP
#endif

/*
 *  shim_strscpy()
 *	safer string copy
 */
ssize_t shim_strscpy(char *dst, const char *src, size_t len)
{
	register size_t i;

	if (UNLIKELY(!len || (len > INT_MAX)))
		return -E2BIG;

	for (i = 0; len; i++, len--) {
		register const char ch = src[i];

		dst[i] = ch;
		if (!ch)
			return (ssize_t)i;
	}
	if (i)
		dst[i - 1] = '\0';
	return -E2BIG;
}

/*
 *   shim_strlcat()
 *	wrapper / implementation of BSD strlcat
 */
size_t shim_strlcat(char *dst, const char *src, size_t len)
{
#if defined(HAVE_BSD_STRLCAT) &&	\
    !defined(BUILD_STATIC)
	return strlcat(dst, src, len);
#else
	register char *d = dst;
	register const char *s = src;
	register size_t n = len, tmplen;

	while (n-- && (*d != '\0'))
		d++;

	tmplen = d - dst;
	n = len - tmplen;

	if (!n)
		return strlen(s) + tmplen;

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
 *  shim_sync
 *	wrapper of sync
 */
void shim_sync(void)
{
#if defined(HAVE_SYNC)
	sync();
#endif
}

/*
 *  shim_fsync
 *	wrapper for fsync
 */
int shim_fsync(int fd)
{
#if defined(__APPLE__) &&	\
    defined(F_FULLFSYNC)
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
#elif defined(__NR_fdatasync) &&	\
      defined(HAVE_SYSCALL)
	return (int)syscall(__NR_fdatasync, fd);
#else
	return (int)shim_enosys(0, fd);
#endif
}

/*
 *   shim_pkey_alloc()
 *	wrapper for pkey_alloc()
 */
int shim_pkey_alloc(unsigned int flags, unsigned int access_rights)
{
#if defined(HAVE_PKEY_ALLOC)
	return pkey_alloc(flags, access_rights);
#elif defined(__NR_pkey_alloc) &&	\
      defined(HAVE_SYSCALL)
	return (int)syscall(__NR_pkey_alloc, flags, access_rights);
#else
	return (int)shim_enosys(0, flags, access_rights);
#endif
}

/*
 *   shim_pkey_free()
 *	wrapper for pkey_free()
 */
int shim_pkey_free(int pkey)
{
#if defined(HAVE_PKEY_FREE)
	return pkey_free(pkey);
#elif defined(__NR_pkey_free) &&	\
      defined(HAVE_SYSCALL)
	return (int)syscall(__NR_pkey_free, pkey);
#else
	return (int)shim_enosys(0, pkey);
#endif
}

/*
 *   shim_pkey_mprotect()
 *	wrapper for pkey_mprotect()
 */
int shim_pkey_mprotect(void *addr, size_t len, int prot, int pkey)
{
#if defined(HAVE_PKEY_MPROTECT)
	return pkey_mprotect(addr, len, prot, pkey);
#elif defined(__NR_pkey_mprotect) &&	\
      defined(HAVE_SYSCALL)
	return (int)syscall(__NR_pkey_mprotect, addr, len, prot, pkey);
#else
	return (int)shim_enosys(0, addr, len, prot, pkey);
#endif
}

/*
 *   shim_pkey_get()
 *	wrapper for pkey_get()
 */
int shim_pkey_get(int pkey)
{
#if defined(HAVE_PKEY_GET)
	return pkey_get(pkey);
#elif defined(__NR_pkey_get) &&	\
      defined(HAVE_SYSCALL)
	return (int)syscall(__NR_pkey_get, pkey);
#else
	return (int)shim_enosys(0, pkey);
#endif
}

/*
 *   shim_pkey_set()
 *	wrapper for pkey_set()
 */
int shim_pkey_set(int pkey, unsigned int rights)
{
#if defined(HAVE_PKEY_SET)
	return pkey_set(pkey, rights);
#elif defined(__NR_pkey_set) &&	\
      defined(HAVE_SYSCALL)
	return (int)syscall(__NR_pkey_set, pkey, rights);
#else
	return (int)shim_enosys(0, pkey, rights);
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
#if defined(__NR_execveat) &&	\
    defined(HAVE_SYSCALL)
	return (int)syscall(__NR_execveat, dir_fd, pathname, argv, envp, flags);
#else
	return (int)shim_enosys(0, dir_fd, pathname, argv, envp, flags);
#endif
}

/*
 *   shim_waitpid()
 *	wrapper for waitpid with EINTR retry
 */
pid_t shim_waitpid(pid_t pid, int *wstatus, int options)
{
	pid_t ret;
	int count = 0;

	for (;;) {
		errno = 0;
#if defined(HAVE_WAITPID)
		ret = waitpid(pid, wstatus, options);
#elif defined(__NR_waitpid) &&	\
    defined(HAVE_SYSCALL)
		ret = (pid_t)syscall(__NR_waitpid, pid, wstatus, options);
#else
		ret = (pid_t)shim_enosys(0, pid, wstatus, options);
#endif
		if ((ret >= 0) || (errno != EINTR))
			break;

		count++;
		/*
		 *  Retry if EINTR unless we've have 2 mins
		 *  consecutive EINTRs then give up.
		 */
		if (UNLIKELY(!stress_continue_flag())) {
			(void)shim_kill(pid, SIGALRM);
			if (count > 120)
				(void)stress_kill_pid(pid);
		}
		/*
		 *  Process seems unkillable, report and bail out after 10 mins
		 */
		if (UNLIKELY(count > 600)) {
			pr_dbg("waitpid: SIGALRM on PID %" PRIdMAX" has not resulted in "
				"process termination after 10 minutes, giving up\n",
				(intmax_t)pid);
			break;
		}
		if (count > 10)
			(void)sleep(1);
	}
	return ret;
}

/*
 *   shim_wait()
 *	shim wrapper for wait, try waitpid if available
 */
pid_t shim_wait(int *wstatus)
{
#if defined(HAVE_WAITPID)
	return waitpid(-1, wstatus, 0);
#elif defined(__NR_waitpid) &&	\
    defined(HAVE_SYSCALL)
	return (pid_t)syscall(__NR_waitpid, -1, wstatus, 0);
#else
	return wait(wstatus);
#endif
}

/*
 *   shim_wait3()
 *	wrapper for wait3()
 *
 */
pid_t shim_wait3(int *wstatus, int options, struct rusage *rusage)
{
#if defined(__NR_wait3) &&	\
    defined(HAVE_SYSCALL)
	return (pid_t)syscall(__NR_wait3, wstatus, options, rusage);
#elif defined(HAVE_WAIT3)
	return wait3(wstatus, options, rusage);
#else
	return (pid_t)shim_enosys(0, wstatus, options, rusage);
#endif
}

/*
 *   shim_wait4()
 *	wrapper for wait4()
 *
 */
pid_t shim_wait4(pid_t pid, int *wstatus, int options, struct rusage *rusage)
{
#if defined(__NR_wait4) &&	\
    defined(HAVE_SYSCALL)
	return (pid_t)syscall(__NR_wait4, pid, wstatus, options, rusage);
#elif defined(HAVE_WAIT4)
	return wait4(pid, wstatus, options, rusage);
#else
	return (pid_t)shim_enosys(0, pid, wstatus, options, rusage);
#endif
}

/*
 *   shim_exit_group()
 *	wrapper for exit_group(), fall back to _exit() if it does
 *	not exist
 */
void shim_exit_group(int status)
{
#if defined(__NR_exit_group) &&	\
    defined(HAVE_SYSCALL)
	(void)syscall(__NR_exit_group, status);
#else
	_exit(status);
#endif
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
#if defined(__NR_pidfd_send_signal) &&	\
    defined(HAVE_SYSCALL)
	return (int)syscall(__NR_pidfd_send_signal, pidfd, sig, info, flags);
#else
	return (int)shim_enosys(0, pidfd, sig, info, flags);
#endif
}

/*
 *   shim_pidfd_open()
 *	wrapper for pidfd_open
 */
int shim_pidfd_open(pid_t pid, unsigned int flags)
{
#if defined(HAVE_PIDFD_OPEN)
	return pidfd_open(pid, flags);
#elif defined(__NR_pidfd_open) &&	\
      defined(HAVE_SYSCALL)
	return (int)syscall(__NR_pidfd_open, pid, flags);
#else
	return (int)shim_enosys(0, pid, flags);
#endif
}

/*
 *   shim_pidfd_getfd()
 *	wrapper for pidfd_getfd
 */
int shim_pidfd_getfd(int pidfd, int targetfd, unsigned int flags)
{
#if defined(HAVE_PIDFD_GETFD)
	return pidfd_getfd(pidfd, targetfd, flags);
#elif defined(__NR_pidfd_getfd) &&	\
      defined(HAVE_SYSCALL)
	return (int)syscall(__NR_pidfd_getfd, pidfd, targetfd, flags);
#else
	return (int)shim_enosys(0, pidfd, targetfd, flags);
#endif
}

/*
 *  If we have the fancy new Linux 5.2 mount system
 *  calls then provide shim wrappers for these
 */
int shim_fsopen(const char *fsname, unsigned int flags)
{
#if defined(__NR_fsopen) &&	\
    defined(HAVE_SYSCALL)
	return (int)syscall(__NR_fsopen, fsname, flags);
#else
	return (int)shim_enosys(0, fsname, flags);
#endif
}

/*
 *  shim_fsmount()
 *	wrapper for fsmount
 */
int shim_fsmount(int fd, unsigned int flags, unsigned int ms_flags)
{
#if defined(__NR_fsmount) &&	\
    defined(HAVE_SYSCALL)
	return (int)syscall(__NR_fsmount, fd, flags, ms_flags);
#else
	return (int)shim_enosys(0, fd, flags, ms_flags);
#endif
}

/*
 *  shim_fsconfig()
 *	wrapper for fsconfig
 */
int shim_fsconfig(
	int fd,
	unsigned int cmd,
	const char *key,
	const void *value,
	int aux)
{
#if defined(__NR_fsconfig) &&	\
    defined(HAVE_SYSCALL)
	return (int)syscall(__NR_fsconfig, fd, cmd, key, value, aux);
#else
	return (int)shim_enosys(0, fd, cmd, key, value, aux);
#endif
}

/*
 *  shim_move_mount()
 *	wrapper for move_mount
 */
int shim_move_mount(
	int from_dfd,
	const char *from_pathname,
	int to_dfd,
	const char *to_pathname,
	unsigned int flags)
{
#if defined(__NR_move_mount) &&	\
    defined(HAVE_SYSCALL)
	return (int)syscall(__NR_move_mount, from_dfd, from_pathname,
		to_dfd, to_pathname, flags);
#else
	return (int)shim_enosys(0, from_dfd, from_pathname,
		to_dfd, to_pathname, flags);
#endif
}

/*
 *  shim_clone3()
 *	Linux clone3 system call wrapper
 */
int shim_clone3(struct shim_clone_args *cl_args, size_t size)
{
#if defined(__NR_clone3) &&	\
    defined(HAVE_SYSCALL)
	return (int)syscall(__NR_clone3, cl_args, size);
#else
	return (int)shim_enosys(0, cl_args, size);
#endif
}

/*
 *  shim_ustat
 *	ustat system call wrapper
 */
int shim_ustat(dev_t dev, struct shim_ustat *ubuf)
{
#if defined(HAVE_USTAT) &&	\
     defined(HAVE_USTAT_H)
	return ustat(dev, (void *)ubuf);
#elif defined(__NR_ustat) &&	\
      defined(HAVE_SYSCALL)
	return (int)syscall(__NR_ustat, dev, ubuf);
#else
	return (int)shim_enosys(0, dev, ubuf);
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
#elif defined(__NR_getxattr) &&	\
      defined(HAVE_SYSCALL)
	return (ssize_t)syscall(__NR_getxattr, path, name, value, size);
#else
	return (ssize_t)shim_enosys(0, path, name, value, size);
#endif
}

/*
 *  shim_getxattrat
 *	wrapper for getxattrat (Linux 6.13)
 */
ssize_t shim_getxattrat(int dfd, const char *path, unsigned int at_flags,
		    const char *name, struct shim_xattr_args *args,
		    size_t size)
{
#if defined(HAVE_GETXATTRAT)
	return (ssize_t)getxattrat(dfd, path, name, at_flags, name, args, size);
#elif defined(__NR_getxattrat) &&	\
      defined(HAVE_SYSCALL)
	return (ssize_t)syscall(__NR_getxattrat, dfd, path, name, at_flags, name, args, size);
#else
	return (ssize_t)shim_enosys(0, dfd, path, name, at_flags, name, args, size);
#endif
}

/*
 *  shim_listxattr
 *	wrapper for listxattr
 */
ssize_t shim_listxattr(const char *path, char *list, size_t size)
{
#if defined(HAVE_LISTXATTR)
#if defined(__APPLE__)
	return listxattr(path, list, size, 0);
#else
	return listxattr(path, list, size);
#endif
#elif defined(__NR_listxattr) &&	\
      defined(HAVE_SYSCALL)
	return (ssize_t)syscall(__NR_listxattr, path, list, size);
#else
	return (ssize_t)shim_enosys(0, path, list, size);
#endif
}

/*
 *  shim_listxattrat
 *	wrapper for listxattrat
 */
ssize_t shim_listxattrat(int dfd, const char *path, unsigned int at_flags, char *list, size_t size)
{
#if defined(HAVE_LISTXATTRAT)
	return listxattrat(dfd, path, at_flags, list, size);
#elif defined(__NR_listxattrat) &&	\
      defined(HAVE_SYSCALL)
	return (ssize_t)syscall(__NR_listxattrat, dfd, path, at_flags, list, size);
#else
	return (ssize_t)shim_enosys(0, dfd, path, at_flags, list, size);
#endif
}

/*
 *  shim_flistxattr
 *	wrapper for flistxattr
 */
ssize_t shim_flistxattr(int fd, char *list, size_t size)
{
#if defined(HAVE_FLISTXATTR)
#if defined(__APPLE__)
	return flistxattr(fd, list, size, 0);
#else
	return flistxattr(fd, list, size);
#endif
#elif defined(__NR_flistxattr) &&	\
      defined(HAVE_SYSCALL)
	return (ssize_t)syscall(__NR_flistxattr, fd, list, size);
#else
	return (ssize_t)shim_enosys(0, fd, list, size);
#endif
}

/*
 *  shim_setxattr
 *	wrapper for setxattr
 */
int shim_setxattr(const char *path, const char *name, const void *value, size_t size, int flags)
{
#if defined(HAVE_SETXATTR)
#if defined(__APPLE__)
	return setxattr(path, name, value, size, 0, flags);
#else
	return setxattr(path, name, value, size, flags);
#endif
#elif defined(__NR_setxattr) &&	\
      defined(HAVE_SYSCALL)
	return (int)syscall(__NR_setxattr, path, name, value, size, flags);
#else
	return (int)shim_enosys(0, path, name, value, size, flags);
#endif
}

/*
 *  shim_setxattrat
 *	wrapper for setxattrat (Linux 6.13)
 */
int shim_setxattrat(int dfd, const char *path, unsigned int at_flags,
		const char *name, const struct shim_xattr_args *args,
		size_t size)
{
#if defined(HAVE_SETXATTRAT)
	return setxattr(dfd, path, name, at_flags, name, args, size);
#elif defined(__NR_setxattrat) &&	\
      defined(HAVE_SYSCALL)
	return (int)syscall(__NR_setxattrat, dfd, path, at_flags, name, args, size);
#else
	return (int)shim_enosys(0, dfd, path, at_flags, name, args, size);
#endif
}

/*
 *  shim_fsetxattr
 *	wrapper for fsetxattr
 */
int shim_fsetxattr(int fd, const char *name, const void *value, size_t size, int flags)
{
#if defined(HAVE_FSETXATTR)
#if defined(__APPLE__)
	return fsetxattr(fd, name, value, size, 0, flags);
#else
	return fsetxattr(fd, name, value, size, flags);
#endif
#elif defined(__NR_fsetxattr) &&	\
      defined(HAVE_SYSCALL)
	return (int)syscall(__NR_fsetxattr, fd, name, value, size, flags);
#else
	return (int)shim_enosys(0, fd, name, value, size, flags);
#endif
}

/*
 *  shim_lsetxattr
 *	wrapper for lsetxattr
 */
int shim_lsetxattr(const char *path, const char *name, const void *value, size_t size, int flags)
{
#if defined(HAVE_LSETXATTR)
	return lsetxattr(path, name, value, size, flags);
#elif defined(__NR_lsetxattr) &&	\
      defined(HAVE_SYSCALL)
	return (int)syscall(__NR_lsetxattr, path, name, value, size, flags);
#else
	return (int)shim_enosys(0, path, name, value, size, flags);
#endif
}

/*
 *  shim_lgetxattr
 *	wrapper for lgetxattr
 */
ssize_t shim_lgetxattr(const char *path, const char *name, void *value, size_t size)
{
#if defined(HAVE_LGETXATTR)
	return lgetxattr(path, name, value, size);
#elif defined(__NR_lgetxattr) &&	\
      defined(HAVE_SYSCALL)
	return (ssize_t)syscall(__NR_lgetxattr, path, name, value, size);
#else
	return (ssize_t)shim_enosys(0, path, name, value, size);
#endif
}

/*
 *  shim_fgetxattr
 *	wrapper for fgetxattr
 */
ssize_t shim_fgetxattr(int fd, const char *name, void *value, size_t size)
{
#if defined(HAVE_FGETXATTR)
#if defined(__APPLE__)
	return fgetxattr(fd, name, value, size, 0, 0);
#else
	return fgetxattr(fd, name, value, size);
#endif
#elif defined(__NR_fgetxattr) &&	\
      defined(HAVE_SYSCALL)
	return (ssize_t)syscall(__NR_fgetxattr, fd, name, value, size);
#else
	return (ssize_t)shim_enosys(0, fd, name, value, size);
#endif
}

/*
 *  shim_removexattr
 *	wrapper for removexattr
 */
int shim_removexattr(const char *path, const char *name)
{
#if defined(HAVE_REMOVEXATTR)
#if defined(__APPLE__)
	return removexattr(path, name, 0);
#else
	return removexattr(path, name);
#endif
#elif defined(__NR_removexattr) &&	\
      defined(HAVE_SYSCALL)
	return (int)syscall(__NR_removexattr, path, name);
#else
	return (int)shim_enosys(0, path, name);
#endif
}

/*
 *  shim_removexattrat
 *	wrapper for removexattrat
 */
int shim_removexattrat(int dfd, const char *path, unsigned int at_flags, const char *name)
{
#if defined(HAVE_REMOVEXATTRAT)
	return removexattrat(dfd, path, at_flags, name);
#elif defined(__NR_removexattrat) &&	\
      defined(HAVE_SYSCALL)
	return (int)syscall(__NR_removexattrat, dfd, path, at_flags, name);
#else
	return (int)shim_enosys(0, dfd, path, at_flags, name);
#endif
}


/*
 *  shim_lremovexattr
 *	wrapper for lremovexattr
 */
int shim_lremovexattr(const char *path, const char *name)
{
#if defined(HAVE_LREMOVEXATTR)
	return lremovexattr(path, name);
#elif defined(__NR_lremovexattr) &&	\
      defined(HAVE_SYSCALL)
	return (int)syscall(__NR_lremovexattr, path, name);
#else
	return (int)shim_enosys(0, path, name);
#endif
}

/*
 *  shim_fremovexattr
 *	wrapper for fremovexattr
 */
int shim_fremovexattr(int fd, const char *name)
{
#if defined(HAVE_FREMOVEXATTR)
#if defined(__APPLE__)
	return fremovexattr(fd, name, 0);
#else
	return fremovexattr(fd, name);
#endif
#elif defined(__NR_fremovexattr) &&	\
      defined(HAVE_SYSCALL)
	return (int)syscall(__NR_fremovexattr, fd, name);
#else
	return (int)shim_enosys(0, fd, name);
#endif
}

/*
 *  shim_llistxattr
 *	wrapper for fllistxattr
 */
ssize_t shim_llistxattr(const char *path, char *list, size_t size)
{
#if defined(HAVE_LLISTXATTR)
	return llistxattr(path, list, size);
#elif defined(__NR_llistxattr) &&	\
      defined(HAVE_SYSCALL)
	return (ssize_t)syscall(__NR_llistxattr, path, list, size);
#else
	return (ssize_t)shim_enosys(0, path, list, size);
#endif
}

/*
 *  shim_reboot
 *	wrapper for linux reboot system call
 */
int shim_reboot(int magic, int magic2, int cmd, void *arg)
{
#if defined(__linux__) &&	\
    defined(__NR_reboot) &&	\
    defined(HAVE_SYSCALL)
	return (int)syscall(__NR_reboot, magic, magic2, cmd, arg);
#else
	return (int)shim_enosys(0, magic, magic2, cmd, arg);
#endif
}

/*
 *   shim_process_madvise
 *	wrapper for the new linux 5.10 process_madvise system call
 *	ref: commit 28a305ae24da ("mm/madvise: introduce process_madvise()
 *	syscall: an external memory hinting API"
 */
ssize_t shim_process_madvise(
	int pidfd,
	const struct iovec *iovec,
	unsigned long int vlen,
	int advice,
	unsigned int flags)
{
#if defined(__NR_process_madvise) &&	\
    defined(HAVE_SYSCALL)
	return (ssize_t)syscall(__NR_process_madvise, pidfd,
		iovec, vlen, advice, flags);
#else
	return (ssize_t)shim_enosys(0, pidfd,
		iovec, vlen, advice, flags);
#endif
}

/*
 *   shim_clock_getres
 *	wrapper for linux clock_getres system call,
 *	prefer to use the system call to avoid and
 *	glibc avoidance of the system call
 */
int shim_clock_getres(clockid_t clk_id, struct timespec *res)
{
#if defined(CLOCK_THREAD_CPUTIME_ID) && \
    defined(HAVE_CLOCK_GETRES)
#if defined(__NR_clock_getres) &&	\
    defined(HAVE_SYSCALL)
	return (int)syscall(__NR_clock_getres, clk_id, res);
#else
	return clock_getres(clk_id, res);
#endif
#else
	return (int)shim_enosys(0, clk_id, res);
#endif
}

/*
 *   shim_clock_adjtime
 *	wrapper for linux clock_adjtime system call
 */
int shim_clock_adjtime(clockid_t clk_id, shim_timex_t *buf)
{
#if defined(CLOCK_THREAD_CPUTIME_ID) && \
    defined(HAVE_CLOCK_ADJTIME) &&	\
    defined(HAVE_SYS_TIMEX_H) &&	\
    defined(HAVE_TIMEX)
	return clock_adjtime(clk_id, buf);
#else
	return (int)shim_enosys(0, clk_id, buf);
#endif
}

/*
 *   shim_clock_gettime
 *	wrapper for linux clock_gettime system call,
 *	prefer to use the system call to avoid and
 *	glibc avoidance of the system call
 */
int shim_clock_gettime(clockid_t clk_id, struct timespec *tp)
{
#if defined(CLOCK_THREAD_CPUTIME_ID) && \
    defined(HAVE_CLOCK_GETTIME)
#if defined(__NR_clock_gettime) &&	\
    defined(HAVE_SYSCALL)
	return (int)syscall(__NR_clock_gettime, clk_id, tp);
#else
	return clock_gettime(clk_id, tp);
#endif
#else
	return (int)shim_enosys(0, clk_id, tp);
#endif
}

/*
 *   shim_clock_settime
 *	wrapper for linux clock_settime system call,
 *	prefer to use the system call to avoid and
 *	glibc avoidance of the system call
 */
int shim_clock_settime(clockid_t clk_id, struct timespec *tp)
{
#if defined(CLOCK_THREAD_CPUTIME_ID) && \
    defined(HAVE_CLOCK_SETTIME)
#if defined(__NR_clock_settime) &&	\
    defined(HAVE_SYSCALL)
	return (int)syscall(__NR_clock_settime, clk_id, tp);
#else
	return clock_settime(clk_id, tp);
#endif
#else
	return (int)shim_enosys(0, clk_id, tp);
#endif
}

static int shim_nice_autogroup(int niceness)
{
#if defined(__linux__)
	if ((g_opt_flags & OPT_FLAGS_AUTOGROUP) && (errno == 0)) {
		int fd, saved_err = errno;
		int retries = 0;

		fd = open("/proc/self/autogroup", O_WRONLY);
		if (fd != -1) {
			char buf[32];
			ssize_t ret;

			(void)snprintf(buf, sizeof(buf), "%d\n", niceness);
retry:
			ret = write(fd, buf, strlen(buf));
			if (ret < 0)
				if (errno == EAGAIN) {
					stress_random_small_sleep();
					lseek(fd, 0, SEEK_SET);
					retries++;
					if ((retries < 30) && stress_continue_flag())
						goto retry;
				}
			(void)close(fd);
		}
		errno = saved_err;
	}
#endif
	return niceness;
}

/*
 *  shim_nice
 *	wrapper for nice.  Some C libraries may use setpriority
 *	and hence the nice system call is not being used. Directly
 *	call the nice system call if it's available, else use
 *	setpriority or fall back onto the libc version.
 *
 *	Some operating systems like Hiaku don't even support nice,
 *	so handle these cases too.
 */
int shim_nice(int inc)
{
#if defined(__NR_nice) &&	\
    defined(HAVE_SYSCALL)
	int ret;

	errno = 0;
	ret = (int)syscall(__NR_nice, inc);
	if ((ret < 0) && (errno == ENOSYS)) {
		errno = 0;
		return shim_nice_autogroup(nice(inc));
	}
	return shim_nice_autogroup(ret);
#elif defined(HAVE_GETPRIORITY) &&	\
      defined(HAVE_SETPRIORITY) &&	\
      defined(PRIO_PROCESS)
	int prio, ret, saved_err;

	prio = getpriority(PRIO_PROCESS, 0);
	if (prio == -1) {
		if (errno != 0) {
#if defined(HAVE_NICE)
			/* fall back to nice */
			errno = 0;
			return shim_nice_autogroup(nice(inc));
#else
			/* ok, give up */
			errno = EPERM;
			return -1;
#endif
		}
	}
	ret = setpriority(PRIO_PROCESS, 0, prio + inc);
	if (ret == -1) {
		if (errno == EACCES)
			errno = EPERM;
		return -1;
	}
	saved_err = errno;
	ret = shim_nice_autogroup(getpriority(PRIO_PROCESS, 0));
	errno = saved_err;
	return ret;
#elif defined(HAVE_NICE)
	errno = 0;
	return shim_nice_autogroup(nice(inc));
#else
	(void)inc;

	UNEXPECTED
	errno = -ENOSYS;
	return -1;
#endif
}

/*
 *  shim_time
 *	wrapper for time system call to
 *	avoid libc calling gettimeofday via
 *	the VDSO and force the time system call
 *	for linux, otherwise use time()
 */
time_t shim_time(time_t *tloc)
{
#if defined(__NR_time) &&	\
    defined(HAVE_SYSCALL) &&	\
    defined(__linux__)
	return (time_t)syscall(__NR_time, tloc);
#elif defined(HAVE_TIME)
	return time(tloc);
#else
	return (time_t)shim_enosys(0, tloc);
#endif
}

/*
 *  shim_gettimeofday
 *	wrapper for gettimeofday system call to
 *	avoid libc calling gettimeofday via
 *	the VDSO and force the time system call
 */
int shim_gettimeofday(struct timeval *tv, shim_timezone_t *tz)
{
#if defined(__NR_gettimeofday) &&	\
    defined(HAVE_SYSCALL) &&		\
    defined(__linux__)
	return (int)syscall(__NR_gettimeofday, tv, tz);
#elif defined(HAVE_GETTIMEOFDAY)
	return gettimeofday(tv, tz);
#else
	return (int)shim_enosys(0, tv, tz);
#endif
}

/*
 *  shim_close_range()
 *	wrapper for close_range - close a range of
 *	file descriptors
 */
int shim_close_range(unsigned int fd, unsigned int max_fd, unsigned int flags)
{
#if defined(HAVE_CLOSE_RANGE)
	return close_range(fd, max_fd, flags);
#elif defined(__NR_close_range) &&	\
      defined(HAVE_SYSCALL)
	return (int)syscall(__NR_close_range, fd, max_fd, flags);
#else
	return (int)shim_enosys(0, fd, max_fd, flags);
#endif
}

/*
 *  shim_lookup_dcookie()
 *	wrapper for linux lookup_dcookie
 */
int shim_lookup_dcookie(uint64_t cookie, char *buffer, size_t len)
{
#if defined(__NR_lookup_dcookie) &&	\
    defined(HAVE_SYSCALL)
	return (int)syscall(__NR_lookup_dcookie, cookie, buffer, len);
#else
	return (int)shim_enosys(0, cookie, buffer, len);
#endif
}

/*
 *  shim_readlink()
 *	wrapper for readlink because some libc wrappers call
 *	readlinkat
 */
ssize_t shim_readlink(const char *pathname, char *buf, size_t bufsiz)
{
#if defined(__NR_readlink) &&	\
    defined(HAVE_SYSCALL)
	return (ssize_t)syscall(__NR_readlink, pathname, buf, bufsiz);
#else
	return readlink(pathname, buf, bufsiz);
#endif
}

/*
 *  shim_sgetmask
 *	wrapper for obsolete linux system call sgetmask()
 */
long int shim_sgetmask(void)
{
#if defined(__NR_sgetmask) &&	\
    defined(HAVE_SYSCALL)
	return (long int)syscall(__NR_sgetmask);
#else
	return (long int)shim_enosys(0);
#endif
}

/*
 *  shim_ssetmask
 *	wrapper for obsolete linux system call ssetmask()
 */
long int shim_ssetmask(long int newmask)
{
#if defined(__NR_ssetmask) &&	\
    defined(HAVE_SYSCALL)
	return (long int)syscall(__NR_ssetmask, newmask);
#else
	return (long int)shim_enosys(0, newmask);
#endif
}

/*
 *  shim_stime()
 *	wrapper for obsolete SVr4 stime system call
 */
int shim_stime(const time_t *t)
{
#if defined(HAVE_STIME) &&	\
    defined(__minix__)
	/*
	 *  Minix does not provide the prototype for stime
	 *  and does not use a const time_t * argument
	 */
	extern int stime(time_t *t);
	time_t *ut = (time_t *)shim_unconstify_ptr(t);

	return stime(ut);
#elif defined(HAVE_STIME) &&	\
      !NEED_GLIBC(2, 31, 0)
	return stime(t);
#elif defined(__NR_stime) &&	\
      defined(HAVE_SYSCALL)
	return (int)syscall(__NR_stime, t);
#else
	return (int)shim_enosys(0, t);
#endif
}

/*
 *  shim_vhangup()
 *	wrapper for linux vhangup system call
 */
int shim_vhangup(void)
{
#if defined(HAVE_VHANGUP)
	return vhangup();
#elif defined(__NR_vhangup) &&	\
      defined(HAVE_SYSCALL)
	return (int)syscall(__NR_vhangup);
#else
	return (int)shim_enosys(0);
#endif
}

/*
 *  shim_arch_prctl()
 *	wrapper for arch specific prctl system call
 */
int shim_arch_prctl(int code, unsigned long int addr)
{
#if defined(__NR_arch_prctl) && 	\
      defined(__linux__) &&		\
      defined(HAVE_SYSCALL)
        return (int)syscall(__NR_arch_prctl, code, addr);
#elif defined(HAVE_ARCH_PRCTL) &&	\
       defined(__linux__)
        extern int arch_prctl();

        return arch_prctl(code, addr);
#else
        return (int)shim_enosys(0, code, addr);
#endif
}

/*
 *  shim_tgkill()
 *	wrapper for linux thread kill tgkill system call
 */
int shim_tgkill(int tgid, int tid, int sig)
{
#if defined(HAVE_TGKILL_LIBC)
	return tgkill(tgid, tid, sig);
#elif defined(__NR_tgkill) &&	\
      defined(HAVE_SYSCALL)
	return (int)syscall(__NR_tgkill, tgid, tid, sig);
#else
	return (int)shim_enosys(0, tgid, tid, sig);
#endif
}

/*
 *  shim_tkill()
 *	wrapper for deprecated thread kill tkill system
 *	call. No libc wrapper, so just try system call,
 *      then emulate via tgkill.
 */
int shim_tkill(int tid, int sig)
{
#if defined(__NR_tkill) &&	\
    defined(HAVE_SYSCALL)
	return (int)syscall(__NR_tkill, tid, sig);
#else
	return shim_tgkill(0, tid, sig);
#endif
}

/*
 *  shim_memfd_secret()
 *	wrapper for the new memfd_secret system call
 */
int shim_memfd_secret(unsigned long int flags)
{
#if defined(__NR_memfd_secret) &&	\
    defined(HAVE_SYSCALL)
	return (int)syscall(__NR_memfd_secret, flags);
#else
	return (int)shim_enosys(0, flags);
#endif
}

/*
 *   shim_getrusage()
 *	wrapper for getrusage
 */
int shim_getrusage(int who, struct rusage *usage)
{
#if defined(HAVE_GETRUSAGE)
	return getrusage((shim_rusage_who_t)who, usage);
#elif defined(__NR_getrusage) &&	\
      defined(HAVE_SYSCALL)
	return (int)syscall(__NR_getrusage, who, usage);
#else
	return (int)shim_enosys(0, who, usage);
#endif
}

/*
 *  shim_quotactl_fd()
 *  	wrapper for Linux 5.13 quotactl_fd
 */
int shim_quotactl_fd(unsigned int fd, unsigned int cmd, int id, void *addr)
{
#if defined(HAVE_QUOTACTL_FD)
	return quotactl_fd(fd, cmd, id, addr);
#elif defined(__NR_quotactl_fd) &&	\
      defined(HAVE_SYSCALL)
	return (int)syscall(__NR_quotactl_fd, fd, cmd, id, addr);
#else
	return (int)shim_enosys(0, fd, cmd, id, addr);
#endif
}

/*
 *  shim_modify_ldt()
 *	system call wrapper for modify_ldt()
 */
int shim_modify_ldt(int func, void *ptr, unsigned long int bytecount)
{
#if defined(HAVE_MODIFY_LDT) &&	\
    defined(__NR_modify_ldt) &&	\
    defined(HAVE_SYSCALL)
	return (int)syscall(__NR_modify_ldt, func, ptr, bytecount);
#else
	return (int)shim_enosys(0, func, ptr, bytecount);
#endif
}

/*
 *  shim_process_mrelease()
 *	system call wrapper for Linux 5.14 process_mrelease
 *	- note sparc64 SEGVs on the syscall, so commenting
 *	  this out for some kernels for SPARC
 */
int shim_process_mrelease(int pidfd, unsigned int flags)
{
#if defined(__NR_process_mrelease) &&	\
    defined(HAVE_SYSCALL) &&		\
    !defined(STRESS_ARCH_SPARC)
	return (int)syscall(__NR_process_mrelease, pidfd, flags);
#else
	return (int)shim_enosys(0, pidfd, flags);
#endif
}

/*
 *  shim_futex_waitv()
 *      system call wrapper for Linux 5.16 futex_waitv
 */
int shim_futex_waitv(
	struct shim_futex_waitv *waiters,
	unsigned int nr_futexes,
	unsigned int flags,
	struct timespec *timeout,
	clockid_t clockid)
{
#if defined(__NR_futex_waitv) &&	\
    defined(HAVE_SYSCALL)
	return (int)syscall(__NR_futex_waitv, waiters, nr_futexes, flags, timeout, clockid);
#else
	return (int)shim_enosys(0, waiters, nr_futexes, flags, timeout, clockid);
#endif
}

/*
 *  shim_force_unlink(const char *pathname)
 *	unlink always
 */
int shim_force_unlink(const char *pathname)
{
	int ret;

	ret = unlink(pathname);
	if (ret < 0) {
		stress_unset_chattr_flags(pathname);
		ret = unlink(pathname);
	}
	return ret;
}

/*
 *  shim_unlink()
 *	unlink, skip operation if --keep-files is enabled
 */
int shim_unlink(const char *pathname)
{
	if (g_opt_flags & OPT_FLAGS_KEEP_FILES)
		return 0;
	return shim_force_unlink(pathname);
}

/*
 *  shim_unlinkat()
 *	unlinkat, skip operation if --keep-files is enabled
 */
int shim_unlinkat(int dir_fd, const char *pathname, int flags)
{
	if (g_opt_flags & OPT_FLAGS_KEEP_FILES)
		return 0;
#if defined(HAVE_UNLINKAT)
	return unlinkat(dir_fd, pathname, flags);
#elif defined(__NR_unlinkat) &&	\
      defined(HAVE_SYSCALL)
	return (int)syscall(__NR_unlinkat, dir_fd, pathname, flags);
#else
	return (int)shim_enosys(0, dir_fd, pathname, flags);
#endif
}

/*
 *  shim_force_rmdir()
 *	rmdir always
 */
int shim_force_rmdir(const char *pathname)
{
	int ret;

	ret = rmdir(pathname);
	if (ret < 0) {
		stress_unset_chattr_flags(pathname);
		ret = rmdir(pathname);
	}
	return ret;
}

/*
 *  shim_rmdir()
 *	rmdir, skip operation if --keep-files is enabled
 */
int shim_rmdir(const char *pathname)
{
	if (g_opt_flags & OPT_FLAGS_KEEP_FILES)
		return 0;
	return shim_force_rmdir(pathname);
}

/*
 *  shim_getdomainname()
 *	shim required for sun due to non-extern
 */
int shim_getdomainname(char *name, size_t len)
{
#if defined(HAVE_GETDOMAINNAME)
	return getdomainname(name, len);
#elif defined(__NR_getdomainname) &&	\
      defined(HAVE_SYSCALL)
	return (int)syscall(__NR_getdomainname, name, len);
#else
	return (int)shim_enosys(0, name, len);
#endif
}

/*
 *  shim_setdomainname()
 *	shim required for sun due to non-extern
 */
int shim_setdomainname(const char *name, size_t len)
{
#if defined(HAVE_SETDOMAINNAME)
	return setdomainname(name, len);
#elif defined(__NR_setdomainname) &&	\
      defined(HAVE_SYSCALL)
	return (int)syscall(__NR_setdomainname, name, len);
#else
	return (int)shim_enosys(0, name, len);
#endif
}

/*
 *  shim_setgroups()
 *	shim for POSIX version of setgroups
 */
int shim_setgroups(int size, const gid_t *list)
{
#if defined(HAVE_GRP_H)
#if defined(__linux__) &&	\
    defined(HAVE_SETGROUPS)
	/* Linux passes size as size_t */
	return setgroups((size_t)size, list);
#elif defined(HAVE_SETGROUPS)
	/* POSIX variant, as used by BSD etc */
	return setgroups(size, list);
#else
	return (int)shim_enosys(size, list);
#endif
#else
	return (int)shim_enosys(size, list);
#endif
}

/*
 *  shim_finit_module()
 *      shim for linux finit_module
 */
int shim_finit_module(int fd, const char *uargs, int flags)
{
#if defined(HAVE_FINIT_MODULE)
	extern finit_module(int fd, const char *param_values, int flags);

	return finit_module(fd, uargs, flags);
#elif defined(__NR_finit_module)
	return syscall(__NR_finit_module, fd, uargs, flags);
#else
	return shim_enosys(0, fd, uargs, flags);
#endif
}

/*
 *  shim_delete_module()
 *      shim for linux delete_module
 */
int shim_delete_module(const char *name, unsigned int flags)
{
#if defined(HAVE_DELETE_MODULE)
	extern int delete_module(const char *name, int flags);

	return delete_module(name, flags);
#elif defined(__NR_delete_module)
	return syscall(__NR_delete_module, name, flags);
#else
	return shim_enosys(0, name, flags);
#endif
}

/*
 *  shim_raise
 *	shim for raise(), workaround SH4 raise not working
 *	with glibc 2.34-7
 */
int shim_raise(int sig)
{
#if defined(STRESS_ARCH_SH4)
	return shim_kill(getpid(), sig);
#else
	return raise(sig);
#endif
}

/*
 *  shim_kill
 *	shim for kill() and ignore kill on pid 0, 1
 *	with glibc 2.34-7
 */
int shim_kill(pid_t pid, int sig)
{
	if (sig == 0)
		return kill(pid, sig);
	if (UNLIKELY(pid == 1)) {
		errno = EPERM;
		return -1;
	}
	if (UNLIKELY((pid == -1) && (sig == SIGKILL))) {
		errno = EINVAL;
		return -1;
	}
	if (geteuid() != 0)
		return kill(pid, sig);
	if (UNLIKELY(pid <= 0)) {
		errno = EPERM;
		return -1;
	}
	return kill(pid, sig);
}

/*
 *  shim_set_mempolicy_home_node()
 *	wrapper for NUMA system call set_mempolicy_home_node()
 */
int shim_set_mempolicy_home_node(
        unsigned long int start,
        unsigned long int len,
        unsigned long int home_node,
        unsigned long int flags)
{
#if defined(__NR_set_mempolicy_home_node)
        return (int)syscall(__NR_set_mempolicy_home_node, start, len, home_node, flags);
#else
	return shim_enosys(0, start, len, home_node, flags);
#endif
}

/*
 *  shim_fchmodat
 *	shim for Linux fchmodat
 */
int shim_fchmodat(int dfd, const char *filename, mode_t mode, unsigned int flags)
{
#if defined(HAVE_FCHMODAT)
	return fchmodat(dfd, filename, mode, flags);
#elif defined(__NR_fchmodat)
	/* No flags field in system call */
	(void)flags;
	return (int)syscall(__NR_fchmodat, dfd, filename, mode);
#else
	return shim_enosys(0, dfd, filename, mode, flags);
#endif
}

/*
 *  shim_fchmodat2
 *	shim for Linux 6.6 fchmodat2
 */
int shim_fchmodat2(int dfd, const char *filename, mode_t mode, unsigned int flags)
{
#if defined(HAVE_FCHMODAT2)
	return fchmodat2(dfd, filename, mode, flags);
#elif defined(__NR_fchmodat2)
	return (int)syscall(__NR_fchmodat2, dfd, filename, mode, flags);
#else
	return shim_enosys(0, dfd, filename, mode, flags);
#endif
}

/*
 *  shim_fstat()
 *	shim for fstat
 */
int shim_fstat(int fd, struct stat *statbuf)
{
#if defined(HAVE_FSTAT)
	return fstat(fd, statbuf);
#else
	return shim_enosys(0, fd, statbuf);
#endif
}

/*
 *  shim_lstat()
 *	shim for lstat
 */
int shim_lstat(const char *pathname, struct stat *statbuf)
{
#if defined(HAVE_LSTAT)
	return lstat(pathname, statbuf);
#else
	return shim_enosys(0, pathname, statbuf);
#endif
}

/*
 *  shim_stat()
 *	shim for stat
 */
int shim_stat(const char *pathname, struct stat *statbuf)
{
#if defined(HAVE_STAT)
	return stat(pathname, statbuf);
#else
	return shim_enosys(0, pathname, statbuf);
#endif
}

/*
 *  shim_dirent_type()
 *	shim / emulation of d->d_type
 */
unsigned char shim_dirent_type(const char *path, const struct dirent *d)
{
	char filename[PATH_MAX];
	struct stat statbuf;

#if defined(HAVE_DIRENT_D_TYPE)
	/* Some file systems can determine d->d_type */
	if (d->d_type != SHIM_DT_UNKNOWN)
		return d->d_type;
#endif
	/* Systems without d->d_type or have d->d_type as unknown */
	(void)snprintf(filename, sizeof(filename), "%s/%s", path, d->d_name);
	if (lstat(filename, &statbuf) == 0) {
		switch (statbuf.st_mode & S_IFMT) {
#if defined(S_IFBLK)
		case S_IFBLK:
			return SHIM_DT_BLK;
#endif
#if defined(S_IFCHR)
		case S_IFCHR:
			return SHIM_DT_CHR;
#endif
#if defined(S_IFDIR)
		case S_IFDIR:
			return SHIM_DT_DIR;
#endif
#if defined(S_IFIFO)
		case S_IFIFO:
			return SHIM_DT_FIFO;
#endif
#if defined(S_IFLNK)
		case S_IFLNK:
			return SHIM_DT_LNK;
#endif
#if defined(S_IFREG)
		case S_IFREG:
			return SHIM_DT_REG;
#endif
#if defined(S_IFSOCK)
		case S_IFSOCK:
			return SHIM_DT_SOCK;
#endif
		default:
			break;
		}
	}
	return SHIM_DT_UNKNOWN;
}

/*
 *  shim_mseal()
 *	shim wrapper for the Linux 5.10 mseal system call
 */
int shim_mseal(void *addr, size_t len, unsigned long int flags)
{
#if defined(HAVE_MSEAL)
	return mseal(addr, len, flags);
#elif defined(__NR_mseal)
	return (int)syscall(__NR_mseal, addr, len, flags);
#else
	return shim_enosys(0, addr, len, flags);
#endif
}

/*
 *  shim_ppoll()
 *	shim wrapper for ppoll
 */
int shim_ppoll(
	shim_pollfd_t *fds,
	shim_nfds_t nfds,
	const struct timespec *tmo_p,
	const sigset_t *sigmask)
{
#if defined(HAVE_PPOLL)
	return ppoll(fds, nfds, tmo_p, sigmask);
#elif defined(__NR_ppoll) && defined(_NSIG)
	struct timespec tval;

	/*
	 * The kernel will update the timeout, so use a tmp val
	 * instead
	 */
	if (tmo_p != NULL) {
		tval = *tmo_p;
		tmo_p = &tval;
	}
	return (int)syscall(__NR_ppoll, fds, nfds, tmo_p, sigmask, _NSIG / 8);
#else
	return shim_enosys(0, fds, nfds, tmo_p, sigmask);
#endif
}

/*
 * shim_file_getattr()
 *	shim wrapper for Linux 6.16 file_getattr
 */
int shim_file_getattr(
	int dfd,
	const char *filename,
	struct shim_file_attr *ufattr,
	size_t usize,
	unsigned int at_flags)
{
#if defined(__NR_file_getattr)
	return (int)syscall(__NR_file_getattr, dfd, filename, ufattr, usize, at_flags);
#else
	return shim_enosys(0, dfd, filename, ufattr, usize, at_flags);
#endif
}

/*
 * shim_file_setattr()
 *	shim wrapper for Linux 6.16 file_setattr
 */
int shim_file_setattr(
	int dfd,
	const char *filename,
	struct shim_file_attr *ufattr,
	size_t usize,
	unsigned int at_flags)
{
#if defined(__NR_file_getattr)
	return (int)syscall(__NR_file_setattr, dfd, filename, ufattr, usize, at_flags);
#else
	return shim_enosys(0, dfd, filename, ufattr, usize, at_flags);
#endif
}

/*
 * shim_pause()
 *	shim wrapper for pause()
 */
int shim_pause(void)
{
#if defined(HAVE_PAUSE)
	return pause();
#elif defined(HAVE_SYS_SELECT_H) &&	\
      defined(HAVE_SELECT)
	while (select(0, NULL, NULL, NULL, NULL) == 0)
		;
	errno = EINTR;
	return -1;
#else
	while (sleep(~(unsigned int)0) == 0)
		;
	errno = EINTR;
	return -1;
#endif
}
