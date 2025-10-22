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
#include "core-arch.h"
#include "core-capabilities.h"
#include "core-cpu.h"
#include "core-killpid.h"
#include "core-out-of-memory.h"
#include "core-pragma.h"

static const stress_help_t help[] = {
	{ NULL,	"enosys N",	"start N workers that call non-existent system calls" },
	{ NULL,	"enosys-ops N",	"stop after N enosys bogo operations" },
	{ NULL,	NULL,		NULL }
};

#if defined(HAVE_SYSCALL_H) &&	\
    defined(HAVE_SYSCALL)

#define HASH_SYSCALL_SIZE	(1987)

#if defined(__NR_syscalls)
#define MAX_SYSCALL	__NR_syscalls
#else
#define MAX_SYSCALL	(2048)		/* Guess */
#endif

#if defined(__linux__) &&       	\
    !defined(HAVE_COMPILER_TCC) &&	\
    !defined(HAVE_COMPILER_PCC) &&	\
    defined(STRESS_ARCH_X86_64)
#define STRESS_EXERCISE_X86_SYSCALL
#endif

#define CALL_UNDEFINED		(0)
#define CALL_BY_SYSCALL		(1)	/* syscall() call */
#define CALL_BY_X86_SYSCALL	(2)	/* x86 syscall opcode */

typedef struct hash_syscall {
	struct hash_syscall *next;
	long int number;
} stress_hash_syscall_t;

typedef union {
	long int syscall_num;		/* syscall number */
	struct {
		int rc;			/* errno return */
		unsigned int count;	/* syscall count*/
	} ret;
} stress_enosys_rpc_t;

static stress_hash_syscall_t *hash_syscall_table[HASH_SYSCALL_SIZE];

static int stress_call_type = CALL_UNDEFINED;
static sigjmp_buf jmp_env;

#if defined(STRESS_EXERCISE_X86_SYSCALL)
static bool stress_x86syscall_available;

/*
 *  x86_64_syscall6()
 *      syscall 6 arg wrapper
 */
static inline long int x86_64_syscall6(
	long int number,
	long int arg1,
	long int arg2,
	long int arg3,
	long int arg4,
	long int arg5,
	long int arg6)
{
	long int ret;

	stress_call_type = CALL_BY_X86_SYSCALL;

	{
		long int tmp_arg1 = arg1;
		long int tmp_arg2 = arg2;
		long int tmp_arg3 = arg3;
		long int tmp_arg4 = arg4;
		long int tmp_arg5 = arg5;
		long int tmp_arg6 = arg6;

		register long int asm_arg1 __asm__ ("rdi") = tmp_arg1;
		register long int asm_arg2 __asm__ ("rsi") = tmp_arg2;
		register long int asm_arg3 __asm__ ("rdx") = tmp_arg3;
		register long int asm_arg4 __asm__ ("r10") = tmp_arg4;
		register long int asm_arg5 __asm__ ("r8")  = tmp_arg5;
		register long int asm_arg6 __asm__ ("r9")  = tmp_arg6;

		__asm__ __volatile__("syscall\n\t"
			: "=a" (ret)
			: "0" (number), "r" (asm_arg1), "r" (asm_arg2), "r" (asm_arg3),
			  "r" (asm_arg4), "r" (asm_arg5), "r" (asm_arg6)
			: "memory", "cc", "r11", "cx");
		if (ret < 0) {
			errno = -(int)ret;
			ret = -1;
		}
	}
	return ret;
}
#endif

STRESS_PRAGMA_PUSH
STRESS_PRAGMA_WARN_OFF
static inline long int syscall7(long int number, long int arg1, long int arg2,
			    long int arg3, long int arg4, long int arg5,
			    long int arg6, long int arg7)
{
	stress_call_type = CALL_BY_SYSCALL;

	return syscall(number, arg1, arg2, arg3, arg4, arg5, arg6, arg7);
}
STRESS_PRAGMA_POP

static inline void exit_if_child(const pid_t pid)
{
	if (getpid() != pid) {
		/* Somehow we forked/cloned ourselves, so exit */
		_exit(0);
	}
}

static inline void itimer_set(stress_args_t *args)
{
	struct itimerval it;

	/*
	 * Force abort if we take too long
	 */
	it.it_interval.tv_sec = 0;
	it.it_interval.tv_usec = 100000;
	it.it_value.tv_sec = 0;
	it.it_value.tv_usec = 100000;
	if (setitimer(ITIMER_REAL, &it, NULL) < 0) {
		pr_dbg("%s setitimer failed, errno=%d (%s)\n",
			args->name, errno, strerror(errno));
		_exit(EXIT_NO_RESOURCE);
	}
}

static inline void itimer_stop(stress_args_t *args)
{
	struct itimerval it;

	/*
	 * Force abort if we take too long
	 */
	it.it_interval.tv_sec = 0;
	it.it_interval.tv_usec = 0;
	it.it_value.tv_sec = 0;
	it.it_value.tv_usec = 0;
	if (setitimer(ITIMER_REAL, &it, NULL) < 0) {
		pr_dbg("%s setitimer failed, errno=%d (%s)\n",
			args->name, errno, strerror(errno));
		_exit(EXIT_NO_RESOURCE);
	}
}

static int stress_enosys_syscall(
	const pid_t pid,
	stress_enosys_rpc_t *rpc)
{
	int ret;
	static int errno_enosys = 0;

	ret = sigsetjmp(jmp_env, 1);
	if (ret)
		goto try_x86_syscall;
	errno = 0;
	ret = (int)syscall7(rpc->syscall_num, -1, -1, -1, -1, -1, -1, -1);
	rpc->ret.count++;
	exit_if_child(pid);
	if ((ret < 0) && (errno == ENOSYS))
		errno_enosys = errno;
	else
		return errno;

try_x86_syscall:
#if defined(STRESS_EXERCISE_X86_SYSCALL)
	{
		static bool x86_syscall_ok = true;

		ret = sigsetjmp(jmp_env, 1);
		if (ret) {
			x86_syscall_ok = false;
			goto finish;
		}
		if (stress_x86syscall_available && x86_syscall_ok) {
			errno = 0;
			ret = (int)x86_64_syscall6(rpc->syscall_num, -1, -1, -1, -1, -1, -1);
			rpc->ret.count++;
			exit_if_child(pid);
			if ((ret < 0) && (errno == ENOSYS))
				errno_enosys = errno;
			else
				return errno;
		}
	}
finish:
#endif

	return errno_enosys;
}

/* Dodgy hack */
#if defined(__linux__) &&	\
    !defined(__NR_sigsuspend)
#define __NR_sigsuspend 72
#endif

static const int syscall_ignore[] = {
#if defined(SYS_reboot)
	SYS_reboot,
#endif
#if defined(__NR_reboot)
	__NR_reboot,
#endif
#if defined(SYS_clone)
	SYS_clone,
#endif
#if defined(__NR_clone)
	__NR_clone,
#endif
#if defined(SYS_clone2)
	SYS_clone2,
#endif
#if defined(__NR_clone2)
	__NR_clone2,
#endif
#if defined(SYS_clone3)
	SYS_clone3,
#endif
#if defined(__NR_clone3)
	__NR_clone3,
#endif
#if defined(SYS_fork)
	SYS_fork,
#endif
#if defined(__NR_fork)
	__NR_fork,
#endif
#if defined(SYS_sigsuspend)
	SYS_sigsuspend,
#endif
#if defined(__NR_sigsuspend)
	__NR_sigsuspend,
#endif
#if defined(SYS_vfork)
	SYS_vfork,
#endif
#if defined(__NR_vfork)
	__NR_vfork,
#endif
#if defined(SYS_vhangup)
	SYS_vhangup,
#endif
#if defined(__NR_vhangup)
	__NR_vhangup,
#endif
};

static inline bool OPTIMIZE3 syscall_find(long int number)
{
	register stress_hash_syscall_t *h;
	register int i;
	register const long int number16 = number & 0xffff;
	const unsigned long int idx = (unsigned long int)number;

	/* Really make sure some syscalls are never called */
	for (i = 0; i < (int)SIZEOF_ARRAY(syscall_ignore); i++) {
		if (number16 == syscall_ignore[i])
			return true;
	}

	h = hash_syscall_table[idx % HASH_SYSCALL_SIZE];
	while (h) {
		if (h->number == number)
			return true;
		h = h->next;
	}
	return false;
}

static inline void OPTIMIZE3 syscall_add(const long int number)
{
	const unsigned long int idx = (unsigned long int)number;
	const unsigned long int hash = idx % HASH_SYSCALL_SIZE;
	stress_hash_syscall_t *newh, *h = hash_syscall_table[hash];

	while (h) {
		if (h->number == number)
			return;		/* Already exists */
		h = h->next;
	}

	newh = (stress_hash_syscall_t *)malloc(sizeof(*newh));
	if (!newh)
		return;	/* Can't add, ignore */

	newh->number = number;
	newh->next = hash_syscall_table[hash];
	hash_syscall_table[hash] = newh;
}

static inline void syscall_free(void)
{
	size_t i;

	for (i = 0; i < HASH_SYSCALL_SIZE; i++) {
		stress_hash_syscall_t *h = hash_syscall_table[i];

		while (h) {
			stress_hash_syscall_t *next = h->next;

			free(h);
			h = next;
		}
	}
}


static const int sigs[] = {
#if defined(SIGILL)
	SIGILL,
#endif
#if defined(SIGBUS)
	SIGBUS,
#endif
#if defined(SIGSEGV)
	SIGSEGV,
#endif
};

static const int exit_sigs[] = {
#if defined(SIGTRAP)
	SIGTRAP,
#endif
#if defined(SIGFPE)
	SIGFPE,
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

static const long int skip_syscalls[] = {
/* Traditional SYS_ syscall interface */
#if defined(SYS_accept)
	SYS_accept,
#endif
#if defined(SYS_accept4)
	SYS_accept4,
#endif
#if defined(SYS_access)
	SYS_access,
#endif
#if defined(SYS_acct)
	SYS_acct,
#endif
#if defined(SYS_add_key)
	SYS_add_key,
#endif
#if defined(SYS_adjtimex)
	SYS_adjtimex,
#endif
#if defined(SYS_afs_syscall)
	SYS_afs_syscall,
#endif
#if defined(SYS_alarm)
	SYS_alarm,
#endif
#if defined(SYS_arch_prctl)
	SYS_arch_prctl,
#endif
#if defined(SYS_bdflush)
	SYS_bdflush,
#endif
#if defined(SYS_bind)
	SYS_bind,
#endif
#if defined(SYS_bpf)
	SYS_bpf,
#endif
#if defined(SYS_break)
	SYS_break,
#endif
#if defined(SYS_brk)
	SYS_brk,
#endif
#if defined(SYS_cachectl)
	SYS_cachectl,
#endif
#if defined(SYS_cacheflush)
	SYS_cacheflush,
#endif
#if defined(SYS_cachestat)
	SYS_cachestat,
#endif
#if defined(SYS_cache_sync)
	SYS_cache_sync,
#endif
#if defined(SYS_capget)
	SYS_capget,
#endif
#if defined(SYS_capset)
	SYS_capset,
#endif
#if defined(SYS_chdir)
	SYS_chdir,
#endif
#if defined(SYS_chmod)
	SYS_chmod,
#endif
#if defined(SYS_chown)
	SYS_chown,
#endif
#if defined(SYS_chown32)
	SYS_chown32,
#endif
#if defined(SYS_chroot)
	SYS_chroot,
#endif
#if defined(SYS_clock_adjtime)
	SYS_clock_adjtime,
#endif
#if defined(SYS_clock_getres)
	SYS_clock_getres,
#endif
#if defined(SYS_clock_gettime)
	SYS_clock_gettime,
#endif
#if defined(SYS_clock_nanosleep)
	SYS_clock_nanosleep,
#endif
#if defined(SYS_clock_settime)
	SYS_clock_settime,
#endif
#if defined(SYS_clone)
	SYS_clone,
#endif
#if defined(SYS_clone2)
	SYS_clone2,
#endif
#if defined(SYS_clone3)
	SYS_clone3,
#endif
#if defined(SYS_close)
	SYS_close,
#endif
#if defined(SYS_connect)
	SYS_connect,
#endif
#if defined(SYS_copy_file_range)
	SYS_copy_file_range,
#endif
#if defined(SYS_creat)
	SYS_creat,
#endif
#if defined(SYS_create_module)
	SYS_create_module,
#endif
#if defined(SYS_delete_module)
	SYS_delete_module,
#endif
#if defined(SYS_dup)
	SYS_dup,
#endif
#if defined(SYS_dup2)
	SYS_dup2,
#endif
#if defined(SYS_dup3)
	SYS_dup3,
#endif
#if defined(SYS_epoll_create)
	SYS_epoll_create,
#endif
#if defined(SYS_epoll_create1)
	SYS_epoll_create1,
#endif
#if defined(SYS_epoll_ctl)
	SYS_epoll_ctl,
#endif
#if defined(SYS_epoll_ctl_old)
	SYS_epoll_ctl_old,
#endif
#if defined(SYS_epoll_pwait)
	SYS_epoll_pwait,
#endif
#if defined(SYS_epoll_wait)
	SYS_epoll_wait,
#endif
#if defined(SYS_epoll_wait_old)
	SYS_epoll_wait_old,
#endif
#if defined(SYS_eventfd)
	SYS_eventfd,
#endif
#if defined(SYS_eventfd2)
	SYS_eventfd2,
#endif
#if defined(SYS_execve)
	SYS_execve,
#endif
#if defined(SYS_execveat)
	SYS_execveat,
#endif
#if defined(SYS_exit)
	SYS_exit,
#endif
#if defined(SYS_exit_group)
	SYS_exit_group,
#endif
#if defined(SYS_faccessat)
	SYS_faccessat,
#endif
#if defined(SYS_fadvise64)
	SYS_fadvise64,
#endif
#if defined(SYS_fadvise64_64)
	SYS_fadvise64_64,
#endif
#if defined(SYS_fallocate)
	SYS_fallocate,
#endif
#if defined(SYS_fanotify_init)
	SYS_fanotify_init,
#endif
#if defined(SYS_fanotify_mark)
	SYS_fanotify_mark,
#endif
#if defined(SYS_fchdir)
	SYS_fchdir,
#endif
#if defined(SYS_fchmod)
	SYS_fchmod,
#endif
#if defined(SYS_fchmodat)
	SYS_fchmodat,
#endif
#if defined(SYS_fchmodat2)
	SYS_fchmodat2,
#endif
#if defined(SYS_fchown)
	SYS_fchown,
#endif
#if defined(SYS_fchown32)
	SYS_fchown32,
#endif
#if defined(SYS_fchownat)
	SYS_fchownat,
#endif
#if defined(SYS_fcntl)
	SYS_fcntl,
#endif
#if defined(SYS_fcntl64)
	SYS_fcntl64,
#endif
#if defined(SYS_fdatasync)
	SYS_fdatasync,
#endif
#if defined(SYS_fgetxattr)
	SYS_fgetxattr,
#endif
#if defined(SYS_file_getattr)
	SYS_file_getattr,
#endif
#if defined(SYS_file_setattr)
	SYS_file_setattr,
#endif
#if defined(SYS_finit_module)
	SYS_finit_module,
#endif
#if defined(SYS_flistxattr)
	SYS_flistxattr,
#endif
#if defined(SYS_flock)
	SYS_flock,
#endif
#if defined(SYS_fork)
	SYS_fork,
#endif
#if defined(SYS_fremovexattr)
	SYS_fremovexattr,
#endif
#if defined(SYS_fsconfig)
	SYS_fsconfig,
#endif
#if defined(SYS_fsetxattr)
	SYS_fsetxattr,
#endif
#if defined(SYS_fsmount)
	SYS_fsmount,
#endif
#if defined(SYS_fsopen)
	SYS_fsopen,
#endif
#if defined(SYS_fspick)
	SYS_fspick,
#endif
#if defined(SYS_fstat)
	SYS_fstat,
#endif
#if defined(SYS_fstat64)
	SYS_fstat64,
#endif
#if defined(SYS_fstatat64)
	SYS_fstatat64,
#endif
#if defined(SYS_fstatfs)
	SYS_fstatfs,
#endif
#if defined(SYS_fstatfs64)
	SYS_fstatfs64,
#endif
#if defined(SYS_fsync)
	SYS_fsync,
#endif
#if defined(SYS_ftime)
	SYS_ftime,
#endif
#if defined(SYS_ftruncate)
	SYS_ftruncate,
#endif
#if defined(SYS_ftruncate64)
	SYS_ftruncate64,
#endif
#if defined(SYS_futex)
	SYS_futex,
#endif
#if defined(SYS_futexrequeue)
	SYS_futexrequeue,
#endif
#if defined(SYS_futexwake)
	SYS_futexwake,
#endif
#if defined(SYS_futexwait)
	SYS_futexwait,
#endif
#if defined(SYS_futimesat)
	SYS_futimesat,
#endif
#if defined(SYS_getcpu)
	SYS_getcpu,
#endif
#if defined(SYS_getcwd)
	SYS_getcwd,
#endif
#if defined(SYS_getdents)
	SYS_getdents,
#endif
#if defined(SYS_getdents64)
	SYS_getdents64,
#endif
#if defined(SYS_getegid)
	SYS_getegid,
#endif
#if defined(SYS_getegid32)
	SYS_getegid32,
#endif
#if defined(SYS_geteuid)
	SYS_geteuid,
#endif
#if defined(SYS_geteuid32)
	SYS_geteuid32,
#endif
#if defined(SYS_getgid)
	SYS_getgid,
#endif
#if defined(SYS_getgid32)
	SYS_getgid32,
#endif
#if defined(SYS_getgroups)
	SYS_getgroups,
#endif
#if defined(SYS_getgroups32)
	SYS_getgroups32,
#endif
#if defined(SYS_getitimer)
	SYS_getitimer,
#endif
#if defined(SYS_get_kernel_syms)
	SYS_get_kernel_syms,
#endif
#if defined(SYS_get_mempolicy)
	SYS_get_mempolicy,
#endif
#if defined(SYS_getpeername)
	SYS_getpeername,
#endif
#if defined(SYS_getpgid)
	SYS_getpgid,
#endif
#if defined(SYS_getpgrp)
	SYS_getpgrp,
#endif
#if defined(SYS_getpid)
	SYS_getpid,
#endif
#if defined(SYS_getpmsg)
	SYS_getpmsg,
#endif
#if defined(SYS_getppid)
	SYS_getppid,
#endif
#if defined(SYS_getpriority)
	SYS_getpriority,
#endif
#if defined(SYS_getrandom)
	SYS_getrandom,
#endif
#if defined(SYS_getresgid)
	SYS_getresgid,
#endif
#if defined(SYS_getresgid32)
	SYS_getresgid32,
#endif
#if defined(SYS_getresuid)
	SYS_getresuid,
#endif
#if defined(SYS_getresuid32)
	SYS_getresuid32,
#endif
#if defined(SYS_getrlimit)
	SYS_getrlimit,
#endif
#if defined(SYS_get_robust_list)
	SYS_get_robust_list,
#endif
#if defined(SYS_getrusage)
	SYS_getrusage,
#endif
#if defined(SYS_getsid)
	SYS_getsid,
#endif
#if defined(SYS_getsockname)
	SYS_getsockname,
#endif
#if defined(SYS_getsockopt)
	SYS_getsockopt,
#endif
#if defined(SYS_get_thread_area)
	SYS_get_thread_area,
#endif
#if defined(SYS_gettid)
	SYS_gettid,
#endif
#if defined(SYS_gettimeofday)
	SYS_gettimeofday,
#endif
#if defined(SYS_getuid)
	SYS_getuid,
#endif
#if defined(SYS_getuid32)
	SYS_getuid32,
#endif
#if defined(SYS_getxattr)
	SYS_getxattr,
#endif
#if defined(SYS_getxattrat)
	SYS_getxattrat,
#endif
#if defined(SYS_gtty)
	SYS_gtty,
#endif
#if defined(SYS_idle)
	SYS_idle,
#endif
#if defined(SYS_init_module)
	SYS_init_module,
#endif
#if defined(SYS_inotify_add_watch)
	SYS_inotify_add_watch,
#endif
#if defined(SYS_inotify_init)
	SYS_inotify_init,
#endif
#if defined(SYS_inotify_init1)
	SYS_inotify_init1,
#endif
#if defined(SYS_inotify_rm_watch)
	SYS_inotify_rm_watch,
#endif
#if defined(SYS_io_cancel)
	SYS_io_cancel,
#endif
#if defined(SYS_io_destroy)
	SYS_io_destroy,
#endif
#if defined(SYS_io_getevents)
	SYS_io_getevents,
#endif
#if defined(SYS_io_pgetevents)
	SYS_io_pgetevents,
#endif
#if defined(SYS_ioperm)
	SYS_ioperm,
#endif
#if defined(SYS_iopl)
	SYS_iopl,
#endif
#if defined(SYS_ioprio_get)
	SYS_ioprio_get,
#endif
#if defined(SYS_ioprio_set)
	SYS_ioprio_set,
#endif
#if defined(SYS_io_setup)
	SYS_io_setup,
#endif
#if defined(SYS_io_submit)
	SYS_io_submit,
#endif
#if defined(SYS_io_uring_enter)
	SYS_io_uring_enter,
#endif
#if defined(SYS_io_uring_register)
	SYS_io_uring_register,
#endif
#if defined(SYS_io_uring_setup)
	SYS_io_uring_setup,
#endif
#if defined(SYS_ioctl)
	SYS_ioctl,
#endif
#if defined(SYS_ipc)
	SYS_ipc,
#endif
#if defined(SYS_kcmp)
	SYS_kcmp,
#endif
#if defined(SYS_kexec_file_load)
	SYS_kexec_file_load,
#endif
#if defined(SYS_kexec_load)
	SYS_kexec_load,
#endif
#if defined(SYS_keyctl)
	SYS_keyctl,
#endif
#if defined(SYS_kill)
	SYS_kill,
#endif
#if defined(SYS_lchown)
	SYS_lchown,
#endif
#if defined(SYS_lchown32)
	SYS_lchown32,
#endif
#if defined(SYS_lgetxattr)
	SYS_lgetxattr,
#endif
#if defined(SYS_link)
	SYS_link,
#endif
#if defined(SYS_linkat)
	SYS_linkat,
#endif
#if defined(SYS_listen)
	SYS_listen,
#endif
#if defined(SYS_listmount)
	SYS_listmount,
#endif
#if defined(SYS_listxattr)
	SYS_listxattr,
#endif
#if defined(SYS_listxattrat)
	SYS_listxattrat,
#endif
#if defined(SYS_llistxattr)
	SYS_llistxattr,
#endif
#if defined(SYS__llseek)
	SYS__llseek,
#endif
#if defined(SYS_lock)
	SYS_lock,
#endif
#if defined(SYS_lookup_dcookie)
	SYS_lookup_dcookie,
#endif
#if defined(SYS_lremovexattr)
	SYS_lremovexattr,
#endif
#if defined(SYS_lseek)
	SYS_lseek,
#endif
#if defined(SYS_lsetxattr)
	SYS_lsetxattr,
#endif
#if defined(SYS_lstat)
	SYS_lstat,
#endif
#if defined(SYS_lstat64)
	SYS_lstat64,
#endif
#if defined(SYS_lsm_get_self_attr)
	SYS_lsm_get_self_attr,
#endif
#if defined(SYS_lsm_list_modules)
	SYS_lsm_list_modules,
#endif
#if defined(SYS_lsm_set_self_attr)
	SYS_lsm_set_self_attr,
#endif
#if defined(SYS_madvise)
	SYS_madvise,
#endif
#if defined(SYS_map_shadow_stack)
	SYS_map_shadow_stack,
#endif
#if defined(SYS_mbind)
	SYS_mbind,
#endif
#if defined(SYS_memory_ordering)
	SYS_memory_ordering,
#endif
#if defined(SYS_membarrier)
	SYS_membarrier,
#endif
#if defined(SYS_memfd_create)
	SYS_memfd_create,
#endif
#if defined(SYS_migrate_pages)
	SYS_migrate_pages,
#endif
#if defined(SYS_mincore)
	SYS_mincore,
#endif
#if defined(SYS_mkdir)
	SYS_mkdir,
#endif
#if defined(SYS_mkdirat)
	SYS_mkdirat,
#endif
#if defined(SYS_mknod)
	SYS_mknod,
#endif
#if defined(SYS_mknodat)
	SYS_mknodat,
#endif
#if defined(SYS_mlock)
	SYS_mlock,
#endif
#if defined(SYS_mlock2)
	SYS_mlock2,
#endif
#if defined(SYS_mlockall)
	SYS_mlockall,
#endif
#if defined(SYS_mmap)
	SYS_mmap,
#endif
#if defined(SYS_mmap2)
	SYS_mmap2,
#endif
#if defined(SYS_modify_ldt)
	SYS_modify_ldt,
#endif
#if defined(SYS_mount)
	SYS_mount,
#endif
#if defined(SYS_move_mount)
	SYS_move_mount,
#endif
#if defined(SYS_move_pages)
	SYS_move_pages,
#endif
#if defined(SYS_mprotect)
	SYS_mprotect,
#endif
#if defined(SYS_mpx)
	SYS_mpx,
#endif
#if defined(SYS_mq_getsetattr)
	SYS_mq_getsetattr,
#endif
#if defined(SYS_mq_notify)
	SYS_mq_notify,
#endif
#if defined(SYS_mq_open)
	SYS_mq_open,
#endif
#if defined(SYS_mq_timedreceive)
	SYS_mq_timedreceive,
#endif
#if defined(SYS_mq_timedsend)
	SYS_mq_timedsend,
#endif
#if defined(SYS_mq_unlink)
	SYS_mq_unlink,
#endif
#if defined(SYS_mremap)
	SYS_mremap,
#endif
#if defined(SYS_mseal)
	SYS_mseal,
#endif
#if defined(SYS_msgctl)
	SYS_msgctl,
#endif
#if defined(SYS_msgget)
	SYS_msgget,
#endif
#if defined(SYS_msgrcv)
	SYS_msgrcv,
#endif
#if defined(SYS_msgsnd)
	SYS_msgsnd,
#endif
#if defined(SYS_msync)
	SYS_msync,
#endif
#if defined(SYS_munlock)
	SYS_munlock,
#endif
#if defined(SYS_munlockall)
	SYS_munlockall,
#endif
#if defined(SYS_munmap)
	SYS_munmap,
#endif
#if defined(SYS_name_to_handle_at)
	SYS_name_to_handle_at,
#endif
#if defined(SYS_nanosleep)
	SYS_nanosleep,
#endif
#if defined(SYS_newfstatat)
	SYS_newfstatat,
#endif
#if defined(SYS__newselect)
	SYS__newselect,
#endif
#if defined(SYS_nfsservctl)
	SYS_nfsservctl,
#endif
#if defined(SYS_nice)
	SYS_nice,
#endif
#if defined(SYS_oldfstat)
	SYS_oldfstat,
#endif
#if defined(SYS_oldlstat)
	SYS_oldlstat,
#endif
#if defined(SYS_oldolduname)
	SYS_oldolduname,
#endif
#if defined(SYS_oldstat)
	SYS_oldstat,
#endif
#if defined(SYS_olduname)
	SYS_olduname,
#endif
#if defined(SYS_open)
	SYS_open,
#endif
#if defined(SYS_openat)
	SYS_openat,
#endif
#if defined(SYS_open_by_handle_at)
	SYS_open_by_handle_at,
#endif
#if defined(SYS_open_tree)
	SYS_open_tree,
#endif
#if defined(SYS_open_tree_attr)
	SYS_open_tree_attr,
#endif
#if defined(SYS_pause)
	SYS_pause,
#endif
#if defined(SYS_perf_event_open)
	SYS_perf_event_open,
#endif
#if defined(SYS_personality)
	SYS_personality,
#endif
#if defined(SYS_pidfd_open)
	SYS_pidfd_open,
#endif
#if defined(SYS_pidfd_send_signal)
	SYS_pidfd_send_signal,
#endif
#if defined(SYS_pipe)
	SYS_pipe,
#endif
#if defined(SYS_pipe2)
	SYS_pipe2,
#endif
#if defined(SYS_pivot_root)
	SYS_pivot_root,
#endif
#if defined(SYS_pkey_alloc)
	SYS_pkey_alloc,
#endif
#if defined(SYS_pkey_free)
	SYS_pkey_free,
#endif
#if defined(SYS_pkey_get)
	SYS_pkey_get,
#endif
#if defined(SYS_pkey_mprotect)
	SYS_pkey_mprotect,
#endif
#if defined(SYS_pkey_set)
	SYS_pkey_set,
#endif
#if defined(SYS_poll)
	SYS_poll,
#endif
#if defined(SYS_ppoll)
	SYS_ppoll,
#endif
#if defined(SYS_prctl)
	SYS_prctl,
#endif
#if defined(SYS_pread64)
	SYS_pread64,
#endif
#if defined(SYS_preadv)
	SYS_preadv,
#endif
#if defined(SYS_preadv2)
	SYS_preadv2,
#endif
#if defined(SYS_prlimit64)
	SYS_prlimit64,
#endif
#if defined(SYS_process_vm_readv)
	SYS_process_vm_readv,
#endif
#if defined(SYS_process_vm_writev)
	SYS_process_vm_writev,
#endif
#if defined(SYS_prof)
	SYS_prof,
#endif
#if defined(SYS_profil)
	SYS_profil,
#endif
#if defined(SYS_pselect6)
	SYS_pselect6,
#endif
#if defined(SYS_pselect6_time64)
	SYS_pselect6_time64,
#endif
#if defined(SYS_ptrace)
	SYS_ptrace,
#endif
#if defined(SYS_putpmsg)
	SYS_putpmsg,
#endif
#if defined(SYS_pwrite64)
	SYS_pwrite64,
#endif
#if defined(SYS_pwritev)
	SYS_pwritev,
#endif
#if defined(SYS_pwritev2)
	SYS_pwritev2,
#endif
#if defined(SYS_query_module)
	SYS_query_module,
#endif
#if defined(SYS_quotactl)
	SYS_quotactl,
#endif
#if defined(SYS_quotactl_fd)
	SYS_quotactl_fd,
#endif
#if defined(SYS_read)
	SYS_read,
#endif
#if defined(SYS_readahead)
	SYS_readahead,
#endif
#if defined(SYS_readdir)
	SYS_readdir,
#endif
#if defined(SYS_readlink)
	SYS_readlink,
#endif
#if defined(SYS_readlinkat)
	SYS_readlinkat,
#endif
#if defined(SYS_readv)
	SYS_readv,
#endif
#if defined(SYS_reboot)
	SYS_reboot,
#endif
#if defined(SYS_recvfrom)
	SYS_recvfrom,
#endif
#if defined(SYS_recvmmsg)
	SYS_recvmmsg,
#endif
#if defined(SYS_recvmsg)
	SYS_recvmsg,
#endif
#if defined(SYS_remap_file_pages)
	SYS_remap_file_pages,
#endif
#if defined(SYS_removexattr)
	SYS_removexattr,
#endif
#if defined(SYS_removexattrat)
	SYS_removexattrat,
#endif
#if defined(SYS_rename)
	SYS_rename,
#endif
#if defined(SYS_renameat)
	SYS_renameat,
#endif
#if defined(SYS_renameat2)
	SYS_renameat2,
#endif
#if defined(SYS_request_key)
	SYS_request_key,
#endif
#if defined(SYS_restart_syscall)
	SYS_restart_syscall,
#endif
#if defined(SYS_rmdir)
	SYS_rmdir,
#endif
#if defined(SYS_rseq)
	SYS_rseq,
#endif
#if defined(SYS_rt_sigaction)
	SYS_rt_sigaction,
#endif
#if defined(SYS_rt_sigpending)
	SYS_rt_sigpending,
#endif
#if defined(SYS_rt_sigprocmask)
	SYS_rt_sigprocmask,
#endif
#if defined(SYS_rt_sigqueueinfo)
	SYS_rt_sigqueueinfo,
#endif
#if defined(SYS_rt_sigreturn)
	SYS_rt_sigreturn,
#endif
#if defined(SYS_rt_sigsuspend)
	SYS_rt_sigsuspend,
#endif
#if defined(SYS_rt_sigtimedwait)
	SYS_rt_sigtimedwait,
#endif
#if defined(SYS_rt_tgsigqueueinfo)
	SYS_rt_tgsigqueueinfo,
#endif
#if defined(SYS_sched_getaffinity)
	SYS_sched_getaffinity,
#endif
#if defined(SYS_sched_getattr)
	SYS_sched_getattr,
#endif
#if defined(SYS_sched_getparam)
	SYS_sched_getparam,
#endif
#if defined(SYS_sched_get_priority_max)
	SYS_sched_get_priority_max,
#endif
#if defined(SYS_sched_get_priority_min)
	SYS_sched_get_priority_min,
#endif
#if defined(SYS_sched_getscheduler)
	SYS_sched_getscheduler,
#endif
#if defined(SYS_sched_rr_get_interval)
	SYS_sched_rr_get_interval,
#endif
#if defined(SYS_sched_setaffinity)
	SYS_sched_setaffinity,
#endif
#if defined(SYS_sched_setattr)
	SYS_sched_setattr,
#endif
#if defined(SYS_sched_setparam)
	SYS_sched_setparam,
#endif
#if defined(SYS_sched_setscheduler)
	SYS_sched_setscheduler,
#endif
#if defined(SYS_sched_yield)
	SYS_sched_yield,
#endif
#if defined(SYS_seccomp)
	SYS_seccomp,
#endif
#if defined(SYS_security)
	SYS_security,
#endif
#if defined(SYS_select)
	SYS_select,
#endif
#if defined(SYS_semctl)
	SYS_semctl,
#endif
#if defined(SYS_semget)
	SYS_semget,
#endif
#if defined(SYS_semop)
	SYS_semop,
#endif
#if defined(SYS_semtimedop)
	SYS_semtimedop,
#endif
#if defined(SYS_sendfile)
	SYS_sendfile,
#endif
#if defined(SYS_sendfile64)
	SYS_sendfile64,
#endif
#if defined(SYS_sendmmsg)
	SYS_sendmmsg,
#endif
#if defined(SYS_sendmsg)
	SYS_sendmsg,
#endif
#if defined(SYS_sendto)
	SYS_sendto,
#endif
#if defined(SYS_setdomainname)
	SYS_setdomainname,
#endif
#if defined(SYS_setfsgid)
	SYS_setfsgid,
#endif
#if defined(SYS_setfsgid32)
	SYS_setfsgid32,
#endif
#if defined(SYS_setfsuid)
	SYS_setfsuid,
#endif
#if defined(SYS_setfsuid32)
	SYS_setfsuid32,
#endif
#if defined(SYS_setgid)
	SYS_setgid,
#endif
#if defined(SYS_setgid32)
	SYS_setgid32,
#endif
#if defined(SYS_setgroups)
	SYS_setgroups,
#endif
#if defined(SYS_setgroups32)
	SYS_setgroups32,
#endif
#if defined(SYS_sethostname)
	SYS_sethostname,
#endif
#if defined(SYS_setitimer)
	SYS_setitimer,
#endif
#if defined(SYS_set_mempolicy)
	SYS_set_mempolicy,
#endif
#if defined(SYS_setns)
	SYS_setns,
#endif
#if defined(SYS_setpgid)
	SYS_setpgid,
#endif
#if defined(SYS_setpriority)
	SYS_setpriority,
#endif
#if defined(SYS_setregid)
	SYS_setregid,
#endif
#if defined(SYS_setregid32)
	SYS_setregid32,
#endif
#if defined(SYS_setresgid)
	SYS_setresgid,
#endif
#if defined(SYS_setresgid32)
	SYS_setresgid32,
#endif
#if defined(SYS_setresuid)
	SYS_setresuid,
#endif
#if defined(SYS_setresuid32)
	SYS_setresuid32,
#endif
#if defined(SYS_setreuid)
	SYS_setreuid,
#endif
#if defined(SYS_setreuid32)
	SYS_setreuid32,
#endif
#if defined(SYS_setrlimit)
	SYS_setrlimit,
#endif
#if defined(SYS_set_robust_list)
	SYS_set_robust_list,
#endif
#if defined(SYS_setsid)
	SYS_setsid,
#endif
#if defined(SYS_setsockopt)
	SYS_setsockopt,
#endif
#if defined(SYS_set_thread_area)
	SYS_set_thread_area,
#endif
#if defined(SYS_set_tid_address)
	SYS_set_tid_address,
#endif
#if defined(SYS_settimeofday)
	SYS_settimeofday,
#endif
#if defined(SYS_setuid)
	SYS_setuid,
#endif
#if defined(SYS_setuid32)
	SYS_setuid32,
#endif
#if defined(SYS_setxattr)
	SYS_setxattr,
#endif
#if defined(SYS_setxattrat)
	SYS_setxattrat,
#endif
#if defined(SYS_sgetmask)
	SYS_sgetmask,
#endif
#if defined(SYS_shmat)
	SYS_shmat,
#endif
#if defined(SYS_shmctl)
	SYS_shmctl,
#endif
#if defined(SYS_shmdt)
	SYS_shmdt,
#endif
#if defined(SYS_shmget)
	SYS_shmget,
#endif
#if defined(SYS_shutdown)
	SYS_shutdown,
#endif
#if defined(SYS_sigaction)
	SYS_sigaction,
#endif
#if defined(SYS_sigaltstack)
	SYS_sigaltstack,
#endif
#if defined(SYS_signal)
	SYS_signal,
#endif
#if defined(SYS_signalfd)
	SYS_signalfd,
#endif
#if defined(SYS_signalfd4)
	SYS_signalfd4,
#endif
#if defined(SYS_sigpending)
	SYS_sigpending,
#endif
#if defined(SYS_sigprocmask)
	SYS_sigprocmask,
#endif
#if defined(SYS_sigreturn)
	SYS_sigreturn,
#endif
#if defined(SYS_sigsuspend)
	SYS_sigsuspend,
#endif
#if defined(SYS_socket)
	SYS_socket,
#endif
#if defined(SYS_socketcall)
	SYS_socketcall,
#endif
#if defined(SYS_socketpair)
	SYS_socketpair,
#endif
#if defined(SYS_splice)
	SYS_splice,
#endif
#if defined(SYS_ssetmask)
	SYS_ssetmask,
#endif
#if defined(SYS_stat)
	SYS_stat,
#endif
#if defined(SYS_stat64)
	SYS_stat64,
#endif
#if defined(SYS_statfs)
	SYS_statfs,
#endif
#if defined(SYS_statfs64)
	SYS_statfs64,
#endif
#if defined(SYS_statmount)
	SYS_statmount,
#endif
#if defined(SYS_statx)
	SYS_statx,
#endif
#if defined(SYS_stime)
	SYS_stime,
#endif
#if defined(SYS_stty)
	SYS_stty,
#endif
#if defined(SYS_swapoff)
	SYS_swapoff,
#endif
#if defined(SYS_swapon)
	SYS_swapon,
#endif
#if defined(SYS_symlink)
	SYS_symlink,
#endif
#if defined(SYS_symlinkat)
	SYS_symlinkat,
#endif
#if defined(SYS_sync)
	SYS_sync,
#endif
#if defined(SYS_sync_file_range)
	SYS_sync_file_range,
#endif
#if defined(SYS_syncfs)
	SYS_syncfs,
#endif
#if defined(SYS__sysctl)
	SYS__sysctl,
#endif
#if defined(SYS_sysfs)
	SYS_sysfs,
#endif
#if defined(SYS_sysinfo)
	SYS_sysinfo,
#endif
#if defined(SYS_syslog)
	SYS_syslog,
#endif
#if defined(SYS_tee)
	SYS_tee,
#endif
#if defined(SYS_tgkill)
	SYS_tgkill,
#endif
#if defined(SYS_time)
	SYS_time,
#endif
#if defined(SYS_timer_create)
	SYS_timer_create,
#endif
#if defined(SYS_timer_delete)
	SYS_timer_delete,
#endif
#if defined(SYS_timerfd_create)
	SYS_timerfd_create,
#endif
#if defined(SYS_timerfd_gettime)
	SYS_timerfd_gettime,
#endif
#if defined(SYS_timerfd_settime)
	SYS_timerfd_settime,
#endif
#if defined(SYS_timer_getoverrun)
	SYS_timer_getoverrun,
#endif
#if defined(SYS_timer_gettime)
	SYS_timer_gettime,
#endif
#if defined(SYS_timer_settime)
	SYS_timer_settime,
#endif
#if defined(SYS_times)
	SYS_times,
#endif
#if defined(SYS_tkill)
	SYS_tkill,
#endif
#if defined(SYS_truncate)
	SYS_truncate,
#endif
#if defined(SYS_truncate64)
	SYS_truncate64,
#endif
#if defined(SYS_tuxcall)
	SYS_tuxcall,
#endif
#if defined(SYS_ugetrlimit)
	SYS_ugetrlimit,
#endif
#if defined(SYS_ulimit)
	SYS_ulimit,
#endif
#if defined(SYS_umask)
	SYS_umask,
#endif
#if defined(SYS_umount)
	SYS_umount,
#endif
#if defined(SYS_umount2)
	SYS_umount2,
#endif
#if defined(SYS_uname)
	SYS_uname,
#endif
#if defined(SYS_unlink)
	SYS_unlink,
#endif
#if defined(SYS_unlinkat)
	SYS_unlinkat,
#endif
#if defined(SYS_unshare)
	SYS_unshare,
#endif
#if defined(SYS_uretprobe)
	SYS_uretprobe,
#endif
#if defined(SYS_uselib)
	SYS_uselib,
#endif
#if defined(SYS_userfaultfd)
	SYS_userfaultfd,
#endif
#if defined(SYS_ustat)
	SYS_ustat,
#endif
#if defined(SYS_utime)
	SYS_utime,
#endif
#if defined(SYS_utimensat)
	SYS_utimensat,
#endif
#if defined(SYS_utimes)
	SYS_utimes,
#endif
#if defined(SYS_vfork)
	SYS_vfork,
#endif
#if defined(SYS_vhangup)
	SYS_vhangup,
#endif
#if defined(SYS_vm86)
	SYS_vm86,
#endif
#if defined(SYS_vm86old)
	SYS_vm86old,
#endif
#if defined(SYS_vmsplice)
	SYS_vmsplice,
#endif
#if defined(SYS_vserver)
	SYS_vserver,
#endif
#if defined(SYS_wait4)
	SYS_wait4,
#endif
#if defined(SYS_waitid)
	SYS_waitid,
#endif
#if defined(SYS_waitpid)
	SYS_waitpid,
#endif
#if defined(SYS_write)
	SYS_write,
#endif
#if defined(SYS_writev)
	SYS_writev,
#endif

/* Linux syscall numbers */
#if defined(__NR__llseek)
	__NR__llseek,
#endif
#if defined(__NR__newselect)
	__NR__newselect,
#endif
#if defined(__NR__sysctl)
	__NR__sysctl,
#endif
#if defined(__NR_accept)
	__NR_accept,
#endif
#if defined(__NR_accept4)
	__NR_accept4,
#endif
#if defined(__NR_access)
	__NR_access,
#endif
#if defined(__NR_acct)
	__NR_acct,
#endif
#if defined(__NR_acl_get)
	__NR_acl_get,
#endif
#if defined(__NR_acl_set)
	__NR_acl_set,
#endif
#if defined(__NR_add_key)
	__NR_add_key,
#endif
#if defined(__NR_adjtimex)
	__NR_adjtimex,
#endif
#if defined(__NR_afs_syscall)
	__NR_afs_syscall,
#endif
#if defined(__NR_alarm)
	__NR_alarm,
#endif
#if defined(__NR_alloc_hugepages)
	__NR_alloc_hugepages,
#endif
#if defined(__NR_arc_gettls)
	__NR_arc_gettls,
#endif
#if defined(__NR_arch_prctl)
	__NR_arch_prctl,
#endif
#if defined(__NR_arch_specific_syscall)
	__NR_arch_specific_syscall,
#endif
#if defined(__NR_arc_settls)
	__NR_arc_settls,
#endif
#if defined(__NR_arc_usr_cmpxchg)
	__NR_arc_usr_cmpxchg,
#endif
#if defined(__NR_arm_fadvise64_64)
	__NR_arm_fadvise64_64,
#endif
#if defined(__NR_arm_sync_file_range)
	__NR_arm_sync_file_range,
#endif
#if defined(__NR_atomic_barrier)
	__NR_atomic_barrier,
#endif
#if defined(__NR_atomic_cmpxchg_32)
	__NR_atomic_cmpxchg_32,
#endif
#if defined(__NR_attrctl)
	__NR_attrctl,
#endif
#if defined(__NR_bdflush)
	__NR_bdflush,
#endif
#if defined(__NR_bfin_spinlock)
	__NR_bfin_spinlock,
#endif
#if defined(__NR_bind)
	__NR_bind,
#endif
#if defined(__NR_bpf)
	__NR_bpf,
#endif
#if defined(__NR_break)
	__NR_break,
#endif
#if defined(__NR_breakpoint)
	__NR_breakpoint,
#endif
#if defined(__NR_brk)
	__NR_brk,
#endif
#if defined(__NR_cachectl)
	__NR_cachectl,
#endif
#if defined(__NR_cacheflush)
	__NR_cacheflush,
#endif
#if defined(__NR_cachestat)
	__NR_cachestat,
#endif
#if defined(__NR_cache_sync)
	__NR_cache_sync,
#endif
#if defined(__NR_capget)
	__NR_capget,
#endif
#if defined(__NR_capset)
	__NR_capset,
#endif
#if defined(__NR_chdir)
	__NR_chdir,
#endif
#if defined(__NR_chmod)
	__NR_chmod,
#endif
#if defined(__NR_chown)
	__NR_chown,
#endif
#if defined(__NR_chown32)
	__NR_chown32,
#endif
#if defined(__NR_chroot)
	__NR_chroot,
#endif
#if defined(__NR_clock_adjtime)
	__NR_clock_adjtime,
#endif
#if defined(__NR_clock_adjtime64)
	__NR_clock_adjtime64,
#endif
#if defined(__NR_clock_getres)
	__NR_clock_getres,
#endif
#if defined(__NR_clock_getres_time64)
	__NR_clock_getres_time64,
#endif
#if defined(__NR_clock_gettime)
	__NR_clock_gettime,
#endif
#if defined(__NR_clock_gettime64)
	__NR_clock_gettime64,
#endif
#if defined(__NR_clock_nanosleep)
	__NR_clock_nanosleep,
#endif
#if defined(__NR_clock_nanosleep_time64)
	__NR_clock_nanosleep_time64,
#endif
#if defined(__NR_clock_nanosleep64)
	__NR_clock_nanosleep64,
#endif
#if defined(__NR_clock_settime)
	__NR_clock_settime,
#endif
#if defined(__NR_clock_settime64)
	__NR_clock_settime64,
#endif
#if defined(__NR_clone)
	__NR_clone,
#endif
#if defined(__NR_clone2)
	__NR_clone2,
#endif
#if defined(__NR_clone3)
	__NR_clone3,
#endif
#if defined(__NR_close)
	__NR_close,
#endif
#if defined(__NR_close_range)
	__NR_close_range,
#endif
#if defined(__NR_cmpxchg_badaddr)
	__NR_cmpxchg_badaddr,
#endif
#if defined(__NR_compat_exit)
	__NR_compat_exit,
#endif
#if defined(__NR_compat_read)
	__NR_compat_read,
#endif
#if defined(__NR_compat_restart_syscall)
	__NR_compat_restart_syscall,
#endif
#if defined(__NR_compat_rt_sigreturn)
	__NR_compat_rt_sigreturn,
#endif
#if defined(__NR_compat_sigreturn)
	__NR_compat_sigreturn,
#endif
#if defined(__NR_compat_syscalls)
	__NR_compat_syscalls,
#endif
#if defined(__NR_compat_write)
	__NR_compat_write,
#endif
#if defined(__NR_connect)
	__NR_connect,
#endif
#if defined(__NR_copy_file_range)
	__NR_copy_file_range,
#endif
#if defined(__NR_creat)
	__NR_creat,
#endif
#if defined(__NR_create_module)
	__NR_create_module,
#endif
#if defined(__NR_delete_module)
	__NR_delete_module,
#endif
#if defined(__NR_dipc)
	__NR_dipc,
#endif
#if defined(__NR_dma_memcpy)
	__NR_dma_memcpy,
#endif
#if defined(__NR_dup)
	__NR_dup,
#endif
#if defined(__NR_dup2)
	__NR_dup2,
#endif
#if defined(__NR_dup3)
	__NR_dup3,
#endif
#if defined(__NR_epoll_create)
	__NR_epoll_create,
#endif
#if defined(__NR_epoll_create1)
	__NR_epoll_create1,
#endif
#if defined(__NR_epoll_ctl)
	__NR_epoll_ctl,
#endif
#if defined(__NR_epoll_ctl_old)
	__NR_epoll_ctl_old,
#endif
#if defined(__NR_epoll_pwait)
	__NR_epoll_pwait,
#endif
#if defined(__NR_epoll_pwait2)
	__NR_epoll_pwait2,
#endif
#if defined(__NR_epoll_wait)
	__NR_epoll_wait,
#endif
#if defined(__NR_epoll_wait_old)
	__NR_epoll_wait_old,
#endif
#if defined(__NR_eventfd)
	__NR_eventfd,
#endif
#if defined(__NR_eventfd2)
	__NR_eventfd2,
#endif
#if defined(__NR_exec_with_loader)
	__NR_exec_with_loader,
#endif
#if defined(__NR_execv)
	__NR_execv,
#endif
#if defined(__NR_execve)
	__NR_execve,
#endif
#if defined(__NR_execveat)
	__NR_execveat,
#endif
#if defined(__NR_exit)
	__NR_exit,
#endif
#if defined(__NR__exit)
	__NR__exit,
#endif
#if defined(__NR_exit_group)
	__NR_exit_group,
#endif
#if defined(__NR_faccessat)
	__NR_faccessat,
#endif
#if defined(__NR_faccessat2)
	__NR_faccessat2,
#endif
#if defined(__NR_fadvise64)
	__NR_fadvise64,
#endif
#if defined(__NR_fadvise64_64)
	__NR_fadvise64_64,
#endif
#if defined(__NR_fallocate)
	__NR_fallocate,
#endif
#if defined(__NR_fanotify_init)
	__NR_fanotify_init,
#endif
#if defined(__NR_fanotify_mark)
	__NR_fanotify_mark,
#endif
#if defined(__NR_FAST_atomic_update)
	__NR_FAST_atomic_update,
#endif
#if defined(__NR_FAST_cmpxchg)
	__NR_FAST_cmpxchg,
#endif
#if defined(__NR_FAST_cmpxchg64)
	__NR_FAST_cmpxchg64,
#endif
#if defined(__NR_fchdir)
	__NR_fchdir,
#endif
#if defined(__NR_fchmod)
	__NR_fchmod,
#endif
#if defined(__NR_fchmodat)
	__NR_fchmodat,
#endif
#if defined(__NR_fchmodat2)
	__NR_fchmodat2,
#endif
#if defined(__NR_fchown)
	__NR_fchown,
#endif
#if defined(__NR_fchown32)
	__NR_fchown32,
#endif
#if defined(__NR_fchownat)
	__NR_fchownat,
#endif
#if defined(__NR_fcntl)
	__NR_fcntl,
#endif
#if defined(__NR_fcntl64)
	__NR_fcntl64,
#endif
#if defined(__NR_fdatasync)
	__NR_fdatasync,
#endif
#if defined(__NR_fgetxattr)
	__NR_fgetxattr,
#endif
#if defined(__NR_file_getattr)
	__NR_file_getattr,
#endif
#if defined(__NR_file_setattr)
	__NR_file_setattr,
#endif
#if defined(__NR_finit_module)
	__NR_finit_module,
#endif
#if defined(__NR_flistxattr)
	__NR_flistxattr,
#endif
#if defined(__NR_flock)
	__NR_flock,
#endif
#if defined(__NR_fork)
	__NR_fork,
#endif
#if defined(__NR_fp_udfiex_crtl)
	__NR_fp_udfiex_crtl,
#endif
#if defined(__NR_free_hugepages)
	__NR_free_hugepages,
#endif
#if defined(__NR_fremovexattr)
	__NR_fremovexattr,
#endif
#if defined(__NR_fsconfig)
	__NR_fsconfig,
#endif
#if defined(__NR_fsetxattr)
	__NR_fsetxattr,
#endif
#if defined(__NR_fsmount)
	__NR_fsmount,
#endif
#if defined(__NR_fsopen)
	__NR_fsopen,
#endif
#if defined(__NR_fspick)
	__NR_fspick,
#endif
#if defined(__NR_fstat)
	__NR_fstat,
#endif
#if defined(__NR_fstat64)
	__NR_fstat64,
#endif
#if defined(__NR_fstatat64)
	__NR_fstatat64,
#endif
#if defined(__NR_fstatfs)
	__NR_fstatfs,
#endif
#if defined(__NR_fstatfs64)
	__NR_fstatfs64,
#endif
#if defined(__NR_fsync)
	__NR_fsync,
#endif
#if defined(__NR_ftime)
	__NR_ftime,
#endif
#if defined(__NR_ftruncate)
	__NR_ftruncate,
#endif
#if defined(__NR_ftruncate64)
	__NR_ftruncate64,
#endif
#if defined(__NR_futex)
	__NR_futex,
#endif
#if defined(__NR_futexrequeue)
	__NR_futexrequeue,
#endif
#if defined(__NR_futexwake)
	__NR_futexwake,
#endif
#if defined(__NR_futexwait)
	__NR_futexwait,
#endif
#if defined(__NR_futex_time64)
	__NR_futex_time64,
#endif
#if defined(__NR_futex_waitv)
	__NR_futex_waitv,
#endif
#if defined(__NR_futimens)
	__NR_futimens,
#endif
#if defined(__NR_futimesat)
	__NR_futimesat,
#endif
#if defined(__NR_get_kernel_syms)
	__NR_get_kernel_syms,
#endif
#if defined(__NR_get_mempolicy)
	__NR_get_mempolicy,
#endif
#if defined(__NR_get_robust_list)
	__NR_get_robust_list,
#endif
#if defined(__NR_get_thread_area)
	__NR_get_thread_area,
#endif
#if defined(__NR_get_tls)
	__NR_get_tls,
#endif
#if defined(__NR_gettls)
	__NR_gettls,
#endif
#if defined(__NR_getcpu)
	__NR_getcpu,
#endif
#if defined(__NR_getcwd)
	__NR_getcwd,
#endif
#if defined(__NR_getdents)
	__NR_getdents,
#endif
#if defined(__NR_getdents64)
	__NR_getdents64,
#endif
#if defined(__NR_getdomainname)
	__NR_getdomainname,
#endif
#if defined(__NR_getdtablesize)
	__NR_getdtablesize,
#endif
#if defined(__NR_getegid)
	__NR_getegid,
#endif
#if defined(__NR_getegid32)
	__NR_getegid32,
#endif
#if defined(__NR_geteuid)
	__NR_geteuid,
#endif
#if defined(__NR_geteuid32)
	__NR_geteuid32,
#endif
#if defined(__NR_getgid)
	__NR_getgid,
#endif
#if defined(__NR_getgid32)
	__NR_getgid32,
#endif
#if defined(__NR_getgroups)
	__NR_getgroups,
#endif
#if defined(__NR_getgroups32)
	__NR_getgroups32,
#endif
#if defined(__NR_gethostname)
	__NR_gethostname,
#endif
#if defined(__NR_getitimer)
	__NR_getitimer,
#endif
#if defined(__NR_getpagesize)
	__NR_getpagesize,
#endif
#if defined(__NR_getpeername)
	__NR_getpeername,
#endif
#if defined(__NR_getpgid)
	__NR_getpgid,
#endif
#if defined(__NR_getpgrp)
	__NR_getpgrp,
#endif
#if defined(__NR_getpid)
	__NR_getpid,
#endif
#if defined(__NR_getpmsg)
	__NR_getpmsg,
#endif
#if defined(__NR_getppid)
	__NR_getppid,
#endif
#if defined(__NR_getpriority)
	__NR_getpriority,
#endif
#if defined(__NR_getrandom)
	__NR_getrandom,
#endif
#if defined(__NR_getresgid)
	__NR_getresgid,
#endif
#if defined(__NR_getresgid32)
	__NR_getresgid32,
#endif
#if defined(__NR_getresuid)
	__NR_getresuid,
#endif
#if defined(__NR_getresuid32)
	__NR_getresuid32,
#endif
#if defined(__NR_getrlimit)
	__NR_getrlimit,
#endif
#if defined(__NR_getrusage)
	__NR_getrusage,
#endif
#if defined(__NR_getsid)
	__NR_getsid,
#endif
#if defined(__NR_getsockname)
	__NR_getsockname,
#endif
#if defined(__NR_getsockopt)
	__NR_getsockopt,
#endif
#if defined(__NR_gettid)
	__NR_gettid,
#endif
#if defined(__NR_gettimeofday)
	__NR_gettimeofday,
#endif
#if defined(__NR_getuid)
	__NR_getuid,
#endif
#if defined(__NR_getuid32)
	__NR_getuid32,
#endif
#if defined(__NR_getunwind)
	__NR_getunwind,
#endif
#if defined(__NR_getxattr)
	__NR_getxattr,
#endif
#if defined(__NR_getxattrat)
	__NR_getxattrat,
#endif
#if defined(__NR_getxgid)
	__NR_getxgid,
#endif
#if defined(__NR_getxpid)
	__NR_getxpid,
#endif
#if defined(__NR_getxuid)
	__NR_getxuid,
#endif
#if defined(__NR_gtty)
	__NR_gtty,
#endif
#if defined(__NR_idle)
	__NR_idle,
#endif
#if defined(__NR_init_module)
	__NR_init_module,
#endif
#if defined(__NR_inotify_add_watch)
	__NR_inotify_add_watch,
#endif
#if defined(__NR_inotify_init)
	__NR_inotify_init,
#endif
#if defined(__NR_inotify_init1)
	__NR_inotify_init1,
#endif
#if defined(__NR_inotify_rm_watch)
	__NR_inotify_rm_watch,
#endif
#if defined(__NR_io_cancel)
	__NR_io_cancel,
#endif
#if defined(__NR_io_destroy)
	__NR_io_destroy,
#endif
#if defined(__NR_io_getevents)
	__NR_io_getevents,
#endif
#if defined(__NR_io_pgetevents)
	__NR_io_pgetevents,
#endif
#if defined(__NR_io_pgetevents_time64)
	__NR_io_pgetevents_time64,
#endif
#if defined(__NR_io_setup)
	__NR_io_setup,
#endif
#if defined(__NR_io_submit)
	__NR_io_submit,
#endif
#if defined(__NR_io_uring_enter)
	__NR_io_uring_enter,
#endif
#if defined(__NR_io_uring_register)
	__NR_io_uring_register,
#endif
#if defined(__NR_io_uring_setup)
	__NR_io_uring_setup,
#endif
#if defined(__NR_ioctl)
	__NR_ioctl,
#endif
#if defined(__NR_ioperm)
	__NR_ioperm,
#endif
#if defined(__NR_iopl)
	__NR_iopl,
#endif
#if defined(__NR_ioprio_set)
	__NR_ioprio_set,
#endif
#if defined(__NR_ioprio_get)
	__NR_ioprio_get,
#endif
#if defined(__NR_ipc)
	__NR_ipc,
#endif
#if defined(__NR_kcmp)
	__NR_kcmp,
#endif
#if defined(__NR_kern_features)
	__NR_kern_features,
#endif
#if defined(__NR_kexec_file_load)
	__NR_kexec_file_load,
#endif
#if defined(__NR_kexec_load)
	__NR_kexec_load,
#endif
#if defined(__NR_keyctl)
	__NR_keyctl,
#endif
#if defined(__NR_kill)
	__NR_kill,
#endif
#if defined(__NR_landlock_add_rule)
	__NR_landlock_add_rule,
#endif
#if defined(__NR_landlock_create_ruleset)
	__NR_landlock_create_ruleset,
#endif
#if defined(__NR_landlock_restrict_self)
	__NR_landlock_restrict_self,
#endif
#if defined(__NR_lchown)
	__NR_lchown,
#endif
#if defined(__NR_lchown32)
	__NR_lchown32,
#endif
#if defined(__NR_lgetxattr)
	__NR_lgetxattr,
#endif
#if defined(__NR_link)
	__NR_link,
#endif
#if defined(__NR_linkat)
	__NR_linkat,
#endif
#if defined(__NR_Linux)
	__NR_Linux,
#endif
#if defined(__NR_Linux_syscalls)
	__NR_Linux_syscalls,
#endif
#if defined(__NR_listen)
	__NR_listen,
#endif
#if defined(__NR_listmount)
	__NR_listmount,
#endif
#if defined(__NR_listxattr)
	__NR_listxattr,
#endif
#if defined(__NR_listxattrat)
	__NR_listxattrat,
#endif
#if defined(__NR_llistxattr)
	__NR_llistxattr,
#endif
#if defined(__NR_llseek)
	__NR_llseek,
#endif
#if defined(__NR__llseek)
	__NR__llseek,
#endif
#if defined(__NR_lock)
	__NR_lock,
#endif
#if defined(__NR_lookup_dcookie)
	__NR_lookup_dcookie,
#endif
#if defined(__NR_lremovexattr)
	__NR_lremovexattr,
#endif
#if defined(__NR_lseek)
	__NR_lseek,
#endif
#if defined(__NR_lsetxattr)
	__NR_lsetxattr,
#endif
#if defined(__NR_lstat)
	__NR_lstat,
#endif
#if defined(__NR_lstat64)
	__NR_lstat64,
#endif
#if defined(__NR_lsm_get_self_attr)
	__NR_lsm_get_self_attr,
#endif
#if defined(__NR_lsm_list_modules)
	__NR_lsm_list_modules,
#endif
#if defined(__NR_lsm_set_self_attr)
	__NR_lsm_set_self_attr,
#endif
#if defined(__NR_lws_entries)
	__NR_lws_entries,
#endif
#if defined(__NR_madvise)
	__NR_madvise,
#endif
#if defined(__NR_madvise1)
	__NR_madvise1,
#endif
#if defined(__NR_map_shadow_stack)
	__NR_map_shadow_stack,
#endif
#if defined(__NR_mbind)
	__NR_mbind,
#endif
#if defined(__NR_membarrier)
	__NR_membarrier,
#endif
#if defined(__NR_memfd_create)
	__NR_memfd_create,
#endif
#if defined(__NR_memfd_secret)
	__NR_memfd_secret,
#endif
#if defined(__NR_memory_ordering)
	__NR_memory_ordering,
#endif
#if defined(__NR_metag_get_tls)
	__NR_metag_get_tls,
#endif
#if defined(__NR_metag_set_fpu_flags)
	__NR_metag_set_fpu_flags,
#endif
#if defined(__NR_metag_setglobalbit)
	__NR_metag_setglobalbit,
#endif
#if defined(__NR_metag_set_tls)
	__NR_metag_set_tls,
#endif
#if defined(__NR_migrate_pages)
	__NR_migrate_pages,
#endif
#if defined(__NR_mincore)
	__NR_mincore,
#endif
#if defined(__NR_mkdir)
	__NR_mkdir,
#endif
#if defined(__NR_mkdirat)
	__NR_mkdirat,
#endif
#if defined(__NR_mknod)
	__NR_mknod,
#endif
#if defined(__NR_mknodat)
	__NR_mknodat,
#endif
#if defined(__NR_mlock)
	__NR_mlock,
#endif
#if defined(__NR_mlock2)
	__NR_mlock2,
#endif
#if defined(__NR_mlockall)
	__NR_mlockall,
#endif
#if defined(__NR_mmap)
	__NR_mmap,
#endif
#if defined(__NR_mmap2)
	__NR_mmap2,
#endif
#if defined(__NR_mmap_pgoff)
	__NR_mmap_pgoff,
#endif
#if defined(__NR_modify_ldt)
	__NR_modify_ldt,
#endif
#if defined(__NR_mount)
	__NR_mount,
#endif
#if defined(__NR_mount_setattr)
	__NR_mount_setattr,
#endif
#if defined(__NR_move_mount)
	__NR_move_mount,
#endif
#if defined(__NR_move_pages)
	__NR_move_pages,
#endif
#if defined(__NR_mprotect)
	__NR_mprotect,
#endif
#if defined(__NR_mpx)
	__NR_mpx,
#endif
#if defined(__NR_mq_close)
	__NR_mq_close,
#endif
#if defined(__NR_mq_getsetattr)
	__NR_mq_getsetattr,
#endif
#if defined(__NR_mq_notify)
	__NR_mq_notify,
#endif
#if defined(__NR_mq_open)
	__NR_mq_open,
#endif
#if defined(__NR_mq_receive)
	__NR_mq_receive,
#endif
#if defined(__NR_mq_send)
	__NR_mq_send,
#endif
#if defined(__NR_mq_timedreceive)
	__NR_mq_timedreceive,
#endif
#if defined(__NR_mq_timedreceive_time64)
	__NR_mq_timedreceive_time64,
#endif
#if defined(__NR_mq_timedsend)
	__NR_mq_timedsend,
#endif
#if defined(__NR_mq_timedsend_time64)
	__NR_mq_timedsend_time64,
#endif
#if defined(__NR_mq_unlink)
	__NR_mq_unlink,
#endif
#if defined(__NR_mremap)
	__NR_mremap,
#endif
#if defined(__NR_mseal)
	__NR_mseal,
#endif
#if defined(__NR_msgctl)
	__NR_msgctl,
#endif
#if defined(__NR_msgget)
	__NR_msgget,
#endif
#if defined(__NR_msgrcv)
	__NR_msgrcv,
#endif
#if defined(__NR_msgsnd)
	__NR_msgsnd,
#endif
#if defined(__NR_msync)
	__NR_msync,
#endif
#if defined(__NR_multiplexer)
	__NR_multiplexer,
#endif
#if defined(__NR_munlock)
	__NR_munlock,
#endif
#if defined(__NR_munlockall)
	__NR_munlockall,
#endif
#if defined(__NR_munmap)
	__NR_munmap,
#endif
#if defined(__NR_N32_Linux)
	__NR_N32_Linux,
#endif
#if defined(__NR_N32_Linux_syscalls)
	__NR_N32_Linux_syscalls,
#endif
#if defined(__NR_N32_restart_syscall)
	__NR_N32_restart_syscall,
#endif
#if defined(__NR_name_to_handle_at)
	__NR_name_to_handle_at,
#endif
#if defined(__NR_nanosleep)
	__NR_nanosleep,
#endif
#if defined(__NR_newfstat)
	__NR_newfstat,
#endif
#if defined(__NR_newfstatat)
	__NR_newfstatat,
#endif
#if defined(__NR_newlstat)
	__NR_newlstat,
#endif
#if defined(__NR__newselect)
	__NR__newselect,
#endif
#if defined(__NR_newstat)
	__NR_newstat,
#endif
#if defined(__NR_newuname)
	__NR_newuname,
#endif
#if defined(__NR_nfsservctl)
	__NR_nfsservctl,
#endif
#if defined(__NR_nice)
	__NR_nice,
#endif
#if defined(__NR_ni_syscall)
	__NR_ni_syscall,
#endif
#if defined(__NR_old_adjtimex)
	__NR_old_adjtimex,
#endif
#if defined(__NR_olddebug_setcontext)
	__NR_olddebug_setcontext,
#endif
#if defined(__NR_old_getpagesize)
	__NR_old_getpagesize,
#endif
#if defined(__NR_oldfstat)
	__NR_oldfstat,
#endif
#if defined(__NR_old_getrlimit)
	__NR_old_getrlimit,
#endif
#if defined(__NR_oldlstat)
	__NR_oldlstat,
#endif
#if defined(__NR_oldolduname)
	__NR_oldolduname,
#endif
#if defined(__NR_oldstat)
	__NR_oldstat,
#endif
#if defined(__NR_oldumount)
	__NR_oldumount,
#endif
#if defined(__NR_olduname)
	__NR_olduname,
#endif
#if defined(__NR_oldwait4)
	__NR_oldwait4,
#endif
#if defined(__NR_open)
	__NR_open,
#endif
#if defined(__NR_open_by_handle_at)
	__NR_open_by_handle_at,
#endif
#if defined(__NR_open_tree)
	__NR_open_tree,
#endif
#if defined(__NR_open_tree_attr)
	__NR_open_tree_attr,
#endif
#if defined(__NR_openat)
	__NR_openat,
#endif
#if defined(__NR_openat2)
	__NR_openat2,
#endif
#if defined(__NR_or1k_atomic)
	__NR_or1k_atomic,
#endif
#if defined(__NR_osf_adjtime)
	__NR_osf_adjtime,
#endif
#if defined(__NR_osf_afs_syscall)
	__NR_osf_afs_syscall,
#endif
#if defined(__NR_osf_alt_plock)
	__NR_osf_alt_plock,
#endif
#if defined(__NR_osf_alt_setsid)
	__NR_osf_alt_setsid,
#endif
#if defined(__NR_osf_alt_sigpending)
	__NR_osf_alt_sigpending,
#endif
#if defined(__NR_osf_asynch_daemon)
	__NR_osf_asynch_daemon,
#endif
#if defined(__NR_osf_audcntl)
	__NR_osf_audcntl,
#endif
#if defined(__NR_osf_audgen)
	__NR_osf_audgen,
#endif
#if defined(__NR_osf_chflags)
	__NR_osf_chflags,
#endif
#if defined(__NR_osf_execve)
	__NR_osf_execve,
#endif
#if defined(__NR_osf_exportfs)
	__NR_osf_exportfs,
#endif
#if defined(__NR_osf_fchflags)
	__NR_osf_fchflags,
#endif
#if defined(__NR_osf_fdatasync)
	__NR_osf_fdatasync,
#endif
#if defined(__NR_osf_fpathconf)
	__NR_osf_fpathconf,
#endif
#if defined(__NR_osf_fstat)
	__NR_osf_fstat,
#endif
#if defined(__NR_osf_fstatfs64)
	__NR_osf_fstatfs64,
#endif
#if defined(__NR_osf_fstatfs)
	__NR_osf_fstatfs,
#endif
#if defined(__NR_osf_fuser)
	__NR_osf_fuser,
#endif
#if defined(__NR_osf_getaddressconf)
	__NR_osf_getaddressconf,
#endif
#if defined(__NR_osf_getdirentries)
	__NR_osf_getdirentries,
#endif
#if defined(__NR_osf_getdomainname)
	__NR_osf_getdomainname,
#endif
#if defined(__NR_osf_getfh)
	__NR_osf_getfh,
#endif
#if defined(__NR_osf_getfsstat)
	__NR_osf_getfsstat,
#endif
#if defined(__NR_osf_gethostid)
	__NR_osf_gethostid,
#endif
#if defined(__NR_osf_getitimer)
	__NR_osf_getitimer,
#endif
#if defined(__NR_osf_getlogin)
	__NR_osf_getlogin,
#endif
#if defined(__NR_osf_getmnt)
	__NR_osf_getmnt,
#endif
#if defined(__NR_osf_getrusage)
	__NR_osf_getrusage,
#endif
#if defined(__NR_osf_getsysinfo)
	__NR_osf_getsysinfo,
#endif
#if defined(__NR_osf_gettimeofday)
	__NR_osf_gettimeofday,
#endif
#if defined(__NR_osf_kloadcall)
	__NR_osf_kloadcall,
#endif
#if defined(__NR_osf_kmodcall)
	__NR_osf_kmodcall,
#endif
#if defined(__NR_osf_lstat)
	__NR_osf_lstat,
#endif
#if defined(__NR_osf_memcntl)
	__NR_osf_memcntl,
#endif
#if defined(__NR_osf_mincore)
	__NR_osf_mincore,
#endif
#if defined(__NR_osf_mount)
	__NR_osf_mount,
#endif
#if defined(__NR_osf_mremap)
	__NR_osf_mremap,
#endif
#if defined(__NR_osf_msfs_syscall)
	__NR_osf_msfs_syscall,
#endif
#if defined(__NR_osf_msleep)
	__NR_osf_msleep,
#endif
#if defined(__NR_osf_mvalid)
	__NR_osf_mvalid,
#endif
#if defined(__NR_osf_mwakeup)
	__NR_osf_mwakeup,
#endif
#if defined(__NR_osf_naccept)
	__NR_osf_naccept,
#endif
#if defined(__NR_osf_nfssvc)
	__NR_osf_nfssvc,
#endif
#if defined(__NR_osf_ngetpeername)
	__NR_osf_ngetpeername,
#endif
#if defined(__NR_osf_ngetsockname)
	__NR_osf_ngetsockname,
#endif
#if defined(__NR_osf_nrecvfrom)
	__NR_osf_nrecvfrom,
#endif
#if defined(__NR_osf_nrecvmsg)
	__NR_osf_nrecvmsg,
#endif
#if defined(__NR_osf_nsendmsg)
	__NR_osf_nsendmsg,
#endif
#if defined(__NR_osf_ntp_adjtime)
	__NR_osf_ntp_adjtime,
#endif
#if defined(__NR_osf_ntp_gettime)
	__NR_osf_ntp_gettime,
#endif
#if defined(__NR_osf_old_creat)
	__NR_osf_old_creat,
#endif
#if defined(__NR_osf_old_fstat)
	__NR_osf_old_fstat,
#endif
#if defined(__NR_osf_old_getpgrp)
	__NR_osf_old_getpgrp,
#endif
#if defined(__NR_osf_old_killpg)
	__NR_osf_old_killpg,
#endif
#if defined(__NR_osf_old_lstat)
	__NR_osf_old_lstat,
#endif
#if defined(__NR_osf_old_open)
	__NR_osf_old_open,
#endif
#if defined(__NR_osf_old_sigaction)
	__NR_osf_old_sigaction,
#endif
#if defined(__NR_osf_old_sigblock)
	__NR_osf_old_sigblock,
#endif
#if defined(__NR_osf_old_sigreturn)
	__NR_osf_old_sigreturn,
#endif
#if defined(__NR_osf_old_sigsetmask)
	__NR_osf_old_sigsetmask,
#endif
#if defined(__NR_osf_old_sigvec)
	__NR_osf_old_sigvec,
#endif
#if defined(__NR_osf_old_stat)
	__NR_osf_old_stat,
#endif
#if defined(__NR_osf_old_vadvise)
	__NR_osf_old_vadvise,
#endif
#if defined(__NR_osf_old_vtrace)
	__NR_osf_old_vtrace,
#endif
#if defined(__NR_osf_old_wait)
	__NR_osf_old_wait,
#endif
#if defined(__NR_osf_oldquota)
	__NR_osf_oldquota,
#endif
#if defined(__NR_osf_pathconf)
	__NR_osf_pathconf,
#endif
#if defined(__NR_osf_pid_block)
	__NR_osf_pid_block,
#endif
#if defined(__NR_osf_pid_unblock)
	__NR_osf_pid_unblock,
#endif
#if defined(__NR_osf_plock)
	__NR_osf_plock,
#endif
#if defined(__NR_osf_priocntlset)
	__NR_osf_priocntlset,
#endif
#if defined(__NR_osf_profil)
	__NR_osf_profil,
#endif
#if defined(__NR_osf_proplist_syscall)
	__NR_osf_proplist_syscall,
#endif
#if defined(__NR_osf_reboot)
	__NR_osf_reboot,
#endif
#if defined(__NR_osf_revoke)
	__NR_osf_revoke,
#endif
#if defined(__NR_osf_sbrk)
	__NR_osf_sbrk,
#endif
#if defined(__NR_osf_security)
	__NR_osf_security,
#endif
#if defined(__NR_osf_select)
	__NR_osf_select,
#endif
#if defined(__NR_osf_set_program_attributes)
	__NR_osf_set_program_attributes,
#endif
#if defined(__NR_osf_set_speculative)
	__NR_osf_set_speculative,
#endif
#if defined(__NR_osf_sethostid)
	__NR_osf_sethostid,
#endif
#if defined(__NR_osf_setitimer)
	__NR_osf_setitimer,
#endif
#if defined(__NR_osf_setlogin)
	__NR_osf_setlogin,
#endif
#if defined(__NR_osf_setsysinfo)
	__NR_osf_setsysinfo,
#endif
#if defined(__NR_osf_settimeofday)
	__NR_osf_settimeofday,
#endif
#if defined(__NR_osf_shmat)
	__NR_osf_shmat,
#endif
#if defined(__NR_osf_signal)
	__NR_osf_signal,
#endif
#if defined(__NR_osf_sigprocmask)
	__NR_osf_sigprocmask,
#endif
#if defined(__NR_osf_sigsendset)
	__NR_osf_sigsendset,
#endif
#if defined(__NR_osf_sigstack)
	__NR_osf_sigstack,
#endif
#if defined(__NR_osf_sigwaitprim)
	__NR_osf_sigwaitprim,
#endif
#if defined(__NR_osf_sstk)
	__NR_osf_sstk,
#endif
#if defined(__NR_osf_stat)
	__NR_osf_stat,
#endif
#if defined(__NR_osf_statfs64)
	__NR_osf_statfs64,
#endif
#if defined(__NR_osf_statfs)
	__NR_osf_statfs,
#endif
#if defined(__NR_osf_subsys_info)
	__NR_osf_subsys_info,
#endif
#if defined(__NR_osf_swapctl)
	__NR_osf_swapctl,
#endif
#if defined(__NR_osf_swapon)
	__NR_osf_swapon,
#endif
#if defined(__NR_osf_syscall)
	__NR_osf_syscall,
#endif
#if defined(__NR_osf_sysinfo)
	__NR_osf_sysinfo,
#endif
#if defined(__NR_osf_table)
	__NR_osf_table,
#endif
#if defined(__NR_osf_uadmin)
	__NR_osf_uadmin,
#endif
#if defined(__NR_osf_usleep_thread)
	__NR_osf_usleep_thread,
#endif
#if defined(__NR_osf_uswitch)
	__NR_osf_uswitch,
#endif
#if defined(__NR_osf_utc_adjtime)
	__NR_osf_utc_adjtime,
#endif
#if defined(__NR_osf_utc_gettime)
	__NR_osf_utc_gettime,
#endif
#if defined(__NR_osf_utimes)
	__NR_osf_utimes,
#endif
#if defined(__NR_osf_utsname)
	__NR_osf_utsname,
#endif
#if defined(__NR_osf_wait4)
	__NR_osf_wait4,
#endif
#if defined(__NR_osf_waitid)
	__NR_osf_waitid,
#endif
#if defined(__NR_pause)
	__NR_pause,
#endif
#if defined(__NR_pciconfig_iobase)
	__NR_pciconfig_iobase,
#endif
#if defined(__NR_pciconfig_read)
	__NR_pciconfig_read,
#endif
#if defined(__NR_pciconfig_write)
	__NR_pciconfig_write,
#endif
#if defined(__NR_perf_event_open)
	__NR_perf_event_open,
#endif
#if defined(__NR_perfctr)
	__NR_perfctr,
#endif
#if defined(__NR_perfmonctl)
	__NR_perfmonctl,
#endif
#if defined(__NR_personality)
	__NR_personality,
#endif
#if defined(__NR_pidfd_getfd)
	__NR_pidfd_getfd,
#endif
#if defined(__NR_pidfd_open)
	__NR_pidfd_open,
#endif
#if defined(__NR_pidfd_send_signal)
	__NR_pidfd_send_signal,
#endif
#if defined(__NR_pipe)
	__NR_pipe,
#endif
#if defined(__NR_pipe2)
	__NR_pipe2,
#endif
#if defined(__NR_pivot_root)
	__NR_pivot_root,
#endif
#if defined(__NR_pkey_alloc)
	__NR_pkey_alloc,
#endif
#if defined(__NR_pkey_free)
	__NR_pkey_free,
#endif
#if defined(__NR_pkey_get)
	__NR_pkey_get,
#endif
#if defined(__NR_pkey_mprotect)
	__NR_pkey_mprotect,
#endif
#if defined(__NR_pkey_set)
	__NR_pkey_set,
#endif
#if defined(__NR_poll)
	__NR_poll,
#endif
#if defined(__NR_ppoll)
	__NR_ppoll,
#endif
#if defined(__NR_ppoll_time64)
	__NR_ppoll_time64,
#endif
#if defined(__NR_prctl)
	__NR_prctl,
#endif
#if defined(__NR_pread)
	__NR_pread,
#endif
#if defined(__NR_pread64)
	__NR_pread64,
#endif
#if defined(__NR_preadv)
	__NR_preadv,
#endif
#if defined(__NR_preadv2)
	__NR_preadv2,
#endif
#if defined(__NR_prlimit)
	__NR_prlimit,
#endif
#if defined(__NR_prlimit64)
	__NR_prlimit64,
#endif
#if defined(__NR_process_madvise)
	__NR_process_madvise,
#endif
#if defined(__NR_process_mrelease)
	__NR_process_mrelease,
#endif
#if defined(__NR_process_vm_readv)
	__NR_process_vm_readv,
#endif
#if defined(__NR_process_vm_writev)
	__NR_process_vm_writev,
#endif
#if defined(__NR_prof)
	__NR_prof,
#endif
#if defined(__NR_profil)
	__NR_profil,
#endif
#if defined(__NR_pselect6)
	__NR_pselect6,
#endif
#if defined(__NR_pselect6_time64)
	__NR_pselect6_time64,
#endif
#if defined(__NR_ptrace)
	__NR_ptrace,
#endif
#if defined(__NR_putpmsg)
	__NR_putpmsg,
#endif
#if defined(__NR_pwrite)
	__NR_pwrite,
#endif
#if defined(__NR_pwrite64)
	__NR_pwrite64,
#endif
#if defined(__NR_pwritev)
	__NR_pwritev,
#endif
#if defined(__NR_pwritev2)
	__NR_pwritev2,
#endif
#if defined(__NR_query_module)
	__NR_query_module,
#endif
#if defined(__NR_quotactl)
	__NR_quotactl,
#endif
#if defined(__NR_quotactl_fd)
	__NR_quotactl_fd,
#endif
#if defined(__NR_read)
	__NR_read,
#endif
#if defined(__NR_readahead)
	__NR_readahead,
#endif
#if defined(__NR_readdir)
	__NR_readdir,
#endif
#if defined(__NR_readlink)
	__NR_readlink,
#endif
#if defined(__NR_readlinkat)
	__NR_readlinkat,
#endif
#if defined(__NR_readv)
	__NR_readv,
#endif
#if defined(__NR_reboot)
	__NR_reboot,
#endif
#if defined(__NR_recv)
	__NR_recv,
#endif
#if defined(__NR_recvfrom)
	__NR_recvfrom,
#endif
#if defined(__NR_recvmmsg)
	__NR_recvmmsg,
#endif
#if defined(__NR_recvmmsg_time64)
	__NR_recvmmsg_time64,
#endif
#if defined(__NR_recvmsg)
	__NR_recvmsg,
#endif
#if defined(__NR_remap_file_pages)
	__NR_remap_file_pages,
#endif
#if defined(__NR_removexattr)
	__NR_removexattr,
#endif
#if defined(__NR_removexattrat)
	__NR_removexattrat,
#endif
#if defined(__NR_rename)
	__NR_rename,
#endif
#if defined(__NR_renameat)
	__NR_renameat,
#endif
#if defined(__NR_renameat2)
	__NR_renameat2,
#endif
#if defined(__NR_request_key)
	__NR_request_key,
#endif
#if defined(__NR_reserved152)
	__NR_reserved152,
#endif
#if defined(__NR_reserved153)
	__NR_reserved153,
#endif
#if defined(__NR_reserved177)
	__NR_reserved177,
#endif
#if defined(__NR_reserved193)
	__NR_reserved193,
#endif
#if defined(__NR_reserved221)
	__NR_reserved221,
#endif
#if defined(__NR_reserved253)
	__NR_reserved253,
#endif
#if defined(__NR_reserved82)
	__NR_reserved82,
#endif
#if defined(__NR_restart_syscall)
	__NR_restart_syscall,
#endif
#if defined(__NR_riscv_flush_icache)
	__NR_riscv_flush_icache,
#endif
#if defined(__NR_rmdir)
	__NR_rmdir,
#endif
#if defined(__NR_rseq)
	__NR_rseq,
#endif
#if defined(__NR_rt_sigaction)
	__NR_rt_sigaction,
#endif
#if defined(__NR_rt_sigpending)
	__NR_rt_sigpending,
#endif
#if defined(__NR_rt_sigprocmask)
	__NR_rt_sigprocmask,
#endif
#if defined(__NR_rt_sigqueueinfo)
	__NR_rt_sigqueueinfo,
#endif
#if defined(__NR_rt_sigreturn)
	__NR_rt_sigreturn,
#endif
#if defined(__NR_rt_sigsuspend)
	__NR_rt_sigsuspend,
#endif
#if defined(__NR_rt_sigtimedwait)
	__NR_rt_sigtimedwait,
#endif
#if defined(__NR_rt_sigtimedwait_time64)
	__NR_rt_sigtimedwait_time64,
#endif
#if defined(__NR_rt_tgsigqueueinfo)
	__NR_rt_tgsigqueueinfo,
#endif
#if defined(__NR_rtas)
	__NR_rtas,
#endif
#if defined(__NR_s390_guarded_storage)
	__NR_s390_guarded_storage,
#endif
#if defined(__NR_s390_pci_mmio_read)
	__NR_s390_pci_mmio_read,
#endif
#if defined(__NR_s390_pci_mmio_write)
	__NR_s390_pci_mmio_write,
#endif
#if defined(__NR_s390_runtime_instr)
	__NR_s390_runtime_instr,
#endif
#if defined(__NR_s390_sthyi)
	__NR_s390_sthyi,
#endif
#if defined(__NR_sched_get_affinity)
	__NR_sched_get_affinity,
#endif
#if defined(__NR_sched_get_priority_max)
	__NR_sched_get_priority_max,
#endif
#if defined(__NR_sched_get_priority_min)
	__NR_sched_get_priority_min,
#endif
#if defined(__NR_sched_getaffinity)
	__NR_sched_getaffinity,
#endif
#if defined(__NR_sched_getattr)
	__NR_sched_getattr,
#endif
#if defined(__NR_sched_getparam)
	__NR_sched_getparam,
#endif
#if defined(__NR_sched_getscheduler)
	__NR_sched_getscheduler,
#endif
#if defined(__NR_sched_rr_get_interval)
	__NR_sched_rr_get_interval,
#endif
#if defined(__NR_sched_rr_stress_get_interval)
	__NR_sched_rr_stress_get_interval,
#endif
#if defined(__NR_sched_set_affinity)
	__NR_sched_set_affinity,
#endif
#if defined(__NR_sched_setaffinity)
	__NR_sched_setaffinity,
#endif
#if defined(__NR_sched_setattr)
	__NR_sched_setattr,
#endif
#if defined(__NR_sched_setparam)
	__NR_sched_setparam,
#endif
#if defined(__NR_sched_setscheduler)
	__NR_sched_setscheduler,
#endif
#if defined(__NR_sched_yield)
	__NR_sched_yield,
#endif
#if defined(__NR_seccomp)
	__NR_seccomp,
#endif
#if defined(__NR_seccomp_exit)
	__NR_seccomp_exit,
#endif
#if defined(__NR_seccomp_exit_32)
	__NR_seccomp_exit_32,
#endif
#if defined(__NR_seccomp_read)
	__NR_seccomp_read,
#endif
#if defined(__NR_seccomp_read_32)
	__NR_seccomp_read_32,
#endif
#if defined(__NR_seccomp_sigreturn)
	__NR_seccomp_sigreturn,
#endif
#if defined(__NR_seccomp_sigreturn_32)
	__NR_seccomp_sigreturn_32,
#endif
#if defined(__NR_seccomp_write)
	__NR_seccomp_write,
#endif
#if defined(__NR_seccomp_write_32)
	__NR_seccomp_write_32,
#endif
#if defined(__NR_security)
	__NR_security,
#endif
#if defined(__NR_select)
	__NR_select,
#endif
#if defined(__NR_semctl)
	__NR_semctl,
#endif
#if defined(__NR_semget)
	__NR_semget,
#endif
#if defined(__NR_semop)
	__NR_semop,
#endif
#if defined(__NR_semtimedop)
	__NR_semtimedop,
#endif
#if defined(__NR_semtimedop_time64)
	__NR_semtimedop_time64,
#endif
#if defined(__NR_send)
	__NR_send,
#endif
#if defined(__NR_sendfile)
	__NR_sendfile,
#endif
#if defined(__NR_sendfile64)
	__NR_sendfile64,
#endif
#if defined(__NR_sendmmsg)
	__NR_sendmmsg,
#endif
#if defined(__NR_sendmsg)
	__NR_sendmsg,
#endif
#if defined(__NR_sendto)
	__NR_sendto,
#endif
#if defined(__NR_set_mempolicy)
	__NR_set_mempolicy,
#endif
#if defined(__NR_set_mempolicy_home_node)
	__NR_set_mempolicy_home_node,
#endif
#if defined(__NR_set_robust_list)
	__NR_set_robust_list,
#endif
#if defined(__NR_set_thread_area)
	__NR_set_thread_area,
#endif
#if defined(__NR_set_tid_address)
	__NR_set_tid_address,
#endif
#if defined(__NR_set_tls)
	__NR_set_tls,
#endif
#if defined(__NR_setdomainname)
	__NR_setdomainname,
#endif
#if defined(__NR_setfsgid)
	__NR_setfsgid,
#endif
#if defined(__NR_setfsgid32)
	__NR_setfsgid32,
#endif
#if defined(__NR_setfsuid)
	__NR_setfsuid,
#endif
#if defined(__NR_setfsuid32)
	__NR_setfsuid32,
#endif
#if defined(__NR_setgid)
	__NR_setgid,
#endif
#if defined(__NR_setgid32)
	__NR_setgid32,
#endif
#if defined(__NR_setgroups)
	__NR_setgroups,
#endif
#if defined(__NR_setgroups32)
	__NR_setgroups32,
#endif
#if defined(__NR_sethae)
	__NR_sethae,
#endif
#if defined(__NR_sethostname)
	__NR_sethostname,
#endif
#if defined(__NR_setitimer)
	__NR_setitimer,
#endif
#if defined(__NR_setmntent)
	__NR_setmntent,
#endif
#if defined(__NR_setns)
	__NR_setns,
#endif
#if defined(__NR_setpgid)
	__NR_setpgid,
#endif
#if defined(__NR_setpgrp)
	__NR_setpgrp,
#endif
#if defined(__NR_setpriority)
	__NR_setpriority,
#endif
#if defined(__NR_setregid)
	__NR_setregid,
#endif
#if defined(__NR_setregid32)
	__NR_setregid32,
#endif
#if defined(__NR_setresgid)
	__NR_setresgid,
#endif
#if defined(__NR_setresgid32)
	__NR_setresgid32,
#endif
#if defined(__NR_setresuid)
	__NR_setresuid,
#endif
#if defined(__NR_setresuid32)
	__NR_setresuid32,
#endif
#if defined(__NR_setreuid)
	__NR_setreuid,
#endif
#if defined(__NR_setreuid32)
	__NR_setreuid32,
#endif
#if defined(__NR_setrlimit)
	__NR_setrlimit,
#endif
#if defined(__NR_setsid)
	__NR_setsid,
#endif
#if defined(__NR_setsockopt)
	__NR_setsockopt,
#endif
#if defined(__NR_settimeofday)
	__NR_settimeofday,
#endif
#if defined(__NR_setuid)
	__NR_setuid,
#endif
#if defined(__NR_setuid32)
	__NR_setuid32,
#endif
#if defined(__NR_setxattr)
	__NR_setxattr,
#endif
#if defined(__NR_setxattrat)
	__NR_setxattr,
#endif
#if defined(__NR_sgetmask)
	__NR_sgetmask,
#endif
#if defined(__NR_shmat)
	__NR_shmat,
#endif
#if defined(__NR_shmctl)
	__NR_shmctl,
#endif
#if defined(__NR_shmdt)
	__NR_shmdt,
#endif
#if defined(__NR_shmget)
	__NR_shmget,
#endif
#if defined(__NR_shutdown)
	__NR_shutdown,
#endif
#if defined(__NR_sigaction)
	__NR_sigaction,
#endif
#if defined(__NR_sigaltstack)
	__NR_sigaltstack,
#endif
#if defined(__NR_signal)
	__NR_signal,
#endif
#if defined(__NR_signalfd)
	__NR_signalfd,
#endif
#if defined(__NR_signalfd4)
	__NR_signalfd4,
#endif
#if defined(__NR_sigpending)
	__NR_sigpending,
#endif
#if defined(__NR_sigprocmask)
	__NR_sigprocmask,
#endif
#if defined(__NR_sigreturn)
	__NR_sigreturn,
#endif
#if defined(__NR_sigsuspend)
	__NR_sigsuspend,
#endif
#if defined(__NR_sigtimedwait)
	__NR_sigtimedwait,
#endif
#if defined(__NR_sigwaitinfo)
	__NR_sigwaitinfo,
#endif
#if defined(__NR_socket)
	__NR_socket,
#endif
#if defined(__NR_socketcall)
	__NR_socketcall,
#endif
#if defined(__NR_socketpair)
	__NR_socketpair,
#endif
#if defined(__NR_spill)
	__NR_spill,
#endif
#if defined(__NR_splice)
	__NR_splice,
#endif
#if defined(__NR_spu_create)
	__NR_spu_create,
#endif
#if defined(__NR_spu_run)
	__NR_spu_run,
#endif
#if defined(__NR_sram_alloc)
	__NR_sram_alloc,
#endif
#if defined(__NR_sram_free)
	__NR_sram_free,
#endif
#if defined(__NR_ssetmask)
	__NR_ssetmask,
#endif
#if defined(__NR_stat)
	__NR_stat,
#endif
#if defined(__NR_stat64)
	__NR_stat64,
#endif
#if defined(__NR_statfs)
	__NR_statfs,
#endif
#if defined(__NR_statfs64)
	__NR_statfs64,
#endif
#if defined(__NR_statmount)
	__NR_statmount,
#endif
#if defined(__NR_statx)
	__NR_statx,
#endif
#if defined(__NR_stime)
	__NR_stime,
#endif
#if defined(__NR_stty)
	__NR_stty,
#endif
#if defined(__NR_subpage_prot)
	__NR_subpage_prot,
#endif
#if defined(__NR_swapcontext)
	__NR_swapcontext,
#endif
#if defined(__NR_swapoff)
	__NR_swapoff,
#endif
#if defined(__NR_swapon)
	__NR_swapon,
#endif
#if defined(__NR_switch_endian)
	__NR_switch_endian,
#endif
#if defined(__NR_symlink)
	__NR_symlink,
#endif
#if defined(__NR_symlinkat)
	__NR_symlinkat,
#endif
#if defined(__NR_sync)
	__NR_sync,
#endif
#if defined(__NR_sync_file_range)
	__NR_sync_file_range,
#endif
#if defined(__NR_sync_file_range2)
	__NR_sync_file_range2,
#endif
#if defined(__NR_syncfs)
	__NR_syncfs,
#endif
#if defined(__NR_sys_debug_setcontext)
	__NR_sys_debug_setcontext,
#endif
#if defined(__NR_sys_epoll_create)
	__NR_sys_epoll_create,
#endif
#if defined(__NR_sys_epoll_ctl)
	__NR_sys_epoll_ctl,
#endif
#if defined(__NR_sys_epoll_wait)
	__NR_sys_epoll_wait,
#endif
#if defined(__NR_syscall)
	__NR_syscall,
#endif
#if defined(__NR_SYSCALL_BASE)
	__NR_SYSCALL_BASE,
#endif
#if defined(__NR_syscall_compat_max)
	__NR_syscall_compat_max,
#endif
#if defined(__NR_syscall_count)
	__NR_syscall_count,
#endif
#if defined(__NR_syscalls)
	__NR_syscalls,
#endif
#if defined(__NR_sysctl)
	__NR_sysctl,
#endif
#if defined(__NR__sysctl)
	__NR__sysctl,
#endif
#if defined(__NR_sysfs)
	__NR_sysfs,
#endif
#if defined(__NR_sysinfo)
	__NR_sysinfo,
#endif
#if defined(__NR_syslog)
	__NR_syslog,
#endif
#if defined(__NR_sysmips)
	__NR_sysmips,
#endif
#if defined(__NR_tas)
	__NR_tas,
#endif
#if defined(__NR_tee)
	__NR_tee,
#endif
#if defined(__NR_tgkill)
	__NR_tgkill,
#endif
#if defined(__NR_time)
	__NR_time,
#endif
#if defined(__NR_timer_create)
	__NR_timer_create,
#endif
#if defined(__NR_timer_delete)
	__NR_timer_delete,
#endif
#if defined(__NR_timer_getoverrun)
	__NR_timer_getoverrun,
#endif
#if defined(__NR_timer_gettime)
	__NR_timer_gettime,
#endif
#if defined(__NR_timer_gettime64)
	__NR_timer_gettime64,
#endif
#if defined(__NR_timer_settime)
	__NR_timer_settime,
#endif
#if defined(__NR_timer_settime64)
	__NR_timer_settime64,
#endif
#if defined(__NR_timerfd)
	__NR_timerfd,
#endif
#if defined(__NR_timerfd_create)
	__NR_timerfd_create,
#endif
#if defined(__NR_timerfd_gettime)
	__NR_timerfd_gettime,
#endif
#if defined(__NR_timerfd_gettime64)
	__NR_timerfd_gettime64,
#endif
#if defined(__NR_timerfd_settime)
	__NR_timerfd_settime,
#endif
#if defined(__NR_timerfd_settime64)
	__NR_timerfd_settime64,
#endif
#if defined(__NR_times)
	__NR_times,
#endif
#if defined(__NR_tkill)
	__NR_tkill,
#endif
#if defined(__NR_truncate)
	__NR_truncate,
#endif
#if defined(__NR_truncate64)
	__NR_truncate64,
#endif
#if defined(__NR_tuxcall)
	__NR_tuxcall,
#endif
#if defined(__NR_udftrap)
	__NR_udftrap,
#endif
#if defined(__NR_ugetrlimit)
	__NR_ugetrlimit,
#endif
#if defined(__NR_ulimit)
	__NR_ulimit,
#endif
#if defined(__NR_umask)
	__NR_umask,
#endif
#if defined(__NR_umount)
	__NR_umount,
#endif
#if defined(__NR_umount2)
	__NR_umount2,
#endif
#if defined(__NR_uname)
	__NR_uname,
#endif
#if defined(__NR_unlink)
	__NR_unlink,
#endif
#if defined(__NR_unlinkat)
	__NR_unlinkat,
#endif
#if defined(__NR_unshare)
	__NR_unshare,
#endif
#if defined(__NR_uretprobe)
	__NR_uretprobe,
#endif
#if defined(__NR_unused109)
	__NR_unused109,
#endif
#if defined(__NR_unused150)
	__NR_unused150,
#endif
#if defined(__NR_unused18)
	__NR_unused18,
#endif
#if defined(__NR_unused28)
	__NR_unused28,
#endif
#if defined(__NR_unused59)
	__NR_unused59,
#endif
#if defined(__NR_unused84)
	__NR_unused84,
#endif
#if defined(__NR_uselib)
	__NR_uselib,
#endif
#if defined(__NR_userfaultfd)
	__NR_userfaultfd,
#endif
#if defined(__NR_usr26)
	__NR_usr26,
#endif
#if defined(__NR_usr32)
	__NR_usr32,
#endif
#if defined(__NR_ustat)
	__NR_ustat,
#endif
#if defined(__NR_utime)
	__NR_utime,
#endif
#if defined(__NR_utimensat)
	__NR_utimensat,
#endif
#if defined(__NR_utimensat_time64)
	__NR_utimensat_time64,
#endif
#if defined(__NR_utimes)
	__NR_utimes,
#endif
#if defined(__NR_utrap_install)
	__NR_utrap_install,
#endif
#if defined(__NR_vfork)
	__NR_vfork,
#endif
#if defined(__NR_vhangup)
	__NR_vhangup,
#endif
#if defined(__NR_vm86)
	__NR_vm86,
#endif
#if defined(__NR_vm86old)
	__NR_vm86old,
#endif
#if defined(__NR_vmsplice)
	__NR_vmsplice,
#endif
#if defined(__NR_vserver)
	__NR_vserver,
#endif
#if defined(__NR_wait3)
	__NR_wait3,
#endif
#if defined(__NR_wait4)
	__NR_wait4,
#endif
#if defined(__NR_waitid)
	__NR_waitid,
#endif
#if defined(__NR_waitpid)
	__NR_waitpid,
#endif
#if defined(__NR_write)
	__NR_write,
#endif
#if defined(__NR_writev)
	__NR_writev,
#endif
#if defined(__NR_xtensa)
	__NR_xtensa,
#endif
	0	/* ensure at least 1 element */
};

/*
 *  limit_procs()
 *	try to limit child resources
 */
static void limit_procs(const unsigned long int procs)
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
	lim.rlim_cur = (unsigned long int)procs;
	lim.rlim_max = (unsigned long int)procs;
	(void)setrlimit(RLIMIT_NPROC, &lim);
#else
	(void)procs;
#endif
}

static void NORETURN MLOCKED_TEXT stress_sig_handler(int signum)
{
	(void)signum;

	if (stress_call_type == CALL_UNDEFINED) {
		/* No idea how we got here, bail out */
		_exit(1);
	}
	siglongjmp(jmp_env, 1);
	stress_no_return();
}

/*
 *  stress_enosys_push_syscall()
 *
 */
static int stress_enosys_push_syscall(
	stress_args_t *args,
	const int rd_fd,
	const int wr_fd,
	const long int number,
	const bool do_random,
	uint64_t *syscalls)
{
	ssize_t sret;
	stress_enosys_rpc_t rpc;

	/* Check if this is a known non-ENOSYS syscall */
	if (syscall_find(number))
		return 0;

	rpc.syscall_num = number;

	sret = write(wr_fd, &rpc, sizeof(rpc));
	if (sret != (ssize_t)sizeof(rpc))
		return -1;
	errno = 0;
	sret = read(rd_fd, &rpc, sizeof(rpc));
	if (sret != (ssize_t)sizeof(rpc)) {
		if (errno < 0)
			syscall_add(number);
		return -1;
	}
	*syscalls += rpc.ret.count;
	if (rpc.ret.rc != ENOSYS) {
		if ((!do_random) || (number < 65536))
			syscall_add(number);
	}
	stress_bogo_inc(args);
	return 0;
}

/*
 *  stress_enosys_child()
 *	child process reads syscall number, performs
 *	syscall and writes return errno and number of
 *	calls made
 */
static inline int stress_enosys_child(
	stress_args_t *args,
	const int rd_fd,
	const int wr_fd,
	const pid_t pid)
{
	ssize_t sret;
	stress_enosys_rpc_t rpc;

	sret = read(rd_fd, &rpc, sizeof(rpc));
	if (sret != (ssize_t)sizeof(rpc))
		return -1;
	rpc.ret.count = 0;

	itimer_set(args);
	rpc.ret.rc = stress_enosys_syscall(pid, &rpc);
	itimer_stop(args);

	sret = write(wr_fd, &rpc, sizeof(rpc));
	if (sret != (ssize_t)sizeof(rpc))
		return -1;

	return 0;
}

/*
 *  stress_enosys_parent()
 *	parent writes syscalls to pipe for child
 *	to read and execute in it's own context
 */
static inline int stress_enosys_parent(
	stress_args_t *args,
	const int rd_fd,
	const int wr_fd,
	uint64_t *syscalls)
{
	const unsigned long int mask24 = 0xffffffUL;
#if ULONG_MAX > 0xffffffff
	const unsigned long int mask40 = 0xffffffffffUL;
	const unsigned long int mask48 = 0xffffffffffffUL;
	const unsigned long int mask56 = 0xffffffffffffffUL;
#else
	const unsigned long int mask28 = 0xfffffffUL;
#endif
	static unsigned long int syscall_seq = 0UL;
	unsigned long int number;

	ssize_t j;

	switch (stress_mwc8modn(5)) {
	case 0:
		/* Low sequential syscalls */
		for (number = 0; number < MAX_SYSCALL + 1024; number++) {
			if (UNLIKELY(!stress_continue(args)))
				return 0;
			if (stress_enosys_push_syscall(args, rd_fd, wr_fd, number, false, syscalls) < 0)
				return -1;
			if (stress_enosys_push_syscall(args, rd_fd, wr_fd, ++syscall_seq, true, syscalls) < 0)
				return -1;
		}
		break;
	case 1:
		/* Various high syscalls */
		for (number = 0xff; number; number <<= 1) {
			long int n;

			for (n = 0; n < 0xff; n++) {
				if (UNLIKELY(!stress_continue(args)))
					return 0;
				if (stress_enosys_push_syscall(args, rd_fd, wr_fd, n + number, false, syscalls) < 0)
					return -1;
			}
		}
		break;
	case 2:
		/* Random syscalls */
		for (j = 0; j < 1024; j++) {
			if (UNLIKELY(!stress_continue(args)))
				return 0;
			if (stress_enosys_push_syscall(args, rd_fd, wr_fd, (long int)stress_mwc8(), true, syscalls) < 0)
				return -1;
			if (stress_enosys_push_syscall(args, rd_fd, wr_fd, (long int)stress_mwc16(), true, syscalls) < 0)
				return -1;
			if (stress_enosys_push_syscall(args, rd_fd, wr_fd, (long int)stress_mwc32() & mask24, true, syscalls) < 0)
				return -1;
			if (stress_enosys_push_syscall(args, rd_fd, wr_fd, (long int)stress_mwc32(), true, syscalls) < 0)
				return -1;
#if ULONG_MAX > 0xffffffff
			if (stress_enosys_push_syscall(args, rd_fd, wr_fd, (long int)stress_mwc64(), true, syscalls) < 0)
				return -1;
#endif
		}
		break;
	case 3:
		if (UNLIKELY(!stress_continue(args)))
			return 0;
#if ULONG_MAX > 0xffffffff
		if (stress_enosys_push_syscall(args, rd_fd, wr_fd, (long int)(stress_mwc64() & mask40), true, syscalls) < 0)
			return -1;
		if (stress_enosys_push_syscall(args, rd_fd, wr_fd, (long int)(stress_mwc64() & mask48), true, syscalls) < 0)
			return -1;
		if (stress_enosys_push_syscall(args, rd_fd, wr_fd, (long int)(stress_mwc64() & mask56), true, syscalls) < 0)
			return -1;
		if (stress_enosys_push_syscall(args, rd_fd, wr_fd, (long int)stress_mwc64(), true, syscalls) < 0)
			return -1;
#else
		if (stress_enosys_push_syscall(args, rd_fd, wr_fd, (long int)(stress_mwc32() & mask24), true, syscalls) < 0)
			return -1;
		if (stress_enosys_push_syscall(args, rd_fd, wr_fd, (long int)(stress_mwc32() & mask28), true, syscalls) < 0)
			return -1;
		if (stress_enosys_push_syscall(args, rd_fd, wr_fd, (long int)stress_mwc32(), true, syscalls) < 0)
			return -1;

#endif
		break;
	case 4:
		/* Various bit masks */
		for (number = 0x400; number; number <<= 1) {
			if (UNLIKELY(!stress_continue(args)))
				return 0;
			if (stress_enosys_push_syscall(args, rd_fd, wr_fd, number, false, syscalls) < 0)
				return -1;
			if (stress_enosys_push_syscall(args, rd_fd, wr_fd, number, false, syscalls) < 0)
				return -1;
			if (stress_enosys_push_syscall(args, rd_fd, wr_fd, number | 1, false, syscalls) < 0)
				return -1;
			if (stress_enosys_push_syscall(args, rd_fd, wr_fd, number | (number << 1U), false, syscalls) < 0)
				return -1;
			if (stress_enosys_push_syscall(args, rd_fd, wr_fd, ~number, false, syscalls) < 0)
				return -1;
		}
		break;
	}

	return 0;
}

/*
 *  stress_enosys
 *	stress system calls
 */
static int stress_enosys(stress_args_t *args)
{
	pid_t pid;
	int rd_fds[2], wr_fds[2];
	size_t i;
	uint64_t syscalls = 0;
	double t_start, duration, rate;

#if defined(STRESS_EXERCISE_X86_SYSCALL)
	stress_x86syscall_available = stress_cpu_x86_has_syscall();
#endif
	stress_set_proc_state(args->name, STRESS_STATE_SYNC_WAIT);
	stress_sync_start_wait(args);
	stress_set_proc_state(args->name, STRESS_STATE_RUN);

	for (i = 0; i < (ssize_t)SIZEOF_ARRAY(skip_syscalls) - 1; i++)
		syscall_add(skip_syscalls[i]);

	if (stress_sighandler(args->name, SIGPIPE, stress_sighandler_nop, NULL) < 0)
		return EXIT_FAILURE;

	t_start = stress_time_now();
	do {
		if (pipe(rd_fds) < 0) {
			pr_inf_skip("%s: pipe failed, errno=%d (%s), skipping stressor\n",
				args->name, errno, strerror(errno));
			return EXIT_NO_RESOURCE;
		}
		if (pipe(wr_fds) < 0) {
			pr_inf_skip("%s: pipe failed, errno=%d (%s), skipping stressor\n",
				args->name, errno, strerror(errno));
			(void)close(rd_fds[0]);
			(void)close(rd_fds[1]);
			return EXIT_NO_RESOURCE;
		}
again:
		if (UNLIKELY(!stress_continue(args)))
			return EXIT_SUCCESS;
		pid = fork();
		if (pid < 0) {
			if (stress_redo_fork(args, errno))
				goto again;
			if (UNLIKELY(!stress_continue(args)))
				goto deinit;
			pr_err("%s: fork failed, errno=%d: (%s)\n",
				args->name, errno, strerror(errno));
			(void)close(rd_fds[0]);
			(void)close(rd_fds[1]);
			(void)close(wr_fds[0]);
			(void)close(wr_fds[1]);
			goto deinit;
		} else if (pid == 0) {
			const pid_t mypid = getpid();

			(void)close(rd_fds[1]);
			(void)close(wr_fds[0]);

			stress_set_proc_state(args->name, STRESS_STATE_RUN);

			/* Try to limit child from spawning */
			limit_procs(2);

			/* We don't want bad ops clobbering this region */
			stress_shared_readonly();

			/* Drop all capabilities */
			if (stress_drop_capabilities(args->name) < 0)
				_exit(EXIT_NO_RESOURCE);
			for (i = 0; i < SIZEOF_ARRAY(exit_sigs); i++) {
				if (stress_sighandler(args->name, exit_sigs[i], stress_sig_handler_exit, NULL) < 0)
					_exit(EXIT_FAILURE);
			}
			for (i = 0; i < SIZEOF_ARRAY(sigs); i++) {
				if (stress_sighandler(args->name, sigs[i], stress_sig_handler, NULL) < 0)
					_exit(EXIT_FAILURE);
			}

			stress_parent_died_alarm();
			(void)sched_settings_apply(true);

			do {
				if (stress_enosys_child(args, rd_fds[0], wr_fds[1], mypid) < 0)
					break;
			} while (stress_continue(args));

			(void)close(rd_fds[0]);
			(void)close(wr_fds[1]);

			_exit(EXIT_SUCCESS);
		} else if (pid > 0) {
			(void)close(rd_fds[0]);
			(void)close(wr_fds[1]);

			rd_fds[0] = -1;
			wr_fds[1] = -1;

			do {
				if (stress_enosys_parent(args, wr_fds[0], rd_fds[1], &syscalls) < 0)
					break;
			} while (stress_continue(args));

			/* Parent, wait for child */
			(void)stress_kill_and_wait(args, pid, SIGKILL, false);
			(void)close(rd_fds[1]);
			(void)close(wr_fds[0]);

			rd_fds[1] = -1;
			wr_fds[0] = -1;
		}
	} while (stress_continue(args));

deinit:
	duration = stress_time_now() - t_start;
	rate = (duration > 0.0) ? (double)syscalls / duration : 0.0;
	stress_metrics_set(args, 0, "syscalls per second", rate, STRESS_METRIC_GEOMETRIC_MEAN);

	syscall_free();
	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);

	return EXIT_SUCCESS;
}
const stressor_info_t stress_enosys_info = {
	.stressor = stress_enosys,
	.classifier = CLASS_OS,
	.help = help
};
#else
const stressor_info_t stress_enosys_info = {
	.stressor = stress_unimplemented,
	.classifier = CLASS_OS,
	.help = help,
	.unimplemented_reason = "built without sys/syscall.h or syscall support"
};
#endif
