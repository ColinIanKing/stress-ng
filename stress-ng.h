/*
 * Copyright (C) 2013-2015 Canonical, Ltd.
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

#ifndef __STRESS_NG_H__
#define __STRESS_NG_H__

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <inttypes.h>
#include <semaphore.h>
#include <sys/time.h>
#include <sys/times.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/mman.h>
#include <fcntl.h>

#define _GNU_SOURCE
/* GNU HURD */
#ifndef PATH_MAX
#define PATH_MAX 		(4096)		/* Some systems don't define this */
#endif

#define STRESS_FD_MAX		(65536)		/* Max fds if we can't figure it out */
#define STRESS_PROCS_MAX	(1024)		/* Max number of processes per stressor */

#ifndef PIPE_BUF
#define PIPE_BUF		(512)		/* PIPE I/O buffer size */
#endif
#define SOCKET_BUF		(8192)		/* Socket I/O buffer size */
#define UDP_BUF			(1024)		/* UDP I/O buffer size */

/* Option bit masks */
#define OPT_FLAGS_AFFINITY_RAND	0x00000001	/* Change affinity randomly */
#define OPT_FLAGS_DRY_RUN	0x00000002	/* Don't actually run */
#define OPT_FLAGS_METRICS	0x00000004	/* Dump metrics at end */
#define OPT_FLAGS_VM_KEEP	0x00000008	/* Don't keep re-allocating */
#define OPT_FLAGS_RANDOM	0x00000010	/* Randomize */
#define OPT_FLAGS_SET		0x00000020	/* Set if user specifies stress procs */
#define OPT_FLAGS_KEEP_NAME	0x00000040	/* Keep stress names to stress-ng */
#define OPT_FLAGS_UTIME_FSYNC	0x00000080	/* fsync after utime modification */
#define OPT_FLAGS_METRICS_BRIEF	0x00000100	/* dump brief metrics */
#define OPT_FLAGS_VERIFY	0x00000200	/* verify mode */
#define OPT_FLAGS_MMAP_MADVISE	0x00000400	/* enable random madvise settings */
#define OPT_FLAGS_MMAP_MINCORE	0x00000800	/* mincore force pages into mem */
#define OPT_FLAGS_TIMES		0x00001000	/* user/system time summary */
#define OPT_FLAGS_CACHE_FLUSH	0x00004000	/* cache flush */
#define OPT_FLAGS_CACHE_FENCE	0x00008000	/* cache fence */
#define OPT_FLAGS_CACHE_MASK	(OPT_FLAGS_CACHE_FLUSH | OPT_FLAGS_CACHE_FENCE)
#define OPT_FLAGS_MMAP_FILE	0x00010000	/* mmap onto a file */
#define OPT_FLAGS_MMAP_ASYNC	0x00020000	/* mmap onto a file */
#define OPT_FLAGS_MMAP_MPROTECT	0x00040000	/* mmap mprotect enabled */
#define OPT_FLAGS_LOCKF_NONBLK	0x00080000	/* Non-blocking lockf */
#define OPT_FLAGS_MINCORE_RAND	0x00100000	/* mincore randomize */
#define OPT_FLAGS_BRK_NOTOUCH	0x00200000	/* brk, don't touch page */
#define OPT_FLAGS_HDD_SYNC	0x00400000	/* HDD O_SYNC */
#define OPT_FLAGS_HDD_DSYNC	0x00800000	/* HDD O_DYNC */
#define OPT_FLAGS_HDD_DIRECT	0x01000000	/* HDD O_DIRECT */
#define OPT_FLAGS_HDD_NOATIME	0x02000000	/* HDD O_NOATIME */

/* Stressor classes */
#define CLASS_CPU		0x00000001	/* CPU only */
#define CLASS_MEMORY		0x00000002	/* Memory thrashers */
#define CLASS_CPU_CACHE		0x00000004	/* CPU cache */
#define CLASS_IO		0x00000008	/* I/O read/writes etc */
#define CLASS_NETWORK		0x00000010	/* Network, sockets, etc */
#define CLASS_SCHEDULER		0x00000020	/* Scheduling */
#define CLASS_VM		0x00000040	/* VM stress, big memory, swapping */
#define CLASS_INTERRUPT		0x00000080	/* interrupt floods */
#define CLASS_OS		0x00000100	/* generic OS tests */

/* debug output bitmasks */
#define PR_ERROR		0x10000000	/* Print errors */
#define PR_INFO			0x20000000	/* Print info */
#define PR_DEBUG		0x40000000	/* Print debug */
#define PR_FAIL			0x80000000	/* Print test failure message */
#define PR_ALL			(PR_ERROR | PR_INFO | PR_DEBUG | PR_FAIL)

/* Large prime to stride around large VM regions */
#define PRIME_64		(0x8f0000000017116dULL)

/* Logging helpers */
extern int print(FILE *fp, const int flag,
	const char *const fmt, ...) __attribute__((format(printf, 3, 4)));
extern void pr_failed(const int flag, const char *name, const char *what);

#define pr_dbg(fp, fmt, args...)	print(fp, PR_DEBUG, fmt, ## args)
#define pr_inf(fp, fmt, args...)	print(fp, PR_INFO, fmt, ## args)
#define pr_err(fp, fmt, args...)	print(fp, PR_ERROR, fmt, ## args)
#define pr_fail(fp, fmt, args...)	print(fp, PR_FAIL, fmt, ## args)
#define pr_tidy(fp, fmt, args...)	print(fp, opt_sigint ? PR_INFO : PR_DEBUG, fmt, ## args)

#define pr_failed_err(name, what)	pr_failed(PR_ERROR, name, what)
#define pr_failed_dbg(name, what)	pr_failed(PR_DEBUG, name, what)

#define ABORT_FAILURES		(5)

/* Memory size constants */
#define KB			(1024ULL)
#define	MB			(KB * KB)
#define GB			(KB * KB * KB)
#define PAGE_4K_SHIFT		(12)
#define PAGE_4K			(1 << PAGE_4K_SHIFT)

#define MIN_OPS			(1ULL)
#define MAX_OPS			(100000000ULL)

/* Stressor defaults */
#define MIN_AIO_REQUESTS	(1)
#define MAX_AIO_REQUESTS	(4096)
#define DEFAULT_AIO_REQUESTS	(16)

#define MIN_BIGHEAP_GROWTH	(4 * KB)
#define MAX_BIGHEAP_GROWTH	(64 * MB)
#define DEFAULT_BIGHEAP_GROWTH	(64 * KB)

#define MIN_BSEARCH_SIZE	(1 * KB)
#define MAX_BSEARCH_SIZE	(4 * MB)
#define DEFAULT_BSEARCH_SIZE	(64 * KB)

#define MIN_DENTRIES		(1)
#define MAX_DENTRIES		(100000000)
#define DEFAULT_DENTRIES	(2048)

#define MIN_EPOLL_PORT		(1024)
#define MAX_EPOLL_PORT		(65535)
#define DEFAULT_EPOLL_PORT	(6000)

#define MIN_HDD_BYTES		(1 * MB)
#define MAX_HDD_BYTES		(256 * GB)
#define DEFAULT_HDD_BYTES	(1 * GB)

#define MIN_HDD_WRITE_SIZE	(1)
#define MAX_HDD_WRITE_SIZE	(4 * MB)
#define DEFAULT_HDD_WRITE_SIZE	(64 * 1024)

#define MIN_FIFO_READERS	(1)
#define MAX_FIFO_READERS	(64)
#define DEFAULT_FIFO_READERS	(4)

#define MIN_MQ_SIZE		(1)
#define MAX_MQ_SIZE		(32)
#define DEFAULT_MQ_SIZE		(10)

#define MIN_SEMAPHORE_PROCS	(4)
#define MAX_SEMAPHORE_PROCS	(64)
#define DEFAULT_SEMAPHORE_PROCS	(1)

#define MIN_FORKS		(1)
#define MAX_FORKS		(16000)
#define DEFAULT_FORKS		(1)

#define MIN_VFORKS		(1)
#define MAX_VFORKS		(16000)
#define DEFAULT_VFORKS		(1)

#define MIN_HSEARCH_SIZE	(1 * KB)
#define MAX_HSEARCH_SIZE	(4 * MB)
#define DEFAULT_HSEARCH_SIZE	(8 * KB)

#define MIN_LEASE_BREAKERS	(1)
#define MAX_LEASE_BREAKERS	(16)
#define DEFAULT_LEASE_BREAKERS	(1)

#define MIN_LSEARCH_SIZE	(1 * KB)
#define MAX_LSEARCH_SIZE	(4 * MB)
#define DEFAULT_LSEARCH_SIZE	(8 * KB)

#define MIN_MALLOC_BYTES	(1 * KB)
#define MAX_MALLOC_BYTES	(1 * GB)
#define DEFAULT_MALLOC_BYTES	(64 * KB)

#define MIN_MALLOC_MAX		(32)
#define MAX_MALLOC_MAX		(256 * 1024)
#define DEFAULT_MALLOC_MAX	(64 * KB)

#define MIN_MMAP_BYTES		(4 * KB)
#define MAX_MMAP_BYTES		(1 * GB)
#define DEFAULT_MMAP_BYTES	(256 * MB)

#define MIN_MREMAP_BYTES	(4 * KB)
#define MAX_MREMAP_BYTES	(1 * GB)
#define DEFAULT_MREMAP_BYTES	(256 * MB)

#define MIN_PTHREAD		(1)
#define MAX_PTHREAD		(30000)
#define DEFAULT_PTHREAD		(16)

#define MIN_QSORT_SIZE		(1 * KB)
#define MAX_QSORT_SIZE		(64 * MB)
#define DEFAULT_QSORT_SIZE	(256 * KB)

#define MIN_SENDFILE_SIZE	(1 * KB)
#define MAX_SENDFILE_SIZE	(1 * GB)
#define DEFAULT_SENDFILE_SIZE	(4 * MB)

#define MIN_SEEK_SIZE		(1 * MB)
#define MAX_SEEK_SIZE 		(256 * GB)
#define DEFAULT_SEEK_SIZE	(16 * MB)

#define MIN_SEQUENTIAL		(0)
#define MAX_SEQUENTIAL		(1000000)
#define DEFAULT_SEQUENTIAL	(0)	/* Disabled */

#define MIN_SHM_SYSV_BYTES	(1 * MB)
#define MAX_SHM_SYSV_BYTES	(1 * GB)
#define DEFAULT_SHM_SYSV_BYTES	(8 * MB)

#define MIN_SHM_SYSV_SEGMENTS	(1)
#define MAX_SHM_SYSV_SEGMENTS	(128)
#define DEFAULT_SHM_SYSV_SEGMENTS (8)

#define MIN_SOCKET_PORT		(1024)
#define MAX_SOCKET_PORT		(65535)
#define DEFAULT_SOCKET_PORT	(5000)

#define MIN_SPLICE_BYTES	(1*KB)
#define MAX_SPLICE_BYTES	(64*MB)
#define DEFAULT_SPLICE_BYTES	(64*KB)

#define MIN_TSEARCH_SIZE	(1 * KB)
#define MAX_TSEARCH_SIZE	(4 * MB)
#define DEFAULT_TSEARCH_SIZE	(64 * KB)

#define MIN_TIMER_FREQ		(1)
#define MAX_TIMER_FREQ		(100000000)
#define DEFAULT_TIMER_FREQ	(1000000)

#define MIN_UDP_PORT		(1024)
#define MAX_UDP_PORT		(65535)
#define DEFAULT_UDP_PORT	(7000)

#define MIN_VM_BYTES		(4 * KB)
#define MAX_VM_BYTES		(1 * GB)
#define DEFAULT_VM_BYTES	(256 * MB)

#define MIN_VM_HANG		(0)
#define MAX_VM_HANG		(3600)
#define DEFAULT_VM_HANG		(~0ULL)

#define MIN_VM_RW_BYTES		(4 * KB)
#define MAX_VM_RW_BYTES		(1 * GB)
#define DEFAULT_VM_RW_BYTES	(16 * MB)

#define MIN_VM_SPLICE_BYTES	(4*KB)
#define MAX_VM_SPLICE_BYTES	(64*MB)
#define DEFAULT_VM_SPLICE_BYTES	(64*KB)

#define DEFAULT_TIMEOUT		(60 * 60 * 24)
#define DEFAULT_BACKOFF		(0)
#define DEFAULT_LINKS		(8192)
#define DEFAULT_DIRS		(8192)

#define SWITCH_STOP		'X'
#define PIPE_STOP		"PIPE_STOP"

#define MEM_CACHE_SIZE		(65536 * 32)
#define UNDEFINED		(-1)

#define PAGE_MAPPED		(0x01)
#define PAGE_MAPPED_FAIL	(0x02)

#define FFT_SIZE		(4096)

#define SIEVE_GETBIT(a, i)	(a[i / 32] & (1 << (i & 31)))
#define SIEVE_CLRBIT(a, i)	(a[i / 32] &= ~(1 << (i & 31)))
#define SIEVE_SIZE 		(10000000)

/* MWC random number initial seed */
#define MWC_SEED_Z		(362436069ULL)
#define MWC_SEED_W		(521288629ULL)
#define MWC_SEED()		mwc_seed(MWC_SEED_W, MWC_SEED_Z)

#define SIZEOF_ARRAY(a)		(sizeof(a) / sizeof(a[0]))

#if defined(__x86_64__) || defined(__x86_64) || defined(__i386__) || defined(__i386)
#define STRESS_X86	1
#endif

#if defined(__GNUC__) && defined (__GNUC_MINOR__) && \
    (__GNUC__ >= 4) && (__GNUC_MINOR__ >= 6)
#define STRESS_VECTOR	1
#endif

/* NetBSD does not define MAP_ANONYMOUS */
#if defined(MAP_ANON) && !defined(MAP_ANONYMOUS)
#define MAP_ANONYMOUS MAP_ANON
#endif

#if defined(__GNUC__) && !defined(__clang__) && defined(__SIZEOF_INT128__)
#define STRESS_INT128	1
#endif

/* stress process prototype */
typedef int (*stress_func)(uint64_t *const counter, const uint32_t instance,
		    const uint64_t max_ops, const char *name);

/* Help information for options */
typedef struct {
	const char *opt_s;		/* short option */
	const char *opt_l;		/* long option */
	const char *description;	/* description */
} help_t;

/* Fast random number generator state */
typedef struct {
	uint64_t w;
	uint64_t z;
} mwc_t;

/* Force aligment to nearest cache line */
#ifdef __GNUC__
#define ALIGN64	__attribute__ ((aligned(64)))
#else
#define ALIGN64
#endif

/* Per process statistics and accounting info */
typedef struct {
	uint64_t counter;		/* number of bogo ops */
	struct tms tms;			/* run time stats of process */
	double start;			/* wall clock start time */
	double finish;			/* wall clock stop time */
} proc_stats_t;

/* Shared memory segment */
typedef struct {
	uint8_t	 mem_cache[MEM_CACHE_SIZE] ALIGN64;	/* Shared memory cache */
	uint32_t futex[STRESS_PROCS_MAX] ALIGN64;	/* Shared futexes */
	uint64_t futex_timeout[STRESS_PROCS_MAX] ALIGN64;
	sem_t sem_posix ALIGN64;			/* Shared semaphores */
	bool sem_posix_init ALIGN64;
	key_t sem_sysv_key_id ALIGN64;			/* System V semaphore key id */
	int sem_sysv_id ALIGN64;			/* System V semaphore id */
	bool sem_sysv_init ALIGN64;			/* System V semaphore initialized */
	proc_stats_t stats[0] ALIGN64;			/* Shared statistics */
} shared_t;

/* Stress test classes */
typedef struct {
	uint32_t class;			/* Class type bit mask */
	const char *name;		/* Name of class */
} class_t;

/* Stress tests */
typedef enum {
#if defined(__linux__)
	STRESS_AFFINITY = 0,
#endif
#if defined(__linux__)
	STRESS_AIO,
#endif
	STRESS_BRK,
	STRESS_BSEARCH,
	STRESS_BIGHEAP,
	STRESS_CACHE,
	STRESS_CHMOD,
#if _POSIX_C_SOURCE >= 199309L
	STRESS_CLOCK,
#endif
	STRESS_CPU,
	STRESS_DENTRY,
	STRESS_DIR,
	STRESS_DUP,
#if defined(__linux__)
	STRESS_EPOLL,
#endif
#if defined(__linux__)
	STRESS_EVENTFD,
#endif
#if _XOPEN_SOURCE >= 600 || _POSIX_C_SOURCE >= 200112L
	STRESS_FALLOCATE,
#endif
	STRESS_FAULT,
	STRESS_FIFO,
	STRESS_FLOCK,
	STRESS_FORK,
	STRESS_FSTAT,
#if defined(__linux__)
	STRESS_FUTEX,
#endif
	STRESS_GET,
	STRESS_HDD,
	STRESS_HSEARCH,
#if defined(__linux__)
	STRESS_INOTIFY,
#endif
	STRESS_IOSYNC,
	STRESS_KILL,
#if defined(F_SETLEASE) && defined(F_WRLCK) && defined(F_UNLCK)
	STRESS_LEASE,
#endif
	STRESS_LINK,
#if _BSD_SOURCE || _SVID_SOURCE || _XOPEN_SOURCE >= 500 || \
     (_XOPEN_SOURCE && _XOPEN_SOURCE_EXTENDED)
	STRESS_LOCKF,
#endif
	STRESS_LSEARCH,
	STRESS_MALLOC,
	STRESS_MEMCPY,
#if (_BSD_SOURCE || _SVID_SOURCE) && !defined(__gnu_hurd__)
	STRESS_MINCORE,
#endif
	STRESS_MMAP,
#if defined(__linux__)
	STRESS_MREMAP,
#endif
#if !defined(__gnu_hurd__)
	STRESS_MSG,
#endif
#if defined(__linux__)
	STRESS_MQ,
#endif
	STRESS_NICE,
	STRESS_NULL,
	STRESS_OPEN,
	STRESS_PIPE,
	STRESS_POLL,
#if defined(__linux__)
	STRESS_PROCFS,
#endif
	STRESS_PTHREAD,
	STRESS_QSORT,
#if defined(STRESS_X86)
	STRESS_RDRAND,
#endif
	STRESS_RENAME,
	STRESS_SEEK,
	STRESS_SEMAPHORE_POSIX,
#if !defined(__gnu_hurd__)
	STRESS_SEMAPHORE_SYSV,
#endif
#if defined(__linux__)
	STRESS_SENDFILE,
#endif
	STRESS_SHM_SYSV,
#if defined(__linux__)
	STRESS_SIGFD,
#endif
	STRESS_SIGFPE,
#if _POSIX_C_SOURCE >= 199309L && !defined(__gnu_hurd__)
	STRESS_SIGQUEUE,
#endif
	STRESS_SIGSEGV,
	STRESS_SOCKET,
#if defined(__linux__)
	STRESS_SPLICE,
#endif
	STRESS_STACK,
	STRESS_SWITCH,
	STRESS_SYMLINK,
	STRESS_SYSINFO,
#if defined(__linux__)
	STRESS_TIMER,
#endif
	STRESS_TSEARCH,
	STRESS_UDP,
#if defined(__linux__) || defined(__gnu_hurd__)
	STRESS_URANDOM,
#endif
	STRESS_UTIME,
#if defined(STRESS_VECTOR)
	STRESS_VECMATH,
#endif
#if  _BSD_SOURCE || \
    (_XOPEN_SOURCE >= 500 || _XOPEN_SOURCE && _XOPEN_SOURCE_EXTENDED) && \
    !(_POSIX_C_SOURCE >= 200809L || _XOPEN_SOURCE >= 700)
	STRESS_VFORK,
#endif
	STRESS_VM,
#if defined(__linux__)
	STRESS_VM_RW,
#endif
#if defined(__linux__)
	STRESS_VM_SPLICE,
#endif
#if !defined(__gnu_hurd__) && !defined(__NetBSD__)
	STRESS_WAIT,
#endif
#if defined (_POSIX_PRIORITY_SCHEDULING)
	STRESS_YIELD,
#endif
	STRESS_ZERO,
	/* STRESS_MAX must be last one */
	STRESS_MAX
} stress_id;

/* Command line long options */
typedef enum {
	/* Short options */
	OPT_QUERY = '?',
	OPT_ALL = 'a',
	OPT_BACKOFF = 'b',
	OPT_BIGHEAP = 'B',
	OPT_CPU = 'c',
	OPT_CACHE = 'C',
	OPT_HDD = 'd',
	OPT_DENTRY = 'D',
	OPT_FORK = 'f',
#if _XOPEN_SOURCE >= 600 || _POSIX_C_SOURCE >= 200112L
	OPT_FALLOCATE = 'F',
#endif
	OPT_IOSYNC = 'i',
	OPT_HELP = 'h',
	OPT_KEEP_NAME = 'k',
	OPT_CPU_LOAD = 'l',
	OPT_VM = 'm',
	OPT_METRICS = 'M',
	OPT_DRY_RUN = 'n',
	OPT_RENAME = 'R',
	OPT_OPEN = 'o',
	OPT_PIPE = 'p',
	OPT_POLL = 'P',
	OPT_QUIET = 'q',
	OPT_RANDOM = 'r',
	OPT_SWITCH = 's',
	OPT_SOCKET = 'S',
	OPT_TIMEOUT = 't',
#if defined (__linux__)
	OPT_TIMER = 'T',
#endif
#if defined (__linux__) || defined(__gnu_hurd__)
	OPT_URANDOM = 'u',
#endif
	OPT_VERBOSE = 'v',
	OPT_VERSION = 'V',
	OPT_YIELD = 'y',

	/* Long options only */

#if defined(__linux__)
	OPT_AFFINITY = 0x80,
	OPT_AFFINITY_OPS,
	OPT_AFFINITY_RAND,
#endif

#if defined (__linux__)
	OPT_AIO,
	OPT_AIO_OPS,
	OPT_AIO_REQUESTS,
#endif
	OPT_BRK,
	OPT_BRK_OPS,
	OPT_BRK_NOTOUCH,

	OPT_BSEARCH,
	OPT_BSEARCH_OPS,
	OPT_BSEARCH_SIZE,

	OPT_BIGHEAP_OPS,
	OPT_BIGHEAP_GROWTH,

	OPT_CLASS,
	OPT_CACHE_OPS,
	OPT_CACHE_FLUSH,
	OPT_CACHE_FENCE,

	OPT_CHMOD,
	OPT_CHMOD_OPS,

#if _POSIX_C_SOURCE >= 199309L
	OPT_CLOCK,
	OPT_CLOCK_OPS,
#endif

	OPT_CPU_OPS,
	OPT_CPU_METHOD,

	OPT_DENTRY_OPS,
	OPT_DENTRIES,
	OPT_DENTRY_ORDER,

	OPT_DIR,
	OPT_DIR_OPS,

	OPT_DUP,
	OPT_DUP_OPS,

#if defined(__linux__)
	OPT_EPOLL,
	OPT_EPOLL_OPS,
	OPT_EPOLL_PORT,
	OPT_EPOLL_DOMAIN,
#endif

	OPT_HDD_BYTES,
	OPT_HDD_WRITE_SIZE,
	OPT_HDD_OPS,
	OPT_HDD_OPTS,

#if defined(__linux__)
	OPT_EVENTFD,
	OPT_EVENTFD_OPS,
#endif

#if _XOPEN_SOURCE >= 600 || _POSIX_C_SOURCE >= 200112L
	OPT_FALLOCATE_OPS,
#endif
	OPT_FAULT,
	OPT_FAULT_OPS,

	OPT_FIFO,
	OPT_FIFO_OPS,
	OPT_FIFO_READERS,

	OPT_FLOCK,
	OPT_FLOCK_OPS,

	OPT_FORK_OPS,
	OPT_FORK_MAX,

	OPT_FSTAT,
	OPT_FSTAT_OPS,
	OPT_FSTAT_DIR,

	OPT_FUTEX,
	OPT_FUTEX_OPS,

	OPT_GET,
	OPT_GET_OPS,

	OPT_HSEARCH,
	OPT_HSEARCH_OPS,
	OPT_HSEARCH_SIZE,

#if defined (__linux__)
	OPT_INOTIFY,
	OPT_INOTIFY_OPS,
#endif

#if defined (__linux__)
	OPT_IONICE_CLASS,
	OPT_IONICE_LEVEL,
#endif

	OPT_IOSYNC_OPS,

	OPT_KILL,
	OPT_KILL_OPS,

#if defined(F_SETLEASE) && defined(F_WRLCK) && defined(F_UNLCK)
	OPT_LEASE,
	OPT_LEASE_OPS,
	OPT_LEASE_BREAKERS,
#endif

	OPT_LINK,
	OPT_LINK_OPS,

#if _BSD_SOURCE || _SVID_SOURCE || _XOPEN_SOURCE >= 500 || \
     (_XOPEN_SOURCE && _XOPEN_SOURCE_EXTENDED)
	OPT_LOCKF,
	OPT_LOCKF_OPS,
	OPT_LOCKF_NONBLOCK,
#endif

	OPT_LSEARCH,
	OPT_LSEARCH_OPS,
	OPT_LSEARCH_SIZE,

	OPT_MALLOC,
	OPT_MALLOC_OPS,
	OPT_MALLOC_BYTES,
	OPT_MALLOC_MAX,

	OPT_MEMCPY,
	OPT_MEMCPY_OPS,

	OPT_METRICS_BRIEF,

#if (_BSD_SOURCE || _SVID_SOURCE) && !defined(__gnu_hurd__)
	OPT_MINCORE,
	OPT_MINCORE_OPS,
	OPT_MINCORE_RAND,
#endif

	OPT_MMAP,
	OPT_MMAP_OPS,
	OPT_MMAP_BYTES,
	OPT_MMAP_FILE,
	OPT_MMAP_ASYNC,
	OPT_MMAP_MPROTECT,

#if defined(__linux__)
	OPT_MREMAP,
	OPT_MREMAP_OPS,
	OPT_MREMAP_BYTES,
#endif

	OPT_MSG,
	OPT_MSG_OPS,

#if defined(__linux__)
	OPT_MQ,
	OPT_MQ_OPS,
	OPT_MQ_SIZE,
#endif

	OPT_NICE,
	OPT_NICE_OPS,

	OPT_NO_MADVISE,

	OPT_NULL,
	OPT_NULL_OPS,

	OPT_OPEN_OPS,

#if (_BSD_SOURCE || _SVID_SOURCE) && !defined(__gnu_hurd__)
	OPT_PAGE_IN,
#endif

	OPT_PIPE_OPS,

	OPT_POLL_OPS,

	OPT_PROCFS,
	OPT_PROCFS_OPS,

	OPT_PTHREAD,
	OPT_PTHREAD_OPS,
	OPT_PTHREAD_MAX,

	OPT_QSORT,
	OPT_QSORT_OPS,
	OPT_QSORT_INTEGERS,

	OPT_RDRAND,
	OPT_RDRAND_OPS,

	OPT_RENAME_OPS,

	OPT_SCHED,
	OPT_SCHED_PRIO,

	OPT_SEEK,
	OPT_SEEK_OPS,
	OPT_SEEK_SIZE,

	OPT_SENDFILE,
	OPT_SENDFILE_OPS,
	OPT_SENDFILE_SIZE,

	OPT_SEMAPHORE_POSIX,
	OPT_SEMAPHORE_POSIX_OPS,
	OPT_SEMAPHORE_POSIX_PROCS,

#if !defined(__gnu_hurd__)
	OPT_SEMAPHORE_SYSV,
	OPT_SEMAPHORE_SYSV_OPS,
	OPT_SEMAPHORE_SYSV_PROCS,
#endif

	OPT_SHM_SYSV,
	OPT_SHM_SYSV_OPS,
	OPT_SHM_SYSV_BYTES,
	OPT_SHM_SYSV_SEGMENTS,

	OPT_SEQUENTIAL,

	OPT_SIGFD,
	OPT_SIGFD_OPS,

	OPT_SIGFPE,
	OPT_SIGFPE_OPS,

	OPT_SIGSEGV,
	OPT_SIGSEGV_OPS,

#if _POSIX_C_SOURCE >= 199309L
	OPT_SIGQUEUE,
	OPT_SIGQUEUE_OPS,
#endif

	OPT_SOCKET_OPS,
	OPT_SOCKET_PORT,
	OPT_SOCKET_DOMAIN,

	OPT_SWITCH_OPS,

#if defined(__linux__)
	OPT_SPLICE,
	OPT_SPLICE_OPS,
	OPT_SPLICE_BYTES,
#endif

	OPT_STACK,
	OPT_STACK_OPS,

	OPT_SYMLINK,
	OPT_SYMLINK_OPS,

	OPT_SYSINFO,
	OPT_SYSINFO_OPS,

#if defined (__linux__)
	OPT_TIMER_OPS,
	OPT_TIMER_FREQ,
#endif

	OPT_TSEARCH,
	OPT_TSEARCH_OPS,
	OPT_TSEARCH_SIZE,

	OPT_TIMES,

	OPT_UDP,
	OPT_UDP_OPS,
	OPT_UDP_PORT,
	OPT_UDP_DOMAIN,

#if defined (__linux__) || defined(__gnu_hurd__)
	OPT_URANDOM_OPS,
#endif
	OPT_UTIME,
	OPT_UTIME_OPS,
	OPT_UTIME_FSYNC,

#if defined(STRESS_VECTOR)
	OPT_VECMATH,
	OPT_VECMATH_OPS,
#endif

	OPT_VERIFY,

#if  _BSD_SOURCE || \
     (_XOPEN_SOURCE >= 500 || _XOPEN_SOURCE && _XOPEN_SOURCE_EXTENDED) && \
     !(_POSIX_C_SOURCE >= 200809L || _XOPEN_SOURCE >= 700)
	OPT_VFORK,
	OPT_VFORK_OPS,
	OPT_VFORK_MAX,
#endif

	OPT_VM_BYTES,
	OPT_VM_HANG,
	OPT_VM_KEEP,
#ifdef MAP_POPULATE
	OPT_VM_MMAP_POPULATE,
#endif
#ifdef MAP_LOCKED
	OPT_VM_MMAP_LOCKED,
#endif
	OPT_VM_OPS,
	OPT_VM_METHOD,

#if defined(__linux__)
	OPT_VM_RW,
	OPT_VM_RW_OPS,
	OPT_VM_RW_BYTES,
#endif

#if defined(__linux__)
	OPT_VM_SPLICE,
	OPT_VM_SPLICE_OPS,
	OPT_VM_SPLICE_BYTES,
#endif

#if !defined(__gnu_hurd__) && !defined(__NetBSD__)
	OPT_WAIT,
	OPT_WAIT_OPS,
#endif

#if defined (_POSIX_PRIORITY_SCHEDULING)
	OPT_YIELD_OPS,
#endif

	OPT_ZERO,
	OPT_ZERO_OPS,
} stress_op;

/* stress test metadata */
typedef struct {
	const stress_func stress_func;	/* stress test function */
	const stress_id id;		/* stress test ID */
	const short int short_getopt;	/* getopt short option */
	const stress_op op;		/* ops option */
	const char *name;		/* name of stress test */
	const uint32_t class;		/* class of stress test */
} stress_t;

typedef struct {
	pid_t	*pids;			/* process id */
	int32_t started_procs;		/* count of started processes */
	int32_t num_procs;		/* number of process per stressor */
	uint64_t bogo_ops;		/* number of bogo ops */
} proc_info_t;

typedef struct {
	const char	ch;		/* Scaling suffix */
	const uint64_t	scale;		/* Amount to scale by */
} scale_t;

/* Various option settings and flags */
extern const char *app_name;		/* Name of application */
extern shared_t *shared;		/* shared memory */
extern uint64_t	opt_timeout;		/* timeout in seconds */
extern int32_t	opt_flags;		/* option flags */
extern uint64_t opt_sequential;		/* Number of sequential iterations */
extern volatile bool opt_do_run;	/* false to exit stressor */
extern volatile bool opt_sigint;	/* true if stopped by SIGINT */
extern mwc_t __mwc;			/* internal mwc random state */

/*
 *  externs to force gcc to stash computed values and hence
 *  to stop the optimiser optimising code away to zero. The
 *  *_put funcs are essentially no-op functions.
 */
extern void double_put(const double a);
extern void uint64_put(const uint64_t a);
extern uint64_t uint64_zero(void);

/* Filenames and directories */
extern int stress_temp_filename(char *path, const size_t len, const char *name,
	const pid_t pid, const uint32_t instance, const uint64_t magic);
extern int stress_temp_dir(char *path, const size_t len, const char *name,
	const pid_t pid, const uint32_t instance);
extern int stress_temp_dir_mk(const char *name, const pid_t pid, const uint32_t instance);
extern int stress_temp_dir_rm(const char *name, const pid_t pid, const uint32_t instance);

#if defined(STRESS_X86)

static inline void clflush(volatile void *ptr)
{
        asm volatile("clflush %0" : "+m" (*(volatile char *)ptr));
}

static inline void mfence(void)
{
	asm volatile("mfence" : : : "memory");
}

#else

#define clflush(ptr)	/* No-op */
#define mfence()	/* No-op */

#endif

/*
 *  mwc() 
 *      Multiply-with-carry random numbers
 *      fast pseudo random number generator, see
 *      http://www.cse.yorku.ca/~oz/marsaglia-rng.html
 */
static inline uint64_t mwc(void)
{
	__mwc.z = 36969 * (__mwc.z & 65535) + (__mwc.z >> 16);
	__mwc.w = 18000 * (__mwc.w & 65535) + (__mwc.w >> 16);
	return (__mwc.z << 16) + __mwc.w;
}

extern void mwc_seed(const uint64_t w, const uint64_t z);
extern void mwc_reseed(void);

/* Time handling */
extern double timeval_to_double(const struct timeval *tv);
extern double time_now(void);

/* Misc settings helpers */
extern void set_oom_adjustment(const char *name, bool killable);
extern void set_sched(const int sched, const int sched_priority);
extern void set_iopriority(const int class, const int level);
extern void set_oom_adjustment(const char *name, bool killable);
extern void set_coredump(const char *name);
extern void set_proc_name(const char *name);

/* Argument parsing and range checking */
extern int get_opt_sched(const char *const str);
extern int get_opt_ionice_class(const char *const str);
extern int get_int(const char *const str);
extern uint64_t get_uint64(const char *const str);
extern uint64_t get_uint64_scale(const char *const str, const scale_t scales[],
	const char *const msg);
extern uint64_t get_uint64_byte(const char *const str);
extern uint64_t get_uint64_time(const char *const str);
extern long int opt_long(const char *opt, const char *str);
extern void check_value(const char *const msg, const int val);
extern void check_range(const char *const opt, const uint64_t val,
	const uint64_t lo, const uint64_t hi);

/* Misc helper funcs */
extern char *munge_underscore(char *str);
extern size_t stress_get_pagesize(void);
extern long stress_get_processors_online(void);
extern long stress_get_ticks_per_second(void);
extern void set_max_limits(void);

/* Memory tweaking */
extern int madvise_random(void *addr, size_t length);
extern int mincore_touch_pages(void *buf, size_t buf_len);

/* Mounts */
extern int mount_add(char *mnts[], const int max, int *n, const char *name);
extern void mount_free(char *mnts[], const int n);
extern int mount_get(char *mnts[], const int max);

/* Network helpers */
void stress_set_net_port(const char *optname, const char *optarg,
	const int min_port, const int max_port, int *port);
int stress_set_net_domain(const char *name, const char *domain_name, int *domain);
void stress_set_sockaddr(const char *name, const uint32_t instance,
	const pid_t ppid, const int domain, const int port,
	struct sockaddr **sockaddr, socklen_t *len);

extern void stress_semaphore_posix_init(void);
extern void stress_semaphore_posix_destroy(void);

extern void stress_semaphore_sysv_init(void);
extern void stress_semaphore_sysv_destroy(void);

/* Used to set options for specific stressors */
extern void stress_adjust_ptread_max(uint64_t max);
extern void stress_set_aio_requests(const char *optarg);
extern void stress_set_bigheap_growth(const char *optarg);
extern void stress_set_bsearch_size(const char *optarg);
extern void stress_set_cpu_load(const char *optarg);
extern int  stress_set_cpu_method(const char *name);
extern void stress_set_dentries(const char *optarg);
extern int  stress_set_dentry_order(const char *optarg);
extern void stress_set_epoll_port(const char *optarg);
extern int  stress_set_epoll_domain(const char *optarg);
extern void stress_set_fifo_readers(const char *optarg);
extern void stress_set_fork_max(const char *optarg);
extern void stress_set_fstat_dir(const char *optarg);
extern void stress_set_hdd_bytes(const char *optarg);
extern int stress_hdd_opts(char *opts);
extern void stress_set_hdd_write_size(const char *optarg);
extern void stress_set_hsearch_size(const char *optarg);
extern void stress_set_lease_breakers(const char *optarg);
extern void stress_set_lsearch_size(const char *optarg);
extern void stress_set_malloc_bytes(const char *optarg);
extern void stress_set_malloc_max(const char *optarg);
extern void stress_set_mmap_bytes(const char *optarg);
extern void stress_set_mremap_bytes(const char *optarg);
extern void stress_set_mq_size(const char *optarg);
extern void stress_set_pthread_max(const char *optarg);
extern void stress_set_qsort_size(const void *optarg);
extern void stress_set_seek_size(const char *optarg);
extern void stress_set_sendfile_size(const char *optarg);
extern void stress_set_semaphore_posix_procs(const char *optarg);
extern void stress_set_semaphore_sysv_procs(const char *optarg);
extern void stress_set_shm_sysv_bytes(const char *optarg);
extern void stress_set_shm_sysv_segments(const char *optarg);
extern int  stress_set_socket_domain(const char *name);
extern void stress_set_socket_port(const char *optarg);
extern void stress_set_splice_bytes(const char *optarg);
extern void stress_set_timer_freq(const char *optarg);
extern void stress_set_tsearch_size(const char *optarg);
extern int  stress_set_udp_domain(const char *name);
extern void stress_set_udp_port(const char *optarg);
extern void stress_set_vfork_max(const char *optarg);
extern void stress_set_vm_bytes(const char *optarg);
extern void stress_set_vm_flags(const int flag);
extern void stress_set_vm_hang(const char *optarg);
extern int  stress_set_vm_method(const char *name);
extern void stress_set_vm_rw_bytes(const char *optarg);
extern void stress_set_vm_splice_bytes(const char *optarg);

#define STRESS(name)							\
extern int name(uint64_t *const counter, const uint32_t instance,	\
        const uint64_t max_ops, const char *name)

/* Stressors */
STRESS(stress_affinity);
STRESS(stress_aio);
STRESS(stress_bigheap);
STRESS(stress_brk);
STRESS(stress_bsearch);
STRESS(stress_cache);
STRESS(stress_chmod);
STRESS(stress_clock);
STRESS(stress_cpu);
STRESS(stress_dentry);
STRESS(stress_dir);
STRESS(stress_dup);
STRESS(stress_epoll);
STRESS(stress_eventfd);
STRESS(stress_hdd);
STRESS(stress_hsearch);
STRESS(stress_fallocate);
STRESS(stress_fault);
STRESS(stress_fifo);
STRESS(stress_flock);
STRESS(stress_fork);
STRESS(stress_fstat);
STRESS(stress_futex);
STRESS(stress_get);
STRESS(stress_inotify);
STRESS(stress_iosync);
STRESS(stress_kill);
STRESS(stress_lease);
STRESS(stress_link);
STRESS(stress_lockf);
STRESS(stress_lsearch);
STRESS(stress_malloc);
STRESS(stress_memcpy);
STRESS(stress_mincore);
STRESS(stress_mmap);
STRESS(stress_mremap);
STRESS(stress_msg);
STRESS(stress_mq);
STRESS(stress_nice);
STRESS(stress_noop);
STRESS(stress_null);
STRESS(stress_open);
STRESS(stress_pipe);
STRESS(stress_poll);
STRESS(stress_procfs);
STRESS(stress_pthread);
STRESS(stress_qsort);
STRESS(stress_rdrand);
STRESS(stress_rename);
STRESS(stress_seek);
STRESS(stress_sem_posix);
STRESS(stress_sem_sysv);
STRESS(stress_shm_sysv);
STRESS(stress_sendfile);
STRESS(stress_sigfd);
STRESS(stress_sigfpe);
STRESS(stress_sigsegv);
STRESS(stress_sigq);
STRESS(stress_socket);
STRESS(stress_splice);
STRESS(stress_stack);
STRESS(stress_switch);
STRESS(stress_symlink);
STRESS(stress_sysinfo);
STRESS(stress_timer);
STRESS(stress_tsearch);
STRESS(stress_udp);
STRESS(stress_urandom);
STRESS(stress_utime);
STRESS(stress_vecmath);
STRESS(stress_vfork);
STRESS(stress_vm);
STRESS(stress_vm_rw);
STRESS(stress_vm_splice);
STRESS(stress_wait);
STRESS(stress_yield);
STRESS(stress_zero);

#endif
