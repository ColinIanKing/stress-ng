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
#include "core-killpid.h"
#include "core-out-of-memory.h"
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
#define ARG_PTR_WR		0x20000000UL	/* kernel writes data to ptr */
#define ARG_ACCESS_MODE		0x40000000UL	/* faccess modes */
#define ARG_MISC		0x80000000UL

/*
 *  misc system call args
 */
#define ARG_ADD_KEY_TYPES	0x00000001UL | ARG_MISC
#define ARG_ADD_KEY_DESCRS	0x00000002UL | ARG_MISC
#define ARG_BPF_CMDS		0x00000003UL | ARG_MISC
#define ARG_BPF_LEN		0x00000004UL | ARG_MISC

#define ARG_VALUE(x, v)		{ (x), SIZEOF_ARRAY(v), (unsigned long int *)v }
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
	unsigned long int arg_bitmasks[6];	/* semantic info about each argument */
} stress_syscall_arg_t;

/*
 *  argument semantic information, unique argument types
 *  have one of these records to represent the different
 *  invalid argument values. Keep these values as short
 *  as possible as each new value increases the number of
 *  permutations
 */
typedef struct {
	unsigned long int bitmask;	/* bitmask representing arg type */
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
	unsigned long int args[6];	/* arguments */
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
static sigjmp_buf jmpbuf;

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

static uint8_t *small_ptr;
static uint8_t *small_ptr_wr;
static uint8_t *page_ptr;
static uint8_t *page_ptr_wr;

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
	{ SYS(bfin_spinlock, 1, { ARG_PTR, 0, 0, 0, 0, 0 } },
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
	{ SYS(brk), 1, { ARG_PTR_WR | ARG_BRK_ADDR, 0, 0, 0, 0, 0 } },
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
	{ SYS(capget), 2, { ARG_INT, ARG_PTR_WR, 0, 0, 0, 0 } },
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
	{ SYS(clock_getres), 2, { ARG_CLOCKID_T, ARG_PTR_WR, 0, 0, 0, 0 } },
#endif
#if DEFSYS(clock_getres_time64)
	{ SYS(clock_getres_time64), 2, { ARG_CLOCKID_T, ARG_PTR, 0, 0, 0, 0 } },
	{ SYS(clock_getres_time64), 2, { ARG_CLOCKID_T, ARG_PTR_WR, 0, 0, 0, 0 } },
#endif
#if DEFSYS(clock_gettime)
	{ SYS(clock_gettime), 2, { ARG_CLOCKID_T, ARG_PTR, 0, 0, 0, 0 } },
	{ SYS(clock_gettime), 2, { ARG_CLOCKID_T, ARG_PTR_WR, 0, 0, 0, 0 } },
#endif
#if DEFSYS(clock_gettime64)
	{ SYS(clock_gettime64), 2, { ARG_CLOCKID_T, ARG_PTR, 0, 0, 0, 0 } },
	{ SYS(clock_gettime64), 2, { ARG_CLOCKID_T, ARG_PTR_WR, 0, 0, 0, 0 } },
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
	{ SYS(compat_read), 3, { ARG_FD, ARG_PTR_WR, ARG_LEN, 0, 0, 0 } },
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
	{ SYS(epoll_wait), 4, { ARG_FD, ARG_PTR_WR, ARG_INT, ARG_TIMEOUT, 0, 0 } },
#endif
#if DEFSYS(epoll_wait_old)
	{ SYS(epoll_wait_old), 3, { ARG_FD, ARG_PTR, ARG_INT, 0 , 0, 0 } },
	{ SYS(epoll_wait_old), 3, { ARG_FD, ARG_PTR_WR, ARG_INT, 0 , 0, 0 } },
#endif
#if DEFSYS(epoll_pwait)
	{ SYS(epoll_pwait), 5, { ARG_FD, ARG_PTR, ARG_INT, ARG_TIMEOUT, ARG_PTR, 0 } },
	{ SYS(epoll_pwait), 5, { ARG_FD, ARG_PTR, ARG_INT, ARG_TIMEOUT, ARG_PTR_WR, 0 } },
	{ SYS(epoll_pwait), 5, { ARG_FD, ARG_PTR_WR, ARG_INT, ARG_TIMEOUT, ARG_PTR_WR, 0 } },
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
	{ SYS(fgetxattr), 4, { ARG_FD, ARG_EMPTY_FILENAME, ARG_PTR_WR, ARG_LEN, 0, 0 } },
	{ SYS(fgetxattr), 4, { ARG_FD, ARG_DEVNULL_FILENAME, ARG_PTR_WR, ARG_LEN, 0, 0 } },
#endif
#if DEFSYS(finit_module)
	{ SYS(finit_module), 3, { ARG_PTR, ARG_LEN, ARG_PTR, 0, 0, 0 } },
#endif
#if DEFSYS(flistxattr)
	{ SYS(flistxattr), 3, { ARG_FD, ARG_PTR, ARG_LEN, 0, 0, 0 } },
	{ SYS(flistxattr), 3, { ARG_FD, ARG_PTR_WR, ARG_LEN, 0, 0, 0 } },
#endif
#if DEFSYS(flock)
	{ SYS(flock), 2, { ARG_FD, ARG_INT, 0, 0, 0, 0 } },
#endif
#if DEFSYS(fork)
	/* { SYS(fork), 0, { 0, 0, 0, 0, 0, 0 } }, */
#endif
#if DEFSYS(fp_udfiex_crtl)
	{ SYS(fp_udfiex_crtl), ARG_INT, ARG_INT, 0, 0, 0, 0 } },
#endif
#if DEFSYS(free_hugepages)
	{ SYS(free_hugepages), ARG_PTR, 0, 0, 0, 0, 0 } },
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
	{ SYS(fstat), 2, { ARG_FD, ARG_PTR_WR, 0, 0, 0, 0 } },
#endif
#if DEFSYS(fstat64)
	{ SYS(fstat64), 2, { ARG_FD, ARG_PTR, 0, 0, 0, 0 } },
	{ SYS(fstat64), 2, { ARG_FD, ARG_PTR_WR, 0, 0, 0, 0 } },
#endif
#if DEFSYS(fstatat)
	{ SYS(fstatat), 4, { ARG_DIRFD, ARG_EMPTY_FILENAME, ARG_PTR, ARG_FLAG, 0, 0 } },
	{ SYS(fstatat), 4, { ARG_DIRFD, ARG_EMPTY_FILENAME, ARG_PTR_WR, ARG_FLAG, 0, 0 } },
	{ SYS(fstatat), 4, { ARG_DIRFD, ARG_DEVNULL_FILENAME, ARG_PTR, ARG_FLAG, 0, 0 } },
	{ SYS(fstatat), 4, { ARG_DIRFD, ARG_DEVNULL_FILENAME, ARG_PTR_WR, ARG_FLAG, 0, 0 } },
#endif
#if DEFSYS(fstatat64)
	{ SYS(fstatat64), 4, { ARG_DIRFD, ARG_EMPTY_FILENAME, ARG_PTR, ARG_FLAG, 0, 0 } },
	{ SYS(fstatat64), 4, { ARG_DIRFD, ARG_EMPTY_FILENAME, ARG_PTR_WR, ARG_FLAG, 0, 0 } },
	{ SYS(fstatat64), 4, { ARG_DIRFD, ARG_DEVNULL_FILENAME, ARG_PTR, ARG_FLAG, 0, 0 } },
	{ SYS(fstatat64), 4, { ARG_DIRFD, ARG_DEVNULL_FILENAME, ARG_PTR_WR, ARG_FLAG, 0, 0 } },
#endif
#if DEFSYS(fstatfs)
	{ SYS(fstatfs), 2, { ARG_FD, ARG_PTR, 0, 0, 0, 0 } },
	{ SYS(fstatfs), 2, { ARG_FD, ARG_PTR_WR, 0, 0, 0, 0 } },
#endif
#if DEFSYS(fstatfs64)
	{ SYS(fstatfs64), 2, { ARG_FD, ARG_PTR, 0, 0, 0, 0 } },
	{ SYS(fstatfs64), 2, { ARG_FD, ARG_PTR_WR, 0, 0, 0, 0 } },
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
	{ SYS(get_kernel_syms), 1, { ARG_PTR_WR, 0, 0, 0, 0, 0 } },
#endif
#if DEFSYS(get_mempolicy)
	{ SYS(get_mempolicy), 5, { ARG_PTR, ARG_PTR, ARG_UINT, ARG_PTR, ARG_FLAG, 0 } },
	{ SYS(get_mempolicy), 5, { ARG_PTR, ARG_PTR, ARG_UINT, ARG_PTR_WR, ARG_FLAG, 0 } },
#endif
#if DEFSYS(get_robust_list)
	{ SYS(get_robust_list), 3, { ARG_PID, ARG_PTR, ARG_PTR, 0, 0, 0 } },
#endif
#if DEFSYS(get_thread_area)
	{ SYS(get_thread_area), 1, { ARG_PTR, 0, 0, 0, 0, 0 } },
	{ SYS(get_thread_area), 1, { ARG_PTR_WR, 0, 0, 0, 0, 0 } },
#endif
#if DEFSYS(get_tls)
	/* ARM OABI only */
	{ SYS(get_tls), 0, { 0, 0, 0, 0, 0, 0 } },
#endif
#if DEFSYS(getcpu)
	{ SYS(getcpu), 3, { ARG_NON_NULL_PTR, ARG_NON_NULL_PTR, ARG_PTR, 0, 0, 0 } },
	{ SYS(getcpu), 3, { ARG_PTR_WR, ARG_PTR_WR, ARG_PTR_WR, 0, 0, 0 } },
#endif
#if DEFSYS(getcwd)
	{ SYS(getcwd), 2, { ARG_PTR, ARG_LEN, 0, 0, 0, 0 } },
	{ SYS(getcwd), 2, { ARG_PTR_WR, ARG_LEN, 0, 0, 0, 0 } },
#endif
#if DEFSYS(getdtablesize)
	/* SPARC, removed in 2.6.26 */
#endif
#if DEFSYS(getdents)
	{ SYS(getdents), 3, { ARG_FD, ARG_PTR, ARG_LEN, 0, 0, 0 } },
	{ SYS(getdents), 3, { ARG_FD, ARG_PTR_WR, ARG_LEN, 0, 0, 0 } },
#endif
#if DEFSYS(getdents64)
	{ SYS(getdents64), 3, { ARG_FD, ARG_PTR, ARG_LEN, 0, 0, 0 } },
	{ SYS(getdents64), 3, { ARG_FD, ARG_PTR_WR, ARG_LEN, 0, 0, 0 } },
#endif
#if DEFSYS(getdomainname)
	{ SYS(getdomainname), 2, { ARG_PTR, ARG_LEN, 0, 0, 0, 0 } },
	{ SYS(getdomainname), 2, { ARG_PTR_WR, ARG_LEN, 0, 0, 0, 0 } },
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
	{ SYS(getgroups), 2, { ARG_INT, ARG_PTR_WR, 0, 0, 0, 0 } },
#endif
#if DEFSYS(getgroups32)
	{ SYS(getgroups32), 2, { ARG_INT, ARG_PTR, 0, 0, 0, 0 } },
	{ SYS(getgroups32), 2, { ARG_INT, ARG_PTR_WR, 0, 0, 0, 0 } },
#endif
#if DEFSYS(gethostname)
	{ SYS(gethostname), 2, { ARG_PTR, ARG_LEN, 0, 0, 0, 0 } },
	{ SYS(gethostname), 2, { ARG_PTR_WR, ARG_LEN, 0, 0, 0, 0 } },
#endif
#if DEFSYS(getitimer)
	{ SYS(getitimer), 2, { ARG_INT, ARG_PTR, 0, 0, 0, 0 } },
	{ SYS(getitimer), 2, { ARG_INT, ARG_PTR_WR, 0, 0, 0, 0 } },
#endif
#if DEFSYS(getpagesize)
	{ SYS(getpagesize), 0, { 0, 0, 0, 0, 0, 0 } },
#endif
#if DEFSYS(getpeername)
	{ SYS(getpeername), 3, { ARG_SOCKFD, ARG_PTR, ARG_PTR, 0, 0, 0 } },
	{ SYS(getpeername), 3, { ARG_SOCKFD, ARG_PTR_WR, ARG_PTR_WR, 0, 0, 0 } },
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
	{ SYS(getrandom), 3, { ARG_PTR_WR, ARG_INT, ARG_FLAG, 0, 0, 0 } },
#endif
#if DEFSYS(getresgid)
	{ SYS(getresgid), 3, { ARG_PTR, ARG_PTR, ARG_PTR, 0, 0, 0 } },
	{ SYS(getresgid), 3, { ARG_PTR_WR, ARG_PTR_WR, ARG_PTR_WR, 0, 0, 0 } },
#endif
#if DEFSYS(getresgid32)
	{ SYS(getresgid32), 3, { ARG_PTR, ARG_PTR, ARG_PTR, 0, 0, 0 } },
	{ SYS(getresgid32), 3, { ARG_PTR_WR, ARG_PTR_WR, ARG_PTR_WR, 0, 0, 0 } },
#endif
#if DEFSYS(getresuid)
	{ SYS(getresuid), 3, { ARG_PTR, ARG_PTR, ARG_PTR, 0, 0, 0 } },
	{ SYS(getresuid), 3, { ARG_PTR_WR, ARG_PTR_WR, ARG_PTR_WR, 0, 0, 0 } },
#endif
#if DEFSYS(getresuid32)
	{ SYS(getresuid32), 3, { ARG_PTR, ARG_PTR, ARG_PTR, 0, 0, 0 } },
	{ SYS(getresuid32), 3, { ARG_PTR_WR, ARG_PTR_WR, ARG_PTR_WR, 0, 0, 0 } },
#endif
#if DEFSYS(getrlimit)
	{ SYS(getrlimit), 2, { ARG_RND, ARG_PTR, 0, 0, 0, 0 } },
	{ SYS(getrlimit), 2, { ARG_INT, ARG_PTR, 0, 0, 0, 0 } },
	{ SYS(getrlimit), 2, { ARG_RND, ARG_PTR_WR, 0, 0, 0, 0 } },
	{ SYS(getrlimit), 2, { ARG_INT, ARG_PTR_WR, 0, 0, 0, 0 } },
#endif
#if DEFSYS(getrusage)
	{ SYS(getrusage), 2, { ARG_RND, ARG_PTR, 0, 0, 0, 0 } },
	{ SYS(getrusage), 2, { ARG_INT, ARG_PTR, 0, 0, 0, 0 } },
	{ SYS(getrusage), 2, { ARG_RND, ARG_PTR_WR, 0, 0, 0, 0 } },
	{ SYS(getrusage), 2, { ARG_INT, ARG_PTR_WR, 0, 0, 0, 0 } },
#endif
#if DEFSYS(getsid)
	{ SYS(getsid), 1, { ARG_PID, 0, 0, 0, 0, 0 } },
#endif
#if DEFSYS(getsockname)
	{ SYS(getsockname), 3, { ARG_SOCKFD, ARG_PTR | ARG_STRUCT_SOCKADDR, ARG_PTR, 0, 0, 0 } },
	{ SYS(getsockname), 3, { ARG_SOCKFD, ARG_PTR | ARG_STRUCT_SOCKADDR, ARG_PTR_WR, 0, 0, 0 } },
#endif
#if DEFSYS(getsockopt)
	{ SYS(getsockopt), 5, { ARG_SOCKFD, ARG_INT, ARG_INT, ARG_PTR, ARG_PTR, 0 } },
	{ SYS(getsockopt), 5, { ARG_SOCKFD, ARG_INT, ARG_INT, ARG_PTR_WR, ARG_PTR, 0 } },
	{ SYS(getsockopt), 5, { ARG_SOCKFD, ARG_INT, ARG_INT, ARG_PTR, ARG_PTR_WR, 0 } },
	{ SYS(getsockopt), 5, { ARG_SOCKFD, ARG_INT, ARG_INT, ARG_PTR_WR, ARG_PTR_WR, 0 } },
#endif
#if DEFSYS(gettid)
	{ SYS(gettid), 0, { 0, 0, 0, 0, 0, 0 } },
#endif
#if DEFSYS(gettimeofday)
	{ SYS(gettimeofday), 2, { ARG_NON_NULL_PTR, ARG_NON_NULL_PTR, 0, 0, 0, 0 } },
	{ SYS(gettimeofday), 2, { ARG_PTR_WR, ARG_NON_NULL_PTR, 0, 0, 0, 0 } },
	{ SYS(gettimeofday), 2, { ARG_NON_NULL_PTR, ARG_PTR_WR, 0, 0, 0, 0 } },
	{ SYS(gettimeofday), 2, { ARG_PTR_WR, ARG_PTR_WR, 0, 0, 0, 0 } },
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
	{ SYS(getunwind), 2, { ARG_PTR_WR, ARG_LEN, 0, 0, 0, 0 } },
#endif
#if DEFSYS(getxattr)
	{ SYS(getxattr), 4, { ARG_EMPTY_FILENAME, ARG_PTR, ARG_PTR, ARG_LEN, 0, 0 } },
	{ SYS(getxattr), 4, { ARG_DEVNULL_FILENAME, ARG_PTR, ARG_PTR, ARG_LEN, 0, 0 } },
	{ SYS(getxattr), 4, { ARG_EMPTY_FILENAME, ARG_PTR, ARG_PTR_WR, ARG_LEN, 0, 0 } },
	{ SYS(getxattr), 4, { ARG_DEVNULL_FILENAME, ARG_PTR, ARG_PTR_WR, ARG_LEN, 0, 0 } },
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
	{ SYS(lgetxattr), 4, { ARG_EMPTY_FILENAME, ARG_PTR, ARG_PTR_WR, ARG_LEN, 0, 0 } },
	{ SYS(lgetxattr), 4, { ARG_DEVNULL_FILENAME, ARG_PTR, ARG_PTR_WR, ARG_LEN, 0, 0 } },
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
	{ SYS(listxattr), 3, { ARG_EMPTY_FILENAME, ARG_PTR_WR, ARG_LEN, 0, 0, 0 } },
	{ SYS(listxattr), 3, { ARG_DEVNULL_FILENAME, ARG_PTR, ARG_LEN, 0, 0, 0 } },
	{ SYS(listxattr), 3, { ARG_DEVNULL_FILENAME, ARG_PTR_WR, ARG_LEN, 0, 0, 0 } },
#endif
#if DEFSYS(llistxattr)
	{ SYS(llistxattr), 3, { ARG_EMPTY_FILENAME, ARG_PTR_WR, ARG_LEN, 0, 0, 0 } },
	{ SYS(llistxattr), 3, { ARG_EMPTY_FILENAME, ARG_PTR, ARG_LEN, 0, 0, 0 } },
	{ SYS(llistxattr), 3, { ARG_DEVNULL_FILENAME, ARG_PTR_WR, ARG_LEN, 0, 0, 0 } },
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
	{ SYS(lookup_dcookie), 3, { ARG_UINT, ARG_PTR_WR, ARG_LEN, 0, 0, 0 } },
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
	{ SYS(lstat), 2, { ARG_EMPTY_FILENAME, ARG_PTR_WR, 0, 0, 0, 0 } },
	{ SYS(lstat), 2, { ARG_DEVNULL_FILENAME, ARG_PTR, 0, 0, 0, 0 } },
	{ SYS(lstat), 2, { ARG_DEVNULL_FILENAME, ARG_PTR_WR, 0, 0, 0, 0 } },
#endif
#if DEFSYS(lstat64)
	{ SYS(lstat64), 2, { ARG_EMPTY_FILENAME, ARG_PTR, 0, 0, 0, 0 } },
	{ SYS(lstat64), 2, { ARG_EMPTY_FILENAME, ARG_PTR_WR, 0, 0, 0, 0 } },
	{ SYS(lstat64), 2, { ARG_DEVNULL_FILENAME, ARG_PTR, 0, 0, 0, 0 } },
	{ SYS(lstat64), 2, { ARG_DEVNULL_FILENAME, ARG_PTR_WR, 0, 0, 0, 0 } },
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
	{ SYS(mincore), 3, { ARG_PTR, ARG_LEN, ARG_PTR_WR, 0, 0, 0 } },
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
	{ SYS(modify_ldt), 3, { ARG_INT, ARG_PTR_WR, ARG_LEN, 0, 0, 0 } },
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
	{ SYS(mq_receive), 4, { ARG_INT, ARG_PTR_WR, ARG_LEN, ARG_INT, 0, 0 } },
#endif
#if DEFSYS(mq_send)
	{ SYS(mq_send), 4, { ARG_INT, ARG_PTR, ARG_LEN, ARG_UINT, 0, 0 } },
#endif
#if DEFSYS(mq_timedreceive)
	{ SYS(mq_timedreceive), 4, { ARG_INT, ARG_PTR_WR, ARG_LEN, ARG_PTR, 0, 0 } },
#endif
#if DEFSYS(mq_timedreceive_time64)
	{ SYS(mq_timedreceive_time64), 4, { ARG_INT, ARG_PTR_WR, ARG_LEN, ARG_PTR, 0, 0 } },
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
	{ SYS(msgrcv), 5, { ARG_INT, ARG_PTR_WR, ARG_LEN, ARG_INT, ARG_INT, 0 } },
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
	{ SYS(name_to_handle_at), 5, { ARG_DIRFD, ARG_EMPTY_FILENAME, ARG_PTR_WR, ARG_PTR, ARG_FLAG } },
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
	{ SYS(newlstat), 2, { ARG_EMPTY_FILENAME, ARG_PTR_WR, 0, 0, 0, 0 } },
	{ SYS(newlstat), 2, { ARG_DEVNULL_FILENAME, ARG_PTR, 0, 0, 0, 0 } },
	{ SYS(newlstat), 2, { ARG_DEVNULL_FILENAME, ARG_PTR_WR, 0, 0, 0, 0 } },
#endif
#if DEFSYS(newstat)
	{ SYS(newstat), 2, { ARG_EMPTY_FILENAME, ARG_PTR, 0, 0, 0, 0 } },
	{ SYS(newstat), 2, { ARG_EMPTY_FILENAME, ARG_PTR_WR, 0, 0, 0, 0 } },
	{ SYS(newstat), 2, { ARG_DEVNULL_FILENAME, ARG_PTR, 0, 0, 0, 0 } },
	{ SYS(newstat), 2, { ARG_DEVNULL_FILENAME, ARG_PTR_WR, 0, 0, 0, 0 } },
#endif
#if DEFSYS(newuname)
	{ SYS(newuname), 1, { ARG_PTR, 0, 0, 0, 0, 0 } },
	{ SYS(newuname), 1, { ARG_PTR_WR, 0, 0, 0, 0, 0 } },
#endif
#if DEFSYS(nfsservctl)
	{ SYS(nfsservctl), 3, { ARG_INT, ARG_PTR, ARG_PTR, 0, 0, 0 } },
	{ SYS(nfsservctl), 3, { ARG_INT, ARG_PTR, ARG_PTR_WR, 0, 0, 0 } },
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
	{ SYS(old_getrlimit), 2, { ARG_RND, ARG_PTR_WR, 0, 0, 0, 0 } },
	{ SYS(old_getrlimit), 2, { ARG_INT, ARG_PTR_WR, 0, 0, 0, 0 } },
#endif
#if DEFSYS(oldlstat)
	{ SYS(oldlstat), 2, { ARG_FD, ARG_PTR, 0, 0, 0, 0 } },
#endif
#if DEFSYS(oldolduname)
	{ SYS(oldolduname), 1, { ARG_PTR, 0, 0, 0, 0, 0 } },
	{ SYS(oldolduname), 1, { ARG_PTR_WR, 0, 0, 0, 0, 0 } },
#endif
#if DEFSYS(oldstat)
	{ SYS(oldstat), 2, { ARG_EMPTY_FILENAME, ARG_PTR, 0, 0, 0, 0 } },
	{ SYS(oldstat), 2, { ARG_EMPTY_FILENAME, ARG_PTR_WR, 0, 0, 0, 0 } },
	{ SYS(oldstat), 2, { ARG_DEVNULL_FILENAME, ARG_PTR, 0, 0, 0, 0 } },
	{ SYS(oldstat), 2, { ARG_DEVNULL_FILENAME, ARG_PTR_WR, 0, 0, 0, 0 } },
#endif
#if DEFSYS(oldumount)
	{ SYS(oldumount), 1, { ARG_EMPTY_FILENAME, 0, 0, 0, 0, 0 } },
	{ SYS(oldumount), 1, { ARG_DEVNULL_FILENAME, 0, 0, 0, 0, 0 } },
#endif
#if DEFSYS(olduname)
	{ SYS(olduname), 1, { ARG_PTR, 0, 0, 0, 0, 0 } },
	{ SYS(olduname), 1, { ARG_PTR_WR, 0, 0, 0, 0, 0 } },
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
	/* { SYS(pause), 0, { 0, 0, 0, 0, 0, 0 } }, */
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
	{ SYS(pread), 4, { ARG_FD, ARG_PTR_WR, ARG_LEN, ARG_UINT, 0, 0 } },
#endif
#if DEFSYS(pread64)
	{ SYS(pread64), 4, { ARG_FD, ARG_PTR, ARG_LEN, ARG_UINT, 0, 0 } },
	{ SYS(pread64), 4, { ARG_FD, ARG_PTR_WR, ARG_LEN, ARG_UINT, 0, 0 } },
#endif
#if DEFSYS(preadv)
	{ SYS(preadv), 4, { ARG_FD, ARG_PTR, ARG_INT, ARG_UINT, 0, 0 } },
	{ SYS(preadv), 4, { ARG_FD, ARG_PTR_WR, ARG_INT, ARG_UINT, 0, 0 } },
#endif
#if DEFSYS(preadv2)
	{ SYS(preadv2), 4, { ARG_FD, ARG_PTR, ARG_INT, ARG_UINT, ARG_FLAG, 0 } },
	{ SYS(preadv2), 4, { ARG_FD, ARG_PTR_WR, ARG_INT, ARG_UINT, ARG_FLAG, 0 } },
#endif
#if DEFSYS(prlimit)
	{ SYS(prlimit), 2, { ARG_INT, ARG_PTR, ARG_PTR, 0, 0, 0 } },
	{ SYS(prlimit), 2, { ARG_INT, ARG_PTR_WR, ARG_PTR, 0, 0, 0 } },
	{ SYS(prlimit), 2, { ARG_INT, ARG_PTR, ARG_PTR_WR, 0, 0, 0 } },
	{ SYS(prlimit), 2, { ARG_INT, ARG_PTR_WR, ARG_PTR_WR, 0, 0, 0 } },
#endif
#if DEFSYS(prlimit64)
	{ SYS(prlimit64), 2, { ARG_INT, ARG_PTR, ARG_PTR, 0, 0, 0 } },
	{ SYS(prlimit64), 2, { ARG_INT, ARG_PTR_WR, ARG_PTR, 0, 0, 0 } },
	{ SYS(prlimit64), 2, { ARG_INT, ARG_PTR, ARG_PTR_WR, 0, 0, 0 } },
	{ SYS(prlimit64), 2, { ARG_INT, ARG_PTR_WR, ARG_PTR_WR, 0, 0, 0 } },
#endif
#if DEFSYS(process_madvise)
	{ SYS(process_madvise), 6, { ARG_INT, ARG_PID, ARG_PTR, ARG_LEN, ARG_INT, ARG_FLAG } },
#endif
#if DEFSYS(process_mrelease)
	{ SYS(process_mrelease), 2, { ARG_PID, ARG_INT, 0, 0, 0, 0 } },
#endif
#if DEFSYS(process_vm_readv)
	{ SYS(process_vm_readv), 6, { ARG_PID, ARG_PTR, ARG_UINT, ARG_PTR, ARG_UINT, ARG_UINT } },
	{ SYS(process_vm_readv), 6, { ARG_PID, ARG_PTR, ARG_UINT, ARG_PTR_WR, ARG_UINT, ARG_UINT } },
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
	{ SYS(query_module), 5, { ARG_PTR, ARG_INT, ARG_PTR_WR, ARG_LEN, ARG_PTR, 0 } },
	{ SYS(query_module), 5, { ARG_PTR, ARG_INT, ARG_PTR, ARG_LEN, ARG_PTR_WR, 0 } },
	{ SYS(query_module), 5, { ARG_PTR, ARG_INT, ARG_PTR_WR, ARG_LEN, ARG_PTR, 0 } },
#endif
#if DEFSYS(quotactl)
	{ SYS(quotactl), 4, { ARG_INT, ARG_PTR, ARG_INT, ARG_PTR, 0, 0 } },
#endif
#if DEFSYS(quotactl_fd)
	{ SYS(quotactl_fd), 4, { ARG_INT, ARG_INT, ARG_INT, ARG_PTR, 0, 0 } },
#endif
#if DEFSYS(read)
	{ SYS(read), 3, { ARG_FD, ARG_PTR, ARG_LEN, 0, 0, 0 } },
	{ SYS(read), 3, { ARG_FD, ARG_PTR_WR, ARG_LEN, 0, 0, 0 } },
#endif
#if DEFSYS(readahead)
	{ SYS(readahead), 3, { ARG_FD, ARG_UINT, ARG_LEN, 0, 0, 0 } },
#endif
#if DEFSYS(readdir)
	{ SYS(readdir), 3, { ARG_FD, ARG_PTR, ARG_LEN, 0, 0, 0 } },
	{ SYS(readdir), 3, { ARG_FD, ARG_PTR_WR, ARG_LEN, 0, 0, 0 } },
#endif
#if DEFSYS(readlink)
	{ SYS(readlink), 3, { ARG_EMPTY_FILENAME, ARG_PTR, ARG_LEN, 0, 0, 0 } },
	{ SYS(readlink), 3, { ARG_EMPTY_FILENAME, ARG_PTR_WR, ARG_LEN, 0, 0, 0 } },
	{ SYS(readlink), 3, { ARG_DEVNULL_FILENAME, ARG_PTR, ARG_LEN, 0, 0, 0 } },
	{ SYS(readlink), 3, { ARG_DEVNULL_FILENAME, ARG_PTR_WR, ARG_LEN, 0, 0, 0 } },
#endif
#if DEFSYS(readlinkat)
	{ SYS(readlinkat), 4, { ARG_DIRFD, ARG_EMPTY_FILENAME, ARG_PTR, ARG_LEN, 0, 0 } },
	{ SYS(readlinkat), 4, { ARG_DIRFD, ARG_EMPTY_FILENAME, ARG_PTR_WR, ARG_LEN, 0, 0 } },
	{ SYS(readlinkat), 4, { ARG_DIRFD, ARG_DEVNULL_FILENAME, ARG_PTR, ARG_LEN, 0, 0 } },
	{ SYS(readlinkat), 4, { ARG_DIRFD, ARG_DEVNULL_FILENAME, ARG_PTR_WR, ARG_LEN, 0, 0 } },
#endif
#if DEFSYS(readv)
	{ SYS(readv), 3, { ARG_FD, ARG_PTR, ARG_INT, 0, 0, 0 } },
	{ SYS(readv), 3, { ARG_FD, ARG_PTR_WR, ARG_INT, 0, 0, 0 } },
#endif
#if DEFSYS(reboot)
	/* { SYS(reboot), 3, { ARG_INT, ARG_INT, ARG_PTR, 0, 0, 0 } }, */
#endif
#if DEFSYS(recv)
	{ SYS(recv), 4, { ARG_SOCKFD, ARG_PTR, ARG_LEN, ARG_FLAG, 0, 0 } },
	{ SYS(recv), 4, { ARG_SOCKFD, ARG_PTR_WR, ARG_LEN, ARG_FLAG, 0, 0 } },
#endif
#if DEFSYS(recvfrom)
	{ SYS(recvfrom), 6, { ARG_SOCKFD, ARG_PTR, ARG_LEN, ARG_FLAG, ARG_PTR, ARG_PTR } },
	{ SYS(recvfrom), 6, { ARG_SOCKFD, ARG_PTR, ARG_LEN, ARG_FLAG, ARG_PTR, ARG_PTR } },
#endif
#if DEFSYS(recvmsg)
	{ SYS(recvmsg), 3, { ARG_SOCKFD, ARG_PTR, ARG_FLAG, 0, 0, 0 } },
	{ SYS(recvmsg), 3, { ARG_SOCKFD, ARG_PTR_WR, ARG_FLAG, 0, 0, 0 } },
#endif
#if DEFSYS(recvmmsg)
	{ SYS(recvmmsg), 5, { ARG_SOCKFD, ARG_PTR, ARG_LEN, ARG_FLAG, ARG_PTR, 0 } },
	{ SYS(recvmmsg), 5, { ARG_SOCKFD, ARG_PTR_WR, ARG_LEN, ARG_FLAG, ARG_PTR, 0 } },
#endif
#if DEFSYS(recvmmsg_time64)
	{ SYS(recvmmsg_time64), 5, { ARG_SOCKFD, ARG_PTR, ARG_LEN, ARG_FLAG, ARG_PTR, 0 } },
	{ SYS(recvmmsg_time64), 5, { ARG_SOCKFD, ARG_PTR_WR, ARG_LEN, ARG_FLAG, ARG_PTR, 0 } },
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
	{ SYS(sched_getaffinity), 3, { ARG_PID, ARG_LEN, ARG_PTR_WR, 0, 0, 0 } },
#endif
#if DEFSYS(sched_getattr)
	{ SYS(sched_getattr), 3, { ARG_PID, ARG_PTR, ARG_FLAG, 0, 0, 0 } },
	{ SYS(sched_getattr), 3, { ARG_PID, ARG_PTR_WR, ARG_FLAG, 0, 0, 0 } },
#endif
#if DEFSYS(sched_getparam)
	{ SYS(sched_getparam), 2, { ARG_PID, ARG_PTR, 0, 0, 0, 0 } },
	{ SYS(sched_getparam), 2, { ARG_PID, ARG_PTR_WR, 0, 0, 0, 0 } },
#endif
#if DEFSYS(sched_getscheduler)
	{ SYS(sched_getscheduler), 1, { ARG_PID, 0, 0, 0, 0, 0 } },
#endif
#if DEFSYS(sched_get_rr_interval)
	{ SYS(sched_get_rr_interval), 2, { ARG_PID, ARG_PTR, 0, 0, 0, 0 } },
	{ SYS(sched_get_rr_interval), 2, { ARG_PID, ARG_PTR_WR, 0, 0, 0, 0 } },
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
	{ SYS(seccomp_read), 3, { ARG_FD, ARG_PTR_WR, ARG_LEN, 0, 0, 0 } },
#endif
#if DEFSYS(seccomp_read_32)
	{ SYS(seccomp_read_32), 3, { ARG_FD, ARG_PTR, ARG_LEN, 0, 0, 0 } },
	{ SYS(seccomp_read_32), 3, { ARG_FD, ARG_PTR_WR, ARG_LEN, 0, 0, 0 } },
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
	{ SYS(stat), 2, { ARG_EMPTY_FILENAME, ARG_PTR_WR, 0, 0, 0, 0 } },
	{ SYS(stat), 2, { ARG_DEVNULL_FILENAME, ARG_PTR, 0, 0, 0, 0 } },
	{ SYS(stat), 2, { ARG_DEVNULL_FILENAME, ARG_PTR_WR, 0, 0, 0, 0 } },
#endif
#if DEFSYS(stat64)
	{ SYS(stat64), 2, { ARG_EMPTY_FILENAME, ARG_PTR, 0, 0, 0, 0 } },
	{ SYS(stat64), 2, { ARG_EMPTY_FILENAME, ARG_PTR_WR, 0, 0, 0, 0 } },
	{ SYS(stat64), 2, { ARG_DEVNULL_FILENAME, ARG_PTR, 0, 0, 0, 0 } },
	{ SYS(stat64), 2, { ARG_DEVNULL_FILENAME, ARG_PTR_WR, 0, 0, 0, 0 } },
#endif
#if DEFSYS(statfs)
	{ SYS(statfs), 2, { ARG_EMPTY_FILENAME, ARG_PTR, 0, 0, 0, 0 } },
	{ SYS(statfs), 2, { ARG_EMPTY_FILENAME, ARG_PTR_WR, 0, 0, 0, 0 } },
	{ SYS(statfs), 2, { ARG_DEVNULL_FILENAME, ARG_PTR, 0, 0, 0, 0 } },
	{ SYS(statfs), 2, { ARG_DEVNULL_FILENAME, ARG_PTR_WR, 0, 0, 0, 0 } },
#endif
#if DEFSYS(statfs64)
	{ SYS(statfs64), 2, { ARG_EMPTY_FILENAME, ARG_PTR, 0, 0, 0, 0 } },
	{ SYS(statfs64), 2, { ARG_EMPTY_FILENAME, ARG_PTR_WR, 0, 0, 0, 0 } },
	{ SYS(statfs64), 2, { ARG_DEVNULL_FILENAME, ARG_PTR, 0, 0, 0, 0 } },
	{ SYS(statfs64), 2, { ARG_DEVNULL_FILENAME, ARG_PTR_WR, 0, 0, 0, 0 } },
#endif
#if DEFSYS(statx)
	{ SYS(statx), 5, { ARG_DIRFD, ARG_EMPTY_FILENAME, ARG_FLAG, ARG_UINT, ARG_PTR, 0 } },
	{ SYS(statx), 5, { ARG_DIRFD, ARG_EMPTY_FILENAME, ARG_FLAG, ARG_UINT, ARG_PTR_WR, 0 } },
	{ SYS(statx), 5, { ARG_DIRFD, ARG_DEVNULL_FILENAME, ARG_FLAG, ARG_UINT, ARG_PTR, 0 } },
	{ SYS(statx), 5, { ARG_DIRFD, ARG_DEVNULL_FILENAME, ARG_FLAG, ARG_UINT, ARG_PTR_WR, 0 } },
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
	{ SYS(sysfs), 2, { ARG_INT, ARG_PTR_WR, 0, 0, 0, 0 } },
	{ SYS(sysfs), 3, { ARG_INT, ARG_UINT, ARG_PTR, 0, 0, 0 } },
	{ SYS(sysfs), 3, { ARG_INT, ARG_UINT, ARG_PTR_WR, 0, 0, 0 } },
	{ SYS(sysfs), 1, { ARG_INT, 0, 0, 0, 0, 0 } },
#endif
#if DEFSYS(sysinfo)
	{ SYS(sysinfo), 1, { ARG_PTR, 0, 0, 0, 0, 0 } },
	{ SYS(sysinfo), 1, { ARG_PTR_WR, 0, 0, 0, 0, 0 } },
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
	{ SYS(time), 1, { ARG_PTR_WR, 0, 0, 0, 0, 0 } },
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
	{ SYS(timer_gettime), 2, { ARG_UINT, ARG_PTR_WR, 0, 0, 0, 0 } },
#endif
#if DEFSYS(timer_gettime64)
	{ SYS(timer_gettime64), 2, { ARG_UINT, ARG_PTR, 0, 0, 0, 0 } },
	{ SYS(timer_gettime64), 2, { ARG_UINT, ARG_PTR_WR, 0, 0, 0, 0 } },
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
	{ SYS(timerfd_gettime), 2, { ARG_CLOCKID_T, ARG_PTR_WR, 0, 0, 0, 0 } },
#endif
#if DEFSYS(timerfd_gettime64)
	{ SYS(timerfd_gettime64), 2, { ARG_CLOCKID_T, ARG_PTR, 0, 0, 0, 0 } },
	{ SYS(timerfd_gettime64), 2, { ARG_CLOCKID_T, ARG_PTR_WR, 0, 0, 0, 0 } },
#endif
#if DEFSYS(timerfd_settime)
	{ SYS(timerfd_settime), 4, { ARG_FD, ARG_FLAG, ARG_PTR, ARG_PTR, 0, 0 } },
#endif
#if DEFSYS(timerfd_settime64)
	{ SYS(timerfd_settime64), 4, { ARG_FD, ARG_FLAG, ARG_PTR, ARG_PTR, 0, 0 } },
#endif
#if DEFSYS(times)
	{ SYS(times), 1, { ARG_PTR, 0, 0, 0, 0, 0 } },
	{ SYS(times), 1, { ARG_PTR_WR, 0, 0, 0, 0, 0 } },
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
	{ SYS(tuxcall), 2, { ARG_INT, ARG_PTR_WR, 0, 0, 0, 0 } },
#endif
#if DEFSYS(ugetrlimit)
	{ SYS(ugetrlimit), 2, { ARG_RND, ARG_PTR, 0, 0, 0, 0 } },
	{ SYS(ugetrlimit), 2, { ARG_INT, ARG_PTR, 0, 0, 0, 0 } },
	{ SYS(ugetrlimit), 2, { ARG_RND, ARG_PTR_WR, 0, 0, 0, 0 } },
	{ SYS(ugetrlimit), 2, { ARG_INT, ARG_PTR_WR, 0, 0, 0, 0 } },
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
	{ SYS(uname), 1, { ARG_PTR_WR, 0, 0, 0, 0, 0 } },
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
	{ SYS(usr32), 1, { ARH_INT, 0, 0, 0, 0, 0 } },
#endif
#if DEFSYS(ustat)
	{ SYS(ustat), 2, { ARG_UINT, ARG_PTR, 0, 0, 0, 0 } },
	{ SYS(ustat), 2, { ARG_UINT, ARG_PTR_WR, 0, 0, 0, 0 } },
#endif
#if DEFSYS(utime)
	{ SYS(utime), 2, { ARG_EMPTY_FILENAME, ARG_PTR, 0, 0, 0, 0 } },
	{ SYS(utime), 2, { ARG_EMPTY_FILENAME, ARG_PTR_WR, 0, 0, 0, 0 } },
#endif
#if DEFSYS(utimensat)
	{ SYS(utimensat), 4, { ARG_DIRFD, ARG_EMPTY_FILENAME, ARG_PTR, ARG_FLAG, 0, 0 } },
	{ SYS(utimensat), 4, { ARG_DIRFD, ARG_EMPTY_FILENAME, ARG_PTR_WR, ARG_FLAG, 0, 0 } },
#endif
#if DEFSYS(utimensat_time64)
	{ SYS(utimensat_time64), 4, { ARG_DIRFD, ARG_EMPTY_FILENAME, ARG_PTR, ARG_FLAG, 0, 0 } },
	{ SYS(utimensat_time64), 4, { ARG_DIRFD, ARG_EMPTY_FILENAME, ARG_PTR_WR, ARG_FLAG, 0, 0 } },
#endif
#if DEFSYS(utimes)
	{ SYS(utimes), 2, { ARG_EMPTY_FILENAME, ARG_PTR, 0, 0, 0, 0 } },
	{ SYS(utimes), 2, { ARG_EMPTY_FILENAME, ARG_PTR_WR, 0, 0, 0, 0 } },
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
	{ SYS(wait3), 3, { ARG_PTR_WR, ARG_INT, ARG_PTR, 0, 0, 0 } },
	{ SYS(wait3), 3, { ARG_PTR, ARG_INT, ARG_PTR_WR, 0, 0, 0 } },
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
	unsigned long int args[6];
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
	(unsigned long int)~(F_OK | R_OK | W_OK | X_OK)
};
static long int sockfds[] = {
	/* sockfd */ 0, 0, -1, INT_MAX, INT_MIN, ~(long int)0
};
static long int fds[] = {
	/* fd */ 0, -1, INT_MAX, INT_MIN, ~(long int)0
};
static long int dirfds[] = {
	-1, AT_FDCWD, INT_MIN, ~(long int)0
};
static long int clockids[] = {
	-1, INT_MAX, INT_MIN, ~(long int)0, SHR_UL(0xfe23ULL, 18)
};
static long int sockaddrs[] = {
	/*small_ptr*/ 0, /*page_ptr*/ 0, 0, -1, INT_MAX, INT_MIN
};
static unsigned long int brk_addrs[] = {
	0, (unsigned long int)-1, INT_MAX, (unsigned long int)INT_MIN,
	~(unsigned long int)0, 4096
};
static unsigned long int empty_filenames[] = {
	(unsigned long int)"", (unsigned long int)NULL
};
static unsigned long int zero_filenames[] = {
	(unsigned long int)"/dev/zero"
};
static unsigned long int null_filenames[] = {
	(unsigned long int)"/dev/null"
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
	0x10000000, 0x20000000, 0x40000000, 0x80000000
};
static unsigned long int lengths[] = {
	(unsigned long int)-1, (unsigned long int)-2,
	(unsigned long int)INT_MIN, INT_MAX,
	~(unsigned long int)0, -SHR_UL(1, 31)
};
static long int ints[] = {
	0, -1, -2, INT_MIN, INT_MAX, SHR_UL(0xff, 30), SHR_UL(1, 30),
	(long int)-SHR_UL(0xff, 30), (long int)-SHR_UL(1, 30)
};
static unsigned long int uints[] = {
	INT_MAX, SHR_UL(0xff, 30), -SHR_UL(0xff, 30), ~(unsigned long int)0
};
static unsigned long int func_ptrs[] = {
	(unsigned long int)func_exit
};
static unsigned long int ptrs[] = {
	/*small_ptr*/ 0, /*page_ptr*/ 0, 0, (unsigned long int)-1,
	INT_MAX, (unsigned long int)INT_MIN, (unsigned long int)~4096L
};
static unsigned long int ptrs_wr[] = {
	/*small_ptr_wr*/ 0, /*page_ptr_wr*/ 0, 0,
	(unsigned long int)-1, INT_MAX, (unsigned long int)INT_MIN,
	(unsigned long int)~4096L
};
static unsigned long int futex_ptrs[] = {
	/*small_ptr*/ 0, /*page_ptr*/ 0
};
static unsigned long int non_null_ptrs[] = {
	/*small_ptr*/ 0, /*page_ptr*/ 0, (unsigned long int)-1,
	INT_MAX, (unsigned long int)INT_MIN, (unsigned long int)~4096L
};
static long int socklens[] = {
	0, -1, INT_MAX, INT_MIN, 8192
};
static unsigned long int timeouts[] = {
	0
};
static pid_t pids[] = {
	INT_MIN, -1, INT_MAX, ~0
};
static gid_t gids[] = {
	(gid_t)~0L, INT_MAX
};
static uid_t uids[] = {
	(uid_t)~0L, INT_MAX
};

/*
 *  Misc per system-call args
 */
static char *add_key_types[] = { "key_ring" };
static char *add_key_descrs[] = { "." };
static unsigned long int bpf_cmds[] = {
	0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
	0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f,
	0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17,
	0x18, 0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f,
	0x20, 0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27,
	0x28, 0x29, 0x2a, 0x2b, 0x2c, 0x2d, 0x2e, 0x2f,
};
static int bpf_lengths[] = { 0, 16, 256, 1024, 4096, 65536, 1024 * 1024 };

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
	ARG_VALUE(ARG_PTR_WR, ptrs_wr),
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
	const unsigned long int args[6])
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

static void MLOCKED_TEXT stress_syscall_itimer_handler(int sig)
{
	(void)sig;

	if (do_jmp && current_context) {
		do_jmp = false;
		current_context->type = SYSCALL_TIMED_OUT;
		siglongjmp(jmpbuf, 1);
		stress_no_return();
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
	}
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
	const stress_syscall_arg_t *stress_syscall_arg,
	volatile bool *syscall_exercised)
{
	unsigned long int arg_bitmask = stress_syscall_arg->arg_bitmasks[arg_num];
	size_t i;
	unsigned long int *values = NULL;
	unsigned long int rnd_values[4];
	size_t num_values = 0;

	if (UNLIKELY(stress_time_now() > args->time_end))
		_exit(EXIT_SUCCESS);

	if (arg_num >= stress_syscall_arg->num_args) {
		int ret;
		const unsigned long int syscall_num = stress_syscall_arg->syscall;
		const unsigned long int hash = stress_syscall_hash(syscall_num, current_context->args);
		stress_syscall_arg_hash_t *h = hash_table->table[hash];
		struct itimerval it;

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

		ret = sigsetjmp(jmpbuf, 1);
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

		/*
		(void)printf("syscall: %s(%lx,%lx,%lx,%lx,%lx,%lx) -> %d\n",
			current_context->name,
			current_context->args[0],
			current_context->args[1],
			current_context->args[2],
			current_context->args[3],
			current_context->args[4],
			current_context->args[5], ret);
		*/

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
		return;
	}

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
		rnd_values[2] = (unsigned long int)small_ptr;
		rnd_values[3] = (unsigned long int)page_ptr;
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

	if (arg_bitmask & ARG_PTR_WR)
		(void)shim_memset(page_ptr_wr, 0, args->page_size);
	/*
	 *  This should not fail!
	 */
	if (UNLIKELY(!num_values)) {
		pr_dbg("%s: argument %d has bad bitmask %lx\n", args->name, arg_num, arg_bitmask);
		current_context->args[arg_num] = 0;
		return;
	}

	/*
	 *  And permute and call all the argument values for this
	 *  specific argument
	 */
	for (i = 0; i < num_values; i++) {
		current_context->args[arg_num] = values[i];
		syscall_permute(args, arg_num + 1, stress_syscall_arg, syscall_exercised);
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

	if (stress_drop_capabilities(args->name) < 0)
		return EXIT_NO_RESOURCE;

	pid = fork();
	if (pid < 0) {
		_exit(EXIT_NO_RESOURCE);
	} else if (pid == 0) {
		size_t i, n;
		size_t reorder[SYSCALL_ARGS_SIZE];

		/* We don't want bad ops clobbering this region */
		stress_set_stack_smash_check_flag(false);
		stress_shared_readonly();
		stress_process_dumpable(false);

		/* Drop all capabilities */
		if (stress_drop_capabilities(args->name) < 0) {
			_exit(EXIT_NO_RESOURCE);
		}
		for (i = 0; i < SIZEOF_ARRAY(sigs); i++) {
			if (UNLIKELY(stress_sighandler(args->name, sigs[i], stress_sig_handler_exit, NULL) < 0))
				_exit(EXIT_FAILURE);
		}

		if (UNLIKELY(stress_sighandler(args->name, SIGALRM, stress_syscall_itimer_handler, NULL) < 0))
			_exit(EXIT_FAILURE);

		stress_parent_died_alarm();
		(void)sched_settings_apply(true);
		stress_mwc_reseed();

		while (stress_continue_flag()) {
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
						register size_t j = (sz == 0) ? 0 : stress_mwc32modn(sz);

						tmp = reorder[i];
						reorder[i] = reorder[j];
						reorder[j] = tmp;
					}
				}
			}

			for (i = 0; LIKELY(stress_continue(args) && (i < SYSCALL_ARGS_SIZE)); i++) {
				const size_t j = reorder[i];

				(void)shim_memset(current_context->args, 0, sizeof(current_context->args));
				current_context->syscall = stress_syscall_args[j].syscall;
				current_context->idx = j;
				current_context->name = stress_syscall_args[j].name;

				/* Ignore too many crashes from this system call */
				if (current_context->crash_count[j] >= MAX_CRASHES)
					continue;
				syscall_permute(args, 0, &stress_syscall_args[j], &stress_syscall_exercised[j]);
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
	} while (stress_continue(args));

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
	size_t i;
	uint64_t syscalls_exercised, syscalls_unique, syscalls_crashed;
	const size_t page_size = args->page_size;
	const size_t current_context_size =
		(sizeof(*current_context) + page_size) & ~(page_size - 1);
	size_t small_ptr_size = page_size << 1;		/* cppcheck-suppress duplicateAssignExpression */
	size_t page_ptr_wr_size = page_size << 1;	/* cppcheck-suppress duplicateAssignExpression */
	char filename[PATH_MAX];
	const size_t stress_syscall_exercised_sz = SYSCALL_ARGS_SIZE * sizeof(*stress_syscall_exercised);
	bool syscall_exercised[SYSCALL_ARGS_SIZE];
	bool syscall_unique[SYSCALL_ARGS_SIZE];

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

	sockfds[0] = socket(AF_UNIX, SOCK_STREAM, 0);

	ret = stress_temp_dir_mk_args(args);
	if (ret < 0)
		return stress_exit_status(-ret);

	(void)stress_temp_filename_args(args,
		filename, sizeof(filename), stress_mwc32());
	fds[0] = open(filename, O_CREAT | O_RDWR, S_IRUSR | S_IWUSR);
	if (fds[0] < 0) {
		pr_fail("%s: open %s failed, errno=%d (%s)\n",
			args->name, filename, errno, strerror(errno));
		goto err_dir;
	}
	(void)shim_unlink(filename);

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
	stress_set_vma_anon_name(stress_syscall_exercised, stress_syscall_exercised_sz, "syscall-stats");

	hash_table = (stress_syscall_hash_table_t *)mmap(NULL,
			sizeof(*hash_table),
			PROT_READ | PROT_WRITE,
			MAP_SHARED | MAP_ANONYMOUS, -1, 0);
	if (hash_table == MAP_FAILED) {
		pr_fail("%s: mmap failed, errno=%d (%s)\n",
			args->name, errno, strerror(errno));
		goto tidy;
	}
	stress_set_vma_anon_name(hash_table, sizeof(*hash_table), "syscall-hash-table");

	current_context = (syscall_current_context_t*)
		mmap(NULL, current_context_size, PROT_READ | PROT_WRITE,
			MAP_SHARED | MAP_ANONYMOUS, -1, 0);
	if (current_context == MAP_FAILED) {
		pr_fail("%s: mmap failed, errno=%d (%s)\n",
			args->name, errno, strerror(errno));
		goto tidy;
	}
	stress_set_vma_anon_name(current_context, current_context_size, "syscall-context");

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

	small_ptr = (uint8_t *)mmap(NULL, small_ptr_size, PROT_READ | PROT_WRITE,
		MAP_SHARED | MAP_ANONYMOUS, -1, 0);
	if (small_ptr == MAP_FAILED) {
		pr_fail("%s: mmap failed, errno=%d (%s)\n",
			args->name, errno, strerror(errno));
		goto tidy;
	}
#if defined(HAVE_MPROTECT)
	stress_set_vma_anon_name(small_ptr, small_ptr_size, "syscall-small");
	(void)mprotect((void *)(small_ptr + page_size), page_size, PROT_NONE);
#else
	(void)munmap((void *)(small_ptr + page_size), page_size);
	small_ptr_size -= page_size;
#endif

	page_ptr = (uint8_t *)mmap(NULL, page_size, PROT_NONE,
		MAP_SHARED | MAP_ANONYMOUS, -1, 0);
	if (page_ptr == MAP_FAILED) {
		pr_fail("%s: mmap failed, errno=%d (%s)\n",
			args->name, errno, strerror(errno));
		goto tidy;
	}
	stress_set_vma_anon_name(page_ptr, page_size, "syscall-none");

	page_ptr_wr = (uint8_t *)mmap(NULL, page_ptr_wr_size, PROT_WRITE,
		MAP_SHARED | MAP_ANONYMOUS, -1, 0);
	if (page_ptr_wr == MAP_FAILED) {
		pr_fail("%s: mmap failed, errno=%d (%s)\n",
			args->name, errno, strerror(errno));
		goto tidy;
	}
	small_ptr_wr = page_ptr_wr + page_size - 1;
#if defined(HAVE_MPROTECT)
	(void)mprotect((void *)(page_ptr_wr + page_size), page_size, PROT_NONE);
	stress_set_vma_anon_name(page_ptr_wr, page_ptr_wr_size, "syscall-half-write");
#else
	(void)munmap((void *)(page_ptr_wr + page_size), page_size);
	page_ptr_wr_size -= page_size;
#endif

	sockaddrs[0] = (long int)(small_ptr + page_size - 1);
	sockaddrs[1] = (long int)page_ptr;
	ptrs[0] = (unsigned long int)(small_ptr + page_size -1);
	ptrs[1] = (unsigned long int)page_ptr;
	non_null_ptrs[0] = (unsigned long int)(small_ptr + page_size -1);
	non_null_ptrs[1] = (unsigned long int)page_ptr;
	futex_ptrs[0] = (unsigned long int)(small_ptr + page_size -1);
	futex_ptrs[1] = (unsigned long int)page_ptr;

	(void)shim_memset(current_context->crash_count, 0, sizeof(current_context->crash_count));

	stress_set_proc_state(args->name, STRESS_STATE_SYNC_WAIT);
	stress_sync_start_wait(args);
	stress_set_proc_state(args->name, STRESS_STATE_RUN);

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
		unsigned long int syscall_num = stress_syscall_args[i].syscall;
		size_t exercised = 0, unique = 0;
		size_t j;

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
	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);
	if (stress_syscall_exercised && (stress_syscall_exercised != MAP_FAILED))
		(void)munmap((void *)stress_syscall_exercised, stress_syscall_exercised_sz);
	if (hash_table && (hash_table != MAP_FAILED))
		(void)munmap((void *)hash_table, sizeof(*hash_table));
	if (page_ptr_wr && (page_ptr_wr != MAP_FAILED))
		(void)munmap((void *)page_ptr_wr, page_ptr_wr_size);
	if (page_ptr && (page_ptr != MAP_FAILED))
		(void)munmap((void *)page_ptr, page_size);
	if (small_ptr && (small_ptr != MAP_FAILED))
		(void)munmap((void *)small_ptr, small_ptr_size);
	if (current_context && (current_context != MAP_FAILED))
		(void)munmap((void *)current_context, current_context_size);
	if (sockfds[0] >= 0)
		(void)close((int)sockfds[0]);
	if (fds[0] >= 0)
		(void)close((int)fds[0]);

err_dir:
	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);
	(void)stress_temp_dir_rm_args(args);

	return rc;
}

const stressor_info_t stress_sysinval_info = {
	.stressor = stress_sysinval,
	.classifier = CLASS_OS | CLASS_PATHOLOGICAL,
	.help = help
};
#else
const stressor_info_t stress_sysinval_info = {
	.stressor = stress_unimplemented,
	.classifier = CLASS_OS | CLASS_PATHOLOGICAL,
	.help = help,
	.unimplemented_reason = "built without syscall.h, syscall() or system is GNU/HURD or OS X"
};
#endif
