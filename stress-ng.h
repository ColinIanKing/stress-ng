/*
 * Copyright (C) 2013-2017 Canonical, Ltd.
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

#define _GNU_SOURCE

/* Some Solaris tool chains only define __sun */
#if defined(__sun) && !defined(__sun__)
#define __sun__
#endif

#include <stdarg.h>
#include <stdint.h>
#include <stdlib.h>
#include <stddef.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <inttypes.h>
#include <ctype.h>
#include <dirent.h>
#include <limits.h>
#include <setjmp.h>
#include <semaphore.h>
#include <sched.h>
#if defined(__GNUC__) && defined(__linux__)
#include <malloc.h>
#endif
#if defined(HAVE_LIB_PTHREAD)
#include <pthread.h>
#endif
#include <signal.h>
#include <time.h>
#include <sys/file.h>
#include <sys/wait.h>
#include <sys/time.h>
#include <sys/times.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <sys/resource.h>
#if defined (__linux__)
#include <sys/mount.h>
#include <sys/syscall.h>
#include <sys/quota.h>
#include <sys/prctl.h>
#include <netinet/in.h>
#include <linux/fs.h>
#include <linux/fiemap.h>
#include <linux/posix_types.h>
#endif
#include <fcntl.h>
#include <errno.h>
#if defined (__GLIBC__)
#include <features.h>
#endif
#if defined (__sun__)
#include <alloca.h>
#include <strings.h>
#endif

#if defined (__linux__)
/*
 *  BeagleBoneBlack with 4.1.15 kernel does not
 *  define the following, these should be defined
 *  in linux/posix_types.h - define them just in
 *  case they don't exist.
 */
#ifndef __kernel_long_t
typedef long int __kernel_long_t;
typedef unsigned long int __kernel_ulong_t;
#endif
#endif

#define EXIT_NOT_SUCCESS	(2)
#define EXIT_NO_RESOURCE	(3)
#define EXIT_NOT_IMPLEMENTED	(4)

/*
 * STRESS_ASSERT(test)
 *   throw compile time error if test not true
 */
#define STRESS_CONCAT(a, b) a ## b
#define STRESS_CONCAT_EXPAND(a, b) STRESS_CONCAT(a, b)
#define STRESS_ASSERT(expr) \
	enum { STRESS_CONCAT_EXPAND(STRESS_ASSERT_AT_LINE_, __LINE__) = \
		1 / !!(expr) };

#define STRESS_MINIMUM(a,b) (((a) < (b)) ? (a) : (b))
#define STRESS_MAXIMUM(a,b) (((a) > (b)) ? (a) : (b))

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

/* NetBSD does not define MAP_ANONYMOUS */
#if defined(MAP_ANON) && !defined(MAP_ANONYMOUS)
#define MAP_ANONYMOUS MAP_ANON
#endif

/* GNU HURD */
#ifndef PATH_MAX
#define PATH_MAX 		(4096)		/* Some systems don't define this */
#endif

/*
 * making local static fixes globbering warnings on older gcc versions
 */
#if defined(__GNUC__) || defined(__clang__)
#define NOCLOBBER	static
#else
#define NOCLOBBER
#endif

/* Specific compilers have __uint128_t type support */
#if defined(__GNUC__) && !defined(__clang__) && defined(__SIZEOF_INT128__)
#define STRESS_INT128	1
#endif

/* Linux supports ionice */
#if defined(__linux__)
#define STRESS_IONICE
#endif

#if (_BSD_SOURCE || _SVID_SOURCE || !defined(__gnu_hurd__))
#define STRESS_PAGE_IN
#endif


#define STRESS_FD_MAX		(65536)		/* Max fds if we can't figure it out */
#define STRESS_PROCS_MAX	(4096)		/* Max number of processes per stressor */

#define DCCP_BUF		(1024)		/* DCCP I/O buffer size */
#define SOCKET_BUF		(8192)		/* Socket I/O buffer size */
#define UDP_BUF			(1024)		/* UDP I/O buffer size */
#define SOCKET_PAIR_BUF		(64)		/* Socket pair I/O buffer size */

#define ABORT_FAILURES		(5)		/* Number of failures before we abort */

/* debug output bitmasks */
#define PR_ERROR		 0x0000000000001ULL 	/* Print errors */
#define PR_INFO			 0x0000000000002ULL 	/* Print info */
#define PR_DEBUG		 0x0000000000004ULL 	/* Print debug */
#define PR_FAIL			 0x0000000000008ULL 	/* Print test failure message */
#define PR_ALL			 (PR_ERROR | PR_INFO | PR_DEBUG | PR_FAIL)

/* Option bit masks */
#define OPT_FLAGS_AFFINITY_RAND	 0x0000000000010ULL	/* Change affinity randomly */
#define OPT_FLAGS_DRY_RUN	 0x0000000000020ULL	/* Don't actually run */
#define OPT_FLAGS_METRICS	 0x0000000000040ULL	/* Dump metrics at end */
#define OPT_FLAGS_VM_KEEP	 0x0000000000080ULL	/* Don't keep re-allocating */
#define OPT_FLAGS_RANDOM	 0x0000000000100ULL	/* Randomize */
#define OPT_FLAGS_SET		 0x0000000000200ULL	/* Set if user specifies stress procs */
#define OPT_FLAGS_KEEP_NAME	 0x0000000000400ULL	/* Keep stress names to stress-ng */
#define OPT_FLAGS_UTIME_FSYNC	 0x0000000000800ULL	/* fsync after utime modification */
#define OPT_FLAGS_METRICS_BRIEF	 0x0000000001000ULL	/* dump brief metrics */
#define OPT_FLAGS_VERIFY	 0x0000000002000ULL	/* verify mode */
#define OPT_FLAGS_MMAP_MADVISE	 0x0000000004000ULL	/* enable random madvise settings */
#define OPT_FLAGS_MMAP_MINCORE	 0x0000000008000ULL	/* mincore force pages into mem */
#define OPT_FLAGS_TIMES		 0x0000000010000ULL	/* user/system time summary */
#define OPT_FLAGS_CACHE_PREFETCH 0x0000000020000ULL 	/* cache prefetch */
#define OPT_FLAGS_CACHE_FLUSH	 0x0000000040000ULL	/* cache flush */
#define OPT_FLAGS_CACHE_FENCE	 0x0000000080000ULL	/* cache fence */
#define OPT_FLAGS_CACHE_MASK	(OPT_FLAGS_CACHE_FLUSH | \
				 OPT_FLAGS_CACHE_FENCE | \
				 OPT_FLAGS_CACHE_PREFETCH)
#define OPT_FLAGS_MMAP_FILE	 0x0000000100000ULL	/* mmap onto a file */
#define OPT_FLAGS_MMAP_ASYNC	 0x0000000200000ULL	/* mmap file asynchronous I/O */
#define OPT_FLAGS_MMAP_MPROTECT	 0x0000000400000ULL	/* mmap mprotect enabled */
#define OPT_FLAGS_LOCKF_NONBLK	 0x0000000800000ULL	/* Non-blocking lockf */
#define OPT_FLAGS_MINCORE_RAND	 0x0000001000000ULL	/* mincore randomize */
#define OPT_FLAGS_BRK_NOTOUCH	 0x0000002000000ULL	/* brk, don't touch page */
#define OPT_FLAGS_HDD_SYNC	 0x0000004000000ULL	/* HDD O_SYNC */
#define OPT_FLAGS_HDD_DSYNC	 0x0000008000000ULL	/* HDD O_DYNC */
#define OPT_FLAGS_HDD_DIRECT	 0x0000010000000ULL	/* HDD O_DIRECT */
#define OPT_FLAGS_HDD_NOATIME	 0x0000020000000ULL	/* HDD O_NOATIME */
#define OPT_FLAGS_STACK_FILL	 0x0000040000000ULL	/* Fill stack */
#define OPT_FLAGS_MINIMIZE	 0x0000080000000ULL	/* Minimize */
#define OPT_FLAGS_MAXIMIZE	 0x0000100000000ULL	/* Maximize */
#define OPT_FLAGS_MINMAX_MASK	 (OPT_FLAGS_MINIMIZE | OPT_FLAGS_MAXIMIZE)
#define OPT_FLAGS_SYSLOG	 0x0000200000000ULL	/* log test progress to syslog */
#define OPT_FLAGS_AGGRESSIVE	 0x0000400000000ULL	/* aggressive mode enabled */
#define OPT_FLAGS_TIMER_RAND	 0x0000800000000ULL	/* Enable random timer freq */
#define OPT_FLAGS_TIMERFD_RAND	 0x0001000000000ULL	/* Enable random timerfd freq */
#define OPT_FLAGS_ALL		 0x0002000000000ULL	/* --all mode */
#define OPT_FLAGS_SEQUENTIAL	 0x0004000000000ULL	/* --sequential mode */
#define OPT_FLAGS_PERF_STATS	 0x0008000000000ULL	/* --perf stats mode */
#define OPT_FLAGS_LOG_BRIEF	 0x0010000000000ULL	/* --log-brief */
#define OPT_FLAGS_THERMAL_ZONES  0x0020000000000ULL	/* --tz thermal zones */
#define OPT_FLAGS_TIMER_SLACK	 0x0040000000000ULL	/* --timer-slack */
#define OPT_FLAGS_SOCKET_NODELAY 0x0080000000000ULL	/* --sock-nodelay */
#define OPT_FLAGS_UDP_LITE	 0x0100000000000ULL	/* --udp-lite */
#define OPT_FLAGS_SEEK_PUNCH	 0x0200000000000ULL	/* --seek-punch */
#define OPT_FLAGS_CACHE_NOAFF	 0x0400000000000ULL	/* disable CPU affinity */
#define OPT_FLAGS_IGNITE_CPU	 0x0800000000000ULL	/* --cpu-ignite */
#define OPT_FLAGS_PATHOLOGICAL	 0x1000000000000ULL	/* --pathological */
#define OPT_FLAGS_NO_RAND_SEED	 0x2000000000000ULL	/* --no-rand-seed */
#define OPT_FLAGS_THRASH	 0x4000000000000ULL	/* --thrash */

#define OPT_FLAGS_AGGRESSIVE_MASK \
	(OPT_FLAGS_AFFINITY_RAND | OPT_FLAGS_UTIME_FSYNC | \
	 OPT_FLAGS_MMAP_MADVISE | OPT_FLAGS_MMAP_MINCORE | \
	 OPT_FLAGS_CACHE_FLUSH | OPT_FLAGS_CACHE_FENCE |   \
	 OPT_FLAGS_MMAP_FILE | OPT_FLAGS_MMAP_ASYNC |      \
	 OPT_FLAGS_MMAP_MPROTECT | OPT_FLAGS_LOCKF_NONBLK |\
	 OPT_FLAGS_MINCORE_RAND | OPT_FLAGS_HDD_SYNC |     \
	 OPT_FLAGS_HDD_DSYNC | OPT_FLAGS_HDD_DIRECT |      \
	 OPT_FLAGS_STACK_FILL | OPT_FLAGS_CACHE_PREFETCH | \
	 OPT_FLAGS_AGGRESSIVE | OPT_FLAGS_IGNITE_CPU)

#define WARN_ONCE_NO_CACHE	0x00000001	/* No /sys/../cpu0/cache */
#define WARN_ONCE_CACHE_DEFAULT	0x00000002	/* default cache size */
#define WARN_ONCE_CACHE_NONE	0x00000004	/* no cache info */
#define WARN_ONCE_CACHE_WAY	0x00000008	/* cache way too high */
#define WARN_ONCE_CACHE_SIZE	0x00000010	/* cache size info */
#define WARN_ONCE_CACHE_REDUCED	0x00000020	/* reduced cache */


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
#define CLASS_SECURITY		0x00001000	/* security APIs */
#define CLASS_PATHOLOGICAL	0x00002000	/* can hang a machine */

/* Network domains flags */
#define DOMAIN_INET		0x00000001	/* AF_INET */
#define DOMAIN_INET6		0x00000002	/* AF_INET6 */
#define DOMAIN_UNIX		0x00000004	/* AF_UNIX */

#define DOMAIN_INET_ALL		(DOMAIN_INET | DOMAIN_INET6)
#define DOMAIN_ALL		(DOMAIN_INET | DOMAIN_INET6 | DOMAIN_UNIX)

/* Large prime to stride around large VM regions */
#define PRIME_64		(0x8f0000000017116dULL)

/* stressor args */
typedef struct {
	uint64_t *const counter;	/* stressor counter */
	const char *name;		/* stressor name */
	const uint64_t max_ops;		/* max number of bogo ops */
	const uint32_t instance;	/* stressor instance # */
	pid_t pid;			/* stressor pid */
	pid_t ppid;			/* stressor ppid */
	size_t page_size;		/* page size */
} args_t;

/* pthread wrapped args_t */
typedef struct {
	const args_t *args;
} pthread_args_t;

/* gcc 4.7 and later support vector ops */
#if defined(__GNUC__) && NEED_GNUC(4,7,0)
#define STRESS_VECTOR	1
#endif

/* gcc 7.0 and later support __attribute__((fallthrough)); */
#if defined(__GNUC__) && NEED_GNUC(7,0,0)
#define CASE_FALLTHROUGH __attribute__((fallthrough))
#else
#define CASE_FALLTHROUGH
#endif

#if defined(__GNUC__) && NEED_GNUC(2,5,0)
#define NORETURN 	__attribute__ ((noreturn))
#else
#define NORETURN
#endif

#if defined(__GNUC__) && NEED_GNUC(3,4,0)	/* or possibly earier */
#define ALWAYS_INLINE	__attribute__ ((always_inline))
#else
#define ALWAYS_INLINE
#endif

/* -O3 attribute support */
#if defined(__GNUC__) && !defined(__clang__) && NEED_GNUC(4,6,0)
#define OPTIMIZE3 	__attribute__((optimize("-O3")))
#else
#define OPTIMIZE3
#endif

/* warn unused attribute */
#if defined(__GNUC__) && NEED_GNUC(4,2,0)
#define WARN_UNUSED	__attribute__((warn_unused_result))
#else
#define WARN_UNUSED
#endif

/* Force aligment to nearest cache line */
#if defined(__GNUC__) &&  NEED_GNUC(3,3,0)
#define ALIGN64		__attribute__ ((aligned(64)))
#else
#define ALIGN64
#endif

/* GCC hot attribute */
#if defined(__GNUC__) && NEED_GNUC(4,6,0)
#define HOT		__attribute__ ((hot))
#else
#define HOT
#endif

/* GCC mlocked section attribute */
#if defined(__GNUC__) && NEED_GNUC(4,6,0) && !defined(__sun__)
#define MLOCKED		__attribute__((__section__("mlocked")))
#define MLOCKED_SECTION 1
#else
#define MLOCKED
#endif

#if defined(__GNUC__) && NEED_GNUC(3,2,0)
#define FORMAT(func, a, b) __attribute__((format(func, a, b)))
#else
#define FORMAT(func, a, b)
#endif

/* restrict keyword */
#if defined(__GNUC__) || defined(__clang__)
#define RESTRICT __restrict
#else
#define RESTRICT
#endif

/* optimisation on branching */
#if defined(__GNUC__)
#define LIKELY(x)	__builtin_expect((x),1)
#define UNLIKELY(x)	__builtin_expect((x),0)
#else
#define LIKELY(x)	(x)
#define UNLIKELY(x)	(x)
#endif

/* waste some cycles */
#if defined(__GNUC__) || defined(__clang__)
#  if defined(HAVE_ASM_NOP)
#    define FORCE_DO_NOTHING() __asm__ __volatile__("nop;")
#  else
#    define FORCE_DO_NOTHING() __asm__ __volatile__("")
#  endif
#else
#  define FORCE_DO_NOTHING() while (0)
#endif

/* Logging helpers */
extern int pr_msg(FILE *fp, const uint64_t flag,
	const char *const fmt, va_list va);
extern void pr_msg_fail(const uint64_t flag, const char *name, const char *what, const int err);
extern int pr_yaml(FILE *fp, const char *const fmt, ...) __attribute__((format(printf, 2, 3)));
extern void pr_yaml_runinfo(FILE *fp);
extern void pr_openlog(const char *filename);
extern void pr_closelog(void);

extern void pr_dbg(const char *fmt, ...)  FORMAT(printf, 1, 2);
extern void pr_inf(const char *fmt, ...)  FORMAT(printf, 1, 2);
extern void pr_err(const char *fmt, ...)  FORMAT(printf, 1, 2);
extern void pr_fail(const char *fmt, ...) FORMAT(printf, 1, 2);
extern void pr_tidy(const char *fmt, ...) FORMAT(printf, 1, 2);

extern void pr_fail_err__(const args_t *args, const char *msg);
extern void pr_fail_errno__(const args_t *args, const char *msg, int err);
extern void pr_fail_dbg__(const args_t *args, const char *msg);

#define pr_fail_err(msg)		pr_fail_err__(args, msg)
#define pr_fail_errno(msg, err)		pr_fail_errno__(args, msg, err)
#define pr_fail_dbg(msg)		pr_fail_dbg__(args, msg)

/* Memory size constants */
#define KB			(1ULL << 10)
#define	MB			(1ULL << 20)
#define GB			(1ULL << 30)
#define TB			(1ULL << 40)
#define PB			(1ULL << 50)
#define EB			(1ULL << 60)

#define PAGE_4K_SHIFT		(12)
#define PAGE_4K			(1 << PAGE_4K_SHIFT)

#define STACK_ALIGNMENT		(64)	/* thread stacks align to 64 bytes */

#define MIN_OPS			(1ULL)
#define MAX_OPS			(100000000ULL)
#define MAX_32			(0xffffffffUL)
#define MAX_48			(0xffffffffffffULL)
#define MAX_64			(0xffffffffffffffffULL)

/* Maximum memory limits, 256TB for 64 bit is good enough for 2017 */
#if UINTPTR_MAX == MAX_32
#define MAX_MEM_LIMIT		MAX_32
#else
#define MAX_MEM_LIMIT		MAX_48
#endif

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

#define MIN_COPY_FILE_BYTES	(128 * MB)
#define MAX_COPY_FILE_BYTES	(256ULL * GB)
#define DEFAULT_COPY_FILE_BYTES	(256 * MB)
#define DEFAULT_COPY_FILE_SIZE  (2 * MB)

#define MIN_DCCP_PORT		(1024)
#define MAX_DCCP_PORT		(65535)
#define DEFAULT_DCCP_PORT	(10000)

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

#define MIN_FIEMAP_SIZE		(1 * MB)
#if UINTPTR_MAX == MAX_32
#define MAX_FIEMAP_SIZE		(0xffffe00)
#else
#define MAX_FIEMAP_SIZE		(256ULL * GB)
#endif
#define DEFAULT_FIEMAP_SIZE	(16 * MB)

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

#define MIN_EXECS		(1)
#define MAX_EXECS		(16000)
#define DEFAULT_EXECS		(1)

#define MIN_FORKS		(1)
#define MAX_FORKS		(16000)
#define DEFAULT_FORKS		(1)

#define MIN_HEAPSORT_SIZE	(1 * KB)
#define MAX_HEAPSORT_SIZE	(4 * MB)
#define DEFAULT_HEAPSORT_SIZE	(256 * KB)

#define MIN_IOMIX_BYTES		(1 * MB)
#define MAX_IOMIX_BYTES		(256ULL * GB)
#define DEFAULT_IOMIX_BYTES	(1 * GB)

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

#define MIN_MEMFD_BYTES		(2 * MB)
#if UINTPTR_MAX == MAX_32
#define MAX_MEMFD_BYTES		(MAX_32)
#else
#define MAX_MEMFD_BYTES		(4 * GB)
#endif
#define DEFAULT_MEMFD_BYTES	(256 * MB)

#define MIN_MERGESORT_SIZE	(1 * KB)
#define MAX_MERGESORT_SIZE	(4 * MB)
#define DEFAULT_MERGESORT_SIZE	(256 * KB)

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

#define MIN_MSYNC_BYTES		(1 * MB)	/* MUST NOT BE page size or less! */
#if UINTPTR_MAX == MAX_32
#define MAX_MSYNC_BYTES		(MAX_32)
#else
#define MAX_MSYNC_BYTES		(4 * GB)
#endif
#define DEFAULT_MSYNC_BYTES	(256 * MB)

#define MIN_PTHREAD		(1)
#define MAX_PTHREAD		(30000)
#define DEFAULT_PTHREAD		(1024)

#define MIN_QSORT_SIZE		(1 * KB)
#define MAX_QSORT_SIZE		(4 * MB)
#define DEFAULT_QSORT_SIZE	(256 * KB)

#define MIN_READAHEAD_BYTES	(1 * MB)
#define MAX_READAHEAD_BYTES	(256ULL * GB)
#define DEFAULT_READAHEAD_BYTES	(1 * GB)

#define MIN_SCTP_PORT		(1024)
#define MAX_SCTP_PORT		(65535)
#define DEFAULT_SCTP_PORT	(9000)

#define MIN_SENDFILE_SIZE	(1 * KB)
#define MAX_SENDFILE_SIZE	(1 * GB)
#define DEFAULT_SENDFILE_SIZE	(4 * MB)

#define MIN_SEEK_SIZE		(1 * MB)
#if UINTPTR_MAX == MAX_32
#define MAX_SEEK_SIZE		(0xffffe00)
#else
#define MAX_SEEK_SIZE 		(256ULL * GB)
#endif
#define DEFAULT_SEEK_SIZE	(16 * MB)

#define MIN_SEQUENTIAL		(0)
#define MAX_SEQUENTIAL		(1000000)
#define DEFAULT_SEQUENTIAL	(0)	/* Disabled */

#define MIN_SHM_SYSV_BYTES	(1 * MB)
#define MAX_SHM_SYSV_BYTES	(256 * MB)
#define DEFAULT_SHM_SYSV_BYTES	(8 * MB)

#define MIN_SHM_SYSV_SEGMENTS	(1)
#define MAX_SHM_SYSV_SEGMENTS	(128)
#define DEFAULT_SHM_SYSV_SEGMENTS (8)

#define MIN_SHM_POSIX_BYTES	(1 * MB)
#define MAX_SHM_POSIX_BYTES	(1 * GB)
#define DEFAULT_SHM_POSIX_BYTES	(8 * MB)

#define MIN_SHM_POSIX_OBJECTS	(1)
#define MAX_SHM_POSIX_OBJECTS	(128)
#define DEFAULT_SHM_POSIX_OBJECTS (32)

#define MAX_SIGSUSPEND_PIDS	(4)

#define MIN_SLEEP		(1)
#define MAX_SLEEP		(30000)
#define DEFAULT_SLEEP		(1024)

#define MIN_SOCKET_PORT		(1024)
#define MAX_SOCKET_PORT		(65535)
#define DEFAULT_SOCKET_PORT	(5000)

#define MIN_SOCKET_FD_PORT	(1024)
#define MAX_SOCKET_FD_PORT	(65535)
#define DEFAULT_SOCKET_FD_PORT	(8000)

#define MIN_SPLICE_BYTES	(1*KB)
#define MAX_SPLICE_BYTES	(64*MB)
#define DEFAULT_SPLICE_BYTES	(64*KB)

#define MIN_STREAM_L3_SIZE	(4 * KB)
#if UINTPTR_MAX == MAX_32
#define MAX_STREAM_L3_SIZE	(MAX_32)
#else
#define MAX_STREAM_L3_SIZE	(4 * GB)
#endif
#define DEFAULT_STREAM_L3_SIZE	(4 * MB)

#define MIN_SYNC_FILE_BYTES	(1 * MB)
#if UINTPTR_MAX == MAX_32
#define MAX_SYNC_FILE_BYTES	(MAX_32)
#else
#define MAX_SYNC_FILE_BYTES	(4 * GB)
#endif
#define DEFAULT_SYNC_FILE_BYTES	(1 * GB)

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

#define MIN_USERFAULTFD_BYTES	(4 * KB)
#if UINTPTR_MAX == MAX_32
#define MAX_USERFAULTFD_BYTES	(MAX_32)
#else
#define MAX_USERFAULTFD_BYTES	(4 * GB)
#endif
#define DEFAULT_USERFAULTFD_BYTES (16 * MB)

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

#define STR_SHARED_SIZE		(65536 * 32)
#define MEM_CACHE_SIZE		(65536 * 32)
#define DEFAULT_CACHE_LEVEL     3
#define UNDEFINED		(-1)

#define PAGE_MAPPED		(0x01)
#define PAGE_MAPPED_FAIL	(0x02)

#define FFT_SIZE		(4096)

#define STRESS_CPU_DITHER_X	(1024)
#define STRESS_CPU_DITHER_Y	(768)

/* Generic bit setting on an array macros */
#define STRESS_NBITS(a)		(sizeof(a[0]) * 8)
#define STRESS_GETBIT(a, i)	(a[i / STRESS_NBITS(a)] & \
				 (1 << (i & (STRESS_NBITS(a)-1))))
#define STRESS_CLRBIT(a, i)	(a[i / STRESS_NBITS(a)] &= \
				 ~(1 << (i & (STRESS_NBITS(a)-1))))
#define STRESS_SETBIT(a, i)	(a[i / STRESS_NBITS(a)] |= \
				 (1 << (i & (STRESS_NBITS(a)-1))))

#define SIEVE_SIZE 		(10000000)

/* MWC random number initial seed */
#define MWC_SEED_Z		(362436069UL)
#define MWC_SEED_W		(521288629UL)
#define MWC_SEED()		mwc_seed(MWC_SEED_W, MWC_SEED_Z)

#define SIZEOF_ARRAY(a)		(sizeof(a) / sizeof(a[0]))

#if defined(__linux__)
#define STRESS_THRASH
#endif

/* Arch specific, x86 */
#if defined(__x86_64__) || defined(__x86_64) || \
    defined(__i386__) || defined(__i386)
#define STRESS_X86	1
#endif

/* Arch specific, ARM */
#if defined(__ARM_ARCH_6__) || defined(__ARM_ARCH_6J__) ||     \
    defined(__ARM_ARCH_6K__) || defined(__ARM_ARCH_6Z__) ||    \
    defined(__ARM_ARCH_6ZK__) || defined(__ARM_ARCH_6T2__) ||  \
    defined(__ARM_ARCH_6M__) ||  defined(__ARM_ARCH_7__) ||    \
    defined(__ARM_ARCH_7A__) || defined(__ARM_ARCH_7R__) ||    \
    defined(__ARM_ARCH_7M__) || defined(__ARM_ARCH_7EM__) ||   \
    defined(__ARM_ARCH_8A__) || defined(__aarch64__)
#define STRESS_ARM      1
#endif

/* Arch specific, IBM S390 */
#if defined(__s390__)
#define STRESS_S390
#endif

/* Arch specific PPC64 */
#if defined(__PPC64__)
#define STRESS_PPC64
#endif

#if defined(__linux__)
/*
 *  See ioprio_set(2) and linux/ioprio.h, glibc has no definitions
 *  for these at present. Also refer to Documentation/block/ioprio.txt
 *  in the Linux kernel source.
 */
#define IOPRIO_CLASS_RT         (1)
#define IOPRIO_CLASS_BE         (2)
#define IOPRIO_CLASS_IDLE       (3)

#define IOPRIO_WHO_PROCESS      (1)
#define IOPRIO_WHO_PGRP         (2)
#define IOPRIO_WHO_USER         (3)

#define IOPRIO_PRIO_VALUE(class, data)  (((class) << 13) | data)
#endif

/* prctl(2) timer slack support */
#if defined(__linux__) && \
    defined(PR_SET_TIMERSLACK) && \
    defined(PR_GET_TIMERSLACK)
#define PRCTL_TIMER_SLACK
#endif

/*
 *  checks to see if we should keep in running the stressors
 */
extern bool __keep_stressing(const args_t *args);

#define keep_stressing()	__keep_stressing(args)

/* increment the stessor bogo ops counter */
static inline void ALWAYS_INLINE inc_counter(const args_t *args)
{
	(*(args->counter))++;
}

/* stress process prototype */
typedef int (*stress_func)(const args_t *args);

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


/* perf related constants */
#if defined(HAVE_LIB_PTHREAD) && \
    defined(__linux__) && \
    defined(__NR_perf_event_open)
#define STRESS_PERF_STATS	(1)
#define STRESS_PERF_INVALID	(~0ULL)
enum {
	STRESS_PERF_HW_CPU_CYCLES = 0,
	STRESS_PERF_HW_INSTRUCTIONS,
	STRESS_PERF_HW_CACHE_REFERENCES,
	STRESS_PERF_HW_CACHE_MISSES,
	STRESS_PERF_HW_STALLED_CYCLES_FRONTEND,
	STRESS_PERF_HW_STALLED_CYCLES_BACKEND,
	STRESS_PERF_HW_BRANCH_INSTRUCTIONS,
	STRESS_PERF_HW_BRANCH_MISSES,
	STRESS_PERF_HW_BUS_CYCLES,
	STRESS_PERF_HW_REF_CPU_CYCLES,

	STRESS_PERF_SW_PAGE_FAULTS_MIN,
	STRESS_PERF_SW_PAGE_FAULTS_MAJ,
	STRESS_PERF_SW_CONTEXT_SWITCHES,
	STRESS_PERF_SW_CPU_MIGRATIONS,
	STRESS_PERF_SW_ALIGNMENT_FAULTS,

	STRESS_PERF_TP_PAGE_FAULT_USER,
	STRESS_PERF_TP_PAGE_FAULT_KERNEL,
	STRESS_PERF_TP_SYSCALLS_ENTER,
	STRESS_PERF_TP_SYSCALLS_EXIT,
	STRESS_PERF_TP_TLB_FLUSH,
	STRESS_PERF_TP_KMALLOC,
	STRESS_PERF_TP_KMALLOC_NODE,
	STRESS_PERF_TP_KFREE,
	STRESS_PERF_TP_MM_PAGE_ALLOC,
	STRESS_PERF_TP_MM_PAGE_FREE,
	STRESS_PERF_TP_KMEM_CACHE_ALLOC,
	STRESS_PERF_TP_KMEM_CACHE_ALLOC_NODE,
	STRESS_PERF_TP_KMEM_CACHE_FREE,
	STRESS_PERF_TP_RCU_UTILIZATION,
	STRESS_PERF_TP_SCHED_MIGRATE_TASK,
	STRESS_PERF_TP_SCHED_MOVE_NUMA,
	STRESS_PERF_TP_SCHED_WAKEUP,
	STRESS_PERF_TP_SIGNAL_GENERATE,
	STRESS_PERF_TP_SIGNAL_DELIVER,
	STRESS_PERF_TP_IRQ_ENTRY,
	STRESS_PERF_TP_IRQ_EXIT,
	STRESS_PERF_TP_SOFTIRQ_ENTRY,
	STRESS_PERF_TP_SOFTIRQ_EXIT,
	STRESS_PERF_TP_WRITEBACK_DIRTY_INODE,
	STRESS_PERF_TP_WRITEBACK_DIRTY_PAGE,
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
	int		perf_opened;	/* count of opened counters */
} stress_perf_t;
#endif

/* linux thermal zones */
#if defined(__linux__)
#define	STRESS_THERMAL_ZONES	 (1)
#define STRESS_THERMAL_ZONES_MAX (31)	/* best if prime */
#endif

#if defined(STRESS_THERMAL_ZONES)
/* per stressor thermal zone info */
typedef struct tz_info {
	char	*path;			/* thermal zone path */
	char 	*type;			/* thermal zone type */
	size_t	index;			/* thermal zone # index */
	struct tz_info *next;		/* next thermal zone in list */
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
	bool run_ok;			/* true if stressor exited OK */
} proc_stats_t;

/* Shared memory segment */
typedef struct {
	size_t length;					/* Size of segment */
	uint8_t	*mem_cache;				/* Shared memory cache */
	uint64_t mem_cache_size;			/* Bytes */
	uint16_t mem_cache_level;			/* 1=L1, 2=L2, 3=L3 */
	uint32_t mem_cache_ways;			/* cache ways size */
	struct {
		uint32_t	flags;			/* flag bits */
#if defined(HAVE_LIB_PTHREAD)
		pthread_spinlock_t lock;		/* protection lock */
#endif
	} warn_once;
	uint32_t warn_once_flags;			/* Warn once flags */
	uint8_t  str_shared[STR_SHARED_SIZE];		/* str copying buffer */
	struct {
		uint64_t val64;
		uint32_t val32;
		uint16_t val16;
		uint8_t	 val8;
	} atomic;					/* Shared atomic temp vars */
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
#if defined(STRESS_PERF_STATS)
	struct {
		bool no_perf;				/* true = Perf not available */
		pthread_spinlock_t lock;		/* spinlock on no_perf updates */
	} perf;
#endif
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
	STRESS_AFFINITY,
	STRESS_AF_ALG,
	STRESS_AIO,
	STRESS_AIO_LINUX,
	STRESS_APPARMOR,
	STRESS_ATOMIC,
	STRESS_BRK,
	STRESS_BSEARCH,
	STRESS_BIGHEAP,
	STRESS_BIND_MOUNT,
	STRESS_CACHE,
	STRESS_CAP,
	STRESS_CHDIR,
	STRESS_CHMOD,
	STRESS_CHOWN,
	STRESS_CHROOT,
	STRESS_CLOCK,
	STRESS_CLONE,
	STRESS_CONTEXT,
	STRESS_COPY_FILE,
	STRESS_CPU,
	STRESS_CPU_ONLINE,
	STRESS_CRYPT,
	STRESS_DAEMON,
	STRESS_DCCP,
	STRESS_DENTRY,
	STRESS_DIR,
	STRESS_DIRDEEP,
	STRESS_DNOTIFY,
	STRESS_DUP,
	STRESS_EPOLL,
	STRESS_EVENTFD,
	STRESS_EXEC,
	STRESS_FALLOCATE,
	STRESS_FAULT,
	STRESS_FCNTL,
	STRESS_FIEMAP,
	STRESS_FIFO,
	STRESS_FILENAME,
	STRESS_FLOCK,
	STRESS_FANOTIFY,
	STRESS_FORK,
	STRESS_FP_ERROR,
	STRESS_FSTAT,
	STRESS_FULL,
	STRESS_FUTEX,
	STRESS_GET,
	STRESS_GETRANDOM,
	STRESS_GETDENT,
	STRESS_HANDLE,
	STRESS_HDD,
	STRESS_HEAPSORT,
	STRESS_HSEARCH,
	STRESS_ICACHE,
	STRESS_ICMP_FLOOD,
	STRESS_INOTIFY,
	STRESS_IOMIX,
	STRESS_IOPRIO,
	STRESS_IOSYNC,
	STRESS_ITIMER,
	STRESS_KCMP,
	STRESS_KEY,
	STRESS_KILL,
	STRESS_KLOG,
	STRESS_LEASE,
	STRESS_LINK,
	STRESS_LOCKBUS,
	STRESS_LOCKA,
	STRESS_LOCKF,
	STRESS_LOCKOFD,
	STRESS_LONGJMP,
	STRESS_LSEARCH,
	STRESS_MADVISE,
	STRESS_MALLOC,
	STRESS_MATRIX,
	STRESS_MEMBARRIER,
	STRESS_MEMCPY,
	STRESS_MEMFD,
	STRESS_MERGESORT,
	STRESS_MINCORE,
	STRESS_MKNOD,
	STRESS_MLOCK,
	STRESS_MMAP,
	STRESS_MMAPFORK,
	STRESS_MMAPMANY,
	STRESS_MREMAP,
	STRESS_MSG,
	STRESS_MSYNC,
	STRESS_MQ,
	STRESS_NETLINK_PROC,
	STRESS_NICE,
	STRESS_NOP,
	STRESS_NULL,
	STRESS_NUMA,
	STRESS_OOM_PIPE,
	STRESS_OPCODE,
	STRESS_OPEN,
	STRESS_PERSONALITY,
	STRESS_PIPE,
	STRESS_POLL,
	STRESS_PROCFS,
	STRESS_PTHREAD,
	STRESS_PTRACE,
	STRESS_PTY,
	STRESS_QSORT,
	STRESS_QUOTA,
	STRESS_RDRAND,
	STRESS_READAHEAD,
	STRESS_REMAP_FILE_PAGES,
	STRESS_RENAME,
	STRESS_RESOURCES,
	STRESS_RLIMIT,
	STRESS_RMAP,
	STRESS_RTC,
	STRESS_SCHEDPOLICY,
	STRESS_SCTP,
	STRESS_SEAL,
	STRESS_SECCOMP,
	STRESS_SEEK,
	STRESS_SEMAPHORE_POSIX,
	STRESS_SEMAPHORE_SYSV,
	STRESS_SENDFILE,
	STRESS_SHM_POSIX,
	STRESS_SHM_SYSV,
	STRESS_SIGFD,
	STRESS_SIGFPE,
	STRESS_SIGPENDING,
	STRESS_SIGQUEUE,
	STRESS_SIGSEGV,
	STRESS_SIGSUSPEND,
	STRESS_SLEEP,
	STRESS_SOCKET,
	STRESS_SOCKET_FD,
	STRESS_SOCKET_PAIR,
	STRESS_SPAWN,
	STRESS_SPLICE,
	STRESS_STACK,
	STRESS_STACKMMAP,
	STRESS_STR,
	STRESS_STREAM,
	STRESS_SWITCH,
	STRESS_SYMLINK,
	STRESS_SYNC_FILE,
	STRESS_SYSINFO,
	STRESS_SYSFS,
	STRESS_TEE,
	STRESS_TIMER,
	STRESS_TIMERFD,
	STRESS_TLB_SHOOTDOWN,
	STRESS_TMPFS,
	STRESS_TSC,
	STRESS_TSEARCH,
	STRESS_UDP,
	STRESS_UDP_FLOOD,
	STRESS_UNSHARE,
	STRESS_URANDOM,
	STRESS_USERFAULTFD,
	STRESS_UTIME,
	STRESS_VECMATH,
	STRESS_VFORK,
	STRESS_VFORKMANY,
	STRESS_VM,
	STRESS_VM_RW,
	STRESS_VM_SPLICE,
	STRESS_WAIT,
	STRESS_WCS,
	STRESS_XATTR,
	STRESS_YIELD,
	STRESS_ZERO,
	STRESS_ZLIB,
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
	OPT_FALLOCATE = 'F',
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
	OPT_TIMER = 'T',
	OPT_URANDOM = 'u',
	OPT_VERBOSE = 'v',
	OPT_VERSION = 'V',
	OPT_YIELD = 'y',
	OPT_YAML = 'Y',
	OPT_EXCLUDE = 'x',

	/* Long options only */

	OPT_LONG_OPS_START = 0x7f,

	OPT_AFFINITY,
	OPT_AFFINITY_OPS,
	OPT_AFFINITY_RAND,

	OPT_AF_ALG,
	OPT_AF_ALG_OPS,

	OPT_AGGRESSIVE,

	OPT_AIO,
	OPT_AIO_OPS,
	OPT_AIO_REQUESTS,

	OPT_AIO_LINUX,
	OPT_AIO_LINUX_OPS,
	OPT_AIO_LINUX_REQUESTS,

	OPT_APPARMOR,
	OPT_APPARMOR_OPS,

	OPT_ATOMIC,
	OPT_ATOMIC_OPS,

	OPT_BRK,
	OPT_BRK_OPS,
	OPT_BRK_NOTOUCH,

	OPT_BSEARCH,
	OPT_BSEARCH_OPS,
	OPT_BSEARCH_SIZE,

	OPT_BIGHEAP_OPS,
	OPT_BIGHEAP_GROWTH,

	OPT_BIND_MOUNT,
	OPT_BIND_MOUNT_OPS,

	OPT_CLASS,
	OPT_CACHE_OPS,
	OPT_CACHE_PREFETCH,
	OPT_CACHE_FLUSH,
	OPT_CACHE_FENCE,
	OPT_CACHE_LEVEL,
	OPT_CACHE_WAYS,
	OPT_CACHE_NO_AFFINITY,

	OPT_CAP,
	OPT_CAP_OPS,

	OPT_CHDIR,
	OPT_CHDIR_OPS,

	OPT_CHMOD,
	OPT_CHMOD_OPS,

	OPT_CHOWN,
	OPT_CHOWN_OPS,

	OPT_CHROOT,
	OPT_CHROOT_OPS,

	OPT_CLOCK,
	OPT_CLOCK_OPS,

	OPT_CLONE,
	OPT_CLONE_OPS,
	OPT_CLONE_MAX,

	OPT_CONTEXT,
	OPT_CONTEXT_OPS,

	OPT_COPY_FILE,
	OPT_COPY_FILE_OPS,
	OPT_COPY_FILE_BYTES,

	OPT_CPU_OPS,
	OPT_CPU_METHOD,
	OPT_CPU_LOAD_SLICE,

	OPT_CPU_ONLINE,
	OPT_CPU_ONLINE_OPS,

	OPT_CRYPT,
	OPT_CRYPT_OPS,

	OPT_DAEMON,
	OPT_DAEMON_OPS,

	OPT_DCCP,
	OPT_DCCP_DOMAIN,
	OPT_DCCP_OPS,
	OPT_DCCP_OPTS,
	OPT_DCCP_PORT,

	OPT_DENTRY_OPS,
	OPT_DENTRIES,
	OPT_DENTRY_ORDER,

	OPT_DIR,
	OPT_DIR_OPS,

	OPT_DIRDEEP,
	OPT_DIRDEEP_OPS,

	OPT_DNOTIFY,
	OPT_DNOTIFY_OPS,

	OPT_DUP,
	OPT_DUP_OPS,

	OPT_EPOLL,
	OPT_EPOLL_OPS,
	OPT_EPOLL_PORT,
	OPT_EPOLL_DOMAIN,

	OPT_EVENTFD,
	OPT_EVENTFD_OPS,

	OPT_EXEC,
	OPT_EXEC_OPS,
	OPT_EXEC_MAX,

	OPT_FALLOCATE_OPS,
	OPT_FALLOCATE_BYTES,

	OPT_FAULT,
	OPT_FAULT_OPS,

	OPT_FCNTL,
	OPT_FCNTL_OPS,

	OPT_FIEMAP,
	OPT_FIEMAP_OPS,
	OPT_FIEMAP_BYTES,

	OPT_FIFO,
	OPT_FIFO_OPS,
	OPT_FIFO_READERS,

	OPT_FILENAME,
	OPT_FILENAME_OPS,
	OPT_FILENAME_OPTS,

	OPT_FLOCK,
	OPT_FLOCK_OPS,

	OPT_FANOTIFY,
	OPT_FANOTIFY_OPS,

	OPT_FORK_OPS,
	OPT_FORK_MAX,

	OPT_FP_ERROR,
	OPT_FP_ERROR_OPS,

	OPT_FSTAT,
	OPT_FSTAT_OPS,
	OPT_FSTAT_DIR,

	OPT_FULL,
	OPT_FULL_OPS,

	OPT_FUTEX,
	OPT_FUTEX_OPS,

	OPT_GET,
	OPT_GET_OPS,

	OPT_GETRANDOM,
	OPT_GETRANDOM_OPS,

	OPT_GETDENT,
	OPT_GETDENT_OPS,

	OPT_HANDLE,
	OPT_HANDLE_OPS,

	OPT_HDD_BYTES,
	OPT_HDD_WRITE_SIZE,
	OPT_HDD_OPS,
	OPT_HDD_OPTS,

	OPT_HEAPSORT,
	OPT_HEAPSORT_OPS,
	OPT_HEAPSORT_INTEGERS,

	OPT_HSEARCH,
	OPT_HSEARCH_OPS,
	OPT_HSEARCH_SIZE,

	OPT_ICACHE,
	OPT_ICACHE_OPS,

	OPT_ICMP_FLOOD,
	OPT_ICMP_FLOOD_OPS,

	OPT_IGNITE_CPU,

	OPT_INOTIFY,
	OPT_INOTIFY_OPS,

	OPT_IOMIX,
	OPT_IOMIX_BYTES,
	OPT_IOMIX_OPS,

	OPT_IONICE_CLASS,
	OPT_IONICE_LEVEL,

	OPT_IOPRIO,
	OPT_IOPRIO_OPS,

	OPT_IOSYNC_OPS,

	OPT_ITIMER,
	OPT_ITIMER_OPS,
	OPT_ITIMER_FREQ,

	OPT_KCMP,
	OPT_KCMP_OPS,

	OPT_KEY,
	OPT_KEY_OPS,

	OPT_KILL,
	OPT_KILL_OPS,

	OPT_KLOG,
	OPT_KLOG_OPS,

	OPT_LEASE,
	OPT_LEASE_OPS,
	OPT_LEASE_BREAKERS,

	OPT_LINK,
	OPT_LINK_OPS,

	OPT_LOCKBUS,
	OPT_LOCKBUS_OPS,

	OPT_LOCKA,
	OPT_LOCKA_OPS,

	OPT_LOCKF,
	OPT_LOCKF_OPS,
	OPT_LOCKF_NONBLOCK,

	OPT_LOCKOFD,
	OPT_LOCKOFD_OPS,

	OPT_LOG_BRIEF,
	OPT_LOG_FILE,

	OPT_LONGJMP,
	OPT_LONGJMP_OPS,

	OPT_LSEARCH,
	OPT_LSEARCH_OPS,
	OPT_LSEARCH_SIZE,

	OPT_MADVISE,
	OPT_MADVISE_OPS,

	OPT_MALLOC,
	OPT_MALLOC_OPS,
	OPT_MALLOC_BYTES,
	OPT_MALLOC_MAX,
	OPT_MALLOC_THRESHOLD,

	OPT_MATRIX,
	OPT_MATRIX_OPS,
	OPT_MATRIX_SIZE,
	OPT_MATRIX_METHOD,

	OPT_MAXIMIZE,

	OPT_MEMBARRIER,
	OPT_MEMBARRIER_OPS,

	OPT_MEMCPY,
	OPT_MEMCPY_OPS,

	OPT_MEMFD,
	OPT_MEMFD_OPS,
	OPT_MEMFD_BYTES,

	OPT_MERGESORT,
	OPT_MERGESORT_OPS,
	OPT_MERGESORT_INTEGERS,

	OPT_METRICS_BRIEF,

	OPT_MINCORE,
	OPT_MINCORE_OPS,
	OPT_MINCORE_RAND,

	OPT_MKNOD,
	OPT_MKNOD_OPS,

	OPT_MINIMIZE,

	OPT_MLOCK,
	OPT_MLOCK_OPS,

	OPT_MMAP,
	OPT_MMAP_OPS,
	OPT_MMAP_BYTES,
	OPT_MMAP_FILE,
	OPT_MMAP_ASYNC,
	OPT_MMAP_MPROTECT,

	OPT_MMAPFORK,
	OPT_MMAPFORK_OPS,

	OPT_MMAPMANY,
	OPT_MMAPMANY_OPS,

	OPT_MREMAP,
	OPT_MREMAP_OPS,
	OPT_MREMAP_BYTES,

	OPT_MSG,
	OPT_MSG_OPS,

	OPT_MSYNC,
	OPT_MSYNC_BYTES,
	OPT_MSYNC_OPS,

	OPT_MQ,
	OPT_MQ_OPS,
	OPT_MQ_SIZE,

	OPT_NETLINK_PROC,
	OPT_NETLINK_PROC_OPS,

	OPT_NICE,
	OPT_NICE_OPS,

	OPT_NO_MADVISE,
	OPT_NO_RAND_SEED,

	OPT_NOP,
	OPT_NOP_OPS,

	OPT_NULL,
	OPT_NULL_OPS,

	OPT_NUMA,
	OPT_NUMA_OPS,

	OPT_OOM_PIPE,
	OPT_OOM_PIPE_OPS,

	OPT_OPCODE,
	OPT_OPCODE_OPS,

	OPT_OPEN_OPS,

	OPT_PAGE_IN,
	OPT_PATHOLOGICAL,

	OPT_PERF_STATS,

	OPT_PERSONALITY,
	OPT_PERSONALITY_OPS,

	OPT_PIPE_OPS,
	OPT_PIPE_SIZE,
	OPT_PIPE_DATA_SIZE,

	OPT_POLL_OPS,

	OPT_PROCFS,
	OPT_PROCFS_OPS,

	OPT_PTHREAD,
	OPT_PTHREAD_OPS,
	OPT_PTHREAD_MAX,

	OPT_PTRACE,
	OPT_PTRACE_OPS,

	OPT_PTY,
	OPT_PTY_OPS,

	OPT_QSORT,
	OPT_QSORT_OPS,
	OPT_QSORT_INTEGERS,

	OPT_QUOTA,
	OPT_QUOTA_OPS,

	OPT_RDRAND,
	OPT_RDRAND_OPS,

	OPT_READAHEAD,
	OPT_READAHEAD_OPS,
	OPT_READAHEAD_BYTES,

	OPT_REMAP_FILE_PAGES,
	OPT_REMAP_FILE_PAGES_OPS,

	OPT_RENAME_OPS,

	OPT_RESOURCES,
	OPT_RESOURCES_OPS,

	OPT_RLIMIT,
	OPT_RLIMIT_OPS,

	OPT_RMAP,
	OPT_RMAP_OPS,

	OPT_RTC,
	OPT_RTC_OPS,

	OPT_SCHED,
	OPT_SCHED_PRIO,

	OPT_SCHEDPOLICY,
	OPT_SCHEDPOLICY_OPS,

	OPT_SCTP,
	OPT_SCTP_OPS,
	OPT_SCTP_DOMAIN,
	OPT_SCTP_PORT,

	OPT_SEAL,
	OPT_SEAL_OPS,

	OPT_SECCOMP,
	OPT_SECCOMP_OPS,

	OPT_SEEK,
	OPT_SEEK_OPS,
	OPT_SEEK_PUNCH,
	OPT_SEEK_SIZE,

	OPT_SENDFILE,
	OPT_SENDFILE_OPS,
	OPT_SENDFILE_SIZE,

	OPT_SEMAPHORE_POSIX,
	OPT_SEMAPHORE_POSIX_OPS,
	OPT_SEMAPHORE_POSIX_PROCS,

	OPT_SEMAPHORE_SYSV,
	OPT_SEMAPHORE_SYSV_OPS,
	OPT_SEMAPHORE_SYSV_PROCS,

	OPT_SHM_POSIX,
	OPT_SHM_POSIX_OPS,
	OPT_SHM_POSIX_BYTES,
	OPT_SHM_POSIX_OBJECTS,

	OPT_SHM_SYSV,
	OPT_SHM_SYSV_OPS,
	OPT_SHM_SYSV_BYTES,
	OPT_SHM_SYSV_SEGMENTS,

	OPT_SEQUENTIAL,

	OPT_SIGFD,
	OPT_SIGFD_OPS,

	OPT_SIGFPE,
	OPT_SIGFPE_OPS,

	OPT_SIGPENDING,
	OPT_SIGPENDING_OPS,

	OPT_SIGQUEUE,
	OPT_SIGQUEUE_OPS,

	OPT_SIGSEGV,
	OPT_SIGSEGV_OPS,

	OPT_SIGSUSPEND,
	OPT_SIGSUSPEND_OPS,

	OPT_SLEEP,
	OPT_SLEEP_OPS,
	OPT_SLEEP_MAX,

	OPT_SOCKET_OPS,
	OPT_SOCKET_DOMAIN,
	OPT_SOCKET_NODELAY,
	OPT_SOCKET_OPTS,
	OPT_SOCKET_PORT,
	OPT_SOCKET_TYPE,

	OPT_SOCKET_FD,
	OPT_SOCKET_FD_OPS,
	OPT_SOCKET_FD_PORT,

	OPT_SOCKET_PAIR,
	OPT_SOCKET_PAIR_OPS,

	OPT_SWITCH_OPS,

	OPT_SPAWN,
	OPT_SPAWN_OPS,

	OPT_SPLICE,
	OPT_SPLICE_OPS,
	OPT_SPLICE_BYTES,

	OPT_STACK,
	OPT_STACK_OPS,
	OPT_STACK_FILL,

	OPT_STACKMMAP,
	OPT_STACKMMAP_OPS,

	OPT_STR,
	OPT_STR_OPS,
	OPT_STR_METHOD,

	OPT_STREAM,
	OPT_STREAM_OPS,
	OPT_STREAM_L3_SIZE,

	OPT_STRESSORS,

	OPT_SYMLINK,
	OPT_SYMLINK_OPS,

	OPT_SYNC_FILE,
	OPT_SYNC_FILE_OPS,
	OPT_SYNC_FILE_BYTES,

	OPT_SYSINFO,
	OPT_SYSINFO_OPS,

	OPT_SYSFS,
	OPT_SYSFS_OPS,

	OPT_SYSLOG,

	OPT_TEE,
	OPT_TEE_OPS,

	OPT_TASKSET,

	OPT_TEMP_PATH,

	OPT_THERMAL_ZONES,

	OPT_THRASH,

	OPT_TIMER_SLACK,

	OPT_TIMER_OPS,
	OPT_TIMER_FREQ,
	OPT_TIMER_RAND,

	OPT_TIMERFD,
	OPT_TIMERFD_OPS,
	OPT_TIMERFD_FREQ,
	OPT_TIMERFD_RAND,

	OPT_TIMES,

	OPT_TLB_SHOOTDOWN,
	OPT_TLB_SHOOTDOWN_OPS,

	OPT_TMPFS,
	OPT_TMPFS_OPS,

	OPT_TSC,
	OPT_TSC_OPS,

	OPT_TSEARCH,
	OPT_TSEARCH_OPS,
	OPT_TSEARCH_SIZE,

	OPT_UDP,
	OPT_UDP_OPS,
	OPT_UDP_PORT,
	OPT_UDP_DOMAIN,
	OPT_UDP_LITE,

	OPT_UDP_FLOOD,
	OPT_UDP_FLOOD_OPS,
	OPT_UDP_FLOOD_DOMAIN,

	OPT_UNSHARE,
	OPT_UNSHARE_OPS,

	OPT_URANDOM_OPS,

	OPT_USERFAULTFD,
	OPT_USERFAULTFD_OPS,
	OPT_USERFAULTFD_BYTES,

	OPT_UTIME,
	OPT_UTIME_OPS,
	OPT_UTIME_FSYNC,

	OPT_VECMATH,
	OPT_VECMATH_OPS,

	OPT_VERIFY,

	OPT_VFORK,
	OPT_VFORK_OPS,
	OPT_VFORK_MAX,

	OPT_VFORKMANY,
	OPT_VFORKMANY_OPS,

	OPT_VM_BYTES,
	OPT_VM_HANG,
	OPT_VM_KEEP,
	OPT_VM_MMAP_POPULATE,
	OPT_VM_MMAP_LOCKED,
	OPT_VM_OPS,
	OPT_VM_METHOD,

	OPT_VM_RW,
	OPT_VM_RW_OPS,
	OPT_VM_RW_BYTES,

	OPT_VM_SPLICE,
	OPT_VM_SPLICE_OPS,
	OPT_VM_SPLICE_BYTES,

	OPT_WAIT,
	OPT_WAIT_OPS,

	OPT_WCS,
	OPT_WCS_OPS,
	OPT_WCS_METHOD,

	OPT_XATTR,
	OPT_XATTR_OPS,

	OPT_YIELD_OPS,

	OPT_ZERO,
	OPT_ZERO_OPS,

	OPT_ZLIB,
	OPT_ZLIB_OPS,
	OPT_ZLIB_METHOD,

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

/* Per process information */
typedef struct {
	pid_t	*pids;			/* process id */
	int32_t started_procs;		/* count of started processes */
	int32_t num_procs;		/* number of process per stressor */
	uint64_t bogo_ops;		/* number of bogo ops */
	bool	exclude;		/* true if excluded */
} proc_info_t;

/* Scale lookup mapping, suffix -> scale by */
typedef struct {
	const char	ch;		/* Scaling suffix */
	const uint64_t	scale;		/* Amount to scale by */
} scale_t;

#if defined(__linux__)
/* Cache types */
typedef enum cache_type {
	CACHE_TYPE_UNKNOWN = 0,		/* Unknown type */
	CACHE_TYPE_DATA,		/* D$ */
	CACHE_TYPE_INSTRUCTION,		/* I$ */
	CACHE_TYPE_UNIFIED,		/* D$ + I$ */
} cache_type_t;

/* CPU cache information */
typedef struct cpu_cache {
	uint16_t           level;	/* cache level, L1, L2 etc */
	cache_type_t       type;	/* cache type */
	uint64_t           size;      	/* cache size in bytes */
	uint32_t           line_size;	/* cache line size in bytes */
	uint32_t           ways;	/* cache ways */
} cpu_cache_t;

typedef struct cpu {
	uint32_t       num;		/* CPU # number */
	bool           online;		/* CPU online when true */
	uint32_t       cache_count;	/* CPU cache #  */
	cpu_cache_t   *caches;		/* CPU cache data */
} cpu_t;

typedef struct cpus {
	uint32_t   count;		/* CPU count */
	cpu_t     *cpus;		/* CPU data */
} cpus_t;
#endif

/* Various global option settings and flags */
extern const char *g_app_name;		/* Name of application */
extern shared_t *g_shared;		/* shared memory */
extern uint64_t	g_opt_timeout;		/* timeout in seconds */
extern uint64_t	g_opt_flags;		/* option flags */
extern int32_t g_opt_sequential;	/* Number of sequential iterations */
extern volatile bool g_keep_stressing_flag; /* false to exit stressor */
extern volatile bool g_caught_sigint;	/* true if stopped by SIGINT */
extern pid_t g_pgrp;			/* proceess group leader */

/*
 *  externs to force gcc to stash computed values and hence
 *  to stop the optimiser optimising code away to zero. The
 *  *_put funcs are essentially no-op functions.
 */
extern uint64_t uint64_zero(void);

static volatile uint64_t uint64_val;
static volatile double   double_val;

/*
 *  uint64_put()
 *	stash a uint64_t value
 */
static inline void ALWAYS_INLINE uint64_put(const uint64_t a)
{
	uint64_val = a;
}

/*
 *  double_put()
 *	stash a double value
 */
static inline void ALWAYS_INLINE double_put(const double a)
{
	double_val = a;
}

/* Filenames and directories */
extern int stress_temp_filename(char *path, const size_t len,
	const char *name, const pid_t pid, const uint32_t instance,
	const uint64_t magic);
extern int stress_temp_filename_args(const args_t *args, char *path,
	const size_t len, const uint64_t magic);
extern int stress_temp_dir(char *path, const size_t len,
	const char *name, const pid_t pid, const uint32_t instance);
extern int stress_temp_dir_args(const args_t *args, char *path, const size_t len);
extern WARN_UNUSED int stress_temp_dir_mk(const char *name, const pid_t pid,
	const uint32_t instance);
extern WARN_UNUSED int stress_temp_dir_mk_args(const args_t *args);
extern int stress_temp_dir_rm(const char *name, const pid_t pid,
	const uint32_t instance);
extern int stress_temp_dir_rm_args(const args_t *args);
extern void stress_cwd_readwriteable(void);

extern const char *stress_strsignal(const int signum);

#if defined(STRESS_X86)
/*
 *  clflush()
 *	flush a cache line
 */
static inline void ALWAYS_INLINE clflush(volatile void *ptr)
{
        asm volatile("clflush %0" : "+m" (*(volatile char *)ptr));
}
#else
#define clflush(ptr)	do { } while (0) /* No-op */
#endif

/*
 *  mfence()
 *	serializing memory fence
 */
static inline void ALWAYS_INLINE mfence(void)
{
#if NEED_GNUC(4, 2, 0)
	__sync_synchronize();
#else
#if defined(STRESS_X86)
	asm volatile("mfence" : : : "memory");
#else
	/* Other arches not yet implemented for older GCC flavours */
#endif
#endif
}

/* Fast random numbers */
extern uint32_t mwc32(void);
extern uint64_t mwc64(void);
extern uint16_t mwc16(void);
extern uint8_t mwc8(void);
extern void mwc_seed(const uint32_t w, const uint32_t z);
extern void mwc_reseed(void);

/* Time handling */

/*
 *  timeval_to_double()
 *      convert timeval to seconds as a double
 */
static inline WARN_UNUSED double timeval_to_double(const struct timeval *tv)
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
extern void set_sched(const int32_t sched, const int sched_priority);
extern const char *get_sched_name(const int sched);
extern void set_iopriority(const int32_t class, const int32_t level);
extern void set_proc_name(const char *name);

/* Memory locking */
extern int stress_mlock_region(const void *addr_start, const void *addr_end);

/* Argument parsing and range checking */
extern WARN_UNUSED int32_t  get_opt_sched(const char *const str);
extern WARN_UNUSED int32_t  get_opt_ionice_class(const char *const str);
extern WARN_UNUSED uint32_t get_uint32(const char *const str);
extern WARN_UNUSED int32_t  get_int32(const char *const str);
extern WARN_UNUSED uint64_t get_uint64(const char *const str);
extern WARN_UNUSED uint64_t get_uint64_scale(const char *const str,
	const scale_t scales[], const char *const msg);
extern WARN_UNUSED uint64_t get_uint64_byte(const char *const str);
extern WARN_UNUSED uint64_t get_uint64_byte_memory(const char *const str,
	const uint32_t instances);
extern WARN_UNUSED uint64_t get_uint64_byte_filesystem(const char *const str,
	const uint32_t instances);
extern WARN_UNUSED uint64_t get_uint64_time(const char *const str);
extern void check_value(const char *const msg, const int val);
extern void check_range(const char *const opt, const uint64_t val,
	const uint64_t lo, const uint64_t hi);
extern void check_range_bytes(const char *const opt, const uint64_t val,
	const uint64_t lo, const uint64_t hi);
extern WARN_UNUSED int set_cpu_affinity(char *const arg);

/* Misc helper funcs */
extern void stress_unmap_shared(void);
extern void log_system_mem_info(void);
extern WARN_UNUSED char *munge_underscore(const char *str);
extern size_t stress_get_pagesize(void);
extern WARN_UNUSED int32_t stress_get_processors_online(void);
extern WARN_UNUSED int32_t stress_get_processors_configured(void);
extern WARN_UNUSED int32_t stress_get_ticks_per_second(void);
extern WARN_UNUSED ssize_t stress_get_stack_direction(void);
extern void stress_get_memlimits(size_t *shmall, size_t *freemem, size_t *totalmem);
extern WARN_UNUSED int stress_get_load_avg(double *min1, double *min5, double *min15);
extern void set_max_limits(void);
extern void stress_parent_died_alarm(void);
extern int stress_process_dumpable(const bool dumpable);
extern void stress_set_timer_slack_ns(const char *optarg);
extern void stress_set_timer_slack(void);
extern WARN_UNUSED int stress_set_temp_path(const char *path);
extern void stress_strnrnd(char *str, const size_t len);
extern void stress_get_cache_size(uint64_t *l2, uint64_t *l3);
extern WARN_UNUSED unsigned int stress_get_cpu(void);
extern WARN_UNUSED int stress_cache_alloc(const char *name);
extern void stress_cache_free(void);
extern void ignite_cpu_start(void);
extern void ignite_cpu_stop(void);
extern int system_write(const char *path, const char *buf, const size_t buf_len);
extern WARN_UNUSED int stress_set_nonblock(const int fd);
extern WARN_UNUSED int system_read(const char *path, char *buf, const size_t buf_len);
extern WARN_UNUSED uint64_t stress_get_prime64(const uint64_t n);
extern WARN_UNUSED size_t stress_get_file_limit(void);
extern WARN_UNUSED int stress_sigaltstack(const void *stack, const size_t size);
extern WARN_UNUSED int stress_sighandler(const char *name, const int signum, void (*handler)(int), struct sigaction *orig_action);
extern int stress_sigrestore(const char *name, const int signum, struct sigaction *orig_action);
extern WARN_UNUSED int stress_not_implemented(const args_t *args);
extern WARN_UNUSED size_t stress_probe_max_pipe_size(void);
extern WARN_UNUSED void *align_address(const void *addr, const size_t alignment);
extern void mmap_set(uint8_t *buf, const size_t sz, const size_t page_size);
extern WARN_UNUSED int mmap_check(uint8_t *buf, const size_t sz, const size_t page_size);
extern WARN_UNUSED uint64_t stress_get_phys_mem_size(void);
extern WARN_UNUSED uint64_t stress_get_filesystem_size(void);
extern char *stress_uint64_to_str(char *str, size_t len, const uint64_t val);

/*
 *  Indicate a stress test failed because of limited resources
 *  rather than a failure of the tests during execution.
 *  err is the errno of the failure.
 */
static inline int ALWAYS_INLINE exit_status(const int err)
{
	return ((err == ENOMEM) || (err == ENOSPC)) ?
		EXIT_NO_RESOURCE : EXIT_FAILURE;
}

/*
 *  Stack aligning for clone() system calls
 *	align to nearest 16 bytes for aarch64 et al,
 *	assumes we have enough slop to do this
 */
static inline WARN_UNUSED ALWAYS_INLINE void *align_stack(void *stack_top)
{
	return (void *)((uintptr_t)stack_top & ~(uintptr_t)0xf);
}

/*
 *  Check if flag is set, and set flag
 */
static inline WARN_UNUSED uint32_t warn_once(const uint32_t flag)
{
	uint32_t tmp;

#if defined(HAVE_LIB_PTHREAD)
	pthread_spin_lock(&g_shared->warn_once.lock);
#endif
	tmp = !(g_shared->warn_once.flags & flag);
	g_shared->warn_once.flags |= flag;
#if defined(HAVE_LIB_PTHREAD)
	pthread_spin_unlock(&g_shared->warn_once.lock);
#endif
	return tmp;
}

/* Memory tweaking */
extern int madvise_random(void *addr, const size_t length);
extern int mincore_touch_pages(void *buf, const size_t buf_len);

/* Mounts */
extern void mount_free(char *mnts[], const int n);
extern WARN_UNUSED int mount_get(char *mnts[], const int max);

#if defined(STRESS_THERMAL_ZONES)
/* thermal zones */
extern int tz_init(tz_info_t **tz_info_list);
extern void tz_free(tz_info_t **tz_info_list);
extern int tz_get_temperatures(tz_info_t **tz_info_list, stress_tz_t *tz);
extern void tz_dump(FILE *fp, const stress_t stressors[],
	const proc_info_t procs[STRESS_MAX], const int32_t max_procs);
#endif

/* Network helpers */

#define NET_ADDR_ANY		(0)
#define NET_ADDR_LOOPBACK	(1)

extern void stress_set_net_port(const char *optname, const char *optarg,
	const int min_port, const int max_port, int *port);
extern WARN_UNUSED int stress_set_net_domain(const int domain_mask, const char *name, const char *domain_name, int *domain);
extern void stress_set_sockaddr(const char *name, const uint32_t instance,
	const pid_t ppid, const int domain, const int port,
	struct sockaddr **sockaddr, socklen_t *len, int net_addr);
extern void stress_set_sockaddr_port(const int domain, const int port, struct sockaddr *sockaddr);

extern void stress_semaphore_posix_init(void);
extern void stress_semaphore_posix_destroy(void);

extern void stress_semaphore_sysv_init(void);
extern void stress_semaphore_sysv_destroy(void);

/* CPU caches */
#if defined (__linux__)
extern cpus_t *get_all_cpu_cache_details(void);
extern uint16_t get_max_cache_level(const cpus_t *cpus);
extern cpu_cache_t *get_cpu_cache(const cpus_t *cpus, const uint16_t cache_level);
extern void free_cpu_caches(cpus_t *cpus);
#endif

/* CPU thrashing start/stop helpers */
extern int  thrash_start(void);
extern void thrash_stop(void);

/* Used to set options for specific stressors */
extern void stress_adjust_pthread_max(uint64_t max);
extern void stress_adjust_sleep_max(uint64_t max);
extern int  stress_apparmor_supported(void);
extern void stress_set_aio_requests(const char *optarg);
extern void stress_set_aio_linux_requests(const char *optarg);
extern void stress_set_bigheap_growth(const char *optarg);
extern void stress_set_bsearch_size(const char *optarg);
extern int  stress_chroot_supported(void);
extern void stress_set_clone_max(const char *optarg);
extern void stress_set_copy_file_bytes(const char *optarg);
extern void stress_set_cpu_load(const char *optarg);
extern void stress_set_cpu_load_slice(const char *optarg);
extern int  stress_set_cpu_method(const char *name);
extern int  stress_set_dccp_domain(const char *name);
extern int  stress_set_dccp_opts(const char *optarg);
extern void stress_set_dccp_port(const char *optarg);
extern void stress_set_dentries(const char *optarg);
extern int  stress_set_dentry_order(const char *optarg);
extern void stress_set_epoll_port(const char *optarg);
extern int  stress_set_epoll_domain(const char *optarg);
extern void stress_set_exec_max(const char *optarg);
extern void stress_set_fallocate_bytes(const char *optarg);
extern void stress_set_fifo_readers(const char *optarg);
extern int  stress_filename_opts(const char *opt);
extern void stress_set_fiemap_size(const char *optarg);
extern void stress_set_fork_max(const char *optarg);
extern int  stress_fanotify_supported(void);
extern void stress_set_fstat_dir(const char *optarg);
extern void stress_set_hdd_bytes(const char *optarg);
extern int  stress_hdd_opts(char *opts);
extern void stress_set_hdd_write_size(const char *optarg);
extern void stress_set_heapsort_size(const void *optarg);
extern void stress_set_hsearch_size(const char *optarg);
extern int  stress_icmp_flood_supported(void);
extern void stress_set_itimer_freq(const char *optarg);
extern void stress_set_iomix_bytes(const char *optarg);
extern void stress_set_lease_breakers(const char *optarg);
extern void stress_set_lsearch_size(const char *optarg);
extern void stress_set_malloc_bytes(const char *optarg);
extern void stress_set_malloc_max(const char *optarg);
extern void stress_set_malloc_threshold(const char *optarg);
extern int  stress_set_matrix_method(const char *name);
extern void stress_set_matrix_size(const char *optarg);
extern void stress_set_memfd_bytes(const char *optarg);
extern void stress_set_mergesort_size(const void *optarg);
extern void stress_set_mmap_bytes(const char *optarg);
extern void stress_set_mq_size(const char *optarg);
extern void stress_set_mremap_bytes(const char *optarg);
extern void stress_set_msync_bytes(const char *optarg);
extern int  stress_netlink_proc_supported(void);
extern void stress_set_pipe_data_size(const char *optarg);
extern void stress_set_pipe_size(const char *optarg);
extern void stress_set_pthread_max(const char *optarg);
extern void stress_set_qsort_size(const void *optarg);
extern int  stress_rdrand_supported(void);
extern void stress_set_readahead_bytes(const char *optarg);
extern int  stress_set_sctp_domain(const char *optarg);
extern void stress_set_sctp_port(const char *optarg);
extern void stress_set_seek_size(const char *optarg);
extern void stress_set_sendfile_size(const char *optarg);
extern void stress_set_semaphore_posix_procs(const char *optarg);
extern void stress_set_semaphore_sysv_procs(const char *optarg);
extern void stress_set_shm_posix_bytes(const char *optarg);
extern void stress_set_shm_posix_objects(const char *optarg);
extern void stress_set_shm_sysv_bytes(const char *optarg);
extern void stress_set_shm_sysv_segments(const char *optarg);
extern void stress_set_sleep_max(const char *optarg);
extern int  stress_set_socket_domain(const char *name);
extern int  stress_set_socket_opts(const char *optarg);
extern int  stress_set_socket_type(const char *optarg);
extern void stress_set_socket_port(const char *optarg);
extern void stress_set_socket_fd_port(const char *optarg);
extern void stress_set_splice_bytes(const char *optarg);
extern int  stress_set_str_method(const char *name);
extern void stress_set_stream_L3_size(const char *optarg);
extern void stress_set_sync_file_bytes(const char *optarg);
extern int  stress_set_wcs_method(const char *name);
extern void stress_set_timer_freq(const char *optarg);
extern void stress_set_timerfd_freq(const char *optarg);
extern int  stress_tsc_supported(void);
extern void stress_set_tsearch_size(const char *optarg);
extern int  stress_set_udp_domain(const char *name);
extern void stress_set_udp_port(const char *optarg);
extern int  stress_set_udp_flood_domain(const char *name);
extern void stress_set_userfaultfd_bytes(const char *optarg);
extern void stress_set_vfork_max(const char *optarg);
extern void stress_set_vm_bytes(const char *optarg);
extern void stress_set_vm_flags(const int flag);
extern void stress_set_vm_hang(const char *optarg);
extern int  stress_set_vm_method(const char *name);
extern void stress_set_vm_rw_bytes(const char *optarg);
extern void stress_set_vm_splice_bytes(const char *optarg);
extern int  stress_set_zlib_method(const char *name);
extern void stress_set_zombie_max(const char *optarg);

/*
 *  shim'd abstracted system or library calls
 *  that have a layer of OS abstraction
 */
struct shim_linux_dirent {
	unsigned long	d_ino;		/* Inode number */
	unsigned long	d_off;		/* Offset to next linux_dirent */
	unsigned short	d_reclen;	/* Length of this linux_dirent */
	char		d_name[];	/* Filename (null-terminated) */
};

struct shim_linux_dirent64 {
#if defined(__linux__)
	ino64_t		d_ino;		/* 64-bit inode number */
#else
	int64_t		d_ino;		/* 64-bit inode number */
#endif
#if defined(__linux__)
	off64_t		d_off;		/* 64-bit offset to next structure */
#else
	int64_t		d_off;		/* 64-bit offset to next structure */
#endif
	unsigned short	d_reclen;	/* Size of this dirent */
	unsigned char	d_type;		/* File type */
	char		d_name[];	/* Filename (null-terminated) */
};

/* sched_getattr attributes */
struct shim_sched_attr {
	uint32_t size;			/* size of struct */
	uint32_t sched_policy;		/* policy, SCHED_* */
	uint64_t sched_flags;		/* scheduling flags */
	int32_t  sched_nice;		/* nice value SCHED_OTHER, SCHED_BATCH */
	uint32_t sched_priority;	/* priority SCHED_FIFO, SCHED_RR */
	uint64_t sched_runtime;		/* runtime SCHED_DEADLINE, ns */
	uint64_t sched_deadline;	/* deadline time, ns */
	uint64_t sched_period;		/* period, ns */
};

#if defined(__linux__)
typedef	loff_t		shim_loff_t;	/* loff_t shim for linux */
#else
typedef uint64_t	shim_loff_t;	/* loff_t for any other OS */
#endif

extern int shim_ioprio_set(int which, int who, int ioprio);
extern int shim_ioprio_get(int which, int who);
extern int shim_sched_yield(void);
extern int shim_cacheflush(char *addr, int nbytes, int cache) ;
extern ssize_t shim_copy_file_range(int fd_in, shim_loff_t *off_in,
        int fd_out, shim_loff_t *off_out, size_t len, unsigned int flags);
extern int shim_fallocate(int fd, int mode, off_t offset, off_t len);
extern int shim_gettid(void);
extern long shim_getcpu(unsigned *cpu, unsigned *node, void *tcache);
extern int shim_getrandom(void *buff, size_t buflen, unsigned int flags);
extern void shim_clear_cache(char* begin, char *end);
extern long shim_kcmp(int pid1, int pid2, int type, int fd1, int fd2);
extern int shim_syslog(int type, char *bufp, int len);
extern int shim_membarrier(int cmd, int flags);
extern int shim_memfd_create(const char *name, unsigned int flags);
extern int shim_get_mempolicy(int *mode, unsigned long *nodemask,
	unsigned long maxnode, unsigned long addr, unsigned long flags);
extern int shim_set_mempolicy(int mode, unsigned long *nodemask,
	unsigned long maxnode);
extern long shim_mbind(void *addr, unsigned long len,
	int mode, const unsigned long *nodemask,
	unsigned long maxnode, unsigned flags);
extern long shim_migrate_pages(int pid, unsigned long maxnode,
	const unsigned long *old_nodes, const unsigned long *new_nodes);
extern long shim_move_pages(int pid, unsigned long count,
	void **pages, const int *nodes, int *status, int flags);
extern int shim_userfaultfd(int flags);
extern int shim_seccomp(unsigned int operation, unsigned int flags, void *args);
extern int shim_unshare(int flags);
extern int shim_getdents(unsigned int fd, struct shim_linux_dirent *dirp,
	unsigned int count);
extern int shim_getdents64(unsigned int fd, struct shim_linux_dirent64 *dirp,
	unsigned int count);
extern int shim_sched_getattr(pid_t pid, struct shim_sched_attr *attr,
	unsigned int size, unsigned int flags);
extern int shim_sched_setattr(pid_t pid, struct shim_sched_attr *attr,
	unsigned int flags);
extern int shim_mlock2(const void *addr, size_t len, int flags);
extern int shim_usleep(uint64_t usec);
extern char *shim_getlogin(void);
extern int shim_msync(void *addr, size_t length, int flags);
extern int shim_sysfs(int option, ...);
extern int shim_madvise(void *addr, size_t length, int advice);
extern int shim_mincore(void *addr, size_t length, unsigned char *vec);

#define STRESS(func) extern int func(const args_t *args);

/* Stressors */
STRESS(stress_affinity);
STRESS(stress_af_alg);
STRESS(stress_aio);
STRESS(stress_aiol);
STRESS(stress_apparmor);
STRESS(stress_atomic);
STRESS(stress_bigheap);
STRESS(stress_bind_mount);
STRESS(stress_brk);
STRESS(stress_bsearch);
STRESS(stress_cache);
STRESS(stress_cap);
STRESS(stress_chdir);
STRESS(stress_chmod);
STRESS(stress_chown);
STRESS(stress_chroot);
STRESS(stress_clock);
STRESS(stress_clone);
STRESS(stress_context);
STRESS(stress_copy_file);
STRESS(stress_cpu);
STRESS(stress_cpu_online);
STRESS(stress_crypt);
STRESS(stress_daemon);
STRESS(stress_dccp);
STRESS(stress_dentry);
STRESS(stress_dir);
STRESS(stress_dirdeep);
STRESS(stress_dnotify);
STRESS(stress_dup);
STRESS(stress_epoll);
STRESS(stress_eventfd);
STRESS(stress_exec);
STRESS(stress_fallocate);
STRESS(stress_fault);
STRESS(stress_fcntl);
STRESS(stress_fiemap);
STRESS(stress_fifo);
STRESS(stress_filename);
STRESS(stress_flock);
STRESS(stress_fanotify);
STRESS(stress_fork);
STRESS(stress_fp_error);
STRESS(stress_fstat);
STRESS(stress_full);
STRESS(stress_futex);
STRESS(stress_get);
STRESS(stress_getrandom);
STRESS(stress_getdent);
STRESS(stress_handle);
STRESS(stress_hdd);
STRESS(stress_heapsort);
STRESS(stress_hsearch);
STRESS(stress_icache);
STRESS(stress_icmp_flood);
STRESS(stress_inotify);
STRESS(stress_io);
STRESS(stress_iomix);
STRESS(stress_ioprio);
STRESS(stress_itimer);
STRESS(stress_kcmp);
STRESS(stress_key);
STRESS(stress_kill);
STRESS(stress_klog);
STRESS(stress_lease);
STRESS(stress_link);
STRESS(stress_lockbus);
STRESS(stress_locka);
STRESS(stress_lockf);
STRESS(stress_lockofd);
STRESS(stress_longjmp);
STRESS(stress_lsearch);
STRESS(stress_madvise);
STRESS(stress_malloc);
STRESS(stress_matrix);
STRESS(stress_membarrier);
STRESS(stress_memcpy);
STRESS(stress_memfd);
STRESS(stress_mergesort);
STRESS(stress_mincore);
STRESS(stress_mknod);
STRESS(stress_mlock);
STRESS(stress_mmap);
STRESS(stress_mmapfork);
STRESS(stress_mmapmany);
STRESS(stress_mremap);
STRESS(stress_msg);
STRESS(stress_msync);
STRESS(stress_mq);
STRESS(stress_netlink_proc);
STRESS(stress_nice);
STRESS(stress_nop);
STRESS(stress_null);
STRESS(stress_numa);
STRESS(stress_oom_pipe);
STRESS(stress_opcode);
STRESS(stress_open);
STRESS(stress_personality);
STRESS(stress_pipe);
STRESS(stress_poll);
STRESS(stress_procfs);
STRESS(stress_pthread);
STRESS(stress_ptrace);
STRESS(stress_pty);
STRESS(stress_qsort);
STRESS(stress_quota);
STRESS(stress_rdrand);
STRESS(stress_readahead);
STRESS(stress_remap);
STRESS(stress_rename);
STRESS(stress_resources);
STRESS(stress_rlimit);
STRESS(stress_rmap);
STRESS(stress_rtc);
STRESS(stress_schedpolicy);
STRESS(stress_sctp);
STRESS(stress_seal);
STRESS(stress_seccomp);
STRESS(stress_seek);
STRESS(stress_sem);
STRESS(stress_sem_sysv);
STRESS(stress_sendfile);
STRESS(stress_shm);
STRESS(stress_shm_sysv);
STRESS(stress_sigfd);
STRESS(stress_sigfpe);
STRESS(stress_sigpending);
STRESS(stress_sigsegv);
STRESS(stress_sigsuspend);
STRESS(stress_sigq);
STRESS(stress_sleep);
STRESS(stress_sock);
STRESS(stress_sockfd);
STRESS(stress_sockpair);
STRESS(stress_spawn);
STRESS(stress_splice);
STRESS(stress_stack);
STRESS(stress_stackmmap);
STRESS(stress_str);
STRESS(stress_stream);
STRESS(stress_switch);
STRESS(stress_symlink);
STRESS(stress_sync_file);
STRESS(stress_sysinfo);
STRESS(stress_sysfs);
STRESS(stress_tee);
STRESS(stress_timer);
STRESS(stress_timerfd);
STRESS(stress_tlb_shootdown);
STRESS(stress_tmpfs);
STRESS(stress_tsc);
STRESS(stress_tsearch);
STRESS(stress_udp);
STRESS(stress_udp_flood);
STRESS(stress_unshare);
STRESS(stress_urandom);
STRESS(stress_userfaultfd);
STRESS(stress_utime);
STRESS(stress_vecmath);
STRESS(stress_vfork);
STRESS(stress_vforkmany);
STRESS(stress_vm);
STRESS(stress_vm_rw);
STRESS(stress_vm_splice);
STRESS(stress_wait);
STRESS(stress_wcs);
STRESS(stress_xattr);
STRESS(stress_yield);
STRESS(stress_zero);
STRESS(stress_zlib);
STRESS(stress_zombie);

#endif
