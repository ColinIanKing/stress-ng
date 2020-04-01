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

typedef void *(*stress_bad_addr_t)(const stress_args_t *args);
typedef int (*stress_bad_syscall_t)(void *addr);

static void *ro_page;
static void *rw_page;

static const stress_help_t help[] = {
	{ NULL,	"sysbadaddr N",	    "start N workers that pass bad addresses to syscalls" },
	{ NULL,	"sysbadaddr-ops N", "stop after N sysbadaddr bogo syscalls" },
	{ NULL,	NULL,		    NULL }
};

static const int sigs[] = {
#if defined(SIGILL)
	SIGILL,
#endif
#if defined(SIGTRAP)
	SIGTRAP,
#endif
#if defined(SIGFPE)
	SIGFPE,
#endif
#if defined(SIGBUS)
	SIGBUS,
#endif
#if defined(SIGSEGV)
	SIGSEGV,
#endif
#if defined(SIGIOT)
	SIGIOT,
#endif
#if defined(SIGEMT)
	SIGEMT,
#endif
#if defined(SIGALRM)
	SIGALRM,
#endif
#if defined(SIGINT)
	SIGINT,
#endif
#if defined(SIGHUP)
	SIGHUP
#endif
};

/*
 *  limit_procs()
 *	try to limit child resources
 */
static void limit_procs(const int procs)
{
#if defined(RLIMIT_CPU) || defined(RLIMIT_NPROC)
	struct rlimit lim;
#endif

#if defined(RLIMIT_CPU)
	lim.rlim_cur = 1;
	lim.rlim_max = 1;
	(void)setrlimit(RLIMIT_CPU, &lim);
#endif
#if defined(RLIMIT_NPROC)
	lim.rlim_cur = procs;
	lim.rlim_max = procs;
	(void)setrlimit(RLIMIT_NPROC, &lim);
#else
	(void)procs;
#endif
}

static void MLOCKED_TEXT stress_badhandler(int signum)
{
	(void)signum;

	_exit(1);
}

static inline void *inc_addr(void *ptr, const size_t inc)
{
	return (void *)((char *)ptr + inc);
}

static void *unaligned_addr(const stress_args_t *args)
{
	static uint64_t data[8] = { -1, -1, -1, -1, -1, -1, -1, -1 };
	uint8_t *ptr = (uint8_t *)data;

	(void)args;

	return ptr + 1;
}

static void *readonly_addr(const stress_args_t *args)
{
	(void)args;

	return ro_page;
}

static void *null_addr(const stress_args_t *args)
{
	(void)args;

	return NULL;
}

static void *text_addr(const stress_args_t *args)
{
	(void)args;

	return (void *)&write;
}

static void *bad_end_addr(const stress_args_t *args)
{
	return ((uint8_t *)rw_page) + args->page_size - 1;
}

static void *bad_max_addr(const stress_args_t *args)
{
	(void)args;

	return (void *)~(uintptr_t)0;
}

static void *unmapped_addr(const stress_args_t *args)
{
	return ((uint8_t *)rw_page) + args->page_size;
}

static stress_bad_addr_t bad_addrs[] = {
	unaligned_addr,
	readonly_addr,
	null_addr,
	text_addr,
	bad_end_addr,
	bad_max_addr,
	unmapped_addr,
};

static int bad_access(void *addr)
{
	return access((char *)addr, R_OK);
}

/*
static int bad_acct(void *addr)
{
	return acct((char *)addr);
}
*/

static int bad_bind(void *addr)
{
	return bind(0, (struct sockaddr *)addr, 0);
}

static int bad_chdir(void *addr)
{
	return chdir((char *)addr);
}

static int bad_chmod(void *addr)
{
	return chmod((char *)addr, 0);
}

static int bad_chown(void *addr)
{
	return chown((char *)addr, getuid(), getgid());
}

#if defined(HAVE_CHROOT)
static int bad_chroot(void *addr)
{
	return chroot((char *)addr);
}
#endif

#if defined(HAVE_CLOCK_GETRES) &&	\
    defined(CLOCK_REALTIME)
static int bad_clock_getres(void *addr)
{
	return clock_getres(CLOCK_REALTIME, (struct timespec *)addr);
}
#endif

#if defined(HAVE_CLOCK_GETTIME) &&	\
    defined(CLOCK_REALTIME)
static int bad_clock_gettime(void *addr)
{
	return clock_gettime(CLOCK_REALTIME, (struct timespec *)addr);
}
#endif

#if defined(HAVE_CLOCK_NANOSLEEP) &&	\
    defined(CLOCK_REALTIME)
static int bad_clock_nanosleep(void *addr)
{
	return clock_nanosleep(CLOCK_REALTIME, 0,
		(const struct timespec *)addr,
		(struct timespec *)addr);
}
#endif

#if defined(CLOCK_THREAD_CPUTIME_ID) &&	\
    defined(HAVE_CLOCK_SETTIME)
static int bad_clock_settime(void *addr)
{
	return clock_settime(CLOCK_THREAD_CPUTIME_ID, (const struct timespec *)addr);
}
#endif

#if defined(HAVE_CLONE) && 	\
    defined(__linux__)
static int bad_clone(void *addr)
{
	typedef int (*fn)(void *);

	return clone((fn)addr, (void *)addr, 0, (void *)addr,
		(pid_t *)inc_addr(addr, 1),
		(void *)inc_addr(addr, 2),
		(pid_t *)inc_addr(addr, 3));
}
#endif

static int bad_connect(void *addr)
{
	return connect(0, (const struct sockaddr *)addr, sizeof(struct sockaddr));
}

/*
static int bad_creat(void *addr)
{
	return creat((char *)addr, 0);
}
*/

static int bad_execve(void *addr)
{
	return execve((char *)addr, (char **)inc_addr(addr, 1),
		(char **)inc_addr(addr, 2));
}

#if defined(HAVE_FACCESSAT)
static int bad_faccessat(void *addr)
{
	return faccessat(AT_FDCWD, (char *)addr, R_OK, 0);
}
#endif

static int bad_fstat(void *addr)
{
	return fstat(0, (struct stat *)addr);
}

static int bad_getcpu(void *addr)
{
	return shim_getcpu((unsigned *)addr, (unsigned *)inc_addr(addr, 2),
		(void *)inc_addr(addr, 2));
}

static int bad_getcwd(void *addr)
{
	if (getcwd((char *)addr, 1024) == NULL)
		return -1;

	return 0;
}

#if defined(HAVE_GETDOMAINNAME)
static int bad_getdomainname(void *addr)
{
	return getdomainname((char *)addr, 8192);
}
#endif

static int bad_getgroups(void *addr)
{
	return getgroups(8192, (gid_t *)addr);
}

#if defined(HAVE_GETHOSTNAME)
static int bad_gethostname(void *addr)
{
	return gethostname((char *)addr, 8192);
}
#endif

static int bad_getitimer(void *addr)
{
	return getitimer(ITIMER_PROF, (struct itimerval *)addr);
}

static int bad_getpeername(void *addr)
{
	return getpeername(0, (struct sockaddr *)addr, (socklen_t *)inc_addr(addr, 1));
}

static int bad_get_mempolicy(void *addr)
{
	return shim_get_mempolicy((int *)addr,
		(unsigned long *)inc_addr(addr, 1), 1,
		(unsigned long)inc_addr(addr, 2), 0UL);
}

static int bad_getrandom(void *addr)
{
	return shim_getrandom((void *)addr, 1024, 0);
}

#if defined(HAVE_GETRESGID)
static int bad_getresgid(void *addr)
{
	return getresgid((gid_t *)addr, (gid_t *)inc_addr(addr, 1),
		(gid_t *)inc_addr(addr, 2));
}
#endif

#if defined(HAVE_GETRESUID)
static int bad_getresuid(void *addr)
{
	return getresuid((uid_t *)addr, (uid_t *)inc_addr(addr, 1),
		(uid_t *)inc_addr(addr, 2));
}
#endif

static int bad_getrlimit(void *addr)
{
	return getrlimit(RLIMIT_CPU, (struct rlimit *)addr);
}

static int bad_getrusage(void *addr)
{
	return getrusage(RUSAGE_SELF, (struct rusage *)addr);
}

static int bad_getsockname(void *addr)
{
	return getsockname(0, (struct sockaddr *)addr, (socklen_t *)inc_addr(addr, 1));
}

static int bad_gettimeofday(void *addr)
{
	struct timezone *tz = (struct timezone *)inc_addr(addr, 1);
	return gettimeofday((struct timeval *)addr, tz);
}

#if defined(HAVE_GETXATTR) &&	\
    (defined(HAVE_SYS_XATTR_H) || defined(HAVE_ATTR_XATTR_H))
static int bad_getxattr(void *addr)
{
	return shim_getxattr((char *)addr, (char *)inc_addr(addr, 1),
		(void *)inc_addr(addr, 2), (size_t)32);
}
#endif

#if defined(TCGETS)
static int bad_ioctl(void *addr)
{
	return ioctl(0, TCGETS, addr);
}
#endif

static int bad_lchown(void *addr)
{
	return lchown((char *)addr, getuid(), getgid());
}

static int bad_link(void *addr)
{
	return link((char *)addr, (char *)inc_addr(addr, 1));
}

static int bad_lstat(void *addr)
{
	return lstat((const char *)addr, (struct stat *)inc_addr(addr, 1));
}

#if defined(HAVE_MADVISE)
static int bad_madvise(void *addr)
{
	return madvise((void *)addr, 8192, MADV_NORMAL);
}
#endif

static int bad_migrate_pages(void *addr)
{
	return shim_migrate_pages(getpid(), 1, (unsigned long *)addr,
		(unsigned long *)inc_addr(addr, 1));
}

static int bad_mincore(void *addr)
{
	return shim_mincore((void *)ro_page, 1, (unsigned char *)addr);
}

#if defined(HAVE_MLOCK)
static int bad_mlock(void *addr)
{
	return shim_mlock((void *)addr, 4096);
}
#endif

#if defined(HAVE_MLOCK2)
static int bad_mlock2(void *addr)
{
	return shim_mlock2((void *)addr, 4096, 0);
}
#endif

#if defined(__NR_move_pages)
static int bad_move_pages(void *addr)
{
	return shim_move_pages(getpid(), (unsigned long)1, (void **)addr,
		(const int *)inc_addr(addr, 1), (int *)inc_addr(addr, 2), 0);
}
#endif

#if defined(HAVE_MLOCK)
static int bad_munlock(void *addr)
{
	return shim_munlock((void *)addr, 4096);
}

#endif
/*
#if defined(HAVE_MPROTECT)
static int bad_mprotect(void *addr)
{
	return mprotect((void *)addr, 4096, PROT_READ | PROT_WRITE);
}
#endif
*/

#if defined(HAVE_MSYNC)
static int bad_msync(void *addr)
{
	return shim_msync((void *)addr, 4096, MS_SYNC);
}
#endif

#if defined(HAVE_NANOSLEEP)
static int bad_nanosleep(void *addr)
{
	return nanosleep((struct timespec *)addr,
		(struct timespec *)inc_addr(addr, 1));
}
#endif

static int bad_open(void *addr)
{
	int fd;

	fd = open((char *)addr, O_RDONLY);
	if (fd != -1)
		(void)close(fd);

	return fd;
}

static int bad_pipe(void *addr)
{
	return pipe((int *)addr);
}

static int bad_pread(void *addr)
{
	int fd, ret = 0;

	fd = open("/dev/zero", O_RDONLY);
	if (fd > -1) {
		ret = pread(fd, addr, 1024, 0);
		(void)close(fd);
	}
	return ret;
}

#if defined(HAVE_PTRACE) && 	\
    defined(PTRACE_GETREGS)
static int bad_ptrace(void *addr)
{
	return ptrace(PTRACE_GETREGS, getpid(), (void *)addr,
		(void *)inc_addr(addr, 1));
}
#endif

#if defined(HAVE_POLL_H)
static int bad_poll(void *addr)
{
	return poll((struct pollfd *)addr, (nfds_t)16, (int)1);
}
#endif

static int bad_pwrite(void *addr)
{
	int fd, ret = 0;

	fd = open("/dev/null", O_WRONLY);
	if (fd > -1) {
		ret = pwrite(fd, (void *)addr, 1024, 0);
		(void)close(fd);
	}
	return ret;
}

static int bad_read(void *addr)
{
	int fd, ret = 0;

	fd = open("/dev/zero", O_RDONLY);
	if (fd > -1) {
		ret = read(fd, (void *)addr, 1024);
		(void)close(fd);
	}
	return ret;
}

static int bad_readlink(void *addr)
{
	return readlink((const char *)addr, (char *)inc_addr(addr, 1), 8192);
}

static int bad_readv(void *addr)
{
	int fd, ret = 0;

	fd = open("/dev/zero", O_RDONLY);
	if (fd > -1) {
		ret = readv(fd, (void *)addr, 32);
		(void)close(fd);
	}
	return ret;
}

static int bad_rename(void *addr)
{
	return rename((char *)addr, (char *)inc_addr(addr, 1));
}

#if defined(HAVE_SCHED_GETAFFINITY)
static int bad_sched_getaffinity(void *addr)
{
	return sched_getaffinity(getpid(), (size_t)8192, (cpu_set_t *)addr);
}
#endif

static int bad_select(void *addr)
{
	int fd, ret = 0;
	fd_set *readfds = addr;
	fd_set *writefds = readfds + 1;
	fd_set *exceptfds = writefds + 1;

	fd = open("/dev/zero", O_RDONLY);
	if (fd > -1) {
		ret = select(fd, readfds, writefds, exceptfds, (struct timeval *)inc_addr(addr, 4));
		(void)close(fd);
	}
	return ret;
}

static int bad_setitimer(void *addr)
{
	return setitimer(ITIMER_PROF, (struct itimerval *)addr,
		(struct itimerval *)inc_addr(addr, 1));
}

static int bad_setrlimit(void *addr)
{
	return setrlimit(RLIMIT_CPU, (struct rlimit *)addr);
}

static int bad_stat(void *addr)
{
	return stat((char *)addr, (struct stat *)inc_addr(addr, 1));
}

#if defined(HAVE_STATFS)
static int bad_statfs(void *addr)
{
	return statfs(".", (struct statfs *)addr);
}
#endif

#if defined(HAVE_SYS_SYSINFO_H) && 	\
    defined(HAVE_SYSINFO)
static int bad_sysinfo(void *addr)
{
	return sysinfo((struct sysinfo *)addr);
}
#endif

static int bad_time(void *addr)
{
	return (int )time((time_t *)addr);
}

#if defined(HAVE_LIB_RT) &&	\
    defined(HAVE_TIMER_CREATE)
static int bad_timer_create(void *addr)
{
	timer_t *timerid = (timer_t *)addr;

	timerid++;
	return timer_create(CLOCK_MONOTONIC, (struct sigevent *)addr, timerid);
}
#endif

static int bad_times(void *addr)
{
	return times((struct tms *)addr);
}

static int bad_truncate(void *addr)
{
	return truncate((char *)addr, 8192);
}

#if defined(HAVE_UNAME) && defined(HAVE_SYS_UTSNAME_H)
static int bad_uname(void *addr)
{
	return uname((struct utsname *)addr);
}
#endif

static int bad_ustat(void *addr)
{
	dev_t dev = { 0 };
	int ret;

	ret = shim_ustat(dev, (struct shim_ustat *)addr);
	if ((ret < 0) && (errno == ENOSYS)) {
		ret = 0;
		errno = 0;
	}
	return ret;
}

#if defined(HAVE_UTIME_H)
static int bad_utime(void *addr)
{
	return utime(addr, (struct utimbuf *)addr);
}
#endif

static int bad_utimes(void *addr)
{
	return utimes(addr, (const struct timeval *)inc_addr(addr, 1));
}

static int bad_wait(void *addr)
{
	return wait((int *)addr);
}

static int bad_waitpid(void *addr)
{
	return waitpid(getpid(), (int *)addr, (int)0);
}

#if defined(HAVE_WAITID)
static int bad_waitid(void *addr)
{
	return waitid(P_PID, getpid(), (siginfo_t *)addr, 0);
}
#endif

static int bad_write(void *addr)
{
	int fd, ret = 0;

	fd = open("/dev/null", O_WRONLY);
	if (fd > -1) {
		ret = write(fd, (void *)addr, 1024);
		(void)close(fd);
	}
	return ret;
}

static int bad_writev(void *addr)
{
	int fd, ret = 0;

	fd = open("/dev/zero", O_RDONLY);
	if (fd > -1) {
		ret = writev(fd, (void *)addr, 32);
		(void)close(fd);
	}
	return ret;
}

static stress_bad_syscall_t bad_syscalls[] = {
	bad_access,
/*
	bad_acct,
*/
	bad_bind,
	bad_chdir,
	bad_chmod,
	bad_chown,
#if defined(HAVE_CHROOT)
	bad_chroot,
#endif
#if defined(HAVE_CLOCK_GETRES) &&	\
    defined(CLOCK_REALTIME)
	bad_clock_getres,
#endif
#if defined(HAVE_CLOCK_GETTIME) &&	\
    defined(CLOCK_REALTIME)
	bad_clock_gettime,
#endif
#if defined(HAVE_CLOCK_NANOSLEEP) &&	\
    defined(CLOCK_REALTIME)
	bad_clock_nanosleep,
#endif
#if defined(CLOCK_THREAD_CPUTIME_ID) &&	\
    defined(HAVE_CLOCK_SETTIME)
	bad_clock_settime,
#endif
#if defined(HAVE_CLONE) && 	\
    defined(__linux__)
	bad_clone,
#endif
	bad_connect,
/*
	bad_creat,
*/
	bad_execve,
#if defined(HAVE_FACCESSAT)
	bad_faccessat,
#endif
	bad_fstat,
	bad_getcpu,
	bad_getcwd,
#if defined(HAVE_GETDOMAINNAME)
	bad_getdomainname,
#endif
	bad_getgroups,
	bad_get_mempolicy,
	bad_gethostname,
	bad_getitimer,
	bad_getpeername,
	bad_getrandom,
	bad_getrlimit,
#if defined(HAVE_GETRESGID)
	bad_getresgid,
#endif
#if defined(HAVE_GETRESUID)
	bad_getresuid,
#endif
	bad_getrlimit,
	bad_getrusage,
	bad_getsockname,
	bad_gettimeofday,
#if defined(HAVE_GETXATTR) &&	\
    (defined(HAVE_SYS_XATTR_H) || defined(HAVE_ATTR_XATTR_H))
	bad_getxattr,
#endif
#if defined(TCGETS)
	bad_ioctl,
#endif
	bad_lchown,
	bad_link,
	bad_lstat,
#if defined(HAVE_MADVISE)
	bad_madvise,
#endif
	bad_migrate_pages,
	bad_mincore,
#if defined(HAVE_MLOCK)
	bad_mlock,
#endif
#if defined(HAVE_MLOCK2)
	bad_mlock2,
#endif
#if defined(__NR_move_pages)
	bad_move_pages,
#endif
#if defined(HAVE_MSYNC)
	bad_msync,
#endif
#if defined(HAVE_MLOCK)
	bad_munlock,
#endif
#if defined(HAVE_NANOSLEEP)
	bad_nanosleep,
#endif
	bad_open,
	bad_pipe,
#if defined(HAVE_POLL_H)
	bad_poll,
#endif
	bad_pread,
#if defined(HAVE_PTRACE) &&	\
    defined(PTRACE_GETREGS)
	bad_ptrace,
#endif
	bad_pwrite,
	bad_read,
	bad_readlink,
	bad_readv,
	bad_rename,
#if defined(HAVE_SCHED_GETAFFINITY)
	bad_sched_getaffinity,
#endif
	bad_select,
	bad_setitimer,
	bad_setrlimit,
	bad_stat,
#if defined(HAVE_STATFS)
	bad_statfs,
#endif
#if defined(HAVE_SYS_SYSINFO_H) &&	\
    defined(HAVE_SYSINFO)
	bad_sysinfo,
#endif
	bad_time,
#if defined(HAVE_LIB_RT) &&	\
    defined(HAVE_TIMER_CREATE)
	bad_timer_create,
#endif
	bad_times,
	bad_truncate,
#if defined(HAVE_UNAME) && defined(HAVE_SYS_UTSNAME_H)
	bad_uname,
#endif
	bad_ustat,
#if defined(HAVE_UTIME_H)
	bad_utime,
#endif
	bad_utimes,
	bad_wait,
	bad_waitpid,
#if defined(HAVE_WAITID)
	bad_waitid,
#endif
	bad_write,
	bad_writev,
};


/*
 *  Call a system call in a child context so we don't clobber
 *  the parent
 */
static inline int stress_do_syscall(
	const stress_args_t *args,
	stress_bad_syscall_t bad_syscall,
	void *addr)
{
	pid_t pid;
	int rc = 0;

	if (!keep_stressing())
		return 0;
	pid = fork();
	if (pid < 0) {
		_exit(EXIT_NO_RESOURCE);
	} else if (pid == 0) {
		struct itimerval it;
		size_t i;
		int ret;

		/* Try to limit child from spawning */
		limit_procs(2);

		/* We don't want bad ops clobbering this region */
		stress_unmap_shared();

		/* Drop all capabilities */
		if (stress_drop_capabilities(args->name) < 0) {
			_exit(EXIT_NO_RESOURCE);
		}
		for (i = 0; i < SIZEOF_ARRAY(sigs); i++) {
			if (stress_sighandler(args->name, sigs[i], stress_badhandler, NULL) < 0)
				_exit(EXIT_FAILURE);
		}

		(void)setpgid(0, g_pgrp);
		stress_parent_died_alarm();

		/*
		 * Force abort if we take too long
		 */
		it.it_interval.tv_sec = 0;
		it.it_interval.tv_usec = 100000;
		it.it_value.tv_sec = 0;
		it.it_value.tv_usec = 100000;
		if (setitimer(ITIMER_REAL, &it, NULL) < 0) {
			pr_fail_dbg("setitimer");
			_exit(EXIT_NO_RESOURCE);
		}

		ret = bad_syscall(addr);
		if (ret < 0)
			ret = errno;
		_exit(ret);
	} else {
		int ret, status;

		ret = shim_waitpid(pid, &status, 0);
		if (ret < 0) {
			if (errno != EINTR)
				pr_dbg("%s: waitpid(): errno=%d (%s)\n",
					args->name, errno, strerror(errno));
			(void)kill(pid, SIGKILL);
			(void)shim_waitpid(pid, &status, 0);

		}
		rc = WEXITSTATUS(status);

		inc_counter(args);
	}
	return rc;
}

static int stress_sysbadaddr_child(const stress_args_t *args, void *context)
{
	(void)context;

	do {
		size_t i;

		for (i = 0; i < SIZEOF_ARRAY(bad_syscalls); i++) {
			size_t j;

			for (j = 0; j < SIZEOF_ARRAY(bad_addrs); j++) {
				int ret;
				void *addr = bad_addrs[j](args);

				ret = stress_do_syscall(args, bad_syscalls[i], addr);
				(void)ret;
			}
		}
	} while (keep_stressing());

	return EXIT_SUCCESS;
}

/*
 *  stress_sysbadaddr
 *	stress system calls with bad addresses
 */
static int stress_sysbadaddr(const stress_args_t *args)
{
	size_t page_size = args->page_size;
	int ret;

	ro_page = mmap(NULL, page_size, PROT_READ,
		MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
	if (ro_page == MAP_FAILED) {
		pr_inf("%s: cannot mmap anonymous read-only page: "
		       "errno=%d (%s)\n", args->name,errno, strerror(errno));
		return EXIT_NO_RESOURCE;
	}
	rw_page = mmap(NULL, page_size << 1, PROT_READ,
		MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
	if (rw_page == MAP_FAILED) {
		(void)munmap(ro_page, page_size);
		pr_inf("%s: cannot mmap anonymous read-write page: "
		       "errno=%d (%s)\n", args->name,errno, strerror(errno));
		return EXIT_NO_RESOURCE;
	}
	/*
	 * Unmap last page, so we know we have an unmapped
	 * page following the r/w page
	 */
	(void)munmap((void *)(((uint8_t *)rw_page) + page_size), page_size);

	ret = stress_oomable_child(args, NULL, stress_sysbadaddr_child, STRESS_OOMABLE_DROP_CAP);

	(void)munmap(rw_page, page_size);
	(void)munmap(ro_page, page_size);

	return ret;
}

stressor_info_t stress_sysbadaddr_info = {
	.stressor = stress_sysbadaddr,
	.class = CLASS_OS,
	.help = help
};
