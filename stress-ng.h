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
#include <pthread.h>
#include <sys/time.h>
#include <sys/times.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/mman.h>
#if defined (__linux__)
#include <sys/syscall.h>
#include <sys/quota.h>
#endif
#include <fcntl.h>
#include <errno.h>
#if defined (__GLIBC__)
#include <features.h>
#endif

/*
 * STRESS_ASSERT(test)
 *   throw compile time error if test not true
 */
#define STRESS_CONCAT(a, b) a ## b
#define STRESS_CONCAT_EXPAND(a, b) STRESS_CONCAT(a, b)
#define STRESS_ASSERT(expr) \
	enum { STRESS_CONCAT_EXPAND(STRESS_ASSERT_AT_LINE_, __LINE__) = 1 / !!(expr) };

#define STRESS_MIN(a,b) (((a) < (b)) ? (a) : (b))
#define STRESS_MAX(a,b) (((a) > (b)) ? (a) : (b))

#define _VER_(major, minor, patchlevel)			\
	((major * 10000) + (minor * 100) + patchlevel)

#if defined(__GLIBC__) && defined(__GLIBC_MINOR__)
#define NEED_GLIBC(major, minor, patchlevel) 			\
	_VER_(major, minor, patchlevel) <= _VER_(__GLIBC__, __GLIBC_MINOR__, 0)
#else
#define NEED_GLIBC(major, minor, patchlevel) 	(0)
#endif

#if defined(__GNUC__) && defined(__GNUC_MINOR__)
#if defined(__GNUC_PATCHLEVEL__)
#define NEED_GNUC(major, minor, patchlevel) 			\
	_VER_(major, minor, patchlevel) <= _VER_(__GNUC__, __GNUC_MINOR__, __GNUC_PATCHLEVEL__)
#else
#define NEED_GNUC(major, minor, patchlevel) 			\
	_VER_(major, minor, patchlevel) <= _VER_(__GNUC__, __GNUC_MINOR__, 0)
#endif
#else
#define NEED_GNUC(major, minor, patchlevel) 	(0)
#endif

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
#define SOCKET_PAIR_BUF		(64)		/* Socket pair I/O buffer size */

/* debug output bitmasks */
#define PR_ERROR		0x0000000001ULL /* Print errors */
#define PR_INFO			0x0000000002ULL /* Print info */
#define PR_DEBUG		0x0000000004ULL /* Print debug */
#define PR_FAIL			0x0000000008ULL /* Print test failure message */
#define PR_ALL			(PR_ERROR | PR_INFO | PR_DEBUG | PR_FAIL)

/* Option bit masks */
#define OPT_FLAGS_AFFINITY_RAND	0x00000000010ULL	/* Change affinity randomly */
#define OPT_FLAGS_DRY_RUN	0x00000000020ULL	/* Don't actually run */
#define OPT_FLAGS_METRICS	0x00000000040ULL	/* Dump metrics at end */
#define OPT_FLAGS_VM_KEEP	0x00000000080ULL	/* Don't keep re-allocating */
#define OPT_FLAGS_RANDOM	0x00000000100ULL	/* Randomize */
#define OPT_FLAGS_SET		0x00000000200ULL	/* Set if user specifies stress procs */
#define OPT_FLAGS_KEEP_NAME	0x00000000400ULL	/* Keep stress names to stress-ng */
#define OPT_FLAGS_UTIME_FSYNC	0x00000000800ULL	/* fsync after utime modification */
#define OPT_FLAGS_METRICS_BRIEF	0x00000001000ULL	/* dump brief metrics */
#define OPT_FLAGS_VERIFY	0x00000002000ULL	/* verify mode */
#define OPT_FLAGS_MMAP_MADVISE	0x00000004000ULL	/* enable random madvise settings */
#define OPT_FLAGS_MMAP_MINCORE	0x00000008000ULL	/* mincore force pages into mem */
#define OPT_FLAGS_TIMES		0x00000010000ULL	/* user/system time summary */
#define OPT_FLAGS_CACHE_PREFETCH 0x00000020000ULL 	/* cache prefetch */
#define OPT_FLAGS_CACHE_FLUSH	0x00000040000ULL	/* cache flush */
#define OPT_FLAGS_CACHE_FENCE	0x00000080000ULL	/* cache fence */
#define OPT_FLAGS_CACHE_MASK	(OPT_FLAGS_CACHE_FLUSH | \
				 OPT_FLAGS_CACHE_FENCE | \
				 OPT_FLAGS_CACHE_PREFETCH)
#define OPT_FLAGS_MMAP_FILE	0x00000100000ULL	/* mmap onto a file */
#define OPT_FLAGS_MMAP_ASYNC	0x00000200000ULL	/* mmap file asynchronous I/O */
#define OPT_FLAGS_MMAP_MPROTECT	0x00000400000ULL	/* mmap mprotect enabled */
#define OPT_FLAGS_LOCKF_NONBLK	0x00000800000ULL	/* Non-blocking lockf */
#define OPT_FLAGS_MINCORE_RAND	0x00001000000ULL	/* mincore randomize */
#define OPT_FLAGS_BRK_NOTOUCH	0x00002000000ULL	/* brk, don't touch page */
#define OPT_FLAGS_HDD_SYNC	0x00004000000ULL	/* HDD O_SYNC */
#define OPT_FLAGS_HDD_DSYNC	0x00008000000ULL	/* HDD O_DYNC */
#define OPT_FLAGS_HDD_DIRECT	0x00010000000ULL	/* HDD O_DIRECT */
#define OPT_FLAGS_HDD_NOATIME	0x00020000000ULL	/* HDD O_NOATIME */
#define OPT_FLAGS_STACK_FILL	0x00040000000ULL	/* Fill stack */
#define OPT_FLAGS_MINIMIZE	0x00080000000ULL	/* Minimize */
#define OPT_FLAGS_MAXIMIZE	0x00100000000ULL	/* Maximize */
#define OPT_FLAGS_MINMAX_MASK	(OPT_FLAGS_MINIMIZE | OPT_FLAGS_MAXIMIZE)
#define OPT_FLAGS_SYSLOG	0x00200000000ULL	/* log test progress to syslog */
#define OPT_FLAGS_AGGRESSIVE	0x00400000000ULL	/* aggressive mode enabled */
#define OPT_FLAGS_TIMER_RAND	0x00800000000ULL	/* Enable random timer freq */
#define OPT_FLAGS_TIMERFD_RAND	0x01000000000ULL	/* Enable random timerfd freq */
#define OPT_FLAGS_ALL		0x02000000000ULL	/* --all mode */
#define OPT_FLAGS_SEQUENTIAL	0x04000000000ULL	/* --sequential mode */
#define OPT_FLAGS_PERF_STATS	0x08000000000ULL	/* --perf stats mode */
#define OPT_FLAGS_LOG_BRIEF	0x10000000000ULL	/* --log-brief */
#define OPT_FLAGS_THERMAL_ZONES 0x20000000000ULL	/* --tz thermal zones */

#define OPT_FLAGS_AGGRESSIVE_MASK \
	(OPT_FLAGS_AFFINITY_RAND | OPT_FLAGS_UTIME_FSYNC | \
	 OPT_FLAGS_MMAP_MADVISE | OPT_FLAGS_MMAP_MINCORE | \
	 OPT_FLAGS_CACHE_FLUSH | OPT_FLAGS_CACHE_FENCE |   \
	 OPT_FLAGS_MMAP_FILE | OPT_FLAGS_MMAP_ASYNC |      \
	 OPT_FLAGS_MMAP_MPROTECT | OPT_FLAGS_LOCKF_NONBLK |\
	 OPT_FLAGS_MINCORE_RAND | OPT_FLAGS_HDD_SYNC |     \
	 OPT_FLAGS_HDD_DSYNC | OPT_FLAGS_HDD_DIRECT |      \
	 OPT_FLAGS_STACK_FILL | OPT_FLAGS_CACHE_PREFETCH | \
	 OPT_FLAGS_AGGRESSIVE)


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
#define CLASS_PIPE_IO		0x00000200	/* pipe I/O */
#define CLASS_FILESYSTEM	0x00000400	/* file system */
#define CLASS_DEV		0x00000800	/* device (null, zero, etc) */

/* Network domains flags */
#define DOMAIN_INET		0x00000001	/* AF_INET */
#define DOMAIN_INET6		0x00000002	/* AF_INET6 */
#define DOMAIN_UNIX		0x00000004	/* AF_UNIX */

#define DOMAIN_INET_ALL		(DOMAIN_INET | DOMAIN_INET6)
#define DOMAIN_ALL		(DOMAIN_INET | DOMAIN_INET6 | DOMAIN_UNIX)

/* Large prime to stride around large VM regions */
#define PRIME_64		(0x8f0000000017116dULL)

/* Logging helpers */
extern int print(FILE *fp, const uint64_t flag,
	const char *const fmt, ...) __attribute__((format(printf, 3, 4)));
extern void pr_failed(const uint64_t flag, const char *name, const char *what, const int err);
extern int pr_yaml(FILE *fp, const char *const fmt, ...);
extern void pr_yaml_runinfo(FILE *fp);


#define pr_dbg(fp, fmt, args...)	print(fp, PR_DEBUG, fmt, ## args)
#define pr_inf(fp, fmt, args...)	print(fp, PR_INFO, fmt, ## args)
#define pr_err(fp, fmt, args...)	print(fp, PR_ERROR, fmt, ## args)
#define pr_fail(fp, fmt, args...)	print(fp, PR_FAIL, fmt, ## args)
#define pr_tidy(fp, fmt, args...)	print(fp, opt_sigint ? PR_INFO : PR_DEBUG, fmt, ## args)

#define pr_failed_err(name, what)	pr_failed(PR_FAIL | PR_ERROR, name, what, errno)
#define pr_failed_errno(name, what, e)	pr_failed(PR_FAIL | PR_ERROR, name, what, e)
#define pr_failed_dbg(name, what)	pr_failed(PR_DEBUG, name, what, errno)

#define ABORT_FAILURES		(5)

/* Memory size constants */
#define KB			(1024ULL)
#define	MB			(KB * KB)
#define GB			(KB * KB * KB)
#define PAGE_4K_SHIFT		(12)
#define PAGE_4K			(1 << PAGE_4K_SHIFT)

#define MIN_OPS			(1ULL)
#define MAX_OPS			(100000000ULL)
#define MAX_32			(0xffffffffUL)

/* Stressor defaults */
#define MIN_AIO_REQUESTS	(1)
#define MAX_AIO_REQUESTS	(4096)
#define DEFAULT_AIO_REQUESTS	(16)

#define MIN_AIO_LINUX_REQUESTS	(1)
#define MAX_AIO_LINUX_REQUESTS	(4096)
#define DEFAULT_AIO_LINUX_REQUESTS	(64)

#define MIN_BIGHEAP_GROWTH	(4 * KB)
#define MAX_BIGHEAP_GROWTH	(64 * MB)
#define DEFAULT_BIGHEAP_GROWTH	(64 * KB)

#define MIN_BSEARCH_SIZE	(1 * KB)
#define MAX_BSEARCH_SIZE	(4 * MB)
#define DEFAULT_BSEARCH_SIZE	(64 * KB)

#define MIN_CLONES		(1)
#define MAX_CLONES		(1000000)
#define DEFAULT_CLONES		(8192)

#define MIN_DENTRIES		(1)
#define MAX_DENTRIES		(1000000)
#define DEFAULT_DENTRIES	(2048)

#define MIN_EPOLL_PORT		(1024)
#define MAX_EPOLL_PORT		(65535)
#define DEFAULT_EPOLL_PORT	(6000)

#define MIN_HDD_BYTES		(1 * MB)
#define MAX_HDD_BYTES		(256ULL * GB)
#define DEFAULT_HDD_BYTES	(1 * GB)

#define MIN_HDD_WRITE_SIZE	(1)
#define MAX_HDD_WRITE_SIZE	(4 * MB)
#define DEFAULT_HDD_WRITE_SIZE	(64 * 1024)

#define MIN_FALLOCATE_BYTES	(1 * MB)
#if UINTPTR_MAX == MAX_32
#define MAX_FALLOCATE_BYTES	(MAX_32)
#else
#define MAX_FALLOCATE_BYTES	(4 * GB)
#endif
#define DEFAULT_FALLOCATE_BYTES	(1 * GB)

#define MIN_FIFO_READERS	(1)
#define MAX_FIFO_READERS	(64)
#define DEFAULT_FIFO_READERS	(4)

#define MIN_ITIMER_FREQ		(1)
#define MAX_ITIMER_FREQ		(100000000)
#define DEFAULT_ITIMER_FREQ	(1000000)

#define MIN_MQ_SIZE		(1)
#define MAX_MQ_SIZE		(32)
#define DEFAULT_MQ_SIZE		(10)

#define MIN_SEMAPHORE_PROCS	(2)
#define MAX_SEMAPHORE_PROCS	(64)
#define DEFAULT_SEMAPHORE_PROCS	(2)

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
#define MAX_LEASE_BREAKERS	(64)
#define DEFAULT_LEASE_BREAKERS	(1)

#define MIN_LSEARCH_SIZE	(1 * KB)
#define MAX_LSEARCH_SIZE	(1 * MB)
#define DEFAULT_LSEARCH_SIZE	(8 * KB)

#define MIN_MALLOC_BYTES	(1 * KB)
#if UINTPTR_MAX == MAX_32
#define MAX_MALLOC_BYTES	(MAX_32)
#else
#define MAX_MALLOC_BYTES	(4 * GB)
#endif
#define DEFAULT_MALLOC_BYTES	(64 * KB)

#define MIN_MALLOC_MAX		(32)
#define MAX_MALLOC_MAX		(256 * 1024)
#define DEFAULT_MALLOC_MAX	(64 * KB)

#define MIN_MALLOC_THRESHOLD	(1)
#define MAX_MALLOC_THRESHOLD	(256 * MB)
#define DEFAULT_MALLOC_THRESHOLD (128 * KB)

#define MIN_MATRIX_SIZE		(16)
#define MAX_MATRIX_SIZE		(4096)
#define DEFAULT_MATRIX_SIZE	(256)

#define MIN_MMAP_BYTES		(4 * KB)
#if UINTPTR_MAX == MAX_32
#define MAX_MMAP_BYTES		(MAX_32)
#else
#define MAX_MMAP_BYTES		(4 * GB)
#endif
#define DEFAULT_MMAP_BYTES	(256 * MB)

#define MIN_MREMAP_BYTES	(4 * KB)
#if UINTPTR_MAX == MAX_32
#define MAX_MREMAP_BYTES	(MAX_32)
#else
#define MAX_MREMAP_BYTES	(4 * GB)
#endif
#define DEFAULT_MREMAP_BYTES	(256 * MB)

#define MIN_PTHREAD		(1)
#define MAX_PTHREAD		(30000)
#define DEFAULT_PTHREAD		(1024)

#define MIN_QSORT_SIZE		(1 * KB)
#define MAX_QSORT_SIZE		(4 * MB)
#define DEFAULT_QSORT_SIZE	(256 * KB)

#define MIN_READAHEAD_BYTES	(1 * MB)
#define MAX_READAHEAD_BYTES	(256ULL * GB)
#define DEFAULT_READAHEAD_BYTES	(1 * GB)

#define MIN_SENDFILE_SIZE	(1 * KB)
#define MAX_SENDFILE_SIZE	(1 * GB)
#define DEFAULT_SENDFILE_SIZE	(4 * MB)

#define MIN_SEEK_SIZE		(1 * MB)
#define MAX_SEEK_SIZE 		(256ULL * GB)
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

#define MIN_TIMERFD_FREQ	(1)
#define MAX_TIMERFD_FREQ	(100000000)
#define DEFAULT_TIMERFD_FREQ	(1000000)

#define MIN_UDP_PORT		(1024)
#define MAX_UDP_PORT		(65535)
#define DEFAULT_UDP_PORT	(7000)

#define MIN_VM_BYTES		(4 * KB)
#if UINTPTR_MAX == MAX_32
#define MAX_VM_BYTES		(MAX_32)
#else
#define MAX_VM_BYTES		(4 * GB)
#endif
#define DEFAULT_VM_BYTES	(256 * MB)

#define MIN_VM_HANG		(0)
#define MAX_VM_HANG		(3600)
#define DEFAULT_VM_HANG		(~0ULL)

#define MIN_VM_RW_BYTES		(4 * KB)
#if UINTPTR_MAX == MAX_32
#define MAX_VM_RW_BYTES		(MAX_32)
#else
#define MAX_VM_RW_BYTES		(4 * GB)
#endif
#define DEFAULT_VM_RW_BYTES	(16 * MB)

#define MIN_VM_SPLICE_BYTES	(4*KB)
#define MAX_VM_SPLICE_BYTES	(64*MB)
#define DEFAULT_VM_SPLICE_BYTES	(64*KB)

#define MIN_ZOMBIES		(1)
#define MAX_ZOMBIES		(1000000)
#define DEFAULT_ZOMBIES		(8192)

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

#define STRESS_CPU_DITHER_X	(1024)
#define STRESS_CPU_DITHER_Y	(768)

#define STRESS_NBITS(a)		(sizeof(a[0]) * 8)
#define STRESS_GETBIT(a, i)	(a[i / STRESS_NBITS(a)] & (1 << (i & (STRESS_NBITS(a)-1))))
#define STRESS_CLRBIT(a, i)	(a[i / STRESS_NBITS(a)] &= ~(1 << (i & (STRESS_NBITS(a)-1))))
#define STRESS_SETBIT(a, i)	(a[i / STRESS_NBITS(a)] |= (1 << (i & (STRESS_NBITS(a)-1))))

#define SIEVE_SIZE 		(10000000)

/* MWC random number initial seed */
#define MWC_SEED_Z		(362436069UL)
#define MWC_SEED_W		(521288629UL)
#define MWC_SEED()		mwc_seed(MWC_SEED_W, MWC_SEED_Z)

#define SIZEOF_ARRAY(a)		(sizeof(a) / sizeof(a[0]))

#if defined(__x86_64__) || defined(__x86_64) || defined(__i386__) || defined(__i386)
#define STRESS_X86	1
#endif

#if NEED_GNUC(4,7,0)
#define STRESS_VECTOR	1
#endif

/* NetBSD does not define MAP_ANONYMOUS */
#if defined(MAP_ANON) && !defined(MAP_ANONYMOUS)
#define MAP_ANONYMOUS MAP_ANON
#endif

#if defined(__GNUC__) && !defined(__clang__) && defined(__SIZEOF_INT128__)
#define STRESS_INT128	1
#endif

#if defined(__linux__)
#define STRESS_IONICE
#endif

#if (_BSD_SOURCE || _SVID_SOURCE || !defined(__gnu_hurd__))
#define STRESS_PAGE_IN
#endif

#if defined(__linux__)
#define STRESS_IOPRIO
#endif

#if defined(__GNUC__) && defined(__linux__)
#define STRESS_MALLOPT
#endif

#if defined(__GNUC__) && !defined(__clang__) && NEED_GNUC(4,6,0)
#define OPTIMIZE3 __attribute__((optimize("-O3")))
#else
#define OPTIMIZE3
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
	uint32_t w;
	uint32_t z;
} mwc_t;

/* Force aligment to nearest cache line */
#if defined(__GNUC__)
#define ALIGN64	__attribute__ ((aligned(64)))
#else
#define ALIGN64
#endif

#if defined(__GNUC__) && NEED_GNUC(4,6,0)
#define HOT	__attribute__ ((hot))
#else
#define HOT
#endif

#if defined(__GNUC__) && NEED_GNUC(4,6,0)
#define MLOCKED __attribute__((__section__("mlocked")))
#define MLOCKED_SECTION 1
#else
#define MLOCKED
#endif

#if defined(__linux__) && defined(__NR_perf_event_open)
#define STRESS_PERF_STATS	(1)
#define STRESS_PERF_INVALID	(~0ULL)
enum {
	STRESS_PERF_HW_CPU_CYCLES = 0,
	STRESS_PERF_HW_INSTRUCTIONS,
	STRESS_PERF_HW_CACHE_REFERENCES,
	STRESS_PERF_HW_CACHE_MISSES,
	STRESS_PERF_HW_BRANCH_INSTRUCTIONS,
	STRESS_PERF_HW_BRANCH_MISSES,
	STRESS_PERF_HW_BUS_CYCLES,
	STRESS_PERF_HW_REF_CPU_CYCLES,
	STRESS_PERF_HW_STALLED_CYCLES_FRONTEND,
	STRESS_PERF_HW_STALLED_CYCLES_BACKEND,

	STRESS_PERF_SW_PAGE_FAULTS_MIN,
	STRESS_PERF_SW_PAGE_FAULTS_MAJ,
	STRESS_PERF_SW_CONTEXT_SWITCHES,
	STRESS_PERF_SW_CPU_MIGRATIONS,
	STRESS_PERF_SW_ALIGNMENT_FAULTS,

	STRESS_PERF_TP_SYSCALLS_ENTER,
	STRESS_PERF_TP_SYSCALLS_EXIT,
	STRESS_PERF_TP_TLB_FLUSH,
	STRESS_PERF_TP_KMALLOC,
	STRESS_PERF_TP_KMALLOC_NODE,
	STRESS_PERF_TP_KFREE,
	STRESS_PERF_TP_KMEM_CACHE_ALLOC,
	STRESS_PERF_TP_KMEM_CACHE_ALLOC_NODE,
	STRESS_PERF_TP_KMEM_CACHE_FREE,
	STRESS_PERF_TP_MM_PAGE_ALLOC,
	STRESS_PERF_TP_MM_PAGE_FREE,
	STRESS_PERF_TP_RCU_UTILIZATION,
	STRESS_PERF_TP_SCHED_MIGRATE_TASK,
	STRESS_PERF_TP_SCHED_MOVE_NUMA,
	STRESS_PERF_TP_SIGNAL_GENERATE,
	STRESS_PERF_TP_SIGNAL_DELIVER,
	STRESS_PERF_TP_PAGE_FAULT_USER,
	STRESS_PERF_TP_PAGE_FAULT_KERNEL,
	
	STRESS_PERF_MAX
};

/* per perf counter info */
typedef struct {
	uint64_t counter;		/* perf counter */
	int	 fd;			/* perf per counter fd */
} perf_stat_t;

/* per stressor perf info */
typedef struct {
	perf_stat_t	perf_stat[STRESS_PERF_MAX]; /* perf counters */
	int		perf_opened;		/* count of opened counters */
} stress_perf_t;
#endif

#if defined(__linux__)
#define	STRESS_THERMAL_ZONES	 (1)
#define STRESS_THERMAL_ZONES_MAX (31)	/* best if prime */
#endif

#if defined(STRESS_THERMAL_ZONES)
/* per stressor thermal zone info */
typedef struct tz_info {
	char	*path;
	char 	*type;
	size_t	index;
	struct tz_info *next;
} tz_info_t;

typedef struct {
	uint64_t temperature;		/* temperature in Celsius * 1000 */
} tz_stat_t;

typedef struct {
	tz_stat_t tz_stat[STRESS_THERMAL_ZONES_MAX];
} stress_tz_t;
#endif

/* Per process statistics and accounting info */
typedef struct {
	uint64_t counter;		/* number of bogo ops */
	struct tms tms;			/* run time stats of process */
	double start;			/* wall clock start time */
	double finish;			/* wall clock stop time */
#if defined(STRESS_PERF_STATS)
	stress_perf_t sp;		/* perf counters */
#endif
#if defined(STRESS_THERMAL_ZONES)
	stress_tz_t tz;			/* thermal zones */
#endif
} proc_stats_t;


/* Shared memory segment */
typedef struct {
	uint8_t	 mem_cache[MEM_CACHE_SIZE];		/* Shared memory cache */
	struct {
		uint32_t futex[STRESS_PROCS_MAX];	/* Shared futexes */
		uint64_t timeout[STRESS_PROCS_MAX];	/* Shared futex timeouts */
	} futex;
#if defined(__linux__)
	struct {
		sem_t sem;				/* Shared posix semaphores */
		bool init;				/* Semaphores initialised? */
	} sem_posix;
#endif
	struct {
		key_t key_id;				/* System V semaphore key id */
		int sem_id;				/* System V semaphore id */
		bool init;				/* System V semaphore initialized */
	} sem_sysv;
	struct {
		bool no_perf;				/* true = Perf not available */
		pthread_spinlock_t lock;		/* spinlock on no_perf updates */
	} perf;
	struct {
		pthread_spinlock_t lock;		/* spinlock on sigsuspend updates */
	} sigsuspend;
#if defined(STRESS_THERMAL_ZONES)
	tz_info_t *tz_info;				/* List of valid thermal zones */
#endif
	proc_stats_t stats[0];				/* Shared statistics */
} shared_t;

/* Stress test classes */
typedef struct {
	uint32_t class;			/* Class type bit mask */
	const char *name;		/* Name of class */
} class_t;

/* Stress tests */
typedef enum {
	STRESS_START = -1,
#if defined(__linux__) && NEED_GLIBC(2,3,0)
	__STRESS_AFFINITY,
#define STRESS_AFFINITY __STRESS_AFFINITY
#endif
#if defined(__linux__) && NEED_GLIBC(2,1,0)
	__STRESS_AIO,
#define STRESS_AIO __STRESS_AIO
#endif
#if defined(__linux__) &&		\
    defined(__NR_io_setup) &&		\
    defined(__NR_io_destroy) &&		\
    defined(__NR_io_submit) && 		\
    defined(__NR_io_getevents)
	__STRESS_AIO_LINUX,
#define STRESS_AIO_LINUX __STRESS_AIO_LINUX
#endif
	STRESS_BRK,
	STRESS_BSEARCH,
	STRESS_BIGHEAP,
	STRESS_CACHE,
	STRESS_CHDIR,
	STRESS_CHMOD,
#if _POSIX_C_SOURCE >= 199309L
	__STRESS_CLOCK,
#define STRESS_CLOCK __STRESS_CLOCK
#endif
#if defined(__linux__) && NEED_GLIBC(2,14,0)
	__STRESS_CLONE,
#define STRESS_CLONE __STRESS_CLONE
#endif
#if !defined(__OpenBSD__)
	__STRESS_CONTEXT,
#define STRESS_CONTEXT __STRESS_CONTEXT
#endif
	STRESS_CPU,
	STRESS_CRYPT,
	STRESS_DENTRY,
	STRESS_DIR,
	STRESS_DUP,
#if defined(__linux__) && NEED_GLIBC(2,3,2)
	__STRESS_EPOLL,
#define STRESS_EPOLL __STRESS_EPOLL
#endif
#if defined(__linux__) && NEED_GLIBC(2,8,0)
	__STRESS_EVENTFD,
#define STRESS_EVENTFD __STRESS_EVENTFD
#endif
#if (_XOPEN_SOURCE >= 600 || _POSIX_C_SOURCE >= 200112L) && NEED_GLIBC(2,10,0)
	__STRESS_FALLOCATE,
#define STRESS_FALLOCATE __STRESS_FALLOCATE
#endif
	STRESS_FAULT,
	STRESS_FCNTL,
	STRESS_FIFO,
	STRESS_FLOCK,
	STRESS_FORK,
	STRESS_FSTAT,
#if defined(__linux__) && defined(__NR_futex)
	__STRESS_FUTEX,
#define STRESS_FUTEX __STRESS_FUTEX
#endif
	STRESS_GET,
#if defined(__linux__) && defined(__NR_getrandom)
	__STRESS_GETRANDOM,
#define STRESS_GETRANDOM __STRESS_GETRANDOM
#endif
	STRESS_HDD,
	STRESS_HSEARCH,
#if defined(STRESS_X86) && defined(__GNUC__) && NEED_GNUC(4,6,0)
	__STRESS_ICACHE,
#define STRESS_ICACHE __STRESS_ICACHE
#endif
#if defined(__linux__) && NEED_GLIBC(2,9,0)
	__STRESS_INOTIFY,
#define STRESS_INOTIFY __STRESS_INOTIFY
#endif
	STRESS_IOSYNC,
	STRESS_ITIMER,
#if defined(__linux__) && defined(__NR_kcmp)
	__STRESS_KCMP,
#define STRESS_KCMP __STRESS_KCMP
#endif
#if defined(__linux__) && defined(__NR_add_key) && defined(__NR_keyctl)
	__STRESS_KEY,
#define STRESS_KEY __STRESS_KEY
#endif
	STRESS_KILL,
#if defined(F_SETLEASE) && defined(F_WRLCK) && defined(F_UNLCK)
	__STRESS_LEASE,
#define STRESS_LEASE __STRESS_LEASE
#endif
	STRESS_LINK,
#if _BSD_SOURCE || _SVID_SOURCE || _XOPEN_SOURCE >= 500 || \
     (_XOPEN_SOURCE && _XOPEN_SOURCE_EXTENDED)
	__STRESS_LOCKF,
#define STRESS_LOCKF __STRESS_LOCKF
#endif
	STRESS_LONGJMP,
	STRESS_LSEARCH,
	STRESS_MALLOC,
	STRESS_MATRIX,
	STRESS_MEMCPY,
#if defined(__linux__) && defined(__NR_memfd_create)
	__STRESS_MEMFD,
#define STRESS_MEMFD __STRESS_MEMFD
#endif
#if !defined(__gnu_hurd__) && NEED_GLIBC(2,2,0)
	__STRESS_MINCORE,
#define STRESS_MINCORE __STRESS_MINCORE
#endif
#if defined(_POSIX_MEMLOCK_RANGE)
	__STRESS_MLOCK,
#define STRESS_MLOCK __STRESS_MLOCK
#endif
	STRESS_MMAP,
#if defined(__linux__)
	__STRESS_MMAPFORK,
#define STRESS_MMAPFORK	__STRESS_MMAPFORK
#endif
	STRESS_MMAPMANY,
#if defined(__linux__) && NEED_GLIBC(2,4,0)
	__STRESS_MREMAP,
#define STRESS_MREMAP __STRESS_MREMAP
#endif
#if !defined(__gnu_hurd__) && NEED_GLIBC(2,0,0)
	__STRESS_MSG,
#define STRESS_MSG __STRESS_MSG
#endif
#if defined(__linux__)
	__STRESS_MQ,
#define STRESS_MQ __STRESS_MQ
#endif
	STRESS_NICE,
	STRESS_NULL,
#if defined(__linux__) &&		\
    defined(__NR_get_mempolicy) &&	\
    defined(__NR_mbind) &&		\
    defined(__NR_migrate_pages) &&	\
    defined(__NR_move_pages) &&		\
    defined(__NR_set_mempolicy)
	__STRESS_NUMA,
#define STRESS_NUMA __STRESS_NUMA
#endif
	STRESS_OPEN,
	STRESS_PIPE,
	STRESS_POLL,
#if defined(__linux__)
	__STRESS_PROCFS,
#define STRESS_PROCFS __STRESS_PROCFS
#endif
	STRESS_PTHREAD,
#if defined(__linux__)
	__STRESS_PTRACE,
#define STRESS_PTRACE __STRESS_PTRACE
#endif
	STRESS_QSORT,
#if defined(__linux__) && (		\
    defined(Q_GETQUOTA) ||		\
    defined(Q_GETFMT) ||		\
    defined(Q_GETINFO) ||		\
    defined(Q_GETSTATS) ||		\
    defined(Q_SYNC))
	__STRESS_QUOTA,
#define STRESS_QUOTA __STRESS_QUOTA
#endif
#if defined(STRESS_X86) && !defined(__OpenBSD__) && NEED_GNUC(4,6,0)
	__STRESS_RDRAND,
#define STRESS_RDRAND __STRESS_RDRAND
#endif
#if defined(__linux__) && NEED_GLIBC(2,3,0)
	__STRESS_READAHEAD,
#define STRESS_READAHEAD __STRESS_READAHEAD
#endif
	STRESS_RENAME,
#if defined(__linux__)
	__STRESS_RLIMIT,
#define STRESS_RLIMIT __STRESS_RLIMIT
#endif
	STRESS_SEEK,
#if defined(__linux__)
	__STRESS_SEMAPHORE_POSIX,
#define STRESS_SEMAPHORE_POSIX __STRESS_SEMAPHORE_POSIX
#endif
#if defined(__linux__)
	__STRESS_SEMAPHORE_SYSV,
#define STRESS_SEMAPHORE_SYSV __STRESS_SEMAPHORE_SYSV
#endif
#if defined(__linux__) && NEED_GLIBC(2,1,0)
	__STRESS_SENDFILE,
#define STRESS_SENDFILE __STRESS_SENDFILE
#endif
	STRESS_SHM_SYSV,
#if defined(__linux__) && NEED_GLIBC(2,8,0)
	__STRESS_SIGFD,
#define STRESS_SIGFD __STRESS_SIGFD
#endif
	STRESS_SIGFPE,
	STRESS_SIGPENDING,
#if _POSIX_C_SOURCE >= 199309L && !defined(__gnu_hurd__)
	__STRESS_SIGQUEUE,
#define STRESS_SIGQUEUE __STRESS_SIGQUEUE
#endif
	STRESS_SIGSEGV,
	STRESS_SIGSUSPEND,
	STRESS_SOCKET,
	STRESS_SOCKET_PAIR,
#if defined(__linux__) && NEED_GLIBC(2,5,0)
	__STRESS_SPLICE,
#define STRESS_SPLICE __STRESS_SPLICE
#endif
	STRESS_STACK,
	STRESS_STR,
	STRESS_SWITCH,
	STRESS_SYMLINK,
	STRESS_SYSINFO,
#if defined(__linux__)
	__STRESS_SYSFS,
#define STRESS_SYSFS __STRESS_SYSFS
#endif
#if defined(__linux__) && NEED_GLIBC(2,5,0)
	__STRESS_TEE,
#define STRESS_TEE __STRESS_TEE
#endif
#if defined(__linux__)
	__STRESS_TIMER,
#define STRESS_TIMER __STRESS_TIMER
#endif
#if defined(__linux__)
	__STRESS_TIMERFD,
#define STRESS_TIMERFD __STRESS_TIMERFD
#endif
	STRESS_TSEARCH,
	STRESS_UDP,
#if defined(AF_PACKET)
	__STRESS_UDP_FLOOD,
#define STRESS_UDP_FLOOD __STRESS_UDP_FLOOD
#endif
#if defined(__linux__) || defined(__gnu_hurd__)
	__STRESS_URANDOM,
#define STRESS_URANDOM __STRESS_URANDOM
#endif
	STRESS_UTIME,
#if defined(STRESS_VECTOR)
	__STRESS_VECMATH,
#define STRESS_VECMATH __STRESS_VECMATH
#endif
	__STRESS_VFORK,
#define STRESS_VFORK __STRESS_VFORK
	STRESS_VM,
#if defined(__linux__) && \
    defined(__NR_process_vm_readv) && defined(__NR_process_vm_writev) && \
    NEED_GLIBC(2,15,0)
	__STRESS_VM_RW,
#define STRESS_VM_RW __STRESS_VM_RW
#endif
#if defined(__linux__) && NEED_GLIBC(2,5,0)
	__STRESS_VM_SPLICE,
#define STRESS_VM_SPLICE __STRESS_VM_SPLICE
#endif
#if !defined(__gnu_hurd__) && !defined(__NetBSD__)
	__STRESS_WAIT,
#define STRESS_WAIT __STRESS_WAIT
#endif
	STRESS_WCS,
#if defined(__linux__)
	__STRESS_XATTR,
#define STRESS_XATTR __STRESS_XATTR
#endif
#if defined(_POSIX_PRIORITY_SCHEDULING)
	__STRESS_YIELD,
#define STRESS_YIELD __STRESS_YIELD
#endif
	STRESS_ZERO,
	STRESS_ZOMBIE,
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
#if defined(STRESS_FALLOCATE)
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
#if defined(STRESS_TIMER)
	OPT_TIMER = 'T',
#endif
#if defined(STRESS_URANDOM)
	OPT_URANDOM = 'u',
#endif
	OPT_VERBOSE = 'v',
	OPT_VERSION = 'V',
	OPT_YIELD = 'y',
	OPT_YAML = 'Y',
	OPT_EXCLUDE = 'x',

	/* Long options only */

	OPT_LONG_OPS_START = 0x7f,

#if defined(STRESS_AFFINITY)
	OPT_AFFINITY,
	OPT_AFFINITY_OPS,
	OPT_AFFINITY_RAND,
#endif

	OPT_AGGRESSIVE,

#if defined(STRESS_AIO)
	OPT_AIO,
	OPT_AIO_OPS,
	OPT_AIO_REQUESTS,
#endif

#if defined(STRESS_AIO_LINUX)
	OPT_AIO_LINUX,
	OPT_AIO_LINUX_OPS,
	OPT_AIO_LINUX_REQUESTS,
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
	OPT_CACHE_PREFETCH,
	OPT_CACHE_FLUSH,
	OPT_CACHE_FENCE,

	OPT_CHDIR,
	OPT_CHDIR_OPS,

	OPT_CHMOD,
	OPT_CHMOD_OPS,

#if defined(STRESS_CLOCK)
	OPT_CLOCK,
	OPT_CLOCK_OPS,
#endif

#if defined(STRESS_CLONE)
	OPT_CLONE,
	OPT_CLONE_OPS,
	OPT_CLONE_MAX,
#endif

#if defined(STRESS_CONTEXT)
	OPT_CONTEXT,
	OPT_CONTEXT_OPS,
#endif

	OPT_CPU_OPS,
	OPT_CPU_METHOD,
	OPT_CPU_LOAD_SLICE,

	OPT_CRYPT,
	OPT_CRYPT_OPS,

	OPT_DENTRY_OPS,
	OPT_DENTRIES,
	OPT_DENTRY_ORDER,

	OPT_DIR,
	OPT_DIR_OPS,

	OPT_DUP,
	OPT_DUP_OPS,

#if defined(STRESS_EPOLL)
	OPT_EPOLL,
	OPT_EPOLL_OPS,
	OPT_EPOLL_PORT,
	OPT_EPOLL_DOMAIN,
#endif

#if defined(STRESS_EVENTFD)
	OPT_EVENTFD,
	OPT_EVENTFD_OPS,
#endif

#if defined(STRESS_FALLOCATE)
	OPT_FALLOCATE_OPS,
	OPT_FALLOCATE_BYTES,
#endif
	OPT_FAULT,
	OPT_FAULT_OPS,

	OPT_FCNTL,
	OPT_FCNTL_OPS,

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

#if defined(STRESS_GETRANDOM)
	OPT_GETRANDOM,
	OPT_GETRANDOM_OPS,
#endif

	OPT_HDD_BYTES,
	OPT_HDD_WRITE_SIZE,
	OPT_HDD_OPS,
	OPT_HDD_OPTS,

	OPT_HSEARCH,
	OPT_HSEARCH_OPS,
	OPT_HSEARCH_SIZE,

	OPT_ICACHE,
	OPT_ICACHE_OPS,

#if defined(STRESS_INOTIFY)
	OPT_INOTIFY,
	OPT_INOTIFY_OPS,
#endif

#if defined(STRESS_IONICE)
	OPT_IONICE_CLASS,
	OPT_IONICE_LEVEL,
#endif

	OPT_IOSYNC_OPS,

	OPT_ITIMER,
	OPT_ITIMER_OPS,
	OPT_ITIMER_FREQ,

#if defined(STRESS_KCMP)
	OPT_KCMP,
	OPT_KCMP_OPS,
#endif

#if defined(STRESS_KEY)
	OPT_KEY,
	OPT_KEY_OPS,
#endif

	OPT_KILL,
	OPT_KILL_OPS,

#if defined(STRESS_LEASE)
	OPT_LEASE,
	OPT_LEASE_OPS,
	OPT_LEASE_BREAKERS,
#endif

	OPT_LINK,
	OPT_LINK_OPS,

#if defined(STRESS_LOCKF)
	OPT_LOCKF,
	OPT_LOCKF_OPS,
	OPT_LOCKF_NONBLOCK,
#endif

	OPT_LOG_BRIEF,

	OPT_LONGJMP,
	OPT_LONGJMP_OPS,

	OPT_LSEARCH,
	OPT_LSEARCH_OPS,
	OPT_LSEARCH_SIZE,

	OPT_MALLOC,
	OPT_MALLOC_OPS,
	OPT_MALLOC_BYTES,
	OPT_MALLOC_MAX,
#if defined(STRESS_MALLOPT)
	OPT_MALLOC_THRESHOLD,
#endif

	OPT_MATRIX,
	OPT_MATRIX_OPS,
	OPT_MATRIX_SIZE,
	OPT_MATRIX_METHOD,

	OPT_MAXIMIZE,

	OPT_MEMCPY,
	OPT_MEMCPY_OPS,

#if defined(STRESS_MEMFD)
	OPT_MEMFD,
	OPT_MEMFD_OPS,
#endif

	OPT_METRICS_BRIEF,

#if defined(STRESS_MINCORE)
	OPT_MINCORE,
	OPT_MINCORE_OPS,
	OPT_MINCORE_RAND,
#endif

	OPT_MINIMIZE,

#if defined(STRESS_MLOCK)
	OPT_MLOCK,
	OPT_MLOCK_OPS,
#endif

	OPT_MMAP,
	OPT_MMAP_OPS,
	OPT_MMAP_BYTES,
	OPT_MMAP_FILE,
	OPT_MMAP_ASYNC,
	OPT_MMAP_MPROTECT,

#if defined(__linux__)
	OPT_MMAPFORK,
	OPT_MMAPFORK_OPS,
#endif

	OPT_MMAPMANY,
	OPT_MMAPMANY_OPS,

#if defined(STRESS_MREMAP)
	OPT_MREMAP,
	OPT_MREMAP_OPS,
	OPT_MREMAP_BYTES,
#endif

	OPT_MSG,
	OPT_MSG_OPS,

#if defined(STRESS_MQ)
	OPT_MQ,
	OPT_MQ_OPS,
	OPT_MQ_SIZE,
#endif

	OPT_NICE,
	OPT_NICE_OPS,

	OPT_NO_MADVISE,

	OPT_NULL,
	OPT_NULL_OPS,

#if defined(STRESS_NUMA)
	OPT_NUMA,
	OPT_NUMA_OPS,
#endif

	OPT_OPEN_OPS,

#if defined(STRESS_PAGE_IN)
	OPT_PAGE_IN,
#endif

#if defined(STRESS_PERF_STATS)
	OPT_PERF_STATS,
#endif

	OPT_PIPE_OPS,

	OPT_POLL_OPS,

#if defined(STRESS_PROCFS)
	OPT_PROCFS,
	OPT_PROCFS_OPS,
#endif

	OPT_PTHREAD,
	OPT_PTHREAD_OPS,
	OPT_PTHREAD_MAX,

	OPT_PTRACE,
	OPT_PTRACE_OPS,

	OPT_QSORT,
	OPT_QSORT_OPS,
	OPT_QSORT_INTEGERS,

	OPT_QUOTA,
	OPT_QUOTA_OPS,

#if defined(STRESS_RDRAND)
	OPT_RDRAND,
	OPT_RDRAND_OPS,
#endif

#if defined(STRESS_READAHEAD)
	OPT_READAHEAD,
	OPT_READAHEAD_OPS,
	OPT_READAHEAD_BYTES,
#endif

	OPT_RENAME_OPS,

#if defined(STRESS_RLIMIT)
	OPT_RLIMIT,
	OPT_RLIMIT_OPS,
#endif

	OPT_SCHED,
	OPT_SCHED_PRIO,

	OPT_SEEK,
	OPT_SEEK_OPS,
	OPT_SEEK_SIZE,

#if defined(STRESS_SENDFILE)
	OPT_SENDFILE,
	OPT_SENDFILE_OPS,
	OPT_SENDFILE_SIZE,
#endif

	OPT_SEMAPHORE_POSIX,
	OPT_SEMAPHORE_POSIX_OPS,
	OPT_SEMAPHORE_POSIX_PROCS,

#if defined(STRESS_SEMAPHORE_SYSV)
	OPT_SEMAPHORE_SYSV,
	OPT_SEMAPHORE_SYSV_OPS,
	OPT_SEMAPHORE_SYSV_PROCS,
#endif

	OPT_SHM_SYSV,
	OPT_SHM_SYSV_OPS,
	OPT_SHM_SYSV_BYTES,
	OPT_SHM_SYSV_SEGMENTS,

	OPT_SEQUENTIAL,

#if defined(STRESS_SIGFD)
	OPT_SIGFD,
	OPT_SIGFD_OPS,
#endif

	OPT_SIGFPE,
	OPT_SIGFPE_OPS,

	OPT_SIGPENDING,
	OPT_SIGPENDING_OPS,

#if defined(STRESS_SIGQUEUE)
	OPT_SIGQUEUE,
	OPT_SIGQUEUE_OPS,
#endif

	OPT_SIGSEGV,
	OPT_SIGSEGV_OPS,

	OPT_SIGSUSPEND,
	OPT_SIGSUSPEND_OPS,

	OPT_SOCKET_OPS,
	OPT_SOCKET_PORT,
	OPT_SOCKET_DOMAIN,

	OPT_SOCKET_PAIR,
	OPT_SOCKET_PAIR_OPS,

	OPT_SWITCH_OPS,

#if defined(STRESS_SPLICE)
	OPT_SPLICE,
	OPT_SPLICE_OPS,
	OPT_SPLICE_BYTES,
#endif

	OPT_STACK,
	OPT_STACK_OPS,
	OPT_STACK_FILL,

	OPT_STR,
	OPT_STR_OPS,
	OPT_STR_METHOD,

	OPT_SYMLINK,
	OPT_SYMLINK_OPS,

	OPT_SYSINFO,
	OPT_SYSINFO_OPS,

#if defined(STRESS_SYSFS)
	OPT_SYSFS,
	OPT_SYSFS_OPS,
#endif

	OPT_SYSLOG,

#if defined(STRESS_TEE)
	OPT_TEE,
	OPT_TEE_OPS,
#endif

#if defined(STRESS_TIMER)
	OPT_TIMER_OPS,
	OPT_TIMER_FREQ,
	OPT_TIMER_RAND,
#endif

#if defined(STRESS_TIMERFD)
	OPT_TIMERFD,
	OPT_TIMERFD_OPS,
	OPT_TIMERFD_FREQ,
	OPT_TIMERFD_RAND,
#endif

	OPT_THERMAL_ZONES,

	OPT_TSEARCH,
	OPT_TSEARCH_OPS,
	OPT_TSEARCH_SIZE,

	OPT_TIMES,

	OPT_UDP,
	OPT_UDP_OPS,
	OPT_UDP_PORT,
	OPT_UDP_DOMAIN,

#if defined(STRESS_UDP_FLOOD)
	OPT_UDP_FLOOD,
	OPT_UDP_FLOOD_OPS,
	OPT_UDP_FLOOD_DOMAIN,
#endif

#if defined(STRESS_URANDOM)
	OPT_URANDOM_OPS,
#endif
	OPT_UTIME,
	OPT_UTIME_OPS,
	OPT_UTIME_FSYNC,

#if defined(STRESS_VECMATH)
	OPT_VECMATH,
	OPT_VECMATH_OPS,
#endif

	OPT_VERIFY,

#if defined(STRESS_VFORK)
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

#if defined(STRESS_VM_RW)
	OPT_VM_RW,
	OPT_VM_RW_OPS,
	OPT_VM_RW_BYTES,
#endif

#if defined(STRESS_VM_SPLICE)
	OPT_VM_SPLICE,
	OPT_VM_SPLICE_OPS,
	OPT_VM_SPLICE_BYTES,
#endif

#if defined(STRESS_WAIT)
	OPT_WAIT,
	OPT_WAIT_OPS,
#endif

	OPT_WCS,
	OPT_WCS_OPS,
	OPT_WCS_METHOD,

#if defined(STRESS_XATTR)
	OPT_XATTR,
	OPT_XATTR_OPS,
#endif

#if defined(STRESS_YIELD)
	OPT_YIELD_OPS,
#endif

	OPT_ZERO,
	OPT_ZERO_OPS,

	OPT_ZOMBIE,
	OPT_ZOMBIE_OPS,
	OPT_ZOMBIE_MAX,
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
	bool	exclude;		/* true if excluded */
} proc_info_t;

typedef struct {
	const char	ch;		/* Scaling suffix */
	const uint64_t	scale;		/* Amount to scale by */
} scale_t;

/* Various option settings and flags */
extern const char *app_name;		/* Name of application */
extern shared_t *shared;		/* shared memory */
extern uint64_t	opt_timeout;		/* timeout in seconds */
extern uint64_t	opt_flags;		/* option flags */
extern int32_t opt_sequential;		/* Number of sequential iterations */
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
extern void stress_cwd_readwriteable(void);

extern const char *stress_strsignal(const int signum);

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

#define clflush(ptr)	do { } while (0) /* No-op */
#define mfence()	do { } while (0) /* No-op */

#endif

/*
 *  mwc32()
 *      Multiply-with-carry random numbers
 *      fast pseudo random number generator, see
 *      http://www.cse.yorku.ca/~oz/marsaglia-rng.html
 */
static inline uint32_t mwc32(void)
{
	__mwc.z = 36969 * (__mwc.z & 65535) + (__mwc.z >> 16);
	__mwc.w = 18000 * (__mwc.w & 65535) + (__mwc.w >> 16);
	return (__mwc.z << 16) + __mwc.w;
}

static inline uint64_t mwc64(void)
{
	return (((uint64_t)mwc32()) << 32) | mwc32();
}

static inline uint16_t mwc16(void)
{
	return mwc32() & 0xffff;
}

static inline uint8_t mwc8(void)
{
	return mwc32() & 0xff;
}

extern void mwc_seed(const uint32_t w, const uint32_t z);
extern void mwc_reseed(void);

/* Time handling */

/*
 *  timeval_to_double()
 *      convert timeval to seconds as a double
 */
static inline double timeval_to_double(const struct timeval *tv)
{
        return (double)tv->tv_sec + ((double)tv->tv_usec / 1000000.0);
}

extern int stressor_instances(const stress_id id);

#if defined(STRESS_PERF_STATS)
/* Perf stats */
extern int perf_open(stress_perf_t *sp);
extern int perf_enable(stress_perf_t *sp);
extern int perf_disable(stress_perf_t *sp);
extern int perf_close(stress_perf_t *sp);
extern int perf_get_counter_by_index(const stress_perf_t *sp, const int index, uint64_t *counter, int *id);
extern int perf_get_counter_by_id(const stress_perf_t *sp, int id, uint64_t *counter, int *index);
extern bool perf_stat_succeeded(const stress_perf_t *sp);
extern const char *perf_get_label_by_index(const int i);
extern const char *perf_stat_scale(const uint64_t counter, const double duration);
extern void perf_stat_dump(FILE *yaml, const stress_t stressors[], const proc_info_t procs[STRESS_MAX],
	const int32_t max_procs, const double duration);
extern void perf_init(void);
#endif

extern double time_now(void);
extern const char *duration_to_str(const double duration);

/* Misc settings helpers */
extern void set_oom_adjustment(const char *name, const bool killable);
extern void set_sched(const int32_t sched, const int32_t sched_priority);
extern void set_iopriority(const int32_t class, const int32_t level);
extern void set_coredump(const char *name);
extern void set_proc_name(const char *name);

/* Memory locking */
extern int stress_mlock_region(void *addr_start, void *addr_end);

/* Argument parsing and range checking */
extern int32_t get_opt_sched(const char *const str);
extern int32_t get_opt_ionice_class(const char *const str);
extern int32_t get_int32(const char *const str);
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
extern long stress_get_processors_configured(void);
extern long stress_get_ticks_per_second(void);
extern void set_max_limits(void);

/* Memory tweaking */
extern int madvise_random(void *addr, const size_t length);
extern int mincore_touch_pages(void *buf, const size_t buf_len);

/* Mounts */
extern void mount_free(char *mnts[], const int n);
extern int mount_get(char *mnts[], const int max);

#if defined(STRESS_THERMAL_ZONES)
/* thermal zones */
extern int tz_init(tz_info_t **tz_info_list);
extern void tz_free(tz_info_t **tz_info_list);
extern int tz_get_temperatures(tz_info_t **tz_info_list, stress_tz_t *tz);
extern void tz_dump(FILE *fp, const shared_t *shared, const stress_t stressors[],
	const proc_info_t procs[STRESS_MAX], const int32_t max_procs);
#endif

/* Network helpers */
extern void stress_set_net_port(const char *optname, const char *optarg,
	const int min_port, const int max_port, int *port);
extern int stress_set_net_domain(const int domain_mask, const char *name, const char *domain_name, int *domain);
extern void stress_set_sockaddr(const char *name, const uint32_t instance,
	const pid_t ppid, const int domain, const int port,
	struct sockaddr **sockaddr, socklen_t *len);
extern void stress_set_sockaddr_port(const int domain, const int port, struct sockaddr *sockaddr);

extern void stress_semaphore_posix_init(void);
extern void stress_semaphore_posix_destroy(void);

extern void stress_semaphore_sysv_init(void);
extern void stress_semaphore_sysv_destroy(void);

/* Used to set options for specific stressors */
extern void stress_adjust_ptread_max(uint64_t max);
extern void stress_set_aio_requests(const char *optarg);
extern void stress_set_aio_linux_requests(const char *optarg);
extern void stress_set_bigheap_growth(const char *optarg);
extern void stress_set_bsearch_size(const char *optarg);
extern void stress_set_clone_max(const char *optarg);
extern void stress_set_cpu_load(const char *optarg);
extern void stress_set_cpu_load_slice(const char *optarg);
extern int  stress_set_cpu_method(const char *name);
extern void stress_set_dentries(const char *optarg);
extern int  stress_set_dentry_order(const char *optarg);
extern void stress_set_epoll_port(const char *optarg);
extern int  stress_set_epoll_domain(const char *optarg);
extern void stress_set_fallocate_bytes(const char *optarg);
extern void stress_set_fifo_readers(const char *optarg);
extern void stress_set_fork_max(const char *optarg);
extern void stress_set_fstat_dir(const char *optarg);
extern void stress_set_hdd_bytes(const char *optarg);
extern int  stress_hdd_opts(char *opts);
extern void stress_set_hdd_write_size(const char *optarg);
extern void stress_set_hsearch_size(const char *optarg);
extern void stress_set_itimer_freq(const char *optarg);
extern void stress_set_lease_breakers(const char *optarg);
extern void stress_set_lsearch_size(const char *optarg);
extern void stress_set_malloc_bytes(const char *optarg);
extern void stress_set_malloc_max(const char *optarg);
extern void stress_set_malloc_threshold(const char *optarg);
extern int  stress_set_matrix_method(const char *name);
extern void stress_set_matrix_size(const char *optarg);
extern void stress_set_mmap_bytes(const char *optarg);
extern void stress_set_mremap_bytes(const char *optarg);
extern void stress_set_mq_size(const char *optarg);
extern void stress_set_pthread_max(const char *optarg);
extern void stress_set_qsort_size(const void *optarg);
extern int  stress_rdrand_supported(void);
extern void stress_set_readahead_bytes(const char *optarg);
extern void stress_set_seek_size(const char *optarg);
extern void stress_set_sendfile_size(const char *optarg);
extern void stress_set_semaphore_posix_procs(const char *optarg);
extern void stress_set_semaphore_sysv_procs(const char *optarg);
extern void stress_set_shm_sysv_bytes(const char *optarg);
extern void stress_set_shm_sysv_segments(const char *optarg);
extern int  stress_set_socket_domain(const char *name);
extern void stress_set_socket_port(const char *optarg);
extern void stress_set_splice_bytes(const char *optarg);
extern int  stress_set_str_method(const char *name);
extern int  stress_set_wcs_method(const char *name);
extern void stress_set_timer_freq(const char *optarg);
extern void stress_set_timerfd_freq(const char *optarg);
extern void stress_set_tsearch_size(const char *optarg);
extern int  stress_set_udp_domain(const char *name);
extern void stress_set_udp_port(const char *optarg);
extern int  stress_set_udp_flood_domain(const char *name);
extern void stress_set_vfork_max(const char *optarg);
extern void stress_set_vm_bytes(const char *optarg);
extern void stress_set_vm_flags(const int flag);
extern void stress_set_vm_hang(const char *optarg);
extern int  stress_set_vm_method(const char *name);
extern void stress_set_vm_rw_bytes(const char *optarg);
extern void stress_set_vm_splice_bytes(const char *optarg);
extern void stress_set_zombie_max(const char *optarg);

#define STRESS(name)							\
extern int name(uint64_t *const counter, const uint32_t instance,	\
        const uint64_t max_ops, const char *name)

/* Stressors */
STRESS(stress_affinity);
STRESS(stress_aio);
STRESS(stress_aio_linux);
STRESS(stress_bigheap);
STRESS(stress_brk);
STRESS(stress_bsearch);
STRESS(stress_cache);
STRESS(stress_chdir);
STRESS(stress_chmod);
STRESS(stress_clock);
STRESS(stress_clone);
STRESS(stress_context);
STRESS(stress_cpu);
STRESS(stress_crypt);
STRESS(stress_dentry);
STRESS(stress_dir);
STRESS(stress_dup);
STRESS(stress_epoll);
STRESS(stress_eventfd);
STRESS(stress_fallocate);
STRESS(stress_fault);
STRESS(stress_fcntl);
STRESS(stress_fifo);
STRESS(stress_flock);
STRESS(stress_fork);
STRESS(stress_fstat);
STRESS(stress_futex);
STRESS(stress_get);
STRESS(stress_getrandom);
STRESS(stress_hdd);
STRESS(stress_hsearch);
STRESS(stress_icache);
STRESS(stress_inotify);
STRESS(stress_iosync);
STRESS(stress_itimer);
STRESS(stress_kcmp);
STRESS(stress_key);
STRESS(stress_kill);
STRESS(stress_lease);
STRESS(stress_link);
STRESS(stress_lockf);
STRESS(stress_longjmp);
STRESS(stress_lsearch);
STRESS(stress_malloc);
STRESS(stress_matrix);
STRESS(stress_memcpy);
STRESS(stress_memfd);
STRESS(stress_mincore);
STRESS(stress_mlock);
STRESS(stress_mmap);
STRESS(stress_mmapfork);
STRESS(stress_mmapmany);
STRESS(stress_mremap);
STRESS(stress_msg);
STRESS(stress_mq);
STRESS(stress_nice);
STRESS(stress_noop);
STRESS(stress_null);
STRESS(stress_numa);
STRESS(stress_open);
STRESS(stress_pipe);
STRESS(stress_poll);
STRESS(stress_procfs);
STRESS(stress_pthread);
STRESS(stress_ptrace);
STRESS(stress_qsort);
STRESS(stress_quota);
STRESS(stress_rdrand);
STRESS(stress_readahead);
STRESS(stress_rename);
STRESS(stress_rlimit);
STRESS(stress_seek);
STRESS(stress_sem_posix);
STRESS(stress_sem_sysv);
STRESS(stress_shm_sysv);
STRESS(stress_sendfile);
STRESS(stress_sigfd);
STRESS(stress_sigfpe);
STRESS(stress_sigpending);
STRESS(stress_sigsegv);
STRESS(stress_sigsuspend);
STRESS(stress_sigq);
STRESS(stress_socket);
STRESS(stress_socket_pair);
STRESS(stress_splice);
STRESS(stress_stack);
STRESS(stress_str);
STRESS(stress_switch);
STRESS(stress_symlink);
STRESS(stress_sysinfo);
STRESS(stress_sysfs);
STRESS(stress_tee);
STRESS(stress_timer);
STRESS(stress_timerfd);
STRESS(stress_tsearch);
STRESS(stress_udp);
STRESS(stress_udp_flood);
STRESS(stress_urandom);
STRESS(stress_utime);
STRESS(stress_vecmath);
STRESS(stress_vfork);
STRESS(stress_vm);
STRESS(stress_vm_rw);
STRESS(stress_vm_splice);
STRESS(stress_wait);
STRESS(stress_wcs);
STRESS(stress_xattr);
STRESS(stress_yield);
STRESS(stress_zero);
STRESS(stress_zombie);

#endif
