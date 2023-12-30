/*
 * Copyright (C) 2013-2021 Canonical, Ltd.
 * Copyright (C) 2022-2023 Colin Ian King.
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
#include "core-cpu-cache.h"
#include "core-killpid.h"
#include "core-madvise.h"
#include "core-out-of-memory.h"
#include <sys/socket.h>
#include <sched.h>

#if defined(HAVE_TERMIOS_H)
#include <termios.h>
#endif

#if defined(HAVE_SYS_STATFS_H)
#include <sys/statfs.h>
#else
UNEXPECTED
#endif

#if defined(HAVE_SYS_UIO_H)
#include <sys/uio.h>
#else
UNEXPECTED
#endif

#if defined(HAVE_SYS_UTSNAME_H)
#include <sys/utsname.h>
#else
UNEXPECTED
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

#if defined(HAVE_POLL_H)
#include <poll.h>
#else
UNEXPECTED
#endif

#if defined(HAVE_UTIME_H)
#include <utime.h>
#else
UNEXPECTED
#endif

#if defined(HAVE_PTRACE)
#include <sys/ptrace.h>
#endif

#if defined(CLONE_CHILD_CLEARTID) &&	\
    defined(CLONE_CHILD_SETTID) &&	\
    defined(SIGCHLD)
#define STRESS_CLONE_FLAGS (CLONE_CHILD_CLEARTID | CLONE_CHILD_SETTID | SIGCHLD)
#else
#define STRESS_CLONE_FLAGS (0)
#endif

typedef void *(*stress_bad_addr_t)(stress_args_t *args);
typedef int (*stress_bad_syscall_t)(void *addr);
typedef struct {
	volatile size_t syscall_index;
	volatile size_t addr_index;
	volatile uint64_t counter;
	uint64_t max_ops;
} stress_sysbadaddr_state_t;

static stress_sysbadaddr_state_t *state;
static void *no_page;	/* no protect page */
static void *ro_page;	/* Read only page */
static void *rw_page;	/* Read-Write page */
static void *rx_page;	/* Read-eXecute page */
static void *wo_page;	/* Write only page */
static void *wx_page;	/* Write-eXecute page */

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
	lim.rlim_cur = (rlim_t)procs;
	lim.rlim_max = (rlim_t)procs;
	(void)setrlimit(RLIMIT_NPROC, &lim);
#else
	(void)procs;
#endif
}

static inline void *inc_addr(void *ptr, const size_t inc)
{
	return (void *)((char *)ptr + inc);
}

static void *unaligned_addr(stress_args_t *args)
{
	static uint64_t data[8] = {
		~0ULL, ~0ULL, ~0ULL, ~0ULL,
		~0ULL, ~0ULL, ~0ULL, ~0ULL
	};
	uint8_t *ptr = (uint8_t *)data;

	(void)args;

	return ptr + 1;
}

static void *readonly_addr(stress_args_t *args)
{
	(void)args;

	return ro_page;
}

static void *null_addr(stress_args_t *args)
{
	(void)args;

	return NULL;
}

static void *text_addr(stress_args_t *args)
{
	(void)args;

	return (void *)&write;
}

static void *bad_end_addr(stress_args_t *args)
{
	return ((uint8_t *)rw_page) + args->page_size - 1;
}

static void *bad_max_addr(stress_args_t *args)
{
	(void)args;

	return (void *)~(uintptr_t)0;
}

static void *unmapped_addr(stress_args_t *args)
{
	return ((uint8_t *)rw_page) + args->page_size;
}

static void *exec_addr(stress_args_t *args)
{
	(void)args;

	return rx_page;
}

static void *none_addr(stress_args_t *args)
{
	(void)args;

	return no_page;
}

static void *write_addr(stress_args_t *args)
{
	(void)args;

	return wo_page;
}

static void *write_exec_addr(stress_args_t *args)
{
	(void)args;

	return wx_page;
}

static const stress_bad_addr_t bad_addrs[] = {
	unaligned_addr,
	readonly_addr,
	null_addr,
	text_addr,
	bad_end_addr,
	bad_max_addr,
	unmapped_addr,
	exec_addr,
	none_addr,
	write_addr,
	write_exec_addr,
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

#if defined(HAVE_ASM_CACHECTL_H) &&	\
    defined(HAVE_CACHEFLUSH) &&		\
    defined(STRESS_ARCH_MIPS)
static int bad_cacheflush(void *addr)
{	
	return cacheflush(addr, 4096, SHIM_DCACHE);
}
#endif

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
	if (stress_mwc1())
		return clock_getres(CLOCK_REALTIME, (struct timespec *)addr);
	else
		return shim_clock_getres(CLOCK_REALTIME, (struct timespec *)addr);
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
static int bad_clock_nanosleep1(void *addr)
{
	return clock_nanosleep(CLOCK_REALTIME, 0,
		(const struct timespec *)addr,
		(struct timespec *)addr);
}

static int bad_clock_nanosleep2(void *addr)
{
	return clock_nanosleep(CLOCK_REALTIME, 0,
		(const struct timespec *)addr,
		(struct timespec *)NULL);
}

static int bad_clock_nanosleep3(void *addr)
{
	struct timespec ts;

	ts.tv_sec = 0;
	ts.tv_nsec = 0;

	return clock_nanosleep(CLOCK_REALTIME, 0,
		(const struct timespec *)&ts,
		(struct timespec *)addr);
}
#endif

#if defined(CLOCK_THREAD_CPUTIME_ID) &&	\
    defined(HAVE_CLOCK_SETTIME)
static int bad_clock_settime(void *addr)
{
	if (stress_mwc1())
		return clock_settime(CLOCK_THREAD_CPUTIME_ID, (struct timespec *)addr);
	else
		return shim_clock_settime(CLOCK_THREAD_CPUTIME_ID, (struct timespec *)addr);
}
#endif

#if defined(HAVE_CLONE) && 	\
    defined(__linux__)

static int clone_func(void *ptr)
{
	(void)ptr;

	_exit(0);
	return 0;
}

static int bad_clone1(void *addr)
{
	typedef int (*fn)(void *);
	int pid, status;

	pid = clone((fn)addr, (void *)addr, STRESS_CLONE_FLAGS, (void *)addr,
		(pid_t *)inc_addr(addr, 1),
		(void *)inc_addr(addr, 2),
		(pid_t *)inc_addr(addr, 3));
	if (pid > 1)
		(void)stress_kill_pid_wait(pid, &status);
	return 0;
}

static int bad_clone2(void *addr)
{
	int pid, status;

	pid = clone(clone_func, (void *)addr, STRESS_CLONE_FLAGS, NULL, NULL, NULL);
	if (pid > 1)
		(void)stress_kill_pid_wait(pid, &status);
	return 0;
}

static int bad_clone3(void *addr)
{
	char stack[8192];
	int pid, status;

	pid = clone(clone_func, (void *)stack, STRESS_CLONE_FLAGS, addr, NULL, NULL);
	if (pid > 1)
		(void)stress_kill_pid_wait(pid, &status);
	return 0;
}

static int bad_clone4(void *addr)
{
	char stack[8192];
	int pid, status;

	pid = clone(clone_func, (void *)stack, STRESS_CLONE_FLAGS, NULL, addr, NULL);
	if (pid > 1)
		(void)stress_kill_pid_wait(pid, &status);
	return 0;
}

static int bad_clone5(void *addr)
{
	char stack[8192];
	int pid, status;

	pid = clone(clone_func, (void *)stack, STRESS_CLONE_FLAGS, NULL, NULL, addr);
	if (pid > 1)
		(void)stress_kill_pid_wait(pid, &status);
	return 0;
}
#endif

static int bad_connect(void *addr)
{
	return connect(0, (const struct sockaddr *)addr, sizeof(struct sockaddr));
}

#if defined(HAVE_COPY_FILE_RANGE)
static int bad_copy_file_range(shim_off64_t *off_in, shim_off64_t *off_out)
{
	int ret, fdin, fdout;

	fdin = open("/dev/zero", O_RDONLY);
	if (fdin < 0)
		return -1;
	fdout = open("/dev/null", O_WRONLY);
	if (fdout < 0) {
		(void)close(fdout);
		return -1;
	}
	ret = (int)shim_copy_file_range(fdin, off_in, fdout, off_out, 1, 0);
	(void)close(fdout);
	(void)close(fdin);
	return ret;
}

static int bad_copy_file_range1(void *addr)
{
	return bad_copy_file_range((shim_off64_t *)addr, (shim_off64_t *)addr);
}

static int bad_copy_file_range2(void *addr)
{
	shim_off64_t off_out = 0ULL;

	return bad_copy_file_range((shim_off64_t *)addr, &off_out);
}

static int bad_copy_file_range3(void *addr)
{
	shim_off64_t off_in = 0ULL;

	return bad_copy_file_range(&off_in, (shim_off64_t *)addr);
}
#endif

/*
static int bad_creat(void *addr)
{
	return creat((char *)addr, 0);
}
*/

static int bad_execve1(void *addr)
{
	return execve((char *)addr, (char **)inc_addr(addr, 1),
		(char **)inc_addr(addr, 2));
}

static int bad_execve2(void *addr)
{
	char name[PATH_MAX];

	if (stress_get_proc_self_exe(name, sizeof(name)) == 0)
		return execve(name, addr, NULL);
	return -1;
}

static int bad_execve3(void *addr)
{
	char name[PATH_MAX];
	static char *newargv[] = { NULL, NULL };

	if (stress_get_proc_self_exe(name, sizeof(name)) == 0)
		return execve(name, newargv, addr);
	return -1;
}

static int bad_execve4(void *addr)
{
	static char *newargv[] = { NULL, NULL };

	return execve(addr, newargv, NULL);
}

#if defined(HAVE_FACCESSAT)
static int bad_faccessat(void *addr)
{
	return faccessat(AT_FDCWD, (char *)addr, R_OK, 0);
}
#endif

static int bad_fstat(void *addr)
{
	int ret, fd = 0;

#if defined(O_DIRECTORY)
	fd = open(stress_get_temp_path(), O_RDONLY | O_DIRECTORY);
	if (fd < 0)
		fd = 0;
#endif
	ret = shim_fstat(fd, (struct stat *)addr);
#if defined(O_DIRECTORY)
	if (fd)
		(void)close(fd);
#endif
	return ret;
}

static int bad_getcpu1(void *addr)
{
	return (int)shim_getcpu((unsigned *)addr, (unsigned *)inc_addr(addr, 2),
		(void *)inc_addr(addr, 2));
}

static int bad_getcpu2(void *addr)
{
	unsigned int node = 0;

	return (int)shim_getcpu((unsigned *)addr, &node, NULL);
}

static int bad_getcpu3(void *addr)
{
	unsigned int cpu;

	return (int)shim_getcpu(&cpu, (unsigned *)addr, NULL);
}

static int bad_getcpu4(void *addr)
{
	unsigned int node;
	unsigned int cpu;

	return (int)shim_getcpu(&cpu, &node, addr);
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
	return shim_getdomainname((char *)addr, 8192);
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

#if defined(HAVE_GETITIMER)
static int bad_getitimer(void *addr)
{
	return getitimer(ITIMER_PROF, (struct itimerval *)addr);
}
#endif

static int bad_getpeername1(void *addr)
{
	return getpeername(0, (struct sockaddr *)addr, (socklen_t *)inc_addr(addr, 1));
}

static int bad_getpeername2(void *addr)
{
	struct sockaddr saddr;

	(void)memset(&saddr, 0, sizeof(saddr));
	return getpeername(0, &saddr, (socklen_t *)addr);
}

static int bad_getpeername3(void *addr)
{
	socklen_t addrlen = sizeof(struct sockaddr);

	return getpeername(0, addr, &addrlen);
}

static int bad_get_mempolicy1(void *addr)
{
	return shim_get_mempolicy((int *)addr,
		(unsigned long *)inc_addr(addr, 1), 1,
		inc_addr(addr, 2), 0UL);
}

static int bad_get_mempolicy2(void *addr)
{
	int mode = 0;

	return shim_get_mempolicy(&mode, (unsigned long *)addr, 1, addr, 0UL);
}

static int bad_get_mempolicy3(void *addr)
{
	unsigned long nodemask = 1;

	return shim_get_mempolicy((int *)addr, &nodemask, 1, addr, 0UL);
}


static int bad_getrandom(void *addr)
{
	return shim_getrandom((void *)addr, 1024, 0);
}

#if defined(HAVE_GETRESGID)
static int bad_getresgid1(void *addr)
{
	return getresgid((gid_t *)addr, (gid_t *)inc_addr(addr, 1),
		(gid_t *)inc_addr(addr, 2));
}

static int bad_getresgid2(void *addr)
{
	uid_t egid, sgid;

	return getresgid((gid_t *)addr, &egid, &sgid);
}

static int bad_getresgid3(void *addr)
{
	gid_t rgid, sgid;

	return getresgid(&rgid, (uid_t *)addr, &sgid);
}

static int bad_getresgid4(void *addr)
{
	gid_t rgid, egid;

	return getresgid(&rgid, &egid, (gid_t *)addr);
}
#endif

#if defined(HAVE_GETRESUID)
static int bad_getresuid1(void *addr)
{
	return getresuid((uid_t *)addr, (uid_t *)inc_addr(addr, 1),
		(uid_t *)inc_addr(addr, 2));
}

static int bad_getresuid2(void *addr)
{
	uid_t euid, suid;

	return getresuid((uid_t *)addr, &euid, &suid);
}

static int bad_getresuid3(void *addr)
{
	uid_t ruid, suid;

	return getresuid(&ruid, (uid_t *)addr, &suid);
}

static int bad_getresuid4(void *addr)
{
	uid_t ruid, euid;

	return getresuid(&ruid, &euid, (uid_t *)addr);
}
#endif

static int bad_getrlimit(void *addr)
{
	return getrlimit(RLIMIT_CPU, (struct rlimit *)addr);
}

#if defined(HAVE_GETRUSAGE) &&	\
    defined(RUSAGE_SELF)
static int bad_getrusage(void *addr)
{
	return shim_getrusage(RUSAGE_SELF, (struct rusage *)addr);
}
#endif

static int bad_getsockname1(void *addr)
{
	return getsockname(0, (struct sockaddr *)addr, (socklen_t *)inc_addr(addr, 1));
}

static int bad_getsockname2(void *addr)
{
	struct sockaddr saddr;

	(void)memset(&addr, 0, sizeof(saddr));
	return getsockname(0, &saddr, (socklen_t *)addr);
}

static int bad_getsockname3(void *addr)
{
	socklen_t socklen = sizeof(struct sockaddr);

	return getsockname(0,  addr, &socklen);
}

static int bad_gettimeofday1(void *addr)
{
	struct timezone *tz = (struct timezone *)inc_addr(addr, 1);
	return gettimeofday((struct timeval *)addr, tz);
}

static int bad_gettimeofday2(void *addr)
{
	struct timeval tv;

	return gettimeofday(&tv, (struct timezone *)addr);
}

static int bad_gettimeofday3(void *addr)
{
	struct timezone tz;

	return gettimeofday((struct timeval *)addr, &tz);
}

#if defined(HAVE_GETXATTR) &&	\
    (defined(HAVE_SYS_XATTR_H) || defined(HAVE_ATTR_XATTR_H))
static int bad_getxattr1(void *addr)
{
	return (int)shim_getxattr((char *)addr, (char *)inc_addr(addr, 1),
		(void *)inc_addr(addr, 2), (size_t)32);
}

static int bad_getxattr2(void *addr)
{
	char buf[1024];

	return (int)shim_getxattr((char *)addr, "somename", buf, sizeof(buf));
}

static int bad_getxattr3(void *addr)
{
	char buf[1024];

	return (int)shim_getxattr(stress_get_temp_path(), (char *)addr, buf, sizeof(buf));
}

static int bad_getxattr4(void *addr)
{
	return (int)shim_getxattr(stress_get_temp_path(), "somename", addr, 1024);
}
#endif

#if defined(TCGETS)
static int bad_ioctl(void *addr)
{
	return ioctl(0, TCGETS, addr);
}
#else
UNEXPECTED
#endif

static int bad_lchown(void *addr)
{
	return lchown((char *)addr, getuid(), getgid());
}

static int bad_link1(void *addr)
{
	return link((char *)addr, (char *)inc_addr(addr, 1));
}

static int bad_link2(void *addr)
{
	return link(stress_get_temp_path(), (char *)addr);
}

static int bad_link3(void *addr)
{
	return link((char *)addr, stress_get_temp_path());
}

static int bad_lstat1(void *addr)
{
	return shim_lstat((const char *)addr, (struct stat *)inc_addr(addr, 1));
}

static int bad_lstat2(void *addr)
{
	struct stat statbuf;

	return shim_lstat(addr, &statbuf);
}

static int bad_lstat3(void *addr)
{
	return shim_lstat(stress_get_temp_path(), (struct stat *)addr);
}

#if defined(HAVE_MADVISE)
static int bad_madvise(void *addr)
{
	return madvise((void *)addr, 8192, MADV_NORMAL);
}
#else
UNEXPECTED
#endif

#if defined(HAVE_MEMFD_CREATE)
static int bad_memfd_create(void *addr)
{
	int fd;

	fd = shim_memfd_create(addr, 0);
	if (fd > 0)
		(void)close(fd);

	return fd;
}
#endif

static int bad_migrate_pages1(void *addr)
{
	return (int)shim_migrate_pages(getpid(), 1, (unsigned long *)addr,
		(unsigned long *)inc_addr(addr, 1));
}

static int bad_migrate_pages2(void *addr)
{
	unsigned long nodes = 0;

	return (int)shim_migrate_pages(getpid(), 1, &nodes, (unsigned long *)addr);
}

static int bad_migrate_pages3(void *addr)
{
	unsigned long nodes = 0;

	return (int)shim_migrate_pages(getpid(), 1, (unsigned long *)addr, &nodes);
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
#else
UNEXPECTED
#endif

#if defined(HAVE_MLOCK2)
static int bad_mlock2(void *addr)
{
	return shim_mlock2((void *)addr, 4096, 0);
}
#endif

#if defined(__NR_move_pages)
static int bad_move_pages1(void *addr)
{
	return (int)shim_move_pages(getpid(), (unsigned long)1, (void **)addr,
		(const int *)inc_addr(addr, 1), (int *)inc_addr(addr, 2), 0);
}

static int bad_move_pages2(void *addr)
{
	int nodes, status;

	return (int)shim_move_pages(getpid(), (unsigned long)1, (void **)addr, &nodes, &status, 0);
}

static int bad_move_pages3(void *addr)
{
	void *pages[1] = { addr };
	int status = 0;

	return (int)shim_move_pages(getpid(), (unsigned long)1, pages, (int *)addr, &status, 0);
}

static int bad_move_pages4(void *addr)
{
	void *pages[1] = { addr };
	int nodes = 0;

	return (int)shim_move_pages(getpid(), (unsigned long)1, pages, &nodes, (int *)addr, 0);
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
#else
UNEXPECTED
#endif

#if defined(HAVE_NANOSLEEP)
static int bad_nanosleep1(void *addr)
{
	return nanosleep((struct timespec *)addr,
		(struct timespec *)inc_addr(addr, 1));
}

static int bad_nanosleep2(void *addr)
{
	struct timespec rem;

	return nanosleep((struct timespec *)addr, &rem);
}

static int bad_nanosleep3(void *addr)
{
	struct timespec req;

	req.tv_sec = 0;
	req.tv_nsec = 0;

	return nanosleep(&req, (struct timespec *)addr);
}
#else
UNEXPECTED
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
	int fd;
	ssize_t ret = 0;

	fd = open("/dev/zero", O_RDONLY);
	if (fd > -1) {
		ret = pread(fd, addr, 1024, 0);
		(void)close(fd);
	}
	return (int)ret;
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
#else
UNEXPECTED
#endif

static int bad_pwrite(void *addr)
{
	int fd;
	ssize_t ret = 0;

	fd = open("/dev/null", O_WRONLY);
	if (fd > -1) {
		ret = pwrite(fd, (void *)addr, 1024, 0);
		(void)close(fd);
	}
	return (int)ret;
}

static int bad_read(void *addr)
{
	int fd;
	ssize_t ret = 0;

	fd = open("/dev/zero", O_RDONLY);
	if (fd > -1) {
		ret = read(fd, (void *)addr, 1024);
		(void)close(fd);
	}
	return (int)ret;
}

static int bad_readlink1(void *addr)
{
	return (int)shim_readlink((const char *)addr, (char *)inc_addr(addr, 1), 8192);
}

static int bad_readlink2(void *addr)
{
	return (int)shim_readlink(stress_get_temp_path(), (char *)addr, 8192);
}

static int bad_readlink3(void *addr)
{
	char buf[PATH_MAX];

	return (int)shim_readlink((const char *)addr, (char *)buf, PATH_MAX);
}

#if defined(HAVE_SYS_UIO_H)
static int bad_readv(void *addr)
{
	int fd;
	ssize_t ret = 0;

	fd = open("/dev/zero", O_RDONLY);
	if (fd > -1) {
		ret = readv(fd, (void *)addr, 32);
		(void)close(fd);
	}
	return (int)ret;
}
#endif

static int bad_rename1(void *addr)
{
	return rename((char *)addr, (char *)inc_addr(addr, 1));
}

static int bad_rename2(void *addr)
{
	return rename((char *)addr, "sng-tmp-17262");
}

#if defined(HAVE_SCHED_GETAFFINITY)
static int bad_sched_getaffinity(void *addr)
{
	return sched_getaffinity(getpid(), (size_t)8192, (cpu_set_t *)addr);
}
#else
UNEXPECTED
#endif

#if defined(HAVE_SELECT)
static int bad_select1(void *addr)
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

static int bad_select2(void *addr)
{
	int fd, ret = 0;
	fd_set readfds;
	fd_set writefds;
	fd_set exceptfds;

	FD_ZERO(&readfds);
	FD_ZERO(&writefds);
	FD_ZERO(&exceptfds);

	fd = open("/dev/zero", O_RDONLY);
	if (fd > -1) {
		ret = select(fd, &readfds, &writefds, &exceptfds, (struct timeval *)addr);
		(void)close(fd);
	}
	return ret;
}

static int bad_select3(void *addr)
{
	int fd, ret = 0;
	fd_set writefds;
	fd_set exceptfds;
	struct timeval tv;

	tv.tv_sec = 0;
	tv.tv_usec = 0;

	FD_ZERO(&writefds);
	FD_ZERO(&exceptfds);

	fd = open("/dev/zero", O_RDONLY);
	if (fd > -1) {
		ret = select(fd, addr, &writefds, &exceptfds, &tv);
		(void)close(fd);
	}
	return ret;
}

static int bad_select4(void *addr)
{
	int fd, ret = 0;
	fd_set readfds;
	fd_set exceptfds;
	struct timeval tv;

	tv.tv_sec = 0;
	tv.tv_usec = 0;

	FD_ZERO(&readfds);
	FD_ZERO(&exceptfds);

	fd = open("/dev/zero", O_RDONLY);
	if (fd > -1) {
		ret = select(fd, &readfds, addr, &exceptfds, &tv);
		(void)close(fd);
	}
	return ret;
}

static int bad_select5(void *addr)
{
	int fd, ret = 0;
	fd_set readfds;
	fd_set writefds;
	struct timeval tv;

	tv.tv_sec = 0;
	tv.tv_usec = 0;

	FD_ZERO(&readfds);
	FD_ZERO(&writefds);

	fd = open("/dev/zero", O_RDONLY);
	if (fd > -1) {
		ret = select(fd, &readfds, &writefds, addr, &tv);
		(void)close(fd);
	}
	return ret;
}

#endif

#if defined(HAVE_SETITIMER)
static int bad_setitimer1(void *addr)
{
	return setitimer(ITIMER_PROF, (struct itimerval *)addr,
		(struct itimerval *)inc_addr(addr, 1));
}

static int bad_setitimer2(void *addr)
{
	struct itimerval oldval;

	return setitimer(ITIMER_PROF, (struct itimerval *)addr, &oldval);
}

static int bad_setitimer3(void *addr)
{
	struct itimerval newval;

	(void)memset(&newval, 0, sizeof(newval));

	return setitimer(ITIMER_PROF, &newval, (struct itimerval *)addr);
}
#endif

static int bad_setrlimit(void *addr)
{
	return setrlimit(RLIMIT_CPU, (struct rlimit *)addr);
}

static int bad_stat1(void *addr)
{
	return shim_stat((char *)addr, (struct stat *)inc_addr(addr, 1));
}

static int bad_stat2(void *addr)
{
	return shim_stat(stress_get_temp_path(), (struct stat *)addr);
}

static int bad_stat3(void *addr)
{
	struct stat statbuf;

	return shim_stat((char *)addr, &statbuf);
}

#if defined(HAVE_STATFS)
static int bad_statfs(void *addr)
{
	return statfs(".", (struct statfs *)addr);
}
#else
UNEXPECTED
#endif

#if defined(HAVE_SYS_SYSINFO_H) && 	\
    defined(HAVE_SYSINFO)
static int bad_sysinfo(void *addr)
{
	return sysinfo((struct sysinfo *)addr);
}
#else
UNEXPECTED
#endif

static int bad_time(void *addr)
{
	time_t ret;

	if (stress_mwc1())
		ret = time((time_t *)addr);
	else
		ret = shim_time((time_t *)addr);

	return (ret == ((time_t) -1)) ? -1 : 0;
}

#if defined(HAVE_LIB_RT) &&	\
    defined(HAVE_TIMER_CREATE)
static int bad_timer_create(void *addr)
{
	timer_t *timerid = (timer_t *)addr;

	timerid++;
	return timer_create(CLOCK_MONOTONIC, (struct sigevent *)addr, timerid);
}
#else
UNEXPECTED
#endif

static int bad_times(void *addr)
{
	return (int)times((struct tms *)addr);
}

static int bad_truncate(void *addr)
{
	return truncate((char *)addr, 8192);
}

#if defined(HAVE_UNAME) &&	\
    defined(HAVE_SYS_UTSNAME_H)
static int bad_uname(void *addr)
{
	return uname((struct utsname *)addr);
}
#else
UNEXPECTED
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

static int bad_utimes1(void *addr)
{
	return utimes(addr, (const struct timeval *)inc_addr(addr, 1));
}

static int bad_utimes2(void *addr)
{
	return utimes(stress_get_temp_path(), (const struct timeval *)addr);
}

static int bad_utimes3(void *addr)
{
	return utimes(addr, NULL);
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
	return waitid(P_PID, (id_t)getpid(), (siginfo_t *)addr, 0);
}
#else
UNEXPECTED
#endif

static int bad_write(void *addr)
{
	int fd;
	ssize_t ret = 0;

	fd = open("/dev/null", O_WRONLY);
	if (fd > -1) {
		ret = write(fd, (void *)addr, 1024);
		(void)close(fd);
	}
	return (int)ret;
}

#if defined(HAVE_SYS_UIO_H)
static int bad_writev(void *addr)
{
	int fd;
	ssize_t ret = 0;

	fd = open("/dev/zero", O_RDONLY);
	if (fd > -1) {
		ret = writev(fd, (void *)addr, 32);
		(void)close(fd);
	}
	return (int)ret;
}
#else
UNEXPECTED
#endif

static stress_bad_syscall_t bad_syscalls[] = {
	bad_access,
/*
	bad_acct,
*/
	bad_bind,
#if defined(HAVE_ASM_CACHECTL_H) &&     \
    defined(HAVE_CACHEFLUSH) &&         \
    defined(STRESS_ARCH_MIPS)
	bad_cacheflush,
#endif
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
	bad_clock_nanosleep1,
	bad_clock_nanosleep2,
	bad_clock_nanosleep3,
#endif
#if defined(CLOCK_THREAD_CPUTIME_ID) &&	\
    defined(HAVE_CLOCK_SETTIME)
	bad_clock_settime,
#endif
#if defined(HAVE_CLONE) && 	\
    defined(__linux__)
	bad_clone1,
	bad_clone2,
	bad_clone3,
	bad_clone4,
	bad_clone5,
#endif
	bad_connect,
#if defined(HAVE_COPY_FILE_RANGE)
	bad_copy_file_range1,
	bad_copy_file_range2,
	bad_copy_file_range3,
#endif
/*
	bad_creat,
*/
	bad_execve1,
	bad_execve2,
	bad_execve3,
	bad_execve4,
#if defined(HAVE_FACCESSAT)
	bad_faccessat,
#endif
	bad_fstat,
	bad_getcpu1,
	bad_getcpu2,
	bad_getcpu3,
	bad_getcpu4,
	bad_getcwd,
#if defined(HAVE_GETDOMAINNAME)
	bad_getdomainname,
#endif
	bad_getgroups,
	bad_get_mempolicy1,
	bad_get_mempolicy2,
	bad_get_mempolicy3,
#if defined(HAVE_GETHOSTNAME)
	bad_gethostname,
#endif
#if defined(HAVE_GETITIMER)
	bad_getitimer,
#endif
	bad_getpeername1,
	bad_getpeername2,
	bad_getpeername3,
	bad_getrandom,
	bad_getrlimit,
#if defined(HAVE_GETRESGID)
	bad_getresgid1,
	bad_getresgid2,
	bad_getresgid3,
	bad_getresgid4,
#endif
#if defined(HAVE_GETRESUID)
	bad_getresuid1,
	bad_getresuid2,
	bad_getresuid3,
	bad_getresuid4,
#endif
	bad_getrlimit,
#if defined(HAVE_GETRUSAGE) &&	\
    defined(RUSAGE_SELF)
	bad_getrusage,
#endif
	bad_getsockname1,
	bad_getsockname2,
	bad_getsockname3,
	bad_gettimeofday1,
	bad_gettimeofday2,
	bad_gettimeofday3,
#if defined(HAVE_GETXATTR) &&	\
    (defined(HAVE_SYS_XATTR_H) || defined(HAVE_ATTR_XATTR_H))
	bad_getxattr1,
	bad_getxattr2,
	bad_getxattr3,
	bad_getxattr4,
#endif
#if defined(TCGETS)
	bad_ioctl,
#endif
	bad_lchown,
	bad_link1,
	bad_link2,
	bad_link3,
	bad_lstat1,
	bad_lstat2,
	bad_lstat3,
#if defined(HAVE_MADVISE)
	bad_madvise,
#endif
#if defined(HAVE_MEMFD_CREATE)
	bad_memfd_create,
#endif
	bad_migrate_pages1,
	bad_migrate_pages2,
	bad_migrate_pages3,
	bad_mincore,
#if defined(HAVE_MLOCK)
	bad_mlock,
#endif
#if defined(HAVE_MLOCK2)
	bad_mlock2,
#endif
#if defined(__NR_move_pages)
	bad_move_pages1,
	bad_move_pages2,
	bad_move_pages3,
	bad_move_pages4,
#endif
#if defined(HAVE_MSYNC)
	bad_msync,
#endif
#if defined(HAVE_MLOCK)
	bad_munlock,
#endif
#if defined(HAVE_NANOSLEEP)
	bad_nanosleep1,
	bad_nanosleep2,
	bad_nanosleep3,
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
	bad_readlink1,
	bad_readlink2,
	bad_readlink3,
#if defined(HAVE_SYS_UIO_H)
	bad_readv,
#endif
	bad_rename1,
	bad_rename2,
#if defined(HAVE_SCHED_GETAFFINITY)
	bad_sched_getaffinity,
#endif
#if defined(HAVE_SELECT)
	bad_select1,
	bad_select2,
	bad_select3,
	bad_select4,
	bad_select5,
#endif
#if defined(HAVE_SETITIMER)
	bad_setitimer1,
	bad_setitimer2,
	bad_setitimer3,
#endif
	bad_setrlimit,
	bad_stat1,
	bad_stat2,
	bad_stat3,
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
#if defined(HAVE_UNAME) &&	\
    defined(HAVE_SYS_UTSNAME_H)
	bad_uname,
#endif
	bad_ustat,
#if defined(HAVE_UTIME_H)
	bad_utime,
#endif
	bad_utimes1,
	bad_utimes2,
	bad_utimes3,
	bad_wait,
	bad_waitpid,
#if defined(HAVE_WAITID)
	bad_waitid,
#endif
	bad_write,
#if defined(HAVE_SYS_UIO_H)
	bad_writev,
#endif
};


/*
 *  Call a system call in a child context so we don't clobber
 *  the parent
 */
static inline int stress_do_syscall(stress_args_t *args)
{
	pid_t pid;
	int rc = 0;

	/* force update of 1 bit mwc random val */
	(void)stress_mwc1();

	if (!stress_continue(args))
		return 0;
	pid = fork();
	if (pid < 0) {
		_exit(EXIT_NO_RESOURCE);
	} else if (pid == 0) {
#if defined(HAVE_SETITIMER)
		struct itimerval it;
#endif
		size_t k;

		/* Try to limit child from spawning */
		limit_procs(2);

		/* We don't want bad ops clobbering this region */
		stress_shared_readonly();

		/* Drop all capabilities */
		if (stress_drop_capabilities(args->name) < 0) {
			_exit(EXIT_NO_RESOURCE);
		}
		for (k = 0; k < SIZEOF_ARRAY(sigs); k++) {
			if (stress_sighandler(args->name, sigs[k], stress_sig_handler_exit, NULL) < 0)
				_exit(EXIT_FAILURE);
		}

		stress_parent_died_alarm();
		(void)sched_settings_apply(true);

		state->counter = stress_bogo_get(args);
		while (state->syscall_index < SIZEOF_ARRAY(bad_syscalls)) {
			while (state->addr_index < SIZEOF_ARRAY(bad_addrs)) {
				void *addr = bad_addrs[state->addr_index];
				const stress_bad_syscall_t bad_syscall = bad_syscalls[state->syscall_index];

				if (addr) {
#if defined(HAVE_SETITIMER)
					/*
					 * Force abort if we take too long
					 */
					it.it_interval.tv_sec = 0;
					it.it_interval.tv_usec = 100000;
					it.it_value.tv_sec = 0;
					it.it_value.tv_usec = 100000;
					if (setitimer(ITIMER_REAL, &it, NULL) < 0)
						_exit(EXIT_NO_RESOURCE);
#endif
					state->counter++;
					if ((state->max_ops) && (state->counter >= state->max_ops))
						_exit(EXIT_SUCCESS);

					(void)bad_syscall(addr);
				}
				state->addr_index++;
			}
			state->addr_index = 0;
			state->syscall_index++;
		}
		_exit(EXIT_SUCCESS);
	} else {
		int ret, status;

		ret = shim_waitpid(pid, &status, 0);
		if (ret < 0) {
			if (errno != EINTR)
				pr_dbg("%s: waitpid(): errno=%d (%s)\n",
					args->name, errno, strerror(errno));
			(void)stress_kill_pid_wait(pid, &status);
		}
		rc = WEXITSTATUS(status);
	}
	stress_bogo_set(args, state->counter);
	return rc;
}

static int stress_sysbadaddr_child(stress_args_t *args, void *context)
{
	(void)context;

	do {
		size_t last_syscall_index = 0;

		state->syscall_index = 0;
		while (state->syscall_index < SIZEOF_ARRAY(bad_syscalls)) {
			size_t last_addr_index = 0;

			state->addr_index = 0;
			while (state->addr_index < SIZEOF_ARRAY(bad_addrs)) {
				if (bad_addrs[state->addr_index]) {
					stress_do_syscall(args);
				}
				if (last_addr_index == state->addr_index)
					state->addr_index++;
				last_addr_index = state->addr_index;
			}
			if (last_syscall_index == state->syscall_index)
				state->syscall_index++;
			last_syscall_index = state->syscall_index;
		}
	} while (stress_continue(args));

	return EXIT_SUCCESS;
}

static void stress_munmap(void *addr, size_t sz)
{
	if ((addr != NULL) && (addr != MAP_FAILED))
		(void)munmap(addr, sz);
}

/*
 *  stress_sysbadaddr
 *	stress system calls with bad addresses
 */
static int stress_sysbadaddr(stress_args_t *args)
{
	size_t page_size = args->page_size;
	int ret;

	state = (stress_sysbadaddr_state_t *)stress_mmap_populate(NULL,
		sizeof(*state), PROT_READ | PROT_WRITE,
		MAP_ANONYMOUS | MAP_SHARED, -1, 0);
	if (state == MAP_FAILED) {
		pr_inf_skip("%s: cannot mmap anonymous state structure: "
		       "errno=%d (%s), skipping stressor\n",
			args->name, errno, strerror(errno));
		ret = EXIT_NO_RESOURCE;
		goto cleanup;
	}

	ro_page = stress_mmap_populate(NULL, page_size,
		PROT_READ, MAP_ANONYMOUS | MAP_SHARED, -1, 0);
	if (ro_page == MAP_FAILED) {
		pr_inf_skip("%s: cannot mmap anonymous read-only page: "
		       "errno=%d (%s), skipping stressor\n",
			args->name, errno, strerror(errno));
		ret = EXIT_NO_RESOURCE;
		goto cleanup;
	}
	(void)stress_madvise_mergeable(ro_page, page_size);

	rw_page = stress_mmap_populate(NULL, page_size << 1,
		PROT_READ | PROT_WRITE,
		MAP_ANONYMOUS | MAP_SHARED, -1, 0);
	if (rw_page == MAP_FAILED) {
		pr_inf_skip("%s: cannot mmap anonymous read-write page: "
		       "errno=%d (%s), skipping stressor\n",
			args->name, errno, strerror(errno));
		ret = EXIT_NO_RESOURCE;
		goto cleanup;
	}
	(void)stress_madvise_mergeable(rw_page, page_size << 1);

	rx_page = stress_mmap_populate(NULL, page_size,
		PROT_EXEC | PROT_READ,
		MAP_ANONYMOUS | MAP_SHARED, -1, 0);
	if (rx_page == MAP_FAILED) {
		pr_inf_skip("%s: cannot mmap anonymous execute-only page: "
		       "errno=%d (%s), skipping stressor\n",
			args->name, errno, strerror(errno));
		ret = EXIT_NO_RESOURCE;
		goto cleanup;
	}
	(void)stress_madvise_mergeable(rx_page, page_size);

	no_page = stress_mmap_populate(NULL, page_size,
		PROT_NONE, MAP_ANONYMOUS | MAP_SHARED, -1, 0);
	if (no_page == MAP_FAILED) {
		pr_inf_skip("%s: cannot mmap anonymous prot-none page: "
		       "errno=%d (%s), skipping stressor\n",
			args->name, errno, strerror(errno));
		ret = EXIT_NO_RESOURCE;
		goto cleanup;
	}

	wo_page = stress_mmap_populate(NULL, page_size,
		PROT_WRITE, MAP_ANONYMOUS | MAP_SHARED, -1, 0);
	if (wo_page == MAP_FAILED) {
		pr_inf_skip("%s: cannot mmap anonymous write-only page: "
		       "errno=%d (%s), skipping stressor\n",
			args->name, errno, strerror(errno));
		ret = EXIT_NO_RESOURCE;
		goto cleanup;
	}
	(void)stress_madvise_mergeable(wo_page, page_size);

	/*
	 * write-execute pages are not supported by some kernels
	 * so make this failure non-fatal to the stress testing
	 * and skip MAP_FAILED addresses in the main stressor
	 */
	wx_page = stress_mmap_populate(NULL, page_size,
		PROT_WRITE | PROT_EXEC, MAP_ANONYMOUS | MAP_SHARED, -1, 0);
	/*
	 * Unmap last page, so we know we have an unmapped
	 * page following the r/w page
	 */
	(void)munmap((void *)(((uint8_t *)rw_page) + page_size), page_size);

	stress_set_proc_state(args->name, STRESS_STATE_RUN);

	ret = stress_oomable_child(args, NULL, stress_sysbadaddr_child, STRESS_OOMABLE_DROP_CAP);

cleanup:
	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);
	stress_munmap(wo_page, page_size);
	stress_munmap(no_page, page_size);
	stress_munmap(rx_page, page_size);
	stress_munmap(rw_page, page_size);
	stress_munmap(ro_page, page_size);
	stress_munmap((void *)state, sizeof(*state));

	return ret;
}

stressor_info_t stress_sysbadaddr_info = {
	.stressor = stress_sysbadaddr,
	.class = CLASS_OS,
	.help = help
};
