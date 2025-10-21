/*
 * Copyright (C) 2013-2021 Canonical, Ltd.
 * Copyright (C) 2021-2025 Colin Ian King
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

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#ifndef _LARGEFILE_SOURCE
#define _LARGEFILE_SOURCE	(1)
#endif

#ifdef _FILE_OFFSET_BITS
#undef _FILE_OFFSET_BITS
#endif
#define _FILE_OFFSET_BITS 	(64)

#if defined(__TINYC__) ||	\
    defined(__PCC__)
#undef _FORTIFY_SOURCE
#endif

#if defined(HAVE_FEATURES_H)
#include <features.h>
#endif

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
#elif defined(__clang__) && 	\
   (defined(__INTEL_CLANG_COMPILER) || defined(__INTEL_LLVM_COMPILER))
/* Intel ICX compiler */
#define HAVE_COMPILER_ICX
#elif defined(__clang__)
/* clang */
#define HAVE_COMPILER_CLANG
#elif defined(__GNUC__) && 		\
      defined(HAVE_CC_MUSL_GCC)
/* musl gcc */
#define HAVE_COMPILER_MUSL
#define HAVE_COMPILER_GCC_OR_MUSL
#elif defined(__GNUC__)
/* GNU C compiler */
#define HAVE_COMPILER_GCC
#define HAVE_COMPILER_GCC_OR_MUSL
#endif

#ifndef _ATFILE_SOURCE
#define _ATFILE_SOURCE
#endif
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

/* Some Solaris tool chains only define __sun */
#if defined(__sun) &&	\
    !defined(__sun__)
#define __sun__
#endif
/* Enable re-entrant code for threading, e.g. per thread errno */
#if defined(__sun__)
#define _REENTRANT
#endif

/* Stop pcc complaining about attribute COLD not being available */
#if defined(__PCC__)
#undef __COLD
#define __COLD
#endif

/*
 *  Standard includes, assume we have this as the
 *  minimal standard baseline
 */
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <limits.h>
#include <setjmp.h>
#include <signal.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#if defined(HAVE_STRINGS_H)
#include <strings.h>
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
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/wait.h>
#if defined(HAVE_SYSCALL_H)
#include <sys/syscall.h>
#endif
#if defined(HAVE_SYS_SYSINFO_H)
#include <sys/sysinfo.h>
#if defined(HAVE_COMPILER_GCC_OR_MUSL) &&	\
    !defined(__GLIBC__)
/* Suppress kernel sysinfo to avoid collision with musl */
#define _LINUX_SYSINFO_H
#endif
#endif
#if defined(HAVE_LINUX_POSIX_TYPES_H)
#include <linux/posix_types.h>
#endif

#define STRESS_STRESSOR_STATUS_PASSED		(0)
#define STRESS_STRESSOR_STATUS_FAILED		(1)
#define STRESS_STRESSOR_STATUS_SKIPPED		(2)
#define STRESS_STRESSOR_STATUS_BAD_METRICS	(3)
#define STRESS_STRESSOR_STATUS_MAX		(4)

#define STRESS_MAX_PERMUTATIONS			(20)

/* Some Solaris systems don't have NAME_MAX */
#if !defined(NAME_MAX)
#if defined(MAXNAMELEN)
#define NAME_MAX	(MAXNAMELEN - 1)
#else
#define NAME_MAX	(255)
#endif
#endif

/*
 *  Per stressor misc rate metrics
 */
#define STRESS_MISC_METRICS_MAX			(96)

/* Process tracking info via pid */
typedef struct stress_pid {
	struct stress_pid *next;	/* next stress_pid in list, or NULL */
	pid_t pid;			/* PID of process */
	pid_t oomable_child;		/* oomable child pid, zero = none */
	volatile uint8_t state;		/* sync start state */
	bool reaped;			/* successfully waited for */
} stress_pid_t;

/* Bogo-op counter state */
typedef struct {
	uint64_t counter;		/* bogo-op counter */
	bool counter_ready;		/* ready flag */
	bool run_ok;			/* stressor run w/o issues */
	bool force_killed;		/* true if sent SIGKILL */
	bool padding;			/* padding */
} stress_counter_info_t;

/* Shared mmap'd pages */
typedef struct {
	void *page_none;		/* mmap'd PROT_NONE page */
	void *page_ro;			/* mmap'd PROT_RO page */
	void *page_wo;			/* mmap'd PROT_WO page */
} stress_mapped_t;

/* Individual metric */
typedef struct {
	char *description;		/* description of metric */
	double value;			/* value of metric */
	int mean_type;			/* type of metric, geometric or harmonic mean */
} stress_metrics_item_t;

/* Per stressor metrics */
typedef struct {
	size_t max_metrics;
	stress_metrics_item_t items[STRESS_MISC_METRICS_MAX];
} stress_metrics_data_t;

/* Run-time stressor args */
typedef struct {
	struct {
		uint64_t max_ops;		/* max number of bogo ops */
		stress_counter_info_t ci;	/* counter info struct */
		bool possibly_oom_killed;	/* was oom killed? */
	} bogo;
	const char *name;		/* stressor name */
	uint32_t instance;		/* stressor instance # */
	uint32_t instances;		/* number of instances */
	pid_t pid;			/* stress pid info */
	size_t page_size;		/* page size */
	double time_end;		/* when to end */
	stress_mapped_t *mapped;	/* mmap'd pages, addr of g_shared mapped */
	stress_metrics_data_t *metrics;	/* misc per stressor metrics */
	struct stress_stats *stats; 	/* stressor stats */
	const struct stressor_info *info; /* stressor info */
} stress_args_t;

/* Run-time stressor descriptor list */
typedef struct stress_stressor_info {
	struct stress_stressor_info *next; /* next proc info struct in list */
	struct stress_stressor_info *prev; /* prev proc info struct in list */
	const struct stress *stressor;	/* stressor */
	struct stress_stats **stats;	/* stressor stats info */
	int32_t completed_instances;	/* count of completed instances */
	int32_t instances;		/* number of instances per stressor */
	uint64_t bogo_max_ops;		/* max number of bogo ops, 0 = disabled */
	uint32_t status[STRESS_STRESSOR_STATUS_MAX];
					/* number of instances that passed/failed/skipped */
	struct {
		uint8_t run;		/* ignore running the stressor, unsupported or excluded */
		bool	permute;	/* ignore flag, saved for permute */
	} ignore;
} stress_stressor_t;

#include "core-version.h"
#include "core-attribute.h"
#include "core-asm-generic.h"
#include "core-opts.h"
#include "core-parse-opts.h"
#include "core-perf.h"
#include "core-setting.h"
#include "core-signal.h"
#include "core-stack.h"
#include "core-log.h"
#include "core-lock.h"
#include "core-memory.h"
#include "core-mwc.h"
#include "core-rapl.h"
#include "core-sched.h"
#include "core-sync.h"
#include "core-shim.h"
#include "core-time.h"
#include "core-thermal-zone.h"

/* Per stressor information */

#if defined(CHECK_UNEXPECTED) && 	\
    defined(HAVE_PRAGMA) &&		\
    defined(HAVE_COMPILER_GCC_OR_MUSL)
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

/* Voidification of returns */
#define VOID_RET(type, x)	\
do {				\
	type void_ret = x;	\
				\
	(void)void_ret;		\
} while (0)			\

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
#define STRESS_STATE_SYNC_WAIT		(3)
#define STRESS_STATE_DEINIT		(4)
#define STRESS_STATE_STOP		(5)
#define STRESS_STATE_EXIT		(6)
#define STRESS_STATE_WAIT		(7)
#define STRESS_STATE_ZOMBIE		(8)

#define STRESS_INTERRUPTS_MAX		(8)	/* see core_interrupts.c */
#define STRESS_CSTATES_MAX		(16)

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

#define STRESS_MINIMUM(a, b) (((a) < (b)) ? (a) : (b))
#define STRESS_MAXIMUM(a, b) (((a) > (b)) ? (a) : (b))

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

#define STRESS_PROCS_MAX	(8192)		/* Max number of processes per stressor */

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
#define CLASS_SIGNAL		STRESS_BIT_UL(15)	/* software signals */
#define CLASS_SEARCH		STRESS_BIT_UL(16)	/* Search algorithms */
#define CLASS_COMPUTE		STRESS_BIT_UL(17)	/* CPU computations */
#define CLASS_FP		STRESS_BIT_UL(18)	/* Floating point operations */
#define CLASS_INTEGER		STRESS_BIT_UL(19)	/* Integer operations */
#define CLASS_SORT		STRESS_BIT_UL(20)	/* Sort stressors */
#define CLASS_IPC		STRESS_BIT_UL(21)	/* Inter process communication */
#define CLASS_VECTOR		STRESS_BIT_UL(22)	/* Vector math operations */

/* Help information for options */
typedef struct {
	const char *opt_s;		/* short option */
	const char *opt_l;		/* long option */
	const char *description;	/* description */
} stress_help_t;

/*
 *  Per ELISA request, we have a duplicated counter
 *  and run_ok flag in a different shared memory region
 *  so we can sanity check these just in case the stats
 *  have got corrupted.
 */
typedef struct {
	struct {
		stress_counter_info_t ci; /* Copy of stats counter info ci */
		uint8_t	pad[32 - sizeof(stress_counter_info_t)]; /* Padding */
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
	void *lock;			/* optional lock */
	double	duration;		/* time per op */
	double	count;			/* number of ops */
	volatile double	t_start;	/* optional start time */
} stress_metrics_t;

typedef enum {
	VERIFY_NONE	= 0x00,		/* no verification */
	VERIFY_OPTIONAL = 0x01,		/* --verify can enable verification */
	VERIFY_ALWAYS   = 0x02,		/* verification always enabled */
} stress_verify_t;

/* Per stressor information, as defined in every stressor source */
typedef struct stressor_info {
	int (*stressor)(stress_args_t *args);	/* stressor function */
	int (*supported)(const char *name);	/* return 0 = supported, -1, not */
	void (*init)(const uint32_t instances); /* stressor init, NULL = ignore */
	void (*deinit)(void);		/* stressor de-init, NULL = ignore */
	void (*set_default)(void);	/* default set-up */
	void (*set_limit)(uint64_t max);/* set limits */
	const stress_opt_t *opts;	/* new option settings */
	const stress_help_t *help;	/* stressor help options */
	const stress_class_t classifier;/* stressor class */
	const stress_verify_t verify;	/* verification mode */
	const char *unimplemented_reason;	/* unsupported reason message */
} stressor_info_t;

/* gcc 4.7 and later support vector ops */
#if defined(HAVE_COMPILER_GCC_OR_MUSL) &&	\
    NEED_GNUC(4, 7, 0)
#define STRESS_VECTOR	(1)
#endif

/* restrict keyword */
#if defined(HAVE___RESTRICT)
#define RESTRICT __restrict
#else
#define RESTRICT
#endif

/* optimisation on branching */
#if defined(HAVE_BUILTIN_EXPECT) && \
    !defined(COVERITY)
#define LIKELY(x)	__builtin_expect((x), 1)
#define UNLIKELY(x)	__builtin_expect((x), 0)
#else
#define LIKELY(x)	(x)
#define UNLIKELY(x)	(x)
#endif

/* use syscall if we can, fallback to vfork otherwise */
#define shim_vfork()		g_shared->vfork()

extern const char stress_config[];

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
#define NEVER_END_OPS		(0xffffffffffffffffULL)

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
#define TIMEOUT_NOT_SET		(~0ULL)
#define UNDEFINED		(-1)
#define PAGE_MAPPED		(0x01)
#define PAGE_MAPPED_FAIL	(0x02)

#if defined(HAVE_COMPILER_GCC_OR_MUSL) || defined(HAVE_COMPILER_CLANG)
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

/* Per interrupt stat counters */
typedef struct {
	uint64_t count_start;		/* interrupt count at start */
	uint64_t count_stop;		/* interrupt count at end */
} stress_interrupts_t;

/* C-state stats */
typedef struct {
	bool   valid;			/* C state is valid? */
	double time[STRESS_CSTATES_MAX];
	double residency[STRESS_CSTATES_MAX];
} stress_cstate_stats_t;

/* Per stressor statistics and accounting info */
typedef struct stress_stats {
	stress_args_t args;		/* stressor args */
	double start;			/* wall clock start time */
	double duration;		/* finish - start */
	uint64_t counter_total;		/* counter total */
	double duration_total;		/* wall clock duration */
	stress_pid_t s_pid;		/* stressor pid */
	bool sigalarmed;		/* set true if signalled with SIGALRM */
	bool signalled;			/* set true if signalled with a kill */
	bool completed;			/* true if stressor completed */
#if defined(STRESS_PERF_STATS)
	stress_perf_t sp;		/* perf counters */
#endif
#if defined(STRESS_THERMAL_ZONES)
	stress_tz_t tz;			/* thermal zones */
#endif
#if defined(STRESS_RAPL)
	stress_rapl_t rapl;		/* rapl power measurements */
#endif
	stress_checksum_t *checksum;	/* pointer to checksum data */
	stress_interrupts_t interrupts[STRESS_INTERRUPTS_MAX];
	stress_cstate_stats_t cstates;	/* cstate stats */
	stress_metrics_data_t metrics;	/* misc metrics */
	double rusage_utime;		/* rusage user time */
	double rusage_stime;		/* rusage system time */
	double rusage_utime_total;	/* rusage user time */
	double rusage_stime_total;	/* rusage system time */
	long int rusage_maxrss;		/* rusage max RSS, 0 = unused */
} stress_stats_t;

/* Shared heap info */
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
	void *null_ptr;			/* Null pointer */
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
	struct {
		uint32_t hash[STRESS_WARN_HASH_MAX]; /* hash patterns */
		void *lock;		/* protection lock */
	} warn_once;
	union {
#if defined(HAVE_INT128_T)
		__uint128_t val128[4] ALIGN64;
#endif
		uint64_t val64[8] ALIGN64;
		uint32_t val32[16] ALIGN64;
		uint16_t val16[32] ALIGN64;
		uint8_t	 val8[64] ALIGN64;
	} atomic ALIGN64;		/* Shared atomic temp vars */
	struct {
		void *lock;
		int64_t row;
	} fractal;			/* Fractal stressor row state */
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
#if defined(STRESS_RAPL)
	stress_rapl_domain_t *rapl_domains;
					/* RAPL domain information */
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
typedef struct stress {
	const stressor_info_t *info;	/* stress test info */
	const short int short_getopt;	/* getopt short option */
	const stress_op_t op;		/* ops option */
	char name[16];			/* stressor function name */
} stress_t;

/* Pointer to current running stressor proc info */
extern stress_stressor_t *g_stressor_current;

/* Various global option settings and flags */
extern const char g_app_name[];		/* Name of application */
extern stress_shared_t *g_shared;	/* shared memory */
extern uint64_t	g_opt_timeout;		/* timeout in seconds */
extern uint64_t	g_opt_flags;		/* option flags */
extern volatile bool g_stress_continue_flag; /* false to exit stressor */
extern jmp_buf g_error_env;		/* parsing error env */
extern void *g_nowt;			/* void pointer to NULL */

extern void stress_zero_bogo_max_ops(void);

/*
 *  stress_continue_flag()
 *	get stress_continue_flag state
 */
static inline bool ALWAYS_INLINE stress_continue_flag(void)
{
	return g_stress_continue_flag;
}

/*
 *  stress_continue_set_flag()
 *	set stress_continue_flag state
 */
static inline void ALWAYS_INLINE stress_continue_set_flag(const bool setting)
{
	g_stress_continue_flag = setting;
	if (!setting)
		stress_zero_bogo_max_ops();
}

/*
 *  stress_bogo_add()
 *	add inc to the stressor bogo ops counter
 *	NOTE: try to only add to the counter inside a stressor
 *	and not a child process of a stressor. If one has to add to
 *	the counter in a child and the child is force KILL'd then indicate
 *	so with the stress_force_killed_bogo() call from the parent.
 */
static inline void ALWAYS_INLINE stress_bogo_add(stress_args_t *args, const uint64_t inc)
{
	args->bogo.ci.counter_ready = false;
	stress_asm_mb();
	args->bogo.ci.counter += inc;
	stress_asm_mb();
	args->bogo.ci.counter_ready = true;
}

/*
 *  stress_bogo_inc()
 *	increment the stressor bogo ops counter
 *	NOTE: try to only increment the counter inside a stressor
 *	and not a child process of a stressor. If one has to increment
 *	the counter in a child and the child is force KILL'd then indicate
 *	so with the stress_force_killed_bogo() call from the parent.
 */
static inline void ALWAYS_INLINE stress_bogo_inc(stress_args_t *args)
{
	args->bogo.ci.counter_ready = false;
	stress_asm_mb();
	args->bogo.ci.counter++;
	stress_asm_mb();
	args->bogo.ci.counter_ready = true;
}

/*
 *  stress_bogo_get()
 *	get the stressor bogo ops counter
 */
static inline uint64_t ALWAYS_INLINE stress_bogo_get(stress_args_t *args)
{
	return args->bogo.ci.counter;
}

/*
 *  stress_bogo_ready()
 *	set counter ready flag to true
 */
static inline void ALWAYS_INLINE stress_bogo_ready(stress_args_t *args)
{
	args->bogo.ci.counter_ready = true;
}

/*
 *  stress_bogo_set()
 *	set the stressor bogo ops counter
 *	NOTE: try to only set the counter inside a stressor
 *	and not a child process of a stressor. If one has to set
 *	the counter in a child and the child is force KILL'd then indicate
 *	so with the stress_force_killed_bogo() call from the parent.
 */
static inline void ALWAYS_INLINE stress_bogo_set(stress_args_t *args, const uint64_t val)
{
	args->bogo.ci.counter_ready = false;
	stress_asm_mb();
	args->bogo.ci.counter = val;
	stress_asm_mb();
	args->bogo.ci.counter_ready = true;
}

/*
 *  stress_force_killed_bogo()
 *	note that the process is force killed and counter ready state can
 *	be ignored. Use only if the parent kills the child *and* the child
 *	was used to increment the bogo-op counter.
 */
static inline void ALWAYS_INLINE stress_force_killed_bogo(stress_args_t *args)
{
	args->bogo.ci.force_killed = true;
}

/*
 *  stress_continue()
 *      returns true if we can keep on running a stressor
 */
#define stress_continue(args) 	LIKELY(args->bogo.ci.counter < args->bogo.max_ops)

/*
 *  stress_bogo_add_lock()
 *	add val to the stressor bogo ops counter with lock, return true
 *	if stress_continue is true
 */
static inline void stress_bogo_add_lock(stress_args_t *args, void *lock, const int64_t val)
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
 *	increment the stressor bogo ops counter with lock, return true
 *	if stress_continue is true
 */
static inline bool stress_bogo_inc_lock(stress_args_t *args, void *lock, const bool inc)
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
 *  stress_instance_zero()
 *	return true if stressor is the 0th instance of N stressor instances
 */
static inline bool ALWAYS_INLINE stress_instance_zero(stress_args_t *args)
{
	return args->instance == 0;
}

#include "core-helper.h"
#include "core-filesystem.h"

#define STRESS_METRIC_GEOMETRIC_MEAN	(0x1)
#define STRESS_METRIC_HARMONIC_MEAN	(0x2)
#define STRESS_METRIC_TOTAL		(0x3)
#define STRESS_METRIC_MAXIMUM		(0x4)

extern WARN_UNUSED int stress_parse_opts(int argc, char **argv, const bool jobmode);
extern void stress_shared_readonly(void);
extern void stress_shared_unmap(void);
extern void stress_log_system_mem_info(void);
extern void stress_metrics_set_const_check(stress_args_t *args,
	const size_t idx, char *description, const bool const_description, const double value, const int mean_type);
extern WARN_UNUSED ssize_t stress_stressor_find(const char *name);
#if defined(HAVE_BUILTIN_CONSTANT_P)
#define stress_metrics_set(args, idx, description, value, mean_type)	\
	stress_metrics_set_const_check(args, idx, description, __builtin_constant_p(description), value, mean_type)
#else
#define stress_metrics_set(args, idx, description, value, mean_type)	\
	stress_metrics_set_const_check(args, idx, description, false, value, mean_type)
#endif

#if !defined(STRESS_CORE_SHIM) &&	\
    !defined(HAVE_PEDANTIC) &&		\
    (defined(HAVE_COMPILER_GCC_OR_MUSL) && defined(HAVE_COMPILER_CLANG))
int unlink(const char *pathname) __attribute__((deprecated("use shim_unlink")));
int unlinkat(int dirfd, const char *pathname, int flags) __attribute__((deprecated("use shim_unlinkat")));
int rmdir(const char *pathname) __attribute__((deprecated("use shim_rmdir")));
size_t strlcpy(char *dst, const char *src, size_t size) __attribute__((deprecated("shim_strscpy")));
size_t strlcat(char *dst, const char *src, size_t size) __attribute__((deprecated("shim_strlcat")));

#endif

#endif
