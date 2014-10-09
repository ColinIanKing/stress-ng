/*
 * Copyright (C) 2013-2014 Canonical, Ltd.
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
#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdarg.h>
#include <setjmp.h>
#include <string.h>
#include <unistd.h>
#include <inttypes.h>
#include <math.h>
#include <complex.h>
#include <signal.h>
#include <fcntl.h>
#include <getopt.h>
#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <time.h>
#include <semaphore.h>
#include <poll.h>
#include <dirent.h>

#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <sys/file.h>

#if defined (_POSIX_PRIORITY_SCHEDULING) || defined (__linux__)
#include <sched.h>
#endif
#if defined (__linux__)
#include <sys/prctl.h>
#include <sys/syscall.h>
#endif
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

/* GNU HURD */
#ifndef PATH_MAX
#define PATH_MAX 		(4096)
#endif

#define STRESS_FD_MAX		(65536)
#define STRESS_PROCS_MAX	(1024)

#ifndef PIPE_BUF
#define PIPE_BUF		(512)
#endif
#define SOCKET_BUF		(8192)

/* Option bit masks */
#define OPT_FLAGS_NO_CLEAN	0x00000001	/* Don't remove hdd files */
#define OPT_FLAGS_DRY_RUN	0x00000002	/* Don't actually run */
#define OPT_FLAGS_METRICS	0x00000004	/* Dump metrics at end */
#define OPT_FLAGS_VM_KEEP	0x00000008	/* Don't keep re-allocating */
#define OPT_FLAGS_RANDOM	0x00000010	/* Randomize */
#define OPT_FLAGS_SET		0x00000020	/* Set if user specifies stress procs */
#define OPT_FLAGS_KEEP_NAME	0x00000040	/* Keep stress names to stress-ng */
#define OPT_FLAGS_UTIME_FSYNC	0x00000080	/* fsync after utime modification */

/* debug output bitmasks */
#define PR_ERR			0x00010000	/* Print errors */
#define PR_INF			0x00020000	/* Print info */
#define PR_DBG			0x00040000	/* Print debug */
#define PR_ALL			(PR_ERR | PR_INF | PR_DBG)

#define pr_dbg(fp, fmt, args...)	print(fp, PR_DBG, fmt, ## args)
#define pr_inf(fp, fmt, args...)	print(fp, PR_INF, fmt, ## args)
#define pr_err(fp, fmt, args...)	print(fp, PR_ERR, fmt, ## args)

#define pr_failed_err(name, what)	pr_failed(PR_ERR, name, what)
#define pr_failed_dbg(name, what)	pr_failed(PR_DBG, name, what)

#define KB			(1024ULL)
#define	MB			(KB * KB)
#define GB			(KB * KB * KB)

#define PAGE_4K_SHIFT		(12)
#define PAGE_4K			(1 << PAGE_4K_SHIFT)

#define MIN_VM_BYTES		(4 * KB)
#define MAX_VM_BYTES		(1 * GB)
#define DEFAULT_VM_BYTES	(256 * MB)

#define MIN_MMAP_BYTES		(4 * KB)
#define MAX_MMAP_BYTES		(1 * GB)
#define DEFAULT_MMAP_BYTES	(256 * MB)

#define MIN_VM_STRIDE		(1)
#define MAX_VM_STRIDE		(1 * MB)
#define DEFAULT_VM_STRIDE	(4 * KB)

#define MIN_HDD_BYTES		(1 * MB)
#define MAX_HDD_BYTES		(256 * GB)
#define DEFAULT_HDD_BYTES	(1 * GB)

#define MIN_HDD_WRITE_SIZE	(1)
#define MAX_HDD_WRITE_SIZE	(4 * MB)
#define DEFAULT_HDD_WRITE_SIZE	(64 * 1024)

#define MIN_VM_HANG		(0)
#define MAX_VM_HANG		(3600)
#define DEFAULT_VM_HANG		(~0ULL)

#define DEFAULT_TIMEOUT		(60 * 60 * 24)
#define DEFAULT_BACKOFF		(0)
#define DEFAULT_DENTRIES	(2048)
#define DEFAULT_LINKS		(8192)
#define DEFAULT_DIRS		(8192)

#define DEFAULT_OPS_MIN		(100ULL)
#define DEFAULT_OPS_MAX		(100000000ULL)

#define CTXT_STOP		'X'
#define PIPE_STOP		'S'

#define MEM_CHUNK_SIZE		(65536 * 8)
#define UNDEFINED		(-1)

#define PAGE_MAPPED		(0x01)
#define PAGE_MAPPED_FAIL	(0x02)

#define FFT_SIZE		(4096)

/* stress process prototype */
typedef int (*func)(uint64_t *const counter, const uint32_t instance, const uint64_t max_ops, const char *name);

/* Help information for options */
typedef struct {
	const char *opt_s;		/* short option */
	const char *opt_l;		/* long option */
	const char *description;	/* description */
} help_t;

/* Stress tests */
typedef enum {
	STRESS_IOSYNC	= 0,
	STRESS_CPU,
	STRESS_VM,
	STRESS_HDD,
	STRESS_FORK,
	STRESS_CTXT,
	STRESS_PIPE,
	STRESS_CACHE,
	STRESS_SOCKET,
	STRESS_YIELD,
	STRESS_FALLOCATE,
	STRESS_FLOCK,
	STRESS_AFFINITY,
	STRESS_TIMER,
	STRESS_DENTRY,
	STRESS_URANDOM,
	STRESS_SEMAPHORE,
	STRESS_OPEN,
	STRESS_SIGQUEUE,
	STRESS_POLL,
	STRESS_LINK,
	STRESS_SYMLINK,
	STRESS_DIR,
	STRESS_SIGSEGV,
	STRESS_MMAP,
	STRESS_QSORT,
	STRESS_BIGHEAP,
	STRESS_RENAME,
	STRESS_UTIME,
	STRESS_FSTAT,
	/* Add new stress tests here */
	STRESS_MAX
} stress_id;

/* Command line long options */
typedef enum {
	OPT_QUERY = '?',
	OPT_ALL = 'a',
	OPT_BACKOFF = 'b',
	OPT_CPU = 'c',
	OPT_HDD = 'd',
	OPT_FORK = 'f',
	OPT_IOSYNC = 'i',
	OPT_HELP = 'h',
	OPT_KEEP_NAME = 'k',
	OPT_CPU_LOAD = 'l',
	OPT_VM = 'm',
	OPT_DRY_RUN = 'n',
	OPT_RENAME = 'R',
	OPT_OPEN = 'o',
	OPT_PIPE = 'p',
	OPT_QUIET = 'q',
	OPT_RANDOM = 'r',
	OPT_CTXT = 's',
	OPT_TIMEOUT = 't',
#if defined (__linux__)
	OPT_URANDOM = 'u',
#endif
	OPT_VERBOSE = 'v',
	OPT_YIELD = 'y',
	OPT_CACHE = 'C',
	OPT_DENTRY = 'D',
	OPT_FALLOCATE = 'F',
	OPT_METRICS = 'M',
	OPT_POLL = 'P',
	OPT_SOCKET = 'S',
#if defined (__linux__)
	OPT_TIMER = 'T',
#endif
	OPT_VERSION = 'V',
	OPT_BIGHEAP = 'B',
	OPT_VM_BYTES = 0x80,
	OPT_VM_STRIDE,
	OPT_VM_HANG,
	OPT_VM_KEEP,
#ifdef MAP_POPULATE
	OPT_VM_MMAP_POPULATE,
#endif
#ifdef MAP_LOCKED
	OPT_VM_MMAP_LOCKED,
#endif
	OPT_HDD_BYTES,
	OPT_HDD_NOCLEAN,
	OPT_HDD_WRITE_SIZE,
	OPT_CPU_OPS,
	OPT_CPU_METHOD,
	OPT_IOSYNC_OPS,
	OPT_VM_OPS,
	OPT_HDD_OPS,
	OPT_FORK_OPS,
	OPT_CTXT_OPS,
	OPT_PIPE_OPS,
	OPT_CACHE_OPS,
	OPT_SOCKET_OPS,
	OPT_SOCKET_PORT,
#if defined (__linux__)
	OPT_SCHED,
	OPT_SCHED_PRIO,
	OPT_IONICE_CLASS,
	OPT_IONICE_LEVEL,
	OPT_AFFINITY,
	OPT_AFFINITY_OPS,
	OPT_TIMER_OPS,
	OPT_TIMER_FREQ,
	OPT_URANDOM_OPS,
#endif
#if _POSIX_C_SOURCE >= 199309L
	OPT_SIGQUEUE,
	OPT_SIGQUEUE_OPS,
#endif
#if defined (_POSIX_PRIORITY_SCHEDULING)
	OPT_YIELD_OPS,
#endif
#if _XOPEN_SOURCE >= 600 || _POSIX_C_SOURCE >= 200112L
	OPT_FALLOCATE_OPS,
#endif
	OPT_FLOCK,
	OPT_FLOCK_OPS,
	OPT_DENTRY_OPS,
	OPT_DENTRIES,
	OPT_SEMAPHORE,
	OPT_SEMAPHORE_OPS,
	OPT_OPEN_OPS,
	OPT_POLL_OPS,
	OPT_LINK,
	OPT_LINK_OPS,
	OPT_SYMLINK,
	OPT_SYMLINK_OPS,
	OPT_DIR,
	OPT_DIR_OPS,
	OPT_SIGSEGV,
	OPT_SIGSEGV_OPS,
	OPT_MMAP,
	OPT_MMAP_OPS,
	OPT_MMAP_BYTES,
	OPT_QSORT,
	OPT_QSORT_OPS,
	OPT_QSORT_INTEGERS,
	OPT_BIGHEAP_OPS,
	OPT_RENAME_OPS,
	OPT_UTIME,
	OPT_UTIME_OPS,
	OPT_UTIME_FSYNC,
	OPT_FSTAT,
	OPT_FSTAT_OPS,
	OPT_FSTAT_DIR
} stress_op;

/* stress test metadata */
typedef struct {
	const func stress_func;		/* stress test function */
	const stress_id id;		/* stress test ID */
	const short int short_getopt;	/* getopt short option */
	const stress_op op;		/* ops option */
	const char *name;		/* name of stress test */
} stress_t;

/*
 *  the CPU stress test has different classes of cpu stressor
 */
typedef void (*stress_cpu_func)(void);

typedef struct {
	const char		*name;	/* human readable form of stressor */
	const stress_cpu_func	func;	/* the stressor function */
} stress_cpu_stressor_info_t;

#if defined (__linux__)
/*
 *  See ioprio_set(2) and linux/ioprio.h, glibc has no definitions
 *  for these at present. Also refer to Documentation/block/ioprio.txt
 *  in the Linux kernel source.
 */
#define IOPRIO_CLASS_RT 	(1)
#define IOPRIO_CLASS_BE		(2)
#define IOPRIO_CLASS_IDLE	(3)

#define IOPRIO_WHO_PROCESS	(1)
#define IOPRIO_WHO_PGRP		(2)
#define IOPRIO_WHO_USER		(3)

#define IOPRIO_PRIO_VALUE(class, data)	(((class) << 13) | data)
#endif

typedef struct {
	pid_t	pid;		/* process id */
	double	start;		/* time process started */
	double	finish;		/* time process got reaped */
} proc_info_t;

typedef struct {
	const char	ch;	/* Scaling suffix */
	const uint64_t	scale;	/* Amount to scale by */
} scale_t;

static int print(FILE *fp, const int flag,
	const char *const fmt, ...) __attribute__((format(printf, 3, 4)));

/* Various option settings and flags */
static const char *app_name = "stress-ng";		/* Name of application */
static sem_t	sem;					/* stress_semaphore sem */
static uint8_t *mem_chunk;				/* Cache load shared memory */
static uint64_t	opt_dentries = DEFAULT_DENTRIES;	/* dentries per loop */
static uint64_t opt_ops[STRESS_MAX];			/* max number of bogo ops */
static uint64_t	opt_vm_hang = DEFAULT_VM_HANG;		/* VM delay */
static uint64_t	opt_hdd_bytes = DEFAULT_HDD_BYTES;	/* HDD size in byts */
static uint64_t opt_hdd_write_size = DEFAULT_HDD_WRITE_SIZE;
static uint64_t	opt_timeout = DEFAULT_TIMEOUT;		/* timeout in seconds */
static uint64_t	mwc_z = 362436069, mwc_w = 521288629;	/* random number vals */
static uint64_t opt_qsort_size = 256 * 1024;		/* Default qsort size */
static int64_t	opt_backoff = DEFAULT_BACKOFF;		/* child delay */
static int32_t	started_procs[STRESS_MAX];		/* number of processes per stressor */
static int32_t	opt_flags = PR_ERR | PR_INF;		/* option flags */
static int32_t  opt_cpu_load = 100;			/* CPU max load */
static stress_cpu_stressor_info_t *opt_cpu_stressor;	/* Default stress CPU method */
static size_t	opt_vm_bytes = DEFAULT_VM_BYTES;	/* VM bytes */
static size_t	opt_vm_stride = DEFAULT_VM_STRIDE;	/* VM stride */
static int	opt_vm_flags = 0;			/* VM mmap flags */
static size_t	opt_mmap_bytes = DEFAULT_MMAP_BYTES;	/* MMAP size */
static pid_t	socket_server, socket_client;		/* pids of socket client/servers */
#if defined (__linux__)
static uint64_t	opt_timer_freq = 1000000;		/* timer frequency (Hz) */
static int	opt_sched = UNDEFINED;			/* sched policy */
static int	opt_sched_priority = UNDEFINED;		/* sched priority */
static int 	opt_ionice_class = UNDEFINED;		/* ionice class */
static int	opt_ionice_level = UNDEFINED;		/* ionice level */
#endif
static int	opt_socket_port = 5000;			/* Default socket port */
static char	*opt_fstat_dir = "/dev";		/* Default fstat directory */
static volatile bool opt_do_run = true;			/* false to exit stressor */
static proc_info_t *procs[STRESS_MAX];			/* per process info */
static stress_cpu_stressor_info_t cpu_methods[];	/* fwd decl of cpu stressor methods */

/*
 *  externs to force gcc to stash computed values and hence
 *  to stop the optimiser optimising code away to zero. The
 *  *_put funcs are essentially no-op functions.
 */
extern void double_put(const double a);
extern void uint64_put(const uint64_t a);

/*
 *  Catch signals and set flag to break out of stress loops
 */
static void stress_sighandler(int dummy)
{
	(void)dummy;
	opt_do_run = false;
}

static void pr_failed(const int flag, const char *name, const char *what)
{
	print(stderr, flag, "%s: %s failed, errno=%d (%s)\n",
		name, what, errno, strerror(errno));
}

/*
 *  stress_sethandler()
 *	set signal handler to catch SIGINT and SIGALRM
 */
static int stress_sethandler(const char *stress)
{
	struct sigaction new_action;

	new_action.sa_handler = stress_sighandler;
	sigemptyset(&new_action.sa_mask);
	new_action.sa_flags = 0;
	if (sigaction(SIGINT, &new_action, NULL) < 0) {
		pr_failed_err(stress, "sigaction");
		return -1;
	}
	if (sigaction(SIGALRM, &new_action, NULL) < 0) {
		pr_failed_err(stress, "sigaction");
		return -1;
	}
	return 0;
}

#if defined (__linux__)
/* Set process name, we don't care if it fails */
#define set_proc_name(name) 			\
	if (!(opt_flags & OPT_FLAGS_KEEP_NAME)) \
		(void)prctl(PR_SET_NAME, name)
#else
#define set_proc_name(name)
#endif

#if defined (__linux__)
/*
 *  set_oom_adjustment()
 *	attempt to stop oom killer
 *	if we have root privileges then try and make process
 *	unkillable by oom killer
 */
void set_oom_adjustment(const char *name, bool killable)
{
	char path[PATH_MAX];
	int fd;
	bool high_priv;

	high_priv = (getuid() == 0) && (geteuid() == 0);

	/*
	 *  Try modern oom interface
	 */
	snprintf(path, sizeof(path), "/proc/%d/oom_score_adj", getpid());
	if ((fd = open(path, O_WRONLY)) >= 0) {
		char *str;
		ssize_t n;

		if (killable)
			str = "1000";
		else
			str = high_priv ? "-1000" : "0";

		n = write(fd, str, strlen(str));
		close(fd);

		if (n < 0)
			pr_failed_dbg(name, "can't set oom_score_adj");
		else
			return;
	}
	/*
	 *  Fall back to old oom interface
	 */
	snprintf(path, sizeof(path), "/proc/%d/oom_adj", getpid());
	if ((fd = open(path, O_WRONLY)) >= 0) {
		char *str;
		ssize_t n;

		if (killable)
			str = high_priv ? "-17" : "-16";
		else
			str = "15";

		n = write(fd, str, strlen(str));
		close(fd);

		if (n < 0)
			pr_failed_dbg(name, "can't set oom_adj");
	}
	return;
}
#else
/* no-op */
#define set_oom_adjustment(name, killable)
#endif

#if defined (__linux__)
/*
 *  set_coredump()
 *	limit what is coredumped because
 *	potentially we could have hugh dumps
 *	with the vm and mmap tests
 */
static void set_coredump(const char *name)
{
	char path[PATH_MAX];
	int fd;

	snprintf(path, sizeof(path), "/proc/%d/coredump_filter", getpid());
	if ((fd = open(path, O_WRONLY)) >= 0) {
		const char *str = "0x00";
		ssize_t n = write(fd, str, strlen(str));
		close(fd);

		if (n < 0)
			pr_failed_dbg(name, "can't set coredump_filter");
		else
			return;
	}
}
#else
/* no-op */
#define set_coredump(name)
#endif

/*
 *  mwc()
 *	fast pseudo random number generator, see
 *	http://www.cse.yorku.ca/~oz/marsaglia-rng.html
 */
static uint64_t mwc(void)
{
	mwc_z = 36969 * (mwc_z & 65535) + (mwc_z >> 16);
	mwc_w = 18000 * (mwc_w & 65535) + (mwc_w >> 16);
	return (mwc_z << 16) + mwc_w;
}

/*
 *  mwc_reseed()
 *	dirty mwc reseed
 */
static  void mwc_reseed(void)
{
	struct timeval tv;
	int i, n;

	mwc_z = 0;
	if (gettimeofday(&tv, NULL) == 0)
		mwc_z = (uint64_t)tv.tv_sec ^ (uint64_t)tv.tv_usec;
	mwc_z += ~((unsigned char *)&mwc_z - (unsigned char *)&tv);
	mwc_w = (uint64_t)getpid() ^ (uint64_t)getppid()<<12;

	n = (int)mwc_z % 1733;
	for (i = 0; i < n; i++)
		(void)mwc();
}


/*
 *  timeval_to_double()
 *      convert timeval to seconds as a double
 */
static double timeval_to_double(const struct timeval *tv)
{
	return (double)tv->tv_sec + ((double)tv->tv_usec / 1000000.0);
}

/*
 *  time_now()
 *	time in seconds as a double
 */
static double time_now(void)
{
	struct timeval now;

	gettimeofday(&now, NULL);
	return timeval_to_double(&now);
}

/*
 *  print()
 *	print some debug or info messages
 */
static int print(
	FILE *fp,
	const int flag,
	const char *const fmt, ...)
{
	va_list ap;
	int ret = 0;

	va_start(ap, fmt);
	if (opt_flags & flag) {
		char buf[4096];
		const char *type = "";
		int n;

		if (flag & PR_ERR)
			type = "error";
		if (flag & PR_DBG)
			type = "debug";
		if (flag & PR_INF)
			type = "info";

		n = snprintf(buf, sizeof(buf), "%s: %s: [%i] ",
			app_name, type, getpid());
		ret = vsnprintf(buf + n, sizeof(buf) - n, fmt, ap);
		fprintf(fp, "%s", buf);
		fflush(fp);
	}
	va_end(ap);

	return ret;
}

/*
 *  check_value()
 *	sanity check number of workers
 */
static void check_value(
	const char *const msg,
	const int val)
{
	if (val < 0 || val > STRESS_PROCS_MAX) {
		fprintf(stderr, "Number of %s workers must be between "
			"0 and %d\n", msg, STRESS_PROCS_MAX);
		exit(EXIT_FAILURE);
	}
}

/*
 *  check_range()
 *	Sanity check val against a lo - hi range
 */
static void check_range(
	const char *const opt,
	const uint64_t val,
	const uint64_t lo,
	const uint64_t hi)
{
	if (val < lo || val > hi) {
		fprintf(stderr, "Value %" PRId64 " is out of range for %s,"
			" allowed: %" PRId64 " .. %" PRId64 "\n",
			val, opt, lo, hi);
		exit(EXIT_FAILURE);
	}
}

#if defined (__linux__)
/*
 *  set_sched()
 * 	are sched settings valid, if so, set them
 */
static void set_sched(const int sched, const int sched_priority)
{
#if defined (SCHED_FIFO) || defined (SCHED_RR)
	int min, max;
#endif
	int rc;
	struct sched_param param;

	switch (sched) {
	case UNDEFINED:	/* No preference, don't set */
		return;
#if defined (SCHED_FIFO) || defined (SCHED_RR)
	case SCHED_FIFO:
	case SCHED_RR:
		min = sched_get_priority_min(sched);
		max = sched_get_priority_max(sched);
		if ((sched_priority == UNDEFINED) ||
		    (sched_priority > max) ||
		    (sched_priority < min)) {
			fprintf(stderr, "Scheduler priority level must be set between %d and %d\n",
				min, max);
			exit(EXIT_FAILURE);
		}
		param.sched_priority = sched_priority;
		break;
#endif
	default:
		if (sched_priority != UNDEFINED)
			fprintf(stderr, "Cannot set sched priority for chosen scheduler, defaulting to 0\n");
		param.sched_priority = 0;
	}
	pr_dbg(stderr, "setting scheduler class %d, priority %d\n",
		sched, param.sched_priority);
	rc = sched_setscheduler(getpid(), sched, &param);
	if (rc < 0) {
		fprintf(stderr, "Cannot set scheduler priority: errno=%d (%s)\n",
			errno, strerror(errno));
		exit(EXIT_FAILURE);
	}
}
#endif

#if defined (__linux__)
/*
 *  get_opt_sched()
 *	get scheduler policy
 */
static int get_opt_sched(const char *const str)
{
#ifdef SCHED_OTHER
	if (!strcmp("other", str))
		return SCHED_OTHER;
#endif
#ifdef SCHED_BATCH
	if (!strcmp("batch", str))
		return SCHED_BATCH;
#endif
#ifdef SCHED_IDLE
	if (!strcmp("idle", str))
		return SCHED_IDLE;
#endif
#ifdef SCHED_FIFO
	if (!strcmp("fifo", str))
		return SCHED_FIFO;
#endif
#ifdef SCHED_RR
	if (!strcmp("rr", str))
		return SCHED_RR;
#endif
	if (strcmp("which", str))
		fprintf(stderr, "Invalid sched option: %s\n", str);
	fprintf(stderr, "Available scheduler options are:"
#ifdef SCHED_OTHER
		" other"
#endif
#ifdef SCHED_BATCH
		" batch"
#endif
#ifdef SCHED_IDLE
		" idle"
#endif
#ifdef SCHED_FIFO
		" fifo"
#endif
#ifdef SCHED_FIFO
		" rr"
#endif
		"\n");
	exit(EXIT_FAILURE);
}
#endif

#if defined (__linux__)
/*
 *  ioprio_set()
 *	ioprio_set system call
 */
static int ioprio_set(const int which, const int who, const int ioprio)
{
        return syscall(SYS_ioprio_set, which, who, ioprio);
}
#endif

#if defined (__linux__)
/*
 *  get_opt_ionice_class()
 *	string io scheduler to IOPRIO_CLASS
 */
static int get_opt_ionice_class(const char *const str)
{
	if (!strcmp("idle", str))
		return IOPRIO_CLASS_IDLE;
	if (!strcmp("besteffort", str) ||
	    !strcmp("be", str))
		return IOPRIO_CLASS_BE;
	if (!strcmp("realtime", str) ||
	    !strcmp("rt", str))
		return IOPRIO_CLASS_RT;
	if (strcmp("which", str))
		fprintf(stderr, "Invalid ionice-class option: %s\n", str);
	fprintf(stderr, "Available options are: idle besteffort be realtime rt\n");
	exit(EXIT_FAILURE);
}
#endif

#if defined (__linux__)
/*
 *  set_iopriority()
 *	check ioprio settings and set
 */
static void set_iopriority(const int class, const int level)
{
	int data = level, rc;

	switch (class) {
	case UNDEFINED:	/* No preference, don't set */
		return;
	case IOPRIO_CLASS_RT:
	case IOPRIO_CLASS_BE:
		if (level < 0 || level > 7) {
			fprintf(stderr, "Priority levels range from 0 (max) to 7 (min)\n");
			exit(EXIT_FAILURE);
		}
		break;
	case IOPRIO_CLASS_IDLE:
		if ((level != UNDEFINED) &&
		    (level != 0))
			fprintf(stderr, "Cannot set priority level with idle, defaulting to 0\n");
		data = 0;
		break;
	default:
		fprintf(stderr, "Unknown priority class: %d\n", class);
		exit(EXIT_FAILURE);
	}
	rc = ioprio_set(IOPRIO_WHO_PROCESS, 0, IOPRIO_PRIO_VALUE(class, data));
	if (rc < 0) {
		fprintf(stderr, "Cannot set I/O priority: errno=%d (%s)\n",
			errno, strerror(errno));
		exit(EXIT_FAILURE);
	}
}
#endif

#if defined (__linux__)
/*
 *  get_int()
 *	string to int
 */
static int get_int(const char *const str)
{
	int val;

	if (sscanf(str, "%12d", &val) != 1) {
		fprintf(stderr, "Invalid number %s\n", str);
		exit(EXIT_FAILURE);
	}
	return val;
}
#endif

/*
 *  get_uint64()
 *	string to uint64_t
 */
static uint64_t get_uint64(const char *const str)
{
	uint64_t val;

	if (sscanf(str, "%" SCNu64, &val) != 1) {
		fprintf(stderr, "Invalid number %s\n", str);
		exit(EXIT_FAILURE);
	}
	return val;
}

/*
 *  get_uint64_scale()
 *	get a value and scale it by the given scale factor
 */
static uint64_t get_uint64_scale(
	const char *const str,
	const scale_t scales[],
	const char *const msg)
{
	uint64_t val;
	size_t len = strlen(str);
	char ch;
	int i;

	val = get_uint64(str);
	if (len == 0) {
		fprintf(stderr, "Value %s is an invalid size\n", str);
		exit(EXIT_FAILURE);
	}
	len--;
	ch = str[len];

	if (isdigit(ch))
		return val;

	ch = tolower(ch);
	for (i = 0; scales[i].ch; i++) {
		if (ch == scales[i].ch)
			return val * scales[i].scale;
	}

	printf("Illegal %s specifier %c\n", msg, str[len]);
	exit(EXIT_FAILURE);
}

/*
 *  get_uint64_byte()
 *	size in bytes, K bytes, M bytes or G bytes
 */
static uint64_t get_uint64_byte(const char *const str)
{
	static const scale_t scales[] = {
		{ 'b', 	1 },
		{ 'k',  1 << 10 },
		{ 'm',  1 << 20 },
		{ 'g',  1 << 30 },
		{ 0,    0 },
	};

	return get_uint64_scale(str, scales, "length");
}

/*
 *  get_uint64_time()
 *	time in seconds, minutes, hours, days or years
 */
static uint64_t get_uint64_time(const char *const str)
{
	static const scale_t scales[] = {
		{ 's', 	1 },
		{ 'm',  60 },
		{ 'h',  3600 },
		{ 'd',  24 * 3600 },
		{ 'y',  365 * 24 * 3600 },
	};

	return get_uint64_scale(str, scales, "time");
}

/*
 *  stress on sync()
 *	stress system by IO sync calls
 */
static int stress_iosync(
	uint64_t *const counter,
	const uint32_t instance,
	const uint64_t max_ops,
	const char *name)
{
	(void)instance;
	(void)name;

	do {
		sync();
		(*counter)++;
	} while (opt_do_run && (!max_ops || *counter < max_ops));

	return EXIT_SUCCESS;
}

/*
 *  stress_cpu_sqrt()
 *	stress CPU on square roots
 */
static void stress_cpu_sqrt(void)
{
	int i;

	for (i = 0; i < 16384; i++)
		sqrt((double)mwc());
}

/*
 *  stress_cpu_loop()
 *	simple CPU busy loop
 */
static void stress_cpu_loop(void)
{
	int i, i_sum = 0;

	for (i = 0; i < 16384; i++) {
		i_sum += i;
		__asm__ __volatile__("");	/* Stop optimising out */
	}

	uint64_put(i_sum);
}

/*
 *  stress_cpu_gcd()
 *	compute Greatest Common Divisor
 */
static void stress_cpu_gcd(void)
{
	int i, i_sum = 0;

	for (i = 0; i < 16384; i++) {
		register int a = i;
		register int b = mwc();
		register int r = 0;

		while (b) {
			r = a % b;
			a = b;
			b = r;
		}
		i_sum += r;
		__asm__ __volatile__("");	/* Stop optimising out */
	}

	uint64_put(i_sum);
}

/*
 *  stress_cpu_bitops()
 *	various bit manipulation hacks from bithacks
 *	https://graphics.stanford.edu/~seander/bithacks.html
 */
static void stress_cpu_bitops(void)
{
	unsigned int i, i_sum = 0;

	for (i = 0; i < 16384; i++) {
		{
			unsigned int r, v;
			int s = (sizeof(v) * 8) - 1;

			/* Reverse bits */
			r = v = i;
			for (v >>= 1; v; v >>= 1, s--) {
				r <<= 1;
				r |= v & 1;
			}
			r <<= s;
			i_sum += r;
		}
		{
			/* parity check */
			unsigned int v = i;
			v ^= v >> 16;
			v ^= v >> 8;
			v ^= v >> 4;
			v &= 0xf;
			i_sum += v;
		}
		{
			/* Brian Kernighan count bits */
			unsigned int j, v = i;
			for (j = 0; v; j++)
				v &= v - 1;
			i_sum += j;
		}
		{
			/* round up to nearest highest power of 2 */
			unsigned int v = i - 1;
			v |= v >> 1;
			v |= v >> 2;
			v |= v >> 4;
			v |= v >> 8;
			v |= v >> 16;
			i_sum += v;
		}
		uint64_put(i_sum);
	}
}

/*
 *  stress_cpu_trig()
 *	simple sin, cos trig functions
 */
static void stress_cpu_trig(void)
{
	int i;
	double d_sum = 0.0;

	for (i = 0; i < 16384; i++) {
		double theta = (2.0 * M_PI * (double)i)/16384.0;
		d_sum += (cos(theta) * sin(theta));
	}
	double_put(d_sum);
}

/*
 *  stress_cpu_rand()
 *	generate lots of pseudo-random integers
 */
static void stress_cpu_rand(void)
{
	int i;

	for (i = 0; i < 16384; i++)
		mwc();
}

/*
 *  stress_cpu_nsqrt()
 *	iterative Newton–Raphson square root
 */
static void stress_cpu_nsqrt(void)
{
	int i;

	for (i = 0; i < 16384; i++) {
		double n = (double)i;
		double lo = (n < 1.0) ? n : 1.0;
		double hi = (n < 1.0) ? 1.0 : n;

		while ((hi - lo) > 0.00001) {
			double g = (lo + hi) / 2.0;
			if (g * g > n)
				hi = g;
			else
				lo = g;
		}
		double_put((lo + hi) / 2.0);
	}
}

/*
 *  stress_cpu_phi()
 *	compute the Golden Ratio
 */
static void stress_cpu_phi(void)
{
	double phi; /* Golden ratio */
	register uint64_t a, b;
	const uint64_t mask = 1ULL << 63;
	int i;

	/* Pick any two starting points */
	a = mwc() % 99;
	b = mwc() % 99;

	/* Iterate until we approach overflow */
	for (i = 0; (i < 64) && !((a | b) & mask); i++) {
		/* Find nth term */
		register uint64_t c = a + b;

		a = b;
		b = c;
	}
	/* And we have the golden ratio */
	phi = (double)a / (double)b;

	double_put(phi);
}

/*
 *  fft_partial()
 *  	partial Fast Fourier Transform
 */
static void fft_partial(complex *data, complex *tmp, const int n, const int m)
{
	if (m < n) {
		const int m2 = m * 2;
		int i;

		fft_partial(tmp, data, n, m2);
		fft_partial(tmp + m, data + m, n, m2);
		for (i = 0; i < n; i += m2) {
			complex v = tmp[i];
			complex t =
				cexp((-I * M_PI * (double)i) /
				     (double)n) * tmp[i + m];
			data[i / 2] = v + t;
			data[(i + n) / 2] = v - t;
		}
	}
}

/*
 *  stress_cpu_fft()
 *	Fast Fourier Transform
 */
static void stress_cpu_fft(void)
{
	complex buf[FFT_SIZE], tmp[FFT_SIZE];
	int i;

	for (i = 0; i < FFT_SIZE; i++)
		buf[i] = (complex)(i % 63);

	memcpy(tmp, buf, sizeof(complex) * FFT_SIZE);
	fft_partial(buf, tmp, FFT_SIZE, 1);
}

/*
 *   stress_cpu_euler()
 *	compute e using series
 */
static void stress_cpu_euler(void)
{
	long double e = 1.0;
	long double fact = 1.0;
	int n = 0;

	/* Arbitary precision chosen */
	for (n = 1; n < 32; n++) {
		fact *= n;
		e += (1.0 / fact);
	}

	double_put(e);
}

/*
 *  stress_cpu_jenkin()
 *	Jenkin's hash on 128 byte random data
 *	http://www.burtleburtle.net/bob/hash/doobs.html
 */
static void stress_cpu_jenkin(void)
{
	const size_t len = 128;
	uint8_t i, key;
	register uint32_t h = 0;

	for (i = 0; i < len; i++) {
		key = mwc() & 0xff;
		h += key;
		h += h << 10;
		h ^= h >> 6;
	}
	h += h << 3;
	h ^= h >> 11;
	h += h << 15;

	uint64_put(h);
}

/*
 *  stress_cpu_idct()
 *	compute 8x8 Inverse Discrete Cosine Transform
 */
static void stress_cpu_idct(void)
{
	const double invsqrt2 = 1.0 / sqrt(2.0);
	const double pi_over_16 = M_PI / 16.0;
	const int sz = 8;
	int i, j, u, v;
	float data[sz][sz], idct[sz][sz];

	/*
	 *  Set up DCT
	 */
	for (i = 0; i < sz; i++) {
		for (j = 0; j < sz; j++) {
			data[i][j] = (i + j == 0) ? 2040: 0;
		}
	}
	for (i = 0; i < sz; i++) {
		const double pi_i = (i + i + 1) * pi_over_16;

		for (j = 0; j < sz; j++) {
			const double pi_j = (j + j + 1) * pi_over_16;
			double sum = 0.0;

			for (u = 0; u < sz; u++) {
				const double cos_pi_i_u = cos(pi_i * u);

				for (v = 0; v < sz; v++) {
					const double cos_pi_j_v = cos(pi_j * v);

					sum += (data[u][v] *
						(u ? 1.0 : invsqrt2) *
						(v ? 1.0 : invsqrt2) *
						cos_pi_i_u * cos_pi_j_v);
				}
			}
			idct[i][j] = 0.25 * sum;
		}
	}
	/* Final output should be a 8x8 matrix of values 255 */
	for (i = 0; i < sz; i++) {
		for (j = 0; j < sz; j++) {
			if ((int)idct[i][j] != 255) {
				uint64_put(1);
				return;
			}
		}
	}
}

#define int_ops(a, b, mask)			\
	do {					\
		a += b;				\
		b ^= a;				\
		a >>= 1;			\
		b <<= 2;			\
		b -= a;				\
		a ^= ~0;			\
		b ^= ((~0xf0f0f0f0f0f0f0f0ULL) & mask);	\
		a *= 3;				\
		b *= 7;				\
		a += 2;				\
		b -= 3;				\
		a /= 77;			\
		b /= 3;				\
		a <<= 1;			\
		b <<= 2;			\
		a |= 1;				\
		b |= 3;				\
		a *= mwc();			\
		b ^= mwc();			\
		a += mwc();			\
		b -= mwc();			\
		a /= 7;				\
		b /= 9;				\
		a |= ((0x1000100010001000ULL) & mask);	\
		b &= ((0xffeffffefebefffeULL) & mask);	\
	} while (0);

/*
 *  stress_cpu_int64()
 *	mix of integer ops
 */
static void stress_cpu_int64(void)
{
	register uint64_t a = mwc(), b = mwc();
	int i;

	for (i = 0; i < 10000; i++) {
		int_ops(a, b, 0xffffffffffffULL);
		if (!opt_do_run)
			break;
	}
	uint64_put(a * b);
}

/*
 *  stress_cpu_int32()
 *	mix of integer ops
 */
static void stress_cpu_int32(void)
{
	register uint32_t a = mwc(), b = mwc();
	int i;

	for (i = 0; i < 10000; i++) {
		int_ops(a, b, 0xffffffffUL);
		if (!opt_do_run)
			break;
	}
	uint64_put((a ^ b) & 0xffffffff);
}

/*
 *  stress_cpu_int16()
 *	mix of integer ops
 */
static void stress_cpu_int16(void)
{
	register uint16_t a = mwc(), b = mwc();
	int i;

	for (i = 0; i < 10000; i++) {
		int_ops(a, b, 0xffff);
		if (!opt_do_run)
			break;
	}
	uint64_put((a ^ b) & 0xffff);
}

/*
 *  stress_cpu_int8()
 *	mix of integer ops
 */
static void stress_cpu_int8(void)
{
	register uint8_t a = mwc(), b = mwc();
	int i;

	for (i = 0; i < 10000; i++) {
		int_ops(a, b, 0xff);
		if (!opt_do_run)
			break;
	}
	uint64_put((a + b) ^ 0xff);
}

#define float_ops(a, b, c, d)		\
	do {				\
		a = a + b;		\
		b = a * c;		\
		c = a - b;		\
		d = a / b;		\
		a = c / 0.1923;		\
		b = c + a;		\
		c = b * 3.12;		\
		d = d + b + sin(a);	\
		a = (b + c) / c;	\
		b = b * c;		\
		c = c + 1.0;		\
		d = d - sin(c);		\
		a = a * cos(b);		\
		b = b + cos(c);		\
		c = sin(a) / 2.344;	\
		b = d - 1.0;		\
	} while (0)

/*
 *  stress_cpu_float()
 *	mix of floating point ops
 */
static void stress_cpu_float(void)
{
	uint32_t i;
	float a = 0.18728, b = mwc(), c = mwc(), d;

	for (i = 0; i < 10000; i++) {
		float_ops(a, b, c, d);
		if (!opt_do_run)
			break;
	}
	double_put(a + b + c + d);
}

/*
 *  stress_cpu_double()
 *	mix of floating point ops
 */
static void stress_cpu_double(void)
{
	uint32_t i;
	double a = 0.18728, b = mwc(), c = mwc(), d;

	for (i = 0; i < 10000; i++) {
		float_ops(a, b, c, d);
		if (!opt_do_run)
			break;
	}
	double_put(a + b + c + d);
}

/*
 *  stress_cpu_longdouble()
 *	mix of floating point ops
 */
static void stress_cpu_longdouble(void)
{
	uint32_t i;
	long double a = 0.18728, b = mwc(), c = mwc(), d;

	for (i = 0; i < 10000; i++) {
		float_ops(a, b, c, d);
		if (!opt_do_run)
			break;
	}
	double_put(a + b + c + d);
}

/*
 *  stress_cpu_rgb()
 *	CCIR 601 RGB to YUV to RGB conversion
 */
static void stress_cpu_rgb(void)
{
	int i;
	uint32_t rgb = mwc() & 0xffffff;
	uint8_t r = rgb >> 16;
	uint8_t g = rgb >> 8;
	uint8_t b = rgb;

	/* Do a 1000 colours starting from the rgb seed */
	for (i = 0; i < 1000; i++) {
		float y,u,v;

		/* RGB to CCIR 601 YUV */
		y = (0.299 * r) + (0.587 * g) + (0.114 * b);
		u = (b - y) * 0.565;
		v = (r - y) * 0.713;

		/* YUV back to RGB */
		r = y + (1.403 * v);
		g = y - (0.344 * u) - (0.714 * v);
		b = y + (1.770 * u);

		/* And bump each colour to make next round */
		r += 1;
		g += 2;
		b += 3;
	}
	uint64_put(r + g + b);
}

/*
 *  stress_cpu_matrix_prod(void)
 *	matrix product
 */
static void stress_cpu_matrix_prod(void)
{
	int i, j, k;
	const int n = 128;

	long double a[n][n], b[n][n], r[n][n];
	long double v = 1 / (long double)((uint32_t)~0);

	for (i = 0; i < n; i++) {
		for (j = 0; j < n; j++) {
			a[i][j] = (long double)mwc() * v;
			b[i][j] = (long double)mwc() * v;
			r[i][j] = 0.0;
		}
	}

	for (i = 0; i < n; i++)
		for (j = 0; j < n; j++)
			for (k = 0; k < n; k++)
				r[i][j] += a[i][k] * b[k][j];
}

/*
 *   stress_cpu_fibonacci()
 *	compute fibonacci series
 */
static void stress_cpu_fibonacci(void)
{
	register uint64_t f1 = 0, f2 = 1, fn;

	do {
		fn = f1 + f2;
		f1 = f2;
		f2 = fn;
	} while (!(fn & 0x8000000000000000ULL));

	uint64_put(fn);
}

/*
 *   stress_cpu_ln2
 *	compute ln(2) using series
 */
static void stress_cpu_ln2(void)
{
	register double ln2 = 0.0;
	register const double math_ln2 = log(2.0);
	register uint32_t n = 1;

	/* Arbitary precision chosen */
	while (n < 1000000) {
		double delta;
		/* Unroll, do several ops */
		ln2 += (long double)1.0 / (long double)n++;
		ln2 -= (long double)1.0 / (long double)n++;
		ln2 += (long double)1.0 / (long double)n++;
		ln2 -= (long double)1.0 / (long double)n++;
		ln2 += (long double)1.0 / (long double)n++;
		ln2 -= (long double)1.0 / (long double)n++;
		ln2 += (long double)1.0 / (long double)n++;
		ln2 -= (long double)1.0 / (long double)n++;

		/* Arbitarily accurate enough? */
		delta = ln2 - math_ln2;
		delta = (delta < 0.0) ? -delta : delta;
		if (delta < 0.000001)
			break;
	}

	double_put(ln2);
}

/*
 *  ackermann()
 *	a naive/simple implementation of the ackermann function
 */
static uint32_t ackermann(const uint32_t m, const uint32_t n)
{
	if (m == 0)
		return n + 1;
	else if (n == 0)
		return ackermann(m - 1, 1);
	else
		return ackermann(m - 1, ackermann(m, n - 1));
}

/*
 *   stress_cpu_ackermann
 *	compute ackermann function
 */
static void stress_cpu_ackermann(void)
{
	(void)ackermann(3, 10);
}

/*
 *   stress_cpu_explog
 *	compute exp(log(n))
 */
static void stress_cpu_explog(void)
{
	uint32_t i;
	double n = 1e6;

	for (i = 1; i < 100000; i++)
		n = exp(log(n) / 1.00002);
}



/*
 *  stress_cpu_all()
 *	iterate over all cpu stressors
 */
static void stress_cpu_all(void)
{
	static int i = 1;	/* Skip over stress_cpu_all */

	cpu_methods[i++].func();
	if (!cpu_methods[i].func)
		i = 1;
}

/*
 * Table of cpu stress methods
 */
static stress_cpu_stressor_info_t cpu_methods[] = {
	{ "all",	stress_cpu_all },	/* Special "all test */

	{ "ackermann",	stress_cpu_ackermann },
	{ "bitops",	stress_cpu_bitops },
	{ "double",	stress_cpu_double },
	{ "euler",	stress_cpu_euler },
	{ "explog",	stress_cpu_explog },
	{ "fibonacci",	stress_cpu_fibonacci },
	{ "fft",	stress_cpu_fft },
	{ "float",	stress_cpu_float },
	{ "gcd",	stress_cpu_gcd },
	{ "idct",	stress_cpu_idct },
	{ "int64",	stress_cpu_int64 },
	{ "int32",	stress_cpu_int32 },
	{ "int16",	stress_cpu_int16 },
	{ "int8",	stress_cpu_int8 },
	{ "jenkin",	stress_cpu_jenkin },
	{ "ln2",	stress_cpu_ln2 },
	{ "longdouble",	stress_cpu_longdouble },
	{ "loop",	stress_cpu_loop },
	{ "matrixprod",	stress_cpu_matrix_prod },
	{ "nsqrt",	stress_cpu_nsqrt },
	{ "phi",	stress_cpu_phi },
	{ "rand",	stress_cpu_rand },
	{ "rgb",	stress_cpu_rgb },
	{ "sqrt", 	stress_cpu_sqrt },
	{ "trig",	stress_cpu_trig },
	{ NULL,		NULL }
};

/*
 *  stress_cpu_find_by_name()
 *	find cpu stress method by name
 */
static inline stress_cpu_stressor_info_t *stress_cpu_find_by_name(const char *name)
{
	stress_cpu_stressor_info_t *info = cpu_methods;

	for (info = cpu_methods; info->func; info++) {
		if (!strcmp(info->name, name))
			return info;
	}
	return NULL;
}

/*
 *  stress_cpu()
 *	stress CPU by doing floating point math ops
 */
static int stress_cpu(
	uint64_t *const counter,
	const uint32_t instance,
	const uint64_t max_ops,
	const char *name)
{
	double bias;
	stress_cpu_func func = opt_cpu_stressor->func;

	(void)instance;
	(void)name;

	/*
	 * Normal use case, 100% load, simple spinning on CPU
	 */
	if (opt_cpu_load == 100) {
		do {
			(void)func();
			(*counter)++;
		} while (opt_do_run && (!max_ops || *counter < max_ops));
		return EXIT_SUCCESS;
	}

	/*
	 * It is unlikely, but somebody may request to do a zero
	 * load stress test(!)
	 */
	if (opt_cpu_load == 0) {
		sleep((int)opt_timeout);
		return EXIT_SUCCESS;
	}

	/*
	 * More complex percentage CPU utilisation.  This is
	 * not intended to be 100% accurate timing, it is good
	 * enough for most purposes.
	 */
	bias = 0.0;
	do {
		int j;
		double t, delay;
		struct timeval tv1, tv2, tv3;

		gettimeofday(&tv1, NULL);
		for (j = 0; j < 64; j++) {
			(void)func();
			if (!opt_do_run)
				break;
			(*counter)++;
		}
		gettimeofday(&tv2, NULL);
		t = timeval_to_double(&tv2) - timeval_to_double(&tv1);
		/* Must not calculate this with zero % load */
		delay = t * (((100.0 / (double) opt_cpu_load)) - 1.0);
		delay -= bias;

		tv1.tv_sec = delay;
		tv1.tv_usec = (delay - tv1.tv_sec) * 1000000.0;
		select(0, NULL, NULL, NULL, &tv1);
		gettimeofday(&tv3, NULL);
		/* Bias takes account of the time to do the delay */
		bias = (timeval_to_double(&tv3) - timeval_to_double(&tv2)) - delay;
	} while (opt_do_run && (!max_ops || *counter < max_ops));

	return EXIT_SUCCESS;
}

/*
 *  stress_vm()
 *	stress virtual memory
 */
static int stress_vm(
	uint64_t *const counter,
	const uint32_t instance,
	const uint64_t max_ops,
	const char *name)
{
	uint8_t *buf = NULL;
	uint8_t	val = 0;
	size_t	i;
	const bool keep = (opt_flags & OPT_FLAGS_VM_KEEP);

	(void)instance;

	do {
		const uint8_t gray_code = (val >> 1) ^ val;
		val++;

		if (!keep || (keep && buf == NULL)) {
			buf = mmap(NULL, opt_vm_bytes, PROT_READ | PROT_WRITE,
				MAP_PRIVATE | MAP_ANONYMOUS | opt_vm_flags, -1, 0);
			if (buf == MAP_FAILED) {
				pr_failed_dbg(name, "mmap");
				continue;	/* Try again */
			}
		}

		for (i = 0; i < opt_vm_bytes; i += opt_vm_stride) {
			*(buf + i) = gray_code;
			if (!opt_do_run)
				goto unmap_cont;
		}

		if (opt_vm_hang == 0) {
			for (;;)
				(void)sleep(3600);
		} else if (opt_vm_hang != DEFAULT_VM_HANG) {
			(void)sleep((int)opt_vm_hang);
		}

		for (i = 0; i < opt_vm_bytes; i += opt_vm_stride) {
			if (*(buf + i) != gray_code) {
				pr_err(stderr, "%s: detected memory error, offset : %zd, got: %x\n",
					name, i, *(buf + i));
				(void)munmap(buf, opt_vm_bytes);
				return EXIT_FAILURE;
			}
			if (!opt_do_run)
				goto unmap_cont;
		}

unmap_cont:
		if (!keep)
			(void)munmap(buf, opt_vm_bytes);

		(*counter)++;
	} while (opt_do_run && (!max_ops || *counter < max_ops));

	if (keep)
		(void)munmap(buf, opt_vm_bytes);

	return EXIT_SUCCESS;
}

/*
 *  stress_hdd
 *	stress I/O via writes
 */
static int stress_hdd(
	uint64_t *const counter,
	const uint32_t instance,
	const uint64_t max_ops,
	const char *name)
{
	uint8_t *buf;
	uint64_t i;
	const pid_t pid = getpid();
	int rc = EXIT_FAILURE;

	(void)instance;

	if ((buf = malloc((size_t)opt_hdd_write_size)) == NULL) {
		pr_err(stderr, "%s: cannot allocate buffer\n", name);
		return EXIT_FAILURE;
	}

	for (i = 0; i < opt_hdd_write_size; i++)
		buf[i] = (uint8_t)mwc();

	do {
		int fd;
		char filename[64];

		snprintf(filename, sizeof(filename), "./%s-%i.XXXXXXX", name, pid);

		(void)umask(0077);
		if ((fd = mkstemp(filename)) < 0) {
			pr_failed_err(name, "mkstemp");
			goto finish;
		}
		if (!(opt_flags & OPT_FLAGS_NO_CLEAN))
			(void)unlink(filename);

		for (i = 0; i < opt_hdd_bytes; i += opt_hdd_write_size) {
			if (write(fd, buf, (size_t)opt_hdd_write_size) < 0) {
				pr_failed_err(name, "write");
				goto finish;
			}
			(*counter)++;
			if (!opt_do_run || (max_ops && *counter >= max_ops))
				break;
		}
		(void)close(fd);
		if (!(opt_flags & OPT_FLAGS_NO_CLEAN))
			(void)unlink(filename);
	} while (opt_do_run && (!max_ops || *counter < max_ops));

	rc = EXIT_SUCCESS;
finish:
	free(buf);
	return rc;
}

/*
 *  stress_fork()
 *	stress by forking and exiting
 */
static int stress_fork(
	uint64_t *const counter,
	const uint32_t instance,
	const uint64_t max_ops,
	const char *name)
{
	(void)instance;
	(void)name;

	do {
		pid_t pid;

		pid = fork();
		if (pid == 0) {
			/* Child, immediately exit */
			_exit(0);
		}
		if (pid > 0) {
			int status;
			/* Parent, wait for child */
			waitpid(pid, &status, 0);
		}
		(*counter)++;
	} while (opt_do_run && (!max_ops || *counter < max_ops));

	return EXIT_SUCCESS;
}

/*
 *  stress_ctxt
 *	stress by heavy context switching
 */
static int stress_ctxt(
	uint64_t *const counter,
	const uint32_t instance,
	const uint64_t max_ops,
	const char *name)
{
	pid_t pid;
	int pipefds[2];

	(void)instance;

	if (pipe(pipefds) < 0) {
		pr_failed_dbg(name, "pipe");
		return EXIT_FAILURE;
	}

	pid = fork();
	if (pid < 0) {
		(void)close(pipefds[0]);
		(void)close(pipefds[1]);
		pr_failed_dbg(name, "fork");
		return EXIT_FAILURE;
	} else if (pid == 0) {
		(void)close(pipefds[1]);

		for (;;) {
			char ch;

			for (;;) {
				if (read(pipefds[0], &ch, sizeof(ch)) <= 0) {
					pr_failed_dbg(name, "read");
					break;
				}
				if (ch == CTXT_STOP)
					break;
			}
			(void)close(pipefds[0]);
			exit(EXIT_SUCCESS);
		}
	} else {
		char ch = '_';

		/* Parent */
		(void)close(pipefds[0]);

		do {
			if (write(pipefds[1],  &ch, sizeof(ch)) < 0) {
				pr_failed_dbg(name, "write");
				break;
			}
			(*counter)++;
		} while (opt_do_run && (!max_ops || *counter < max_ops));

		ch = CTXT_STOP;
		if (write(pipefds[1],  &ch, sizeof(ch)) <= 0)
			pr_failed_dbg(name, "termination write");
		(void)kill(pid, SIGKILL);
	}

	return EXIT_SUCCESS;
}

/*
 *  stress_pipe
 *	stress by heavy pipe I/O
 */
static int stress_pipe(
	uint64_t *const counter,
	const uint32_t instance,
	const uint64_t max_ops,
	const char *name)
{
	pid_t pid;
	int pipefds[2];

	(void)instance;

	if (pipe(pipefds) < 0) {
		pr_failed_dbg(name, "pipe");
		return EXIT_FAILURE;
	}

	pid = fork();
	if (pid < 0) {
		(void)close(pipefds[0]);
		(void)close(pipefds[1]);
		pr_failed_dbg(name, "fork");
		return EXIT_FAILURE;
	} else if (pid == 0) {
		(void)close(pipefds[1]);

		for (;;) {
			char buf[PIPE_BUF];

			for (;;) {
				if (read(pipefds[0], buf, sizeof(buf)) <= 0) {
					pr_failed_dbg(name, "read");
					break;
				}
				if (buf[0] == PIPE_STOP)
					break;
			}
			(void)close(pipefds[0]);
			exit(EXIT_SUCCESS);
		}
	} else {
		char buf[PIPE_BUF];

		memset(buf, 0x41, sizeof(buf));

		/* Parent */
		(void)close(pipefds[0]);

		do {
			if (write(pipefds[1], buf, sizeof(buf)) < 0) {
				pr_failed_dbg(name, "write");
				break;
			}
			(*counter)++;
		} while (opt_do_run && (!max_ops || *counter < max_ops));

		memset(buf, PIPE_STOP, sizeof(buf));
		if (write(pipefds[1], buf, sizeof(buf)) <= 0)
			pr_failed_dbg(name, "termination write");
		(void)kill(pid, SIGKILL);
	}
	return EXIT_SUCCESS;
}

/*
 *  stress_cache()
 *	stress cache by psuedo-random memory read/writes and
 *	if possible change CPU affinity to try to cause
 *	poor cache behaviour
 */
static int stress_cache(
	uint64_t *const counter,
	const uint32_t instance,
	const uint64_t max_ops,
	const char *name)
{
	unsigned long total = 0;
#if defined(__linux__)
	const long int cpus = sysconf(_SC_NPROCESSORS_CONF);
	unsigned long int cpu = 0;
	cpu_set_t mask;
#endif
	(void)instance;

	do {
		uint64_t i = mwc() & (MEM_CHUNK_SIZE - 1);
		uint64_t r = mwc();
		int j;

		if ((r >> 13) & 1) {
			for (j = 0; j < MEM_CHUNK_SIZE; j++) {
				mem_chunk[i] += mem_chunk[(MEM_CHUNK_SIZE - 1) - i] + r;
				i = (i + 32769) & (MEM_CHUNK_SIZE - 1);
				if (!opt_do_run)
					break;
			}
		} else {
			for (j = 0; j < MEM_CHUNK_SIZE; j++) {
				total += mem_chunk[i] + mem_chunk[(MEM_CHUNK_SIZE - 1) - i];
				i = (i + 32769) & (MEM_CHUNK_SIZE - 1);
				if (!opt_do_run)
					break;
			}
		}
#if defined(__linux__)
		cpu++;
		cpu %= cpus;
		CPU_ZERO(&mask);
		CPU_SET(cpu, &mask);
		sched_setaffinity(0, sizeof(mask), &mask);
#endif
		(*counter)++;
	} while (opt_do_run && (!max_ops || *counter < max_ops));

	pr_dbg(stderr, "%s: total [%lu]\n", name, total);
	return EXIT_SUCCESS;
}

/*
 *  handle_socket_sigalrm()
 *	catch SIGALRM
 */
static void handle_socket_sigalrm(int dummy)
{
	(void)dummy;
	opt_do_run = false;

	if (socket_client)
		(void)kill(socket_client, SIGKILL);
	if (socket_server)
		(void)kill(socket_server, SIGKILL);
}

/*
 *  stress_socket
 *	stress by heavy socket I/O
 */
static int stress_socket(
	uint64_t *const counter,
	const uint32_t instance,
	const uint64_t max_ops,
	const char *name)
{
	pid_t pid;
	int rc = EXIT_SUCCESS;

	pr_dbg(stderr, "%s: process [%d] using socket port %d\n",
		name, getpid(), opt_socket_port + instance);

	pid = fork();
	if (pid < 0) {
		pr_failed_dbg(name, "fork");
		return EXIT_FAILURE;
	} else if (pid == 0) {
		/* Child, client */

		for (;;) {
			char buf[SOCKET_BUF];
			ssize_t n;
			struct sockaddr_in addr;
			int fd;
			int retries = 0;
retry:
			if ((fd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
				pr_failed_dbg(name, "socket");
				exit(EXIT_FAILURE);
			}

			memset(&addr, 0, sizeof(addr));
			addr.sin_family = AF_INET;
			addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
			addr.sin_port = htons(opt_socket_port + instance);

			if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
				(void)close(fd);
				usleep(10000);
				retries++;
				if (retries > 100) {
					pr_failed_dbg(name, "connect");
					break;
				}
				goto retry;
			}

			retries = 0;
			for (;;) {
				n = read(fd, buf, sizeof(buf));
				if (n == 0)
					break;
				if (n < 0) {
					pr_failed_dbg(name, "write");
					break;
				}
			}
			(void)close(fd);
		}
		(void)kill(getppid(), SIGALRM);
		exit(EXIT_FAILURE);
	} else {
		/* Parent, server */

		char buf[SOCKET_BUF];
		int fd, status;
		struct sockaddr_in addr;
		int so_reuseaddr = 1;
		struct sigaction new_action;

		socket_server = getpid();
		socket_client = pid;

		new_action.sa_handler = handle_socket_sigalrm;
		sigemptyset(&new_action.sa_mask);
		new_action.sa_flags = 0;
		if (sigaction(SIGALRM, &new_action, NULL) < 0) {
			pr_failed_err(name, "sigaction");
			rc = EXIT_FAILURE;
			goto die;
		}
		if ((fd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
			pr_failed_dbg(name, "socket");
			rc = EXIT_FAILURE;
			goto die;
		}
		memset(&addr, 0, sizeof(addr));
		addr.sin_family = AF_INET;
		addr.sin_addr.s_addr = htonl(INADDR_ANY);
		addr.sin_port = htons(opt_socket_port + instance);

		if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &so_reuseaddr, sizeof(so_reuseaddr)) < 0) {
			pr_failed_dbg(name, "setsockopt");
			rc = EXIT_FAILURE;
			goto die_close;
		}
		if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
			pr_failed_dbg(name, "bind");
			rc = EXIT_FAILURE;
			goto die_close;
		}
		if (listen(fd, 10) < 0) {
			pr_failed_dbg(name, "listen");
			rc = EXIT_FAILURE;
			goto die_close;
		}

		do {
			int sfd = accept(fd, (struct sockaddr *)NULL, NULL);
			if (sfd >= 0) {
				size_t i;

				memset(buf, 'A' + (*counter % 26), sizeof(buf));
				for (i = 16; i < sizeof(buf); i += 16) {
					int ret = write(sfd, buf, i);
					if (ret < 0) {
						pr_failed_dbg(name, "write");
						break;
					}
				}
				(void)close(sfd);
			}
			(*counter)++;
		} while (opt_do_run && (!max_ops || *counter < max_ops));

die_close:
		(void)close(fd);
die:
		(void)kill(pid, SIGKILL);
		waitpid(pid, &status, 0);
	}
	return rc;
}

#if defined(_POSIX_PRIORITY_SCHEDULING)
/*
 *  stress on sched_yield()
 *	stress system by sched_yield
 */
static int stress_yield(
	uint64_t *const counter,
	const uint32_t instance,
	const uint64_t max_ops,
	const char *name)
{
	(void)instance;
	(void)name;

	do {
		sched_yield();
		(*counter)++;
	} while (opt_do_run && (!max_ops || *counter < max_ops));

	return EXIT_SUCCESS;
}
#endif

#if _XOPEN_SOURCE >= 600 || _POSIX_C_SOURCE >= 200112L
/*
 *  stress_fallocate
 *	stress I/O via fallocate and ftruncate
 */
static int stress_fallocate(
	uint64_t *const counter,
	const uint32_t instance,
	const uint64_t max_ops,
	const char *name)
{
	const pid_t pid = getpid();
	int fd;
	char filename[64];
	uint64_t ftrunc_errs = 0;

	(void)instance;

	snprintf(filename, sizeof(filename), "./%s-%i.XXXXXXX", name, pid);
	(void)umask(0077);
	if ((fd = mkstemp(filename)) < 0) {
		pr_failed_err(name, "mkstemp");
		return EXIT_FAILURE;
	}
	if (!(opt_flags & OPT_FLAGS_NO_CLEAN))
		(void)unlink(filename);

	do {
		(void)posix_fallocate(fd, (off_t)0, 4096 * 4096);
		if (!opt_do_run)
			break;
		fsync(fd);
		if (ftruncate(fd, 0) < 0)
			ftrunc_errs++;
		if (!opt_do_run)
			break;
		fsync(fd);
		(*counter)++;
	} while (opt_do_run && (!max_ops || *counter < max_ops));
	if (ftrunc_errs)
		pr_dbg(stderr, "%s: %" PRIu64
			" ftruncate errors occurred.\n", name, ftrunc_errs);
	(void)close(fd);
	if (!(opt_flags & OPT_FLAGS_NO_CLEAN))
		(void)unlink(filename);

	return EXIT_SUCCESS;
}
#endif

/*
 *  stress_flock
 *	stress file locking
 */
static int stress_flock(
	uint64_t *const counter,
	const uint32_t instance,
	const uint64_t max_ops,
	const char *name)
{
	int fd;
	char filename[64];

	(void)instance;

	snprintf(filename, sizeof(filename), "./%s-%i", name, getppid());
	if ((fd = open(filename, O_CREAT | O_RDWR, 0666)) < 0) {
		pr_failed_err(name, "open");
		return EXIT_FAILURE;
	}

	do {
		if (flock(fd, LOCK_EX) < 0)
			continue;
#if defined(_POSIX_PRIORITY_SCHEDULING)
		sched_yield();
#endif
		(void)flock(fd, LOCK_UN);
		(*counter)++;
	} while (opt_do_run && (!max_ops || *counter < max_ops));
	(void)unlink(filename);
	(void)close(fd);

	return EXIT_SUCCESS;
}

#if defined(__linux__)
/*
 *  stress on sched_affinity()
 *	stress system by changing CPU affinity periodically
 */
static int stress_affinity(
	uint64_t *const counter,
	const uint32_t instance,
	const uint64_t max_ops,
	const char *name)
{
	const long int cpus = sysconf(_SC_NPROCESSORS_CONF);
	unsigned long int cpu = 0;
	cpu_set_t mask;

	(void)instance;
	(void)name;

	do {
		cpu++;
		cpu %= cpus;
		CPU_ZERO(&mask);
		CPU_SET(cpu, &mask);
		sched_setaffinity(0, sizeof(mask), &mask);
		(*counter)++;
	} while (opt_do_run && (!max_ops || *counter < max_ops));

	return EXIT_SUCCESS;
}
#endif

#if defined (__linux__)
static volatile uint64_t timer_counter = 0;
static timer_t timerid;

/*
 *  stress_timer_handler()
 *	catch timer signal and cancel if no more runs flagged
 */
static void stress_timer_handler(int sig)
{
	(void)sig;
	timer_counter++;

	/* Cancel timer if we detect no more runs */
	if (!opt_do_run) {
		struct itimerspec timer;

		timer.it_value.tv_sec = 0;
		timer.it_value.tv_nsec = 0;
		timer.it_interval.tv_sec = timer.it_value.tv_sec;
		timer.it_interval.tv_nsec = timer.it_value.tv_nsec;

		timer_settime(timerid, 0, &timer, NULL);
	}
}

/*
 *  stress_timer
 *	stress timers
 */
static int stress_timer(
	uint64_t *const counter,
	const uint32_t instance,
	const uint64_t max_ops,
	const char *name)
{
	struct sigaction new_action;
	struct sigevent sev;
	struct itimerspec timer;
	const double rate_ns = opt_timer_freq ? 1000000000 / opt_timer_freq : 1000000000;

	(void)instance;

	new_action.sa_flags = 0;
	new_action.sa_handler = stress_timer_handler;
	sigemptyset(&new_action.sa_mask);
	if (sigaction(SIGRTMIN, &new_action, NULL) < 0) {
		pr_failed_err(name, "sigaction");
		return EXIT_FAILURE;
	}

	sev.sigev_notify = SIGEV_SIGNAL;
	sev.sigev_signo = SIGRTMIN;
	sev.sigev_value.sival_ptr = &timerid;
	if (timer_create(CLOCK_REALTIME, &sev, &timerid) < 0) {
		pr_failed_err(name, "timer_create");
		return EXIT_FAILURE;
	}

	timer.it_value.tv_sec = (long long int)rate_ns / 1000000000;
	timer.it_value.tv_nsec = (long long int)rate_ns % 1000000000;
	timer.it_interval.tv_sec = timer.it_value.tv_sec;
	timer.it_interval.tv_nsec = timer.it_value.tv_nsec;

	if (timer_settime(timerid, 0, &timer, NULL) < 0) {
		pr_failed_err(name, "timer_settime");
		return EXIT_FAILURE;
	}

	do {
		struct timespec req;

		req.tv_sec = 0;
		req.tv_nsec = 10000000;
		(void)nanosleep(&req, NULL);
		*counter = timer_counter;
	} while (opt_do_run && (!max_ops || timer_counter < max_ops));

	if (timer_delete(timerid) < 0) {
		pr_failed_err(name, "timer_delete");
		return EXIT_FAILURE;
	}

	return EXIT_SUCCESS;
}
#endif

/*
 *  stress_dentry_unlink()
 *	remove all dentries
 */
static void stress_dentry_unlink(uint64_t n)
{
	uint64_t i;
	const pid_t pid = getpid();

	for (i = 0; i < n; i++) {
		char path[PATH_MAX];
		uint64_t gray_code = (i >> 1) ^ i;

		snprintf(path, sizeof(path), "stress-dentry-%i-%"
			PRIu64 ".tmp", pid, gray_code);
		(void)unlink(path);
	}
	sync();
}

/*
 *  stress_dentry
 *	stress dentries
 */
static int stress_dentry(
	uint64_t *const counter,
	const uint32_t instance,
	const uint64_t max_ops,
	const char *name)
{
	const pid_t pid = getpid();

	(void)instance;

	do {
		uint64_t i, n = opt_dentries;

		for (i = 0; i < n; i++) {
			char path[PATH_MAX];
			uint64_t gray_code = (i >> 1) ^ i;
			int fd;

			snprintf(path, sizeof(path), "stress-dentry-%i-%"
				PRIu64 ".tmp", pid, gray_code);

			if ((fd = open(path, O_CREAT | O_RDWR, 0666)) < 0) {
				pr_failed_err(name, "open");
				n = i;
				break;
			}
			close(fd);

			if (!opt_do_run ||
			    (max_ops && *counter >= max_ops))
				goto abort;

			(*counter)++;
		}
		stress_dentry_unlink(n);
		if (!opt_do_run)
			break;
		sync();
	} while (opt_do_run && (!max_ops || *counter < max_ops));

abort:
	/* force unlink of all files */
	pr_dbg(stdout, "%s: removing %" PRIu64 " entries\n", name, opt_dentries);
	stress_dentry_unlink(opt_dentries);

	return EXIT_SUCCESS;
}

#if defined (__linux__)
/*
 *  stress_urandom
 *	stress reading of /dev/urandom
 */
static int stress_urandom(
	uint64_t *const counter,
	const uint32_t instance,
	const uint64_t max_ops,
	const char *name)
{
	int fd;

	(void)instance;

	if ((fd = open("/dev/urandom", O_RDONLY)) < 0) {
		pr_failed_err(name, "open");
		return EXIT_FAILURE;
	}

	do {
		char buffer[8192];

		if (read(fd, buffer, sizeof(buffer)) < 0) {
			pr_failed_err(name, "read");
			(void)close(fd);
			return EXIT_FAILURE;
		}
		(*counter)++;
	} while (opt_do_run && (!max_ops || *counter < max_ops));
	(void)close(fd);

	return EXIT_SUCCESS;
}
#endif

/*
 *  stress_sem()
 *	stress system by sem ops
 */
static int stress_semaphore(
	uint64_t *const counter,
	const uint32_t instance,
	const uint64_t max_ops,
	const char *name)
{
	(void)instance;
	(void)name;

	do {
		int i;

		for (i = 0; i < 1000; i++) {
			if (sem_wait(&sem) < 0) {
				pr_failed_dbg(name, "sem_wait");
				break;
			}
			sem_post(&sem);
			if (!opt_do_run)
				break;
		}
		(*counter)++;
	} while (opt_do_run && (!max_ops || *counter < max_ops));

	return EXIT_SUCCESS;
}

/*
 *  stress_open()
 *	stress system by rapid open/close calls
 */
static int stress_open(
	uint64_t *const counter,
	const uint32_t instance,
	const uint64_t max_ops,
	const char *name)
{
	int fds[STRESS_FD_MAX];

	(void)instance;
	(void)name;

	do {
		int i;

		for (i = 0; i < STRESS_FD_MAX; i++) {
			fds[i] = open("/dev/zero", O_RDONLY);
			if (fds[i] < 0)
				break;
			if (!opt_do_run)
				break;
			(*counter)++;
		}
		for (i = 0; i < STRESS_FD_MAX; i++) {
			if (fds[i] < 0)
				break;
			if (!opt_do_run)
				break;
			(void)close(fds[i]);
		}
	} while (opt_do_run && (!max_ops || *counter < max_ops));

	return EXIT_SUCCESS;
}

#if _POSIX_C_SOURCE >= 199309L
static void stress_sigqhandler(int dummy)
{
	(void)dummy;
}

/*
 *  stress_sigq
 *	stress by heavy sigqueue message sending
 */
static int stress_sigq(
	uint64_t *const counter,
	const uint32_t instance,
	const uint64_t max_ops,
	const char *name)
{
	pid_t pid;
	struct sigaction new_action;

	new_action.sa_handler = stress_sigqhandler;
	sigemptyset(&new_action.sa_mask);
	new_action.sa_flags = 0;
	if (sigaction(SIGUSR1, &new_action, NULL) < 0) {
		pr_failed_err(name, "sigaction");
		return EXIT_FAILURE;
	}

	pid = fork();
	if (pid < 0) {
		pr_failed_dbg(name, "fork");
		return EXIT_FAILURE;
	} else if (pid == 0) {
		sigset_t mask;

		sigemptyset(&mask);
		sigaddset(&mask, SIGUSR1);

		for (;;) {
			siginfo_t info;
			sigwaitinfo(&mask, &info);
			if (info.si_value.sival_int)
				break;
		}
		pr_dbg(stderr, "%s: child got termination notice\n", name);
		pr_dbg(stderr, "%s: exited on pid [%d] (instance %" PRIu32 ")\n",
			name, getpid(), instance);
		_exit(0);
	} else {
		/* Parent */
		union sigval s;

		do {
			memset(&s, 0, sizeof(s));
			s.sival_int = 0;
			sigqueue(pid, SIGUSR1, s);
			(*counter)++;
		} while (opt_do_run && (!max_ops || *counter < max_ops));

		pr_dbg(stderr, "%s: parent sent termination notice\n", name);
		memset(&s, 0, sizeof(s));
		s.sival_int = 1;
		sigqueue(pid, SIGUSR1, s);
		usleep(250);
		/* And ensure child is really dead */
		(void)kill(pid, SIGKILL);
	}

	return EXIT_SUCCESS;
}
#endif

/*
 *  stress_poll()
 *	stress system by rapid polling system calls
 */
static int stress_poll(
	uint64_t *const counter,
	const uint32_t instance,
	const uint64_t max_ops,
	const char *name)
{
	(void)instance;
	(void)name;

	do {
		struct timeval tv;

		(void)poll(NULL, 0, 0);
		tv.tv_sec = 0;
		tv.tv_usec = 0;
		(void)select(0, NULL, NULL, NULL, &tv);
		if (!opt_do_run)
			break;
		(void)sleep(0);
		(*counter)++;
	} while (opt_do_run && (!max_ops || *counter < max_ops));

	return EXIT_SUCCESS;
}

/*
 *  stress_link_unlink()
 *	remove all links
 */
static void stress_link_unlink(const char *funcname, const uint64_t n)
{
	uint64_t i;
	const pid_t pid = getpid();

	for (i = 0; i < n; i++) {
		char path[PATH_MAX];

		snprintf(path, sizeof(path), "stress-%s-%i-%"
			PRIu64 ".lnk", funcname, pid, i);
		(void)unlink(path);
	}
	sync();
}

/*
 *  stress_link_generic
 *	stress links, generic case
 */
static int stress_link_generic(
	uint64_t *const counter,
	const uint32_t instance,
	const uint64_t max_ops,
	const char *name,
	int (*linkfunc)(const char *oldpath, const char *newpath),
	const char *funcname)
{
	const pid_t pid = getpid();
	int fd;
	char oldpath[PATH_MAX];

	(void)instance;

	snprintf(oldpath, sizeof(oldpath), "stress-%s-%i", funcname, pid);
	if ((fd = open(oldpath, O_CREAT | O_RDWR, 0666)) < 0) {
		pr_failed_err(name, "open");
		return EXIT_FAILURE;
	}
	(void)close(fd);

	do {
		uint64_t i, n = DEFAULT_LINKS;

		for (i = 0; i < n; i++) {
			char newpath[PATH_MAX];

			snprintf(newpath, sizeof(newpath), "stress-%s-%i-%"
				PRIu64 ".lnk", funcname, pid, i);

			if (linkfunc(oldpath, newpath) < 0) {
				pr_failed_err(name, funcname);
				n = i;
				break;
			}

			if (!opt_do_run ||
			    (max_ops && *counter >= max_ops))
				goto abort;

			(*counter)++;
		}
		stress_link_unlink(funcname, n);
		if (!opt_do_run)
			break;
	} while (opt_do_run && (!max_ops || *counter < max_ops));

abort:
	/* force unlink of all files */
	pr_dbg(stdout, "%s: removing %" PRIu32" entries\n", name, DEFAULT_LINKS);
	stress_link_unlink(funcname, DEFAULT_LINKS);
	(void)unlink(oldpath);

	return EXIT_SUCCESS;
}

/*
 *  stress_link
 *	stress hard links
 */
static int stress_link(
	uint64_t *const counter,
	const uint32_t instance,
	const uint64_t max_ops,
	const char *name)
{
	return stress_link_generic(counter, instance,
		max_ops, name, link, "link");
}

/*
 *  stress_symlink
 *	stress symbolic links
 */
static int stress_symlink(
	uint64_t *const counter,
	const uint32_t instance,
	const uint64_t max_ops,
	const char *name)
{
	return stress_link_generic(counter, instance,
		max_ops, name, symlink, "symlink");
}

/*
 *  stress_dir_tidy()
 *	remove all dentries
 */
static void stress_dir_tidy(const uint64_t n)
{
	uint64_t i;
	const pid_t pid = getpid();

	for (i = 0; i < n; i++) {
		char path[PATH_MAX];
		uint64_t gray_code = (i >> 1) ^ i;

		snprintf(path, sizeof(path), "stress-dir-%i-%"
			PRIu64, pid, gray_code);
		(void)rmdir(path);
	}
}

/*
 *  stress_dir
 *	stress directory mkdir and rmdir
 */
static int stress_dir(
	uint64_t *const counter,
	const uint32_t instance,
	const uint64_t max_ops,
	const char *name)
{
	const pid_t pid = getpid();

	(void)instance;

	do {
		uint64_t i, n = DEFAULT_DIRS;

		for (i = 0; i < n; i++) {
			char path[PATH_MAX];
			uint64_t gray_code = (i >> 1) ^ i;

			snprintf(path, sizeof(path), "stress-dir-%i-%"
				PRIu64, pid, gray_code);
			if (mkdir(path, 0666) < 0) {
				pr_failed_err(name, "mkdir");
				n = i;
				break;
			}

			if (!opt_do_run ||
			    (max_ops && *counter >= max_ops))
				goto abort;

			(*counter)++;
		}
		stress_dir_tidy(n);
		if (!opt_do_run)
			break;
		sync();
	} while (opt_do_run && (!max_ops || *counter < max_ops));

abort:
	/* force unlink of all files */
	pr_dbg(stdout, "%s: removing %" PRIu32 " directories\n", name, DEFAULT_DIRS);
	stress_dir_tidy(DEFAULT_DIRS);

	return EXIT_SUCCESS;
}

static sigjmp_buf jmp_env;

/*
 *  stress_segvhandler()
 *	SEGV handler
 */
static void stress_segvhandler(int dummy)
{
	(void)dummy;

	siglongjmp(jmp_env, 1);		/* Ugly, bounce back */
}

/*
 *  stress_sigsegv
 *	stress by generating segmentation faults
 */
static int stress_sigsegv(
	uint64_t *const counter,
	const uint32_t instance,
	const uint64_t max_ops,
	const char *name)
{
	uint8_t *ptr = NULL;

	(void)instance;

	for (;;) {
		struct sigaction new_action;
		int ret;

		memset(&new_action, 0, sizeof new_action);
		new_action.sa_handler = stress_segvhandler;
		sigemptyset(&new_action.sa_mask);
		new_action.sa_flags = 0;

		if (sigaction(SIGSEGV, &new_action, NULL) < 0) {
			pr_failed_err(name, "sigaction");
			return EXIT_FAILURE;
		}
		ret = sigsetjmp(jmp_env, 1);
		/*
		 * We return here if we segfault, so
		 * first check if we need to terminate
		 */
		if (!opt_do_run || (max_ops && *counter >= max_ops))
			break;

		if (ret)
			(*counter)++;	/* SEGV occurred */
		else
			*ptr = 0;	/* Trip a SEGV */
	}

	return EXIT_SUCCESS;
}

/*
 *  stress_mmap()
 *	stress mmap
 */
static int stress_mmap(
	uint64_t *const counter,
	const uint32_t instance,
	const uint64_t max_ops,
	const char *name)
{
	uint8_t *buf = NULL;
#ifdef _SC_PAGESIZE
	const long page_size = sysconf(_SC_PAGESIZE);
#else
	const long page_size = PAGE_4K;
#endif
	const size_t sz = opt_mmap_bytes & ~(page_size - 1);
	const size_t pages4k = sz / page_size;
	int flags = MAP_PRIVATE | MAP_ANONYMOUS;

#ifdef MAP_POPULATE
	flags |= MAP_POPULATE;
#endif
	(void)instance;

	do {
		uint8_t mapped[pages4k];
		uint8_t *mappings[pages4k];
		size_t n;

		buf = mmap(NULL, sz, PROT_READ | PROT_WRITE, flags, -1, 0);
		if (buf == MAP_FAILED) {
			/* Force MAP_POPULATE off, just in case */
			flags &= ~MAP_POPULATE;
			pr_failed_dbg(name, "mmap");
			continue;	/* Try again */
		}
		memset(mapped, PAGE_MAPPED, sizeof(mapped));
		for (n = 0; n < pages4k; n++)
			mappings[n] = buf + (n * page_size);

		/* Ensure we can write to the mapped pages */
		memset(buf, 0xff, sz);

		/*
		 *  Step #1, unmap all pages in random order
		 */
		for (n = pages4k; n; ) {
			uint64_t j, i = mwc() % pages4k;
			for (j = 0; j < n; j++) {
				uint64_t page = (i + j) % pages4k;
				if (mapped[page] == PAGE_MAPPED) {
					mapped[page] = 0;
					munmap(mappings[page], page_size);
					n--;
					break;
				}
				if (!opt_do_run)
					goto cleanup;
			}
		}

#ifdef MAP_FIXED
		/*
		 *  Step #2, map them back in random order
		 */
		for (n = pages4k; n; ) {
			uint64_t j, i = mwc() % pages4k;
			for (j = 0; j < n; j++) {
				uint64_t page = (i + j) % pages4k;
				if (!mapped[page]) {
					/*
					 * Attempt to map them back into the original address, this
					 * may fail (it's not the most portable operation), so keep
					 * track of failed mappings too
					 */
					mappings[page] = mmap(mappings[page], page_size, PROT_READ | PROT_WRITE, MAP_FIXED | flags, -1, 0);
					if (mappings[page] == MAP_FAILED) {
						mapped[page] = PAGE_MAPPED_FAIL;
						mappings[page] = NULL;
					} else {
						mapped[page] = PAGE_MAPPED;
						/* Ensure we can write to the mapped page */
						memset(mappings[page], 0xff, page_size);
					}
					n--;
					break;
				}
				if (!opt_do_run)
					goto cleanup;
			}
		}
#endif
cleanup:
		/*
		 *  Step #3, unmap them all
		 */
		for (n = 0; n < pages4k; n++) {
			if (mapped[n] & PAGE_MAPPED)
				munmap(mappings[n], page_size);
		}
		(*counter)++;
	} while (opt_do_run && (!max_ops || *counter < max_ops));

	return EXIT_SUCCESS;
}

static int stress_qsort_cmp_1(const void *p1, const void *p2)
{
	int32_t *i1 = (int32_t *)p1;
	int32_t *i2 = (int32_t *)p2;

	return *i1 - *i2;
}

static int stress_qsort_cmp_2(const void *p1, const void *p2)
{
	int32_t *i1 = (int32_t *)p1;
	int32_t *i2 = (int32_t *)p2;

	return *i2 - *i1;
}

static int stress_qsort_cmp_3(const void *p1, const void *p2)
{
	int8_t *i1 = (int8_t *)p1;
	int8_t *i2 = (int8_t *)p2;

	/* Force byte-wise re-ordering */
	return *i1 - (*i2 ^ *i1);
}

/*
 *  stress_qsort()
 *	stress qsort
 */
static int stress_qsort(
	uint64_t *const counter,
	const uint32_t instance,
	const uint64_t max_ops,
	const char *name)
{
	int32_t *data, *ptr;
	const size_t n = (size_t)opt_qsort_size;
	size_t i;

	(void)instance;
	if ((data = malloc(sizeof(int32_t) * n)) == NULL) {
		pr_failed_dbg(name, "malloc");
		return EXIT_FAILURE;
	}

	/* This is expensive, do it once */
	for (ptr = data, i = 0; i < n; i++)
		*ptr++ = mwc();

	do {
		/* Sort "random" data */
		qsort(data, n, sizeof(uint32_t), stress_qsort_cmp_1);
		if (!opt_do_run)
			break;
		/* Reverse sort */
		qsort(data, n, sizeof(uint32_t), stress_qsort_cmp_2);
		if (!opt_do_run)
			break;
		/* Reverse this again */
		qsort(data, n, sizeof(uint32_t), stress_qsort_cmp_1);
		if (!opt_do_run)
			break;
		/* And re-order by byte compare */
		qsort(data, n * 4, sizeof(uint8_t), stress_qsort_cmp_3);

		(*counter)++;
	} while (opt_do_run && (!max_ops || *counter < max_ops));

	free(data);

	return EXIT_SUCCESS;
}

/*
 *  stress_bigheap()
 *	stress that does nowt
 */
static int stress_bigheap(
	uint64_t *const counter,
	const uint32_t instance,
	const uint64_t max_ops,
	const char *name)
{
	void *ptr = NULL, *last_ptr = NULL;
	uint8_t *last_ptr_end = NULL;
	size_t size = 0;
	size_t chunk_size = 16 * 4096;
	size_t stride = 4096;
	pid_t pid;
	uint32_t restarts = 0, nomems = 0;

again:
	pid = fork();
	if (pid < 0) {
		pr_err(stderr, "%s: fork failed: errno=%d: (%s)\n",
			name, errno, strerror(errno));
	} else if (pid > 0) {
		int status, ret;
		/* Parent, wait for child */
		ret = waitpid(pid, &status, 0);
		if (ret < 0) {
			pr_dbg(stderr, "%s: waitpid(): errno=%d (%s)\n", name, errno, strerror(errno));
			(void)kill(pid, SIGTERM);
			(void)kill(pid, SIGKILL);
		}
		if (WIFSIGNALED(status)) {
			pr_dbg(stderr, "%s: child died: %d (instance %d)\n",
				name, WTERMSIG(status), instance);
			/* If we got killed by OOM killer, re-start */
			if (WTERMSIG(status) == SIGKILL) {
				pr_dbg(stderr, "%s: assuming killed by OOM killer, "
					"restarting again (instance %d)\n",
					name, instance);
				restarts++;
				goto again;
			}
		}
	} else if (pid == 0) {
		/* Make sure this is killable by OOM killer */
		set_oom_adjustment(name, true);

		do {
			void *old_ptr = ptr;
			size += chunk_size;

			ptr = realloc(old_ptr, size);
			if (ptr == NULL) {
				pr_dbg(stderr, "%s: out of memory at %" PRIu64 " MB (instance %d)\n",
					name, (uint64_t)(4096ULL * size) >> 20, instance);
				free(old_ptr);
				size = 0;
				nomems++;
			} else {
				size_t i, n;
				uint8_t *u8ptr;

				if (last_ptr == ptr) {
					u8ptr = last_ptr_end;
					n = chunk_size;
				} else {
					u8ptr = ptr;
					n = size;
				}
				for (i = 0; i < n; i+= stride, u8ptr += stride)
					*u8ptr = 0xff;

				last_ptr = ptr;
				last_ptr_end = u8ptr;
			}
			(*counter)++;
		} while (opt_do_run && (!max_ops || *counter < max_ops));
		free(ptr);
	}
	pr_dbg(stderr, "%s: OOM restarts: %" PRIu32 ", out of memory restarts: %" PRIu32 ".\n",
			name, restarts, nomems);

	return EXIT_SUCCESS;
}

/*
 *  stress_rename()
 *	stress system by renames
 */
static int stress_rename(
	uint64_t *const counter,
	const uint32_t instance,
	const uint64_t max_ops,
	const char *name)
{
	char name1[PATH_MAX], name2[PATH_MAX];
	char *oldname = name1, *newname = name2, *tmpname;
	FILE *fp;
	uint32_t i = 0;

restart:
	snprintf(oldname, PATH_MAX, "./%s-%" PRIu32 "-%" PRIu32,
		name, instance, i++);

	if ((fp = fopen(oldname, "w+")) == NULL) {
		pr_err(stderr, "%s: fopen failed: errno=%d: (%s)\n",
			name, errno, strerror(errno));
		exit(EXIT_FAILURE);
	}
	(void)fclose(fp);

	do {
		snprintf(newname, PATH_MAX, "./%s-%" PRIu32 "-%" PRIu32,
			name, instance, i++);
		if (rename(oldname, newname) < 0) {
			(void)unlink(oldname);
			(void)unlink(newname);
			goto restart;
		}

		tmpname = oldname;
		oldname = newname;
		newname = tmpname;

		(*counter)++;
	} while (opt_do_run && (!max_ops || *counter < max_ops));

	(void)unlink(oldname);
	(void)unlink(newname);

	return EXIT_SUCCESS;
}

/*
 *  stress_fstat()
 *	stress system with fstat
 */
static int stress_fstat(
	uint64_t *const counter,
	const uint32_t instance,
	const uint64_t max_ops,
	const char *name)
{
	typedef struct dir_info {
		char	*path;
		struct dir_info *next;
	} dir_info_t;

	DIR *dp;
	dir_info_t *dir_info = NULL, *di;
	struct dirent *d;

	(void)instance;

	if ((dp = opendir(opt_fstat_dir)) == NULL) {
		pr_err(stderr, "%s: opendir on %s failed: errno=%d: (%s)\n",
			name, opt_fstat_dir, errno, strerror(errno));
		exit(EXIT_FAILURE);
	}

	/* Cache all the directory entries */
	while ((d = readdir(dp)) != NULL) {
		char path[PATH_MAX];

		snprintf(path, sizeof(path), "%s/%s", opt_fstat_dir, d->d_name);
		if ((di = calloc(1, sizeof(*di))) == NULL) {
			pr_err(stderr, "%s: out of memory\n", name);
			closedir(dp);
			exit(EXIT_FAILURE);
		}
		if ((di->path = strdup(path)) == NULL) {
			pr_err(stderr, "%s: out of memory\n", name);
			closedir(dp);
			exit(EXIT_FAILURE);
		}
		di->next = dir_info;
		dir_info = di;
	}
	closedir(dp);

	do {
		struct stat buf;

		for (di = dir_info; di; di = di->next) {
			/* We don't care about it failing */
			(void)stat(di->path, &buf);
			(*counter)++;
			if (!opt_do_run || (max_ops && *counter >= max_ops))
				break;
		}
	} while (opt_do_run && (!max_ops || *counter < max_ops));

	/* Free cache */
	for (di = dir_info; di; ) {
		dir_info_t *next = di->next;

		free(di->path);
		free(di);
		di = next;
	}

	return EXIT_SUCCESS;
}

#if _XOPEN_SOURCE >= 700 || _POSIX_C_SOURCE >= 200809L
/*
 *  stress_utime()
 *	stress system by setting file utime
 */
static int stress_utime(
	uint64_t *const counter,
	const uint32_t instance,
	const uint64_t max_ops,
	const char *name)
{
	char filename[PATH_MAX];
	int fd;

	snprintf(filename, sizeof(filename), "./%s-%" PRIu32,
		name, instance);

	if ((fd = open(filename, O_WRONLY | O_CREAT, 0666)) < 0) {
		pr_err(stderr, "%s: open failed: errno=%d: (%s)\n",
			name, errno, strerror(errno));
		exit(EXIT_FAILURE);
	}

	do {
		if (futimens(fd, NULL) < 0) {
			pr_dbg(stderr, "%s: futimens failed: errno=%d: (%s)\n",
				name, errno, strerror(errno));
			break;
		}
		/* forces metadata writeback */
		if (opt_flags & OPT_FLAGS_UTIME_FSYNC)
			(void)fsync(fd);
		(*counter)++;
	} while (opt_do_run && (!max_ops || *counter < max_ops));

	(void)close(fd);
	(void)unlink(filename);

	return EXIT_SUCCESS;
}
#endif


/*
 *  stress_noop()
 *	stress that does nowt
 */
static int stress_noop(
	uint64_t *const counter,
	const uint32_t instance,
	const uint64_t max_ops,
	const char *name)
{
	(void)counter;
	(void)instance;
	(void)max_ops;
	(void)name;

	return EXIT_SUCCESS;
}

/* Human readable stress test names */
static const stress_t stressors[] = {
#if defined(__linux__)
	{ stress_affinity, STRESS_AFFINITY, OPT_AFFINITY, OPT_AFFINITY_OPS,	"affinity" },
#endif
	{ stress_bigheap,STRESS_BIGHEAP,OPT_BIGHEAP,	OPT_BIGHEAP_OPS,	"bigheap" },
	{ stress_cache,  STRESS_CACHE,	OPT_CACHE,	OPT_CACHE_OPS,  	"cache" },
	{ stress_cpu,	 STRESS_CPU,	OPT_CPU,	OPT_CPU_OPS,		"cpu" },
	{ stress_ctxt,	 STRESS_CTXT,	OPT_CTXT,	OPT_CTXT_OPS,   	"ctxt" },
	{ stress_dentry, STRESS_DENTRY, OPT_DENTRY,	OPT_DENTRY_OPS,		"dentry" },
	{ stress_dir,	 STRESS_DIR,	OPT_DIR,	OPT_DIR_OPS,		"dir" },
#if _XOPEN_SOURCE >= 600 || _POSIX_C_SOURCE >= 200112L
	{ stress_fallocate, STRESS_FALLOCATE, OPT_FALLOCATE, OPT_FALLOCATE_OPS,	"fallocate" },
#endif
	{ stress_flock,	 STRESS_FLOCK,	OPT_FLOCK,	OPT_FLOCK_OPS,		"flock" },
	{ stress_fork,	 STRESS_FORK,	OPT_FORK,	OPT_FORK_OPS,   	"fork" },
	{ stress_fstat,	 STRESS_FSTAT,	OPT_FSTAT,	OPT_FSTAT_OPS,		"fstat" },
	{ stress_hdd,	 STRESS_HDD,	OPT_HDD,	OPT_HDD_OPS,		"hdd" },
	{ stress_iosync, STRESS_IOSYNC,	OPT_IOSYNC,	OPT_IOSYNC_OPS, 	"iosync" },
	{ stress_link,	 STRESS_LINK,	OPT_LINK,	OPT_LINK_OPS,		"link" },
	{ stress_mmap,	 STRESS_MMAP,	OPT_MMAP,	OPT_MMAP_OPS,		"mmap" },
	{ stress_open,	 STRESS_OPEN,	OPT_OPEN,  	OPT_OPEN_OPS,		"open" },
	{ stress_pipe,	 STRESS_PIPE,	OPT_PIPE,	OPT_PIPE_OPS,   	"pipe" },
	{ stress_poll,	 STRESS_POLL,	OPT_POLL,	OPT_POLL_OPS,		"poll" },
	{ stress_qsort,	 STRESS_QSORT,	OPT_QSORT,	OPT_QSORT_OPS,		"qsort" },
	{ stress_rename, STRESS_RENAME, OPT_RENAME,	OPT_RENAME_OPS, 	"rename" },
	{ stress_semaphore, STRESS_SEMAPHORE, OPT_SEMAPHORE, OPT_SEMAPHORE_OPS, "semaphore" },
#if  _POSIX_C_SOURCE >= 199309L
	{ stress_sigq,	 STRESS_SIGQUEUE,OPT_SIGQUEUE, OPT_SIGQUEUE_OPS,	"sigq" },
#endif
	{ stress_sigsegv,STRESS_SIGSEGV,OPT_SIGSEGV,	OPT_SIGSEGV_OPS,	"sigsegv" },
	{ stress_socket, STRESS_SOCKET, OPT_SOCKET,	OPT_SOCKET_OPS, 	"socket" },
	{ stress_symlink,STRESS_SYMLINK,OPT_SYMLINK,	OPT_SYMLINK_OPS,	"symlink" },
#if defined(__linux__)
	{ stress_timer,	 STRESS_TIMER,	OPT_TIMER,	OPT_TIMER_OPS,		"timer" },
#endif
#if defined(__linux__)
	{ stress_urandom,STRESS_URANDOM,OPT_URANDOM,	OPT_URANDOM_OPS,	"urandom" },
#endif
#if _XOPEN_SOURCE >= 700 || _POSIX_C_SOURCE >= 200809L
	{ stress_utime,	 STRESS_UTIME,	OPT_UTIME,	OPT_UTIME_OPS,		"utime" },
#endif
	{ stress_vm,	 STRESS_VM,	OPT_VM,		OPT_VM_OPS,		"vm" },
#if defined (_POSIX_PRIORITY_SCHEDULING)
	{ stress_yield,	 STRESS_YIELD,	OPT_YIELD,	OPT_YIELD_OPS,  	"yield" },
#endif
	/* Add new stress tests here */
	{ stress_noop,	STRESS_MAX,	0,		0,			NULL },
};

/*
 *  stress_func
 *	return stress test based on a given stress test id
 */
static inline int stress_info_index(const stress_id id)
{
	unsigned int i;

	for (i = 0; stressors[i].name; i++)
		if (stressors[i].id == id)
			break;

	return i;	/* End of array is a special "NULL" entry */
}

/*
 *  version()
 *	print program version info
 */
static void version(void)
{
	printf("%s, version " VERSION "\n", app_name);
}

static const help_t help[] = {
	{ "?,-h",	"help",			"show help" },
#if defined (__linux__)
	{ NULL,		"affinity N",		"start N workers that rapidly change CPU affinity" },
	{ NULL, 	"affinity-ops N",   	"stop when N affinity bogo operations completed" },
#endif
	{ "a N",	"all N",		"start N workers of each stress test" },
	{ "b N",	"backoff N",		"wait of N microseconds before work starts" },
	{ "B N",	"bigheap N",		"start N workers that grow the heap using calloc()" },
	{ NULL,		"bigheap-ops N",	"stop when N bogo bigheap operations completed" },
	{ "c N",	"cpu N",		"start N workers spinning on sqrt(rand())" },
	{ "l P",	"cpu-load P",		"load CPU by P %%, 0=sleep, 100=full load (see -c)" },
	{ NULL,		"cpu-ops N",		"stop when N cpu bogo operations completed" },
	{ NULL,		"cpu-method m",		"specify stress cpu method m, default is sqrt(rand())" },
	{ "C N",	"cache N",		"start N CPU cache thrashing workers" },
	{ NULL,		"cache-ops N",		"stop when N cache bogo operations completed" },
	{ "D N",	"dentry N",		"start N dentry thrashing processes" },
	{ NULL,		"dentry-ops N",		"stop when N dentry bogo operations completed" },
	{ NULL,		"dentries N",		"create N dentries per iteration" },
	{ NULL,		"dir N",		"start N directory thrashing processes" },
	{ NULL,		"dir-ops N",		"stop when N directory bogo operations completed" },
	{ "d N",	"hdd N",		"start N workers spinning on write()/unlink()" },
	{ NULL,		"hdd-bytes N",		"write N bytes per hdd worker (default is 1GB)" },
	{ NULL,		"hdd-noclean",		"do not unlink files created by hdd workers" },
	{ NULL,		"hdd-ops N",		"stop when N hdd bogo operations completed" },
	{ NULL,		"hdd-write-size N",	"set the default write size to N bytes" },
#if _XOPEN_SOURCE >= 600 || _POSIX_C_SOURCE >= 200112L
	{ NULL,		"fallocate N",		"start N workers fallocating 16MB files" },
	{ NULL,		"fallocate-ops N",	"stop when N fallocate bogo operations completed" },
#endif
	{ NULL,		"flock N",		"start N workers locking a single file" },
	{ NULL,		"flock-ops N",		"stop when N flock bogo operations completed" },
	{ "f N",	"fork N",		"start N workers spinning on fork() and exit()" },
	{ NULL,		"fork-ops N",		"stop when N fork bogo operations completed" },
	{ NULL,		"fstat N",		"start N workers exercising fstat on files" },
	{ NULL,		"fstat-ops N",		"stop when N fstat bogo operations completed" },
	{ NULL,		"fstat-dir path",	"fstat files in the specified directory" },
	{ "i N",	"io N",			"start N workers spinning on sync()" },
	{ NULL,		"io-ops N",		"stop when N io bogo operations completed" },
#if defined (__linux__)
	{ NULL,		"ionice-class C",	"specify ionice class (idle, besteffort, realtime)" },
	{ NULL,		"ionice-level L",	"specify ionice level (0 max, 7 min)" },
#endif
	{ "k",		"keep-name",		"keep stress process names to be 'stress-ng'" },
	{ NULL,		"link N",		"start N workers creating hard links" },
	{ NULL,		"link-ops N",		"stop when N link bogo operations completed" },
	{ NULL,		"mmap N",		"start N workers stressing mmap and munmap" },
	{ NULL,		"mmap-ops N",		"stop when N mmap bogo operations completed" },
	{ NULL,		"mmap-bytes N",		"mmap and munmap N bytes for each stress iteration" },
	{ "M",		"metrics",		"print pseudo metrics of activity" },
	{ "m N",	"vm N",			"start N workers spinning on anonymous mmap" },
	{ NULL,		"vm-bytes N",		"allocate N bytes per vm worker (default 256MB)" },
	{ NULL,		"vm-stride N",		"touch a byte every N bytes (default 4K)" },
	{ NULL,		"vm-hang N",		"sleep N seconds before freeing memory" },
	{ NULL,		"vm-keep",		"redirty memory instead of reallocating" },
	{ NULL,		"vm-ops N",		"stop when N vm bogo operations completed" },
#ifdef MAP_LOCKED
	{ NULL,		"vm-locked",		"lock the pages of the mapped region into memory" },
#endif
#ifdef MAP_POPULATE
	{ NULL,		"vm-populate",		"populate (prefault) page tables for a mapping" },
#endif
	{ "n",		"dry-run",		"do not run" },
	{ "o",		"open N",		"start N workers exercising open/close" },
	{ NULL,		"open-ops N",		"stop when N open/close bogo operations completed" },
	{ "p N",	"pipe N",		"start N workers exercising pipe I/O" },
	{ NULL,		"pipe-ops N",		"stop when N pipe I/O bogo operations completed" },
	{ "P N",	"poll N",		"start N workers exercising zero timeout polling" },
	{ NULL,		"poll-ops N",		"stop when N poll bogo operations completed" },
	{ "Q",		"qsort N",		"start N workers exercising qsort on 32 bit random integers" },
	{ NULL,		"qsort-ops N",		"stop when N qsort bogo operations completed" },
	{ NULL,		"qsort-size N",		"number of 32 bit integers to sort" },
	{ "q",		"quiet",		"quiet output" },
	{ "r",		"random N",		"start N random workers" },
	{ "R",		"rename N",		"start N workers exercising file renames" },
	{ NULL,		"rename-ops N",		"stop when N rename bogo operations completed" },
#if defined (__linux__)
	{ NULL,		"sched type",		"set scheduler type" },
	{ NULL,		"sched-prio N",		"set scheduler priority level N" },
#endif
	{ NULL,		"sem N",		"start N workers doing semaphore operations" },
	{ NULL,		"sem-ops N",		"stop when N semaphore bogo operations completed" },
#if _POSIX_C_SOURCE >= 199309L
	{ NULL,		"sigq N",		"start N workers sending sigqueue signals" },
	{ NULL,		"sigq-ops N",		"stop when N siqqueue bogo operations completed" },
#endif
	{ NULL,		"sigsegv N",		"start N workers generating segmentation faults" },
	{ NULL,		"sigsegv-ops N",	"stop when N bogo segmentation faults completed" },
	{ "s N",	"switch N",		"start N workers doing rapid context switches" },
	{ NULL,		"switch-ops N",		"stop when N context switch bogo operations completed" },
	{ "S N",	"sock N",		"start N workers doing socket activity" },
	{ NULL,		"sock-ops N",		"stop when N socket bogo operations completed" },
	{ NULL,		"sock-port P",		"use socket ports P to P + number of workers - 1" },
	{ NULL,		"symlink N",		"start N workers creating symbolic links" },
	{ NULL,		"symlink-ops N",	"stop when N symbolic link bogo operations completed" },
	{ "t N",	"timeout N",		"timeout after N seconds" },
#if defined (__linux__)
	{ "T N",	"timer N",		"start N workers producing timer events" },
	{ NULL,		"timer-ops N",		"stop when N timer bogo events completed" },
	{ NULL,		"timer-freq F",		"run timer(s) at F Hz, range 1000 to 1000000000" },
	{ "u N",	"urandom N",		"start N workers reading /dev/urandom" },
	{ NULL,		"urandom-ops N",	"stop when N urandom bogo read operations completed" },
#endif
#if _XOPEN_SOURCE >= 700 || _POSIX_C_SOURCE >= 200809L
	{ NULL,		"utime N",		"start N workers updating file timestamps" },
	{ NULL,		"utime-ops N",		"stop after N utime bogo operations completed" },
	{ NULL,		"utime-fsync",		"force utime meta data sync to the file system" },
#endif
	{ "v",		"verbose",		"verbose output" },
	{ "V",		"version",		"show version" },
#if defined(_POSIX_PRIORITY_SCHEDULING)
	{ "y N",	"yield N",		"start N workers doing sched_yield() calls" },
	{ NULL,		"yield-ops N",		"stop when N bogo yield operations completed" },
#endif
	{ NULL,		NULL,			NULL }
};

/*
 *  usage()
 *	print some help
 */
static void usage(void)
{
	int i;

	version();
	printf(	"\nUsage: %s [OPTION [ARG]]\n", app_name);
	for (i = 0; help[i].description; i++) {
		char opt_s[10] = "";

		if (help[i].opt_s)
			snprintf(opt_s, sizeof(opt_s), "-%s,", help[i].opt_s);
		printf(" %-6s--%-17s%s\n", opt_s, help[i].opt_l, help[i].description);
	}
	printf("\nExample %s --cpu 8 --io 4 --vm 2 --vm-bytes 128M --fork 4 --timeout 10s\n\n"
	       "Note: Sizes can be suffixed with B,K,M,G and times with s,m,h,d,y\n", app_name);
	exit(EXIT_SUCCESS);
}

static const struct option long_options[] = {
	{ "help",	0,	0,	OPT_QUERY },
	{ "version",	0,	0,	OPT_VERSION },
	{ "verbose",	0,	0,	OPT_VERBOSE },
	{ "quiet",	0,	0,	OPT_QUIET },
	{ "dry-run",	0,	0,	OPT_DRY_RUN },
	{ "timeout",	1,	0,	OPT_TIMEOUT },
	{ "backoff",	1,	0,	OPT_BACKOFF },
	{ "cpu",	1,	0,	OPT_CPU },
	{ "cpu-ops",	1,	0,	OPT_CPU_OPS },
	{ "cpu-load",	1,	0,	OPT_CPU_LOAD },
	{ "cpu-method",	1,	0,	OPT_CPU_METHOD },
	{ "io",		1,	0,	OPT_IOSYNC },
	{ "vm",		1,	0,	OPT_VM },
	{ "fork",	1,	0,	OPT_FORK },
	{ "switch",	1,	0,	OPT_CTXT },
	{ "vm-bytes",	1,	0,	OPT_VM_BYTES },
	{ "vm-stride",	1,	0,	OPT_VM_STRIDE },
	{ "vm-hang",	1,	0,	OPT_VM_HANG },
	{ "vm-keep",	0,	0,	OPT_VM_KEEP },
#ifdef MAP_POPULATE
	{ "vm-populate",0,	0,	OPT_VM_MMAP_POPULATE },
#endif
#ifdef MAP_LOCKED
	{ "vm-locked",	0,	0,	OPT_VM_MMAP_LOCKED },
#endif
	{ "hdd",	1,	0,	OPT_HDD },
	{ "hdd-bytes",	1,	0,	OPT_HDD_BYTES },
	{ "hdd-noclean",0,	0,	OPT_HDD_NOCLEAN },
	{ "hdd-write-size", 1,	0,	OPT_HDD_WRITE_SIZE },
	{ "metrics",	0,	0,	OPT_METRICS },
	{ "io-ops",	1,	0,	OPT_IOSYNC_OPS },
	{ "vm-ops",	1,	0,	OPT_VM_OPS },
	{ "hdd-ops",	1,	0,	OPT_HDD_OPS },
	{ "fork-ops",	1,	0,	OPT_FORK_OPS },
	{ "switch-ops",	1,	0,	OPT_CTXT_OPS },
	{ "pipe",	1,	0,	OPT_PIPE },
	{ "pipe-ops",	1,	0,	OPT_PIPE_OPS },
	{ "cache",	1,	0, 	OPT_CACHE },
	{ "cache-ops",	1,	0,	OPT_CACHE_OPS },
#if _POSIX_C_SOURCE >= 199309L
	{ "sigq",	1,	0,	OPT_SIGQUEUE },
	{ "sigq-ops",	1,	0,	OPT_SIGQUEUE_OPS },
#endif
	{ "sock",	1,	0,	OPT_SOCKET },
	{ "sock-ops",	1,	0,	OPT_SOCKET_OPS },
	{ "sock-port",	1,	0,	OPT_SOCKET_PORT },
	{ "all",	1,	0,	OPT_ALL },
#if defined (__linux__)
	{ "sched",	1,	0,	OPT_SCHED },
	{ "sched-prio",	1,	0,	OPT_SCHED_PRIO },
	{ "ionice-class",1,	0,	OPT_IONICE_CLASS },
	{ "ionice-level",1,	0,	OPT_IONICE_LEVEL },
	{ "affinity",	1,	0,	OPT_AFFINITY },
	{ "affinity-ops",1,	0,	OPT_AFFINITY_OPS },
	{ "timer",	1,	0,	OPT_TIMER },
	{ "timer-ops",	1,	0,	OPT_TIMER_OPS },
	{ "timer-freq",	1,	0,	OPT_TIMER_FREQ },
	{ "urandom",	1,	0,	OPT_URANDOM },
	{ "urandom-ops",1,	0,	OPT_URANDOM_OPS },
#endif
#if defined (_POSIX_PRIORITY_SCHEDULING)
	{ "yield",	1,	0,	OPT_YIELD },
	{ "yield-ops",	1,	0,	OPT_YIELD_OPS },
#endif
#if _XOPEN_SOURCE >= 600 || _POSIX_C_SOURCE >= 200112L
	{ "fallocate",	1,	0,	OPT_FALLOCATE },
	{ "fallocate-ops",1,	0,	OPT_FALLOCATE_OPS },
#endif
	{ "flock",	1,	0,	OPT_FLOCK },
	{ "flock-ops",	1,	0,	OPT_FLOCK_OPS },
	{ "dentry",	1,	0,	OPT_DENTRY },
	{ "dentry-ops",	1,	0,	OPT_DENTRY_OPS },
	{ "dentries",	1,	0,	OPT_DENTRIES },
	{ "sem",	1,	0,	OPT_SEMAPHORE },
	{ "sem-ops",	1,	0,	OPT_SEMAPHORE_OPS },
	{ "open",	1,	0,	OPT_OPEN },
	{ "open-ops",	1,	0,	OPT_OPEN_OPS },
	{ "random",	1,	0,	OPT_RANDOM },
	{ "keep-name",	0,	0,	OPT_KEEP_NAME },
	{ "poll",	1,	0,	OPT_POLL },
	{ "poll-ops",	1,	0,	OPT_POLL_OPS },
	{ "link",	1,	0,	OPT_LINK },
	{ "link-ops",	1,	0,	OPT_LINK_OPS },
	{ "symlink",	1,	0,	OPT_SYMLINK },
	{ "symlink-ops",1,	0,	OPT_SYMLINK_OPS },
	{ "dir",	1,	0,	OPT_DIR },
	{ "dir-ops",	1,	0,	OPT_DIR_OPS },
	{ "sigsegv",	1,	0,	OPT_SIGSEGV },
	{ "sigsegv-ops",1,	0,	OPT_SIGSEGV_OPS },
	{ "mmap",	1,	0,	OPT_MMAP },
	{ "mmap-ops",	1,	0,	OPT_MMAP_OPS },
	{ "mmap-bytes",	1,	0,	OPT_MMAP_BYTES },
	{ "qsort",	1,	0,	OPT_QSORT },
	{ "qsort-ops",	1,	0,	OPT_QSORT_OPS },
	{ "qsort-size",	1,	0,	OPT_QSORT_INTEGERS },
	{ "bigheap",	1,	0,	OPT_BIGHEAP },
	{ "bigheap-ops",1,	0,	OPT_BIGHEAP_OPS },
	{ "rename",	1,	0,	OPT_RENAME },
	{ "rename-ops",	1,	0,	OPT_RENAME_OPS },
#if _XOPEN_SOURCE >= 700 || _POSIX_C_SOURCE >= 200809L
	{ "utime",	1,	0,	OPT_UTIME },
	{ "utime-ops",	1,	0,	OPT_UTIME_OPS },
	{ "utime-fsync",0,	0,	OPT_UTIME_FSYNC },
#endif
	{ "fstat",	1,	0,	OPT_FSTAT },
	{ "fstat-ops",	1,	0,	OPT_FSTAT_OPS },
	{ "fstat-dir",	1,	0,	OPT_FSTAT_DIR },
	{ NULL,		0, 	0, 	0 }
};

static const char *opt_name(int opt_val)
{
	int i;

	for (i = 0; long_options[i].name; i++)
		if (long_options[i].val == opt_val)
			return long_options[i].name;

	return "<unknown>";
}

/*
 *  kill_procs()
 * 	kill tasks using signal
 */
static void kill_procs(int sig)
{
	static int count = 0;
	int i;

	/* multiple calls will always fallback to SIGKILL */
	count++;
	if (count > 5)
		sig = SIGKILL;

	for (i = 0; i < STRESS_MAX; i++) {
		int j;
		for (j = 0; j < started_procs[i]; j++) {
			if (procs[i][j].pid)
				(void)kill(procs[i][j].pid, sig);
		}
	}
}

/*
 *  handle_sigint()
 *	catch SIGINT
 */
static void handle_sigint(int dummy)
{
	(void)dummy;
	opt_do_run = false;
	kill_procs(SIGALRM);
}

/*
 *  proc_finished()
 *	mark a process as complete
 */
static void proc_finished(const pid_t pid)
{
	const double now = time_now();
	int i, j;

	for (i = 0; i < STRESS_MAX; i++) {
		for (j = 0; j < started_procs[i]; j++) {
			if (procs[i][j].pid == pid) {
				procs[i][j].finish = now;
				procs[i][j].pid = 0;
				return;
			}
		}
	}
}

/*
 *  opt_long()
 *	parse long int option, check for invalid values
 */
static long int opt_long(const char *opt, const char *str)
{
	long int val;

	errno = 0;
	val = strtol(str, NULL, 10);
	if (errno) {
		fprintf(stderr, "Invalid value for the %s option\n", opt);
		exit(EXIT_FAILURE);
	}

	return val;
}

static void free_procs(void)
{
	int32_t i;

	for (i = 0; i < STRESS_MAX; i++)
		free(procs[i]);
}

int main(int argc, char **argv)
{

	int32_t val, opt_random = 0;
	int32_t	num_procs[STRESS_MAX];
	int32_t n_procs, total_procs = 0;
	int32_t max = 0;
	int32_t i, j;
	int	fd;
	double duration;
	size_t len;
	uint64_t *counters;
	struct sigaction new_action;
	double time_start, time_finish;
	char shm_name[64];
	bool success = true;

	memset(started_procs, 0, sizeof(num_procs));
	memset(num_procs, 0, sizeof(num_procs));
	memset(opt_ops, 0, sizeof(opt_ops));
	mwc_reseed();

	opt_cpu_stressor = stress_cpu_find_by_name("sqrt");

	for (;;) {
		int c, option_index;
		stress_id id;
next_opt:
		if ((c = getopt_long(argc, argv, "?hMVvqnt:b:c:i:m:d:f:s:l:p:P:C:S:a:y:F:D:T:u:o:r:B:R:k",
			long_options, &option_index)) == -1)
			break;

		for (id = 0; stressors[id].id != STRESS_MAX; id++) {
			if (stressors[id].short_getopt == c) {
				const char *name = opt_name(c);

				opt_flags |= OPT_FLAGS_SET;
				num_procs[id] = opt_long(name, optarg);
				check_value(name, num_procs[id]);
				goto next_opt;
			}
			if (stressors[id].op == (stress_op)c) {
				opt_ops[id] = get_uint64(optarg);
				check_range(opt_name(c), opt_ops[id], DEFAULT_OPS_MIN, DEFAULT_OPS_MAX);
				goto next_opt;
			}
		}

		switch (c) {
		case OPT_ALL:
			opt_flags |= OPT_FLAGS_SET;
			val = opt_long("-a", optarg);
			check_value("all", val);
			for (i = 0; i < STRESS_MAX; i++)
				num_procs[i] = val;
			break;
		case OPT_RANDOM:
			opt_flags |= OPT_FLAGS_RANDOM;
			opt_random = opt_long("-r", optarg);
			check_value("random", opt_random);
			break;
		case OPT_KEEP_NAME:
			opt_flags |= OPT_FLAGS_KEEP_NAME;
			break;
		case OPT_QUERY:
		case OPT_HELP:
			usage();
		case OPT_VERSION:
			version();
			exit(EXIT_SUCCESS);
		case OPT_VERBOSE:
			opt_flags |= PR_ALL;
			break;
		case OPT_QUIET:
			opt_flags &= ~(PR_ALL);
			break;
		case OPT_DRY_RUN:
			opt_flags |= OPT_FLAGS_DRY_RUN;
			break;
		case OPT_TIMEOUT:
			opt_timeout = get_uint64_time(optarg);
			break;
		case OPT_BACKOFF:
			opt_backoff = opt_long("backoff", optarg);
			break;
		case OPT_CPU_LOAD:
			opt_cpu_load = opt_long("cpu load", optarg);
			if ((opt_cpu_load < 0) || (opt_cpu_load > 100)) {
				fprintf(stderr, "CPU load must in the range 0 to 100.\n");
				exit(EXIT_FAILURE);
			}
			break;
		case OPT_CPU_METHOD:
			opt_cpu_stressor = stress_cpu_find_by_name(optarg);
			if (!opt_cpu_stressor) {
				stress_cpu_stressor_info_t *info = cpu_methods;

				fprintf(stderr, "cpu-method must be one of:");
				for (info = cpu_methods; info->func; info++)
					fprintf(stderr, " %s", info->name);
				fprintf(stderr, "\n");

				exit(EXIT_FAILURE);
			}
			break;
		case OPT_METRICS:
			opt_flags |= OPT_FLAGS_METRICS;
			break;
		case OPT_VM_BYTES:
			opt_vm_bytes = (size_t)get_uint64_byte(optarg);
			check_range("vm-bytes", opt_vm_bytes, MIN_VM_BYTES, MAX_VM_BYTES);
			break;
		case OPT_VM_STRIDE:
			opt_vm_stride = (size_t)get_uint64_byte(optarg);
			check_range("vm-stride", opt_vm_stride, MIN_VM_STRIDE, MAX_VM_STRIDE);
			break;
		case OPT_VM_HANG:
			opt_vm_hang = get_uint64_byte(optarg);
			check_range("vm-hang", opt_vm_hang, MIN_VM_HANG, MAX_VM_HANG);
			break;
		case OPT_VM_KEEP:
			opt_flags |= OPT_FLAGS_VM_KEEP;
		 	break;
#ifdef MAP_POPULATE
		case OPT_VM_MMAP_POPULATE:
			opt_vm_flags |= MAP_POPULATE;
			break;
#endif
#ifdef MAP_LOCKED
		case OPT_VM_MMAP_LOCKED:
			opt_vm_flags |= MAP_LOCKED;
			break;
#endif
		case OPT_HDD_BYTES:
			opt_hdd_bytes =  get_uint64_byte(optarg);
			check_range("hdd-bytes", opt_hdd_bytes, MIN_HDD_BYTES, MAX_HDD_BYTES);
			break;
		case OPT_HDD_NOCLEAN:
			opt_flags |= OPT_FLAGS_NO_CLEAN;
			break;
		case OPT_HDD_WRITE_SIZE:
			opt_hdd_write_size = get_uint64_byte(optarg);
			check_range("hdd-write-size", opt_hdd_write_size, MIN_HDD_WRITE_SIZE, MAX_HDD_WRITE_SIZE);
			break;
		case OPT_DENTRIES:
			opt_dentries = get_uint64(optarg);
			check_range("dentries", opt_dentries, 1, 100000000);
			break;
		case OPT_SOCKET_PORT:
			opt_socket_port = get_uint64(optarg);
			check_range("sock-port", opt_socket_port, 1024, 65536 - num_procs[STRESS_SOCKET]);
			break;
#if defined (__linux__)
		case OPT_TIMER_FREQ:
			opt_timer_freq = get_uint64(optarg);
			check_range("timer-freq", opt_timer_freq, 1000, 100000000);
			break;
		case OPT_SCHED:
			opt_sched = get_opt_sched(optarg);
			break;
		case OPT_SCHED_PRIO:
			opt_sched_priority = get_int(optarg);
			break;
		case OPT_IONICE_CLASS:
			opt_ionice_class = get_opt_ionice_class(optarg);
			break;
		case OPT_IONICE_LEVEL:
			opt_ionice_level = get_int(optarg);
			break;
#endif
		case OPT_MMAP_BYTES:
			opt_mmap_bytes = (size_t)get_uint64_byte(optarg);
			check_range("mmap-bytes", opt_vm_bytes, MIN_MMAP_BYTES, MAX_MMAP_BYTES);
			break;
		case OPT_QSORT_INTEGERS:
			opt_qsort_size = get_uint64(optarg);
			check_range("qsort-size", opt_qsort_size, 1 * KB, 64 * MB);
			break;
		case OPT_UTIME_FSYNC:
			opt_flags |= OPT_FLAGS_UTIME_FSYNC;
			break;
		case OPT_FSTAT_DIR:
			opt_fstat_dir = optarg;
			break;
		default:
			printf("Unknown option\n");
			exit(EXIT_FAILURE);
		}
	}

	if (num_procs[stress_info_index(STRESS_SEMAPHORE)]) {
		/* create a mutex */
		if (sem_init(&sem, 1, 1) < 0) {
			pr_err(stderr, "Semaphore init failed: errno=%d: (%s)\n",
				errno, strerror(errno));
			exit(EXIT_FAILURE);
		}
	}

	if (opt_flags & OPT_FLAGS_RANDOM) {
		int32_t n = opt_random;

		if (opt_flags & OPT_FLAGS_SET) {
			pr_err(stderr, "Cannot specify random option with "
				"other stress processes selected\n");
			exit(EXIT_FAILURE);
		}
		/* create n randomly chosen stressors */
		while (n > 0) {
			int32_t rnd = mwc() % 3;
			if (rnd > n)
				rnd = n;
			n -= rnd;
			num_procs[mwc() % STRESS_MAX] += rnd;
		}
	}

	set_oom_adjustment("main", false);
	set_coredump("main");
#if defined (__linux__)
	set_sched(opt_sched, opt_sched_priority);
	set_iopriority(opt_ionice_class, opt_ionice_level);
#endif
	/* Share bogo ops between processes equally */
	for (i = 0; i < STRESS_MAX; i++)
		opt_ops[i] = num_procs[i] ? opt_ops[i] / num_procs[i] : 0;

	new_action.sa_handler = handle_sigint;
	sigemptyset(&new_action.sa_mask);
	new_action.sa_flags = 0;
	if (sigaction(SIGINT, &new_action, NULL) < 0) {
		pr_err(stderr, "stress_ng: sigaction failed: errno=%d (%s)\n",
			errno, strerror(errno));
		exit(EXIT_FAILURE);
	}

	for (i = 0; i < STRESS_MAX; i++) {
		if (max < num_procs[i])
			max = num_procs[i];
		procs[i] = calloc(num_procs[i], sizeof(proc_info_t));
		if (procs[i] == NULL) {
			pr_err(stderr, "cannot allocate procs\n");
			exit(EXIT_FAILURE);
		}
		total_procs += num_procs[i];
	}

	if (total_procs == 0) {
		pr_err(stderr, "No stress workers specified\n");
		free_procs();
		exit(EXIT_FAILURE);
	}

	pr_inf(stdout, "dispatching hogs:");
	for (i = 0; i < STRESS_MAX; i++) {
		fprintf(stdout, " %" PRId32 " %s%c", num_procs[i], stressors[i].name,
			i == STRESS_MAX - 1 ? '\n' : ',');
	}

	snprintf(shm_name, sizeof(shm_name) - 1, "stress_ng_%d", getpid());
	(void)shm_unlink(shm_name);

	if ((fd = shm_open(shm_name, O_RDWR | O_CREAT, 0)) < 0) {
		pr_err(stderr, "Cannot open shared memory region: errno=%d (%s)\n",
			errno, strerror(errno));
		free_procs();
		exit(EXIT_FAILURE);
	}

	len = sizeof(uint64_t) * STRESS_MAX * max;
	if (ftruncate(fd, MEM_CHUNK_SIZE + len) < 0) {
		pr_err(stderr, "Cannot resize shared memory region: errno=%d (%s)\n",
			errno, strerror(errno));
		(void)close(fd);
		(void)shm_unlink(shm_name);
		free_procs();
		exit(EXIT_FAILURE);
	}
	counters = mmap(NULL, len, PROT_READ | PROT_WRITE, MAP_SHARED, fd, MEM_CHUNK_SIZE);
	if (counters == MAP_FAILED) {
		pr_err(stderr, "Cannot mmap to shared memory region: errno=%d (%s)\n",
			errno, strerror(errno));
		(void)close(fd);
		(void)shm_unlink(shm_name);
		free_procs();
		exit(EXIT_FAILURE);
	}
	if (num_procs[stress_info_index(STRESS_CACHE)]) {
		mem_chunk = mmap(NULL, MEM_CHUNK_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
		if (mem_chunk == MAP_FAILED) {
			pr_err(stderr, "Cannot mmap to shared memory region: errno=%d (%s)\n",
				errno, strerror(errno));
			(void)close(fd);
			(void)shm_unlink(shm_name);
			free_procs();
			exit(EXIT_FAILURE);
		}
		memset(mem_chunk, 0, len);
	}

	(void)close(fd);
	memset(counters, 0, len);

	time_start = time_now();
	pr_dbg(stderr, "starting processes\n");
	for (n_procs = 0; n_procs < total_procs; n_procs++) {
		for (i = 0; i < STRESS_MAX; i++) {
			j = started_procs[i];
			if (j < num_procs[i]) {
				int rc = EXIT_SUCCESS;
				int pid = fork();
				char name[64];

				switch (pid) {
				case -1:
					pr_err(stderr, "Cannot fork: errno=%d (%s)\n",
						errno, strerror(errno));
					kill_procs(SIGALRM);
					goto wait_for_procs;
				case 0:
					/* Child */
					if (stress_sethandler(name) < 0)
						exit(EXIT_FAILURE);
					(void)alarm(opt_timeout);
					mwc_reseed();
					set_oom_adjustment(name, false);
					set_coredump(name);
					snprintf(name, sizeof(name), "%s-%s", app_name, stressors[i].name);
#if defined (__linux__)
					set_iopriority(opt_ionice_class, opt_ionice_level);
#endif
					set_proc_name(name);
					pr_dbg(stderr, "%s: started on pid [%d] (instance %" PRIu32 ")\n",
						name, getpid(), j);

					(void)usleep(opt_backoff * n_procs);
					if (!(opt_flags & OPT_FLAGS_DRY_RUN))
						rc = stressors[i].stress_func(counters + (i * max) + j, j, opt_ops[i], name);
					pr_dbg(stderr, "%s: exited on pid [%d] (instance %" PRIu32 ")\n",
						name, getpid(), j);
					exit(rc);
				default:
					procs[i][j].pid = pid;
					procs[i][j].start = time_now() +
						((double)(opt_backoff * n_procs) / 1000000.0);
					started_procs[i]++;

					/* Forced early abort during startup? */
					if (!opt_do_run) {
						pr_dbg(stderr, "abort signal during startup, cleaning up\n");
						kill_procs(SIGALRM);
						goto wait_for_procs;
					}
					break;
				}
			}
		}
	}
	pr_dbg(stderr, "%d processes running\n", n_procs);

wait_for_procs:
	/* Wait for children to exit */
	while (n_procs) {
		int pid, status;

		if ((pid = wait(&status)) > 0) {
			if (WEXITSTATUS(status)) {
				pr_err(stderr, "Process %d terminated with an error: \n", status);
				success = false;
			}
			proc_finished(pid);
			pr_dbg(stderr, "process [%d] terminated\n", pid);
			n_procs--;
		} else if (pid == -1) {
			kill_procs(SIGALRM);
			printf("Break\n");
		}
	}
	time_finish = time_now();

	duration = time_finish - time_start;
	pr_inf(stdout, "%s run completed in %.2fs\n",
		success ? "successful" : "unsuccessful",
		duration);

	if (opt_flags & OPT_FLAGS_METRICS) {
		for (i = 0; i < STRESS_MAX; i++) {
			uint64_t total = 0;
			double   total_time = 0.0;
			for (j = 0; j < started_procs[i]; j++) {
				total += *(counters + (i * max) + j);
				total_time += procs[i][j].finish - procs[i][j].start;
			}
			pr_inf(stdout, "%s: %" PRIu64 " in %.2f secs, rate: %.2f\n",
				stressors[i].name, total, total_time,
				total_time > 0.0 ? (double)total / total_time : 0.0);
		}
	}
	free_procs();

	if (num_procs[stress_info_index(STRESS_SEMAPHORE)]) {
		if (sem_destroy(&sem) < 0) {
			pr_err(stderr, "Semaphore destroy failed: errno=%d (%s)\n",
				errno, strerror(errno));
		}
	}
	(void)shm_unlink(shm_name);
	exit(EXIT_SUCCESS);
}
