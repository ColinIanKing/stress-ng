/*
 * Copyright (C) 2013-2021 Canonical, Ltd.
 * Copyright (C) 2021-2023 Colin Ian King
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
#ifndef STRESS_NG_H
#define STRESS_NG_H

#include "config.h"

#if defined(__ICC) &&		\
    defined(__INTEL_COMPILER)
/* Intel ICC compiler */
#define HAVE_COMPILER_ICC
#elif defined(__PCC__)
/* Portable C compiler */
#define HAVE_COMPILER_PCC
#pragma GCC diagnostic ignored "-Wunknown-pragmas"
#elif defined(__TINYC__)
/* Tiny C Compiler */
#define HAVE_COMPILER_TCC
#undef _FORTIFY_SOURCE
#elif defined(__clang__) && 	\
   (defined(__INTEL_CLANG_COMPILER) || defined(__INTEL_LLVM_COMPILER))
/* Intel ICX compiler */
#define HAVE_COMPILER_ICX
#elif defined(__clang__)
/* clang */
#define HAVE_COMPILER_CLANG
#elif defined(__GNUC__)
/* GNU C compiler */
#define HAVE_COMPILER_GCC
#endif

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#define _ATFILE_SOURCE
#ifndef _LARGEFILE_SOURCE
#define _LARGEFILE_SOURCE
#endif
#ifndef _LARGEFILE64_SOURCE
#define _LARGEFILE64_SOURCE
#endif
#if !defined(HAVE_COMPILER_PCC) && 	\
    !defined(HAVE_COMPILER_TCC) &&	\
    !defined(_FORTIFY_SOURCE)
#define _FORTIFY_SOURCE 	(2)
#endif

#if !defined(_FILE_OFFSET_BITS)
#define _FILE_OFFSET_BITS 	(64)
#endif

/* Some Solaris tool chains only define __sun */
#if defined(__sun) &&	\
    !defined(__sun__)
#define __sun__
#endif

/*
 *  Standard includes, assume we have this as the
 *  minimal standard baseline
 */
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <inttypes.h>
#include <limits.h>
#include <math.h>
#include <pwd.h>
#include <sched.h>
#include <setjmp.h>
#include <signal.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#if defined(HAVE_FEATURES_H)
#include <features.h>
#endif
#if defined(HAVE_LIB_PTHREAD)
#include <pthread.h>
#endif
#if defined(HAVE_STRINGS_H)
#include <strings.h>
#endif
#if defined(HAVE_TERMIOS_H)
#include <termios.h>
#endif
#if defined(HAVE_BSD_STDLIB_H)
#include <bsd/stdlib.h>
#endif
#if defined(HAVE_BSD_STRING_H)
#include <bsd/string.h>
#endif
#if defined(HAVE_BSD_UNISTD_H)
#include <bsd/unistd.h>
#endif

/*
 *  Various sys include files
 */
#include <sys/ioctl.h>
#include <sys/file.h>
#include <sys/mman.h>
#include <sys/resource.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/times.h>
#include <sys/types.h>
#include <sys/wait.h>
#if defined(HAVE_SYS_PARAM_H)
#include <sys/param.h>
#endif
#if defined(HAVE_SYSCALL_H)
#include <sys/syscall.h>
#endif
#if defined(HAVE_SYS_SYSINFO_H)
#include <sys/sysinfo.h>
#if defined(HAVE_COMPILER_GCC) &&	\
    !defined(__GLIBC__)
/* Suppress kernel sysinfo to avoid collision with musl */
#define _LINUX_SYSINFO_H
#endif
#endif
#if defined(HAVE_LINUX_POSIX_TYPES_H)
#include <linux/posix_types.h>
#endif

#include "core-version.h"
#include "core-asm-generic.h"
#include "core-opts.h"

#if defined(CHECK_UNEXPECTED) && 	\
    defined(HAVE_PRAGMA) &&		\
    defined(HAVE_COMPILER_GCC)
#define UNEXPECTED_PRAGMA(x) _Pragma (#x)
#define UNEXPECTED_XSTR(x) UNEXPECTED_STR(x)
#define UNEXPECTED_STR(x) # x
#define UNEXPECTED	UNEXPECTED_PRAGMA(message	\
	"unexpected default codepath: line #" 		\
	UNEXPECTED_XSTR(__LINE__) " in " 		\
	UNEXPECTED_XSTR(__FILE__));
#else
#define UNEXPECTED
#endif

#define VOID_RET(type, x)	\
do {				\
	type void_ret = x;	\
				\
	(void)void_ret;		\
} while (0)			\

/*
 *  BeagleBoneBlack with 4.1.15 kernel does not
 *  define the following, these should be defined
 *  in linux/posix_types.h - define them if they
 *  don't exist.
 */
#if !defined(HAVE_KERNEL_LONG_T)
typedef long int __kernel_long_t;
#endif
#if !defined(HAVE_KERNEL_ULONG_T)
typedef unsigned long int __kernel_ulong_t;
#endif

#if defined(HAVE_RLIMIT_RESOURCE_T)
typedef __rlimit_resource_t shim_rlimit_resource_t;
#else
typedef int shim_rlimit_resource_t;
#endif

#if defined(HAVE_PRIORITY_WHICH_T)
typedef __priority_which_t shim_priority_which_t;
#else
typedef int shim_priority_which_t;
#endif

#if defined(HAVE_ITIMER_WHICH_T)
typedef __itimer_which_t shim_itimer_which_t;
#else
typedef int shim_itimer_which_t;
#endif

#define STRESS_BIT_U(shift)		(1U << (shift))
#define STRESS_BIT_UL(shift)		(1UL << (shift))
#define STRESS_BIT_ULL(shift)		(1ULL << (shift))

/* EXIT_SUCCESS, EXIT_FAILURE defined in stdlib.h */
#define EXIT_NOT_SUCCESS		(2)
#define EXIT_NO_RESOURCE		(3)
#define EXIT_NOT_IMPLEMENTED		(4)
#define EXIT_SIGNALED			(5)
#define EXIT_BY_SYS_EXIT		(6)
#define EXIT_METRICS_UNTRUSTWORTHY	(7)

/*
 *  Stressor run states
 */
#define STRESS_STATE_START		(0)
#define STRESS_STATE_INIT		(1)
#define STRESS_STATE_RUN		(2)
#define STRESS_STATE_DEINIT		(3)
#define STRESS_STATE_STOP		(4)
#define STRESS_STATE_EXIT		(5)
#define STRESS_STATE_WAIT		(6)
#define STRESS_STATE_ZOMBIE		(7)

#define	STRESS_INTERRUPTS_MAX		(8)	/* see core_interrupts.c */

/* oomable flags */
#define	STRESS_OOMABLE_NORMAL		(0x00000000)	/* Normal oomability */
#define STRESS_OOMABLE_DROP_CAP		(0x00000001)	/* Drop capabilities */
#define STRESS_OOMABLE_QUIET		(0x00000002)	/* Don't report activity */

/*
 *  Timing units
 */
#define STRESS_NANOSECOND		(1000000000L)
#define STRESS_MICROSECOND		(1000000L)
#define STRESS_MILLISECOND		(1000L)

#define STRESS_DBL_NANOSECOND		(1000000000.0)
#define STRESS_DBL_MICROSECOND		(1000000.0)
#define STRESS_DBL_MILLISECOND		(1000.0)

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

/* NetBSD does not define MAP_ANONYMOUS */
#if defined(MAP_ANON) && !defined(MAP_ANONYMOUS)
#define MAP_ANONYMOUS MAP_ANON
#endif

/* GNU HURD and other systems that don't define PATH_MAX */
#ifndef PATH_MAX
#define PATH_MAX (4096)
#endif

/*
 * making local static fixes clobbering warnings
 */
#define NOCLOBBER static

#define STRESS_TRY_OPEN_OK	  (0)		/* File can be opened */
#define STRESS_TRY_OPEN_FORK_FAIL (1)		/* Try failed, e.g. can't fork */
#define STRESS_TRY_OPEN_WAIT_FAIL (2)		/* Wait on child open failed */
#define STRESS_TRY_OPEN_EXIT_FAIL (3)		/* Can't get _exit() status */
#define STRESS_TRY_OPEN_FAIL	  (4)		/* Can't open file */
#define STRESS_TRY_AGAIN	  (5)		/* Device busy, try again */

#define STRESS_FD_MAX		(65536)		/* Max fds if we can't figure it out */
#define STRESS_PROCS_MAX	(8192)		/* Max number of processes per stressor */

#define ABORT_FAILURES		(5)		/* Number of failures before we abort */

/* debug output bitmasks */
#define PR_ERROR		 STRESS_BIT_ULL(0)	/* Print errors */
#define PR_INFO			 STRESS_BIT_ULL(1)	/* Print info */
#define PR_DEBUG		 STRESS_BIT_ULL(2) 	/* Print debug */
#define PR_FAIL			 STRESS_BIT_ULL(3) 	/* Print test failure message */
#define PR_WARN			 STRESS_BIT_ULL(4)	/* Print warning */
#define PR_METRICS		 STRESS_BIT_ULL(5)	/* Print metrics */
#define PR_ALL			 (PR_ERROR | PR_INFO | PR_DEBUG | PR_FAIL | PR_WARN | PR_METRICS)

/* Option bit masks, stats from the next PR_ option onwards */
#define OPT_FLAGS_METRICS	 STRESS_BIT_ULL(6)	/* --metrics, Dump metrics at end */
#define OPT_FLAGS_RANDOM	 STRESS_BIT_ULL(7)	/* --random, Randomize */
#define OPT_FLAGS_SET		 STRESS_BIT_ULL(8)	/* Set if user specifies stress procs */
#define OPT_FLAGS_KEEP_NAME	 STRESS_BIT_ULL(9)	/* --keep-name, Keep stress names to stress-ng */
#define OPT_FLAGS_METRICS_BRIEF	 STRESS_BIT_ULL(10)	/* --metrics-brief, dump brief metrics */
#define OPT_FLAGS_VERIFY	 STRESS_BIT_ULL(11)	/* --verify, verify mode */
#define OPT_FLAGS_MMAP_MADVISE	 STRESS_BIT_ULL(12)	/* --no-madvise, disable random madvise settings */
#define OPT_FLAGS_MMAP_MINCORE	 STRESS_BIT_ULL(13)	/* --page-in, mincore force pages into mem */
#define OPT_FLAGS_TIMES		 STRESS_BIT_ULL(14)	/* --times, user/system time summary */
#define OPT_FLAGS_MINIMIZE	 STRESS_BIT_ULL(15)	/* --minimize, Minimize */
#define OPT_FLAGS_MAXIMIZE	 STRESS_BIT_ULL(16)	/* --maximize Maximize */
#define OPT_FLAGS_SYSLOG	 STRESS_BIT_ULL(17)	/* --syslog, log test progress to syslog */
#define OPT_FLAGS_AGGRESSIVE	 STRESS_BIT_ULL(18)	/* --aggressive, aggressive mode enabled */
#define OPT_FLAGS_ALL		 STRESS_BIT_ULL(19)	/* --all mode */
#define OPT_FLAGS_SEQUENTIAL	 STRESS_BIT_ULL(20)	/* --sequential mode */
#define OPT_FLAGS_PERF_STATS	 STRESS_BIT_ULL(21)	/* --perf stats mode */
#define OPT_FLAGS_LOG_BRIEF	 STRESS_BIT_ULL(22)	/* --log-brief */
#define OPT_FLAGS_THERMAL_ZONES  STRESS_BIT_ULL(23)	/* --tz thermal zones */
#define OPT_FLAGS_SOCKET_NODELAY STRESS_BIT_ULL(24)	/* --sock-nodelay */
#define OPT_FLAGS_IGNITE_CPU	 STRESS_BIT_ULL(25)	/* --cpu-ignite */
#define OPT_FLAGS_PATHOLOGICAL	 STRESS_BIT_ULL(26)	/* --pathological */
#define OPT_FLAGS_NO_RAND_SEED	 STRESS_BIT_ULL(27)	/* --no-rand-seed */
#define OPT_FLAGS_THRASH	 STRESS_BIT_ULL(28)	/* --thrash */
#define OPT_FLAGS_OOMABLE	 STRESS_BIT_ULL(29)	/* --oomable */
#define OPT_FLAGS_ABORT		 STRESS_BIT_ULL(30)	/* --abort */
#define OPT_FLAGS_TIMESTAMP	 STRESS_BIT_ULL(31)	/* --timestamp */
#define OPT_FLAGS_DEADLINE_GRUB  STRESS_BIT_ULL(32)	/* --sched-reclaim */
#define OPT_FLAGS_FTRACE	 STRESS_BIT_ULL(33)	/* --ftrace */
#define OPT_FLAGS_SEED		 STRESS_BIT_ULL(34)	/* --seed */
#define OPT_FLAGS_SKIP_SILENT	 STRESS_BIT_ULL(35)	/* --skip-silent */
#define OPT_FLAGS_SMART		 STRESS_BIT_ULL(36)	/* --smart */
#define OPT_FLAGS_NO_OOM_ADJUST	 STRESS_BIT_ULL(37)	/* --no-oom-adjust */
#define OPT_FLAGS_KEEP_FILES	 STRESS_BIT_ULL(38)	/* --keep-files */
#define OPT_FLAGS_STDERR	 STRESS_BIT_ULL(39)	/* --stderr */
#define OPT_FLAGS_STDOUT	 STRESS_BIT_ULL(40)	/* --stdout */
#define OPT_FLAGS_KLOG_CHECK	 STRESS_BIT_ULL(41)	/* --klog-check */
#define OPT_FLAGS_DRY_RUN	 STRESS_BIT_ULL(42)	/* --dry-run, don't actually run */
#define OPT_FLAGS_OOM_AVOID	 STRESS_BIT_ULL(43)	/* --oom-avoid */
#define OPT_FLAGS_TZ_INFO	 STRESS_BIT_ULL(44)	/* --tz, enable thermal zone info */
#define OPT_FLAGS_LOG_LOCKLESS	 STRESS_BIT_ULL(45)	/* --log-lockless */
#define OPT_FLAGS_SN		 STRESS_BIT_ULL(46)	/* --sn scientific notation */
#define OPT_FLAGS_CHANGE_CPU	 STRESS_BIT_ULL(47)	/* --change-cpu */
#define OPT_FLAGS_KSM		 STRESS_BIT_ULL(48)	/* --ksm */
#define OPT_FLAGS_SETTINGS	 STRESS_BIT_ULL(49)	/* --settings */
#define OPT_FLAGS_WITH		 STRESS_BIT_ULL(50)	/* --with list */
#define OPT_FLAGS_PERMUTE	 STRESS_BIT_ULL(51)	/* --permute N */

#define OPT_FLAGS_MINMAX_MASK		\
	(OPT_FLAGS_MINIMIZE | OPT_FLAGS_MAXIMIZE)

/* Aggressive mode flags */
#define OPT_FLAGS_AGGRESSIVE_MASK 	\
	(OPT_FLAGS_MMAP_MADVISE |	\
	 OPT_FLAGS_MMAP_MINCORE |	\
	 OPT_FLAGS_AGGRESSIVE |		\
	 OPT_FLAGS_IGNITE_CPU)

/* Stressor classes */
#define CLASS_CPU		STRESS_BIT_UL(0)	/* CPU only */
#define CLASS_MEMORY		STRESS_BIT_UL(1)	/* Memory thrashers */
#define CLASS_CPU_CACHE		STRESS_BIT_UL(2)	/* CPU cache */
#define CLASS_IO		STRESS_BIT_UL(3)	/* I/O read/writes etc */
#define CLASS_NETWORK		STRESS_BIT_UL(4)	/* Network, sockets, etc */
#define CLASS_SCHEDULER		STRESS_BIT_UL(5)	/* Scheduling */
#define CLASS_VM		STRESS_BIT_UL(6)	/* VM stress, big memory, swapping */
#define CLASS_INTERRUPT		STRESS_BIT_UL(7)	/* interrupt floods */
#define CLASS_OS		STRESS_BIT_UL(8)	/* generic OS tests */
#define CLASS_PIPE_IO		STRESS_BIT_UL(9)	/* pipe I/O */
#define CLASS_FILESYSTEM	STRESS_BIT_UL(10)	/* file system */
#define CLASS_DEV		STRESS_BIT_UL(11)	/* device (null, zero, etc) */
#define CLASS_SECURITY		STRESS_BIT_UL(12)	/* security APIs */
#define CLASS_PATHOLOGICAL	STRESS_BIT_UL(13)	/* can hang a machine */
#define CLASS_GPU		STRESS_BIT_UL(14)	/* GPU */

/* Help information for options */
typedef struct {
	const char *opt_s;		/* short option */
	const char *opt_l;		/* long option */
	const char *description;	/* description */
} stress_help_t;

/* native setting types */
typedef enum {
	TYPE_ID_UNDEFINED,		/* no-id */
	TYPE_ID_UINT8,			/* uint8_t */
	TYPE_ID_INT8,			/* int8_t */
	TYPE_ID_UINT16,			/* uint16_t */
	TYPE_ID_INT16,			/* int16_t */
	TYPE_ID_UINT32,			/* uint32_t */
	TYPE_ID_INT32,			/* int32_t */
	TYPE_ID_UINT64,			/* uint64_t */
	TYPE_ID_INT64,			/* int64_t */
	TYPE_ID_SIZE_T,			/* size_t */
	TYPE_ID_SSIZE_T,		/* ssize_t */
	TYPE_ID_UINT,			/* unsigned int */
	TYPE_ID_INT,			/* signed int */
	TYPE_ID_ULONG,			/* unsigned long */
	TYPE_ID_LONG,			/* signed long */
	TYPE_ID_OFF_T,			/* off_t */
	TYPE_ID_STR,			/* char * */
	TYPE_ID_BOOL,			/* bool */
} stress_type_id_t;

typedef struct {
	uint64_t counter;		/* bogo-op counter */
	bool counter_ready;		/* ready flag */
	bool run_ok;			/* stressor run w/o issues */
	bool force_killed;		/* true if sent SIGKILL */
} stress_counter_info_t;

/*
 *  Per ELISA request, we have a duplicated counter
 *  and run_ok flag in a different shared memory region
 *  so we can sanity check these just in case the stats
 *  have got corrupted.
 */
typedef struct {
	struct {
		stress_counter_info_t ci; /* Copy of stats counter info ci */
		uint8_t	reserved[7];	/* Padding */
	} data;
	uint32_t hash;			/* Hash of data */
} stress_checksum_t;

/*
 *  Scratch space to store computed values to ensure
 *  compiler does not compile away calculations
 */
typedef union {
	volatile uint8_t	uint8_val;
	volatile uint16_t	uint16_val;
	volatile uint32_t	uint32_val;
	volatile uint64_t	uint64_val;
#if defined(HAVE_INT128_T)
	volatile __uint128_t	uint128_val;
#endif
	volatile float		float_val;
	volatile double		double_val;
	volatile long double	long_double_val;
	volatile void 		*void_ptr_val;
} stress_put_val_t;

typedef uint32_t stress_class_t;

typedef struct {
	void *page_none;		/* mmap'd PROT_NONE page */
	void *page_ro;			/* mmap'd PROT_RO page */
	void *page_wo;			/* mmap'd PROT_WO page */
} stress_mapped_t;

#define STRESS_MISC_METRICS_MAX	(40)

typedef struct {
	void *lock;			/* optional lock */
	double	duration;		/* time per op */
	double	count;			/* number of ops */
	volatile double	t_start;	/* optional start time */
} stress_metrics_t;

typedef struct {
	char *description;		/* description of metric */
	double value;			/* value of metric */
} stress_metrics_data_t;

/* stressor args */
typedef struct {
	stress_counter_info_t *ci;	/* counter info struct */
	const char *name;		/* stressor name */
	uint64_t max_ops;		/* max number of bogo ops */
	const uint32_t instance;	/* stressor instance # */
	const uint32_t num_instances;	/* number of instances */
	pid_t pid;			/* stressor pid */
	size_t page_size;		/* page size */
	double time_end;		/* when to end */
	stress_mapped_t *mapped;	/* mmap'd pages, addr of g_shared mapped */
	stress_metrics_data_t *metrics;	/* misc per stressor metrics */
	const struct stressor_info *info; /* stressor info */
} stress_args_t;

typedef struct {
	const int opt;			/* optarg option*/
	int (*opt_set_func)(const char *opt); /* function to set it */
} stress_opt_set_func_t;

typedef enum {
	VERIFY_NONE	= 0x00,		/* no verification */
	VERIFY_OPTIONAL = 0x01,		/* --verify can enable verification */
	VERIFY_ALWAYS   = 0x02,		/* verification always enabled */
} stress_verify_t;

/* stressor information */
typedef struct stressor_info {
	int (*stressor)(const stress_args_t *args);	/* stressor function */
	int (*supported)(const char *name);	/* return 0 = supported, -1, not */
	void (*init)(void);		/* stressor init, NULL = ignore */
	void (*deinit)(void);		/* stressor de-init, NULL = ignore */
	void (*set_default)(void);	/* default set-up */
	void (*set_limit)(uint64_t max);/* set limits */
	const stress_opt_set_func_t *opt_set_funcs;	/* option functions */
	const stress_help_t *help;	/* stressor help options */
	const stress_class_t class;	/* stressor class */
	const stress_verify_t verify;	/* verification mode */
	const char *unimplemented_reason;	/* unsupported reason message */
} stressor_info_t;

/* gcc 4.7 and later support vector ops */
#if defined(HAVE_COMPILER_GCC) &&	\
    NEED_GNUC(4, 7, 0)
#define STRESS_VECTOR	(1)
#endif

/* gcc 7.0 and later support __attribute__((fallthrough)); */
#if defined(HAVE_ATTRIBUTE_FALLTHROUGH)
#define CASE_FALLTHROUGH __attribute__((fallthrough))
#else
#define CASE_FALLTHROUGH
#endif

#if defined(HAVE_ATTRIBUTE_FAST_MATH) &&		\
    !defined(HAVE_COMPILER_ICC) &&			\
    defined(HAVE_COMPILER_GCC) &&			\
    NEED_GNUC(10, 0, 0)
#define OPTIMIZE_FAST_MATH __attribute__((optimize("fast-math")))
#else
#define OPTIMIZE_FAST_MATH
#endif

/* no return hint */
#if (defined(HAVE_COMPILER_GCC) && NEED_GNUC(2, 5, 0)) || 	\
    (defined(HAVE_COMPILER_CLANG) && NEED_CLANG(3, 0, 0))
#define NORETURN 	__attribute__((noreturn))
#else
#define NORETURN
#endif

/* weak attribute */
#if (defined(HAVE_COMPILER_GCC) && NEED_GNUC(4, 0, 0)) || 	\
    (defined(HAVE_COMPILER_CLANG) && NEED_CLANG(3, 4, 0))
#define WEAK		__attribute__((weak))
#define HAVE_WEAK_ATTRIBUTE
#else
#define WEAK
#endif

#if defined(ALWAYS_INLINE)
#undef ALWAYS_INLINE
#endif
/* force inlining hint */
#if (defined(HAVE_COMPILER_GCC) && NEED_GNUC(3, 4, 0) 				\
     && ((!defined(__s390__) && !defined(__s390x__)) || NEED_GNUC(6, 0, 1))) ||	\
    (defined(HAVE_COMPILER_CLANG) && NEED_CLANG(3, 0, 0))
#define ALWAYS_INLINE	__attribute__((always_inline))
#else
#define ALWAYS_INLINE
#endif

/* force no inlining hint */
#if (defined(HAVE_COMPILER_GCC) && NEED_GNUC(3, 4, 0)) ||	\
    (defined(HAVE_COMPILER_CLANG) && NEED_CLANG(3, 0, 0))
#define NOINLINE	__attribute__((noinline))
#else
#define NOINLINE
#endif

/* -O3 attribute support */
#if defined(HAVE_COMPILER_GCC) &&	\
    !defined(HAVE_COMPILER_CLANG) &&	\
    !defined(HAVE_COMPILER_ICC) &&	\
    NEED_GNUC(4, 6, 0)
#define OPTIMIZE3 	__attribute__((optimize("-O3")))
#else
#define OPTIMIZE3
#endif

/* -O2 attribute support */
#if defined(HAVE_COMPILER_GCC) &&	\
    !defined(HAVE_COMPILER_CLANG) &&	\
    !defined(HAVE_COMPILER_ICC) &&	\
    NEED_GNUC(4, 6, 0)
#define OPTIMIZE2 	__attribute__((optimize("-O2")))
#else
#define OPTIMIZE2
#endif

/* -O1 attribute support */
#if defined(HAVE_COMPILER_GCC) &&	\
    !defined(HAVE_COMPILER_CLANG) &&	\
    !defined(HAVE_COMPILER_ICC) &&	\
    NEED_GNUC(4, 6, 0)
#define OPTIMIZE1 	__attribute__((optimize("-O1")))
#else
#define OPTIMIZE1
#endif

/* -O0 attribute support */
#if defined(HAVE_COMPILER_GCC) &&	\
    !defined(HAVE_COMPILER_ICC) &&	\
    NEED_GNUC(4, 6, 0)
#define OPTIMIZE0 	__attribute__((optimize("-O0")))
#elif (defined(HAVE_COMPILER_CLANG) && NEED_CLANG(10, 0, 0))
#define OPTIMIZE0	__attribute__((optnone))
#else
#define OPTIMIZE0
#endif

/* warn unused attribute */
#if (defined(HAVE_COMPILER_GCC) && NEED_GNUC(4, 2, 0)) ||	\
    (defined(HAVE_COMPILER_CLANG) && NEED_CLANG(3, 0, 0))
#define WARN_UNUSED	__attribute__((warn_unused_result))
#else
#define WARN_UNUSED
#endif

#if ((defined(HAVE_COMPILER_GCC) && NEED_GNUC(3, 3, 0)) ||	\
     (defined(HAVE_COMPILER_CLANG) && NEED_CLANG(3, 0, 0)) ||	\
     (defined(HAVE_COMPILER_ICC) && NEED_ICC(2021, 0, 0))) &&	\
    !defined(HAVE_COMPILER_PCC) &&				\
    !defined(__minix__)
#define ALIGNED(a)	__attribute__((aligned(a)))
#else
#define ALIGNED(a)
#endif

/* Force alignment macros */
#define ALIGN128	ALIGNED(128)
#define ALIGN64		ALIGNED(64)


#if (defined(HAVE_COMPILER_GCC) && NEED_GNUC(4, 6, 0)) ||	\
    (defined(HAVE_COMPILER_CLANG) && NEED_CLANG(3, 0, 0))
#if (defined(__APPLE__) && defined(__MACH__))
#define SECTION(s)	__attribute__((__section__(# s "," # s)))
#else
#define SECTION(s)	__attribute__((__section__(# s)))
#endif
#else
#define SECTION(s)
#endif

/* GCC hot attribute */
#if (defined(HAVE_COMPILER_GCC) && NEED_GNUC(4, 6, 0)) ||	\
    (defined(HAVE_COMPILER_CLANG) && NEED_CLANG(3, 3, 0))
#define HOT		__attribute__((hot))
#else
#define HOT
#endif

/* GCC mlocked data and data section attribute */
#if ((defined(HAVE_COMPILER_GCC) && NEED_GNUC(4, 6, 0) ||	\
     (defined(HAVE_COMPILER_CLANG) && NEED_CLANG(3, 0, 0)))) &&	\
    !defined(__sun__) &&					\
    !defined(__APPLE__) &&					\
    !defined(BUILD_STATIC)
#define MLOCKED_TEXT	__attribute__((__section__("mlocked_text")))
#define MLOCKED_SECTION	(1)
#else
#define MLOCKED_TEXT
#endif

/* print format attribute */
#if ((defined(HAVE_COMPILER_GCC) && NEED_GNUC(3, 2, 0)) ||	\
     (defined(HAVE_COMPILER_CLANG) && NEED_CLANG(3, 0, 0)))
#define FORMAT(func, a, b) __attribute__((format(func, a, b)))
#else
#define FORMAT(func, a, b)
#endif

/* restrict keyword */
#if defined(HAVE___RESTRICT)
#define RESTRICT __restrict
#else
#define RESTRICT
#endif

/* optimisation on branching */
#if defined(HAVE_BUILTIN_EXPECT)
#define LIKELY(x)	__builtin_expect((x),1)
#define UNLIKELY(x)	__builtin_expect((x),0)
#else
#define LIKELY(x)	(x)
#define UNLIKELY(x)	(x)
#endif

/* use syscall if we can, fallback to vfork otherwise */
#define shim_vfork()		g_shared->vfork()

extern const char stress_config[];

/* Printing/Logging helpers */
extern int  pr_yaml(FILE *fp, const char *const fmt, ...) FORMAT(printf, 2, 3);
extern void pr_yaml_runinfo(FILE *fp);
extern void pr_runinfo(void);
extern void pr_openlog(const char *filename);
extern void pr_closelog(void);
extern void pr_fail_check(int *rc);

extern void pr_dbg(const char *fmt, ...)  	FORMAT(printf, 1, 2);
extern void pr_dbg_skip(const char *fmt, ...)	FORMAT(printf, 1, 2);
extern void pr_inf(const char *fmt, ...)	FORMAT(printf, 1, 2);
extern void pr_inf_skip(const char *fmt, ...)	FORMAT(printf, 1, 2);
extern void pr_err(const char *fmt, ...)	FORMAT(printf, 1, 2);
extern void pr_err_skip(const char *fmt, ...)	FORMAT(printf, 1, 2);
extern void pr_fail(const char *fmt, ...)	FORMAT(printf, 1, 2);
extern void pr_tidy(const char *fmt, ...)	FORMAT(printf, 1, 2);
extern void pr_warn(const char *fmt, ...)	FORMAT(printf, 1, 2);
extern void pr_warn_skip(const char *fmt, ...)	FORMAT(printf, 1, 2);
extern void pr_metrics(const char *fmt, ...)	FORMAT(printf, 1, 2);

extern void pr_lock_init(void);
extern void pr_lock(void);
extern void pr_unlock(void);
extern void pr_lock_exited(const pid_t pid);

/* Memory size constants */
#define KB			(1ULL << 10)
#define	MB			(1ULL << 20)
#define GB			(1ULL << 30)
#define TB			(1ULL << 40)
#define PB			(1ULL << 50)
#define EB			(1ULL << 60)

#define ONE_BILLIONTH		(1.0E-9)
#define ONE_MILLIONTH		(1.0E-6)
#define ONE_THOUSANDTH		(1.0E-3)

#define STACK_ALIGNMENT		(64)	/* thread stacks align to 64 bytes */

#define MIN_OPS			(1ULL)
#define MAX_OPS			(100000000ULL)
#define MAX_32			(0xffffffffUL)
#define MAX_48			(0xffffffffffffULL)
#define MAX_64			(0xffffffffffffffffULL)

/* Maximum memory limits, 256TB for 64 bit is good enough for 2017 */
#if UINTPTR_MAX == MAX_32
#define MAX_MEM_LIMIT		(MAX_32)
#else
#define MAX_MEM_LIMIT		(MAX_48)
#endif

#define MAX_FILE_LIMIT		((1ULL << ((sizeof(off_t) * 8) - 1)) - 1)
/*
 * --maximize files must not be so big that we fill up
 * a disk, so make them either the MAX_FILE_FILE_LIMIT for
 * systems with small off_t or 4GB for large off_t systems
 */
#define MAXIMIZED_FILE_SIZE	((sizeof(off_t) < 8) ? MAX_FILE_LIMIT : MAX_32)

/* Stressor defaults */

#define MIN_SEQUENTIAL		(0)
#define MAX_SEQUENTIAL		(1000000)
#define DEFAULT_SEQUENTIAL	(0)	/* Disabled */
#define DEFAULT_PARALLEL	(0)	/* Disabled */

#define TIMEOUT_NOT_SET		(~0ULL)
#define DEFAULT_TIMEOUT		(60 * 60 * 24)
#define DEFAULT_BACKOFF		(0)
#define DEFAULT_LINKS		(8192)
#define DEFAULT_DIRS		(8192)

#define MEM_CACHE_SIZE		(65536 * 32)
#define DEFAULT_CACHE_LEVEL     (3)
#define UNDEFINED		(-1)

#define PAGE_MAPPED		(0x01)
#define PAGE_MAPPED_FAIL	(0x02)

#if defined(HAVE_COMPILER_GCC) || defined(HAVE_COMPILER_CLANG)
#define TYPEOF_CAST(a)	(typeof(a))
#else
#define	TYPEOF_CAST(a)
#endif

/* Generic bit setting on an array macros */
#define STRESS_NBITS(a)		(sizeof(a[0]) * 8)
#define STRESS_GETBIT(a, i)	(a[i / STRESS_NBITS(a)] & \
				 (TYPEOF_CAST(a[0])1U << (i & (STRESS_NBITS(a)-1))))
#define STRESS_CLRBIT(a, i)	(a[i / STRESS_NBITS(a)] &= \
				 ~(TYPEOF_CAST(a[0])1U << (i & (STRESS_NBITS(a)-1))))
#define STRESS_SETBIT(a, i)	(a[i / STRESS_NBITS(a)] |= \
				 (TYPEOF_CAST(a[0])1U << (i & (STRESS_NBITS(a)-1))))

#define SIZEOF_ARRAY(a)		(sizeof(a) / sizeof(a[0]))

/*
 *  abstracted untyped locking primitives
 */
extern WARN_UNUSED void *stress_lock_create(void);
extern int stress_lock_destroy(void *lock_handle);
extern int stress_lock_acquire(void *lock_handle);
extern int stress_lock_release(void *lock_handle);

/* stress process prototype */
typedef int (*stress_func_t)(const stress_args_t *args);

/* perf related constants */
#if defined(HAVE_LIB_PTHREAD) &&	\
    defined(HAVE_LINUX_PERF_EVENT_H) &&	\
    defined(__NR_perf_event_open)
#define STRESS_PERF_STATS	(1)
#define STRESS_PERF_INVALID	(~0ULL)
#define STRESS_PERF_MAX		(128 + 16)

/* per perf counter info */
typedef struct {
	uint64_t counter;		/* perf counter */
	int	 fd;			/* perf per counter fd */
	uint8_t	 padding[4];		/* padding */
} stress_perf_stat_t;

/* per stressor perf info */
typedef struct {
	stress_perf_stat_t perf_stat[STRESS_PERF_MAX]; /* perf counters */
	int perf_opened;		/* count of opened counters */
	uint8_t	padding[4];		/* padding */
} stress_perf_t;
#endif

/* linux thermal zones */
#define	STRESS_THERMAL_ZONES	 (1)
#define STRESS_THERMAL_ZONES_MAX (31)	/* best if prime */

#if defined(STRESS_THERMAL_ZONES)
/* per stressor thermal zone info */
typedef struct stress_tz_info {
	char	*path;			/* thermal zone path */
	char 	*type;			/* thermal zone type */
	uint32_t type_instance;		/* thermal zone instance # */
	size_t	index;			/* thermal zone # index */
	struct stress_tz_info *next;	/* next thermal zone in list */
} stress_tz_info_t;

typedef struct {
	uint64_t temperature;		/* temperature in Celsius * 1000 */
} stress_tz_stat_t;

typedef struct {
	stress_tz_stat_t tz_stat[STRESS_THERMAL_ZONES_MAX];
} stress_tz_t;
#endif

typedef struct {
	uint64_t count_start;
	uint64_t count_stop;
} stress_interrupts_t;

/* Per stressor statistics and accounting info */
typedef struct {
	stress_counter_info_t ci;	/* counter info */
	double start;			/* wall clock start time */
	double duration;		/* finish - start */
	uint64_t counter_total;		/* counter total */
	double duration_total;		/* wall clock duration */
	pid_t pid;			/* stressor pid */
	bool sigalarmed;		/* set true if signalled with SIGALRM */
	bool signalled;			/* set true if signalled with a kill */
	bool completed;			/* true if stressor completed */
#if defined(STRESS_PERF_STATS)
	stress_perf_t sp;		/* perf counters */
#endif
#if defined(STRESS_THERMAL_ZONES)
	stress_tz_t tz;			/* thermal zones */
#endif
	stress_checksum_t *checksum;	/* pointer to checksum data */
	stress_interrupts_t interrupts[STRESS_INTERRUPTS_MAX];
	stress_metrics_data_t metrics[STRESS_MISC_METRICS_MAX];
	double rusage_utime;		/* rusage user time */
	double rusage_stime;		/* rusage system time */
	double rusage_utime_total;	/* rusage user time */
	double rusage_stime_total;	/* rusage system time */
	long int rusage_maxrss;		/* rusage max RSS, 0 = unused */
} stress_stats_t;

typedef struct shared_heap {
	void *str_list_head;		/* list of heap strings */
	void *lock;			/* heap global lock */
	void *heap;			/* mmap'd heap */
	size_t heap_size;		/* heap size */
	size_t offset;			/* next free offset in current slab */
	bool out_of_memory;		/* true if allocation failed */
} shared_heap_t;

#define	STRESS_WARN_HASH_MAX		(128)

/* The stress-ng global shared memory segment */
typedef struct {
	size_t length;			/* Size of shared segment */
	double time_started;		/* Time when stressing started */
	const uint64_t zero;		/* zero'd 64 bit data */
	void *nullptr;			/* Null pointer */
	uint64_t klog_errors;		/* Number of errors detected in klog */
	bool caught_sigint;		/* True if SIGINT caught */
	pid_t (*vfork)(void);		/* vfork syscall */
	stress_mapped_t mapped;		/* mmap'd pages to help testing */
	shared_heap_t shared_heap;
	struct {
		void *lock;		/* Cacheline stressor lock */
		int index;		/* Cacheline stressor index */
		uint8_t *buffer;	/* Cacheline stressor buffer */
		size_t size;		/* Cacheline buffer size */
	} cacheline;
	struct {
		uint32_t started;	/* Number of stressors started */
		uint32_t exited;	/* Number of stressors exited */
		uint32_t reaped;	/* Number of stressors reaped */
		uint32_t failed;	/* Number of stressors failed */
		uint32_t alarmed;	/* Number of stressors got SIGALRM */
	} instance_count;
	struct {
		uint8_t	*buffer;	/* Shared memory cache buffer */
		uint64_t size;		/* buffer size in bytes */
		uint16_t level;		/* 1=L1, 2=L2, 3=L3 */
		uint16_t padding1;	/* alignment padding */
		uint32_t ways;		/* cache ways size */
	} mem_cache;
#if defined(HAVE_ATOMIC_COMPARE_EXCHANGE) &&	\
    defined(HAVE_ATOMIC_STORE)
	struct {
		double whence;		/* pr_* lock time */
		pid_t atomic_lock;	/* pr_* atomic spinlock */
		int lock_count;		/* pr_* lock count, release when zero */
		pid_t pid;		/* pid owning the lock */
	} pr;
#endif
	struct {
		uint32_t hash[STRESS_WARN_HASH_MAX]; /* hash patterns */
		void *lock;		/* protection lock */
	} warn_once;
	union {
		uint64_t val64[1] ALIGN64;
		uint32_t val32[2] ALIGN64;
		uint16_t val16[4] ALIGN64;
		uint8_t	 val8[8] ALIGN64;
	} atomic;			/* Shared atomic temp vars */
	struct {
		/* futexes must be aligned to avoid -EINVAL */
		uint32_t futex[STRESS_PROCS_MAX] ALIGNED(4);/* Shared futexes */
		uint64_t timeout[STRESS_PROCS_MAX];	/* Shared futex timeouts */
	} futex;
#if defined(HAVE_SEM_SYSV) && 	\
    defined(HAVE_KEY_T)
	struct {
		key_t key_id;		/* System V semaphore key id */
		int sem_id;		/* System V semaphore id */
		bool init;		/* System V semaphore initialized */
	} sem_sysv;
#endif
#if defined(STRESS_PERF_STATS)
	struct {
		bool no_perf;		/* true = Perf not available */
		void *lock;		/* lock on no_perf updates */
	} perf;
#endif
#if defined(STRESS_THERMAL_ZONES)
	stress_tz_info_t *tz_info;	/* List of valid thermal zones */
#endif
	struct {
		double start_time ALIGNED(8);	/* Time to complete operation */
		uint32_t value;		/* Dummy value to operate on */
	} syncload;
	struct {
		stress_checksum_t *checksums;	/* per stressor counter checksum */
		size_t	length;		/* size of checksums mapping */
	} checksum;
	struct {
		uint8_t allocated[65536 / sizeof(uint8_t)];	/* allocation bitmap */
		void *lock;		/* lock for allocator */
	} net_port_map;
	struct {
		uint32_t ready;		/* incremented when rawsock stressor is ready */
	} rawsock;
	stress_stats_t stats[];		/* Shared statistics */
} stress_shared_t;

/* stress test metadata */
typedef struct {
	const stressor_info_t *info;	/* stress test info */
	const unsigned int id;		/* stress test ID */
	const short int short_getopt;	/* getopt short option */
	const stress_op_t op;		/* ops option */
	const char *name;		/* name of stress test */
} stress_t;

#define STRESS_STRESSOR_STATUS_PASSED		(0)
#define STRESS_STRESSOR_STATUS_FAILED		(1)
#define STRESS_STRESSOR_STATUS_SKIPPED		(2)
#define STRESS_STRESSOR_STATUS_BAD_METRICS	(3)
#define STRESS_STRESSOR_STATUS_MAX		(4)

/* stress_stressor_info ignore value. 2 bits */
#define STRESS_STRESSOR_NOT_IGNORED		(0)
#define STRESS_STRESSOR_UNSUPPORTED		(1)
#define STRESS_STRESSOR_EXCLUDED		(2)

/* Per stressor information */
typedef struct stress_stressor_info {
	struct stress_stressor_info *next; /* next proc info struct in list */
	struct stress_stressor_info *prev; /* prev proc info struct in list */
	const stress_t *stressor;	/* stressor */
	stress_stats_t **stats;		/* stressor stats info */
	int32_t completed_instances;	/* count of completed instances */
	int32_t num_instances;		/* number of instances per stressor */
	uint64_t bogo_ops;		/* number of bogo ops */
	uint32_t status[STRESS_STRESSOR_STATUS_MAX];
					/* number of instances that passed/failed/skipped */
	struct {
		uint8_t run;		/* ignore running the stressor, unsupported or excluded */
		bool	permute;	/* ignore flag, saved for permute */
	} ignore;
} stress_stressor_t;


/* Pointer to current running stressor proc info */
extern stress_stressor_t *g_stressor_current;

/* Scale lookup mapping, suffix -> scale by */
typedef struct {
	const char	ch;		/* Scaling suffix */
	const uint64_t	scale;		/* Amount to scale by */
} stress_scale_t;

/* Various global option settings and flags */
extern const char g_app_name[];		/* Name of application */
extern stress_shared_t *g_shared;	/* shared memory */
extern uint64_t	g_opt_timeout;		/* timeout in seconds */
extern uint64_t	g_opt_flags;		/* option flags */
extern volatile bool g_stress_continue_flag; /* false to exit stressor */
extern jmp_buf g_error_env;		/* parsing error env */

/*
 *  stress_continue_flag()
 *	get stress_continue_flag state
 */
static inline bool ALWAYS_INLINE OPTIMIZE3 stress_continue_flag(void)
{
	return g_stress_continue_flag;
}

/*
 *  stress_continue_set_flag()
 *	set stress_continue_flag state
 */
static inline void ALWAYS_INLINE OPTIMIZE3 stress_continue_set_flag(const bool setting)
{
	g_stress_continue_flag = setting;
}

/*
 *  stress_bogo_add()
 *	add inc to the stessor bogo ops counter
 *	NOTE: try to only add to the counter inside a stressor
 *	and not a child process of a stressor. If one has to add to
 *	the counter in a child and the child is force KILL'd then indicate
 *	so with the stress_force_killed_bogo() call from the parent.
 */
static inline void ALWAYS_INLINE OPTIMIZE3 stress_bogo_add(const stress_args_t *args, const uint64_t inc)
{
	register stress_counter_info_t * const ci = args->ci;

	ci->counter_ready = false;
	stress_asm_mb();
	ci->counter += inc;
	stress_asm_mb();
	ci->counter_ready = true;
}

/*
 *  stress_bogo_inc()
 *	increment the stessor bogo ops counter
 *	NOTE: try to only increment the counter inside a stressor
 *	and not a child process of a stressor. If one has to increment
 *	the counter in a child and the child is force KILL'd then indicate
 *	so with the stress_force_killed_bogo() call from the parent.
 */
static inline void ALWAYS_INLINE OPTIMIZE3 stress_bogo_inc(const stress_args_t *args)
{
	register stress_counter_info_t * const ci = args->ci;

	ci->counter_ready = false;
	stress_asm_mb();
	ci->counter++;
	stress_asm_mb();
	ci->counter_ready = true;
}

/*
 *  stress_bogo_get()
 *	get the stessor bogo ops counter
 */
static inline uint64_t ALWAYS_INLINE OPTIMIZE3 stress_bogo_get(const stress_args_t *args)
{
	register const stress_counter_info_t * const ci = args->ci;

	return ci->counter;
}

/*
 *  stress_bogo_set()
 *	set the stessor bogo ops counter
 *	NOTE: try to only set the counter inside a stressor
 *	and not a child process of a stressor. If one has to set
 *	the counter in a child and the child is force KILL'd then indicate
 *	so with the stress_force_killed_bogo() call from the parent.
 */
static inline void ALWAYS_INLINE OPTIMIZE3 stress_bogo_set(const stress_args_t *args, const uint64_t val)
{
	register stress_counter_info_t * const ci = args->ci;

	ci->counter_ready = false;
	stress_asm_mb();
	ci->counter = val;
	stress_asm_mb();
	ci->counter_ready = true;
}

/*
 *  stress_force_killed_bogo()
 *	note that the process is force killed and counter ready state can
 *	be ignored. Use only if the parent kills the child *and* the child
 *	was used to increment the bogo-op counter.
 */
static inline void ALWAYS_INLINE stress_force_killed_bogo(const stress_args_t *args)
{
	args->ci->force_killed = true;
}

/*
 *  stress_continue()
 *      returns true if we can keep on running a stressor
 */
static inline bool ALWAYS_INLINE OPTIMIZE3 stress_continue(const stress_args_t *args)
{
	if (UNLIKELY(!g_stress_continue_flag))
		return false;
	if (LIKELY(args->max_ops == 0))
		return true;
	return stress_bogo_get(args) < args->max_ops;
}

/*
 *  stress_bogo_add_lock()
 *	add val to the stessor bogo ops counter with lock, return true
 *	if stress_continue is true
 */
static inline void stress_bogo_add_lock(const stress_args_t *args, void *lock, const int64_t val)
{
	/*
	 *  Failure in lock acquire, don't bump counter
	 *  and get racy stress_continue state, that's
	 *  probably the best we can do in this failure mode
	 */
	if (UNLIKELY(stress_lock_acquire(lock) < 0))
		return;
	stress_bogo_add(args, val);
	stress_lock_release(lock);
}

/*
 *  stress_bogo_inc_lock()
 *	increment the stessor bogo ops counter with lock, return true
 *	if stress_continue is true
 */
static inline bool stress_bogo_inc_lock(const stress_args_t *args, void *lock, const bool inc)
{
	bool ret;

	/*
	 *  Failure in lock acquire, don't bump counter
	 *  and get racy stress_continue state, that's
	 *  probably the best we can do in this failure mode
	 */
	if (UNLIKELY(stress_lock_acquire(lock) < 0))
		return stress_continue(args);
	ret = stress_continue(args);
	if (inc && ret)
		stress_bogo_inc(args);
	stress_lock_release(lock);

	return ret;
}

/*
 *  stressor option value handling
 */
extern int stress_set_setting(const char *name, const stress_type_id_t type_id,
	const void *value);
extern int stress_set_setting_true(const char *name, const char *opt);
extern int stress_set_setting_global(const char *name, const stress_type_id_t type_id,
	const void *value);
extern bool stress_get_setting(const char *name, void *value);
extern void stress_settings_free(void);
extern void stress_settings_show(void);

/*
 *  externs to force gcc to stash computed values and hence
 *  to stop the optimiser optimising code away to zero. The
 *  *_put funcs are essentially no-op functions.
 */
extern WARN_UNUSED uint64_t stress_get_uint64_zero(void);

/* Filenames and directories */
extern int stress_temp_filename(char *path, const size_t len,
	const char *name, const pid_t pid, const uint32_t instance,
	const uint64_t magic);
extern int stress_temp_filename_args(const stress_args_t *args, char *path,
	const size_t len, const uint64_t magic);
extern int stress_temp_dir(char *path, const size_t len,
	const char *name, const pid_t pid, const uint32_t instance);
extern int stress_temp_dir_args(const stress_args_t *args, char *path,
	const size_t len);
extern WARN_UNUSED int stress_temp_dir_mk(const char *name, const pid_t pid,
	const uint32_t instance);
extern WARN_UNUSED int stress_temp_dir_mk_args(const stress_args_t *args);
extern int stress_temp_dir_rm(const char *name, const pid_t pid,
	const uint32_t instance);
extern int stress_temp_dir_rm_args(const stress_args_t *args);
extern void stress_cwd_readwriteable(void);

extern const char *stress_get_signal_name(const int signum);
extern const char *stress_strsignal(const int signum);
extern WARN_UNUSED int stress_sigchld_set_handler(const stress_args_t *args);

/* Fast random numbers */
extern uint8_t stress_mwc1(void);
extern uint8_t stress_mwc8(void);
extern uint16_t stress_mwc16(void);
extern uint32_t stress_mwc32(void);
extern uint64_t stress_mwc64(void);

/* Fast random numbers 1..max inclusive  */
extern uint8_t stress_mwc8modn(const uint8_t max);
extern uint16_t stress_mwc16modn(const uint16_t max);
extern uint32_t stress_mwc32modn(const uint32_t max);
extern uint64_t stress_mwc64modn(const uint64_t max);

/* Fast random numbers 1..max inclusive, where max maybe power of 2  */
extern uint8_t stress_mwc8modn_maybe_pwr2(const uint8_t max);
extern uint16_t stress_mwc16modn_maybe_pwr2(const uint16_t max);
extern uint32_t stress_mwc32modn_maybe_pwr2(const uint32_t max);
extern uint64_t stress_mwc64modn_maybe_pwr2(const uint64_t max);
extern void stress_mwc_seed(void);
extern void stress_mwc_set_seed(const uint32_t w, const uint32_t z);
extern void stress_mwc_get_seed(uint32_t *w, uint32_t *z);
extern void stress_mwc_reseed(void);

/* Time handling */
extern WARN_UNUSED double stress_timeval_to_double(const struct timeval *tv);
extern WARN_UNUSED double stress_timespec_to_double(const struct timespec *ts);
extern WARN_UNUSED double stress_time_now(void);
extern const char *stress_duration_to_str(const double duration, const bool int_secs);

typedef int stress_oomable_child_func_t(const stress_args_t *args, void *context);

/* 64 and 32 char ASCII patterns */
extern const char ALIGN64 stress_ascii64[64];
extern const char ALIGN64 stress_ascii32[32];

/* Misc helpers */
extern size_t stress_mk_filename(char *fullname, const size_t fullname_len,
	const char *pathname, const char *filename);
extern void stress_set_oom_adjustment(const stress_args_t *args, const bool killable);
extern WARN_UNUSED bool stress_process_oomed(const pid_t pid);
extern WARN_UNUSED int stress_oomable_child(const stress_args_t *args,
	void *context, stress_oomable_child_func_t func, const int flag);
extern WARN_UNUSED int stress_set_sched(const pid_t pid, const int sched,
	const int sched_priority, const bool quiet);
extern WARN_UNUSED int stress_set_deadline_sched(const pid_t, const uint64_t period,
	const uint64_t runtime, const uint64_t deadline, const bool quiet);
extern int sched_settings_apply(const bool quiet);
extern const char *stress_get_sched_name(const int sched);
extern void stress_set_iopriority(const int32_t class, const int32_t level);
extern void stress_set_proc_name_init(int argc, char *argv[], char *envp[]);
extern void stress_set_proc_name(const char *name);
extern void stress_set_proc_state_str(const char *name, const char *str);
extern void stress_set_proc_state(const char *name, const int state);
extern WARN_UNUSED int stress_get_unused_uid(uid_t *uid);
extern void NORETURN MLOCKED_TEXT stress_sig_handler_exit(int signum);
extern void stress_clear_warn_once(void);
extern WARN_UNUSED size_t stress_flag_permutation(const int flags, int **permutations);
extern WARN_UNUSED const char *stress_fs_magic_to_name(const unsigned long fs_magic);
extern WARN_UNUSED const char *stress_get_fs_type(const char *filename);
extern void stress_close_fds(int *fds, const size_t n);
extern void stress_file_rw_hint_short(const int fd);
extern void stress_set_vma_anon_name(const void *addr, const size_t size, const char *name);
extern void stress_clean_dir(const char *name, const pid_t pid, const uint32_t instance);

/* Memory locking */
extern int stress_mlock_region(const void *addr_start, const void *addr_end);

/* Argument parsing and range checking */
extern WARN_UNUSED uint64_t stress_get_uint64(const char *const str);
extern WARN_UNUSED uint64_t stress_get_uint64_scale(const char *const str,
	const stress_scale_t scales[], const char *const msg);
extern WARN_UNUSED uint64_t stress_get_uint64_percent(const char *const str,
	const uint32_t instances, const uint64_t max, const char *const errmsg);
extern WARN_UNUSED uint64_t stress_get_uint64_byte(const char *const str);
extern WARN_UNUSED uint64_t stress_get_uint64_byte_memory(
	const char *const str, const uint32_t instances);
extern WARN_UNUSED uint64_t stress_get_uint64_byte_filesystem(
	const char *const str, const uint32_t instances);
extern WARN_UNUSED uint64_t stress_get_uint64_time(const char *const str);
extern void stress_check_max_stressors(const char *const msg, const int val);
extern void stress_check_range(const char *const opt, const uint64_t val,
	const uint64_t lo, const uint64_t hi);
extern void stress_check_range_bytes(const char *const opt,
	const uint64_t val, const uint64_t lo, const uint64_t hi);
extern WARN_UNUSED int stress_set_mbind(const char *arg);
extern int stress_numa_count_mem_nodes(unsigned long *max_node);
extern WARN_UNUSED uint32_t stress_get_uint32(const char *const str);
extern WARN_UNUSED int32_t  stress_get_int32(const char *const str);
extern WARN_UNUSED int32_t  stress_get_opt_sched(const char *const str);
extern WARN_UNUSED int32_t  stress_get_opt_ionice_class(const char *const str);
extern void stress_check_power_of_2(const char *const opt, const uint64_t val,
	const uint64_t lo, const uint64_t hi);

/* Misc helper funcs */
extern WARN_UNUSED size_t stress_get_sig_stack_size(void);
extern WARN_UNUSED size_t stress_get_min_sig_stack_size(void);
extern WARN_UNUSED size_t stress_get_min_pthread_stack_size(void);

#define STRESS_SIGSTKSZ		(stress_get_sig_stack_size())
#define STRESS_MINSIGSTKSZ	(stress_get_min_sig_stack_size())

extern void stress_shared_unmap(void);
extern void stress_log_system_mem_info(void);
extern size_t stress_munge_underscore(char *dst, const char *src, size_t len);
extern WARN_UNUSED int stress_strcmp_munged(const char *s1, const char *s2);
extern size_t stress_get_page_size(void);
extern WARN_UNUSED int32_t stress_get_processors_online(void);
extern WARN_UNUSED int32_t stress_get_processors_configured(void);
extern WARN_UNUSED int32_t stress_get_ticks_per_second(void);
extern WARN_UNUSED ssize_t stress_get_stack_direction(void);
extern WARN_UNUSED void *stress_get_stack_top(void *start, size_t size);
extern void stress_get_memlimits(size_t *shmall, size_t *freemem,
	size_t *totalmem, size_t *freeswap, size_t *totalswap);
extern WARN_UNUSED int stress_get_load_avg(double *min1, double *min5,
	double *min15);
extern void stress_set_max_limits(void);
extern void stress_parent_died_alarm(void);
extern int stress_process_dumpable(const bool dumpable);
extern int stress_set_timer_slack_ns(const char *opt);
extern void stress_set_timer_slack(void);
extern WARN_UNUSED int stress_set_temp_path(const char *path);
extern WARN_UNUSED const char *stress_get_temp_path(void);
extern WARN_UNUSED int stress_check_temp_path(void);
extern void stress_temp_path_free(void);
extern void stress_rndstr(char *str, size_t len);
extern void stress_rndbuf(void *str, const size_t len);
extern void stress_uint8rnd4(uint8_t *data, const size_t len);
extern WARN_UNUSED unsigned int stress_get_cpu(void);
extern WARN_UNUSED const char *stress_get_compiler(void);
extern WARN_UNUSED const char *stress_get_uname_info(void);
extern WARN_UNUSED int stress_cache_alloc(const char *name);
extern void stress_cache_free(void);
extern void stress_klog_start(void);
extern void stress_klog_stop(bool *success);
extern void stress_ignite_cpu_start(void);
extern void stress_ignite_cpu_stop(void);
extern WARN_UNUSED int stress_set_nonblock(const int fd);
extern WARN_UNUSED ssize_t stress_system_read(const char *path, char *buf,
	const size_t buf_len);
extern ssize_t stress_system_write(const char *path, const char *buf,
	const size_t buf_len);
extern WARN_UNUSED bool stress_is_prime64(const uint64_t n);
extern WARN_UNUSED uint64_t stress_get_next_prime64(const uint64_t n);
extern WARN_UNUSED uint64_t stress_get_prime64(const uint64_t n);
extern WARN_UNUSED size_t stress_get_file_limit(void);
extern WARN_UNUSED size_t stress_get_max_file_limit(void);
extern WARN_UNUSED int stress_get_bad_fd(void);
extern void stress_vmstat_start(void);
extern void stress_vmstat_stop(void);
extern WARN_UNUSED char *stress_find_mount_dev(const char *name);
extern WARN_UNUSED int stress_sigaltstack_no_check(void *stack, const size_t size);
extern WARN_UNUSED int stress_sigaltstack(void *stack, const size_t size);
extern void stress_sigaltstack_disable(void);
extern WARN_UNUSED int stress_sighandler(const char *name, const int signum,
	void (*handler)(int), struct sigaction *orig_action);
extern int stress_sighandler_default(const int signum);
extern void stress_handle_stop_stressing(const int signum);
extern WARN_UNUSED int stress_sig_stop_stressing(const char *name,
	const int sig);
extern int stress_sigrestore(const char *name, const int signum,
	struct sigaction *orig_action);
extern WARN_UNUSED int stress_unimplemented(const stress_args_t *args);
extern WARN_UNUSED size_t stress_probe_max_pipe_size(void);
extern WARN_UNUSED void *stress_align_address(const void *addr,
	const size_t alignment);
extern void stress_mmap_set(uint8_t *buf, const size_t sz,
	const size_t page_size);
extern WARN_UNUSED int stress_mmap_check(uint8_t *buf, const size_t sz,
	const size_t page_size);
extern WARN_UNUSED uint64_t stress_get_phys_mem_size(void);
extern WARN_UNUSED uint64_t stress_get_filesystem_size(void);
extern WARN_UNUSED ssize_t stress_read_buffer(const int fd, void* buffer,
	const ssize_t size, const bool ignore_sig_eintr);
extern WARN_UNUSED ssize_t stress_write_buffer(const int fd, const void* buffer,
	const ssize_t size, const bool ignore_sig_eintr);
extern WARN_UNUSED uint64_t stress_get_filesystem_available_inodes(void);
extern WARN_UNUSED int stress_kernel_release(const int major,
	const int minor, const int patchlevel);
extern WARN_UNUSED int stress_get_kernel_release(void);
extern char *stress_uint64_to_str(char *str, size_t len, const uint64_t val);
extern WARN_UNUSED int stress_drop_capabilities(const char *name);
extern WARN_UNUSED bool stress_is_dot_filename(const char *name);
extern WARN_UNUSED char *stress_const_optdup(const char *opt);
extern size_t stress_exec_text_addr(char **start, char **end);
extern WARN_UNUSED bool stress_check_capability(const int capability);
extern WARN_UNUSED bool stress_sigalrm_pending(void);
extern WARN_UNUSED bool stress_is_dev_tty(const int fd);
extern WARN_UNUSED bool stress_little_endian(void);
extern WARN_UNUSED char *stress_get_proc_self_exe(char *path, const size_t path_len);
extern WARN_UNUSED void *stress_shared_heap_init(void);
extern void stress_shared_heap_deinit(void);
extern WARN_UNUSED void *stress_shared_heap_malloc(const size_t size);
extern WARN_UNUSED char *stress_shared_heap_dup_const(const char *str);
#if defined(__FreeBSD__) ||	\
    defined(__NetBSD__) ||	\
    defined(__APPLE__)
extern WARN_UNUSED int stress_bsd_getsysctl(const char *name, void *ptr, size_t size);
extern WARN_UNUSED uint64_t stress_bsd_getsysctl_uint64(const char *name);
extern WARN_UNUSED uint32_t stress_bsd_getsysctl_uint32(const char *name);
extern WARN_UNUSED unsigned int stress_bsd_getsysctl_uint(const char *name);
extern WARN_UNUSED int stress_bsd_getsysctl_int(const char *name);
#endif

extern WARN_UNUSED int stress_try_open(const stress_args_t *args,
	const char *path, const int flags, const unsigned long timeout_ns);
extern WARN_UNUSED int stress_open_timeout(const char *name,
        const char *path, const int flags, const unsigned long timeout_ns);
extern void stress_dirent_list_free(struct dirent **dlist, const int n);
extern WARN_UNUSED int stress_dirent_list_prune(struct dirent **dlist, const int n);
extern WARN_UNUSED uint16_t stress_ipv4_checksum(uint16_t *ptr, const size_t n);
extern int stress_read_fdinfo(const pid_t pid, const int fd);
extern WARN_UNUSED pid_t stress_get_unused_pid_racy(const bool fork_test);
extern WARN_UNUSED size_t stress_get_hostname_length(void);
extern WARN_UNUSED int32_t stress_set_status(const char *const str);
extern WARN_UNUSED int32_t stress_set_vmstat(const char *const str);
extern WARN_UNUSED int32_t stress_set_thermalstat(const char *const str);
extern WARN_UNUSED int32_t stress_set_iostat(const char *const str);
extern void stress_interrupts_start(stress_interrupts_t *counters);
extern void stress_interrupts_stop(stress_interrupts_t *counters);
extern void stress_interrupts_check_failure(const char *name,
	stress_interrupts_t *counters, uint32_t instance, int *rc);
extern void stress_interrupts_dump(FILE *yaml, stress_stressor_t *stressors_list);

extern void stress_metrics_set_const_check(const stress_args_t *args,
	const size_t idx, char *description, const bool const_description, const double value);
#if defined(HAVE_BUILTIN_CONSTANT_P)
#define stress_metrics_set(args, idx, description, value)	\
	stress_metrics_set_const_check(args, idx, description, __builtin_constant_p(description), value)
#else
#define stress_metrics_set(args, idx, description, value)	\
	stress_metrics_set_const_check(args, idx, description, false, value)
#endif
extern WARN_UNUSED int stress_get_tty_width(void);
extern WARN_UNUSED size_t stress_get_extents(const int fd);
extern WARN_UNUSED bool stress_redo_fork(const stress_args_t *args, const int err);
extern void stress_sighandler_nop(int sig);
extern int stress_killpid(const pid_t pid);
extern WARN_UNUSED bool stress_low_memory(const size_t requested);
extern void stress_ksm_memory_merge(const int flag);
extern int stress_kill_and_wait(const stress_args_t *args,
	const pid_t pid, const int signum, const bool set_stress_force_killed_bogo);
extern int stress_kill_and_wait_many(const stress_args_t *args,
	const pid_t *pids, const size_t n_pids, const int signum,
	const bool set_stress_force_killed_bogo);
extern WARN_UNUSED int stress_x86_smi_readmsr64(const int cpu, const uint32_t reg, uint64_t *val);
extern void stress_unset_chattr_flags(const char *pathname);
extern int stress_munmap_retry_enomem(void *addr, size_t length);
extern int stress_swapoff(const char *path);

/* process information */
extern void stress_dump_processes(void);

/* kernel module helpers */
extern int stress_module_load(const char *name, const char *alias,
	const char *options, bool *already_loaded);
extern int stress_module_unload(const char *name, const char *alias,
	const bool already_loaded);

extern WARN_UNUSED int stress_exit_status(const int err);

/*
 *  Stack aligning for clone() system calls
 *	align to nearest 16 bytes for aarch64 et al,
 *	assumes we have enough slop to do this
 */
static inline WARN_UNUSED ALWAYS_INLINE void *stress_align_stack(void *stack_top)
{
	return (void *)((uintptr_t)stack_top & ~(uintptr_t)0xf);
}

/*
 *  stress_warn_once hashes the current filename and line where
 *  the macro is used and returns true if it's never been called
 *  there before across all threads and child processes
 */
extern WARN_UNUSED bool stress_warn_once_hash(const char *filename, const int line);
#define stress_warn_once()	stress_warn_once_hash(__FILE__, __LINE__)

/* Jobfile parsing */
extern WARN_UNUSED int stress_parse_jobfile(int argc, char **argv, const char *jobfile);
extern WARN_UNUSED int stress_parse_opts(int argc, char **argv, const bool jobmode);

/* Memory tweaking */
extern int stress_madvise_random(void *addr, const size_t length);
extern void stress_madvise_pid_all_pages(const pid_t pid, const int advise);
extern int stress_mincore_touch_pages(void *buf, const size_t buf_len);
extern int stress_mincore_touch_pages_interruptible(void *buf, const size_t buf_len);
extern int stress_pagein_self(const char *name);

/* Mounts */
extern void stress_mount_free(char *mnts[], const int n);
extern WARN_UNUSED int stress_mount_get(char *mnts[], const int max);

/* Used to set options for specific stressors */
extern void stress_adjust_pthread_max(const uint64_t max);
extern void stress_adjust_sleep_max(const uint64_t max);

/* Enable/disable stack smashing error message */
extern void stress_set_stack_smash_check_flag(const bool flag);

/* loff_t and off64_t porting shims */
#if defined(HAVE_LOFF_T)
typedef	loff_t		shim_loff_t;
#elif defined(HAVE_OFF_T)
typedef	off_t		shim_loff_t;
#else
typedef long		shim_loff_t;
#endif

#if defined(HAVE_OFF64_T)
typedef off64_t		shim_off64_t;
#else
typedef uint64_t	shim_off64_t;
#endif

/* clone3 clone args */
struct shim_clone_args {
	uint64_t flags;		/* Flags bit mask */
	uint64_t pidfd;		/* (pid_t *) PID fd */
	uint64_t child_tid;	/* (pid_t *) child TID */
	uint64_t parent_tid;	/* (pid_t *) parent TID */
	uint64_t exit_signal;	/* exit signal */
	uint64_t stack;		/* lowest address of stack */
	uint64_t stack_size;	/* size of stack */
	uint64_t tls;		/* tls address */
};

struct shim_getcpu_cache {
        unsigned long blob[128 / sizeof(long)];
};

/* futex2 waitv shim */
struct shim_futex_waitv {
	uint64_t val;
	uint64_t uaddr;
	uint32_t flags;
	uint32_t reserved;
};

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

#if defined(HAVE_INO64_T)
typedef ino64_t	shim_ino64_t;
#else
typedef int64_t shim_ino64_t;
#endif

/* dirent64 porting shim */
struct shim_linux_dirent64 {
	shim_ino64_t	d_ino;		/* 64-bit inode number */
	shim_off64_t	d_off;		/* 64-bit offset to next structure */
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
	uint32_t sched_util_min;	/* utilization hint, min */
	uint32_t sched_util_max;	/* utilization hint, max */
};

/* shim'd STATX flags */
#if defined(STATX_TYPE)
#define SHIM_STATX_TYPE			(STATX_TYPE)
#else
#define SHIM_STATX_TYPE			(0x00000001U)
#endif
#if defined(STATX_MODE)
#define SHIM_STATX_MODE			(STATX_MODE)
#else
#define SHIM_STATX_MODE			(0x00000002U)
#endif
#if defined(STATX_NLINK)
#define SHIM_STATX_NLINK		(STATX_NLINK)
#else
#define SHIM_STATX_NLINK		(0x00000004U)
#endif
#if defined(STATX_UID)
#define SHIM_STATX_UID			(STATX_UID)
#else
#define SHIM_STATX_UID			(0x00000008U)
#endif
#if defined(STATX_GID)
#define SHIM_STATX_GID			(STATX_GID)
#else
#define SHIM_STATX_GID			(0x00000010U)
#endif
#if defined(STATX_ATIME)
#define SHIM_STATX_ATIME		(STATX_ATIME)
#else
#define SHIM_STATX_ATIME		(0x00000020U)
#endif
#if defined(STATX_MTIME)
#define SHIM_STATX_MTIME		(STATX_MTIME)
#else
#define SHIM_STATX_MTIME		(0x00000040U)
#endif
#if defined(STATX_CTIME)
#define SHIM_STATX_CTIME		(STATX_CTIME)
#else
#define SHIM_STATX_CTIME		(0x00000080U)
#endif
#if defined(STATX_INO)
#define SHIM_STATX_INO			(STATX_INO)
#else
#define SHIM_STATX_INO			(0x00000100U)
#endif
#if defined(STATX_SIZE)
#define SHIM_STATX_SIZE			(STATX_SIZE)
#else
#define SHIM_STATX_SIZE			(0x00000200U)
#endif
#if defined(STATX_BLOCKS)
#define SHIM_STATX_BLOCKS		(STATX_BLOCKS)
#else
#define SHIM_STATX_BLOCKS		(0x00000400U)
#endif
#if defined(STATX_BASIC_STATS)
#define SHIM_STATX_BASIC_STATS		(STATX_BASIC_STATS)
#else
#define SHIM_STATX_BASIC_STATS		(0x000007ffU)
#endif
#if defined(STATX_BTIME)
#define SHIM_STATX_BTIME		(STATX_BTIME)
#else
#define SHIM_STATX_BTIME		(0x00000800U)
#endif
#if defined(STATX_ALL)
#define SHIM_STATX_ALL			(STATX_ALL)
#else
#define SHIM_STATX_ALL			(0x00000fffU)
#endif

#if defined(HAVE_STATX)
typedef struct statx shim_statx_t;
#else
typedef struct {
	char reserved[512];
} shim_statx_t;
#endif

/* old ustat struct */
struct shim_ustat {
#if defined(HAVE_DADDR_T)
	daddr_t	f_tfree;
#else
	long	f_tfree;
#endif
	ino_t	f_tinode;
	char	f_fname[6];
	char	f_fpack[6];
};

/* waitid/pidfd shims */
#if !defined(P_PIDFD)
#define P_PIDFD		(3)
#endif

/* shim'd timex struct */
#if defined(HAVE_SYS_TIMEX_H) &&	\
    defined(HAVE_TIMEX)
typedef struct timex	shim_timex_t;
#else
typedef struct shim_timex {
	int modes;
	/* other fields we don't care about */
	uint8_t padding[256 - sizeof(int)];
} shim_timex_t;
#endif

/*
 *  shim_unconstify_ptr()
 *      some older system calls require non-const void *
 *      or caddr_t args, so we need to unconstify them
 */
static inline void *shim_unconstify_ptr(const void *ptr)
{
	union stress_unconstify {
		const void *cptr;
		void *ptr;
	} su;

	su.cptr = ptr;
	return su.ptr;
}

/* Shim'd kernel system calls */
extern int shim_arch_prctl(int code, unsigned long addr);
extern int shim_brk(void *addr);
extern int shim_cacheflush(char *addr, int nbytes, int cache);
extern void shim_flush_icache(void *begin, void *end);
extern int shim_clock_adjtime(clockid_t clk_id, shim_timex_t *buf);
extern int shim_clock_getres(clockid_t clk_id, struct timespec *res);
extern int shim_clock_gettime(clockid_t clk_id, struct timespec *tp);
extern int shim_clock_settime(clockid_t clk_id, struct timespec *tp);
extern int shim_clone3(struct shim_clone_args *cl_args, size_t size);
extern int shim_close_range(unsigned int fd, unsigned int max_fd, unsigned int flags);
extern ssize_t shim_copy_file_range(int fd_in, shim_loff_t *off_in,
	int fd_out, shim_loff_t *off_out, size_t len, unsigned int flags);
extern int shim_delete_module(const char *name, unsigned int flags);
extern int shim_dup3(int oldfd, int newfd, int flags);
extern int shim_execveat(int dir_fd, const char *pathname, char *const argv[],
	char *const envp[], int flags);
extern void shim_exit_group(int status);
extern int shim_fallocate(int fd, int mode, off_t offset, off_t len);
extern int shim_posix_fallocate(int fd, off_t offset, off_t len);
extern int shim_fdatasync(int fd);
extern ssize_t shim_fgetxattr(int fd, const char *name, void *value, size_t size);
extern int shim_finit_module(int fd, const char *uargs, int flags);
extern ssize_t shim_flistxattr(int fd, char *list, size_t size);
extern int shim_fsconfig(int fd, unsigned int cmd, const char *key,
	const void *value, int aux);
extern int shim_fsetxattr(int fd, const char *name, const void *value,
	size_t size, int flags);
extern int shim_fsmount(int fd, unsigned int flags, unsigned int ms_flags);
extern int shim_fsopen(const char *fsname, unsigned int flags);
extern int shim_fsync(int fd);
extern int shim_futex_wait(const void *futex, const int val,
	const struct timespec *timeout);
extern int shim_futex_wake(const void *futex, const int n);
extern long shim_getcpu(unsigned *cpu, unsigned *node, void *tcache);
extern int shim_getdents(unsigned int fd, struct shim_linux_dirent *dirp,
	unsigned int count);
extern int shim_getdents64(unsigned int fd, struct shim_linux_dirent64 *dirp,
	unsigned int count);
extern int shim_getdomainname(char *name, size_t len);
extern char *shim_getlogin(void);
extern int shim_get_mempolicy(int *mode, unsigned long *nodemask,
	unsigned long maxnode, void *addr, unsigned long flags);
extern int shim_getrandom(void *buff, size_t buflen, unsigned int flags);
extern int shim_getrusage(int who, struct rusage *usage);
extern int shim_gettid(void);
extern ssize_t shim_getxattr(const char *path, const char *name,
	void *value, size_t size);
extern int shim_ioprio_set(int which, int who, int ioprio);
extern int shim_ioprio_get(int which, int who);
extern long shim_kcmp(pid_t pid1, pid_t pid2, int type, unsigned long idx1,
	unsigned long idx2);
extern int shim_kill(pid_t pid, int sig);
extern int shim_klogctl(int type, char *bufp, int len);
extern ssize_t shim_lgetxattr(const char *path, const char *name, void *value,
	size_t size);
extern ssize_t shim_llistxattr(const char *path, char *list, size_t size);
extern int shim_lsetxattr(const char *path, const char *name,
	const void *value, size_t size, int flags);
extern ssize_t shim_listxattr(const char *path, char *list, size_t size);
extern int shim_lookup_dcookie(uint64_t cookie, char *buffer, size_t len);
extern int shim_lremovexattr(const char *path, const char *name);
extern int shim_madvise(void *addr, size_t length, int advice);
extern long shim_mbind(void *addr, unsigned long len,
	int mode, const unsigned long *nodemask,
	unsigned long maxnode, unsigned flags);
extern int shim_membarrier(int cmd, int flags, int cpu_id);
extern int shim_memfd_create(const char *name, unsigned int flags);
extern int shim_memfd_secret(unsigned long flags);
extern long shim_migrate_pages(int pid, unsigned long maxnode,
	const unsigned long *old_nodes, const unsigned long *new_nodes);
extern int shim_mincore(void *addr, size_t length, unsigned char *vec);
extern int shim_mlock(const void *addr, size_t len);
extern int shim_mlock2(const void *addr, size_t len, int flags);
extern int shim_mlockall(int flags);
extern int shim_move_mount(int from_dfd, const char *from_pathname,
	int to_dfd, const char *to_pathname, unsigned int flags);
extern long shim_move_pages(int pid, unsigned long count,
	void **pages, const int *nodes, int *status, int flags);
extern int shim_msync(void *addr, size_t length, int flags);
extern int shim_munlock(const void *addr, size_t len);
extern int shim_munlockall(void);
extern int shim_modify_ldt(int func, void *ptr, unsigned long bytecount);
extern int shim_nanosleep_uint64(uint64_t nsec);
extern int shim_nice(int inc);
extern time_t shim_time(time_t *tloc);
extern int shim_gettimeofday(struct timeval *tv, struct timezone *tz);
extern int shim_pidfd_getfd(int pidfd, int targetfd, unsigned int flags);
extern int shim_pidfd_open(pid_t pid, unsigned int flags);
extern int shim_pidfd_send_signal(int pidfd, int sig, siginfo_t *info,
	unsigned int flags);
extern int shim_pkey_alloc(unsigned int flags, unsigned int access_rights);
extern int shim_pkey_free(int pkey);
extern int shim_pkey_mprotect(void *addr, size_t len, int prot, int pkey);
extern int shim_pkey_get(int pkey);
extern int shim_pkey_set(int pkey, unsigned int rights);
extern ssize_t shim_process_madvise(int pidfd, const struct iovec *iovec,
	unsigned long vlen, int advice, unsigned int flags);
extern int shim_process_mrelease(int pidfd, unsigned int flags);
extern int shim_quotactl_fd(unsigned int fd, unsigned int cmd, int id, void *addr);
extern ssize_t shim_readlink(const char *pathname, char *buf, size_t bufsiz);
extern int shim_raise(int sig);
extern int shim_reboot(int magic, int magic2, int cmd, void *arg);
extern int shim_removexattr(const char *path, const char *name);
extern int shim_rmdir(const char *pathname);
extern int shim_force_rmdir(const char *pathname);
extern void *shim_sbrk(intptr_t increment);
extern int shim_sched_getattr(pid_t pid, struct shim_sched_attr *attr,
	unsigned int size, unsigned int flags);
extern int shim_sched_setattr(pid_t pid, struct shim_sched_attr *attr,
	unsigned int flags);
extern int shim_setdomainname(const char *name, size_t len);
extern long shim_sgetmask(void);
extern long shim_ssetmask(long newmask);
extern int shim_stime(const time_t *t);
extern int shim_sched_yield(void);
extern int shim_seccomp(unsigned int operation, unsigned int flags, void *args);
extern int shim_set_mempolicy(int mode, unsigned long *nodemask,
	unsigned long maxnode);
extern int shim_set_mempolicy_home_node(unsigned long start, unsigned long len,
        unsigned long home_node, unsigned long flags);
extern int shim_setgroups(int size, const gid_t *list);
extern int shim_setxattr(const char *path, const char *name, const void *value,
	size_t size, int flags);
extern int shim_statx(int dfd, const char *filename, int flags,
	unsigned int mask, shim_statx_t *buffer);
extern size_t shim_strlcat(char *dst, const char *src, size_t len);
extern size_t shim_strlcpy(char *dst, const char *src, size_t len);
extern int shim_sync_file_range(int fd, shim_off64_t offset,
	shim_off64_t nbytes, unsigned int flags);
extern int shim_sysfs(int option, ...);
extern int shim_tgkill(int tgid, int tid, int sig);
extern int shim_tkill(int tid, int sig);
extern int shim_fremovexattr(int fd, const char *name);
extern int shim_unlink(const char *pathname);
extern int shim_force_unlink(const char *pathname);
extern int shim_unlinkat(int dirfd, const char *pathname, int flags);
extern int shim_unshare(int flags);
extern int shim_userfaultfd(int flags);
extern int shim_usleep(uint64_t usec);
extern int shim_usleep_interruptible(uint64_t usec);
extern int shim_ustat(dev_t dev, struct shim_ustat *ubuf);
extern int shim_vhangup(void);
extern pid_t shim_waitpid(pid_t pid, int *wstatus, int options);
extern pid_t shim_wait(int *wstatus);
extern pid_t shim_wait3(int *wstatus, int options, struct rusage *rusage);
extern pid_t shim_wait4(pid_t pid, int *wstatus, int options, struct rusage *rusage);
extern int shim_futex_waitv(struct shim_futex_waitv *waiters, unsigned int nr_futexes,
	unsigned int flags, struct timespec *timeout, clockid_t clockid);
#endif

#if !defined(STRESS_CORE_SHIM) &&	\
    !defined(HAVE_PEDANTIC) &&		\
    (defined(HAVE_COMPILER_GCC) && defined(HAVE_COMPILER_CLANG))
int unlink(const char *pathname) __attribute__((deprecated("use shim_unlink")));
int unlinkat(int dirfd, const char *pathname, int flags) __attribute__((deprecated("use shim_unlinkat")));
int rmdir(const char *pathname) __attribute__((deprecated("use shim_rmdir")));
#endif
