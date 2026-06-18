/*
 * Copyright (C) 2013-2021 Canonical, Ltd.
 * Copyright (C) 2022-2026 Colin Ian King.
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
#include "core-killpid.h"
#include "core-out-of-memory.h"
#include "core-signal.h"

#include <sys/socket.h>

static const stress_help_t help[] = {
	{ NULL,	"sysinval N",		"start N workers that pass invalid args to syscalls" },
	{ NULL,	"sysinval-ops N",	"stop after N sysinval bogo syscalls" },
	{ NULL,	NULL,		    	NULL }
};

#if defined(HAVE_SYSCALL_H) &&	\
    defined(HAVE_SYSCALL) &&	\
    !defined(__APPLE__) && 	\
    !defined(__gnu_hurd__)

#define ARG_BITMASK(x, bitmask)	(((x) & (bitmask)) == (bitmask))

#define SYSCALL_ARGS_SIZE	(SIZEOF_ARRAY(stress_syscall_args))

#define SYSCALL_HASH_TABLE_SIZE	(10007)	/* Hash table size (prime) */
#define HASH_TABLE_POOL_SIZE	(32768) /* Hash table pool size */
#define SYSCALL_FAIL		(0x00)	/* Expected behaviour */
#define	SYSCALL_CRASH		(0x01)	/* Syscalls that crash the child */
#define SYSCALL_ERRNO_ZERO	(0x02)	/* Syscalls that return 0 */
#define SYSCALL_TIMED_OUT	(0x03)	/* Syscalls that time out */

#define MAX_CRASHES		(100000)
#define SYSCALL_TIMEOUT_USEC	(1000)	/* Timeout syscalls duration */

#define MAX_SYSCALL_ARGS	(6)

/*
 *  tuple of system call number and stringified system call
 */
#if defined(__NR_exit)
#define SYS(x)		__NR_ ## x, # x
#define DEFSYS(x)	__NR_ ## x
#elif defined(SYS_exit)
#define SYS(x)		SYS_ ## x, # x
#define DEFSYS(x)	SYS_ ## x
#else
#define SYS(x) 		0, "unknown"
#define DEFSYS(x) 	0
#endif

/*
 *  system call argument types
 */
#define ARG_NONE		0x00000000UL
#define ARG_PTR			0x00000002UL
#define ARG_INT			0x00000004UL
#define ARG_UINT		0x00000008UL
#define ARG_SOCKFD		0x00000010UL
#define ARG_STRUCT_SOCKADDR	0x00000020UL
#define ARG_SOCKLEN_T		0x00000040UL
#define ARG_FLAG		0x00000080UL
#define ARG_BRK_ADDR		0x00000100UL
#define ARG_MODE		0x00000200UL
#define ARG_LEN			0x00000400UL
#define ARG_SECONDS		0x00001000UL
#define ARG_BPF_ATTR		0x00002000UL
#define ARG_EMPTY_FILENAME	0x00004000UL	/* "" */
#define ARG_DEVZERO_FILENAME	0x00008000UL	/* /dev/zero */
#define ARG_CLOCKID_T		0x00010000UL
#define ARG_FUNC_PTR		0x00020000UL
#define ARG_FD			0x00040000UL
#define ARG_TIMEOUT		0x00080000UL
#define ARG_DIRFD		0x00100000UL
#define ARG_DEVNULL_FILENAME	0x00200000UL	/* /dev/null */
#define ARG_RND			0x00400000UL
#define ARG_PID			0x00800000UL
#define ARG_NON_NULL_PTR	0x01000000UL
#define ARG_NON_ZERO_LEN	0x02000000UL
#define ARG_GID			0x04000000UL
#define ARG_UID			0x08000000UL
#define ARG_FUTEX_PTR		0x10000000UL
#define ARG_ACCESS_MODE		0x20000000UL	/* faccess modes */
#define ARG_MISC		0x40000000UL

/*
 *  misc system call args
 */
#define ARG_ADD_KEY_TYPES	0x00000001UL | ARG_MISC
#define ARG_ADD_KEY_DESCRS	0x00000002UL | ARG_MISC
#define ARG_BPF_CMDS		0x00000003UL | ARG_MISC
#define ARG_BPF_LEN		0x00000004UL | ARG_MISC

#define ARG_VALUE(x, v)		{ (x), SIZEOF_ARRAY(v), (unsigned long int *)(void *)v }
#define ARG_MISC_ID(x)		((x) & ~ARG_MISC)

/*
 *  rotate right for hashing
 */
#define RORn(val, n)						\
do {								\
	val = (sizeof(unsigned long int) == sizeof(uint32_t)) ?	\
		shim_ror32n(val, n) : shim_ror64n(val, n);	\
} while (0)

#define SHR_UL(v, shift) ((unsigned long int)(((unsigned long long int)v) << shift))

/*
 *  per system call testing information, each system call
 *  to be exercised has one or more of these records.
 */
typedef struct {
	const unsigned long int syscall;	/* system call number */
	const char *name;			/* text name of system call */
	const int num_args;			/* number of arguments */
	uint32_t arg_bitmasks[MAX_SYSCALL_ARGS]; /* semantic info about each argument */
} stress_syscall_arg_t;

/*
 *  argument semantic information, unique argument types
 *  have one of these records to represent the different
 *  invalid argument values. Keep these values as short
 *  as possible as each new value increases the number of
 *  permutations
 */
typedef struct {
	uint32_t bitmask;		/* bitmask representing arg type */
	size_t num_values;		/* number of different invalid values */
	unsigned long int *values;	/* invalid values */
} stress_syscall_arg_values_t;

/*
 *  hash table entry for syscalls and arguments that need
 *  to be skipped either because they crash the child or
 *  because the system call succeeds
 */
typedef struct stress_syscall_args_hash {
	struct stress_syscall_args_hash *next;	/* next item in list */
	unsigned long int hash;		/* has of system call and args */
	unsigned long int syscall;	/* system call number */
	unsigned long int args[MAX_SYSCALL_ARGS]; /* arguments */
	uint8_t	 type;			/* type of failure */
} stress_syscall_arg_hash_t;

/*
 *  hash table contains two tables, one the hash lookup table
 *  and the second is a pool of pre-allocated items. The index
 *  reflects the next free index into the pool to be allocated
 */
typedef struct {
	stress_syscall_arg_hash_t *table[SYSCALL_HASH_TABLE_SIZE];
	stress_syscall_arg_hash_t pool[HASH_TABLE_POOL_SIZE];
	size_t index;
} stress_syscall_hash_table_t;

/*
 *  hash table - in the parent context this records system
 *  calls that crash the child. in the child context this
 *  contains the same crash data that the parent has plus
 *  a cache of the system calls that return 0 and we don't
 *  want to retest
 */
static stress_syscall_hash_table_t *hash_table;

static volatile bool do_jmp;
static sigjmp_buf jmp_env;

/*
 *  mappings[] protection flags
 */
static const int prot_flags[] = {
	PROT_NONE,
	PROT_WRITE,
	PROT_READ,
	PROT_READ | PROT_WRITE,
};

/*
 *  mappings are page multiple sized mmappings with
 *  an empty unreadable page at the end. There are
 *  as many mappings as prot flags
 */
static uint8_t *mappings[SIZEOF_ARRAY(prot_flags)];

/*
 *  mappings_small use the last byte of the last
 *  page in mappings[] for a small mmap'd space
 */
static uint8_t *mappings_small[SIZEOF_ARRAY(prot_flags)];

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

static const stress_syscall_arg_t stress_syscall_args[] = {
#if DEFSYS(_llseek)
	{ SYS(_llseek), 5, { ARG_FD, ARG_UINT, ARG_UINT, ARG_PTR, ARG_INT } },
#endif
#if DEFSYS(_newselect)
	{ SYS(_newselect), 5, { ARG_FD, ARG_PTR, ARG_PTR, ARG_PTR, ARG_PTR, 0 } },
#endif
#if DEFSYS(_sysctl)
	{ SYS(_sysctl), 1, { ARG_PTR, 0, 0, 0, 0, 0 } },
#endif
#if DEFSYS(accept)
	{ SYS(accept), 3, { ARG_SOCKFD, ARG_PTR | ARG_STRUCT_SOCKADDR, ARG_PTR, 0, 0, 0 } },
#endif
#if DEFSYS(accept4)
	{ SYS(accept4), 4, { ARG_SOCKFD, ARG_PTR | ARG_STRUCT_SOCKADDR, ARG_PTR, ARG_FLAG, 0, 0 } },
#endif
#if DEFSYS(access)
	{ SYS(access), 2, { ARG_PTR | ARG_EMPTY_FILENAME, ARG_MODE, 0, 0, 0, 0 } },
	{ SYS(access), 2, { ARG_PTR | ARG_DEVZERO_FILENAME, ARG_MODE, 0, 0, 0, 0 } },
	{ SYS(access), 2, { ARG_PTR | ARG_EMPTY_FILENAME, ARG_ACCESS_MODE, 0, 0, 0, 0 } },
	{ SYS(access), 2, { ARG_PTR | ARG_DEVZERO_FILENAME, ARG_ACCESS_MODE, 0, 0, 0, 0 } },
#endif
#if DEFSYS(acct)
	{ SYS(acct), 1, { ARG_PTR | ARG_EMPTY_FILENAME, 0, 0, 0, 0, 0 } },
#endif
#if DEFSYS(acl_get)
	{ SYS(acl_get), 3, { ARG_PTR, ARG_INT, ARG_PTR, 0, 0, 0 } },
#endif
#if DEFSYS(acl_set)
	{ SYS(acl_set), 2, { ARG_PTR, ARG_PTR, 0, 0, 0, 0 } },
#endif
#if DEFSYS(add_key)
	{ SYS(add_key), 5, { ARG_ADD_KEY_TYPES, ARG_ADD_KEY_DESCRS, ARG_PTR, ARG_LEN, ARG_UINT, 0 } },
	{ SYS(add_key), 5, { ARG_PTR, ARG_PTR, ARG_PTR, ARG_LEN, ARG_UINT, 0 } },
#endif
#if DEFSYS(adjtimex)
	/* Need to also test invalid args:
		time.tv_usec < 0
		time.tv_usec > 1000000
		tick <  900000/USER_HZ
		tick > 100000/USER_HZ
		(txc->modes & ADJ_NANO) and txc->time.tv_usec >= NSEC_PER_SEC
	*/
	{ SYS(adjtimex), 1, { ARG_PTR, 0, 0, 0, 0, 0 } },
#endif
#if DEFSYS(afs_syscall)
	/* Should be ENOSYS */
	{ SYS(afs_syscall), 0, { 0, 0, 0, 0, 0, 0 } },
#endif
#if DEFSYS(alarm) && 0
	{ SYS(alarm), 1, { ARG_SECONDS, 0, 0, 0, 0, 0 } },
#endif
#if DEFSYS(alloc_hugepages)
	/* removed in 2.5.44 */
	{ SYS(alloc_hugepages), 5, { ARG_INT, ARG_PTR, ARG_LEN, ARG_INT, ARG_FLAG } },
#endif
#if DEFSYS(arc_gettls)
	/* ARC only */
	{ SYS(arc_gettls), 0, { 0, 0, 0, 0, 0, 0 } },
#endif
#if DEFSYS(arc_settls)
	/* ARC only */
	{ SYS(arc_settls), 1, { ARG_PTR, 0, 0, 0, 0, 0 } },
#endif
#if DEFSYS(arc_usr_cmpxchg)
	/* ARC only */
	{ SYS(arc_usr_cmpxchg), 3, { ARG_PTR, ARG_INT, ARG_INT, 0, 0, 0 } },
#endif
#if DEFSYS(arch_prctl)
	{ SYS(arch_prctl), 2, { ARG_INT, ARG_UINT } },
	{ SYS(arch_prctl), 2, { ARG_INT, ARG_PTR } },
#endif
#if DEFSYS(atomic_barrier)
	/* m68k only */
	{ SYS(atomic_barrier), 0, { 0, 0, 0, 0, 0, 0 } },
#endif
#if DEFSYS(atomic_cmpxchg_32)
	/* m68k only */
	{ SYS(atomic_cmpxchg_32), 6, { ARG_UINT, ARG_INT, ARG_INT, ARG_INT, ARG_INT, ARG_PTR } },
#endif
#if DEFSYS(bdflush)
	/* deprecated */
	{ SYS(bdflush), 2, { ARG_INT, ARG_PTR, 0, 0, 0, 0 } },
	{ SYS(bdflush), 2, { ARG_INT, ARG_UINT, 0, 0, 0, 0 } },
#endif
#if DEFSYS(bfin_spinlock)
	/* blackfin, removed in 4.17 */
	{ SYS(bfin_spinlock), 1, { ARG_PTR, 0, 0, 0, 0, 0 } },
#endif
#if DEFSYS(bind)
	{ SYS(bind), 3, { ARG_SOCKFD, ARG_PTR | ARG_STRUCT_SOCKADDR, ARG_SOCKLEN_T, 0, 0, 0 } },
#endif
#if DEFSYS(bpf)
	/*
	{ SYS(bpf), 3, { ARG_BPF_CMDS, ARG_PTR | ARG_BPF_ATTR, ARG_BPF_LEN, 0, 0, 0 } },
	{ SYS(bpf), 3, { ARG_BPF_CMDS, ARG_PTR | ARG_BPF_ATTR, ARG_LEN, 0, 0, 0 } },
	{ SYS(bpf), 3, { ARG_INT, ARG_PTR | ARG_BPF_ATTR, ARG_LEN, 0, 0, 0 } },
	*/
#endif
#if DEFSYS(brk)
	{ SYS(brk), 1, { ARG_PTR | ARG_BRK_ADDR, 0, 0, 0, 0, 0 } },
#endif
#if DEFSYS(breakpoint)
	{ SYS(breakpoint), 0, { 0, 0, 0, 0, 0, 0 } },
	/* ARM OABI only */
#endif
#if DEFSYS(cachectl)
	/* MIPS */
	{ SYS(cachectl), 3, { ARG_PTR, ARG_INT, ARG_INT, 0, 0, 0 } },
#endif
#if DEFSYS(cacheflush)
	{ SYS(cacheflush), 3, { ARG_PTR, ARG_INT, ARG_INT, 0, 0, 0 } },
#endif
#if DEFSYS(cache_sync)
	/* Unknown */
#endif
#if DEFSYS(capget)
	{ SYS(capget), 2, { ARG_INT, ARG_PTR, 0, 0, 0, 0 } },
#endif
#if DEFSYS(capset)
	{ SYS(capset), 2, { ARG_INT, ARG_PTR, 0, 0, 0, 0 } },
#endif
#if DEFSYS(chdir)
	{ SYS(chdir), 1, { ARG_PTR | ARG_EMPTY_FILENAME, 0, 0, 0, 0, 0 } },
	{ SYS(chdir), 1, { ARG_PTR | ARG_DEVZERO_FILENAME, 0, 0, 0, 0, 0 } },
#endif
#if DEFSYS(chmod)
	{ SYS(chmod), 2, { ARG_PTR | ARG_EMPTY_FILENAME, ARG_INT, 0, 0, 0, 0 } },
#endif
#if DEFSYS(chown)
	{ SYS(chown), 2, { ARG_PTR | ARG_EMPTY_FILENAME, ARG_INT, 0, 0, 0, 0 } },
#endif
#if DEFSYS(chown32)
	{ SYS(chown32), 2, { ARG_PTR | ARG_EMPTY_FILENAME, ARG_INT, 0, 0, 0, 0 } },
#endif
#if DEFSYS(chroot)
	{ SYS(chroot), 1, { ARG_PTR | ARG_EMPTY_FILENAME, 0, 0, 0, 0, 0 } },
	{ SYS(chroot), 1, { ARG_PTR | ARG_DEVZERO_FILENAME, 0, 0, 0, 0, 0 } },
#endif
#if DEFSYS(clock_adjtime)
	{ SYS(clock_adjtime), 2, { ARG_CLOCKID_T, ARG_PTR, 0, 0, 0, 0 } },
#endif
#if DEFSYS(clock_adjtime64)
	{ SYS(clock_adjtime64), 2, { ARG_CLOCKID_T, ARG_PTR, 0, 0, 0, 0 } },
#endif
#if DEFSYS(clock_getres)
	{ SYS(clock_getres), 2, { ARG_CLOCKID_T, ARG_PTR, 0, 0, 0, 0 } },
#endif
#if DEFSYS(clock_getres_time64)
	{ SYS(clock_getres_time64), 2, { ARG_CLOCKID_T, ARG_PTR, 0, 0, 0, 0 } },
#endif
#if DEFSYS(clock_gettime)
	{ SYS(clock_gettime), 2, { ARG_CLOCKID_T, ARG_PTR, 0, 0, 0, 0 } },
#endif
#if DEFSYS(clock_gettime64)
	{ SYS(clock_gettime64), 2, { ARG_CLOCKID_T, ARG_PTR, 0, 0, 0, 0 } },
#endif
#if DEFSYS(clock_nanosleep)
	{ SYS(clock_nanosleep), 4, { ARG_CLOCKID_T, ARG_UINT, ARG_PTR, ARG_PTR, 0, 0 } },
#endif
#if DEFSYS(clock_nanosleep64)
	{ SYS(clock_nanosleep64), 4, { ARG_CLOCKID_T, ARG_UINT, ARG_PTR, ARG_PTR, 0, 0 } },
#endif
#if DEFSYS(clock_settime)
	{ SYS(clock_settime), 2, { ARG_CLOCKID_T, ARG_PTR, 0, 0, 0, 0 } },
#endif
#if DEFSYS(clock_settime64)
	{ SYS(clock_settime64), 2, { ARG_CLOCKID_T, ARG_PTR, 0, 0, 0, 0 } },
#endif
#if DEFSYS(clone)
	/* { SYS(clone), 6, { ARG_FUNC_PTR, ARG_PTR, ARG_INT, ARG_PTR, ARG_PTR, ARG_PTR } }, */
#endif
#if DEFSYS(clone2)
	/* IA-64 only */
	/* { SYS(clone2), 6, { ARG_FUNC_PTR, ARG_PTR, ARG_INT, ARG_PTR, ARG_PTR, ARG_UINT } }, */
#endif
#if DEFSYS(clone3)
	/* { SYS(clone3), 2, { ARG_PTR, ARG_LEN, 0, 0, 0, 0 } }, */
#endif
#if DEFSYS(close)
	{ SYS(close), 1, { ARG_FD, 0, 0, 0, 0, 0 } },
#endif
#if DEFSYS(close_range)
	{ SYS(close_range), 3, { ARG_FD, ARG_FD, ARG_UINT, 0, 0, 0 } },
#endif
#if DEFSYS(compat_exit)
	/* exiting the testing child is not a good idea */
#endif
#if DEFSYS(compat_read)
	{ SYS(compat_read), 3, { ARG_FD, ARG_PTR, ARG_LEN, 0, 0, 0 } },
#endif
#if DEFSYS(compat_restart_syscall)
	{ SYS(compat_restart_syscall), 0, { 0, 0, 0, 0, 0, 0 } },
	{ SYS(compat_restart_syscall), 0, { 0, 0, 0, 0, 0, 0 } },
#endif
#if DEFSYS(compat_rt_sigreturn)
	/* { SYS(compat_rt_sigreturn), 1, { ARG_PTR, 0, 0, 0, 0, 0 } }, */
#endif
#if DEFSYS(compat_write)
	{ SYS(compat_write), 3, { ARG_FD, ARG_PTR, ARG_LEN, 0, 0, 0 } },
#endif
#if DEFSYS(cmpxchg_badaddr)
	/* Tile only, removed 4.17 */
#endif
#if DEFSYS(connect)
	{ SYS(connect), 3, { ARG_SOCKFD, ARG_PTR, ARG_LEN, 0, 0, 0 } },
#endif
#if DEFSYS(copy_file_range)
	{ SYS(copy_file_range), 6, { ARG_FD, ARG_PTR, ARG_FD, ARG_PTR, ARG_LEN, ARG_FLAG } },
#endif
#if DEFSYS(creat)
	{ SYS(creat), 3, { ARG_EMPTY_FILENAME, ARG_FLAG, ARG_MODE, 0, 0, 0 } },
#endif
#if DEFSYS(create_module)
	{ SYS(create_module), 2, { ARG_PTR, ARG_LEN, 0, 0, 0, 0 } },
#endif
#if DEFSYS(delete_module)
	{ SYS(delete_module), 2, { ARG_PTR, ARG_FLAG, 0, 0, 0, 0 } },
#endif
#if DEFSYS(dma_memcpy)
	/* blackfin, removed in 4.17 */
#endif
#if DEFSYS(dup)
	{ SYS(dup), 1, { ARG_FD, 0, 0, 0, 0, 0 } },
#endif
#if DEFSYS(dup2)
	{ SYS(dup2), 2, { ARG_FD, ARG_FD, 0, 0, 0, 0 } },
#endif
#if DEFSYS(dup3)
	{ SYS(dup3), 3, { ARG_FD, ARG_FD, ARG_FLAG, 0, 0, 0 } },
#endif
#if DEFSYS(epoll_create)
	{ SYS(epoll_create), 1, { ARG_LEN,  0, 0, 0, 0, 0 } },
#endif
#if DEFSYS(epoll_create1)
	{ SYS(epoll_create1), 1, { ARG_FLAG, 0, 0, 0, 0, 0 } },
#endif
#if DEFSYS(epoll_ctl)
	{ SYS(epoll_ctl), 4, { ARG_FD, ARG_INT, ARG_FD, ARG_PTR, 0, 0 } },
#endif
#if DEFSYS(epoll_ctl_add)
	{ SYS(epoll_ctl_add), 4, { ARG_FD, ARG_INT, ARG_FD, ARG_PTR, 0, 0 } },
#endif
#if DEFSYS(epoll_wait)
	{ SYS(epoll_wait), 4, { ARG_FD, ARG_PTR, ARG_INT, ARG_TIMEOUT, 0, 0 } },
#endif
#if DEFSYS(epoll_wait_old)
	{ SYS(epoll_wait_old), 3, { ARG_FD, ARG_PTR, ARG_INT, 0 , 0, 0 } },
#endif
#if DEFSYS(epoll_pwait)
	{ SYS(epoll_pwait), 5, { ARG_FD, ARG_PTR, ARG_INT, ARG_TIMEOUT, ARG_PTR, 0 } },
#endif
#if DEFSYS(eventfd)
	{ SYS(eventfd), 2, { ARG_INT, ARG_FLAG, 0, 0, 0, 0 } },
#endif
#if DEFSYS(eventfd2)
	{ SYS(eventfd2), 2, { ARG_INT, ARG_FLAG, 0, 0, 0, 0 } },
#endif
#if DEFSYS(exec_with_loader)
	/* { SYS(exec_with_loader), 5, { ARG_INT, ARG_PTR, ARG_PTR, ARG_PTR, ARG_PTR, 0 } }, */
#endif
#if DEFSYS(execv)
	/* { SYS(execv), 2, { ARG_PTR, ARG_PTR, 0, 0, 0, 0 } }, */
#endif
#if DEFSYS(execve)
	/* { SYS(execve), 2, { ARG_PTR, ARG_PTR, ARG_PTR, 0, 0, 0 } }, */
#endif
#if DEFSYS(execveat)
	/* { SYS(execveat), 5, { ARG_INT, ARG_PTR, ARG_PTR, ARG_PTR, ARG_PTR, ARG_INT, 0 } }, */
#endif
#if DEFSYS(exit)
	/* exiting the testing child is not a good idea */
#endif
#if DEFSYS(exit_group)
	/* exiting the testing child is not a good idea */
#endif
#if DEFSYS(faccessat)
	{ SYS(faccessat), 4, { ARG_DIRFD, ARG_EMPTY_FILENAME, ARG_MODE, ARG_FLAG, 0, 0 } },
	{ SYS(faccessat), 4, { ARG_DIRFD, ARG_DEVNULL_FILENAME, ARG_MODE, ARG_FLAG, 0, 0 } },
	{ SYS(faccessat), 4, { ARG_DIRFD, ARG_EMPTY_FILENAME, ARG_ACCESS_MODE, ARG_FLAG, 0, 0 } },
	{ SYS(faccessat), 4, { ARG_DIRFD, ARG_DEVNULL_FILENAME, ARG_ACCESS_MODE, ARG_FLAG, 0, 0 } },
#endif
#if DEFSYS(fadvise64)
	{ SYS(fadvise64), 4, { ARG_FD, ARG_UINT, ARG_LEN, ARG_INT, 0, 0 } },
#endif
#if DEFSYS(fadvise64_64)
	{ SYS(fadvise64_64), 4, { ARG_FD, ARG_UINT, ARG_LEN, ARG_INT, 0, 0 } },
#endif
#if DEFSYS(fallocate)
	{ SYS(fallocate), 4, { ARG_FD, ARG_MODE, ARG_INT, ARG_INT, 0, 0 } },
#endif
#if DEFSYS(fanotify_init)
	{ SYS(fanotify_init), 2, { ARG_FLAG, ARG_FLAG, 0, 0, 0, 0 } },
#endif
#if DEFSYS(fanotify_mark)
	{ SYS(fanotify_mark), 5, { ARG_FD, ARG_FLAG, ARG_UINT, ARG_FD, ARG_EMPTY_FILENAME, 0 } },
#endif
#if DEFSYS(fchdir)
	{ SYS(fchdir), 1, { ARG_FD, 0, 0, 0, 0, 0 } },
#endif
#if DEFSYS(fchmod)
	{ SYS(fchmod), 2, { ARG_FD, ARG_MODE, 0, 0, 0, 0 } },
#endif
#if DEFSYS(fchmodat)
	{ SYS(fchmodat), 4, { ARG_DIRFD, ARG_EMPTY_FILENAME, ARG_MODE, ARG_FLAG, 0, 0 } },
#endif
#if DEFSYS(fchmodat2)
	{ SYS(fchmodat2), 4, { ARG_DIRFD, ARG_FLAG, 0, 0, 0, 0 } },
#endif
#if DEFSYS(fchown)
	/* { SYS(fchown), 3, { ARG_FD, 0, 0, 0, 0, 0 } }, ;*/
#endif
#if DEFSYS(fchown32)
	/* { SYS(fchown32), 3, { ARG_FD, 0, 0, 0, 0, 0 } }, ;*/
#endif
#if DEFSYS(fchownat)
	{ SYS(fchownat), 5, { ARG_DIRFD, ARG_EMPTY_FILENAME, ARG_UINT, ARG_UINT, ARG_UINT, 0 } },
#endif
#if DEFSYS(fcntl)
	{ SYS(fcntl), 6, { ARG_FD, ARG_RND, ARG_RND, ARG_RND, ARG_RND, ARG_RND } },
#endif
#if DEFSYS(fcntl64)
	{ SYS(fcntl64), 6, { ARG_FD, ARG_RND, ARG_RND, ARG_RND, ARG_RND, ARG_RND } },
#endif
#if DEFSYS(fdatasync)
	{ SYS(fdatasync), 1, { ARG_FD, 0, 0, 0, 0, 0 } },
#endif
#if DEFSYS(fgetxattr)
	{ SYS(fgetxattr), 4, { ARG_FD, ARG_EMPTY_FILENAME, ARG_PTR, ARG_LEN, 0, 0 } },
	{ SYS(fgetxattr), 4, { ARG_FD, ARG_DEVNULL_FILENAME, ARG_PTR, ARG_LEN, 0, 0 } },
#endif
#if DEFSYS(finit_module)
	{ SYS(finit_module), 3, { ARG_PTR, ARG_LEN, ARG_PTR, 0, 0, 0 } },
#endif
#if DEFSYS(flistxattr)
	{ SYS(flistxattr), 3, { ARG_FD, ARG_PTR, ARG_LEN, 0, 0, 0 } },
#endif
#if DEFSYS(flock)
	{ SYS(flock), 2, { ARG_FD, ARG_INT, 0, 0, 0, 0 } },
#endif
#if DEFSYS(fork)
	/* { SYS(fork), 0, { 0, 0, 0, 0, 0, 0 } }, */
#endif
#if DEFSYS(fp_udfiex_crtl)
	{ SYS(fp_udfiex_crtl), 2, { ARG_INT, ARG_INT, 0, 0, 0, 0 } },
#endif
#if DEFSYS(free_hugepages)
	{ SYS(free_hugepages), 1, { ARG_PTR, 0, 0, 0, 0, 0 } },
#endif
#if DEFSYS(fremovexattr)
	{ SYS(fremovexattr), 2, { ARG_FD, ARG_PTR, 0, 0, 0, 0 } },
#endif
#if DEFSYS(fsconfig)
	{ SYS(fsconfig), 5, { ARG_PTR, ARG_UINT, ARG_PTR, ARG_PTR, ARG_INT, 0 } },
#endif
#if DEFSYS(fsetxattr)
	{ SYS(fsetxattr), 5, { ARG_FD, ARG_PTR, ARG_PTR, ARG_LEN, ARG_FLAG, 0 } },
#endif
#if DEFSYS(fsmount)
	{ SYS(fsmount), 3, { ARG_FD, ARG_FLAG, ARG_UINT, 0, 0 , 0 } },
#endif
#if DEFSYS(fsopen)
	{ SYS(fsopen), 2, { ARG_PTR, ARG_UINT, 0, 0 , 0, 0 } },
#endif
#if DEFSYS(fspick)
	{ SYS(fspick), 3, { ARG_DIRFD, ARG_PTR, ARG_FLAG, 0, 0 } },
	{ SYS(fspick), 3, { ARG_DIRFD, ARG_EMPTY_FILENAME, ARG_FLAG, 0, 0 } },
#endif
#if DEFSYS(fstat)
	{ SYS(fstat), 2, { ARG_FD, ARG_PTR, 0, 0, 0, 0 } },
#endif
#if DEFSYS(fstat64)
	{ SYS(fstat64), 2, { ARG_FD, ARG_PTR, 0, 0, 0, 0 } },
#endif
#if DEFSYS(fstatat)
	{ SYS(fstatat), 4, { ARG_DIRFD, ARG_EMPTY_FILENAME, ARG_PTR, ARG_FLAG, 0, 0 } },
	{ SYS(fstatat), 4, { ARG_DIRFD, ARG_DEVNULL_FILENAME, ARG_PTR, ARG_FLAG, 0, 0 } },
#endif
#if DEFSYS(fstatat64)
	{ SYS(fstatat64), 4, { ARG_DIRFD, ARG_EMPTY_FILENAME, ARG_PTR, ARG_FLAG, 0, 0 } },
	{ SYS(fstatat64), 4, { ARG_DIRFD, ARG_DEVNULL_FILENAME, ARG_PTR, ARG_FLAG, 0, 0 } },
#endif
#if DEFSYS(fstatfs)
	{ SYS(fstatfs), 2, { ARG_FD, ARG_PTR, 0, 0, 0, 0 } },
#endif
#if DEFSYS(fstatfs64)
	{ SYS(fstatfs64), 2, { ARG_FD, ARG_PTR, 0, 0, 0, 0 } },
#endif
#if DEFSYS(fsync)
	{ SYS(fsync), 1, { ARG_FD, 0, 0, 0, 0, 0 } },
#endif
#if DEFSYS(ftime)
	/* Deprecated */
	{ SYS(ftime), 0, { 0, 0, 0, 0, 0, 0 } },
#endif
#if DEFSYS(ftruncate)
	{ SYS(ftruncate), 2, { ARG_FD, ARG_LEN, 0, 0, 0, 0 } },
#endif
#if DEFSYS(ftruncate64)
	{ SYS(ftruncate64), 2, { ARG_FD, ARG_LEN, 0, 0, 0, 0 } },
#endif
#if DEFSYS(futex)
	{ SYS(futex), 6, { ARG_FUTEX_PTR, ARG_INT, ARG_INT, ARG_FUTEX_PTR, ARG_FUTEX_PTR, ARG_INT } },
#endif
#if DEFSYS(futex_waitv)
	{ SYS(futex_waitv), 5, { ARG_FUTEX_PTR, ARG_INT, ARG_FLAG, ARG_PTR, ARG_INT, 0 } },
#endif
#if DEFSYS(futex_time64)
	{ SYS(futex_time64), 6, { ARG_FUTEX_PTR, ARG_INT, ARG_INT, ARG_FUTEX_PTR, ARG_FUTEX_PTR, ARG_INT } },
#endif
#if DEFSYS(futimens)
	{ SYS(futimens), 4, { ARG_FD, ARG_EMPTY_FILENAME, ARG_PTR, ARG_FLAG, 0, 0 } },
#endif
#if DEFSYS(futimesat)
	/* Obsolete */
	{ SYS(futimesat), 4, { ARG_FD, ARG_EMPTY_FILENAME, ARG_PTR, 0 , 0, 0 } },
#endif
#if DEFSYS(get_kernel_syms)
	/* deprecated in 2.6 */
	{ SYS(get_kernel_syms), 1, { ARG_PTR, 0, 0, 0, 0, 0 } },
#endif
#if DEFSYS(get_mempolicy)
	{ SYS(get_mempolicy), 5, { ARG_PTR, ARG_PTR, ARG_UINT, ARG_PTR, ARG_FLAG, 0 } },
#endif
#if DEFSYS(get_robust_list)
	{ SYS(get_robust_list), 3, { ARG_PID, ARG_PTR, ARG_PTR, 0, 0, 0 } },
#endif
#if DEFSYS(get_thread_area)
	{ SYS(get_thread_area), 1, { ARG_PTR, 0, 0, 0, 0, 0 } },
#endif
#if DEFSYS(get_tls)
	/* ARM OABI only */
	{ SYS(get_tls), 0, { 0, 0, 0, 0, 0, 0 } },
#endif
#if DEFSYS(getcpu)
	{ SYS(getcpu), 3, { ARG_NON_NULL_PTR, ARG_NON_NULL_PTR, ARG_PTR, 0, 0, 0 } },
#endif
#if DEFSYS(getcwd)
	{ SYS(getcwd), 2, { ARG_PTR, ARG_LEN, 0, 0, 0, 0 } },
#endif
#if DEFSYS(getdtablesize)
	/* SPARC, removed in 2.6.26 */
#endif
#if DEFSYS(getdents)
	{ SYS(getdents), 3, { ARG_FD, ARG_PTR, ARG_LEN, 0, 0, 0 } },
#endif
#if DEFSYS(getdents64)
	{ SYS(getdents64), 3, { ARG_FD, ARG_PTR, ARG_LEN, 0, 0, 0 } },
#endif
#if DEFSYS(getdomainname)
	{ SYS(getdomainname), 2, { ARG_PTR, ARG_LEN, 0, 0, 0, 0 } },
#endif
#if DEFSYS(getdtablesize)
	{ SYS(getdtablesize), 0, { 0, 0, 0, 0, 0, 0 } },
#endif
#if DEFSYS(getegid)
	{ SYS(getegid), 0, { 0, 0, 0, 0, 0, 0 } },
#endif
#if DEFSYS(getegid32)
	{ SYS(getegid32), 0, { 0, 0, 0, 0, 0, 0 } },
#endif
#if DEFSYS(geteuid)
	{ SYS(geteuid), 0, { 0, 0, 0, 0, 0, 0 } },
#endif
#if DEFSYS(geteuid32)
	{ SYS(geteuid32), 0, { 0, 0, 0, 0, 0, 0 } },
#endif
#if DEFSYS(getgid)
	{ SYS(getgid), 0, { 0, 0, 0, 0, 0, 0 } },
#endif
#if DEFSYS(getgid32)
	{ SYS(getgid32), 0, { 0, 0, 0, 0, 0, 0 } },
#endif
#if DEFSYS(getgroups)
	{ SYS(getgroups), 2, { ARG_INT, ARG_PTR, 0, 0, 0, 0 } },
#endif
#if DEFSYS(getgroups32)
	{ SYS(getgroups32), 2, { ARG_INT, ARG_PTR, 0, 0, 0, 0 } },
#endif
#if DEFSYS(gethostname)
	{ SYS(gethostname), 2, { ARG_PTR, ARG_LEN, 0, 0, 0, 0 } },
#endif
#if DEFSYS(getitimer)
	{ SYS(getitimer), 2, { ARG_INT, ARG_PTR, 0, 0, 0, 0 } },
#endif
#if DEFSYS(getpagesize)
	{ SYS(getpagesize), 0, { 0, 0, 0, 0, 0, 0 } },
#endif
#if DEFSYS(getpeername)
	{ SYS(getpeername), 3, { ARG_SOCKFD, ARG_PTR, ARG_PTR, 0, 0, 0 } },
#endif
#if DEFSYS(getpgid)
	{ SYS(getpgid), 1, { ARG_PID, 0, 0, 0, 0, 0 } },
#endif
#if DEFSYS(getpid)
	{ SYS(getpid), 0, { 0, 0, 0, 0, 0, 0 } },
#endif
#if DEFSYS(getpgrp)
	{ SYS(getpgrp), 0, { 0, 0, 0, 0, 0, 0 } },
#endif
#if DEFSYS(getpmsg)
	/* Unimplemented */
	/* { SYS(getpmsg), 0, { 0, 0, 0, 0, 0, 0 } }, */
#endif
#if DEFSYS(getppid)
	{ SYS(getppid), 0, { 0, 0, 0, 0, 0, 0 } },
#endif
#if DEFSYS(getpriority)
	{ SYS(getpriority), 2, { ARG_INT, ARG_INT, 0, 0, 0, 0 } },
#endif
#if DEFSYS(getrandom)
	{ SYS(getrandom), 3, { ARG_PTR, ARG_INT, ARG_FLAG, 0, 0, 0 } },
#endif
#if DEFSYS(getresgid)
	{ SYS(getresgid), 3, { ARG_PTR, ARG_PTR, ARG_PTR, 0, 0, 0 } },
#endif
#if DEFSYS(getresgid32)
	{ SYS(getresgid32), 3, { ARG_PTR, ARG_PTR, ARG_PTR, 0, 0, 0 } },
#endif
#if DEFSYS(getresuid)
	{ SYS(getresuid), 3, { ARG_PTR, ARG_PTR, ARG_PTR, 0, 0, 0 } },
#endif
#if DEFSYS(getresuid32)
	{ SYS(getresuid32), 3, { ARG_PTR, ARG_PTR, ARG_PTR, 0, 0, 0 } },
#endif
#if DEFSYS(getrlimit)
	{ SYS(getrlimit), 2, { ARG_RND, ARG_PTR, 0, 0, 0, 0 } },
	{ SYS(getrlimit), 2, { ARG_INT, ARG_PTR, 0, 0, 0, 0 } },
#endif
#if DEFSYS(getrusage)
	{ SYS(getrusage), 2, { ARG_RND, ARG_PTR, 0, 0, 0, 0 } },
	{ SYS(getrusage), 2, { ARG_INT, ARG_PTR, 0, 0, 0, 0 } },
#endif
#if DEFSYS(getsid)
	{ SYS(getsid), 1, { ARG_PID, 0, 0, 0, 0, 0 } },
#endif
#if DEFSYS(getsockname)
	{ SYS(getsockname), 3, { ARG_SOCKFD, ARG_PTR | ARG_STRUCT_SOCKADDR, ARG_PTR, 0, 0, 0 } },
#endif
#if DEFSYS(getsockopt)
	{ SYS(getsockopt), 5, { ARG_SOCKFD, ARG_INT, ARG_INT, ARG_PTR, ARG_PTR, 0 } },
#endif
#if DEFSYS(gettid)
	{ SYS(gettid), 0, { 0, 0, 0, 0, 0, 0 } },
#endif
#if DEFSYS(gettimeofday)
	{ SYS(gettimeofday), 2, { ARG_NON_NULL_PTR, ARG_NON_NULL_PTR, 0, 0, 0, 0 } },
	{ SYS(gettimeofday), 2, { ARG_PTR, ARG_NON_NULL_PTR, 0, 0, 0, 0 } },
	{ SYS(gettimeofday), 2, { ARG_NON_NULL_PTR, ARG_PTR, 0, 0, 0, 0 } },
	{ SYS(gettimeofday), 2, { ARG_PTR, ARG_PTR, 0, 0, 0, 0 } },
#endif
#if DEFSYS(getuid)
	{ SYS(getuid), 0, { 0, 0, 0, 0, 0, 0 } },
#endif
#if DEFSYS(getuid32)
	{ SYS(getuid32), 0, { 0, 0, 0, 0, 0, 0 } },
#endif
#if DEFSYS(getunwind)
	/* IA-64-specific, obsolete too */
	{ SYS(getunwind), 2, { ARG_PTR, ARG_LEN, 0, 0, 0, 0 } },
#endif
#if DEFSYS(getxattr)
	{ SYS(getxattr), 4, { ARG_EMPTY_FILENAME, ARG_PTR, ARG_PTR, ARG_LEN, 0, 0 } },
	{ SYS(getxattr), 4, { ARG_DEVNULL_FILENAME, ARG_PTR, ARG_PTR, ARG_LEN, 0, 0 } },
#endif
#if DEFSYS(getxgid)
	/* Alpha only */
	{ SYS(getxgid), 0, { 0, 0, 0, 0, 0, 0 } },
#endif
#if DEFSYS(getxpid)
	/* Alpha only */
	{ SYS(getxpid), 0, { 0, 0, 0, 0, 0, 0 } },
#endif
#if DEFSYS(getxuid)
	/* Alpha only */
	{ SYS(getxuid), 0, { 0, 0, 0, 0, 0, 0 } },
#endif
#if DEFSYS(idle)
	{ SYS(idle), 0, { 0, 0, 0, 0, 0, 0 } },
#endif
#if DEFSYS(init_module)
	{ SYS(init_module), 3, { ARG_PTR, ARG_LEN, ARG_PTR } },
#endif
#if DEFSYS(inotify_add_watch)
	{ SYS(inotify_add_watch), 3, { ARG_FD, ARG_EMPTY_FILENAME, ARG_UINT, 0, 0, 0 } },
	{ SYS(inotify_add_watch), 3, { ARG_FD, ARG_DEVNULL_FILENAME, ARG_UINT, 0, 0, 0 } },
#endif
#if DEFSYS(inotify_init)
	{ SYS(inotify_init), 0, { 0, 0, 0, 0, 0, 0 } },
#endif
#if DEFSYS(inotify_init1)
	{ SYS(inotify_init1), 3, { ARG_FLAG, 0, 0, 0, 0, 0 } },
#endif
#if DEFSYS(inotify_rm_watch)
	{ SYS(inotify_rm_watch), 2, { ARG_FD, ARG_INT, 0, 0, 0, 0 } },
#endif
#if DEFSYS(io_cancel)
	{ SYS(io_cancel), 3, { ARG_INT, ARG_PTR, ARG_PTR, 0, 0, 0 } },
#endif
#if DEFSYS(io_destroy)
	{ SYS(io_destroy), 1, { ARG_INT, 0, 0, 0, 0, 0 } },
#endif
#if DEFSYS(io_getevents)
	{ SYS(io_getevents), 5, { ARG_INT, ARG_INT, ARG_INT, ARG_PTR, ARG_PTR, 0 } },
#endif
#if DEFSYS(io_pgetevents)
	{ SYS(io_pgetevents), 6, { ARG_INT, ARG_INT, ARG_INT, ARG_PTR, ARG_PTR, ARG_PTR } },
#endif
#if DEFSYS(io_pgetevents_time32)
	{ SYS(io_pgetevents_time32), 6, { ARG_INT, ARG_INT, ARG_INT, ARG_PTR, ARG_PTR, ARG_PTR } },
#endif
#if DEFSYS(io_pgetevents_time64)
	{ SYS(io_pgetevents_time64), 6, { ARG_INT, ARG_INT, ARG_INT, ARG_PTR, ARG_PTR, ARG_PTR } },
#endif
#if DEFSYS(io_setup)
	{ SYS(io_setup), 2, { ARG_UINT, ARG_PTR, 0, 0, 0, 0 } },
#endif
#if DEFSYS(io_submit)
	{ SYS(io_setup), 3, { ARG_UINT, ARG_INT, ARG_PTR, 0, 0, 0 } },
#endif
#if DEFSYS(io_uring_enter)
	{ SYS(io_uring_enter), 6, { ARG_FD, ARG_UINT, ARG_UINT, ARG_UINT, ARG_PTR, ARG_LEN } },
#endif
#if DEFSYS(io_uring_register)
	{ SYS(io_uring_register), 4, { ARG_FD, ARG_UINT, ARG_PTR, ARG_UINT, 0, 0 } },
#endif
#if DEFSYS(io_uring_setup)
	{ SYS(io_uring_setup), 2, { ARG_LEN, ARG_PTR, 0, 0, 0, 0 } },
#endif
#if DEFSYS(ioctl)
	{ SYS(ioctl), 4, { ARG_FD, ARG_UINT, ARG_PTR, ARG_PTR, 0, 0 } },
#endif
#if DEFSYS(ioperm)
	{ SYS(ioperm), 3, { ARG_UINT, ARG_UINT, ARG_INT, 0, 0, 0 } },
#endif
#if DEFSYS(iopl)
	{ SYS(iopl), 1, { ARG_INT, 0, 0, 0, 0, 0 } },
#endif
#if DEFSYS(ioprio_get)
	{ SYS(ioprio_get), 2, { ARG_INT, ARG_INT, 0, 0, 0, 0 } },
#endif
#if DEFSYS(ioprio_set)
	{ SYS(ioprio_set), 3, { ARG_INT, ARG_INT, ARG_INT, 0, 0, 0 } },
#endif
#if DEFSYS(ipc)
	{ SYS(ipc), 6, { ARG_UINT, ARG_INT, ARG_INT, ARG_INT, ARG_PTR, ARG_UINT } },
#endif
#if DEFSYS(kcmp)
	{ SYS(kcmp), 5, { ARG_PID, ARG_PID, ARG_INT, ARG_UINT, ARG_UINT, 0 } },
#endif
#if DEFSYS(kern_features)
	/* SPARC64 only */
	{ SYS(kern_features), 0, { 0, 0, 0, 0, 0, 0 } },
#endif
#if DEFSYS(kexec_file_load)
	/* { SYS(kexec_file_load), 5, { ARG_FD, ARG_FD, ARG_UINT, ARG_PTR, ARG_FLAG, 0 } }, */
#endif
#if DEFSYS(kexec_load)
	/* { SYS(kexec_load), 4, { ARG_UINT, ARG_UINT, ARG_PTR, ARG_FLAG, 0, 0 } }, */
#endif
#if DEFSYS(keyctl)
	{ SYS(keyctl), 6, { ARG_INT, ARG_UINT, ARG_UINT, ARG_UINT, ARG_UINT, ARG_UINT } },
#endif
#if DEFSYS(kill)
	/* { SYS(kill), ARG_PID, ARG_INT, 0, 0, 0, 0 } }, */
#endif
#if DEFSYS(landlock_add_rule)
	{ SYS(landlock_create_ruleset), 4, { ARG_FD, ARG_INT, ARG_PTR, ARG_FLAG, 0, 0 } },
#endif
#if DEFSYS(landlock_create_ruleset)
	{ SYS(landlock_create_ruleset), 3, { ARG_PTR, ARG_LEN, ARG_FLAG, 0, 0, 0 } },
#endif
#if DEFSYS(landlock_restrict_self)
	{ SYS(landlock_restrict_self), 2, { ARG_FD, ARG_FLAG, 0, 0, 0, 0 } },
#endif
#if DEFSYS(lchown)
	{ SYS(lchown), 3, { ARG_EMPTY_FILENAME, ARG_INT, ARG_INT, 0, 0, 0 } },
#endif
#if DEFSYS(lchown32)
	{ SYS(lchown32), 3, { ARG_EMPTY_FILENAME, ARG_INT, ARG_INT, 0, 0, 0 } },
#endif
#if DEFSYS(lgetxattr)
	{ SYS(lgetxattr), 4, { ARG_EMPTY_FILENAME, ARG_PTR, ARG_PTR, ARG_LEN, 0, 0 } },
	{ SYS(lgetxattr), 4, { ARG_DEVNULL_FILENAME, ARG_PTR, ARG_PTR, ARG_LEN, 0, 0 } },
#endif
#if DEFSYS(link)
	{ SYS(link), 2, { ARG_EMPTY_FILENAME, ARG_PTR, 0, 0, 0, 0 } },
#endif
#if DEFSYS(linkat)
	{ SYS(linkat), 5, { ARG_FD, ARG_EMPTY_FILENAME, ARG_FD, ARG_EMPTY_FILENAME, ARG_INT, 0 } },
#endif
#if DEFSYS(listen)
	{ SYS(listen), 2, { ARG_SOCKFD, ARG_INT, 0, 0, 0, 0 } },
#endif
#if DEFSYS(listxattr)
	{ SYS(listxattr), 3, { ARG_EMPTY_FILENAME, ARG_PTR, ARG_LEN, 0, 0, 0 } },
	{ SYS(listxattr), 3, { ARG_DEVNULL_FILENAME, ARG_PTR, ARG_LEN, 0, 0, 0 } },
#endif
#if DEFSYS(llistxattr)
	{ SYS(llistxattr), 3, { ARG_EMPTY_FILENAME, ARG_PTR, ARG_LEN, 0, 0, 0 } },
	{ SYS(llistxattr), 3, { ARG_DEVNULL_FILENAME, ARG_PTR, ARG_LEN, 0, 0, 0 } },
#endif
#if DEFSYS(llseek)
	{ SYS(llseek), 5, { ARG_FD, ARG_UINT, ARG_UINT, ARG_PTR, ARG_UINT, 0 } },
#endif
#if DEFSYS(lock)
	/* Unimplemented, deprecated */
#endif
#if DEFSYS(lookup_dcookie)
	{ SYS(lookup_dcookie), 3, { ARG_UINT, ARG_PTR, ARG_LEN, 0, 0, 0 } },
#endif
#if DEFSYS(lremovexattr)
	{ SYS(lremovexattr), 3, { ARG_EMPTY_FILENAME, ARG_PTR, 0, 0, 0, 0 } },
#endif
#if DEFSYS(lseek)
	{ SYS(lseek), 3, { ARG_FD, ARG_UINT, ARG_INT, 0, 0, 0 } },
#endif
#if DEFSYS(lsetxattr)
	{ SYS(lsetxattr), 5, { ARG_EMPTY_FILENAME, ARG_PTR, ARG_PTR, ARG_LEN, ARG_INT, 0 } },
#endif
#if DEFSYS(lstat)
	{ SYS(lstat), 2, { ARG_EMPTY_FILENAME, ARG_PTR, 0, 0, 0, 0 } },
	{ SYS(lstat), 2, { ARG_DEVNULL_FILENAME, ARG_PTR, 0, 0, 0, 0 } },
#endif
#if DEFSYS(lstat64)
	{ SYS(lstat64), 2, { ARG_EMPTY_FILENAME, ARG_PTR, 0, 0, 0, 0 } },
	{ SYS(lstat64), 2, { ARG_DEVNULL_FILENAME, ARG_PTR, 0, 0, 0, 0 } },
#endif
#if DEFSYS(lws_enties)
	/* PARISC, todo */
#endif
#if DEFSYS(map_shadow_stack)
	{ SYS(map_shadow_stack), 4, { ARG_PTR, ARG_LEN, ARG_INT, ARG_INT, 0, 0 } },
#endif
#if DEFSYS(madvise)
	{ SYS(madvise), 3, { ARG_PTR, ARG_LEN, ARG_INT, 0, 0, 0 } },
#endif
#if DEFSYS(madvise1)
	/* Unimplemented, deprecated */
#endif
#if DEFSYS(map_shadow_stack)
	{ SYS(map_shadow_stack), 3, { ARG_PTR, ARG_LEN, ARG_FLAG, 0, 0, 0 } },
#endif
#if DEFSYS(mbind)
	{ SYS(mbind), 6, { ARG_PTR, ARG_UINT, ARG_INT, ARG_PTR, ARG_UINT, ARG_UINT } },
#endif
#if DEFSYS(memory_ordering)
	/* SPARC64 only */
	{ SYS(memory_ordering), 1, { ARG_UINT, 0, 0, 0, 0, 0 } },
#endif
#if DEFSYS(membarrier)
	{ SYS(membarrier), 2, { ARG_INT, ARG_FLAG, 0, 0, 0, 0 } },
#endif
#if DEFSYS(memfd_create)
	{ SYS(memfd_create), 2, { ARG_EMPTY_FILENAME, ARG_UINT, 0, 0, 0, 0 } },
#endif
#if DEFSYS(memfd_secret)
	{ SYS(memfd_secret), 1, { ARG_UINT, 0, 0, 0, 0, 0 } },
#endif
#if DEFSYS(memory_ordering)
	{ SYS(memory_ordering), 1, { ARG_UINT, 0, 0, 0, 0, 0 } },
#endif
#if DEFSYS(migrate_pages)
	{ SYS(migrate_pages), 4, { ARG_PID, ARG_UINT, ARG_PTR, ARG_PTR, 0, 0 } },
#endif
#if DEFSYS(mincore)
	{ SYS(mincore), 3, { ARG_PTR, ARG_LEN, ARG_PTR, 0, 0, 0 } },
#endif
#if DEFSYS(mkdir)
	{ SYS(mkdir), 2, { ARG_EMPTY_FILENAME, ARG_MODE, 0, 0, 0, 0 } },
#endif
#if DEFSYS(mkdirat)
	{ SYS(mkdirat), 3, { ARG_DIRFD, ARG_EMPTY_FILENAME, ARG_MODE, 0, 0, 0 } },
#endif
#if DEFSYS(mknod)
	{ SYS(mknod), 3, { ARG_EMPTY_FILENAME, ARG_MODE, ARG_UINT, 0, 0, 0 } },
#endif
#if DEFSYS(mknodat)
	{ SYS(mknodat), 4, { ARG_DIRFD, ARG_EMPTY_FILENAME, ARG_MODE, ARG_UINT, 0, 0 } },
#endif
#if DEFSYS(mlock)
	{ SYS(mlock), 2, { ARG_PTR, ARG_LEN, 0, 0, 0, 0 } },
#endif
#if DEFSYS(mlock2)
	{ SYS(mlock2), 2, { ARG_PTR, ARG_LEN, ARG_FLAG, 0, 0, 0 } },
#endif
#if DEFSYS(mlockall)
	{ SYS(mlockall), 1, { ARG_FLAG, 0, 0, 0, 0, 0 } },
#endif
#if DEFSYS(mmap)
	{ SYS(mmap), 6, { ARG_PTR, ARG_LEN, ARG_INT, ARG_FLAG, ARG_FD, ARG_UINT } },
#endif
#if DEFSYS(mmap2)
	{ SYS(mmap2), 6, { ARG_PTR, ARG_LEN, ARG_INT, ARG_FLAG, ARG_FD, ARG_UINT } },
#endif
#if DEFSYS(mmap_pgoff)
	{ SYS(mmap_pgoff), 6, { ARG_PTR, ARG_LEN, ARG_INT, ARG_FLAG, ARG_FD, ARG_UINT } },
#endif
#if DEFSYS(modify_ldt)
	{ SYS(modify_ldt), 3, { ARG_INT, ARG_PTR, ARG_LEN, 0, 0, 0 } },
#endif
#if DEFSYS(mount)
	{ SYS(mount), 5, { ARG_EMPTY_FILENAME, ARG_EMPTY_FILENAME, ARG_PTR, ARG_UINT, ARG_UINT, 0 } },
#endif
#if DEFSYS(mount_setattr)
	/* { SYS(mount_setattr), 5, { ARG_DIRFD, ARG_EMPTY_FILENAME, ARG_UINT, ARG_PTR, ARG_LEN, 0 } }, */
#endif
#if DEFSYS(move_mount)
	/* { SYS(move_mount), 1, { 0, 0, 0, 0, 0, 0 } }, */
#endif
#if DEFSYS(move_pages)
	{ SYS(move_pages), 6, { ARG_PID, ARG_UINT, ARG_PTR, ARG_PTR, ARG_PTR, ARG_FLAG } },
#endif
#if DEFSYS(mprotect)
	{ SYS(mprotect), 3, { ARG_PTR, ARG_LEN, ARG_UINT, 0, 0, 0 } },
#endif
#if DEFSYS(mpx)
	/* Unimplemented, deprecated */
#endif
#if DEFSYS(mq_close)
	{ SYS(mq_close), 1, { ARG_INT, 0, 0, 0, 0, 0 } },
#endif
#if DEFSYS(mq_getsetattr)
	{ SYS(mq_getsetattr), 3, { ARG_INT, ARG_PTR, ARG_PTR, 0, 0, 0 } },
#endif
#if DEFSYS(mq_notify)
	{ SYS(mq_notify), 2, { ARG_INT, ARG_PTR, 0, 0, 0, 0 } },
#endif
#if DEFSYS(mq_open)
	{ SYS(mq_open), 4, { ARG_EMPTY_FILENAME, ARG_FLAG, ARG_MODE, ARG_PTR, 0, 0 } },
	{ SYS(mq_open), 4, { ARG_DEVNULL_FILENAME, ARG_FLAG, ARG_MODE, ARG_PTR, 0, 0 } },
#endif
#if DEFSYS(mq_receive)
	{ SYS(mq_receive), 4, { ARG_INT, ARG_PTR, ARG_LEN, ARG_INT, 0, 0 } },
#endif
#if DEFSYS(mq_send)
	{ SYS(mq_send), 4, { ARG_INT, ARG_PTR, ARG_LEN, ARG_UINT, 0, 0 } },
#endif
#if DEFSYS(mq_timedreceive)
	{ SYS(mq_timedreceive), 4, { ARG_INT, ARG_PTR, ARG_LEN, ARG_PTR, 0, 0 } },
#endif
#if DEFSYS(mq_timedreceive_time64)
	{ SYS(mq_timedreceive_time64), 4, { ARG_INT, ARG_PTR, ARG_LEN, ARG_PTR, 0, 0 } },
#endif
#if DEFSYS(mq_timedsend)
	{ SYS(mq_timedsend), 4, { ARG_INT, ARG_PTR, ARG_LEN, ARG_INT, 0, 0 } },
#endif
#if DEFSYS(mq_timedsend_time64)
	{ SYS(mq_timedsend_time64), 4, { ARG_INT, ARG_PTR, ARG_LEN, ARG_INT, 0, 0 } },
#endif
#if DEFSYS(mq_unlink)
	{ SYS(mq_unlink), 1, { ARG_EMPTY_FILENAME, 0, 0, 0, 0, 0 } },
#endif
#if DEFSYS(mremap)
	{ SYS(mremap), 5, { ARG_PTR, ARG_LEN, ARG_PTR, ARG_LEN, ARG_FLAG, ARG_PTR } },
#endif
#if DEFSYS(mseal)
	{ SYS(mseal), 3, { ARG_PTR, ARG_LEN, ARG_INT, 0, 0, 0 } },
#endif
#if DEFSYS(msgctl)
	{ SYS(msgctl), 3, { ARG_INT, ARG_INT, ARG_PTR, 0, 0, 0 } },
#endif
#if DEFSYS(msgget)
	{ SYS(msgget), 2, { ARG_INT, ARG_INT, 0, 0, 0, 0 } },
#endif
#if DEFSYS(msgrcv)
	{ SYS(msgrcv), 5, { ARG_INT, ARG_PTR, ARG_LEN, ARG_INT, ARG_INT, 0 } },
#endif
#if DEFSYS(msgsnd)
	{ SYS(msgsnd), 4, { ARG_INT, ARG_PTR, ARG_LEN, ARG_INT, 0, 0 } },
#endif
#if DEFSYS(msync)
	{ SYS(msync), 3, { ARG_PTR, ARG_LEN, ARG_FLAG, 0, 0, 0 } },
#endif
#if DEFSYS(multiplexer)
	/* { SYS(multiplexer), 0, { 0, 0, 0, 0, 0, 0 } }, */
#endif
#if DEFSYS(munlock)
	{ SYS(munlock), 2, { ARG_PTR, ARG_LEN, 0, 0, 0, 0 } },
#endif
#if DEFSYS(munlockall)
	{ SYS(munlockall), 1, { ARG_INT, 0, 0, 0, 0, 0 } },
#endif
#if DEFSYS(munmap)
	/* { SYS(munmap), 2, { ARG_PTR, ARG_LEN, 0, 0, 0, 0 } }, */
#endif
#if DEFSYS(name_to_handle_at)
	{ SYS(name_to_handle_at), 5, { ARG_DIRFD, ARG_EMPTY_FILENAME, ARG_PTR, ARG_PTR, ARG_FLAG } },
#endif
#if DEFSYS(nanosleep)
	{ SYS(nanosleep), 2, { ARG_PTR, ARG_PTR, 0, 0, 0, 0 } },
#endif
#if DEFSYS(newfstat)
	{ SYS(newfstat), 2, { ARG_FD, ARG_PTR, 0, 0, 0, 0 } },
#endif
#if DEFSYS(newfstat64)
	{ SYS(newfstat64), 2, { ARG_FD, ARG_PTR, 0, 0, 0, 0 } },
#endif
#if DEFSYS(newfstatat)
	{ SYS(newfstatat), 4, { ARG_DIRFD, ARG_EMPTY_FILENAME, ARG_PTR, ARG_INT, 0, 0 } },
#endif
#if DEFSYS(newfstatat64)
	{ SYS(newfstatat64), 4, { ARG_DIRFD, ARG_EMPTY_FILENAME, ARG_PTR, ARG_INT, 0, 0 } },
#endif
#if DEFSYS(newlstat)
	{ SYS(newlstat), 2, { ARG_EMPTY_FILENAME, ARG_PTR, 0, 0, 0, 0 } },
	{ SYS(newlstat), 2, { ARG_DEVNULL_FILENAME, ARG_PTR, 0, 0, 0, 0 } },
#endif
#if DEFSYS(newstat)
	{ SYS(newstat), 2, { ARG_EMPTY_FILENAME, ARG_PTR, 0, 0, 0, 0 } },
	{ SYS(newstat), 2, { ARG_DEVNULL_FILENAME, ARG_PTR, 0, 0, 0, 0 } },
#endif
#if DEFSYS(newuname)
	{ SYS(newuname), 1, { ARG_PTR, 0, 0, 0, 0, 0 } },
#endif
#if DEFSYS(nfsservctl)
	{ SYS(nfsservctl), 3, { ARG_INT, ARG_PTR, ARG_PTR, 0, 0, 0 } },
#endif
#if DEFSYS(nice)
	{ SYS(nice), 1, { ARG_INT, 0, 0, 0, 0, 0 } },
#endif
#if DEFSYS(ni_syscall)
	/* Omit */
#endif
#if DEFSYS(old_adjtimex)
	{ SYS(old_adjtimex), 1, { ARG_PTR, 0, 0, 0, 0, 0 } },
#endif
#if DEFSYS(oldfstat)
	{ SYS(oldfstat), 2, { ARG_FD, ARG_PTR, 0, 0, 0, 0 } },
#endif
#if DEFSYS(old_getrlimit)
	{ SYS(old_getrlimit), 2, { ARG_RND, ARG_PTR, 0, 0, 0, 0 } },
	{ SYS(old_getrlimit), 2, { ARG_INT, ARG_PTR, 0, 0, 0, 0 } },
#endif
#if DEFSYS(oldlstat)
	{ SYS(oldlstat), 2, { ARG_FD, ARG_PTR, 0, 0, 0, 0 } },
#endif
#if DEFSYS(oldolduname)
	{ SYS(oldolduname), 1, { ARG_PTR, 0, 0, 0, 0, 0 } },
#endif
#if DEFSYS(oldstat)
	{ SYS(oldstat), 2, { ARG_EMPTY_FILENAME, ARG_PTR, 0, 0, 0, 0 } },
	{ SYS(oldstat), 2, { ARG_DEVNULL_FILENAME, ARG_PTR, 0, 0, 0, 0 } },
#endif
#if DEFSYS(oldumount)
	{ SYS(oldumount), 1, { ARG_EMPTY_FILENAME, 0, 0, 0, 0, 0 } },
	{ SYS(oldumount), 1, { ARG_DEVNULL_FILENAME, 0, 0, 0, 0, 0 } },
#endif
#if DEFSYS(olduname)
	{ SYS(olduname), 1, { ARG_PTR, 0, 0, 0, 0, 0 } },
#endif
#if DEFSYS(oldwait4)
	{ SYS(oldwait4), 4, { ARG_PID, ARG_PTR, ARG_INT, ARG_PTR, 0, 0 } },
#endif
#if DEFSYS(open)
	{ SYS(open), 3, { ARG_EMPTY_FILENAME, ARG_FLAG, ARG_MODE, 0, 0, 0 } },
#endif
#if DEFSYS(open_by_handle_at)
	{ SYS(open_by_handle_at), 3, { ARG_FD, ARG_PTR, ARG_FLAG, 0, 0, 0 } },
#endif
#if DEFSYS(open_tree)
	{ SYS(open_tree), 3, { ARG_DIRFD, ARG_EMPTY_FILENAME, ARG_UINT, 0, 0, 0 } },
#endif
#if DEFSYS(openat)
	{ SYS(openat), 4, { ARG_DIRFD, ARG_EMPTY_FILENAME, ARG_FLAG, ARG_MODE, 0, 0 } },
#endif
#if DEFSYS(openat2)
	{ SYS(openat2), 4, { ARG_DIRFD, ARG_EMPTY_FILENAME, ARG_PTR, ARG_LEN, 0, 0 } },
#endif
#if DEFSYS(or1k_atomic)
	/* OpenRISC 1000 only */
#endif
#if DEFSYS(pause)
	{ SYS(pause), 0, { 0, 0, 0, 0, 0, 0 } },
#endif
#if DEFSYS(pciconfig_iobase)
	/* { SYS(pciconfig_iobase), 3, { ARG_UINT, ARG_UINT, ARG_UINT, 0, 0, 0 } }, */
#endif
#if DEFSYS(pciconfig_read)
	/* { SYS(pciconfig_read), 3, { ARG_UINT, ARG_UINT, ARG_UINT, ARG_LEN, ARG_PTR, 0 } }, */
#endif
#if DEFSYS(pciconfig_write)
	/* { SYS(pciconfig_write), 3, { ARG_UINT, ARG_UINT, ARG_UINT, ARG_LEN, ARG_PTR, 0 } }, */
#endif
#if DEFSYS(perf_event_open)
	{ SYS(perf_event_open), 5, { ARG_PTR, ARG_PID, ARG_INT, ARG_INT, ARG_FLAG, 0 } },
#endif
#if DEFSYS(perfmonctl)
	{ SYS(perfmonctl), 4, { ARG_FD, ARG_INT, ARG_PTR, ARG_INT, 0, 0 } },
#endif
#if DEFSYS(personality)
	{ SYS(personality), 1, { ARG_UINT, 0, 0, 0, 0, 0 } },
#endif
#if DEFSYS(pidfd_getfd)
	{ SYS(pidfd_getfd), 3, { ARG_INT, ARG_INT, ARG_FLAG, 0, 0, 0 } },
#endif
#if DEFSYS(pidfd_open)
	{ SYS(pidfd_open), 2, { ARG_PID, ARG_FLAG, 0, 0, 0, 0 } },
#endif
#if DEFSYS(pidfd_send_signal)
	{ SYS(pidfd_send_signal), 4, { ARG_INT, ARG_INT, ARG_PTR, ARG_FLAG, 0, 0 } },
#endif
#if DEFSYS(pipe)
	{ SYS(pipe), 1, { ARG_PTR, 0, 0, 0, 0, 0 } },
	{ SYS(pipe), 1, { ARG_PTR, 0, 0, 0, 0, 0 } },
#endif
#if DEFSYS(pipe2)
	{ SYS(pipe2), 2, { ARG_PTR, ARG_FLAG, 0, 0, 0, 0 } },
#endif
#if DEFSYS(pivot_root)
	{ SYS(pivot_root), 2, { ARG_EMPTY_FILENAME, ARG_EMPTY_FILENAME, 0, 0, 0, 0 } },
	{ SYS(pivot_root), 2, { ARG_DEVNULL_FILENAME, ARG_EMPTY_FILENAME, 0, 0, 0, 0 } },
	{ SYS(pivot_root), 2, { ARG_EMPTY_FILENAME, ARG_DEVNULL_FILENAME, 0, 0, 0, 0 } },
	{ SYS(pivot_root), 2, { ARG_DEVNULL_FILENAME, ARG_DEVNULL_FILENAME, 0, 0, 0, 0 } },
#endif
#if DEFSYS(pkey_alloc)
	{ SYS(pkey_alloc), 2, { ARG_FLAG, ARG_UINT, 0, 0, 0, 0 } },
#endif
#if DEFSYS(pkey_free)
	{ SYS(pkey_free), 1, { ARG_INT, 0, 0, 0, 0, 0 } },
#endif
#if DEFSYS(pkey_get)
	{ SYS(pkey_get), 1, { ARG_INT, 0, 0, 0, 0, 0 } },
#endif
#if DEFSYS(pkey_mprotect)
	{ SYS(pkey_mprotect), 3, { ARG_PTR, ARG_LEN, ARG_INT, 0, 0, 0 } },
#endif
#if DEFSYS(pkey_set)
	{ SYS(pkey_set), 2, { ARG_INT, ARG_INT, 0, 0, 0, 0 } },
#endif
#if DEFSYS(poll)
	{ SYS(poll), 3, { ARG_PTR, ARG_INT, ARG_INT, 0, 0, 0 } },
#endif
#if DEFSYS(ppoll)
	{ SYS(ppoll), 4, { ARG_PTR, ARG_INT, ARG_PTR, ARG_PTR, 0, 0 } },
#endif
#if DEFSYS(ppoll_time64)
	{ SYS(ppoll_time64), 4, { ARG_PTR, ARG_INT, ARG_PTR, ARG_PTR, 0, 0 } },
#endif
#if DEFSYS(prctl)
	{ SYS(prctl), 5, { ARG_INT, ARG_UINT, ARG_UINT, ARG_UINT, ARG_UINT, 0 } },
#endif
#if DEFSYS(pread)
	{ SYS(pread), 4, { ARG_FD, ARG_PTR, ARG_LEN, ARG_UINT, 0, 0 } },
#endif
#if DEFSYS(pread64)
	{ SYS(pread64), 4, { ARG_FD, ARG_PTR, ARG_LEN, ARG_UINT, 0, 0 } },
#endif
#if DEFSYS(preadv)
	{ SYS(preadv), 4, { ARG_FD, ARG_PTR, ARG_INT, ARG_UINT, 0, 0 } },
#endif
#if DEFSYS(preadv2)
	{ SYS(preadv2), 4, { ARG_FD, ARG_PTR, ARG_INT, ARG_UINT, ARG_FLAG, 0 } },
#endif
#if DEFSYS(prlimit)
	{ SYS(prlimit), 2, { ARG_INT, ARG_PTR, ARG_PTR, 0, 0, 0 } },
#endif
#if DEFSYS(prlimit64)
	{ SYS(prlimit64), 2, { ARG_INT, ARG_PTR, ARG_PTR, 0, 0, 0 } },
#endif
#if DEFSYS(process_madvise)
	{ SYS(process_madvise), 6, { ARG_INT, ARG_PID, ARG_PTR, ARG_LEN, ARG_INT, ARG_FLAG } },
#endif
#if DEFSYS(process_mrelease)
	{ SYS(process_mrelease), 2, { ARG_PID, ARG_INT, 0, 0, 0, 0 } },
#endif
#if DEFSYS(process_vm_readv)
	{ SYS(process_vm_readv), 6, { ARG_PID, ARG_PTR, ARG_UINT, ARG_PTR, ARG_UINT, ARG_UINT } },
#endif
#if DEFSYS(process_vm_writev)
	{ SYS(process_vm_writev), 6, { ARG_PID, ARG_PTR, ARG_UINT, ARG_PTR, ARG_UINT, ARG_UINT } },
#endif
#if DEFSYS(prof)
	/* { SYS(prof), 0, { 0, 0, 0, 0, 0, 0 } }, */
#endif
#if DEFSYS(profil)
	/* { SYS(profil), 0, { 0, 0, 0, 0, 0, 0 } }, */
#endif
#if DEFSYS(pselect)
	{ SYS(pselect), 6, { ARG_INT, ARG_PTR, ARG_PTR, ARG_PTR, ARG_PTR, ARG_PTR } },
#endif
#if DEFSYS(pselect6)
	{ SYS(pselect6), 6, { ARG_INT, ARG_PTR, ARG_PTR, ARG_PTR, ARG_PTR, ARG_PTR } },
#endif
#if DEFSYS(pselect6_time64)
	{ SYS(pselect6_time64), 6, { ARG_INT, ARG_PTR, ARG_PTR, ARG_PTR, ARG_PTR, ARG_PTR } },
#endif
#if DEFSYS(ptrace)
	{ SYS(ptrace), 4, { ARG_INT, ARG_PID, ARG_PTR, ARG_PTR, 0, 0 } },
#endif
#if DEFSYS(putpmsg)
	/* { SYS(putpmsg), 0, { 0, 0, 0, 0, 0, 0 } }, */
#endif
#if DEFSYS(pwrite)
	{ SYS(pwrite), 4, { ARG_FD, ARG_PTR, ARG_LEN, ARG_UINT, 0, 0 } },
#endif
#if DEFSYS(pwrite64)
	{ SYS(pwrite64), 4, { ARG_FD, ARG_PTR, ARG_LEN, ARG_UINT, 0, 0 } },
#endif
#if DEFSYS(pwritev)
	{ SYS(pwritev), 4, { ARG_FD, ARG_PTR, ARG_INT, ARG_UINT, 0, 0 } },
#endif
#if DEFSYS(pwritev2)
	{ SYS(pwritev2), 4, { ARG_FD, ARG_PTR, ARG_INT, ARG_UINT, ARG_FLAG, 0 } },
#endif
#if DEFSYS(query_module)
	{ SYS(query_module), 5, { ARG_PTR, ARG_INT, ARG_PTR, ARG_LEN, ARG_PTR, 0 } },
#endif
#if DEFSYS(quotactl)
	{ SYS(quotactl), 4, { ARG_INT, ARG_PTR, ARG_INT, ARG_PTR, 0, 0 } },
#endif
#if DEFSYS(quotactl_fd)
	{ SYS(quotactl_fd), 4, { ARG_INT, ARG_INT, ARG_INT, ARG_PTR, 0, 0 } },
#endif
#if DEFSYS(read)
	{ SYS(read), 3, { ARG_FD, ARG_PTR, ARG_LEN, 0, 0, 0 } },
#endif
#if DEFSYS(readahead)
	{ SYS(readahead), 3, { ARG_FD, ARG_UINT, ARG_LEN, 0, 0, 0 } },
#endif
#if DEFSYS(readdir)
	{ SYS(readdir), 3, { ARG_FD, ARG_PTR, ARG_LEN, 0, 0, 0 } },
#endif
#if DEFSYS(readlink)
	{ SYS(readlink), 3, { ARG_EMPTY_FILENAME, ARG_PTR, ARG_LEN, 0, 0, 0 } },
	{ SYS(readlink), 3, { ARG_DEVNULL_FILENAME, ARG_PTR, ARG_LEN, 0, 0, 0 } },
#endif
#if DEFSYS(readlinkat)
	{ SYS(readlinkat), 4, { ARG_DIRFD, ARG_EMPTY_FILENAME, ARG_PTR, ARG_LEN, 0, 0 } },
	{ SYS(readlinkat), 4, { ARG_DIRFD, ARG_DEVNULL_FILENAME, ARG_PTR, ARG_LEN, 0, 0 } },
#endif
#if DEFSYS(readv)
	{ SYS(readv), 3, { ARG_FD, ARG_PTR, ARG_INT, 0, 0, 0 } },
#endif
#if DEFSYS(reboot)
	/* { SYS(reboot), 3, { ARG_INT, ARG_INT, ARG_PTR, 0, 0, 0 } }, */
#endif
#if DEFSYS(recv)
	{ SYS(recv), 4, { ARG_SOCKFD, ARG_PTR, ARG_LEN, ARG_FLAG, 0, 0 } },
#endif
#if DEFSYS(recvfrom)
	{ SYS(recvfrom), 6, { ARG_SOCKFD, ARG_PTR, ARG_LEN, ARG_FLAG, ARG_PTR, ARG_PTR } },
	{ SYS(recvfrom), 6, { ARG_SOCKFD, ARG_PTR, ARG_LEN, ARG_FLAG, ARG_PTR, ARG_PTR } },
#endif
#if DEFSYS(recvmsg)
	{ SYS(recvmsg), 3, { ARG_SOCKFD, ARG_PTR, ARG_FLAG, 0, 0, 0 } },
#endif
#if DEFSYS(recvmmsg)
	{ SYS(recvmmsg), 5, { ARG_SOCKFD, ARG_PTR, ARG_LEN, ARG_FLAG, ARG_PTR, 0 } },
#endif
#if DEFSYS(recvmmsg_time64)
	{ SYS(recvmmsg_time64), 5, { ARG_SOCKFD, ARG_PTR, ARG_LEN, ARG_FLAG, ARG_PTR, 0 } },
#endif
#if DEFSYS(remap_file_pages)
	{ SYS(remap_file_pages), 5, { ARG_PTR, ARG_LEN, ARG_INT, ARG_UINT, ARG_FLAG, 0 } },
#endif
#if DEFSYS(removexattr)
	{ SYS(removexattr), 2, { ARG_EMPTY_FILENAME, ARG_PTR, 0, 0, 0, 0 } },
#endif
#if DEFSYS(rename)
	{ SYS(rename), 2, { ARG_EMPTY_FILENAME, ARG_EMPTY_FILENAME, 0, 0, 0, 0 } },
#endif
#if DEFSYS(renameat)
	{ SYS(renameat), 4, { ARG_DIRFD, ARG_EMPTY_FILENAME, ARG_DIRFD, ARG_EMPTY_FILENAME, 0, 0 } },
#endif
#if DEFSYS(renameat2)
	{ SYS(renameat2), 5, { ARG_DIRFD, ARG_EMPTY_FILENAME, ARG_DIRFD, ARG_EMPTY_FILENAME, ARG_FLAG, 0 } },
#endif
#if DEFSYS(request_key)
	{ SYS(request_key), 4, { ARG_PTR, ARG_PTR, ARG_PTR, ARG_INT, 0, 0 } },
#endif
#if DEFSYS(restart_syscall)
	{ SYS(restart_syscall), 0, { 0, 0, 0, 0, 0, 0 } },
	{ SYS(restart_syscall), 0, { 0, 0, 0, 0, 0, 0 } },
#endif
#if DEFSYS(riscv_flush_icache)
	{ SYS(riscv_flush_icache), 3, { ARG_PTR, ARG_PTR, ARG_INT, 0, 0, 0 } },
#endif
#if DEFSYS(riscv_hwprobe)
	{ SYS(riscv_hwprobe), 5, { ARG_PTR, ARG_UINT, ARG_UINT, ARG_PTR, ARG_INT, 0 } },
#endif
#if DEFSYS(rmdir)
	/* { SYS(rmdir), 1, { ARG_EMPTY_FILENAME, 0, 0, 0, 0, 0 } }, */
#endif
#if DEFSYS(rseq)
	{ SYS(rseq), 4, { ARG_PTR, ARG_LEN, ARG_FLAG, ARG_UINT, 0, 0 } },
#endif
#if DEFSYS(sigaction)
	{ SYS(sigaction), 3, { ARG_INT, ARG_PTR, ARG_PTR, 0, 0, 0 } },
#endif
#if DEFSYS(rt_sigaction)
	{ SYS(rt_sigaction), 3, { ARG_INT, ARG_PTR, ARG_PTR, 0, 0, 0 } },
#endif
#if DEFSYS(rt_sigpending)
	{ SYS(rt_sigpending), 1, { ARG_PTR, 0, 0, 0, 0, 0 } },
#endif
#if DEFSYS(rt_sigprocmask)
	{ SYS(rt_sigprocmask), 4, { ARG_INT, ARG_PTR, ARG_PTR, ARG_LEN, 0, 0 } },
#endif
#if DEFSYS(rt_sigqueueinfo)
	{ SYS(rt_sigqueueinfo), 3, { ARG_PID, ARG_INT, ARG_PTR, 0, 0, 0 } },
#endif
#if DEFSYS(rt_sigreturn)
	/* { SYS(rt_sigreturn), 1, { ARG_PTR, 0, 0, 0, 0, 0 } }, */
#endif
#if DEFSYS(rt_sigsuspend)
	{ SYS(rt_sigsuspend), 1, { ARG_PTR, 0, 0, 0, 0, 0 } },
#endif
#if DEFSYS(rt_sigtimedwait)
	{ SYS(rt_sigtimedwait), 3, { ARG_PTR, ARG_PTR, ARG_PTR, 0, 0, 0 } },
#endif
#if DEFSYS(rt_sigtimedwait_64)
	{ SYS(rt_sigtimedwait_64), 3, { ARG_PTR, ARG_PTR, ARG_PTR, 0, 0, 0 } },
#endif
#if DEFSYS(rt_tgsigqueueinfo)
	{ SYS(rt_tgsigqueueinfo), 4, { ARG_PID, ARG_PID, ARG_INT, ARG_PTR, 0, 0 } },
#endif
#if DEFSYS(rtas)
	{ SYS(rtas), 1, { ARG_PTR, 0, 0, 0, 0, 0 } },
#endif
#if DEFSYS(s390_runtime_instr)
	{ SYS(s390_runtime_instr), 2, { ARG_INT, ARG_INT, 0, 0, 0, 0 } },
#endif
#if DEFSYS(s390_pci_mmio_read)
	/* { SYS(s390_pci_mmio_read), 3, { ARG_UINT, ARG_PTR, ARG_LEN, 0, 0, 0 } }, */
#endif
#if DEFSYS(s390_pci_mmio_write)
	/* { SYS(s390_pci_mmio_write), 3, { ARG_UINT, ARG_PTR, ARG_LEN, 0, 0, 0 } }, */
#endif
#if DEFSYS(s390_sthyi)
	{ SYS(s390_sthyi), 4, { ARG_UINT, ARG_PTR, ARG_PTR, ARG_FLAG, 0, 0 } },
#endif
#if DEFSYS(s390_guarded_storage)
	{ SYS(s390_guarded_storage), 4, { ARG_INT, ARG_PTR, 0, 0, 0, 0 } },
#endif
#if DEFSYS(sched_get_priority_max)
	{ SYS(sched_get_priority_max), 1, { ARG_INT, 0, 0, 0, 0, 0 } },
#endif
#if DEFSYS(sched_get_priority_min)
	{ SYS(sched_get_priority_min), 1, { ARG_INT, 0, 0, 0, 0, 0 } },
#endif
#if DEFSYS(sched_getaffinity)
	{ SYS(sched_getaffinity), 3, { ARG_PID, ARG_LEN, ARG_PTR, 0, 0, 0 } },
#endif
#if DEFSYS(sched_getattr)
	{ SYS(sched_getattr), 3, { ARG_PID, ARG_PTR, ARG_FLAG, 0, 0, 0 } },
#endif
#if DEFSYS(sched_getparam)
	{ SYS(sched_getparam), 2, { ARG_PID, ARG_PTR, 0, 0, 0, 0 } },
#endif
#if DEFSYS(sched_getscheduler)
	{ SYS(sched_getscheduler), 1, { ARG_PID, 0, 0, 0, 0, 0 } },
#endif
#if DEFSYS(sched_get_rr_interval)
	{ SYS(sched_get_rr_interval), 2, { ARG_PID, ARG_PTR, 0, 0, 0, 0 } },
#endif
#if DEFSYS(sched_set_affinity)
	/* SPARC & SPARC64 */
	{ SYS(sched_set_affinity), 3, { ARG_PID, ARG_LEN, ARG_PTR, 0, 0, 0 } },
#endif
#if DEFSYS(sched_setaffinity)
	{ SYS(sched_setaffinity), 3, { ARG_PID, ARG_LEN, ARG_PTR, 0, 0, 0 } },
#endif
#if DEFSYS(sched_setattr)
	{ SYS(sched_setattr), 3, { ARG_PID, ARG_PTR, ARG_FLAG, 0, 0, 0 } },
#endif
#if DEFSYS(sched_setparam)
	{ SYS(sched_setparam), 2, { ARG_PID, ARG_PTR, 0, 0, 0, 0 } },
#endif
#if DEFSYS(sched_yield)
	{ SYS(sched_yield), 0, { 0, 0, 0, 0, 0, 0 } },
#endif
#if DEFSYS(seccomp)
	{ SYS(seccomp), 3, { ARG_UINT, ARG_FLAG, ARG_PTR, 0, 0, 0 } },
#endif
#if DEFSYS(seccomp_exit)
	/* { SYS(seccomp_exit), 1, { ARG_INT, 0, 0, 0, 0, 0 } }, */
#endif
#if DEFSYS(seccomp_exit_32)
	/* { SYS(seccomp_exit_32), 1, { ARG_INT, 0, 0, 0, 0, 0 } }, */
#endif
#if DEFSYS(seccomp_read)
	{ SYS(seccomp_read), 3, { ARG_FD, ARG_PTR, ARG_LEN, 0, 0, 0 } },
#endif
#if DEFSYS(seccomp_read_32)
	{ SYS(seccomp_read_32), 3, { ARG_FD, ARG_PTR, ARG_LEN, 0, 0, 0 } },
#endif
#if DEFSYS(seccomp_sigreturn)
	/* { SYS(seccomp_sigreturn), 4, { ARG_PTR, ARG_PTR, ARG_PTR, ARG_PTR, 0, 0 } }, */
#endif
#if DEFSYS(seccomp_sigreturn_32)
	/* { SYS(seccomp_sigreturn_32), 4, { ARG_PTR, ARG_PTR, ARG_PTR, ARG_PTR, 0, 0 } }, */
#endif
#if DEFSYS(seccomp_write)
	{ SYS(seccomp_write), 3, { ARG_FD, ARG_PTR, ARG_LEN, 0, 0, 0 } },
#endif
#if DEFSYS(seccomp_write_32)
	{ SYS(seccomp_write_32), 3, { ARG_FD, ARG_PTR, ARG_LEN, 0, 0, 0 } },
#endif
#if DEFSYS(security)
	/* { SYS(security), 0, { 0, 0, 0, 0, 0, 0 } }, */
#endif
#if DEFSYS(select)
	{ SYS(select), 5, { ARG_FD, ARG_PTR, ARG_PTR, ARG_PTR, ARG_PTR, 0 } },
#endif
#if DEFSYS(semctl)
	{ SYS(semctl), 6, { ARG_INT, ARG_INT, ARG_INT, ARG_PTR, ARG_PTR, ARG_PTR } },
#endif
#if DEFSYS(semget)
	{ SYS(semget), 3, { ARG_INT, ARG_INT, ARG_FLAG, 0, 0, 0 } },
#endif
#if DEFSYS(semop)
	{ SYS(semop), 3, { ARG_INT, ARG_PTR, ARG_LEN, 0, 0, 0 } },
#endif
#if DEFSYS(semtimedop)
	{ SYS(semtimedop), 4, { ARG_INT, ARG_PTR, ARG_LEN, ARG_PTR, 0, 0 } },
#endif
#if DEFSYS(semtimedop_time64)
	{ SYS(semtimedop_time64), 4, { ARG_INT, ARG_PTR, ARG_LEN, ARG_PTR, 0, 0 } },
#endif
/*
 *  The following are not system calls, ignored for now
 */
#if 0
#if DEFSYS(sem_destroy)
	{ SYS(sem_destroy), 1, { ARG_PTR, 0, 0, 0, 0, 0 } },
#endif
#if DEFSYS(sem_init)
	{ SYS(sem_init), 3, { ARG_PTR, ARG_INT, ARG_INT, 0, 0, 0 } },
#endif
#if DEFSYS(sem_post)
	{ SYS(sem_post), 1, { ARG_PTR, 0, 0, 0, 0, 0 } },
#endif
#if DEFSYS(sem_wait)
	{ SYS(sem_wait), 1, { ARG_PTR, 0, 0, 0, 0, 0 } },
#endif
#if DEFSYS(sem_trywait)
	{ SYS(sem_trywait), 1, { ARG_PTR, 0, 0, 0, 0, 0 } },
#endif
#if DEFSYS(sem_timedwait)
	{ SYS(sem_timedwait), 2, { ARG_PTR, ARG_PTR, 0, 0, 0, 0 } },
#endif
#endif
#if DEFSYS(send)
	{ SYS(send), 4, { ARG_SOCKFD, ARG_PTR, ARG_LEN, ARG_FLAG, 0, 0 } },
#endif
#if DEFSYS(sendfile)
	{ SYS(sendfile), 4, { ARG_FD, ARG_FD, ARG_UINT, ARG_LEN, 0, 0 } },
#endif
#if DEFSYS(sendfile64)
	{ SYS(sendfile64), 4, { ARG_FD, ARG_FD, ARG_UINT, ARG_LEN, 0, 0 } },
#endif
#if DEFSYS(sendmmsg)
	{ SYS(sendmmsg), 4, { ARG_SOCKFD, ARG_PTR, ARG_INT, ARG_FLAG, 0, 0 } },
#endif
#if DEFSYS(sendmsg)
	{ SYS(sendmsg), 3, { ARG_SOCKFD, ARG_PTR, ARG_FLAG, 0, 0, 0 } },
#endif
#if DEFSYS(sendto)
	{ SYS(sendto), 6, { ARG_SOCKFD, ARG_PTR, ARG_LEN, ARG_FLAG, ARG_PTR, ARG_LEN } },
#endif
#if DEFSYS(set_mempolicy)
	{ SYS(set_mempolicy), 3, { ARG_INT, ARG_PTR, ARG_UINT, 0, 0, 0 } },
#endif
#if DEFSYS(set_mempolicy_home_node)
	{ SYS(set_mempolicy_home_node), 4, { ARG_UINT, ARG_UINT, ARG_UINT, ARG_UINT, 0, 0 } },
#endif
#if DEFSYS(set_robust_list)
	{ SYS(set_robust_list), 2, { ARG_PTR, ARG_LEN, 0, 0, 0, 0 } },
#endif
#if DEFSYS(set_thread_area)
	{ SYS(set_thread_area), 1, { ARG_PTR, 0, 0, 0, 0, 0 } },
#endif
#if DEFSYS(set_tid_address)
	{ SYS(set_tid_address), 1, { ARG_PTR, 0, 0, 0, 0, 0 } },
#endif
#if DEFSYS(set_tls)
	{ SYS(set_tls), 1, { ARG_UINT, 0, 0, 0, 0, 0 } },
#endif
#if DEFSYS(setdomainname)
	/* { SYS(setdomainname), 2, { ARG_PTR, ARG_LEN, 0, 0, 0, 0 } }, */
#endif
#if DEFSYS(setfsgid)
	{ SYS(setfsgid), 1, { ARG_GID, 0, 0, 0, 0, 0 } },
#endif
#if DEFSYS(setfsgid32)
	{ SYS(setfsgid32), 1, { ARG_GID, 0, 0, 0, 0, 0 } },
#endif
#if DEFSYS(setfsuid)
	{ SYS(setfsuid), 1, { ARG_GID, 0, 0, 0, 0, 0 } },
#endif
#if DEFSYS(setfsuid32)
	{ SYS(setfsuid32), 1, { ARG_GID, 0, 0, 0, 0, 0 } },
#endif
#if DEFSYS(setgid)
	{ SYS(setgid), 1, { ARG_GID, 0, 0, 0, 0, 0 } },
#endif
#if DEFSYS(setgid32)
	{ SYS(setgid32), 1, { ARG_GID, 0, 0, 0, 0, 0 } },
#endif
#if DEFSYS(setgroups)
	{ SYS(setgroups), 2, { ARG_LEN, ARG_PTR, 0, 0, 0, 0 } },
#endif
#if DEFSYS(setgroups32)
	{ SYS(setgroups32), 2, { ARG_LEN, ARG_PTR, 0, 0, 0, 0 } },
#endif
#if DEFSYS(sethae)
	/* ALPHA only */
	{ SYS(sethae), 1, { ARG_UINT, 0, 0, 0, 0, 0 } },
#endif
#if DEFSYS(sethostname)
	{ SYS(sethostname), 2, { ARG_PTR, ARG_LEN, 0, 0, 0, 0 } },
#endif
#if DEFSYS(setitimer)
	{ SYS(setitimer), 3, { ARG_INT, ARG_NON_NULL_PTR, ARG_NON_NULL_PTR, 0, 0, 0 } },
#endif
#if DEFSYS(setmntent)
	{ SYS(setmntent), 2, { ARG_EMPTY_FILENAME, ARG_PTR, 0, 0, 0, 0 } },
#endif
#if DEFSYS(setns)
	{ SYS(setns), 2, { ARG_FD, ARG_INT, 0, 0, 0, 0 } },
#endif
#if DEFSYS(setpgid)
	{ SYS(setpgid), 2, { ARG_PID, ARG_PID, 0, 0, 0, 0 } },
#endif
#if DEFSYS(setpgrp)
	/* ALPHA, alternative to setpgid */
	{ SYS(setpgrp), 2, { ARG_PID, ARG_PID, 0, 0, 0, 0 } },
#endif
#if DEFSYS(setpriority)
	{ SYS(setpriority), 3, { ARG_INT, ARG_INT, ARG_INT, 0, 0, 0 } },
#endif
#if DEFSYS(setregid)
	{ SYS(setregid), 2, { ARG_GID, ARG_GID, 0, 0, 0, 0 } },
#endif
#if DEFSYS(setregid32)
	{ SYS(setregid32), 2, { ARG_GID, ARG_GID, 0, 0, 0, 0 } },
#endif
#if DEFSYS(setresgid)
	{ SYS(setresgid), 3, { ARG_GID, ARG_GID, ARG_GID, 0, 0, 0 } },
#endif
#if DEFSYS(setresgid32)
	{ SYS(setresgid32), 3, { ARG_GID, ARG_GID, ARG_GID, 0, 0, 0 } },
#endif
#if DEFSYS(setresuid)
	{ SYS(setresuid), 3, { ARG_UID, ARG_UID, ARG_UID, 0, 0, 0 } },
#endif
#if DEFSYS(setresuid32)
	{ SYS(setresuid32), 3, { ARG_UID, ARG_UID, ARG_UID, 0, 0, 0 } },
#endif
#if DEFSYS(setreuid)
	{ SYS(setreuid), 2, { ARG_UID, ARG_UID, 0, 0, 0, 0 } },
#endif
#if DEFSYS(setreuid32)
	{ SYS(setreuid32), 2, { ARG_UID, ARG_UID, 0, 0, 0, 0 } },
#endif
#if DEFSYS(setrlimit)
	{ SYS(setrlimit), 2, { ARG_INT, ARG_PTR, 0, 0, 0, 0 } },
#endif
#if DEFSYS(setsid)
	{ SYS(setsid), 0, { 0, 0, 0, 0, 0, 0 } },
#endif
#if DEFSYS(setsockopt)
	{ SYS(setsockopt), 5, { ARG_SOCKFD, ARG_INT, ARG_INT, ARG_PTR, ARG_LEN, 0 } },
#endif
#if DEFSYS(settimeofday)
	{ SYS(settimeofday), 2, { ARG_PTR, ARG_PTR, 0, 0, 0, 0 } },
#endif
#if DEFSYS(setuid)
	{ SYS(setuid), 1, { ARG_UID, 0, 0, 0, 0, 0 } },
#endif
#if DEFSYS(setuid32)
	{ SYS(setuid32), 1, { ARG_UID, 0, 0, 0, 0, 0 } },
#endif
#if DEFSYS(setxattr)
	{ SYS(setxattr), 5, { ARG_EMPTY_FILENAME, ARG_PTR, ARG_PTR, ARG_LEN, ARG_FLAG, 0 } },
#endif
#if DEFSYS(sgetmask)
	{ SYS(sgetmask), 1, { ARG_UINT, 0, 0, 0, 0, 0 } },
#endif
#if DEFSYS(shmat)
	{ SYS(shmat), 3, { ARG_INT, ARG_PTR, ARG_FLAG, 0, 0, 0 } },
#endif
#if DEFSYS(shmctl)
	{ SYS(shmctl), 3, { ARG_INT, ARG_INT, ARG_PTR, 0, 0, 0 } },
#endif
#if DEFSYS(shmdt)
	{ SYS(shmdt), 3, { ARG_INT, ARG_PTR, ARG_FLAG, 0, 0, 0 } },
#endif
#if DEFSYS(shmget)
	{ SYS(shmget), 3, { ARG_INT, ARG_LEN, ARG_FLAG, 0, 0, 0 } },
#endif
#if DEFSYS(shutdown)
	{ SYS(shutdown), 2, { ARG_SOCKFD, ARG_INT, 0, 0, 0, 0 } },
#endif
#if DEFSYS(sigaction)
	{ SYS(sigaction), 3, { ARG_INT, ARG_PTR, ARG_PTR, 0, 0, 0 } },
#endif
#if DEFSYS(sigaltstack)
	{ SYS(sigaltstack), 3, { ARG_NON_NULL_PTR, ARG_NON_NULL_PTR, 0, 0, 0, 0 } },
#endif
#if DEFSYS(signal)
	{ SYS(signal), 2, { ARG_INT, ARG_NON_NULL_PTR, 0, 0, 0, 0 } },
#endif
#if DEFSYS(signalfd)
	{ SYS(signalfd), 3, { ARG_FD, ARG_PTR, ARG_LEN, 0, 0, 0 } },
#endif
#if DEFSYS(signalfd4)
	{ SYS(signalfd4), 3, { ARG_FD, ARG_PTR, ARG_LEN, ARG_FLAG, 0, 0 } },
#endif
#if DEFSYS(sigpending)
	{ SYS(sigpending), 1, { ARG_PTR, 0, 0, 0, 0, 0 } },
#endif
#if DEFSYS(sigprocmask)
	{ SYS(sigprocmask), 4, { ARG_INT, ARG_PTR, ARG_PTR, ARG_LEN, 0, 0 } },
#endif
#if DEFSYS(sigreturn)
	/* { SYS(sigreturn), 4, { ARG_PTR, ARG_PTR, ARG_PTR, ARG_PTR, 0, 0 } }, */
#endif
#if DEFSYS(sigsuspend)
	{ SYS(sigsuspend), 1, { ARG_PTR, 0, 0, 0, 0, 0 } },
#endif
#if DEFSYS(sigtimedwait)
	{ SYS(sigtimedwait), 3, { ARG_PTR, ARG_PTR, ARG_PTR, 0, 0, 0 } },
#endif
#if DEFSYS(sigwaitinfo)
	{ SYS(sigwaitinfo), 2, { ARG_PTR, ARG_PTR, 0, 0, 0, 0 } },
#endif
#if DEFSYS(socket)
	{ SYS(socket), 3, { ARG_INT, ARG_INT, ARG_INT, 0, 0, 0 } },
#endif
#if DEFSYS(socketcall)
	{ SYS(socketcall), 2, { ARG_INT, ARG_PTR, 0, 0, 0, 0 } },
#endif
#if DEFSYS(socketpair)
	{ SYS(socketpair), 4, { ARG_INT, ARG_INT, ARG_INT, ARG_PTR, 0, 0 } },
#endif
#if DEFSYS(spill)
	/* Xtensa only */
#endif
#if DEFSYS(splice)
	{ SYS(splice), 6, { ARG_FD, ARG_PTR, ARG_FD, ARG_PTR, ARG_LEN, ARG_FLAG } },
#endif
#if DEFSYS(spu_create)
	/* PowerPC/PowerPC64 only */
	{ SYS(spu_create), 3, { ARG_EMPTY_FILENAME, ARG_FLAG, ARG_MODE, 0, 0, 0 } },
	{ SYS(spu_create), 4, { ARG_EMPTY_FILENAME, ARG_FLAG, ARG_MODE, ARG_INT, 0, 0 } },
#endif
#if DEFSYS(spu_run)
	/* PowerPC/PowerPC64 only */
	{ SYS(spu_run), 3, { ARG_FD, ARG_PTR, ARG_PTR, 0, 0, 0 } },
#endif
#if DEFSYS(sram_alloc)
	/* Blackfin, remove 4.17 */
#endif
#if DEFSYS(sram_free)
	/* Blackfin, remove 4.17 */
#endif
#if DEFSYS(ssetmask)
	{ SYS(ssetmask), 1, { ARG_UINT, 0, 0, 0, 0, 0 } },
#endif
#if DEFSYS(stat)
	{ SYS(stat), 2, { ARG_EMPTY_FILENAME, ARG_PTR, 0, 0, 0, 0 } },
	{ SYS(stat), 2, { ARG_DEVNULL_FILENAME, ARG_PTR, 0, 0, 0, 0 } },
#endif
#if DEFSYS(stat64)
	{ SYS(stat64), 2, { ARG_EMPTY_FILENAME, ARG_PTR, 0, 0, 0, 0 } },
	{ SYS(stat64), 2, { ARG_DEVNULL_FILENAME, ARG_PTR, 0, 0, 0, 0 } },
#endif
#if DEFSYS(statfs)
	{ SYS(statfs), 2, { ARG_EMPTY_FILENAME, ARG_PTR, 0, 0, 0, 0 } },
	{ SYS(statfs), 2, { ARG_DEVNULL_FILENAME, ARG_PTR, 0, 0, 0, 0 } },
#endif
#if DEFSYS(statfs64)
	{ SYS(statfs64), 2, { ARG_EMPTY_FILENAME, ARG_PTR, 0, 0, 0, 0 } },
	{ SYS(statfs64), 2, { ARG_DEVNULL_FILENAME, ARG_PTR, 0, 0, 0, 0 } },
#endif
#if DEFSYS(statx)
	{ SYS(statx), 5, { ARG_DIRFD, ARG_EMPTY_FILENAME, ARG_FLAG, ARG_UINT, ARG_PTR, 0 } },
	{ SYS(statx), 5, { ARG_DIRFD, ARG_DEVNULL_FILENAME, ARG_FLAG, ARG_UINT, ARG_PTR, 0 } },
#endif
#if DEFSYS(stime)
	{ SYS(stime), 1, { ARG_PTR, 0, 0, 0, 0, 0 } },
#endif
#if DEFSYS(subpage_prot)
	/* PowerPC/PowerPC64 only */
	{ SYS(subpage_prot), 3, { ARG_UINT, ARG_LEN, ARG_PTR, 0, 0, 0 } },
#endif
#if DEFSYS(swapcontext)
	/* PowerPC/PowerPC64 only */
	{ SYS(swapcontext), 3, { ARG_PTR, ARG_PTR, ARG_LEN, 0, 0, 0 } },
#endif
#if DEFSYS(swapon)
	{ SYS(swapon), 2, { ARG_EMPTY_FILENAME, ARG_INT, 0, 0, 0, 0 } },
#endif
#if DEFSYS(swapoff)
	{ SYS(swapoff), 1, { ARG_EMPTY_FILENAME, 0 , 0, 0, 0, 0 } },
#endif
#if DEFSYS(switch_endian)
	/* PowerPC/PowerPC64 only */
	{ SYS(switch_endian), 0, { 0, 0, 0, 0, 0, 0 } },
#endif
#if DEFSYS(symlink)
	{ SYS(symlink), 2, { ARG_EMPTY_FILENAME, ARG_EMPTY_FILENAME, 0, 0, 0, 0 } },
#endif
#if DEFSYS(symlinkat)
	{ SYS(symlinkat), 3, { ARG_EMPTY_FILENAME, ARG_FD, ARG_EMPTY_FILENAME, 0, 0, 0 } },
#endif
#if DEFSYS(sync)
	{ SYS(sync), 0, { 0, 0, 0, 0, 0, 0 } },
#endif
#if DEFSYS(sync_file_range)
	{ SYS(sync_file_range), 4, { ARG_FD, ARG_UINT, ARG_UINT, ARG_FLAG, 0, 0 } },
#endif
#if DEFSYS(sync_file_range2)
	{ SYS(sync_file_range2), 4, { ARG_FD, ARG_FLAG, ARG_UINT, ARG_UINT, 0, 0 } },
#endif
#if DEFSYS(syncfs)
	{ SYS(syncfs), 1, { ARG_FD, 0, 0, 0, 0, 0 } },
#endif
#if DEFSYS(sys_debug_setcontext)
	/* PowerPC/PowerPC64 only */
	{ SYS(sys_debug_setcontext), 3, { ARG_PTR, ARG_INT, ARG_PTR, 0, 0, 0 } },
#endif
#if DEFSYS(sysctl)
	{ SYS(sysctl), 1, { ARG_PTR, 0, 0, 0, 0, 0 } },
#endif
#if DEFSYS(sysfs)
	{ SYS(sysfs), 2, { ARG_INT, ARG_PTR, 0, 0, 0, 0 } },
	{ SYS(sysfs), 3, { ARG_INT, ARG_UINT, ARG_PTR, 0, 0, 0 } },
	{ SYS(sysfs), 1, { ARG_INT, 0, 0, 0, 0, 0 } },
#endif
#if DEFSYS(sysinfo)
	{ SYS(sysinfo), 1, { ARG_PTR, 0, 0, 0, 0, 0 } },
#endif
#if DEFSYS(syslog)
	{ SYS(syslog), 3, { ARG_INT, ARG_PTR, ARG_PTR, 0, 0, 0 } },
#endif
#if DEFSYS(sysmips)
	/* MIPS ABI */
	{ SYS(sysmips), 3, { ARG_INT, ARG_INT, ARG_INT, 0, 0, 0 } },
#endif
#if DEFSYS(tee)
	{ SYS(tee), 4, { ARG_FD, ARG_FD, ARG_LEN, ARG_FLAG, 0, 0 } },
#endif
#if DEFSYS(tgkill)
	/* { SYS(tgkill), 3, { ARG_PID, ARG_PID, ARG_INT, 0, 0, 0 } }, */
#endif
#if DEFSYS(time)
	{ SYS(time), 1, { ARG_PTR, 0, 0, 0, 0, 0 } },
#endif
#if DEFSYS(timer_create)
	{ SYS(timer_create), 3, { ARG_CLOCKID_T, ARG_PTR, ARG_PTR, 0, 0, 0 } },
#endif
#if DEFSYS(timer_delete)
	{ SYS(timer_delete), 1, { ARG_UINT, 0, 0, 0, 0, 0 } },
#endif
#if DEFSYS(timer_getoverrun)
	{ SYS(timer_getoverrun), 1, { ARG_UINT, 0, 0, 0, 0, 0 } },
#endif
#if DEFSYS(timer_gettime)
	{ SYS(timer_gettime), 2, { ARG_UINT, ARG_PTR, 0, 0, 0, 0 } },
#endif
#if DEFSYS(timer_gettime64)
	{ SYS(timer_gettime64), 2, { ARG_UINT, ARG_PTR, 0, 0, 0, 0 } },
#endif
#if DEFSYS(timer_settime)
	{ SYS(timer_settime), 4, { ARG_UINT, ARG_FLAG, ARG_PTR, ARG_PTR, 0, 0 } },
#endif
#if DEFSYS(timer_settime64)
	{ SYS(timer_settime64), 4, { ARG_UINT, ARG_FLAG, ARG_PTR, ARG_PTR, 0, 0 } },
#endif
#if DEFSYS(timerfd_create)
	{ SYS(timerfd_create), 2, { ARG_CLOCKID_T, ARG_FLAG, 0, 0, 0, 0 } },
#endif
#if DEFSYS(timerfd_gettime)
	{ SYS(timerfd_gettime), 2, { ARG_CLOCKID_T, ARG_PTR, 0, 0, 0, 0 } },
#endif
#if DEFSYS(timerfd_gettime64)
	{ SYS(timerfd_gettime64), 2, { ARG_CLOCKID_T, ARG_PTR, 0, 0, 0, 0 } },
#endif
#if DEFSYS(timerfd_settime)
	{ SYS(timerfd_settime), 4, { ARG_FD, ARG_FLAG, ARG_PTR, ARG_PTR, 0, 0 } },
#endif
#if DEFSYS(timerfd_settime64)
	{ SYS(timerfd_settime64), 4, { ARG_FD, ARG_FLAG, ARG_PTR, ARG_PTR, 0, 0 } },
#endif
#if DEFSYS(times)
	{ SYS(times), 1, { ARG_PTR, 0, 0, 0, 0, 0 } },
#endif
#if DEFSYS(tkill)
	/* { SYS(tkill), 2, { ARG_PID, ARG_INT, 0, 0, 0, 0 }, */
#endif
#if DEFSYS(truncate)
	{ SYS(truncate), 2, { ARG_EMPTY_FILENAME, ARG_LEN, 0, 0, 0, 0 } },
#endif
#if DEFSYS(truncate64)
	{ SYS(truncate64), 2, { ARG_EMPTY_FILENAME, ARG_LEN, 0, 0, 0, 0 } },
#endif
#if DEFSYS(tuxcall)
	{ SYS(tuxcall), 2, { ARG_INT, ARG_PTR, 0, 0, 0, 0 } },
#endif
#if DEFSYS(ugetrlimit)
	{ SYS(ugetrlimit), 2, { ARG_RND, ARG_PTR, 0, 0, 0, 0 } },
	{ SYS(ugetrlimit), 2, { ARG_INT, ARG_PTR, 0, 0, 0, 0 } },
#endif
#if DEFSYS(ulimit)
	{ SYS(ulimit), 2, { ARG_UINT, ARG_PTR, 0, 0, 0, 0 } },
#endif
#if DEFSYS(umask)
	{ SYS(umask), 1, { ARG_UINT, 0, 0, 0, 0, 0 } },
#endif
#if DEFSYS(umount)
	{ SYS(umount), 1, { ARG_EMPTY_FILENAME, 0, 0, 0, 0, 0 } },
	{ SYS(umount), 1, { ARG_DEVNULL_FILENAME, 0, 0, 0, 0, 0 } },
#endif
#if DEFSYS(umount2)
	{ SYS(umount2), 1, { ARG_EMPTY_FILENAME, ARG_INT, 0, 0, 0, 0 } },
	{ SYS(umount2), 1, { ARG_DEVNULL_FILENAME, ARG_INT, 0, 0, 0, 0 } },
#endif
#if DEFSYS(uname)
	{ SYS(uname), 1, { ARG_PTR, 0, 0, 0, 0, 0 } },
#endif
#if DEFSYS(unlink)
	{ SYS(unlink), 1, { ARG_EMPTY_FILENAME, 0, 0, 0, 0, 0 } },
#endif
#if DEFSYS(unlinkat)
	{ SYS(unlinkat), 3, { ARG_DIRFD, ARG_EMPTY_FILENAME, ARG_FLAG, 0, 0, 0 } },
#endif
#if DEFSYS(unshare)
	{ SYS(unshare), 1, { ARG_INT, 0, 0, 0, 0, 0 } },
#endif
#if DEFSYS(uselib)
	{ SYS(uselib), 1, { ARG_EMPTY_FILENAME, 0, 0, 0, 0, 0 } },
	{ SYS(uselib), 1, { ARG_DEVNULL_FILENAME, 0, 0, 0, 0, 0 } },
#endif
#if DEFSYS(userfaultfd)
	{ SYS(userfaultfd), 1, { ARG_FLAG, 0, 0, 0, 0, 0 } },
#endif
#if DEFSYS(usr26)
	{ SYS(usr26), 1, { ARG_INT, 0, 0, 0, 0, 0 } },
#endif
#if DEFSYS(usr32)
	{ SYS(usr32), 1, { ARG_INT, 0, 0, 0, 0, 0 } },
#endif
#if DEFSYS(ustat)
	{ SYS(ustat), 2, { ARG_UINT, ARG_PTR, 0, 0, 0, 0 } },
#endif
#if DEFSYS(utime)
	{ SYS(utime), 2, { ARG_EMPTY_FILENAME, ARG_PTR, 0, 0, 0, 0 } },
#endif
#if DEFSYS(utimensat)
	{ SYS(utimensat), 4, { ARG_DIRFD, ARG_EMPTY_FILENAME, ARG_PTR, ARG_FLAG, 0, 0 } },
#endif
#if DEFSYS(utimensat_time64)
	{ SYS(utimensat_time64), 4, { ARG_DIRFD, ARG_EMPTY_FILENAME, ARG_PTR, ARG_FLAG, 0, 0 } },
#endif
#if DEFSYS(utimes)
	{ SYS(utimes), 2, { ARG_EMPTY_FILENAME, ARG_PTR, 0, 0, 0, 0 } },
#endif
#if DEFSYS(utrap_install)
	/* SPARC64 */
	{ SYS(utrap_install), 5, { ARG_INT, ARG_PTR, ARG_PTR, ARG_PTR, ARG_PTR, 0 } },
#endif
#if DEFSYS(vm86old)
	/* x86 */
	{ SYS(vm86old), 1, { ARG_PTR, 0, 0, 0, 0, 0 } },
#endif
#if DEFSYS(vm86)
	/* x86 */
	{ SYS(vm86), 1, { ARG_UINT, ARG_PTR, 0, 0, 0, 0 } },
#endif
#if DEFSYS(vmsplice)
	{ SYS(vmsplice), 4, { ARG_FD, ARG_PTR, ARG_UINT, ARG_FLAG, 0, 0 } },
#endif
#if DEFSYS(vserver)
	/* { SYS(verver), 0, { 0, 0, 0, 0, 0, 0, 0 } }, */
#endif
#if DEFSYS(wait)
	{ SYS(wait), 1, { ARG_PTR, 0, 0, 0, 0, 0 } },
#endif
#if DEFSYS(wait3)
	{ SYS(wait3), 3, { ARG_PTR, ARG_INT, ARG_PTR, 0, 0, 0 } },
#endif
#if DEFSYS(wait4)
	{ SYS(wait4), 4, { ARG_PID, ARG_PTR, ARG_INT, ARG_PTR, 0, 0 } },
#endif
#if DEFSYS(waitid)
	{ SYS(waitid), 4, { ARG_INT, ARG_INT, ARG_PTR, ARG_INT, 0, 0 } },
#endif
#if DEFSYS(waitpid)
	{ SYS(waitpid), 3, { ARG_PID, ARG_PTR, ARG_INT, 0, 0, 0 } },
#endif
#if DEFSYS(write)
	{ SYS(write), 3, { ARG_FD, ARG_PTR, ARG_LEN, 0, 0, 0 } },
#endif
#if DEFSYS(writev)
	{ SYS(writev), 3, { ARG_FD, ARG_PTR, ARG_LEN, 0, 0, 0 } },
#endif
#if DEFSYS(xtensa)
	/* xtensa only */
	/* { SYS(xtensa), 6, { UINT, 0, 0, 0, 0, 0, 0 } }, */
#endif
};

static bool *stress_syscall_exercised;

/*
 *  running context shared between parent and child
 *  this allows us to have enough data about a system call that
 *  caused the child to crash. Also contains running stats
 *  of the number of system calls made.
 */
typedef struct {
	uint64_t hash;
	uint64_t syscall;
	uint64_t type;
	const char *name;
	size_t idx;
	uint64_t counter;
	volatile uint64_t skip_crashed;
	volatile uint64_t skip_errno_zero;
	volatile uint64_t skip_timed_out;
	uint64_t crash_count[SYSCALL_ARGS_SIZE];
	unsigned long int args[MAX_SYSCALL_ARGS];
	unsigned char filler[4096];
	struct {
		mode_t mode;
		uid_t uid;
		gid_t gid;
		char cwd[PATH_MAX];
	} dirfd;
} syscall_current_context_t;

static syscall_current_context_t *current_context;

static void NORETURN func_exit(void)
{
	_exit(EXIT_SUCCESS);
}

/*
 *  Various invalid argument values
 */
static unsigned long int none_values[] = { 0 };

static unsigned long int mode_values[] = {
	(unsigned long int)-1, INT_MAX, (unsigned long int)INT_MIN,
	~(unsigned long int)0, 1ULL << 20,
};

static unsigned long int access_mode_values[] = {
	(unsigned long int)~(F_OK | R_OK | W_OK | X_OK),
};

static long int sockfds[] = {
	0, /* sock_fd */
	0, /* bad_fd */
	0,
	-1,
	INT_MAX,
	INT_MIN,
	~(long int)0,
};

static long int fds[] = {
	0, /* fd */
	0, /* bad_fd */
	-1,
	INT_MAX,
	INT_MIN,
	~(long int)0,
};

static long int dirfds[] = {
	0, /* bad_fd */
	-1,
	AT_FDCWD,
	INT_MIN,
	INT_MAX,
	~(long int)0,
};

static long int clockids[] = {
	-1,
	INT_MAX,
	INT_MIN,
	~(long int)0,
	SHR_UL(0xfe23ULL, 18),
};

static unsigned long int sockaddrs[] = {
	0, /* small_ptr */
	0, /* page_ptr_no */
	0, /* small_ptr_wo */
	0, /* page_ptr_wo */
	0, /* small_ptr_ro */
	0, /* page_ptr_ro */
	0, /* small_ptr_rw */
	0, /* page_ptr_rw */
	0,
	-1, /* invalid */
	INT_MAX,
	INT_MIN,
};

static unsigned long int brk_addrs[] = {
	0,
	(unsigned long int)-1,
	INT_MAX,
	(unsigned long int)INT_MIN,
	~(unsigned long int)0,
	4096,
};

static unsigned long int empty_filenames[] = {
	(unsigned long int)"",
	(unsigned long int)NULL,
};

static unsigned long int zero_filenames[] = {
	(unsigned long int)"/dev/zero",
};

static unsigned long int null_filenames[] = {
	(unsigned long int)"/dev/null",
};

static long int flags[] = {
	-1, -2, INT_MIN, SHR_UL(0xffffULL, 20),
	0x00000001, 0x00000002, 0x00000004, 0x00000008,
	0x00000010, 0x00000020, 0x00000040, 0x00000080,
	0x00000100, 0x00000200, 0x00000400, 0x00000800,
	0x00001000, 0x00002000, 0x00004000, 0x00008000,
	0x00010000, 0x00020000, 0x00040000, 0x00080000,
	0x00100000, 0x00200000, 0x00400000, 0x00800000,
	0x01000000, 0x02000000, 0x04000000, 0x08000000,
	0x10000000, 0x20000000, 0x40000000, 0x80000000,
};

static unsigned long int lengths[] = {
	(unsigned long int)-1,
	(unsigned long int)-2,
	(unsigned long int)INT_MIN,
	(unsigned long int)INT_MAX,
	~(unsigned long int)0,
	(unsigned long int)-SHR_UL(1, 31),
};

static long int ints[] = {
	(long int)0,
	(long int)-1,
	(long int)-2,
	(long int)INT_MIN,
	(long int)INT_MAX,
	(long int)SHR_UL(0xff, 30),
	(long int)SHR_UL(1, 30),
	(long int)-SHR_UL(0xff, 30),
	(long int)-SHR_UL(1, 30),
};

static unsigned long int uints[] = {
	(unsigned long int)INT_MAX,
	(unsigned long int)SHR_UL(0xff, 30),
	(unsigned long int)-SHR_UL(0xff, 30),
	(unsigned long int)~0,
};

static unsigned long int func_ptrs[] = {
	(unsigned long int)func_exit,
};

static unsigned long int ptrs[] = {
	0, /* small_ptr */
	0, /* page_ptr_no */
	0, /* small_ptr_wo */
	0, /* page_ptr_wo */
	0, /* small_ptr_ro */
	0, /* page_ptr_ro */
	0, /* small_ptr_rw */
	0, /* page_ptr_rw */
	0,
	(unsigned long int)-1,
	(unsigned long int)INT_MAX,
	(unsigned long int)INT_MIN,
	(unsigned long int)~4096L,
};


static unsigned long int futex_ptrs[] = {
	0, /* small_ptr */
	0, /* page_ptr_no */
	0, /* small_ptr_wo */
	0, /* page_ptr_wo */
	0, /* small_ptr_ro */
	0, /* page_ptr_ro */
	0, /* small_ptr_rw */
	0, /* page_ptr_rw */
};

static unsigned long int non_null_ptrs[] = {
	0, /* small_ptr */
	0, /* page_ptr_no */
	0, /* small_ptr_wo */
	0, /* page_ptr_wo */
	0, /* small_ptr_ro */
	0, /* page_ptr_ro */
	0, /* small_ptr_rw */
	0, /* page_ptr_rw */
	(unsigned long int)-1,
	(unsigned long int)INT_MAX,
	(unsigned long int)INT_MIN,
	(unsigned long int)~4096L,
};

static long int socklens[] = {
	0,
	-1,
	INT_MAX,
	INT_MIN,
	8192,
};

static unsigned long int timeouts[] = {
	0
};

static pid_t pids[] = {
	(pid_t)INT_MIN,
	(pid_t)-1,
	(pid_t)INT_MAX,
	(pid_t)~0,
};

static gid_t gids[] = {
	(gid_t)~0L, INT_MAX,
};

static uid_t uids[] = {
	(uid_t)~0L, INT_MAX,
};

/*
 *  Misc per system-call args
 */
static const char *add_key_types[] = { "key_ring" };

static const char *add_key_descrs[] = { "." };

static unsigned long int bpf_cmds[] = {
	0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
	0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f,
	0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17,
	0x18, 0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f,
	0x20, 0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27,
	0x28, 0x29, 0x2a, 0x2b, 0x2c, 0x2d, 0x2e, 0x2f,
};

static int bpf_lengths[] = {
	0,
	16,
	256,
	1024,
	4096,
	65536,
	1024 * 1024,
};

/*
 *  mapping of invalid arg types to invalid arg values
 */
static const stress_syscall_arg_values_t arg_values[] = {
	ARG_VALUE(ARG_MODE, mode_values),
	ARG_VALUE(ARG_SOCKFD, sockfds),
	ARG_VALUE(ARG_FD, fds),
	ARG_VALUE(ARG_DIRFD, dirfds),
	ARG_VALUE(ARG_CLOCKID_T, clockids),
	ARG_VALUE(ARG_PID, pids),
	ARG_VALUE(ARG_PTR | ARG_STRUCT_SOCKADDR, sockaddrs),
	ARG_VALUE(ARG_BRK_ADDR, brk_addrs),
	ARG_VALUE(ARG_EMPTY_FILENAME, empty_filenames),
	ARG_VALUE(ARG_DEVZERO_FILENAME, zero_filenames),
	ARG_VALUE(ARG_DEVNULL_FILENAME, null_filenames),
	ARG_VALUE(ARG_FLAG, flags),
	ARG_VALUE(ARG_SOCKLEN_T, socklens),
	ARG_VALUE(ARG_TIMEOUT, timeouts),
	ARG_VALUE(ARG_LEN, lengths),
	ARG_VALUE(ARG_GID, gids),
	ARG_VALUE(ARG_UID, uids),
	ARG_VALUE(ARG_INT, ints),
	ARG_VALUE(ARG_UINT, uints),
	ARG_VALUE(ARG_FUNC_PTR, func_ptrs),
	ARG_VALUE(ARG_NON_NULL_PTR, non_null_ptrs),
	ARG_VALUE(ARG_FUTEX_PTR, futex_ptrs),
	ARG_VALUE(ARG_PTR, ptrs),
	ARG_VALUE(ARG_ACCESS_MODE, access_mode_values),

	/* Misc per-system call values */
	ARG_VALUE(ARG_ADD_KEY_TYPES, add_key_types),
	ARG_VALUE(ARG_ADD_KEY_DESCRS, add_key_descrs),
	ARG_VALUE(ARG_BPF_CMDS, bpf_cmds),
	ARG_VALUE(ARG_BPF_LEN, bpf_lengths),

};

/*
 *   stress_syscall_hash()
 *	generate a simple hash on system call and call arguments
 */
static unsigned long int stress_syscall_hash(
	const unsigned long int syscall_num,
	const unsigned long int args[MAX_SYSCALL_ARGS])
{
	unsigned long int hash = syscall_num;

	RORn(hash, 2);
	hash ^= (args[0]);
	RORn(hash, 2);
	hash ^= (args[1]);
	RORn(hash, 2);
	hash ^= (args[2]);
	RORn(hash, 2);
	hash ^= (args[3]);
	RORn(hash, 2);
	hash ^= (args[4]);
	RORn(hash, 2);
	hash ^= (args[5]);

	return hash % SYSCALL_HASH_TABLE_SIZE;
}

/*
 *  hash_table_add()
 *	add system call info to the hash table
 * 	- will silently fail if out of memory
 */
static void hash_table_add(
	const unsigned long int hash,
	const unsigned long int syscall_num,
	const unsigned long int *args,
	const uint8_t type)
{
	stress_syscall_arg_hash_t *h;

	if (UNLIKELY(hash_table->index >= HASH_TABLE_POOL_SIZE))
		return;
	h = &hash_table->pool[hash_table->index];
	h->hash = hash;
	h->syscall = syscall_num;
	h->type = type;
	(void)shim_memcpy(h->args, args, sizeof(h->args));
	h->next = hash_table->table[hash];
	hash_table->table[hash] = h;
	hash_table->index++;
}

static void MLOCKED_TEXT stress_syscall_itimer_handler(int signum)
{
	if (do_jmp && current_context) {
		current_context->type = SYSCALL_TIMED_OUT;
		stress_signal_siglongjmp_flag(signum, jmp_env, 1, &do_jmp);
	}
}

/*
 *  syscall_set_cwd_perms()
 *	set mode and uid/gid back to original if specific system
 *	calls were called and may have modified them
 */
static void syscall_set_cwd_perms(const unsigned long int syscall_num)
{
	switch (syscall_num) {
#if DEFSYS(chmod)
	case DEFSYS(chmod):
		VOID_RET(int, chmod(current_context->dirfd.cwd, current_context->dirfd.mode));
		break;
#endif
#if DEFSYS(fchmod)
	case DEFSYS(fchmod):
		VOID_RET(int, chmod(current_context->dirfd.cwd, current_context->dirfd.mode));
		break;
#endif
#if DEFSYS(fchmodat)
	case DEFSYS(fchmodat):
		VOID_RET(int, chmod(current_context->dirfd.cwd, current_context->dirfd.mode));
		break;
#endif
#if DEFSYS(chown)
	case DEFSYS(chown):
		VOID_RET(int, chown(current_context->dirfd.cwd, current_context->dirfd.uid, current_context->dirfd.gid));
		break;
#endif
#if DEFSYS(fchown)
	case DEFSYS(fchown):
		VOID_RET(int, chown(current_context->dirfd.cwd, current_context->dirfd.uid, current_context->dirfd.gid));
		break;
#endif
#if DEFSYS(fchownat)
	case DEFSYS(fchownat):
		VOID_RET(int, chown(current_context->dirfd.cwd, current_context->dirfd.uid, current_context->dirfd.gid));
		break;
#endif
#if DEFSYS(lchown)
	case DEFSYS(lchown):
		VOID_RET(int, chown(current_context->dirfd.cwd, current_context->dirfd.uid, current_context->dirfd.gid));
		break;
#endif
	default:
		break;
	}
}

/*
 *  syscall_do_call()
 *	perform the system call
 */
static void syscall_do_call(
	const stress_syscall_arg_t *stress_syscall_arg,
	volatile bool *syscall_exercised)
{
	int ret;
	const unsigned long int syscall_num = stress_syscall_arg->syscall;
	NOCLOBBER unsigned long int hash;
	stress_syscall_arg_hash_t *h = hash_table->table[hash];
	struct itimerval it;

	hash = stress_syscall_hash(syscall_num, current_context->args);

	while (h) {
		if (!shim_memcmp(h->args, current_context->args, sizeof(h->args))) {
			switch (h->type) {
			case SYSCALL_CRASH:
				current_context->skip_crashed++;
				break;
			default:
				break;
			}
			return;
		}
		h = h->next;
	}

	errno = 0;
	current_context->counter++;
	current_context->hash = hash;
	current_context->type = SYSCALL_CRASH;	/* Assume it will crash */

	/*
	 * Force abort if we take too long
	 */
	it.it_interval.tv_sec = 0;
	it.it_interval.tv_usec = SYSCALL_TIMEOUT_USEC;
	it.it_value.tv_sec = 0;
	it.it_value.tv_usec = SYSCALL_TIMEOUT_USEC;
	VOID_RET(int, setitimer(ITIMER_REAL, &it, NULL));

	ret = sigsetjmp(jmp_env, 1);
	if (ret == 1) {
		/* timed out! */
		current_context->type = SYSCALL_TIMED_OUT;
		goto timed_out;
	}
	do_jmp = true;

	*syscall_exercised = true;

	ret = (int)syscall((long int)syscall_num,
		current_context->args[0],
		current_context->args[1],
		current_context->args[2],
		current_context->args[3],
		current_context->args[4],
		current_context->args[5]);

	syscall_set_cwd_perms(syscall_num);

timed_out:
	do_jmp = false;
	if (current_context->type == SYSCALL_TIMED_OUT) {
		/*
		 *  Remember syscalls that block for too long so we don't retry them
		 */
		hash_table_add(hash, syscall_num, current_context->args, SYSCALL_TIMED_OUT);
		current_context->skip_timed_out++;
	} else if (ret == 0) {
		/*
		 *  For this child we remember syscalls that don't fail
		 *  so we don't retry them
		 */
		hash_table_add(hash, syscall_num, current_context->args, SYSCALL_ERRNO_ZERO);
		current_context->skip_errno_zero++;
	}
	current_context->type = SYSCALL_FAIL;	/* it just failed */
}

/*
 *  syscall_permute()
 *	recursively permute all possible system call invalid arguments
 *	- if the system call crashes, the call info is cached in
 *	  the current_context for the parent to record the failure
 *	  so it's not called again.
 *	- if the system call returns 0, the call info is saved
 *	  in the hash table so it won't get called again. This is
 * 	  just in the child context and is lost when the child
 *	  crashes
 */
static void syscall_permute(
	stress_args_t *args,
	const int arg_num,
	const uint32_t arg_bitmask,
	const stress_syscall_arg_t *stress_syscall_arg,
	volatile bool *syscall_exercised)
{
	size_t i;
	unsigned long int *values, rnd_values[4];
	size_t num_values;

	if (UNLIKELY(stress_time_now() > args->time_end))
		return;

	/* all args now permuted, do the call */
	if (arg_num >= stress_syscall_arg->num_args) {
		syscall_do_call(stress_syscall_arg, syscall_exercised);
		return;
	}

	values = NULL;
	num_values = 0;

	switch (arg_bitmask) {
	case ARG_NONE:
		values = none_values;
		num_values = 1;
		break;
	case ARG_RND:
		/*
		 *  Provide some 'random' values
		 */
		rnd_values[0] = stress_mwc64();
		rnd_values[1] = SHR_UL(stress_mwc32(), 20);
		rnd_values[2] = (unsigned long int)mappings[0];
		rnd_values[3] = (unsigned long int)mappings_small[0];
		values = rnd_values;
		num_values = 4;
		break;
	default:
		/*
		 *  Find the arg type to determine the arguments to use
		 */
		if (ARG_BITMASK(arg_bitmask, ARG_MISC)) {
			/*
			 *  Misc enumerated values
			 */
			for (i = 0; i < SIZEOF_ARRAY(arg_values); i++) {
				if (ARG_MISC_ID(arg_bitmask) == ARG_MISC_ID(arg_values[i].bitmask)) {
					values = arg_values[i].values;
					num_values = arg_values[i].num_values;
					break;
				}
			}
		} else {
			/*
			 *  Mixed bitmask values
			 */
			for (i = 0; i < SIZEOF_ARRAY(arg_values); i++) {
				if (ARG_BITMASK(arg_bitmask, arg_values[i].bitmask)) {
					values = arg_values[i].values;
					num_values = arg_values[i].num_values;
					break;
				}
			}
		}
		break;
	}

	/* Zero writable mapping */
	(void)shim_memset(mappings[1], 0, args->page_size);
	/*
	 *  This should not fail!
	 */
	if (UNLIKELY(!num_values)) {
		pr_dbg("%s: argument %d has bad bitmask %" PRIx32 "\n", args->name, arg_num, arg_bitmask);
		current_context->args[arg_num] = 0;
		return;
	}

	/*
	 *  And permute and call all the argument values for this
	 *  specific argument
	 */
	for (i = 0; i < num_values; i++) {
		if (UNLIKELY(stress_time_now() > args->time_end))
			return;
		current_context->args[arg_num] = values[i];
		syscall_permute(args, arg_num + 1, stress_syscall_arg->arg_bitmasks[arg_num], stress_syscall_arg, syscall_exercised);
		current_context->args[arg_num] = 0;
	}
}

/*
 *  Call a system call in a child context so we don't clobber
 *  the parent
 */
static inline int stress_do_syscall(stress_args_t *args)
{
	pid_t pid;
	int rc = 0;

	(void)stress_mwc32();

	if (UNLIKELY(!stress_continue_flag()))
		return 0;

	if (stress_capabilities_drop(args->name) < 0)
		return EXIT_NO_RESOURCE;

	if (UNLIKELY(stress_time_now() > args->time_end))
		return 0;

	pid = fork();
	if (pid < 0) {
		_exit(EXIT_NO_RESOURCE);
	} else if (pid == 0) {
		size_t i, n;
		size_t reorder[SYSCALL_ARGS_SIZE];

		/* We don't want bad ops clobbering this region */
		stress_stack_smash_check_flag_set(false);
		stress_shared_readonly();
		stress_process_dumpable(false);

		/* Drop all capabilities */
		if (stress_capabilities_drop(args->name) < 0) {
			_exit(EXIT_NO_RESOURCE);
		}
		for (i = 0; i < SIZEOF_ARRAY(sigs); i++) {
			if (UNLIKELY(stress_signal_handler(args->name, sigs[i], stress_signal_exit_handler, NULL) < 0))
				_exit(EXIT_FAILURE);
		}

		if (UNLIKELY(stress_signal_handler(args->name, SIGALRM, stress_syscall_itimer_handler, NULL) < 0))
			_exit(EXIT_FAILURE);

		stress_make_it_fail_set();
		stress_parent_died_alarm();
		(void)stress_sched_settings_apply(true);
		stress_mwc_reseed();

		while (stress_continue(args)) {
			const size_t sz = SIZEOF_ARRAY(reorder);

			for (i = 0; i < SIZEOF_ARRAY(reorder); i++)
				reorder[i] = i;

			/*
			 * 50% of the time we do syscalls in shuffled order
			 */
			if (stress_mwc1()) {
				/*
				 *  Shuffle syscall order
				 */
				for (n = 0; n < 5; n++) {
					for (i = 0; i < SIZEOF_ARRAY(reorder); i++) {
						register size_t tmp;
						register const size_t j = (sz == 0) ? 0 : stress_mwc32modn(sz);

						tmp = reorder[i];
						reorder[i] = reorder[j];
						reorder[j] = tmp;
					}
				}
			}

			for (i = 0; LIKELY(stress_continue(args) && (i < SYSCALL_ARGS_SIZE)); i++) {
				register const size_t j = reorder[i];

				if (UNLIKELY(stress_time_now() > args->time_end))
					_exit(EXIT_SUCCESS);

				(void)shim_memset(current_context->args, 0, sizeof(current_context->args));
				current_context->syscall = stress_syscall_args[j].syscall;
				current_context->idx = j;
				current_context->name = stress_syscall_args[j].name;

				/* Ignore too many crashes from this system call */
				if (current_context->crash_count[j] >= MAX_CRASHES)
					continue;
				syscall_permute(args, 0, stress_syscall_args[j].arg_bitmasks[0], &stress_syscall_args[j], &stress_syscall_exercised[j]);
				syscall_set_cwd_perms(current_context->syscall);
			}
		}
		_exit(EXIT_SUCCESS);
	} else {
		int status;

		/*
		 *  Don't use retry shim_waitpid here, we want to force
		 *  kill the child no matter what happens at this point
		 */
		if (waitpid(pid, &status, 0) < 0) {
			/*
			 *  SIGALRM or a waitpid failure, so force
			 *  kill and reap of child to make sure
			 *  it is really dead and buried
			 */
			(void)stress_kill_pid(pid);
			VOID_RET(pid_t, waitpid(pid, &status, 0));
		}
		if (current_context->type == SYSCALL_CRASH) {
			const size_t idx = current_context->idx;

			hash_table_add(current_context->hash,
				current_context->syscall,
				current_context->args,
				SYSCALL_CRASH);

			if (LIKELY(idx < SYSCALL_ARGS_SIZE))
				current_context->crash_count[idx]++;
		}
		rc = WEXITSTATUS(status);
	}
	return rc;
}

static int stress_sysinval_child(stress_args_t *args, void *context)
{
	(void)context;

	do {
		(void)stress_mwc32();
		stress_do_syscall(args);
	} while (stress_continue(args) &&
		 LIKELY(stress_time_now() <= args->time_end));

	return EXIT_SUCCESS;
}

/*
 *  stress_sysinval
 *	stress system calls with bad addresses
 */
static int stress_sysinval(stress_args_t *args)
{
	struct stat statbuf;
	int ret, rc = EXIT_NO_RESOURCE;
	const int bad_fd = stress_fs_bad_fd_get();
	size_t i, j;
	uint64_t syscalls_exercised, syscalls_unique, syscalls_crashed;
	const size_t page_size = args->page_size;
	const size_t current_context_size =
		(sizeof(*current_context) + page_size) & ~(page_size - 1);
	const size_t mmap_size = page_size << 1;		/* cppcheck-suppress duplicateAssignExpression */
	char filename[PATH_MAX];
	const size_t stress_syscall_exercised_sz = SYSCALL_ARGS_SIZE * sizeof(*stress_syscall_exercised);
	bool syscall_exercised[SYSCALL_ARGS_SIZE];
	bool syscall_unique[SYSCALL_ARGS_SIZE];
	size_t mappings_size[SIZEOF_ARRAY(prot_flags)];

	(void)shim_memset(mappings, 0, sizeof(mappings));
	(void)shim_memset(mappings_small, 0, sizeof(mappings_small));
	(void)shim_memset(mappings_size, 0, sizeof(mappings_size));

	/*
	 *  Run-time sanity check of zero syscalls, maybe __NR or SYS_ is not
	 *  defined.
	 */
	if (UNLIKELY(SYSCALL_ARGS_SIZE == (0))) {
		if (stress_instance_zero(args))
			pr_inf_skip("%s: no system calls detected during build, skipping stressor\n",
				args->name);
		return EXIT_NO_RESOURCE;
	}

	dirfds[0] = bad_fd;

	sockfds[0] = socket(AF_UNIX, SOCK_STREAM, 0);
	sockfds[1] = bad_fd;

	ret = stress_fs_temp_dir_make_args(args);
	if (ret < 0)
		return stress_exit_status(-ret);

	(void)stress_fs_temp_filename_args(args,
		filename, sizeof(filename), stress_mwc32());
	fds[0] = open(filename, O_CREAT | O_RDWR, S_IRUSR | S_IWUSR);
	if (fds[0] < 0) {
		pr_fail("%s: open %s failed, errno=%d (%s)\n",
			args->name, filename, errno, strerror(errno));
		goto err_dir;
	}
	(void)shim_unlink(filename);
	fds[1] = bad_fd;

	stress_syscall_exercised =
		(bool *)mmap(NULL, stress_syscall_exercised_sz,
				PROT_READ | PROT_WRITE,
				MAP_SHARED | MAP_ANONYMOUS,
				-1, 0);
	if (stress_syscall_exercised == MAP_FAILED) {
		pr_fail("%s: mmap failed, errno=%d (%s)\n",
			args->name, errno, strerror(errno));
		goto tidy;
	}
	stress_memory_anon_name_set(stress_syscall_exercised, stress_syscall_exercised_sz, "syscall-stats");

	hash_table = (stress_syscall_hash_table_t *)mmap(NULL,
			sizeof(*hash_table),
			PROT_READ | PROT_WRITE,
			MAP_SHARED | MAP_ANONYMOUS, -1, 0);
	if (hash_table == MAP_FAILED) {
		pr_fail("%s: mmap failed, errno=%d (%s)\n",
			args->name, errno, strerror(errno));
		goto tidy;
	}
	stress_memory_anon_name_set(hash_table, sizeof(*hash_table), "syscall-hash-table");

	current_context = (syscall_current_context_t*)
		mmap(NULL, current_context_size, PROT_READ | PROT_WRITE,
			MAP_SHARED | MAP_ANONYMOUS, -1, 0);
	if (current_context == MAP_FAILED) {
		pr_fail("%s: mmap failed, errno=%d (%s)\n",
			args->name, errno, strerror(errno));
		goto tidy;
	}
	stress_memory_anon_name_set(current_context, current_context_size, "syscall-context");

	if (getcwd(current_context->dirfd.cwd, sizeof(current_context->dirfd.cwd)) == NULL ) {
		pr_fail("%s: getcwd failed, errno=%d (%s)\n",
			args->name, errno, strerror(errno));
		goto tidy;
	}
	if (stat(current_context->dirfd.cwd, &statbuf) < 0) {
		pr_fail("%s: stat on '%s' failed, errno=%d (%s)\n",
			args->name, current_context->dirfd.cwd, errno, strerror(errno));
		goto tidy;
	}
	current_context->dirfd.mode = statbuf.st_mode;
	current_context->dirfd.uid = statbuf.st_uid;
	current_context->dirfd.gid = statbuf.st_gid;

	for (i = 0; i < SIZEOF_ARRAY(prot_flags); i++) {
		mappings_size[i] = mmap_size;
	}

	for (i = 0; i < SIZEOF_ARRAY(prot_flags); i++) {
		mappings[i] = (uint8_t *)mmap(NULL, mappings_size[i], prot_flags[i], MAP_SHARED | MAP_ANONYMOUS, -1, 0);
		if (mappings[i] == MAP_FAILED) {
			mappings[i] = NULL;
			pr_fail("%s: mmap failed, errno=%d (%s)\n", args->name, errno, strerror(errno));
			goto tidy;
		}
		mappings_small[i] = mappings[i] + page_size - 1;
		stress_memory_anon_name_set(mappings[i], mappings_size[i], "page-data");
	}

	for (i = 0; i < SIZEOF_ARRAY(prot_flags); i++) {
#if defined(HAVE_MPROTECT)
		/* make last page inaccessible */
		(void)mprotect((void *)(mappings[i] + mappings_size[i] - page_size), page_size, PROT_NONE);
#else
		/* remove last page */
		(void)munmap((void *)(mappings[i] + mappings_size[i] - page_size), page_size);
		mappings_size[i] -= page_size;
#endif
	}

	for (i = 0, j = 0; i < SIZEOF_ARRAY(prot_flags); i++, j += 2) {
		const uint8_t *mapping = mappings[i];
		const uint8_t *mapping_small = mappings_small[i];

		sockaddrs[j] = (unsigned long int)mapping;
		sockaddrs[j + 1] = (unsigned long int)mapping_small;

		ptrs[j] = (unsigned long int)mapping;
		ptrs[j + 1] = (unsigned long int)mapping_small;

		non_null_ptrs[j] = (unsigned long int)mapping;
		non_null_ptrs[j + 1] = (unsigned long int)mapping_small;

		futex_ptrs[j] = (unsigned long int)mapping;
		futex_ptrs[j + 1] = (unsigned long int)mapping_small;
	}

	(void)shim_memset(current_context->crash_count, 0, sizeof(current_context->crash_count));

	stress_proc_state_set(args->name, STRESS_STATE_SYNC_WAIT);
	stress_sync_start_wait(args);
	stress_proc_state_set(args->name, STRESS_STATE_RUN);

	rc = stress_oomable_child(args, NULL, stress_sysinval_child, STRESS_OOMABLE_DROP_CAP);

	(void)shim_memset(syscall_exercised, 0, sizeof(syscall_exercised));
	(void)shim_memset(syscall_unique, 0, sizeof(syscall_unique));
	syscalls_exercised = 0;
	syscalls_unique = 0;
	syscalls_crashed = 0;

	/*
	 *  Determine the number of syscalls that we can test vs
 	 *  the number of syscalls actually exercised
	 */
	for (i = 0; i < SYSCALL_ARGS_SIZE; i++) {
		const unsigned long int syscall_num = stress_syscall_args[i].syscall;
		size_t exercised = 0, unique = 0;

		for (j = 0; j < SYSCALL_ARGS_SIZE; j++) {
			if (syscall_num == stress_syscall_args[j].syscall) {
				if (!syscall_unique[j]) {
					syscall_unique[j] = true;
					unique = true;
				}
				if (!syscall_exercised[j] &&
				    stress_syscall_exercised[j]) {
					syscall_exercised[j] = true;
					exercised = true;
				}
			}
		}
		if (unique)
			syscalls_unique++;
		if (exercised)
			syscalls_exercised++;
		if (current_context->crash_count[i] > 0)
			syscalls_crashed += current_context->crash_count[i];
	}
	pr_block_begin();
	pr_dbg("%s: %" PRIu64 " of %" PRIu64 " (%.2f%%) unique system calls exercised\n",
		args->name, syscalls_exercised, syscalls_unique,
		100.0 * ((double)syscalls_exercised) / (double)syscalls_unique);
	pr_dbg("%s: %" PRIu64 " unique syscalls argument combinations causing premature child termination\n",
		args->name, syscalls_crashed);
	pr_dbg("%s: ignored %" PRIu64 " unique syscall patterns that were not failing and %" PRIu64 " that timed out\n",
		args->name, current_context->skip_errno_zero, current_context->skip_timed_out);
	pr_block_end();

	stress_bogo_set(args, current_context->counter);

tidy:
	stress_proc_state_set(args->name, STRESS_STATE_DEINIT);
	if (stress_syscall_exercised && (stress_syscall_exercised != MAP_FAILED))
		(void)munmap((void *)stress_syscall_exercised, stress_syscall_exercised_sz);
	if (hash_table && (hash_table != MAP_FAILED))
		(void)munmap((void *)hash_table, sizeof(*hash_table));

	for (i = 0; i < SIZEOF_ARRAY(prot_flags); i++) {
		if (mappings[i])
			(void)munmap((void *)mappings[i], mappings_size[i]);
	}
	if (current_context && (current_context != MAP_FAILED))
		(void)munmap((void *)current_context, current_context_size);
	if (sockfds[0] >= 0)
		(void)close((int)sockfds[0]);
	if (fds[0] >= 0)
		(void)close((int)fds[0]);

err_dir:
	stress_proc_state_set(args->name, STRESS_STATE_DEINIT);
	(void)stress_fs_temp_dir_rm_args(args);

	return rc;
}

static const stress_exercises_t exercises[] = {
#if DEFSYS(_llseek)
	STRESS_EX_SYSCALL("_llseek"),
#endif
#if DEFSYS(_newselect)
	STRESS_EX_SYSCALL("_newselect"),
#endif
#if DEFSYS(_sysctl)
	STRESS_EX_SYSCALL("_sysctl"),
#endif
#if DEFSYS(accept)
	STRESS_EX_SYSCALL("accept"),
#endif
#if DEFSYS(accept4)
	STRESS_EX_SYSCALL("accept4"),
#endif
#if DEFSYS(access)
	STRESS_EX_SYSCALL("access"),
#endif
#if DEFSYS(acct)
	STRESS_EX_SYSCALL("acct"),
#endif
#if DEFSYS(acl_get)
	STRESS_EX_SYSCALL("acl_get"),
#endif
#if DEFSYS(acl_set)
	STRESS_EX_SYSCALL("acl_set"),
#endif
#if DEFSYS(add_key)
	STRESS_EX_SYSCALL("add_key"),
#endif
#if DEFSYS(adjtimex)
	STRESS_EX_SYSCALL("adjtimex"),
#endif
#if DEFSYS(afs_syscall)
	/* Should be ENOSYS */
	STRESS_EX_SYSCALL("afs_syscall"),
#endif
#if DEFSYS(alarm) && 0
	STRESS_EX_SYSCALL("alarm"),
#endif
#if DEFSYS(alloc_hugepages)
	/* removed in 2.5.44 */
	STRESS_EX_SYSCALL("alloc_hugepages"),
#endif
#if DEFSYS(arc_gettls)
	/* ARC only */
	STRESS_EX_SYSCALL("arc_gettls"),
#endif
#if DEFSYS(arc_settls)
	/* ARC only */
	STRESS_EX_SYSCALL("arc_settls"),
#endif
#if DEFSYS(arc_usr_cmpxchg)
	/* ARC only */
	STRESS_EX_SYSCALL("arc_usr_cmpxchg"),
#endif
#if DEFSYS(arch_prctl)
	STRESS_EX_SYSCALL("arch_prctl"),
#endif
#if DEFSYS(atomic_barrier)
	/* m68k only */
	STRESS_EX_SYSCALL("atomic_barrier"),
#endif
#if DEFSYS(atomic_cmpxchg_32)
	/* m68k only */
	STRESS_EX_SYSCALL("atomic_cmpxchg_32"),
#endif
#if DEFSYS(bdflush)
	/* deprecated */
	STRESS_EX_SYSCALL("bdflush"),
#endif
#if DEFSYS(bfin_spinlock)
	/* blackfin, removed in 4.17 */
	STRESS_EX_SYSCALL("bfin_spinlock"),
#endif
#if DEFSYS(bind)
	STRESS_EX_SYSCALL("bind"),
#endif
#if DEFSYS(bpf)
	/*
	STRESS_EX_SYSCALL("bpf"),
	*/
#endif
#if DEFSYS(brk)
	STRESS_EX_SYSCALL("brk"),
#endif
#if DEFSYS(breakpoint)
	/* ARM OABI only */
	STRESS_EX_SYSCALL("breakpoint"),
#endif
#if DEFSYS(cachectl)
	/* MIPS */
	STRESS_EX_SYSCALL("cachectl"),
#endif
#if DEFSYS(cacheflush)
	STRESS_EX_SYSCALL("cacheflush"),
#endif
#if DEFSYS(cache_sync)
	/* Unknown */
#endif
#if DEFSYS(capget)
	STRESS_EX_SYSCALL("capget"),
#endif
#if DEFSYS(capset)
	STRESS_EX_SYSCALL("capset"),
#endif
#if DEFSYS(chdir)
	STRESS_EX_SYSCALL("chdir"),
#endif
#if DEFSYS(chmod)
	STRESS_EX_SYSCALL("chmod"),
#endif
#if DEFSYS(chown)
	STRESS_EX_SYSCALL("chown"),
#endif
#if DEFSYS(chown32)
	STRESS_EX_SYSCALL("chown32"),
#endif
#if DEFSYS(chroot)
	STRESS_EX_SYSCALL("chroot"),
#endif
#if DEFSYS(clock_adjtime)
	STRESS_EX_SYSCALL("clock_adjtime"),
#endif
#if DEFSYS(clock_adjtime64)
	STRESS_EX_SYSCALL("clock_adjtime64"),
#endif
#if DEFSYS(clock_getres)
	STRESS_EX_SYSCALL("clock_getres"),
#endif
#if DEFSYS(clock_getres_time64)
	STRESS_EX_SYSCALL("clock_getres_time64"),
#endif
#if DEFSYS(clock_gettime)
	STRESS_EX_SYSCALL("clock_gettime"),
#endif
#if DEFSYS(clock_gettime64)
	STRESS_EX_SYSCALL("clock_gettime64"),
#endif
#if DEFSYS(clock_nanosleep)
	STRESS_EX_SYSCALL("clock_nanosleep"),
#endif
#if DEFSYS(clock_nanosleep64)
	STRESS_EX_SYSCALL("clock_nanosleep64"),
#endif
#if DEFSYS(clock_settime)
	STRESS_EX_SYSCALL("clock_settime"),
#endif
#if DEFSYS(clock_settime64)
	STRESS_EX_SYSCALL("clock_settime64"),
#endif
#if DEFSYS(clone)
	STRESS_EX_SYSCALL("clone"),
#endif
#if DEFSYS(clone2)
	/* IA-64 only */
	STRESS_EX_SYSCALL("clone2"),
#endif
#if DEFSYS(clone3)
	STRESS_EX_SYSCALL("clone3"),
#endif
#if DEFSYS(close)
	STRESS_EX_SYSCALL("close"),
#endif
#if DEFSYS(close_range)
	STRESS_EX_SYSCALL("close_range"),
#endif
#if DEFSYS(compat_exit)
	/* exiting the testing child is not a good idea */
#endif
#if DEFSYS(compat_read)
	STRESS_EX_SYSCALL("compat_read"),
#endif
#if DEFSYS(compat_restart_syscall)
	STRESS_EX_SYSCALL("compat_restart_syscall"),
#endif
#if DEFSYS(compat_rt_sigreturn)
	STRESS_EX_SYSCALL("compat_rt_sigreturn"),
#endif
#if DEFSYS(compat_write)
	STRESS_EX_SYSCALL("compat_write"),
#endif
#if DEFSYS(cmpxchg_badaddr)
	/* Tile only, removed 4.17 */
#endif
#if DEFSYS(connect)
	STRESS_EX_SYSCALL("connect"),
#endif
#if DEFSYS(copy_file_range)
	STRESS_EX_SYSCALL("copy_file_range"),
#endif
#if DEFSYS(creat)
	STRESS_EX_SYSCALL("creat"),
#endif
#if DEFSYS(create_module)
	STRESS_EX_SYSCALL("create_module"),
#endif
#if DEFSYS(delete_module)
	STRESS_EX_SYSCALL("delete_module"),
#endif
#if DEFSYS(dma_memcpy)
	/* blackfin, removed in 4.17 */
#endif
#if DEFSYS(dup)
	STRESS_EX_SYSCALL("dup"),
#endif
#if DEFSYS(dup2)
	STRESS_EX_SYSCALL("dup2"),
#endif
#if DEFSYS(dup3)
	STRESS_EX_SYSCALL("dup3"),
#endif
#if DEFSYS(epoll_create)
	STRESS_EX_SYSCALL("epoll_create"),
#endif
#if DEFSYS(epoll_create1)
	STRESS_EX_SYSCALL("epoll_create1"),
#endif
#if DEFSYS(epoll_ctl)
	STRESS_EX_SYSCALL("epoll_ctl"),
#endif
#if DEFSYS(epoll_ctl_add)
	STRESS_EX_SYSCALL("epoll_ctl_add"),
#endif
#if DEFSYS(epoll_wait)
	STRESS_EX_SYSCALL("epoll_wait"),
#endif
#if DEFSYS(epoll_wait_old)
	STRESS_EX_SYSCALL("epoll_wait_old"),
#endif
#if DEFSYS(epoll_pwait)
	STRESS_EX_SYSCALL("epoll_pwait"),
#endif
#if DEFSYS(eventfd)
	STRESS_EX_SYSCALL("eventfd"),
#endif
#if DEFSYS(eventfd2)
	STRESS_EX_SYSCALL("eventfd2"),
#endif
#if DEFSYS(exec_with_loader)
	STRESS_EX_SYSCALL("exec_with_loader"),
#endif
#if DEFSYS(execv)
	STRESS_EX_SYSCALL("execv"),
#endif
#if DEFSYS(execve)
	STRESS_EX_SYSCALL("execve"),
#endif
#if DEFSYS(execveat)
	STRESS_EX_SYSCALL("execveat"),
#endif
#if DEFSYS(exit)
	/* exiting the testing child is not a good idea */
#endif
#if DEFSYS(exit_group)
	/* exiting the testing child is not a good idea */
#endif
#if DEFSYS(faccessat)
	STRESS_EX_SYSCALL("faccessat"),
#endif
#if DEFSYS(fadvise64)
	STRESS_EX_SYSCALL("fadvise64"),
#endif
#if DEFSYS(fadvise64_64)
	STRESS_EX_SYSCALL("fadvise64_64"),
#endif
#if DEFSYS(fallocate)
	STRESS_EX_SYSCALL("fallocate"),
#endif
#if DEFSYS(fanotify_init)
	STRESS_EX_SYSCALL("fanotify_init"),
#endif
#if DEFSYS(fanotify_mark)
	STRESS_EX_SYSCALL("fanotify_mark"),
#endif
#if DEFSYS(fchdir)
	STRESS_EX_SYSCALL("fchdir"),
#endif
#if DEFSYS(fchmod)
	STRESS_EX_SYSCALL("fchmod"),
#endif
#if DEFSYS(fchmodat)
	STRESS_EX_SYSCALL("fchmodat"),
#endif
#if DEFSYS(fchmodat2)
	STRESS_EX_SYSCALL("fchmodat2"),
#endif
#if DEFSYS(fchown)
	STRESS_EX_SYSCALL("fchown"),
#endif
#if DEFSYS(fchown32)
	STRESS_EX_SYSCALL("fchown32"),
#endif
#if DEFSYS(fchownat)
	STRESS_EX_SYSCALL("fchownat"),
#endif
#if DEFSYS(fcntl)
	STRESS_EX_SYSCALL("fcntl"),
#endif
#if DEFSYS(fcntl64)
	STRESS_EX_SYSCALL("fcntl64"),
#endif
#if DEFSYS(fdatasync)
	STRESS_EX_SYSCALL("fdatasync"),
#endif
#if DEFSYS(fgetxattr)
	STRESS_EX_SYSCALL("fgetxattr"),
#endif
#if DEFSYS(finit_module)
	STRESS_EX_SYSCALL("finit_module"),
#endif
#if DEFSYS(flistxattr)
	STRESS_EX_SYSCALL("flistxattr"),
#endif
#if DEFSYS(flock)
	STRESS_EX_SYSCALL("flock"),
#endif
#if DEFSYS(fork)
	STRESS_EX_SYSCALL("fork"),
#endif
#if DEFSYS(fp_udfiex_crtl)
	STRESS_EX_SYSCALL("fp_udfiex_crtl"),
#endif
#if DEFSYS(free_hugepages)
	STRESS_EX_SYSCALL("free_hugepages"),
#endif
#if DEFSYS(fremovexattr)
	STRESS_EX_SYSCALL("fremovexattr"),
#endif
#if DEFSYS(fsconfig)
	STRESS_EX_SYSCALL("fsconfig"),
#endif
#if DEFSYS(fsetxattr)
	STRESS_EX_SYSCALL("fsetxattr"),
#endif
#if DEFSYS(fsmount)
	STRESS_EX_SYSCALL("fsmount"),
#endif
#if DEFSYS(fsopen)
	STRESS_EX_SYSCALL("fsopen"),
#endif
#if DEFSYS(fspick)
	STRESS_EX_SYSCALL("fspick"),
#endif
#if DEFSYS(fstat)
	STRESS_EX_SYSCALL("fstat"),
#endif
#if DEFSYS(fstat64)
	STRESS_EX_SYSCALL("fstat64"),
#endif
#if DEFSYS(fstatat)
	STRESS_EX_SYSCALL("fstatat"),
#endif
#if DEFSYS(fstatat64)
	STRESS_EX_SYSCALL("fstatat64"),
#endif
#if DEFSYS(fstatfs)
	STRESS_EX_SYSCALL("fstatfs"),
#endif
#if DEFSYS(fstatfs64)
	STRESS_EX_SYSCALL("fstatfs64"),
#endif
#if DEFSYS(fsync)
	STRESS_EX_SYSCALL("fsync"),
#endif
#if DEFSYS(ftime)
	/* Deprecated */
	STRESS_EX_SYSCALL("ftime"),
#endif
#if DEFSYS(ftruncate)
	STRESS_EX_SYSCALL("ftruncate"),
#endif
#if DEFSYS(ftruncate64)
	STRESS_EX_SYSCALL("ftruncate64"),
#endif
#if DEFSYS(futex)
	STRESS_EX_SYSCALL("futex"),
#endif
#if DEFSYS(futex_waitv)
	STRESS_EX_SYSCALL("futex_waitv"),
#endif
#if DEFSYS(futex_time64)
	STRESS_EX_SYSCALL("futex_time64"),
#endif
#if DEFSYS(futimens)
	STRESS_EX_SYSCALL("futimens"),
#endif
#if DEFSYS(futimesat)
	/* Obsolete */
	STRESS_EX_SYSCALL("futimesat"),
#endif
#if DEFSYS(get_kernel_syms)
	/* deprecated in 2.6 */
	STRESS_EX_SYSCALL("get_kernel_syms"),
#endif
#if DEFSYS(get_mempolicy)
	STRESS_EX_SYSCALL("get_mempolicy"),
#endif
#if DEFSYS(get_robust_list)
	STRESS_EX_SYSCALL("get_robust_list"),
#endif
#if DEFSYS(get_thread_area)
	STRESS_EX_SYSCALL("get_thread_area"),
#endif
#if DEFSYS(get_tls)
	/* ARM OABI only */
	STRESS_EX_SYSCALL("get_tls"),
#endif
#if DEFSYS(getcpu)
	STRESS_EX_SYSCALL("getcpu"),
#endif
#if DEFSYS(getcwd)
	STRESS_EX_SYSCALL("getcwd"),
#endif
#if DEFSYS(getdtablesize)
	/* SPARC, removed in 2.6.26 */
#endif
#if DEFSYS(getdents)
	STRESS_EX_SYSCALL("getdents"),
#endif
#if DEFSYS(getdents64)
	STRESS_EX_SYSCALL("getdents64"),
#endif
#if DEFSYS(getdomainname)
	STRESS_EX_SYSCALL("getdomainname"),
#endif
#if DEFSYS(getdtablesize)
	STRESS_EX_SYSCALL("getdtablesize"),
#endif
#if DEFSYS(getegid)
	STRESS_EX_SYSCALL("getegid"),
#endif
#if DEFSYS(getegid32)
	STRESS_EX_SYSCALL("getegid32"),
#endif
#if DEFSYS(geteuid)
	STRESS_EX_SYSCALL("geteuid"),
#endif
#if DEFSYS(geteuid32)
	STRESS_EX_SYSCALL("geteuid32"),
#endif
#if DEFSYS(getgid)
	STRESS_EX_SYSCALL("getgid"),
#endif
#if DEFSYS(getgid32)
	STRESS_EX_SYSCALL("getgid32"),
#endif
#if DEFSYS(getgroups)
	STRESS_EX_SYSCALL("getgroups"),
#endif
#if DEFSYS(getgroups32)
	STRESS_EX_SYSCALL("getgroups32"),
#endif
#if DEFSYS(gethostname)
	STRESS_EX_SYSCALL("gethostname"),
#endif
#if DEFSYS(getitimer)
	STRESS_EX_SYSCALL("getitimer"),
#endif
#if DEFSYS(getpagesize)
	STRESS_EX_SYSCALL("getpagesize"),
#endif
#if DEFSYS(getpeername)
	STRESS_EX_SYSCALL("getpeername"),
#endif
#if DEFSYS(getpgid)
	STRESS_EX_SYSCALL("getpgid"),
#endif
#if DEFSYS(getpid)
	STRESS_EX_SYSCALL("getpid"),
#endif
#if DEFSYS(getpgrp)
	STRESS_EX_SYSCALL("getpgrp"),
#endif
#if DEFSYS(getpmsg)
	/* Unimplemented */
	STRESS_EX_SYSCALL("getpmsg"),
#endif
#if DEFSYS(getppid)
	STRESS_EX_SYSCALL("getppid"),
#endif
#if DEFSYS(getpriority)
	STRESS_EX_SYSCALL("getpriority"),
#endif
#if DEFSYS(getrandom)
	STRESS_EX_SYSCALL("getrandom"),
#endif
#if DEFSYS(getresgid)
	STRESS_EX_SYSCALL("getresgid"),
#endif
#if DEFSYS(getresgid32)
	STRESS_EX_SYSCALL("getresgid32"),
#endif
#if DEFSYS(getresuid)
	STRESS_EX_SYSCALL("getresuid"),
#endif
#if DEFSYS(getresuid32)
	STRESS_EX_SYSCALL("getresuid32"),
#endif
#if DEFSYS(getrlimit)
	STRESS_EX_SYSCALL("getrlimit"),
#endif
#if DEFSYS(getrusage)
	STRESS_EX_SYSCALL("getrusage"),
#endif
#if DEFSYS(getsid)
	STRESS_EX_SYSCALL("getsid"),
#endif
#if DEFSYS(getsockname)
	STRESS_EX_SYSCALL("getsockname"),
#endif
#if DEFSYS(getsockopt)
	STRESS_EX_SYSCALL("getsockopt"),
#endif
#if DEFSYS(gettid)
	STRESS_EX_SYSCALL("gettid"),
#endif
#if DEFSYS(gettimeofday)
	STRESS_EX_SYSCALL("gettimeofday"),
#endif
#if DEFSYS(getuid)
	STRESS_EX_SYSCALL("getuid"),
#endif
#if DEFSYS(getuid32)
	STRESS_EX_SYSCALL("getuid32"),
#endif
#if DEFSYS(getunwind)
	/* IA-64-specific, obsolete too */
	STRESS_EX_SYSCALL("getunwind"),
#endif
#if DEFSYS(getxattr)
	STRESS_EX_SYSCALL("getxattr"),
#endif
#if DEFSYS(getxgid)
	/* Alpha only */
	STRESS_EX_SYSCALL("getxgid"),
#endif
#if DEFSYS(getxpid)
	/* Alpha only */
	STRESS_EX_SYSCALL("getxpid"),
#endif
#if DEFSYS(getxuid)
	/* Alpha only */
	STRESS_EX_SYSCALL("getxuid"),
#endif
#if DEFSYS(idle)
	STRESS_EX_SYSCALL("idle"),
#endif
#if DEFSYS(init_module)
	STRESS_EX_SYSCALL("init_module"),
#endif
#if DEFSYS(inotify_add_watch)
	STRESS_EX_SYSCALL("inotify_add_watch"),
#endif
#if DEFSYS(inotify_init)
	STRESS_EX_SYSCALL("inotify_init"),
#endif
#if DEFSYS(inotify_init1)
	STRESS_EX_SYSCALL("inotify_init1"),
#endif
#if DEFSYS(inotify_rm_watch)
	STRESS_EX_SYSCALL("inotify_rm_watch"),
#endif
#if DEFSYS(io_cancel)
	STRESS_EX_SYSCALL("io_cancel"),
#endif
#if DEFSYS(io_destroy)
	STRESS_EX_SYSCALL("io_destroy"),
#endif
#if DEFSYS(io_getevents)
	STRESS_EX_SYSCALL("io_getevents"),
#endif
#if DEFSYS(io_pgetevents)
	STRESS_EX_SYSCALL("io_pgetevents"),
#endif
#if DEFSYS(io_pgetevents_time32)
	STRESS_EX_SYSCALL("io_pgetevents_time32"),
#endif
#if DEFSYS(io_pgetevents_time64)
	STRESS_EX_SYSCALL("io_pgetevents_time64"),
#endif
#if DEFSYS(io_setup)
	STRESS_EX_SYSCALL("io_setup"),
#endif
#if DEFSYS(io_submit)
	STRESS_EX_SYSCALL("io_setup"),
#endif
#if DEFSYS(io_uring_enter)
	STRESS_EX_SYSCALL("io_uring_enter"),
#endif
#if DEFSYS(io_uring_register)
	STRESS_EX_SYSCALL("io_uring_register"),
#endif
#if DEFSYS(io_uring_setup)
	STRESS_EX_SYSCALL("io_uring_setup"),
#endif
#if DEFSYS(ioctl)
	STRESS_EX_SYSCALL("ioctl"),
#endif
#if DEFSYS(ioperm)
	STRESS_EX_SYSCALL("ioperm"),
#endif
#if DEFSYS(iopl)
	STRESS_EX_SYSCALL("iopl"),
#endif
#if DEFSYS(ioprio_get)
	STRESS_EX_SYSCALL("ioprio_get"),
#endif
#if DEFSYS(ioprio_set)
	STRESS_EX_SYSCALL("ioprio_set"),
#endif
#if DEFSYS(ipc)
	STRESS_EX_SYSCALL("ipc"),
#endif
#if DEFSYS(kcmp)
	STRESS_EX_SYSCALL("kcmp"),
#endif
#if DEFSYS(kern_features)
	/* SPARC64 only */
	STRESS_EX_SYSCALL("kern_features"),
#endif
#if DEFSYS(kexec_file_load)
	STRESS_EX_SYSCALL("kexec_file_load"),
#endif
#if DEFSYS(kexec_load)
	STRESS_EX_SYSCALL("kexec_load"),
#endif
#if DEFSYS(keyctl)
	STRESS_EX_SYSCALL("keyctl"),
#endif
#if DEFSYS(kill)
	STRESS_EX_SYSCALL("kill"),
#endif
#if DEFSYS(landlock_add_rule)
	STRESS_EX_SYSCALL("landlock_create_ruleset"),
#endif
#if DEFSYS(landlock_create_ruleset)
	STRESS_EX_SYSCALL("landlock_create_ruleset"),
#endif
#if DEFSYS(landlock_restrict_self)
	STRESS_EX_SYSCALL("landlock_restrict_self"),
#endif
#if DEFSYS(lchown)
	STRESS_EX_SYSCALL("lchown"),
#endif
#if DEFSYS(lchown32)
	STRESS_EX_SYSCALL("lchown32"),
#endif
#if DEFSYS(lgetxattr)
	STRESS_EX_SYSCALL("lgetxattr"),
#endif
#if DEFSYS(link)
	STRESS_EX_SYSCALL("link"),
#endif
#if DEFSYS(linkat)
	STRESS_EX_SYSCALL("linkat"),
#endif
#if DEFSYS(listen)
	STRESS_EX_SYSCALL("listen"),
#endif
#if DEFSYS(listxattr)
	STRESS_EX_SYSCALL("listxattr"),
#endif
#if DEFSYS(llistxattr)
	STRESS_EX_SYSCALL("llistxattr"),
#endif
#if DEFSYS(llseek)
	STRESS_EX_SYSCALL("llseek"),
#endif
#if DEFSYS(lock)
	/* Unimplemented, deprecated */
#endif
#if DEFSYS(lookup_dcookie)
	STRESS_EX_SYSCALL("lookup_dcookie"),
#endif
#if DEFSYS(lremovexattr)
	STRESS_EX_SYSCALL("lremovexattr"),
#endif
#if DEFSYS(lseek)
	STRESS_EX_SYSCALL("lseek"),
#endif
#if DEFSYS(lsetxattr)
	STRESS_EX_SYSCALL("lsetxattr"),
#endif
#if DEFSYS(lstat)
	STRESS_EX_SYSCALL("lstat"),
#endif
#if DEFSYS(lstat64)
	STRESS_EX_SYSCALL("lstat64"),
#endif
#if DEFSYS(lws_enties)
	/* PARISC, todo */
#endif
#if DEFSYS(map_shadow_stack)
	STRESS_EX_SYSCALL("map_shadow_stack"),
#endif
#if DEFSYS(madvise)
	STRESS_EX_SYSCALL("madvise"),
#endif
#if DEFSYS(madvise1)
	/* Unimplemented, deprecated */
#endif
#if DEFSYS(map_shadow_stack)
	STRESS_EX_SYSCALL("map_shadow_stack"),
#endif
#if DEFSYS(mbind)
	STRESS_EX_SYSCALL("mbind"),
#endif
#if DEFSYS(memory_ordering)
	/* SPARC64 only */
	STRESS_EX_SYSCALL("memory_ordering"),
#endif
#if DEFSYS(membarrier)
	STRESS_EX_SYSCALL("membarrier"),
#endif
#if DEFSYS(memfd_create)
	STRESS_EX_SYSCALL("memfd_create"),
#endif
#if DEFSYS(memfd_secret)
	STRESS_EX_SYSCALL("memfd_secret"),
#endif
#if DEFSYS(memory_ordering)
	STRESS_EX_SYSCALL("memory_ordering"),
#endif
#if DEFSYS(migrate_pages)
	STRESS_EX_SYSCALL("migrate_pages"),
#endif
#if DEFSYS(mincore)
	STRESS_EX_SYSCALL("mincore"),
#endif
#if DEFSYS(mkdir)
	STRESS_EX_SYSCALL("mkdir"),
#endif
#if DEFSYS(mkdirat)
	STRESS_EX_SYSCALL("mkdirat"),
#endif
#if DEFSYS(mknod)
	STRESS_EX_SYSCALL("mknod"),
#endif
#if DEFSYS(mknodat)
	STRESS_EX_SYSCALL("mknodat"),
#endif
#if DEFSYS(mlock)
	STRESS_EX_SYSCALL("mlock"),
#endif
#if DEFSYS(mlock2)
	STRESS_EX_SYSCALL("mlock2"),
#endif
#if DEFSYS(mlockall)
	STRESS_EX_SYSCALL("mlockall"),
#endif
#if DEFSYS(mmap)
	STRESS_EX_SYSCALL("mmap"),
#endif
#if DEFSYS(mmap2)
	STRESS_EX_SYSCALL("mmap2"),
#endif
#if DEFSYS(mmap_pgoff)
	STRESS_EX_SYSCALL("mmap_pgoff"),
#endif
#if DEFSYS(modify_ldt)
	STRESS_EX_SYSCALL("modify_ldt"),
#endif
#if DEFSYS(mount)
	STRESS_EX_SYSCALL("mount"),
#endif
#if DEFSYS(mount_setattr)
	STRESS_EX_SYSCALL("mount_setattr"),
#endif
#if DEFSYS(move_mount)
	STRESS_EX_SYSCALL("move_mount"),
#endif
#if DEFSYS(move_pages)
	STRESS_EX_SYSCALL("move_pages"),
#endif
#if DEFSYS(mprotect)
	STRESS_EX_SYSCALL("mprotect"),
#endif
#if DEFSYS(mpx)
	/* Unimplemented, deprecated */
#endif
#if DEFSYS(mq_close)
	STRESS_EX_SYSCALL("mq_close"),
#endif
#if DEFSYS(mq_getsetattr)
	STRESS_EX_SYSCALL("mq_getsetattr"),
#endif
#if DEFSYS(mq_notify)
	STRESS_EX_SYSCALL("mq_notify"),
#endif
#if DEFSYS(mq_open)
	STRESS_EX_SYSCALL("mq_open"),
#endif
#if DEFSYS(mq_receive)
	STRESS_EX_SYSCALL("mq_receive"),
#endif
#if DEFSYS(mq_send)
	STRESS_EX_SYSCALL("mq_send"),
#endif
#if DEFSYS(mq_timedreceive)
	STRESS_EX_SYSCALL("mq_timedreceive"),
#endif
#if DEFSYS(mq_timedreceive_time64)
	STRESS_EX_SYSCALL("mq_timedreceive_time64"),
#endif
#if DEFSYS(mq_timedsend)
	STRESS_EX_SYSCALL("mq_timedsend"),
#endif
#if DEFSYS(mq_timedsend_time64)
	STRESS_EX_SYSCALL("mq_timedsend_time64"),
#endif
#if DEFSYS(mq_unlink)
	STRESS_EX_SYSCALL("mq_unlink"),
#endif
#if DEFSYS(mremap)
	STRESS_EX_SYSCALL("mremap"),
#endif
#if DEFSYS(mseal)
	STRESS_EX_SYSCALL("mseal"),
#endif
#if DEFSYS(msgctl)
	STRESS_EX_SYSCALL("msgctl"),
#endif
#if DEFSYS(msgget)
	STRESS_EX_SYSCALL("msgget"),
#endif
#if DEFSYS(msgrcv)
	STRESS_EX_SYSCALL("msgrcv"),
#endif
#if DEFSYS(msgsnd)
	STRESS_EX_SYSCALL("msgsnd"),
#endif
#if DEFSYS(msync)
	STRESS_EX_SYSCALL("msync"),
#endif
#if DEFSYS(multiplexer)
	STRESS_EX_SYSCALL("multiplexer"),
#endif
#if DEFSYS(munlock)
	STRESS_EX_SYSCALL("munlock"),
#endif
#if DEFSYS(munlockall)
	STRESS_EX_SYSCALL("munlockall"),
#endif
#if DEFSYS(munmap)
	STRESS_EX_SYSCALL("munmap"),
#endif
#if DEFSYS(name_to_handle_at)
	STRESS_EX_SYSCALL("name_to_handle_at"),
#endif
#if DEFSYS(nanosleep)
	STRESS_EX_SYSCALL("nanosleep"),
#endif
#if DEFSYS(newfstat)
	STRESS_EX_SYSCALL("newfstat"),
#endif
#if DEFSYS(newfstat64)
	STRESS_EX_SYSCALL("newfstat64"),
#endif
#if DEFSYS(newfstatat)
	STRESS_EX_SYSCALL("newfstatat"),
#endif
#if DEFSYS(newfstatat64)
	STRESS_EX_SYSCALL("newfstatat64"),
#endif
#if DEFSYS(newlstat)
	STRESS_EX_SYSCALL("newlstat"),
#endif
#if DEFSYS(newstat)
	STRESS_EX_SYSCALL("newstat"),
#endif
#if DEFSYS(newuname)
	STRESS_EX_SYSCALL("newuname"),
#endif
#if DEFSYS(nfsservctl)
	STRESS_EX_SYSCALL("nfsservctl"),
#endif
#if DEFSYS(nice)
	STRESS_EX_SYSCALL("nice"),
#endif
#if DEFSYS(ni_syscall)
	/* Omit */
#endif
#if DEFSYS(old_adjtimex)
	STRESS_EX_SYSCALL("old_adjtimex"),
#endif
#if DEFSYS(oldfstat)
	STRESS_EX_SYSCALL("oldfstat"),
#endif
#if DEFSYS(old_getrlimit)
	STRESS_EX_SYSCALL("old_getrlimit"),
#endif
#if DEFSYS(oldlstat)
	STRESS_EX_SYSCALL("oldlstat"),
#endif
#if DEFSYS(oldolduname)
	STRESS_EX_SYSCALL("oldolduname"),
#endif
#if DEFSYS(oldstat)
	STRESS_EX_SYSCALL("oldstat"),
	STRESS_EX_SYSCALL("oldstat"),
#endif
#if DEFSYS(oldumount)
	STRESS_EX_SYSCALL("oldumount"),
#endif
#if DEFSYS(olduname)
	STRESS_EX_SYSCALL("olduname"),
#endif
#if DEFSYS(oldwait4)
	STRESS_EX_SYSCALL("oldwait4"),
#endif
#if DEFSYS(open)
	STRESS_EX_SYSCALL("open"),
#endif
#if DEFSYS(open_by_handle_at)
	STRESS_EX_SYSCALL("open_by_handle_at"),
#endif
#if DEFSYS(open_tree)
	STRESS_EX_SYSCALL("open_tree"),
#endif
#if DEFSYS(openat)
	STRESS_EX_SYSCALL("openat"),
#endif
#if DEFSYS(openat2)
	STRESS_EX_SYSCALL("openat2"),
#endif
#if DEFSYS(or1k_atomic)
	/* OpenRISC 1000 only */
#endif
#if DEFSYS(pause)
	STRESS_EX_SYSCALL("pause"),
#endif
#if DEFSYS(pciconfig_iobase)
	STRESS_EX_SYSCALL("pciconfig_iobase"),
#endif
#if DEFSYS(pciconfig_read)
	STRESS_EX_SYSCALL("pciconfig_read"),
#endif
#if DEFSYS(pciconfig_write)
	STRESS_EX_SYSCALL("pciconfig_write"),
#endif
#if DEFSYS(perf_event_open)
	STRESS_EX_SYSCALL("perf_event_open"),
#endif
#if DEFSYS(perfmonctl)
	STRESS_EX_SYSCALL("perfmonctl"),
#endif
#if DEFSYS(personality)
	STRESS_EX_SYSCALL("personality"),
#endif
#if DEFSYS(pidfd_getfd)
	STRESS_EX_SYSCALL("pidfd_getfd"),
#endif
#if DEFSYS(pidfd_open)
	STRESS_EX_SYSCALL("pidfd_open"),
#endif
#if DEFSYS(pidfd_send_signal)
	STRESS_EX_SYSCALL("pidfd_send_signal"),
#endif
#if DEFSYS(pipe)
	STRESS_EX_SYSCALL("pipe"),
#endif
#if DEFSYS(pipe2)
	STRESS_EX_SYSCALL("pipe2"),
#endif
#if DEFSYS(pivot_root)
	STRESS_EX_SYSCALL("pivot_root"),
#endif
#if DEFSYS(pkey_alloc)
	STRESS_EX_SYSCALL("pkey_alloc"),
#endif
#if DEFSYS(pkey_free)
	STRESS_EX_SYSCALL("pkey_free"),
#endif
#if DEFSYS(pkey_get)
	STRESS_EX_SYSCALL("pkey_get"),
#endif
#if DEFSYS(pkey_mprotect)
	STRESS_EX_SYSCALL("pkey_mprotect"),
#endif
#if DEFSYS(pkey_set)
	STRESS_EX_SYSCALL("pkey_set"),
#endif
#if DEFSYS(poll)
	STRESS_EX_SYSCALL("poll"),
#endif
#if DEFSYS(ppoll)
	STRESS_EX_SYSCALL("ppoll"),
#endif
#if DEFSYS(ppoll_time64)
	STRESS_EX_SYSCALL("ppoll_time64"),
#endif
#if DEFSYS(prctl)
	STRESS_EX_SYSCALL("prctl"),
#endif
#if DEFSYS(pread)
	STRESS_EX_SYSCALL("pread"),
#endif
#if DEFSYS(pread64)
	STRESS_EX_SYSCALL("pread64"),
#endif
#if DEFSYS(preadv)
	STRESS_EX_SYSCALL("preadv"),
#endif
#if DEFSYS(preadv2)
	STRESS_EX_SYSCALL("preadv2"),
#endif
#if DEFSYS(prlimit)
	STRESS_EX_SYSCALL("prlimit"),
#endif
#if DEFSYS(prlimit64)
	STRESS_EX_SYSCALL("prlimit64"),
#endif
#if DEFSYS(process_madvise)
	STRESS_EX_SYSCALL("process_madvise"),
#endif
#if DEFSYS(process_mrelease)
	STRESS_EX_SYSCALL("process_mrelease"),
#endif
#if DEFSYS(process_vm_readv)
	STRESS_EX_SYSCALL("process_vm_readv"),
#endif
#if DEFSYS(process_vm_writev)
	STRESS_EX_SYSCALL("process_vm_writev"),
#endif
#if DEFSYS(prof)
	STRESS_EX_SYSCALL("prof"),
#endif
#if DEFSYS(profil)
	STRESS_EX_SYSCALL("profil"),
#endif
#if DEFSYS(pselect)
	STRESS_EX_SYSCALL("pselect"),
#endif
#if DEFSYS(pselect6)
	STRESS_EX_SYSCALL("pselect6"),
#endif
#if DEFSYS(pselect6_time64)
	STRESS_EX_SYSCALL("pselect6_time64"),
#endif
#if DEFSYS(ptrace)
	STRESS_EX_SYSCALL("ptrace"),
#endif
#if DEFSYS(putpmsg)
	STRESS_EX_SYSCALL("putpmsg"),
#endif
#if DEFSYS(pwrite)
	STRESS_EX_SYSCALL("pwrite"),
#endif
#if DEFSYS(pwrite64)
	STRESS_EX_SYSCALL("pwrite64"),
#endif
#if DEFSYS(pwritev)
	STRESS_EX_SYSCALL("pwritev"),
#endif
#if DEFSYS(pwritev2)
	STRESS_EX_SYSCALL("pwritev2"),
#endif
#if DEFSYS(query_module)
	STRESS_EX_SYSCALL("query_module"),
#endif
#if DEFSYS(quotactl)
	STRESS_EX_SYSCALL("quotactl"),
#endif
#if DEFSYS(quotactl_fd)
	STRESS_EX_SYSCALL("quotactl_fd"),
#endif
#if DEFSYS(read)
	STRESS_EX_SYSCALL("read"),
#endif
#if DEFSYS(readahead)
	STRESS_EX_SYSCALL("readahead"),
#endif
#if DEFSYS(readdir)
	STRESS_EX_SYSCALL("readdir"),
#endif
#if DEFSYS(readlink)
	STRESS_EX_SYSCALL("readlink"),
#endif
#if DEFSYS(readlinkat)
	STRESS_EX_SYSCALL("readlinkat"),
#endif
#if DEFSYS(readv)
	STRESS_EX_SYSCALL("readv"),
#endif
#if DEFSYS(reboot)
	STRESS_EX_SYSCALL("reboot"),
#endif
#if DEFSYS(recv)
	STRESS_EX_SYSCALL("recv"),
#endif
#if DEFSYS(recvfrom)
	STRESS_EX_SYSCALL("recvfrom"),
#endif
#if DEFSYS(recvmsg)
	STRESS_EX_SYSCALL("recvmsg"),
#endif
#if DEFSYS(recvmmsg)
	STRESS_EX_SYSCALL("recvmmsg"),
#endif
#if DEFSYS(recvmmsg_time64)
	STRESS_EX_SYSCALL("recvmmsg_time64"),
#endif
#if DEFSYS(remap_file_pages)
	STRESS_EX_SYSCALL("remap_file_pages"),
#endif
#if DEFSYS(removexattr)
	STRESS_EX_SYSCALL("removexattr"),
#endif
#if DEFSYS(rename)
	STRESS_EX_SYSCALL("rename"),
#endif
#if DEFSYS(renameat)
	STRESS_EX_SYSCALL("renameat"),
#endif
#if DEFSYS(renameat2)
	STRESS_EX_SYSCALL("renameat2"),
#endif
#if DEFSYS(request_key)
	STRESS_EX_SYSCALL("request_key"),
#endif
#if DEFSYS(restart_syscall)
	STRESS_EX_SYSCALL("restart_syscall"),
#endif
#if DEFSYS(riscv_flush_icache)
	STRESS_EX_SYSCALL("riscv_flush_icache"),
#endif
#if DEFSYS(riscv_hwprobe)
	STRESS_EX_SYSCALL("riscv_hwprobe"),
#endif
#if DEFSYS(rmdir)
	STRESS_EX_SYSCALL("rmdir"),
#endif
#if DEFSYS(rseq)
	STRESS_EX_SYSCALL("rseq"),
#endif
#if DEFSYS(sigaction)
	STRESS_EX_SYSCALL("sigaction"),
#endif
#if DEFSYS(rt_sigaction)
	STRESS_EX_SYSCALL("sigaction"),
#endif
#if DEFSYS(rt_sigpending)
	STRESS_EX_SYSCALL("sigpending"),
#endif
#if DEFSYS(rt_sigprocmask)
	STRESS_EX_SYSCALL("sigprocmask"),
#endif
#if DEFSYS(rt_sigqueueinfo)
	STRESS_EX_SYSCALL("sigqueueinfo"),
#endif
#if DEFSYS(rt_sigreturn)
	STRESS_EX_SYSCALL("sigreturn"),
#endif
#if DEFSYS(rt_sigsuspend)
	STRESS_EX_SYSCALL("sigsuspend"),
#endif
#if DEFSYS(rt_sigtimedwait)
	STRESS_EX_SYSCALL("sigtimedwait"),
#endif
#if DEFSYS(rt_sigtimedwait_64)
	STRESS_EX_SYSCALL("sigtimedwait_64"),
#endif
#if DEFSYS(rt_tgsigqueueinfo)
	STRESS_EX_SYSCALL("tgsigqueueinfo"),
#endif
#if DEFSYS(rtas)
	STRESS_EX_SYSCALL("rtas"),
#endif
#if DEFSYS(s390_runtime_instr)
	STRESS_EX_SYSCALL("s390_runtime_instr"),
#endif
#if DEFSYS(s390_pci_mmio_read)
	STRESS_EX_SYSCALL("s390_pci_mmio_read"),
#endif
#if DEFSYS(s390_pci_mmio_write)
	STRESS_EX_SYSCALL("s390_pci_mmio_write"),
#endif
#if DEFSYS(s390_sthyi)
	STRESS_EX_SYSCALL("s390_sthyi"),
#endif
#if DEFSYS(s390_guarded_storage)
	STRESS_EX_SYSCALL("s390_guarded_storage"),
#endif
#if DEFSYS(sched_get_priority_max)
	STRESS_EX_SYSCALL("sched_get_priority_max"),
#endif
#if DEFSYS(sched_get_priority_min)
	STRESS_EX_SYSCALL("sched_get_priority_min"),
#endif
#if DEFSYS(sched_getaffinity)
	STRESS_EX_SYSCALL("sched_getaffinity"),
#endif
#if DEFSYS(sched_getattr)
	STRESS_EX_SYSCALL("sched_getattr"),
#endif
#if DEFSYS(sched_getparam)
	STRESS_EX_SYSCALL("sched_getparam"),
#endif
#if DEFSYS(sched_getscheduler)
	STRESS_EX_SYSCALL("sched_getscheduler"),
#endif
#if DEFSYS(sched_get_rr_interval)
	STRESS_EX_SYSCALL("sched_get_rr_interval"),
#endif
#if DEFSYS(sched_set_affinity)
	/* SPARC & SPARC64 */
	STRESS_EX_SYSCALL("sched_set_affinity"),
#endif
#if DEFSYS(sched_setaffinity)
	STRESS_EX_SYSCALL("sched_setaffinity"),
#endif
#if DEFSYS(sched_setattr)
	STRESS_EX_SYSCALL("sched_setattr"),
#endif
#if DEFSYS(sched_setparam)
	STRESS_EX_SYSCALL("sched_setparam"),
#endif
#if DEFSYS(sched_yield)
	STRESS_EX_SYSCALL("sched_yield"),
#endif
#if DEFSYS(seccomp)
	STRESS_EX_SYSCALL("seccomp"),
#endif
#if DEFSYS(seccomp_exit)
	STRESS_EX_SYSCALL("seccomp_exit"),
#endif
#if DEFSYS(seccomp_exit_32)
	STRESS_EX_SYSCALL("seccomp_exit_32"),
#endif
#if DEFSYS(seccomp_read)
	STRESS_EX_SYSCALL("seccomp_read"),
#endif
#if DEFSYS(seccomp_read_32)
	STRESS_EX_SYSCALL("seccomp_read_32"),
#endif
#if DEFSYS(seccomp_sigreturn)
	STRESS_EX_SYSCALL("seccomp_sigreturn"),
#endif
#if DEFSYS(seccomp_sigreturn_32)
	STRESS_EX_SYSCALL("seccomp_sigreturn_32"),
#endif
#if DEFSYS(seccomp_write)
	STRESS_EX_SYSCALL("seccomp_write"),
#endif
#if DEFSYS(seccomp_write_32)
	STRESS_EX_SYSCALL("seccomp_write_32"),
#endif
#if DEFSYS(security)
	STRESS_EX_SYSCALL("security"),
#endif
#if DEFSYS(select)
	STRESS_EX_SYSCALL("select"),
#endif
#if DEFSYS(semctl)
	STRESS_EX_SYSCALL("semctl"),
#endif
#if DEFSYS(semget)
	STRESS_EX_SYSCALL("semget"),
#endif
#if DEFSYS(semop)
	STRESS_EX_SYSCALL("semop"),
#endif
#if DEFSYS(semtimedop)
	STRESS_EX_SYSCALL("semtimedop"),
#endif
#if DEFSYS(semtimedop_time64)
	STRESS_EX_SYSCALL("semtimedop_time64"),
#endif
/*
 *  The following are not system calls, ignored for now
 */
#if 0
#if DEFSYS(sem_destroy)
	STRESS_EX_SYSCALL("sem_destroy"),
#endif
#if DEFSYS(sem_init)
	STRESS_EX_SYSCALL("sem_init"),
#endif
#if DEFSYS(sem_post)
	STRESS_EX_SYSCALL("sem_post"),
#endif
#if DEFSYS(sem_wait)
	STRESS_EX_SYSCALL("sem_wait"),
#endif
#if DEFSYS(sem_trywait)
	STRESS_EX_SYSCALL("sem_trywait"),
#endif
#if DEFSYS(sem_timedwait)
	STRESS_EX_SYSCALL("sem_timedwait"),
#endif
#endif
#if DEFSYS(send)
	STRESS_EX_SYSCALL("send"),
#endif
#if DEFSYS(sendfile)
	STRESS_EX_SYSCALL("sendfile"),
#endif
#if DEFSYS(sendfile64)
	STRESS_EX_SYSCALL("sendfile64"),
#endif
#if DEFSYS(sendmmsg)
	STRESS_EX_SYSCALL("sendmmsg"),
#endif
#if DEFSYS(sendmsg)
	STRESS_EX_SYSCALL("sendmsg"),
#endif
#if DEFSYS(sendto)
	STRESS_EX_SYSCALL("sendto"),
#endif
#if DEFSYS(set_mempolicy)
	STRESS_EX_SYSCALL("set_mempolicy"),
#endif
#if DEFSYS(set_mempolicy_home_node)
	STRESS_EX_SYSCALL("set_mempolicy_home_node"),
#endif
#if DEFSYS(set_robust_list)
	STRESS_EX_SYSCALL("set_robust_list"),
#endif
#if DEFSYS(set_thread_area)
	STRESS_EX_SYSCALL("set_thread_area"),
#endif
#if DEFSYS(set_tid_address)
	STRESS_EX_SYSCALL("set_tid_address"),
#endif
#if DEFSYS(set_tls)
	STRESS_EX_SYSCALL("set_tls"),
#endif
#if DEFSYS(setdomainname)
	STRESS_EX_SYSCALL("setdomainname"),
#endif
#if DEFSYS(setfsgid)
	STRESS_EX_SYSCALL("setfsgid"),
#endif
#if DEFSYS(setfsgid32)
	STRESS_EX_SYSCALL("setfsgid32"),
#endif
#if DEFSYS(setfsuid)
	STRESS_EX_SYSCALL("setfsuid"),
#endif
#if DEFSYS(setfsuid32)
	STRESS_EX_SYSCALL("setfsuid32"),
#endif
#if DEFSYS(setgid)
	STRESS_EX_SYSCALL("setgid"),
#endif
#if DEFSYS(setgid32)
	STRESS_EX_SYSCALL("setgid32"),
#endif
#if DEFSYS(setgroups)
	STRESS_EX_SYSCALL("setgroups"),
#endif
#if DEFSYS(setgroups32)
	STRESS_EX_SYSCALL("setgroups32"),
#endif
#if DEFSYS(sethae)
	/* ALPHA only */
	STRESS_EX_SYSCALL("sethae"),
#endif
#if DEFSYS(sethostname)
	STRESS_EX_SYSCALL("sethostname"),
#endif
#if DEFSYS(setitimer)
	STRESS_EX_SYSCALL("setitimer"),
#endif
#if DEFSYS(setmntent)
	STRESS_EX_SYSCALL("setmntent"),
#endif
#if DEFSYS(setns)
	STRESS_EX_SYSCALL("setns"),
#endif
#if DEFSYS(setpgid)
	STRESS_EX_SYSCALL("setpgid"),
#endif
#if DEFSYS(setpgrp)
	/* ALPHA, alternative to setpgid */
	STRESS_EX_SYSCALL("setpgrp"),
#endif
#if DEFSYS(setpriority)
	STRESS_EX_SYSCALL("setpriority"),
#endif
#if DEFSYS(setregid)
	STRESS_EX_SYSCALL("setregid"),
#endif
#if DEFSYS(setregid32)
	STRESS_EX_SYSCALL("setregid32"),
#endif
#if DEFSYS(setresgid)
	STRESS_EX_SYSCALL("setresgid"),
#endif
#if DEFSYS(setresgid32)
	STRESS_EX_SYSCALL("setresgid32"),
#endif
#if DEFSYS(setresuid)
	STRESS_EX_SYSCALL("setresuid"),
#endif
#if DEFSYS(setresuid32)
	STRESS_EX_SYSCALL("setresuid32"),
#endif
#if DEFSYS(setreuid)
	STRESS_EX_SYSCALL("setreuid"),
#endif
#if DEFSYS(setreuid32)
	STRESS_EX_SYSCALL("setreuid32"),
#endif
#if DEFSYS(setrlimit)
	STRESS_EX_SYSCALL("setrlimit"),
#endif
#if DEFSYS(setsid)
	STRESS_EX_SYSCALL("setsid"),
#endif
#if DEFSYS(setsockopt)
	STRESS_EX_SYSCALL("setsockopt"),
#endif
#if DEFSYS(settimeofday)
	STRESS_EX_SYSCALL("settimeofday"),
#endif
#if DEFSYS(setuid)
	STRESS_EX_SYSCALL("setuid"),
#endif
#if DEFSYS(setuid32)
	STRESS_EX_SYSCALL("setuid32"),
#endif
#if DEFSYS(setxattr)
	STRESS_EX_SYSCALL("setxattr"),
#endif
#if DEFSYS(sgetmask)
	STRESS_EX_SYSCALL("sgetmask"),
#endif
#if DEFSYS(shmat)
	STRESS_EX_SYSCALL("shmat"),
#endif
#if DEFSYS(shmctl)
	STRESS_EX_SYSCALL("shmctl"),
#endif
#if DEFSYS(shmdt)
	STRESS_EX_SYSCALL("shmdt"),
#endif
#if DEFSYS(shmget)
	STRESS_EX_SYSCALL("shmget"),
#endif
#if DEFSYS(shutdown)
	STRESS_EX_SYSCALL("shutdown"),
#endif
#if DEFSYS(sigaction)
	STRESS_EX_SYSCALL("sigaction"),
#endif
#if DEFSYS(sigaltstack)
	STRESS_EX_SYSCALL("sigaltstack"),
#endif
#if DEFSYS(signal)
	STRESS_EX_SYSCALL("signal"),
#endif
#if DEFSYS(signalfd)
	STRESS_EX_SYSCALL("signalfd"),
#endif
#if DEFSYS(signalfd4)
	STRESS_EX_SYSCALL("signalfd4"),
#endif
#if DEFSYS(sigpending)
	STRESS_EX_SYSCALL("sigpending"),
#endif
#if DEFSYS(sigprocmask)
	STRESS_EX_SYSCALL("sigprocmask"),
#endif
#if DEFSYS(sigreturn)
	STRESS_EX_SYSCALL("sigreturn"),
#endif
#if DEFSYS(sigsuspend)
	STRESS_EX_SYSCALL("sigsuspend"),
#endif
#if DEFSYS(sigtimedwait)
	STRESS_EX_SYSCALL("sigtimedwait"),
#endif
#if DEFSYS(sigwaitinfo)
	STRESS_EX_SYSCALL("sigwaitinfo"),
#endif
#if DEFSYS(socket)
	STRESS_EX_SYSCALL("socket"),
#endif
#if DEFSYS(socketcall)
	STRESS_EX_SYSCALL("socketcall"),
#endif
#if DEFSYS(socketpair)
	STRESS_EX_SYSCALL("socketpair"),
#endif
#if DEFSYS(spill)
	/* Xtensa only */
#endif
#if DEFSYS(splice)
	STRESS_EX_SYSCALL("splice"),
#endif
#if DEFSYS(spu_create)
	/* PowerPC/PowerPC64 only */
	STRESS_EX_SYSCALL("spu_create"),
#endif
#if DEFSYS(spu_run)
	/* PowerPC/PowerPC64 only */
	STRESS_EX_SYSCALL("spu_run"),
#endif
#if DEFSYS(sram_alloc)
	/* Blackfin, remove 4.17 */
#endif
#if DEFSYS(sram_free)
	/* Blackfin, remove 4.17 */
#endif
#if DEFSYS(ssetmask)
	STRESS_EX_SYSCALL("ssetmask"),
#endif
#if DEFSYS(stat)
	STRESS_EX_SYSCALL("stat"),
#endif
#if DEFSYS(stat64)
	STRESS_EX_SYSCALL("stat64"),
#endif
#if DEFSYS(statfs)
	STRESS_EX_SYSCALL("statfs"),
#endif
#if DEFSYS(statfs64)
	STRESS_EX_SYSCALL("statfs64"),
#endif
#if DEFSYS(statx)
	STRESS_EX_SYSCALL("statx"),
#endif
#if DEFSYS(stime)
	STRESS_EX_SYSCALL("stime"),
#endif
#if DEFSYS(subpage_prot)
	/* PowerPC/PowerPC64 only */
	STRESS_EX_SYSCALL("subpage_prot"),
#endif
#if DEFSYS(swapcontext)
	/* PowerPC/PowerPC64 only */
	STRESS_EX_SYSCALL("swapcontext"),
#endif
#if DEFSYS(swapon)
	STRESS_EX_SYSCALL("swapon"),
#endif
#if DEFSYS(swapoff)
	STRESS_EX_SYSCALL("swapoff"),
#endif
#if DEFSYS(switch_endian)
	/* PowerPC/PowerPC64 only */
	STRESS_EX_SYSCALL("switch_endian"),
#endif
#if DEFSYS(symlink)
	STRESS_EX_SYSCALL("symlink"),
#endif
#if DEFSYS(symlinkat)
	STRESS_EX_SYSCALL("symlinkat"),
#endif
#if DEFSYS(sync)
	STRESS_EX_SYSCALL("sync"),
#endif
#if DEFSYS(sync_file_range)
	STRESS_EX_SYSCALL("sync_file_range"),
#endif
#if DEFSYS(sync_file_range2)
	STRESS_EX_SYSCALL("sync_file_range2"),
#endif
#if DEFSYS(syncfs)
	STRESS_EX_SYSCALL("syncfs"),
#endif
#if DEFSYS(sys_debug_setcontext)
	/* PowerPC/PowerPC64 only */
	STRESS_EX_SYSCALL("sys_debug_setcontext"),
#endif
#if DEFSYS(sysctl)
	STRESS_EX_SYSCALL("sysctl"),
#endif
#if DEFSYS(sysfs)
	STRESS_EX_SYSCALL("sysfs"),
#endif
#if DEFSYS(sysinfo)
	STRESS_EX_SYSCALL("sysinfo"),
#endif
#if DEFSYS(syslog)
	STRESS_EX_SYSCALL("syslog"),
#endif
#if DEFSYS(sysmips)
	/* MIPS ABI */
	STRESS_EX_SYSCALL("sysmips"),
#endif
#if DEFSYS(tee)
	STRESS_EX_SYSCALL("tee"),
#endif
#if DEFSYS(tgkill)
	STRESS_EX_SYSCALL("tgkill"),
#endif
#if DEFSYS(time)
	STRESS_EX_SYSCALL("time"),
#endif
#if DEFSYS(timer_create)
	STRESS_EX_SYSCALL("timer_create"),
#endif
#if DEFSYS(timer_delete)
	STRESS_EX_SYSCALL("timer_delete"),
#endif
#if DEFSYS(timer_getoverrun)
	STRESS_EX_SYSCALL("timer_getoverrun"),
#endif
#if DEFSYS(timer_gettime)
	STRESS_EX_SYSCALL("timer_gettime"),
#endif
#if DEFSYS(timer_gettime64)
	STRESS_EX_SYSCALL("timer_gettime64"),
#endif
#if DEFSYS(timer_settime)
	STRESS_EX_SYSCALL("timer_settime"),
#endif
#if DEFSYS(timer_settime64)
	STRESS_EX_SYSCALL("timer_settime64"),
#endif
#if DEFSYS(timerfd_create)
	STRESS_EX_SYSCALL("timerfd_create"),
#endif
#if DEFSYS(timerfd_gettime)
	STRESS_EX_SYSCALL("timerfd_gettime"),
#endif
#if DEFSYS(timerfd_gettime64)
	STRESS_EX_SYSCALL("timerfd_gettime64"),
#endif
#if DEFSYS(timerfd_settime)
	STRESS_EX_SYSCALL("timerfd_settime"),
#endif
#if DEFSYS(timerfd_settime64)
	STRESS_EX_SYSCALL("timerfd_settime64"),
#endif
#if DEFSYS(times)
	STRESS_EX_SYSCALL("times"),
#endif
#if DEFSYS(tkill)
	STRESS_EX_SYSCALL("tkill"),
#endif
#if DEFSYS(truncate)
	STRESS_EX_SYSCALL("truncate"),
#endif
#if DEFSYS(truncate64)
	STRESS_EX_SYSCALL("truncate64"),
#endif
#if DEFSYS(tuxcall)
	STRESS_EX_SYSCALL("tuxcall"),
#endif
#if DEFSYS(ugetrlimit)
	STRESS_EX_SYSCALL("ugetrlimit"),
#endif
#if DEFSYS(ulimit)
	STRESS_EX_SYSCALL("ulimit"),
#endif
#if DEFSYS(umask)
	STRESS_EX_SYSCALL("umask"),
#endif
#if DEFSYS(umount)
	STRESS_EX_SYSCALL("umount"),
#endif
#if DEFSYS(umount2)
	STRESS_EX_SYSCALL("umount2"),
#endif
#if DEFSYS(uname)
	STRESS_EX_SYSCALL("uname"),
#endif
#if DEFSYS(unlink)
	STRESS_EX_SYSCALL("unlink"),
#endif
#if DEFSYS(unlinkat)
	STRESS_EX_SYSCALL("unlinkat"),
#endif
#if DEFSYS(unshare)
	STRESS_EX_SYSCALL("unshare"),
#endif
#if DEFSYS(uselib)
	STRESS_EX_SYSCALL("uselib"),
#endif
#if DEFSYS(userfaultfd)
	STRESS_EX_SYSCALL("userfaultfd"),
#endif
#if DEFSYS(usr26)
	STRESS_EX_SYSCALL("usr26"),
#endif
#if DEFSYS(usr32)
	STRESS_EX_SYSCALL("usr32"),
#endif
#if DEFSYS(ustat)
	STRESS_EX_SYSCALL("ustat"),
#endif
#if DEFSYS(utime)
	STRESS_EX_SYSCALL("utime"),
#endif
#if DEFSYS(utimensat)
	STRESS_EX_SYSCALL("utimensat"),
#endif
#if DEFSYS(utimensat_time64)
	STRESS_EX_SYSCALL("utimensat_time64"),
#endif
#if DEFSYS(utimes)
	STRESS_EX_SYSCALL("utimes"),
#endif
#if DEFSYS(utrap_install)
	/* SPARC64 */
	STRESS_EX_SYSCALL("utrap_install"),
#endif
#if DEFSYS(vm86old)
	/* x86 */
	STRESS_EX_SYSCALL("vm86old"),
#endif
#if DEFSYS(vm86)
	/* x86 */
	STRESS_EX_SYSCALL("vm86"),
#endif
#if DEFSYS(vmsplice)
	STRESS_EX_SYSCALL("vmsplice"),
#endif
#if DEFSYS(vserver)
	STRESS_EX_SYSCALL("verver"),
#endif
#if DEFSYS(wait)
	STRESS_EX_SYSCALL("wait"),
#endif
#if DEFSYS(wait3)
	STRESS_EX_SYSCALL("wait3"),
#endif
#if DEFSYS(wait4)
	STRESS_EX_SYSCALL("wait4"),
#endif
#if DEFSYS(waitid)
	STRESS_EX_SYSCALL("waitid"),
#endif
#if DEFSYS(waitpid)
	STRESS_EX_SYSCALL("waitpid"),
#endif
#if DEFSYS(write)
	STRESS_EX_SYSCALL("write"),
#endif
#if DEFSYS(writev)
	STRESS_EX_SYSCALL("writev"),
#endif
#if DEFSYS(xtensa)
	/* xtensa only */
	STRESS_EX_SYSCALL("xtensa"),
#endif
	STRESS_EX_END,
};

const stressor_info_t stress_sysinval_info = {
	.stressor = stress_sysinval,
	.classifier = CLASS_OS | CLASS_PATHOLOGICAL,
	.help = help,
	.exercises = exercises,
};
#else
const stressor_info_t stress_sysinval_info = {
	.stressor = stress_unimplemented,
	.classifier = CLASS_OS | CLASS_PATHOLOGICAL,
	.help = help,
	.unimplemented_reason = "built without syscall.h, syscall() or system is GNU/HURD or OS X"
};
#endif
