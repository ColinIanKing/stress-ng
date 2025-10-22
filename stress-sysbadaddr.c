/*
 * Copyright (C) 2013-2021 Canonical, Ltd.
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
#include "core-capabilities.h"
#include "core-cpu-cache.h"
#include "core-killpid.h"
#include "core-madvise.h"
#include "core-mmap.h"
#include "core-out-of-memory.h"

#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/times.h>
#include <sched.h>
#include <time.h>

#if defined(HAVE_TERMIOS_H)
#include <termios.h>
#endif

#if defined(HAVE_SYS_SELECT_H)
#include <sys/select.h>
#else
UNEXPECTED
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

typedef void *(*stress_bad_addr_func_t)(stress_args_t *args);
typedef struct {
	stress_bad_addr_func_t	func;
	void *addr;
	bool unreadable;
	bool unwriteable;
} stress_bad_addr_t;

typedef void (*stress_bad_syscall_t)(stress_bad_addr_t *ba, volatile uint64_t *counter);
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

static stress_bad_addr_t bad_addrs[] = {
	/* func,	        addr, unreadable, uwriteable */
	{ unaligned_addr,	NULL, false,	false},
	{ readonly_addr,	NULL, false,	true},
	{ null_addr,		NULL, true,	true },
	{ text_addr,		NULL, false,	true },
	{ bad_end_addr,		NULL, false,	true },
	{ bad_max_addr,		NULL, true,	true },
	{ unmapped_addr,	NULL, true,	true },
	{ exec_addr,		NULL, false,	true },
	{ none_addr,		NULL, true,	true },
	{ write_addr,		NULL, false,	false },
	{ write_exec_addr,	NULL, false,	false },
};

static void bad_access(stress_bad_addr_t *ba, volatile uint64_t *counter)
{
	if (ba->unreadable) {
		(*counter)++;
		VOID_RET(int, access((char *)ba->addr, R_OK));
	}
}

/*
static int bad_acct(stress_bad_addr_t *ba, volatile uint64_t *counter)
{
	if (ba->unreadable) {
		(*counter)++;
		VOID_RET(int, acct((char *)ba->addr));
	}
}
*/

static void bad_bind(stress_bad_addr_t *ba, volatile uint64_t *counter)
{
	if (ba->unreadable) {
		(*counter)++;
		VOID_RET(int, bind(0, (struct sockaddr *)ba->addr, 0));
	}
}

#if defined(HAVE_ASM_CACHECTL_H) &&	\
    defined(HAVE_CACHEFLUSH) &&		\
    defined(STRESS_ARCH_MIPS)
static void bad_cacheflush(stress_bad_addr_t *ba, volatile uint64_t *counter)
{
	(*counter)++;
	stress_cpu_data_cache_flush(ba->addr, 4096);
}
#endif

static void bad_chdir(stress_bad_addr_t *ba, volatile uint64_t *counter)
{
	if (ba->unreadable) {
		(*counter)++;
		VOID_RET(int, chdir((char *)ba->addr));
	}
}

static void bad_chmod(stress_bad_addr_t *ba, volatile uint64_t *counter)
{
	if (ba->unreadable) {
		(*counter)++;
		VOID_RET(int, chmod((char *)ba->addr, 0));
	}
}

static void bad_chown(stress_bad_addr_t *ba, volatile uint64_t *counter)
{
	if (ba->unreadable) {
		(*counter)++;
		VOID_RET(int, chown((char *)ba->addr, getuid(), getgid()));
	}
}

#if defined(HAVE_CHROOT)
static void bad_chroot(stress_bad_addr_t *ba, volatile uint64_t *counter)
{
	if (ba->unreadable) {
		(*counter)++;
		VOID_RET(int, chroot((char *)ba->addr));
	}
}
#endif

#if defined(HAVE_CLOCK_GETRES) &&	\
    defined(CLOCK_REALTIME)
static void bad_clock_getres(stress_bad_addr_t *ba, volatile uint64_t *counter)
{
	if (ba->unwriteable) {
		(*counter)++;
		if (stress_mwc1())
			VOID_RET(int, clock_getres(CLOCK_REALTIME, (struct timespec *)ba->addr));
		else
			VOID_RET(int, shim_clock_getres(CLOCK_REALTIME, (struct timespec *)ba->addr));
	}
}
#endif

#if defined(HAVE_CLOCK_GETTIME) &&	\
    defined(CLOCK_REALTIME)
static void bad_clock_gettime(stress_bad_addr_t *ba, volatile uint64_t *counter)
{
	if (ba->unwriteable) {
		(*counter)++;
		VOID_RET(int, clock_gettime(CLOCK_REALTIME, (struct timespec *)ba->addr));
	}
}
#endif

#if defined(HAVE_CLOCK_NANOSLEEP) &&	\
    defined(CLOCK_REALTIME)
static void bad_clock_nanosleep1(stress_bad_addr_t *ba, volatile uint64_t *counter)
{
	(*counter)++;
	VOID_RET(int, clock_nanosleep(CLOCK_REALTIME, 0,
			(const struct timespec *)ba->addr,
			(struct timespec *)ba->addr));
}

static void bad_clock_nanosleep2(stress_bad_addr_t *ba, volatile uint64_t *counter)
{
	if (ba->unreadable) {
		(*counter)++;
		VOID_RET(int, clock_nanosleep(CLOCK_REALTIME, 0,
				(const struct timespec *)ba->addr,
				(struct timespec *)NULL));
	}
}

static void bad_clock_nanosleep3(stress_bad_addr_t *ba, volatile uint64_t *counter)
{
	if (ba->unwriteable) {
		struct timespec ts;

		ts.tv_sec = 0;
		ts.tv_nsec = 0;

		(*counter)++;
		VOID_RET(int, clock_nanosleep(CLOCK_REALTIME, 0,
				(const struct timespec *)&ts,
				(struct timespec *)ba->addr));
	}
}
#endif

#if defined(CLOCK_THREAD_CPUTIME_ID) &&	\
    defined(HAVE_CLOCK_SETTIME)
static void bad_clock_settime(stress_bad_addr_t *ba, volatile uint64_t *counter)
{
	if (ba->unreadable) {
		(*counter)++;
		if (stress_mwc1())
			VOID_RET(int, clock_settime(CLOCK_THREAD_CPUTIME_ID, (struct timespec *)ba->addr));
		else
			VOID_RET(int, shim_clock_settime(CLOCK_THREAD_CPUTIME_ID, (struct timespec *)ba->addr));
	}
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

static void bad_clone1(stress_bad_addr_t *ba, volatile uint64_t *counter)
{
	typedef int (*fn)(void *);
	int pid, status;

	(*counter)++;
	pid = clone((fn)ba->addr, (void *)ba->addr, STRESS_CLONE_FLAGS, (void *)ba->addr,
		(pid_t *)inc_addr(ba->addr, 1),
		(void *)inc_addr(ba->addr, 2),
		(pid_t *)inc_addr(ba->addr, 3));
	if (pid > 1)
		(void)stress_kill_pid_wait(pid, &status);
}

static void bad_clone2(stress_bad_addr_t *ba, volatile uint64_t *counter)
{
	int pid, status;

	(*counter)++;
	pid = clone(clone_func, (void *)ba->addr, STRESS_CLONE_FLAGS, NULL, NULL, NULL);
	if (pid > 1)
		(void)stress_kill_pid_wait(pid, &status);
}

static void bad_clone3(stress_bad_addr_t *ba, volatile uint64_t *counter)
{
	if (ba->unwriteable) {
		char stack[8192];
		int pid, status;

		(*counter)++;
		pid = clone(clone_func, (void *)stack, STRESS_CLONE_FLAGS, ba->addr, NULL, NULL);
		if (pid > 1)
			(void)stress_kill_pid_wait(pid, &status);
	}
}

static void bad_clone4(stress_bad_addr_t *ba, volatile uint64_t *counter)
{
	if (ba->unwriteable) {
		char stack[8192];
		int pid, status;

		(*counter)++;
		pid = clone(clone_func, (void *)stack, STRESS_CLONE_FLAGS, NULL, ba->addr, NULL);
		if (pid > 1)
			(void)stress_kill_pid_wait(pid, &status);
	}
}

static void bad_clone5(stress_bad_addr_t *ba, volatile uint64_t *counter)
{
	if (ba->unwriteable) {
		char stack[8192];
		int pid, status;

		(*counter)++;
		pid = clone(clone_func, (void *)stack, STRESS_CLONE_FLAGS, NULL, NULL, ba->addr);
		if (pid > 1)
			(void)stress_kill_pid_wait(pid, &status);
	}
}
#endif

static void bad_connect(stress_bad_addr_t *ba, volatile uint64_t *counter)
{
	if (ba->unreadable) {
		(*counter)++;
		VOID_RET(int, connect(0, (const struct sockaddr *)ba->addr, sizeof(struct sockaddr)));
	}
}

#if defined(HAVE_COPY_FILE_RANGE)
static void bad_copy_file_range(shim_off64_t *off_in, shim_off64_t *off_out, volatile uint64_t *counter)
{
	int fdin, fdout;

	fdin = open("/dev/zero", O_RDONLY);
	if (fdin < 0)
		return;
	fdout = open("/dev/null", O_WRONLY);
	if (fdout < 0) {
		VOID_RET(int, close(fdin));
		return;
	}
	(*counter)++;
	VOID_RET(ssize_t, shim_copy_file_range(fdin, off_in, fdout, off_out, 1, 0));
	VOID_RET(int, close(fdout));
	VOID_RET(int, close(fdin));
}

static void bad_copy_file_range1(stress_bad_addr_t *ba, volatile uint64_t *counter)
{
	bad_copy_file_range((shim_off64_t *)ba->addr, (shim_off64_t *)ba->addr, counter);
}

static void bad_copy_file_range2(stress_bad_addr_t *ba, volatile uint64_t *counter)
{
	shim_off64_t off_out = 0ULL;

	bad_copy_file_range((shim_off64_t *)ba->addr, &off_out, counter);
}

static void bad_copy_file_range3(stress_bad_addr_t *ba, volatile uint64_t *counter)
{
	shim_off64_t off_in = 0ULL;

	bad_copy_file_range(&off_in, (shim_off64_t *)ba->addr, counter);
}
#endif

/*
static void bad_creat(stress_bad_addr_t *ba, volatile uint64_t *counter)
{
	if (ba->unreadable) {
		(*counter)++;
		VOID_RET(int, creat((char *)ba->addr, 0));
	}
}
*/

static void bad_execve1(stress_bad_addr_t *ba, volatile uint64_t *counter)
{
	if (ba->unreadable) {
		(*counter)++;
		VOID_RET(int, execve((char *)ba->addr, (char **)inc_addr(ba->addr, 1),
				(char **)inc_addr(ba->addr, 2)));
	}
}

static void bad_execve2(stress_bad_addr_t *ba, volatile uint64_t *counter)
{
	if (ba->unreadable) {
		char name[PATH_MAX];

		if (stress_get_proc_self_exe(name, sizeof(name)) == 0) {
			(*counter)++;
			VOID_RET(int, execve(name, ba->addr, NULL));
		}
	}
}

static void bad_execve3(stress_bad_addr_t *ba, volatile uint64_t *counter)
{
	if (ba->unreadable) {
		char name[PATH_MAX];

		if (stress_get_proc_self_exe(name, sizeof(name)) == 0) {
			static char *newargv[] = { NULL, NULL };

			(*counter)++;
			VOID_RET(int, execve(name, newargv, ba->addr));
		}
	}
}

static void bad_execve4(stress_bad_addr_t *ba, volatile uint64_t *counter)
{
	if (ba->unreadable) {
		static char *newargv[] = { NULL, NULL };

		(*counter)++;
		VOID_RET(int, execve(ba->addr, newargv, NULL));
	}
}

#if defined(HAVE_FACCESSAT)
static void bad_faccessat(stress_bad_addr_t *ba, volatile uint64_t *counter)
{
	if (ba->unreadable) {
		(*counter)++;
		VOID_RET(int, faccessat(AT_FDCWD, (char *)ba->addr, R_OK, 0));
	}
}
#endif

#if defined(HAVE_FLISTXATTR) &&	\
    (defined(HAVE_SYS_XATTR_H) || defined(HAVE_ATTR_XATTR_H))
static void bad_flistxattr(stress_bad_addr_t *ba, volatile uint64_t *counter)
{
#if defined(O_DIRECTORY)
	int fd;

	fd = open(stress_get_temp_path(), O_RDONLY | O_DIRECTORY);
	if (fd >= 0) {
		(*counter)++;
		VOID_RET(ssize_t, shim_flistxattr(fd, (char *)ba->addr, 1024));
		VOID_RET(int, close(fd));
	}
#endif
}
#endif

static void bad_fstat(stress_bad_addr_t *ba, volatile uint64_t *counter)
{
	if (ba->unreadable) {
		int fd = 0;

#if defined(O_DIRECTORY)
		fd = open(stress_get_temp_path(), O_RDONLY | O_DIRECTORY);
		if (fd < 0)
			fd = 0;
#endif
		(*counter)++;
		VOID_RET(int, shim_fstat(fd, (struct stat *)ba->addr));
#if defined(O_DIRECTORY)
		if (fd >= 0)
			VOID_RET(int, close(fd));
#endif
	}
}

static void bad_getcpu1(stress_bad_addr_t *ba, volatile uint64_t *counter)
{
	if (ba->unwriteable) {
		(*counter)++;
		VOID_RET(int, shim_getcpu((unsigned *)ba->addr, (unsigned *)inc_addr(ba->addr, 2),
				(void *)inc_addr(ba->addr, 2)));
	}
}

static void bad_getcpu2(stress_bad_addr_t *ba, volatile uint64_t *counter)
{
	if (ba->unwriteable) {
		unsigned int node = 0;

		(*counter)++;
		VOID_RET(int, shim_getcpu((unsigned *)ba->addr, &node, NULL));
	}
}

static void bad_getcpu3(stress_bad_addr_t *ba, volatile uint64_t *counter)
{
	if (ba->unwriteable) {
		unsigned int cpu;

		(*counter)++;
		VOID_RET(int, shim_getcpu(&cpu, (unsigned *)ba->addr, NULL));
	}
}

static void bad_getcpu4(stress_bad_addr_t *ba, volatile uint64_t *counter)
{
	if (ba->unwriteable) {
		unsigned int node;
		unsigned int cpu;

		(*counter)++;
		VOID_RET(int, shim_getcpu(&cpu, &node, ba->addr));
	}
}

static void bad_getcwd(stress_bad_addr_t *ba, volatile uint64_t *counter)
{
	if (ba->unwriteable) {
		(*counter)++;
		VOID_RET(char *, getcwd((char *)ba->addr, 1024));
	}
}

#if defined(HAVE_GETDOMAINNAME)
static void bad_getdomainname(stress_bad_addr_t *ba, volatile uint64_t *counter)
{
	if (ba->unwriteable) {
		(*counter)++;
		VOID_RET(int, shim_getdomainname((char *)ba->addr, 8192));
	}
}
#endif

#if defined(HAVE_GETGROUPS)
static void bad_getgroups(stress_bad_addr_t *ba, volatile uint64_t *counter)
{
	if (ba->unwriteable) {
		(*counter)++;
		VOID_RET(int, getgroups(8192, (gid_t *)ba->addr));
	}
}
#endif

#if defined(HAVE_GETHOSTNAME)
static void bad_gethostname(stress_bad_addr_t *ba, volatile uint64_t *counter)
{
	if (ba->unwriteable) {
		(*counter)++;
		VOID_RET(int, gethostname((char *)ba->addr, 8192));
	}
}
#endif

#if defined(HAVE_GETITIMER)
static void bad_getitimer(stress_bad_addr_t *ba, volatile uint64_t *counter)
{
	if (ba->unwriteable) {
		(*counter)++;
		VOID_RET(int, getitimer(ITIMER_PROF, (struct itimerval *)ba->addr));
	}
}
#endif

static void bad_getpeername1(stress_bad_addr_t *ba, volatile uint64_t *counter)
{
	if (ba->unwriteable) {
		(*counter)++;
		VOID_RET(int, getpeername(0, (struct sockaddr *)ba->addr, (socklen_t *)inc_addr(ba->addr, 1)));
	}
}

static void bad_getpeername2(stress_bad_addr_t *ba, volatile uint64_t *counter)
{
	if (ba->unwriteable) {
		struct sockaddr saddr;

		(void)shim_memset(&saddr, 0, sizeof(saddr));
		(*counter)++;
		VOID_RET(int, getpeername(0, &saddr, (socklen_t *)ba->addr));
	}
}

static void bad_getpeername3(stress_bad_addr_t *ba, volatile uint64_t *counter)
{
	if (ba->unwriteable) {
		socklen_t addrlen = sizeof(struct sockaddr);

		(*counter)++;
		VOID_RET(int, getpeername(0, ba->addr, &addrlen));
	}
}

static void bad_get_mempolicy1(stress_bad_addr_t *ba, volatile uint64_t *counter)
{
	(*counter)++;
	VOID_RET(long int, shim_get_mempolicy((int *)ba->addr,
			(unsigned long int *)inc_addr(ba->addr, 1), 1,
			inc_addr(ba->addr, 2), 0UL));
}

static void bad_get_mempolicy2(stress_bad_addr_t *ba, volatile uint64_t *counter)
{
	int mode = 0;

	(*counter)++;
	VOID_RET(long int, shim_get_mempolicy(&mode, (unsigned long int *)ba->addr, 1, ba->addr, 0UL));
}

static void bad_get_mempolicy3(stress_bad_addr_t *ba, volatile uint64_t *counter)
{
	unsigned long int nodemask = 1;

	(*counter)++;
	VOID_RET(long int, shim_get_mempolicy((int *)ba->addr, &nodemask, 1, ba->addr, 0UL));
}


static void bad_getrandom(stress_bad_addr_t *ba, volatile uint64_t *counter)
{
	if (ba->unwriteable) {
		(*counter)++;
		VOID_RET(ssize_t, shim_getrandom((void *)ba->addr, 1024, 0));
	}
}

#if defined(HAVE_GETRESGID)
static void bad_getresgid1(stress_bad_addr_t *ba, volatile uint64_t *counter)
{
	if (ba->unwriteable) {
		(*counter)++;
		VOID_RET(int, getresgid((gid_t *)ba->addr, (gid_t *)inc_addr(ba->addr, 1),
				(gid_t *)inc_addr(ba->addr, 2)));
	}
}

static void bad_getresgid2(stress_bad_addr_t *ba, volatile uint64_t *counter)
{
	if (ba->unwriteable) {
		uid_t egid, sgid;

		(*counter)++;
		VOID_RET(int, getresgid((gid_t *)ba->addr, &egid, &sgid));
	}
}

static void bad_getresgid3(stress_bad_addr_t *ba, volatile uint64_t *counter)
{
	if (ba->unwriteable) {
		gid_t rgid, sgid;

		(*counter)++;
		VOID_RET(int, getresgid(&rgid, (uid_t *)ba->addr, &sgid));
	}
}

static void bad_getresgid4(stress_bad_addr_t *ba, volatile uint64_t *counter)
{
	if (ba->unwriteable) {
		gid_t rgid, egid;

		(*counter)++;
		VOID_RET(int, getresgid(&rgid, &egid, (gid_t *)ba->addr));
	}
}
#endif

#if defined(HAVE_GETRESUID)
static void bad_getresuid1(stress_bad_addr_t *ba, volatile uint64_t *counter)
{
	if (ba->unwriteable) {
		(*counter)++;
		VOID_RET(int, getresuid((uid_t *)ba->addr, (uid_t *)inc_addr(ba->addr, 1),
					(uid_t *)inc_addr(ba->addr, 2)));
	}
}

static void bad_getresuid2(stress_bad_addr_t *ba, volatile uint64_t *counter)
{
	if (ba->unwriteable) {
		uid_t euid, suid;

		(*counter)++;
		VOID_RET(int, getresuid((uid_t *)ba->addr, &euid, &suid));
	}
}

static void bad_getresuid3(stress_bad_addr_t *ba, volatile uint64_t *counter)
{
	if (ba->unwriteable) {
		uid_t ruid, suid;

		(*counter)++;
		VOID_RET(int, getresuid(&ruid, (uid_t *)ba->addr, &suid));
	}
}

static void bad_getresuid4(stress_bad_addr_t *ba, volatile uint64_t *counter)
{
	if (ba->unwriteable) {
		uid_t ruid, euid;

		(*counter)++;
		VOID_RET(int, getresuid(&ruid, &euid, (uid_t *)ba->addr));
	}
}
#endif

static void bad_getrlimit(stress_bad_addr_t *ba, volatile uint64_t *counter)
{
	if (ba->unwriteable) {
		(*counter)++;
		VOID_RET(int, getrlimit(RLIMIT_CPU, (struct rlimit *)ba->addr));
	}
}

#if defined(HAVE_GETRUSAGE) &&	\
    defined(RUSAGE_SELF)
static void bad_getrusage(stress_bad_addr_t *ba, volatile uint64_t *counter)
{
	if (ba->unwriteable) {
		(*counter)++;
		VOID_RET(int, shim_getrusage(RUSAGE_SELF, (struct rusage *)ba->addr));
	}
}
#endif

static void bad_getsockname1(stress_bad_addr_t *ba, volatile uint64_t *counter)
{
	if (ba->unwriteable) {
		(*counter)++;
		VOID_RET(int, getsockname(0, (struct sockaddr *)ba->addr, (socklen_t *)inc_addr(ba->addr, 1)));
	}
}

static void bad_getsockname2(stress_bad_addr_t *ba, volatile uint64_t *counter)
{
	if (ba->unwriteable) {
		struct sockaddr saddr;

		(void)shim_memset(&ba->addr, 0, sizeof(saddr));
		(*counter)++;
		VOID_RET(int, getsockname(0, &saddr, (socklen_t *)ba->addr));
	}
}

static void bad_getsockname3(stress_bad_addr_t *ba, volatile uint64_t *counter)
{
	if (ba->unwriteable) {
		socklen_t socklen = sizeof(struct sockaddr);

		(*counter)++;
		VOID_RET(int, getsockname(0, ba->addr, &socklen));
	}
}

static void bad_gettimeofday1(stress_bad_addr_t *ba, volatile uint64_t *counter)
{
	shim_timezone_t *tz = (shim_timezone_t *)inc_addr(ba->addr, 1);

	(*counter)++;
	VOID_RET(int, shim_gettimeofday((struct timeval *)ba->addr, tz));
}

static void bad_gettimeofday2(stress_bad_addr_t *ba, volatile uint64_t *counter)
{
	struct timeval tv;

	(*counter)++;
	VOID_RET(int, shim_gettimeofday(&tv, (shim_timezone_t *)ba->addr));
}

static void bad_gettimeofday3(stress_bad_addr_t *ba, volatile uint64_t *counter)
{
	if (ba->unwriteable) {
		shim_timezone_t tz;

		(*counter)++;
		VOID_RET(int, shim_gettimeofday((struct timeval *)ba->addr, &tz));
	}
}

#if defined(HAVE_GETXATTR) &&	\
    (defined(HAVE_SYS_XATTR_H) || defined(HAVE_ATTR_XATTR_H))
static void bad_getxattr1(stress_bad_addr_t *ba, volatile uint64_t *counter)
{
	(*counter)++;
	VOID_RET(ssize_t, shim_getxattr((char *)ba->addr, (char *)inc_addr(ba->addr, 1),
				(void *)inc_addr(ba->addr, 2), (size_t)32));
}

static void bad_getxattr2(stress_bad_addr_t *ba, volatile uint64_t *counter)
{
	if (ba->unreadable) {
		char buf[1024];

		(*counter)++;
		VOID_RET(ssize_t, shim_getxattr((char *)ba->addr, "somename", buf, sizeof(buf)));
	}
}

static void bad_getxattr3(stress_bad_addr_t *ba, volatile uint64_t *counter)
{
	if (ba->unreadable) {
		char buf[1024];

		(*counter)++;
		VOID_RET(ssize_t, shim_getxattr(stress_get_temp_path(), (char *)ba->addr, buf, sizeof(buf)));
	}
}

static void bad_getxattr4(stress_bad_addr_t *ba, volatile uint64_t *counter)
{
	if (ba->unwriteable) {
		(*counter)++;
		VOID_RET(ssize_t, shim_getxattr(stress_get_temp_path(), "somename", ba->addr, 1024));
	}
}
#endif

#if defined(TCGETS)
static void bad_ioctl(stress_bad_addr_t *ba, volatile uint64_t *counter)
{
	if (ba->unwriteable) {
		(*counter)++;
		VOID_RET(int, ioctl(0, TCGETS, ba->addr));
	}
}
#else
UNEXPECTED
#endif

static void bad_lchown(stress_bad_addr_t *ba, volatile uint64_t *counter)
{
	if (ba->unreadable) {
		(*counter)++;
		VOID_RET(int, lchown((char *)ba->addr, getuid(), getgid()));
	}
}

static void bad_link1(stress_bad_addr_t *ba, volatile uint64_t *counter)
{
	if (ba->unreadable) {
		(*counter)++;
		VOID_RET(int, link((char *)ba->addr, (char *)inc_addr(ba->addr, 1)));
	}
}

static void bad_link2(stress_bad_addr_t *ba, volatile uint64_t *counter)
{
	if (ba->unreadable) {
		(*counter)++;
		VOID_RET(int, link(stress_get_temp_path(), (char *)ba->addr));
	}
}

static void bad_link3(stress_bad_addr_t *ba, volatile uint64_t *counter)
{
	if (ba->unreadable) {
		(*counter)++;
		VOID_RET(int, link((char *)ba->addr, stress_get_temp_path()));
	}
}

#if defined(HAVE_LGETXATTR) &&	\
    (defined(HAVE_SYS_XATTR_H) || defined(HAVE_ATTR_XATTR_H))
static void bad_lgetxattr1(stress_bad_addr_t *ba, volatile uint64_t *counter)
{
	(*counter)++;
	VOID_RET(ssize_t, shim_lgetxattr((char *)ba->addr, (char *)inc_addr(ba->addr, 1),
					(void *)inc_addr(ba->addr, 2), (size_t)32));
}

static void bad_lgetxattr2(stress_bad_addr_t *ba, volatile uint64_t *counter)
{
	if (ba->unreadable) {
		char buf[1024];

		(*counter)++;
		VOID_RET(ssize_t, shim_lgetxattr((char *)ba->addr, "somename", buf, sizeof(buf)));
	}
}

static void bad_lgetxattr3(stress_bad_addr_t *ba, volatile uint64_t *counter)
{
	if (ba->unreadable) {
		char buf[1024];

		(*counter)++;
		VOID_RET(ssize_t, shim_lgetxattr(stress_get_temp_path(), (char *)ba->addr, buf, sizeof(buf)));
	}
}

static void bad_lgetxattr4(stress_bad_addr_t *ba, volatile uint64_t *counter)
{
	if (ba->unwriteable) {
		(*counter)++;
		VOID_RET(ssize_t, shim_lgetxattr(stress_get_temp_path(), "somename", ba->addr, 1024));
	}
}
#endif

#if defined(HAVE_LISTXATTR) &&	\
    (defined(HAVE_SYS_XATTR_H) || defined(HAVE_ATTR_XATTR_H))
static void bad_listxattr1(stress_bad_addr_t *ba, volatile uint64_t *counter)
{
	(*counter)++;
	VOID_RET(ssize_t, shim_listxattr((char *)ba->addr, (char *)inc_addr(ba->addr, 1), 1024));
}

static void bad_listxattr2(stress_bad_addr_t *ba, volatile uint64_t *counter)
{
	if (ba->unreadable) {
		char list[4096];

		(*counter)++;
		VOID_RET(ssize_t, shim_listxattr((char *)ba->addr, list, sizeof(list)));
	}
}

static void bad_listxattr3(stress_bad_addr_t *ba, volatile uint64_t *counter)
{
	if (ba->unwriteable) {
		(*counter)++;
		VOID_RET(ssize_t, shim_listxattr(stress_get_temp_path(), (char *)ba->addr, 4096));
	}
}
#endif

#if defined(HAVE_LLISTXATTR) &&	\
    (defined(HAVE_SYS_XATTR_H) || defined(HAVE_ATTR_XATTR_H))
static void bad_llistxattr1(stress_bad_addr_t *ba, volatile uint64_t *counter)
{
	(*counter)++;
	VOID_RET(ssize_t, shim_llistxattr((char *)ba->addr, (char *)inc_addr(ba->addr, 1), 1024));
}

static void bad_llistxattr2(stress_bad_addr_t *ba, volatile uint64_t *counter)
{
	if (ba->unreadable) {
		char list[4096];

		(*counter)++;
		VOID_RET(ssize_t, shim_llistxattr((char *)ba->addr, list, sizeof(list)));
	}
}

static void bad_llistxattr3(stress_bad_addr_t *ba, volatile uint64_t *counter)
{
	if (ba->unwriteable) {
		(*counter)++;
		VOID_RET(ssize_t, shim_llistxattr(stress_get_temp_path(), (char *)ba->addr, 4096));
	}
}
#endif

#if defined(HAVE_LREMOVEXATTR) &&	\
    (defined(HAVE_SYS_XATTR_H) || defined(HAVE_ATTR_XATTR_H))
static void bad_lremovexattr1(stress_bad_addr_t *ba, volatile uint64_t *counter)
{
	if (ba->unreadable) {
		(*counter)++;
		VOID_RET(int, shim_lremovexattr((char *)ba->addr, (char *)inc_addr(ba->addr, 1)));
	}
}

static void bad_lremovexattr2(stress_bad_addr_t *ba, volatile uint64_t *counter)
{
	if (ba->unreadable) {
		(*counter)++;
		VOID_RET(int, shim_lremovexattr((char *)ba->addr, "nameval"));
	}
}

static void bad_lremovexattr3(stress_bad_addr_t *ba, volatile uint64_t *counter)
{
	if (ba->unreadable) {
		(*counter)++;
		VOID_RET(int, shim_lremovexattr(stress_get_temp_path(), (char *)ba->addr));
	}
}
#endif

#if defined(__NR_lsm_get_self_attr)
static void bad_lsm_get_self_attr1(stress_bad_addr_t *ba, volatile uint64_t *counter)
{
	if (ba->unwriteable) {
		size_t size = 1024;

		(*counter)++;
		VOID_RET(int, syscall(__NR_lsm_get_self_attr, 0, ba->addr, &size, 0));
	}
}

static void bad_lsm_get_self_attr2(stress_bad_addr_t *ba, volatile uint64_t *counter)
{
	if (ba->unwriteable) {
		unsigned char ctxt[1024];

		(*counter)++;
		VOID_RET(int, syscall(__NR_lsm_get_self_attr, 0, ctxt, ba->addr, 0));
	}
}
#endif

#if defined(__NR_lsm_set_self_attr)
static void bad_lsm_set_self_attr(stress_bad_addr_t *ba, volatile uint64_t *counter)
{
	if (ba->unreadable) {
		(*counter)++;
		VOID_RET(int, syscall(__NR_lsm_set_self_attr, 0, ba->addr, 1024, 0));
	}
}
#endif

#if defined(__NR_lsm_list_modules)
static void bad_lsm_list_modules1(stress_bad_addr_t *ba, volatile uint64_t *counter)
{
	if (ba->unreadable) {
		size_t size = 64;

		(*counter)++;
		VOID_RET(int, syscall(__NR_lsm_list_modules, (uint64_t *)ba->addr, &size, 0));
	}
}

static void bad_lsm_list_modules2(stress_bad_addr_t *ba, volatile uint64_t *counter)
{
	if (ba->unreadable) {
		uint64_t ids[64];

		(*counter)++;
		VOID_RET(int, syscall(__NR_lsm_list_modules, ids, (size_t *)ba->addr, 0));
	}
}
#endif

static void bad_lstat1(stress_bad_addr_t *ba, volatile uint64_t *counter)
{
	(*counter)++;
	VOID_RET(int, shim_lstat((const char *)ba->addr, (struct stat *)inc_addr(ba->addr, 1)));
}

static void bad_lstat2(stress_bad_addr_t *ba, volatile uint64_t *counter)
{
	if (ba->unreadable) {
		struct stat statbuf;

		(*counter)++;
		VOID_RET(int, shim_lstat(ba->addr, &statbuf));
	}
}

static void bad_lstat3(stress_bad_addr_t *ba, volatile uint64_t *counter)
{
	if (ba->unwriteable) {
		(*counter)++;
		VOID_RET(int, shim_lstat(stress_get_temp_path(), (struct stat *)ba->addr));
	}
}

#if defined(HAVE_MADVISE)
static void bad_madvise(stress_bad_addr_t *ba, volatile uint64_t *counter)
{
	(*counter)++;
	VOID_RET(int, shim_madvise((void *)ba->addr, 8192, SHIM_MADV_NORMAL));
}
#else
UNEXPECTED
#endif

#if defined(HAVE_MEMFD_CREATE)
static void bad_memfd_create(stress_bad_addr_t *ba, volatile uint64_t *counter)
{
	if (ba->unreadable) {
		int fd;

		(*counter)++;
		fd = shim_memfd_create(ba->addr, 0);
		if (fd >= 0)
			VOID_RET(int, close(fd));
	}
}
#endif

static void bad_migrate_pages1(stress_bad_addr_t *ba, volatile uint64_t *counter)
{
	if (ba->unreadable) {
		(*counter)++;
		VOID_RET(long int, shim_migrate_pages(getpid(), 1, (unsigned long int *)ba->addr,
						(unsigned long int *)inc_addr(ba->addr, 1)));
	}
}

static void bad_migrate_pages2(stress_bad_addr_t *ba, volatile uint64_t *counter)
{
	if (ba->unreadable) {
		unsigned long int nodes = 0;

		(*counter)++;
		VOID_RET(long int, shim_migrate_pages(getpid(), 1, &nodes, (unsigned long int *)ba->addr));
	}
}

static void bad_migrate_pages3(stress_bad_addr_t *ba, volatile uint64_t *counter)
{
	if (ba->unreadable) {
		unsigned long int nodes = 0;

		(*counter)++;
		VOID_RET(long int, shim_migrate_pages(getpid(), 1, (unsigned long int *)ba->addr, &nodes));
	}
}

static void bad_mincore(stress_bad_addr_t *ba, volatile uint64_t *counter)
{
	if (ba->unwriteable) {
		(*counter)++;
		VOID_RET(int, shim_mincore((void *)ro_page, 1, (unsigned char *)ba->addr));
	}
}

#if defined(HAVE_MLOCK)
static void bad_mlock(stress_bad_addr_t *ba, volatile uint64_t *counter)
{
	(*counter)++;
	VOID_RET(int, shim_mlock((void *)ba->addr, 4096));
}
#else
UNEXPECTED
#endif

#if defined(HAVE_MLOCK2)
static void bad_mlock2(stress_bad_addr_t *ba, volatile uint64_t *counter)
{
	(*counter)++;
	VOID_RET(int, shim_mlock2((void *)ba->addr, 4096, 0));
}
#endif

#if defined(__NR_move_pages)
static void bad_move_pages1(stress_bad_addr_t *ba, volatile uint64_t *counter)
{
	(*counter)++;
	VOID_RET(long int, shim_move_pages(getpid(), (unsigned long int)1, (void **)ba->addr,
					(const int *)inc_addr(ba->addr, 1), (int *)inc_addr(ba->addr, 2), 0));
}

static void bad_move_pages2(stress_bad_addr_t *ba, volatile uint64_t *counter)
{
	if (ba->unreadable) {
		int nodes = 0, status;

		(*counter)++;
		VOID_RET(long int, shim_move_pages(getpid(), (unsigned long int)1, (void **)ba->addr, &nodes, &status, 0));
	}
}

static void bad_move_pages3(stress_bad_addr_t *ba, volatile uint64_t *counter)
{
	if (ba->unreadable) {
		int status = 0;
		void *pages[1];

		pages[0] = ba->addr;
		(*counter)++;
		VOID_RET(long int, shim_move_pages(getpid(), (unsigned long int)1, pages, (int *)ba->addr, &status, 0));
	}
}

static void bad_move_pages4(stress_bad_addr_t *ba, volatile uint64_t *counter)
{
	if (ba->unwriteable) {
		int nodes = 0;
		void *pages[1];

		pages[0] = ba->addr;
		(*counter)++;
		VOID_RET(long int, shim_move_pages(getpid(), (unsigned long int)1, pages, &nodes, (int *)ba->addr, 0));
	}
}
#endif

#if defined(__NR_seal)
static void bad_mseal(stress_bad_addr_t *ba, volatile uint64_t *counter)
{
	(*counter)++;
	VOID_RET(long int, shim_mseal((void **)ba->addr, 4096, 0));
}
#endif

#if defined(HAVE_MLOCK)
static void bad_munlock(stress_bad_addr_t *ba, volatile uint64_t *counter)
{
	(*counter)++;
	VOID_RET(int, shim_munlock((void *)ba->addr, 4096));
}

#endif
/*
#if defined(HAVE_MPROTECT)
static void bad_mprotect(stress_bad_addr_t *ba, volatile uint64_t *counter)
{
	(*counter)++;
	VOID_RET(int, mprotect((void *)ba->addr, 4096, PROT_READ | PROT_WRITE));
}
#endif
*/

#if defined(HAVE_MSYNC)
static void bad_msync(stress_bad_addr_t *ba, volatile uint64_t *counter)
{
	(*counter)++;
	VOID_RET(int, shim_msync((void *)ba->addr, 4096, MS_SYNC));
}
#else
UNEXPECTED
#endif

#if defined(HAVE_NANOSLEEP)
static void bad_nanosleep1(stress_bad_addr_t *ba, volatile uint64_t *counter)
{
	(*counter)++;
	VOID_RET(int, nanosleep((struct timespec *)ba->addr, (struct timespec *)inc_addr(ba->addr, 1)));
}

static void bad_nanosleep2(stress_bad_addr_t *ba, volatile uint64_t *counter)
{
	if (ba->unreadable) {
		struct timespec rem;

		(*counter)++;
		VOID_RET(int, nanosleep((struct timespec *)ba->addr, &rem));
	}
}

static void bad_nanosleep3(stress_bad_addr_t *ba, volatile uint64_t *counter)
{
	struct timespec req;

	req.tv_sec = 0;
	req.tv_nsec = 0;
	(*counter)++;
	VOID_RET(int, nanosleep(&req, (struct timespec *)ba->addr));
}
#else
UNEXPECTED
#endif

static void bad_open(stress_bad_addr_t *ba, volatile uint64_t *counter)
{
	if (ba->unreadable) {
		int fd;

		(*counter)++;
		fd = open((char *)ba->addr, O_RDONLY);
		if (fd >= 0)
			VOID_RET(int, close(fd));
	}
}

static void bad_pipe(stress_bad_addr_t *ba, volatile uint64_t *counter)
{
	if (ba->unwriteable) {
		int *fds = (int *)ba->addr;

		(*counter)++;
		if (pipe(fds) == 0) {
			/* Should not get here */
			(void)close(fds[0]);
			(void)close(fds[1]);
		}
	}
}

#if defined(HAVE_PREAD)
static void bad_pread(stress_bad_addr_t *ba, volatile uint64_t *counter)
{
	if (ba->unwriteable) {
		int fd;

		fd = open("/dev/zero", O_RDONLY);
		if (fd > -1) {
			(*counter)++;
			VOID_RET(ssize_t, pread(fd, ba->addr, 1024, 0));
			VOID_RET(int, close(fd));
		}
	}
}
#endif

#if defined(HAVE_PREADV)
static void bad_preadv(stress_bad_addr_t *ba, volatile uint64_t *counter)
{
	if (ba->unwriteable) {
		int fd;

		fd = open("/dev/zero", O_RDONLY);
		if (fd > -1) {
			(*counter)++;
			VOID_RET(ssize_t, preadv(fd, (struct iovec *)ba->addr, 1, 0));
			VOID_RET(int, close(fd));
		}
	}
}
#endif

#if defined(HAVE_PREADV2)
static void bad_preadv2(stress_bad_addr_t *ba, volatile uint64_t *counter)
{
	if (ba->unwriteable) {
		int fd;

		fd = open("/dev/zero", O_RDONLY);
		if (fd > -1) {
			(*counter)++;
			VOID_RET(ssize_t, preadv2(fd, (struct iovec *)ba->addr, 1, 0, 0));
			VOID_RET(int, close(fd));
		}
	}
}
#endif

#if defined(HAVE_PTRACE) && 	\
    defined(PTRACE_GETREGS)
static void bad_ptrace(stress_bad_addr_t *ba, volatile uint64_t *counter)
{
	(*counter)++;
	VOID_RET(long int, ptrace(PTRACE_GETREGS, getpid(), (void *)ba->addr, (void *)inc_addr(ba->addr, 1)));
}
#endif

#if defined(HAVE_POLL_H) &&	\
    defined(HAVE_POLL)
static void bad_poll(stress_bad_addr_t *ba, volatile uint64_t *counter)
{
	(*counter)++;
	VOID_RET(int, poll((struct pollfd *)ba->addr, (nfds_t)16, (int)1));
}
#else
UNEXPECTED
#endif

#if defined(HAVE_POLL_H) &&	\
    defined(HAVE_PPOLL)
static void bad_ppoll1(stress_bad_addr_t *ba, volatile uint64_t *counter)
{
	void *addr = (char *)ba->addr;
	const struct timespec *ts = (struct timespec *)inc_addr(addr, sizeof(struct pollfd));
	const sigset_t *ss = (sigset_t *)(inc_addr(addr, sizeof(struct pollfd) + sizeof(struct timespec)));

	(*counter)++;
	VOID_RET(int, shim_ppoll((struct pollfd *)addr, (nfds_t)1, ts, ss));
}

static void bad_ppoll2(stress_bad_addr_t *ba, volatile uint64_t *counter)
{
	struct timespec ts;
	sigset_t sigmask;

	(void)sigemptyset(&sigmask);
	ts.tv_sec = 0;
	ts.tv_nsec = 0;

	(*counter)++;
	VOID_RET(int, shim_ppoll((struct pollfd *)ba->addr, (nfds_t)16, &ts, &sigmask));
}

static void bad_ppoll3(stress_bad_addr_t *ba, volatile uint64_t *counter)
{
	if (ba->unreadable) {
		sigset_t sigmask;
		struct pollfd pfd;

		pfd.fd = fileno(stdin);
		pfd.events = POLLIN;
		pfd.revents = 0;
		(void)sigemptyset(&sigmask);

		(*counter)++;
		VOID_RET(int, shim_ppoll(&pfd, (nfds_t)1, (struct timespec *)ba->addr, &sigmask));
	}
}

static void bad_ppoll4(stress_bad_addr_t *ba, volatile uint64_t *counter)
{
	if (ba->unreadable) {
		struct timespec ts;
		struct pollfd pfd;

		pfd.fd = fileno(stdin);
		pfd.events = POLLIN;
		pfd.revents = 0;
		ts.tv_sec = 0;
		ts.tv_nsec = 0;

		(*counter)++;
		VOID_RET(int, shim_ppoll(&pfd, (nfds_t)1, &ts, (sigset_t *)ba->addr));
	}
}
#endif

#if defined(HAVE_PWRITE)
static void bad_pwrite(stress_bad_addr_t *ba, volatile uint64_t *counter)
{
	if (ba->unreadable) {
		int fd;

		fd = open("/dev/null", O_WRONLY);
		if (fd > -1) {
			(*counter)++;
			VOID_RET(ssize_t, pwrite(fd, ba->addr, 1024, 0));
			VOID_RET(int, close(fd));
		}
	}
}
#endif

#if defined(HAVE_PWRITEV)
static void bad_pwritev(stress_bad_addr_t *ba, volatile uint64_t *counter)
{
	if (ba->unreadable) {
		int fd;

		fd = open("/dev/null", O_WRONLY);
		if (fd > -1) {
			(*counter)++;
			VOID_RET(ssize_t, pwritev(fd, (struct iovec *)ba->addr, 1, 0));
			VOID_RET(int, close(fd));
		}
	}
}
#endif

#if defined(HAVE_PWRITEV2)
static void bad_pwritev2(stress_bad_addr_t *ba, volatile uint64_t *counter)
{
	if (ba->unreadable) {
		int fd;

		fd = open("/dev/null", O_WRONLY);
		if (fd > -1) {
			(*counter)++;
			VOID_RET(ssize_t, pwritev2(fd, (struct iovec *)ba->addr, 1, 0, 0));
			VOID_RET(int, close(fd));
		}
	}
}
#endif

static void bad_read(stress_bad_addr_t *ba, volatile uint64_t *counter)
{
	if (ba->unwriteable) {
		int fd;

		fd = open("/dev/zero", O_RDONLY);
		if (fd > -1) {
			(*counter)++;
			VOID_RET(ssize_t, read(fd, ba->addr, 1024));
			VOID_RET(int, close(fd));
		}
	}
}

static void bad_readlink1(stress_bad_addr_t *ba, volatile uint64_t *counter)
{
	(*counter)++;
	VOID_RET(ssize_t, shim_readlink((const char *)ba->addr, (char *)inc_addr(ba->addr, 1), 8192));
}

static void bad_readlink2(stress_bad_addr_t *ba, volatile uint64_t *counter)
{
	if (ba->unwriteable) {
		(*counter)++;
		VOID_RET(ssize_t, shim_readlink(stress_get_temp_path(), (char *)ba->addr, 8192));
	}
}

static void bad_readlink3(stress_bad_addr_t *ba, volatile uint64_t *counter)
{
	if (ba->unreadable) {
		char buf[PATH_MAX];

		(*counter)++;
		VOID_RET(ssize_t, shim_readlink((const char *)ba->addr, (char *)buf, PATH_MAX));
	}
}

#if defined(HAVE_READV)
static void bad_readv(stress_bad_addr_t *ba, volatile uint64_t *counter)
{
	if (ba->unwriteable) {
		int fd;

		fd = open("/dev/zero", O_RDONLY);
		if (fd > -1) {
			(*counter)++;
			VOID_RET(ssize_t, readv(fd, ba->addr, 32));
			VOID_RET(int, close(fd));
		}
	}
}
#endif

#if defined(HAVE_REMOVEXATTR) &&	\
    (defined(HAVE_SYS_XATTR_H) || defined(HAVE_ATTR_XATTR_H))
static void bad_removexattr1(stress_bad_addr_t *ba, volatile uint64_t *counter)
{
	(*counter)++;
	VOID_RET(int, shim_removexattr((char *)ba->addr, (char *)inc_addr(ba->addr, 1)));
}

static void bad_removexattr2(stress_bad_addr_t *ba, volatile uint64_t *counter)
{
	if (ba->unreadable) {
		(*counter)++;
		VOID_RET(int, shim_removexattr((char *)ba->addr, "nameval"));
	}
}

static void bad_removexattr3(stress_bad_addr_t *ba, volatile uint64_t *counter)
{
	if (ba->unreadable) {
		(*counter)++;
		VOID_RET(int, shim_removexattr(stress_get_temp_path(), (char *)ba->addr));
	}
}
#endif

static void bad_rename1(stress_bad_addr_t *ba, volatile uint64_t *counter)
{
	if (ba->unreadable) {
		(*counter)++;
		VOID_RET(int, rename((char *)ba->addr, (char *)inc_addr(ba->addr, 1)));
	}
}

static void bad_rename2(stress_bad_addr_t *ba, volatile uint64_t *counter)
{
	if (ba->unreadable) {
		(*counter)++;
		VOID_RET(int, shim_removexattr(stress_get_temp_path(), (char *)ba->addr));
	}
}

#if defined(HAVE_SCHED_GETAFFINITY)
static void bad_sched_getaffinity(stress_bad_addr_t *ba, volatile uint64_t *counter)
{
	if (ba->unwriteable) {
		(*counter)++;
		VOID_RET(int, sched_getaffinity(getpid(), (size_t)8192, (cpu_set_t *)ba->addr));
	}
}
#else
UNEXPECTED
#endif

#if defined(HAVE_SELECT)
static void bad_select1(stress_bad_addr_t *ba, volatile uint64_t *counter)
{
	int fd;
	fd_set *readfds = ba->addr;
	fd_set *writefds = readfds + 1;
	fd_set *exceptfds = writefds + 1;

	fd = open("/dev/zero", O_RDONLY);
	if (fd > -1) {
		(*counter)++;
		VOID_RET(int, select(fd, readfds, writefds, exceptfds, (struct timeval *)inc_addr(ba->addr, 4)));
		VOID_RET(int, close(fd));
	}
}

static void bad_select2(stress_bad_addr_t *ba, volatile uint64_t *counter)
{
	if (ba->unreadable) {
		int fd;
		fd_set readfds;
		fd_set writefds;
		fd_set exceptfds;

		FD_ZERO(&readfds);
		FD_ZERO(&writefds);
		FD_ZERO(&exceptfds);

		fd = open("/dev/zero", O_RDONLY);
		if (fd > -1) {
			(*counter)++;
			VOID_RET(int, select(fd, &readfds, &writefds, &exceptfds, (struct timeval *)ba->addr));
			VOID_RET(int, close(fd));
		}
	}
}

static void bad_select3(stress_bad_addr_t *ba, volatile uint64_t *counter)
{
	int fd;
	fd_set writefds;
	fd_set exceptfds;
	struct timeval tv;

	tv.tv_sec = 0;
	tv.tv_usec = 0;

	FD_ZERO(&writefds);
	FD_ZERO(&exceptfds);

	fd = open("/dev/zero", O_RDONLY);
	if (fd > -1) {
		(*counter)++;
		VOID_RET(int, select(fd, ba->addr, &writefds, &exceptfds, &tv));
		VOID_RET(int, close(fd));
	}
}

static void bad_select4(stress_bad_addr_t *ba, volatile uint64_t *counter)
{
	int fd;
	fd_set readfds;
	fd_set exceptfds;
	struct timeval tv;

	tv.tv_sec = 0;
	tv.tv_usec = 0;

	FD_ZERO(&readfds);
	FD_ZERO(&exceptfds);

	fd = open("/dev/zero", O_RDONLY);
	if (fd > -1) {
		(*counter)++;
		VOID_RET(int, select(fd, &readfds, ba->addr, &exceptfds, &tv));
		VOID_RET(int, close(fd));
	}
}

static void bad_select5(stress_bad_addr_t *ba, volatile uint64_t *counter)
{
	int fd;
	fd_set readfds;
	fd_set writefds;
	struct timeval tv;

	tv.tv_sec = 0;
	tv.tv_usec = 0;

	FD_ZERO(&readfds);
	FD_ZERO(&writefds);

	fd = open("/dev/zero", O_RDONLY);
	if (fd > -1) {
		(*counter)++;
		VOID_RET(int, select(fd, &readfds, &writefds, ba->addr, &tv));
		VOID_RET(int, close(fd));
	}
}

#endif

#if defined(HAVE_SETITIMER)
static void bad_setitimer1(stress_bad_addr_t *ba, volatile uint64_t *counter)
{
	if (ba->unreadable) {
		(*counter)++;
		VOID_RET(int, setitimer(ITIMER_PROF, (struct itimerval *)ba->addr,
					(struct itimerval *)inc_addr(ba->addr, 1)));
	}
}

static void bad_setitimer2(stress_bad_addr_t *ba, volatile uint64_t *counter)
{
	if (ba->unreadable) {
		struct itimerval oldval;

		(*counter)++;
		VOID_RET(int, setitimer(ITIMER_PROF, (struct itimerval *)ba->addr, &oldval));
	}
}

static void bad_setitimer3(stress_bad_addr_t *ba, volatile uint64_t *counter)
{
	if (ba->unwriteable) {
		struct itimerval newval;

		(void)shim_memset(&newval, 0, sizeof(newval));
		(*counter)++;
		VOID_RET(int, setitimer(ITIMER_PROF, &newval, (struct itimerval *)ba->addr));
	}
}
#endif

static void bad_setrlimit(stress_bad_addr_t *ba, volatile uint64_t *counter)
{
	if (ba->unreadable) {
		(*counter)++;
		VOID_RET(int, setrlimit(RLIMIT_CPU, (struct rlimit *)ba->addr));
	}
}

static void bad_stat1(stress_bad_addr_t *ba, volatile uint64_t *counter)
{
	(*counter)++;
	VOID_RET(int, shim_stat((char *)ba->addr, (struct stat *)inc_addr(ba->addr, 1)));
}

static void bad_stat2(stress_bad_addr_t *ba, volatile uint64_t *counter)
{
	if (ba->unwriteable) {
		(*counter)++;
		VOID_RET(int, shim_stat(stress_get_temp_path(), (struct stat *)ba->addr));
	}
}

static void bad_stat3(stress_bad_addr_t *ba, volatile uint64_t *counter)
{
	if (ba->unreadable) {
		struct stat statbuf;

		(*counter)++;
		VOID_RET(int, shim_stat((char *)ba->addr, &statbuf));
	}
}

#if defined(HAVE_STATFS)
static void bad_statfs(stress_bad_addr_t *ba, volatile uint64_t *counter)
{
	if (ba->unwriteable) {
		(*counter)++;
		VOID_RET(int, statfs(".", (struct statfs *)ba->addr));
	}
}
#else
UNEXPECTED
#endif

#if defined(HAVE_SYS_SYSINFO_H) && 	\
    defined(HAVE_SYSINFO)
static void bad_sysinfo(stress_bad_addr_t *ba, volatile uint64_t *counter)
{
	if (ba->unwriteable) {
		(*counter)++;
		VOID_RET(int, sysinfo((struct sysinfo *)ba->addr));
	}
}
#else
UNEXPECTED
#endif

static void bad_time(stress_bad_addr_t *ba, volatile uint64_t *counter)
{
	if (ba->unwriteable) {
		(*counter)++;
		if (stress_mwc1())
			VOID_RET(time_t, time((time_t *)ba->addr));
		else
			VOID_RET(time_t, shim_time((time_t *)ba->addr));
	}
}

#if defined(HAVE_LIB_RT) &&	\
    defined(HAVE_TIMER_CREATE)
static void bad_timer_create(stress_bad_addr_t *ba, volatile uint64_t *counter)
{
	if (ba->unreadable) {
		timer_t *timerid = (timer_t *)ba->addr;

		timerid++;
		(*counter)++;
		VOID_RET(int, timer_create(CLOCK_MONOTONIC, (struct sigevent *)ba->addr, timerid));
	}
}
#else
UNEXPECTED
#endif

static void bad_times(stress_bad_addr_t *ba, volatile uint64_t *counter)
{
	if (ba->unwriteable) {
		(*counter)++;
		VOID_RET(clock_t, times((struct tms *)ba->addr));
	}
}

static void bad_truncate(stress_bad_addr_t *ba, volatile uint64_t *counter)
{
	if (ba->unreadable) {
		(*counter)++;
		VOID_RET(int, truncate((char *)ba->addr, 8192));
	}
}

#if defined(HAVE_UNAME) &&	\
    defined(HAVE_SYS_UTSNAME_H)
static void bad_uname(stress_bad_addr_t *ba, volatile uint64_t *counter)
{
	if (ba->unwriteable) {
		(*counter)++;
		VOID_RET(int, uname((struct utsname *)ba->addr));
	}
}
#else
UNEXPECTED
#endif

static void bad_ustat(stress_bad_addr_t *ba, volatile uint64_t *counter)
{
	if (ba->unwriteable) {
		dev_t dev = { 0 };

		(*counter)++;
		VOID_RET(int, shim_ustat(dev, (struct shim_ustat *)ba->addr));
	}
}

#if defined(HAVE_UTIME_H)
static void bad_utime(stress_bad_addr_t *ba, volatile uint64_t *counter)
{
	if (ba->unreadable) {
		(*counter)++;
		VOID_RET(int, utime(ba->addr, (struct utimbuf *)ba->addr));
	}
}
#endif

#if defined(HAVE_UTIMES)
static void bad_utimes1(stress_bad_addr_t *ba, volatile uint64_t *counter)
{
	if (ba->unreadable) {
		(*counter)++;
		VOID_RET(int, utimes(ba->addr, (const struct timeval *)inc_addr(ba->addr, 1)));
	}
}
#endif

#if defined(HAVE_UTIMES)
static void bad_utimes2(stress_bad_addr_t *ba, volatile uint64_t *counter)
{
	if (ba->unreadable) {
		(*counter)++;
		VOID_RET(int, utimes(stress_get_temp_path(), (const struct timeval *)ba->addr));
	}
}
#endif

#if defined(HAVE_UTIMES)
static void bad_utimes3(stress_bad_addr_t *ba, volatile uint64_t *counter)
{
	if (ba->unreadable) {
		(*counter)++;
		VOID_RET(int, utimes(ba->addr, NULL));
	}
}
#endif

static void bad_wait(stress_bad_addr_t *ba, volatile uint64_t *counter)
{
	if (ba->unwriteable) {
		(*counter)++;
		VOID_RET(pid_t, wait((int *)ba->addr));
	}
}

static void bad_waitpid(stress_bad_addr_t *ba, volatile uint64_t *counter)
{
	if (ba->unwriteable) {
		(*counter)++;
		VOID_RET(pid_t, waitpid(getpid(), (int *)ba->addr, (int)0));
	}
}

#if defined(HAVE_WAITID)
static void bad_waitid(stress_bad_addr_t *ba, volatile uint64_t *counter)
{
	if (ba->unwriteable) {
		(*counter)++;
		VOID_RET(int, waitid(P_PID, (id_t)getpid(), (siginfo_t *)ba->addr, 0));
	}
}
#else
UNEXPECTED
#endif

static void bad_write(stress_bad_addr_t *ba, volatile uint64_t *counter)
{
	if (ba->unreadable) {
		int fd;

		fd = open("/dev/null", O_WRONLY);
		if (fd > -1) {
			(*counter)++;
			VOID_RET(ssize_t, write(fd, (void *)ba->addr, 1024));
			VOID_RET(int, close(fd));
		}
	}
}

#if defined(HAVE_WRITEV)
static void bad_writev(stress_bad_addr_t *ba, volatile uint64_t *counter)
{
	if (ba->unreadable) {
		int fd;

		fd = open("/dev/zero", O_RDONLY);
		if (fd > -1) {
			(*counter)++;
			VOID_RET(ssize_t, writev(fd, (void *)ba->addr, 32));
			VOID_RET(int, close(fd));
		}
	}
}
#else
UNEXPECTED
#endif

static const stress_bad_syscall_t bad_syscalls[] = {
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
#if defined(HAVE_FLISTXATTR) &&	\
    (defined(HAVE_SYS_XATTR_H) || defined(HAVE_ATTR_XATTR_H))
	bad_flistxattr,
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
#if defined(HAVE_GETGROUPS)
	bad_getgroups,
#endif
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
#if defined(HAVE_LGETXATTR) &&	\
    (defined(HAVE_SYS_XATTR_H) || defined(HAVE_ATTR_XATTR_H))
	bad_lgetxattr1,
	bad_lgetxattr2,
	bad_lgetxattr3,
	bad_lgetxattr4,
#endif
#if defined(HAVE_LISTXATTR) &&	\
    (defined(HAVE_SYS_XATTR_H) || defined(HAVE_ATTR_XATTR_H))
	bad_listxattr1,
	bad_listxattr2,
	bad_listxattr3,
#endif
#if defined(HAVE_LLISTXATTR) &&	\
    (defined(HAVE_SYS_XATTR_H) || defined(HAVE_ATTR_XATTR_H))
	bad_llistxattr1,
	bad_llistxattr2,
	bad_llistxattr3,
#endif
#if defined(HAVE_LREMOVEXATTR) &&	\
    (defined(HAVE_SYS_XATTR_H) || defined(HAVE_ATTR_XATTR_H))
	bad_lremovexattr1,
	bad_lremovexattr2,
	bad_lremovexattr3,
#endif
#if defined(__NR_lsm_get_self_attr)
	bad_lsm_get_self_attr1,
	bad_lsm_get_self_attr2,
#endif
#if defined(__NR_lsm_set_self_attr)
	bad_lsm_set_self_attr,
#endif
#if defined(__NR_lsm_list_modules)
	bad_lsm_list_modules1,
	bad_lsm_list_modules2,
#endif
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
#if defined(__NR_seal)
	bad_mseal,
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
#if defined(HAVE_POLL_H) &&	\
    defined(HAVE_POLL)
	bad_poll,
#endif
#if defined(HAVE_POLL_H) &&	\
    defined(HAVE_PPOLL)
	bad_ppoll1,
	bad_ppoll2,
	bad_ppoll3,
	bad_ppoll4,
#endif
#if defined(HAVE_PREAD)
	bad_pread,
#endif
#if defined(HAVE_PREADV)
	bad_preadv,
#endif
#if defined(HAVE_PREADV2)
	bad_preadv2,
#endif
#if defined(HAVE_PTRACE) &&	\
    defined(PTRACE_GETREGS)
	bad_ptrace,
#endif
#if defined(HAVE_PWRITE)
	bad_pwrite,
#endif
#if defined(HAVE_PWRITEV)
	bad_pwritev,
#endif
#if defined(HAVE_PWRITEV2)
	bad_pwritev2,
#endif
	bad_read,
	bad_readlink1,
	bad_readlink2,
	bad_readlink3,
#if defined(HAVE_READV)
	bad_readv,
#endif
#if defined(HAVE_REMOVEXATTR) &&	\
    (defined(HAVE_SYS_XATTR_H) || defined(HAVE_ATTR_XATTR_H))
	bad_removexattr1,
	bad_removexattr2,
	bad_removexattr3,
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
#if defined(HAVE_UTIMES)
	bad_utimes1,
#endif
#if defined(HAVE_UTIMES)
	bad_utimes2,
#endif
#if defined(HAVE_UTIMES)
	bad_utimes3,
#endif
	bad_wait,
	bad_waitpid,
#if defined(HAVE_WAITID)
	bad_waitid,
#endif
	bad_write,
#if defined(HAVE_WRITEV)
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

	if (UNLIKELY(!stress_continue(args)))
		return 0;
	pid = fork();
	if (pid < 0) {
		_exit(EXIT_NO_RESOURCE);
	} else if (pid == 0) {
#if defined(HAVE_SETITIMER)
		struct itimerval it;
#endif
		size_t k;

		stress_set_proc_state(args->name, STRESS_STATE_RUN);

		for (k = 0; k < SIZEOF_ARRAY(sigs); k++) {
			if (stress_sighandler(args->name, sigs[k], stress_sig_handler_exit, NULL) < 0)
				_exit(EXIT_FAILURE);
		}

		/* Try to limit child from spawning */
		limit_procs(2);

		/* We don't want bad ops clobbering this region */
		stress_shared_readonly();

		/* Drop all capabilities */
		if (stress_drop_capabilities(args->name) < 0) {
			_exit(EXIT_NO_RESOURCE);
		}

		stress_parent_died_alarm();
		(void)sched_settings_apply(true);

		state->counter = stress_bogo_get(args);
		while (state->syscall_index < SIZEOF_ARRAY(bad_syscalls)) {
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
			while (state->addr_index < SIZEOF_ARRAY(bad_addrs)) {
				stress_bad_addr_t *ba = &bad_addrs[state->addr_index];

				const stress_bad_syscall_t bad_syscall = bad_syscalls[state->syscall_index];

				if (ba->addr) {
					if ((state->max_ops) && (state->counter >= state->max_ops))
						_exit(EXIT_SUCCESS);

					(void)bad_syscall(ba, &state->counter);
				}
				state->addr_index++;
			}
			state->addr_index = 0;
			state->syscall_index++;
		}
		_exit(EXIT_SUCCESS);
	} else {
		pid_t ret;
		int status;

		ret = shim_waitpid(pid, &status, 0);
		if (ret < 0) {
			if (errno != EINTR)
				pr_dbg("%s: waitpid() on PID %" PRIdMAX" failed, errno=%d (%s)\n",
					args->name, (intmax_t)pid, errno, strerror(errno));
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
				const void *addr = bad_addrs[state->addr_index].addr;

				if (addr)
					stress_do_syscall(args);

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
	size_t i;

	state = (stress_sysbadaddr_state_t *)stress_mmap_populate(NULL,
		sizeof(*state), PROT_READ | PROT_WRITE,
		MAP_ANONYMOUS | MAP_SHARED, -1, 0);
	if (state == MAP_FAILED) {
		pr_inf_skip("%s: failed to mmap %zu byte anonymous state structure%s, "
		       "errno=%d (%s), skipping stressor\n",
			args->name, sizeof(*state),
			stress_get_memfree_str(), errno, strerror(errno));
		ret = EXIT_NO_RESOURCE;
		goto cleanup;
	}
	stress_set_vma_anon_name(state, sizeof(*state), "state");

	ro_page = stress_mmap_populate(NULL, page_size,
		PROT_READ, MAP_ANONYMOUS | MAP_SHARED, -1, 0);
	if (ro_page == MAP_FAILED) {
		pr_inf_skip("%s: failed to mmap %zu byte anonymous read-only page%s, "
		       "errno=%d (%s), skipping stressor\n",
			args->name, page_size,
			stress_get_memfree_str(), errno, strerror(errno));
		ret = EXIT_NO_RESOURCE;
		goto cleanup;
	}
	stress_set_vma_anon_name(ro_page, page_size, "ro-page");
	(void)stress_madvise_mergeable(ro_page, page_size);

	rw_page = stress_mmap_populate(NULL, page_size << 1,
		PROT_READ | PROT_WRITE,
		MAP_ANONYMOUS | MAP_SHARED, -1, 0);
	if (rw_page == MAP_FAILED) {
		pr_inf_skip("%s: failed to mmap %zu byte anonymous read-write page%s, "
		       "errno=%d (%s), skipping stressor\n",
			args->name, page_size << 1,
			stress_get_memfree_str(), errno, strerror(errno));
		ret = EXIT_NO_RESOURCE;
		goto cleanup;
	}
	stress_set_vma_anon_name(rw_page, page_size << 1, "rw-page");
	(void)stress_madvise_mergeable(rw_page, page_size << 1);

	rx_page = stress_mmap_populate(NULL, page_size,
		PROT_EXEC | PROT_READ,
		MAP_ANONYMOUS | MAP_SHARED, -1, 0);
	if (rx_page == MAP_FAILED) {
		pr_inf_skip("%s: failed to mmap %zu byte anonymous execute-only page%s, "
		       "errno=%d (%s), skipping stressor\n",
			args->name, page_size,
			stress_get_memfree_str(), errno, strerror(errno));
		ret = EXIT_NO_RESOURCE;
		goto cleanup;
	}
	stress_set_vma_anon_name(rx_page, page_size, "rx-page");
	(void)stress_madvise_mergeable(rx_page, page_size);

	no_page = stress_mmap_populate(NULL, page_size,
		PROT_NONE, MAP_ANONYMOUS | MAP_SHARED, -1, 0);
	if (no_page == MAP_FAILED) {
		pr_inf_skip("%s: failed to mmap %zu anonymous prot-none page%s, "
		       "errno=%d (%s), skipping stressor\n",
			args->name, page_size,
			stress_get_memfree_str(), errno, strerror(errno));
		ret = EXIT_NO_RESOURCE;
		goto cleanup;
	}
	stress_set_vma_anon_name(no_page, page_size, "no-page");

	wo_page = stress_mmap_populate(NULL, page_size,
		PROT_WRITE, MAP_ANONYMOUS | MAP_SHARED, -1, 0);
	if (wo_page == MAP_FAILED) {
		pr_inf_skip("%s: failed to mmap %zu byte anonymous write-only page%s, "
		       "errno=%d (%s), skipping stressor\n",
			args->name, page_size,
			stress_get_memfree_str(), errno, strerror(errno));
		ret = EXIT_NO_RESOURCE;
		goto cleanup;
	}
	stress_set_vma_anon_name(wo_page, page_size, "wo-page");
	(void)stress_madvise_mergeable(wo_page, page_size);

	/*
	 * write-execute pages are not supported by some kernels
	 * so make this failure non-fatal to the stress testing
	 * and skip MAP_FAILED addresses in the main stressor
	 */
	wx_page = stress_mmap_populate(NULL, page_size,
		PROT_WRITE | PROT_EXEC, MAP_ANONYMOUS | MAP_SHARED, -1, 0);
	if (wx_page != MAP_FAILED)
		stress_set_vma_anon_name(wo_page, page_size, "wo-page");
	/*
	 * Unmap last page, so we know we have an unmapped
	 * page following the r/w page
	 */
	(void)munmap((void *)(((uint8_t *)rw_page) + page_size), page_size);

	/*
	 *  resolve the addrs using the helper funcs
	 */
	for (i = 0; i < SIZEOF_ARRAY(bad_addrs); i++) {
		bad_addrs[i].addr = bad_addrs[i].func(args);
	}

	stress_set_proc_state(args->name, STRESS_STATE_SYNC_WAIT);
	stress_sync_start_wait(args);
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

const stressor_info_t stress_sysbadaddr_info = {
	.stressor = stress_sysbadaddr,
	.classifier = CLASS_OS,
	.help = help
};
