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
#include <string.h>
#include <unistd.h>
#include <inttypes.h>
#include <math.h>
#include <signal.h>
#include <fcntl.h>
#include <getopt.h>
#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <time.h>
#include <semaphore.h>
#include <poll.h>

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

#define APP_NAME		"stress-ng"

/* GNU HURD */
#ifndef PATH_MAX
#define PATH_MAX 		(4096)
#endif

#define STRESS_FD_MAX		(65536)

#ifndef PIPE_BUF
#define PIPE_BUF		(512)
#endif
#define SOCKET_BUF		(8192)
#define STRESS_HDD_BUF_SIZE	(64 * 1024)

/* Option bit masks */
#define OPT_FLAGS_NO_CLEAN	0x00000001	/* Don't remove hdd files */
#define OPT_FLAGS_DRY_RUN	0x00000002	/* Don't actually run */
#define OPT_FLAGS_METRICS	0x00000004	/* Dump metrics at end */
#define OPT_FLAGS_VM_KEEP	0x00000008	/* Don't keep re-allocating */
#define OPT_FLAGS_RANDOM	0x00000010	/* Randomize */
#define OPT_FLAGS_SET		0x00000020	/* Set if user specifies stress procs */
#define OPT_FLAGS_KEEP_NAME	0x00000040	/* Keep stress names to stress-ng */

/* debug output bitmasks */
#define PR_ERR			0x00010000	/* Print errors */
#define PR_INF			0x00020000	/* Print info */
#define PR_DBG			0x00040000	/* Print debug */
#define PR_ALL			(PR_ERR | PR_INF | PR_DBG)

#define pr_dbg(fp, fmt, args...) print(fp, "debug", PR_DBG, fmt, ## args)
#define pr_inf(fp, fmt, args...) print(fp, "info",  PR_INF, fmt, ## args)
#define pr_err(fp, fmt, args...) print(fp, "error", PR_ERR, fmt, ## args)

#define KB			(1024ULL)
#define	MB			(KB * KB)
#define GB			(KB * KB * KB)

#define MIN_VM_BYTES		(4 * KB)
#define MAX_VM_BYTES		(1 * GB)
#define DEFAULT_VM_BYTES	(256 * MB)

#define MIN_VM_STRIDE		(1)
#define MAX_VM_STRIDE		(1 * MB)
#define DEFAULT_VM_STRIDE	(4 * KB)

#define MIN_HDD_BYTES		(1 * MB)
#define MAX_HDD_BYTES		(256 * GB)
#define DEFAULT_HDD_BYTES	(1 * GB)

#define MIN_VM_HANG		(0)
#define MAX_VM_HANG		(3600)
#define DEFAULT_VM_HANG		(~0ULL)

#define DEFAULT_TIMEOUT		(60 * 60 * 24)
#define DEFAULT_BACKOFF		(0)
#define DEFAULT_DENTRIES	(2048)

#define CTXT_STOP		'X'
#define PIPE_STOP		'S'

#define MEM_CHUNK_SIZE		(65536 * 8)
#define UNDEFINED		(-1)

/* stress test metadata */
typedef struct {
	const char *name;		/* name of stress test */
	const char *label;		/* human readable label */
} stress_t;

/* Help information for options */
typedef struct {
	const char *opt_s;		/* short option */
	const char *opt_l;		/* long option */
	const char *description;	/* description */
} help_t;

/* Stress tests */
enum {
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
	STRESS_FLOAT,
	STRESS_INT,
	STRESS_SEMAPHORE,
	STRESS_OPEN,
	STRESS_SIGQUEUE,
	STRESS_POLL,
	/* Add new stress tests here */
	STRESS_MAX
};

/* Command line long options */
enum {
	OPT_VM_BYTES = 1,
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
	OPT_METRICS,
	OPT_CPU_OPS,
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
	OPT_TIMER,
	OPT_TIMER_OPS,
	OPT_TIMER_FREQ,
	OPT_URANDOM,
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
	OPT_DENTRY,
	OPT_DENTRY_OPS,
	OPT_DENTRIES,
	OPT_FLOAT,
	OPT_FLOAT_OPS,
	OPT_INT,
	OPT_INT_OPS,
	OPT_SEMAPHORE,
	OPT_SEMAPHORE_OPS,
	OPT_OPEN,
	OPT_OPEN_OPS,
	OPT_POLL,
	OPT_POLL_OPS,
};

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

/* stress process prototype */
typedef int (*func)(uint64_t *const counter, const uint32_t instance, const uint64_t max_ops, const char *name);

typedef struct {
	pid_t	pid;		/* process id */
	double	start;		/* time process started */
	double	finish;		/* time process got reaped */
} proc_info_t;

typedef struct {
	const char	ch;	/* Scaling suffix */
	const uint64_t	scale;	/* Amount to scale by */
} scale_t;

static int print(FILE *fp, const char *const type, const int flag,
	const char *const fmt, ...) __attribute__((format(printf, 4, 5)));

/* Various option settings and flags */
static sem_t	sem;					/* stress_semaphore sem */
static uint8_t *mem_chunk;				/* Cache load shared memory */
static uint64_t	opt_dentries = DEFAULT_DENTRIES;	/* dentries per loop */
static uint64_t opt_ops[STRESS_MAX];			/* max number of bogo ops */
static uint64_t	opt_vm_hang = DEFAULT_VM_HANG;		/* VM delay */
static uint64_t	opt_hdd_bytes = DEFAULT_HDD_BYTES;	/* HDD size in byts */
static uint64_t	opt_timeout = DEFAULT_TIMEOUT;		/* timeout in seconds */
static uint64_t	mwc_z = 362436069, mwc_w = 521288629;	/* random number vals */
static int64_t	opt_backoff = DEFAULT_BACKOFF;		/* child delay */
static int32_t	started_procs[STRESS_MAX];		/* number of processes per stressor */
static int32_t	opt_flags = PR_ERR | PR_INF;		/* option flags */
static int32_t  opt_cpu_load = 100;			/* CPU max load */
static size_t	opt_vm_bytes = DEFAULT_VM_BYTES;	/* VM bytes */
static size_t	opt_vm_stride = DEFAULT_VM_STRIDE;	/* VM stride */
static int	opt_vm_flags = 0;			/* VM mmap flags */
static pid_t	socket_server, socket_client;		/* pids of socket client/servers */
#if defined (__linux__)
static uint64_t	opt_timer_freq = 1000000;		/* timer frequency (Hz) */
static int	opt_sched = UNDEFINED;			/* sched policy */
static int	opt_sched_priority = UNDEFINED;		/* sched priority */
static int 	opt_ionice_class = UNDEFINED;		/* ionice class */
static int	opt_ionice_level = UNDEFINED;		/* ionice level */
#endif
static int	opt_socket_port = 5000;			/* Default socket port */
static volatile bool opt_do_run = true;			/* false to exit stressor */
static proc_info_t *procs[STRESS_MAX];			/* per process info */


/*
 *  externs to force gcc to stash computed values and hence
 *  to stop the optimiser optimising code away to zero. The
 *  *_put funcs are essentially no-op functions.
 */
extern void double_put(double a, double b, double c, double d);
extern void uint64_put(uint64_t a, uint64_t b);

/* Human readable stress test names */
static const stress_t stressors[] = {
	{ "iosync",	"I/O-Sync" },
	{ "cpu",	"CPU" },
	{ "vm",		"VM-mmap" },
	{ "hdd",	"HDD-Write" },
	{ "fork",	"Fork" },
	{ "ctxt",	"Context-switch" },
	{ "pipe",	"Pipe" },
	{ "cache",	"Cache" },
	{ "socket",	"Socket" },
	{ "yield",	"Yield" },
	{ "fallocate",	"Fallocate" },
	{ "flock",	"Flock" },
	{ "affinity",	"Affinity" },
	{ "timer",	"Timer" },
	{ "dentry",	"Dentry" },
	{ "urandom",	"Urandom" },
	{ "float",	"Float" },
	{ "int",	"Int" },
	{ "semaphore",	"Semaphore" },
	{ "open",	"Open" },
	{ "sigq",	"SigQueue" },
	{ "poll",	"Poll" }
	/* Add new stress tests here */
};

/*
 *  Catch signals and set flag to break out of stress loops
 */
static void stress_sighandler(int dummy)
{
	(void)dummy;
	opt_do_run = false;
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
		pr_err(stderr, "%s: sigaction failed: errno=%d (%s)\n",
			stress, errno, strerror(errno));
		return -1;
	}
	if (sigaction(SIGALRM, &new_action, NULL) < 0) {
		pr_err(stderr, "%s: sigaction failed: errno=%d (%s)\n",
			stress, errno, strerror(errno));
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


/*
 *  mwc()
 *	fast pseudo random number generator, see
 *	http://www.cse.yorku.ca/~oz/marsaglia-rng.html
 */
static inline uint64_t mwc(void)
{
	mwc_z = 36969 * (mwc_z & 65535) + (mwc_z >> 16);
	mwc_w = 18000 * (mwc_w & 65535) + (mwc_w >> 16);
	return (mwc_z << 16) + mwc_w;
}

/*
 *  mwc_reseed()
 *	dirty mwc reseed
 */
static inline void mwc_reseed(void)
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
static inline double timeval_to_double(const struct timeval *tv)
{
	return (double)tv->tv_sec + ((double)tv->tv_usec / 1000000.0);
}

/*
 *  time_now()
 *	time in seconds as a double
 */
static inline double time_now(void)
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
	const char *const type,
	const int flag,
	const char *const fmt, ...)
{
	va_list ap;
	int ret = 0;

	va_start(ap, fmt);
	if (opt_flags & flag) {
		char buf[4096];
		int n = snprintf(buf, sizeof(buf), APP_NAME ": %s: [%i] ",
			type, getpid());
		ret = vsnprintf(buf + n, sizeof(buf) - n, fmt, ap);
		fprintf(fp, "%s", buf);
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
	if (val < 0 || val > 1024) {
		fprintf(stderr, "Number of %s workers must be between "
			"0 and 1024\n", msg);
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
static inline int ioprio_set(const int which, const int who, const int ioprio)
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

	(void)instance;
	(void)name;

	/*
	 * Normal use case, 100% load, simple spinning on CPU
	 */
	if (opt_cpu_load == 100) {
		do {
			int i;
			for (i = 0; i < 16384; i++)
				sqrt((double)mwc());
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
		int i, j;
		double t, delay;
		struct timeval tv1, tv2, tv3;

		gettimeofday(&tv1, NULL);
		for (j = 0; j < 64; j++) {
			for (i = 0; i < 16384; i++)
				sqrt((double)mwc());
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
				pr_dbg(stderr, "%s: mmap failed, re-trying\n", name);
				continue;	/* Try again */
			}
		}

		for (i = 0; i < opt_vm_bytes; i += opt_vm_stride)
			*(buf + i) = gray_code;

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
		}

		if (!keep)
			(void)munmap(buf, opt_vm_bytes);

		(*counter)++;
	} while (opt_do_run && (!max_ops || *counter < max_ops));

	if (keep)
		(void)munmap(buf, opt_vm_bytes);

	return EXIT_SUCCESS;
}

/*
 *  stress_io
 *	stress I/O via writes
 */
static int stress_io(
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

	if ((buf = malloc(STRESS_HDD_BUF_SIZE)) == NULL) {
		pr_err(stderr, "%s: cannot allocate buffer\n", name);
		return EXIT_FAILURE;
	}

	for (i = 0; i < STRESS_HDD_BUF_SIZE; i++)
		buf[i] = (uint8_t)mwc();

	do {
		int fd;
		char filename[64];

		snprintf(filename, sizeof(filename), "./%s-%i.XXXXXXX", name, pid);

		(void)umask(0077);
		if ((fd = mkstemp(filename)) < 0) {
			pr_err(stderr, "%s: mkstemp failed\n", name);
			goto finish;
		}
		if (!(opt_flags & OPT_FLAGS_NO_CLEAN))
			(void)unlink(filename);

		for (i = 0; i < opt_hdd_bytes; i += STRESS_HDD_BUF_SIZE) {
			if (write(fd, buf, STRESS_HDD_BUF_SIZE) < 0) {
				pr_err(stderr, "%s: write error: errno=%d (%s)\n",
					name, errno, strerror(errno));
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
		pr_dbg(stderr, "%s: pipe failed, errno=%d (%s)\n",
			name, errno, strerror(errno));
		return EXIT_FAILURE;
	}

	pid = fork();
	if (pid < 0) {
		(void)close(pipefds[0]);
		(void)close(pipefds[1]);
		pr_dbg(stderr, "%s: fork failed, errno=%d (%s)\n",
			name, errno, strerror(errno));
		return EXIT_FAILURE;
	} else if (pid == 0) {
		(void)close(pipefds[1]);

		for (;;) {
			char ch;

			for (;;) {
				if (read(pipefds[0], &ch, sizeof(ch)) <= 0) {
					pr_dbg(stderr, "%s: read failed, errno=%d (%s)\n",
						name, errno, strerror(errno));
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
				pr_dbg(stderr, "%s: write failed, errno=%d (%s)\n",
					name, errno, strerror(errno));
				break;
			}
			(*counter)++;
		} while (opt_do_run && (!max_ops || *counter < max_ops));

		ch = CTXT_STOP;
		if (write(pipefds[1],  &ch, sizeof(ch)) <= 0)
			pr_dbg(stderr, "%s: termination write failed, errno=%d (%s)\n",
				name, errno, strerror(errno));
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
		pr_dbg(stderr, "%s: pipe failed, errno=%d (%s)\n",
			name, errno, strerror(errno));
		return EXIT_FAILURE;
	}

	pid = fork();
	if (pid < 0) {
		(void)close(pipefds[0]);
		(void)close(pipefds[1]);
		pr_dbg(stderr, "%s: fork failed, errno=%d (%s)\n",
			name, errno, strerror(errno));
		return EXIT_FAILURE;
	} else if (pid == 0) {
		(void)close(pipefds[1]);

		for (;;) {
			char buf[PIPE_BUF];

			for (;;) {
				if (read(pipefds[0], buf, sizeof(buf)) <= 0) {
					pr_dbg(stderr, "%s: read failed, errno=%d (%s)\n",
						name, errno, strerror(errno));
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
				pr_dbg(stderr, "%s: write failed, errno=%d (%s)\n",
					name, errno, strerror(errno));
				break;
			}
			(*counter)++;
		} while (opt_do_run && (!max_ops || *counter < max_ops));

		memset(buf, PIPE_STOP, sizeof(buf));
		if (write(pipefds[1], buf, sizeof(buf)) <= 0)
			pr_dbg(stderr, "%s: termination write failed, errno=%d (%s)\n",
				name, errno, strerror(errno));
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
	long int cpus = sysconf(_SC_NPROCESSORS_CONF);
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
			}
		} else {
			for (j = 0; j < MEM_CHUNK_SIZE; j++) {
				total += mem_chunk[i] + mem_chunk[(MEM_CHUNK_SIZE - 1) - i];
				i = (i + 32769) & (MEM_CHUNK_SIZE - 1);
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
		pr_dbg(stderr, "%s: fork failed, errno=%d (%s)\n",
			name, errno, strerror(errno));
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
				pr_dbg(stderr, "%s: socket failed, errno=%d (%s)\n",
					name, errno, strerror(errno));
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
					pr_dbg(stderr, "%s: connect failed after 100 retries, errno=%d (%s)\n",
						name, errno, strerror(errno));
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
					pr_dbg(stderr, "%s: write failed, errno=%d (%s)\n",
						name, errno, strerror(errno));
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
			pr_err(stderr, "%s: sigaction failed: errno=%d (%s)\n",
				name, errno, strerror(errno));
			rc = EXIT_FAILURE;
			goto die;
		}
		if ((fd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
			pr_dbg(stderr, "%s: socket failed, errno=%d (%s)\n",
				name, errno, strerror(errno));
			rc = EXIT_FAILURE;
			goto die;
		}
		memset(&addr, 0, sizeof(addr));
		addr.sin_family = AF_INET;
		addr.sin_addr.s_addr = htonl(INADDR_ANY);
		addr.sin_port = htons(opt_socket_port + instance);

		if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &so_reuseaddr, sizeof(so_reuseaddr)) < 0) {
			pr_dbg(stderr, "%s: setsockopt failed, errno=%d (%s)\n",
				name, errno, strerror(errno));
			rc = EXIT_FAILURE;
			goto die_close;
		}
		if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
			pr_dbg(stderr, "%s: bind failed, errno=%d (%s)\n",
				name, errno, strerror(errno));
			rc = EXIT_FAILURE;
			goto die_close;
		}
		if (listen(fd, 10) < 0) {
			pr_dbg(stderr, "%s: listen failed, errno=%d (%s)\n",
				name, errno, strerror(errno));
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
						pr_dbg(stderr, "%s: write failed, errno=%d (%s)\n",
							name, errno, strerror(errno));
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
#if defined(_POSIX_PRIORITY_SCHEDULING)
	(void)instance;
	(void)name;

	do {
		sched_yield();
		(*counter)++;
	} while (opt_do_run && (!max_ops || *counter < max_ops));

	return EXIT_SUCCESS;
#else
	(void)counter;
	(void)instance;
	(void)max_ops;
	(void)name;

	return EXIT_SUCCESS;
#endif
}

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
#if _XOPEN_SOURCE >= 600 || _POSIX_C_SOURCE >= 200112L
	const pid_t pid = getpid();
	int fd;
	char filename[64];
	uint64_t ftrunc_errs = 0;

	(void)instance;

	snprintf(filename, sizeof(filename), "./%s-%i.XXXXXXX", name, pid);
	(void)umask(0077);
	if ((fd = mkstemp(filename)) < 0) {
		pr_err(stderr, "%s: mkstemp failed\n", name);
		return EXIT_FAILURE;
	}
	if (!(opt_flags & OPT_FLAGS_NO_CLEAN))
		(void)unlink(filename);

	do {
		(void)posix_fallocate(fd, (off_t)0, 4096 * 4096);
		fsync(fd);
		if (ftruncate(fd, 0) < 0)
			ftrunc_errs++;
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
#else
	(void)counter;
	(void)instance;
	(void)max_ops;
	(void)name;

	return EXIT_SUCCESS;
#endif
}

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
		pr_err(stderr, "%s: open failed: errno=%d (%s)\n",
			name, errno, strerror(errno));
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
#if defined(__linux__)
	long int cpus = sysconf(_SC_NPROCESSORS_CONF);
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
#else
	(void)counter;
	(void)instance;
	(void)max_ops;
	(void)name;

	return EXIT_SUCCESS;
#endif
}

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
#endif

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
#if defined (__linux__)
	struct sigaction new_action;
	struct sigevent sev;
	struct itimerspec timer;
	double rate_ns = opt_timer_freq ? 1000000000 / opt_timer_freq : 1000000000;

	(void)instance;

	new_action.sa_flags = 0;
	new_action.sa_handler = stress_timer_handler;
	sigemptyset(&new_action.sa_mask);
	if (sigaction(SIGRTMIN, &new_action, NULL) < 0) {
		pr_err(stderr, "%s: sigaction failed: errno=%d (%s)\n",
			name, errno, strerror(errno));
		return EXIT_FAILURE;
	}

	sev.sigev_notify = SIGEV_SIGNAL;
	sev.sigev_signo = SIGRTMIN;
	sev.sigev_value.sival_ptr = &timerid;
	if (timer_create(CLOCK_REALTIME, &sev, &timerid) < 0) {
		pr_err(stderr, "%s: timer_create failed: errno=%d (%s)\n",
			name, errno, strerror(errno));
		return EXIT_FAILURE;
	}

	timer.it_value.tv_sec = (long long int)rate_ns / 1000000000;
	timer.it_value.tv_nsec = (long long int)rate_ns % 1000000000;
	timer.it_interval.tv_sec = timer.it_value.tv_sec;
	timer.it_interval.tv_nsec = timer.it_value.tv_nsec;

	if (timer_settime(timerid, 0, &timer, NULL) < 0) {
		pr_err(stderr, "%s: timer_settime failed: errno=%d (%s)\n",
			name, errno, strerror(errno));
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
		pr_err(stderr, "%s: timer_delete failed: errno=%d (%s)\n",
			name, errno, strerror(errno));
		return EXIT_FAILURE;
	}

	return EXIT_SUCCESS;
#else
	(void)counter;
	(void)instance;
	(void)max_ops;
	(void)name;

	return EXIT_SUCCESS;
#endif
}

/*
 *  stress_dentry_unlink()
 *	remove all dentries
 */
static void stress_dentry_unlink(uint64_t n)
{
	uint64_t i;
	pid_t pid = getpid();

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
	pid_t pid = getpid();

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
				pr_err(stderr, "%s: open failed: errno=%d (%s)\n",
					name, errno, strerror(errno));
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
		sync();
	} while (opt_do_run && (!max_ops || *counter < max_ops));

abort:
	/* force unlink of all files */
	pr_dbg(stdout, "%s: removing %" PRIu64 " entries\n", name, opt_dentries);
	stress_dentry_unlink(opt_dentries);

	return EXIT_SUCCESS;
}

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
#if defined (__linux__)
	int fd;

	(void)instance;

	if ((fd = open("/dev/urandom", O_RDONLY)) < 0) {
		pr_err(stderr, "%s: open failed: errno=%d (%s)\n",
			name, errno, strerror(errno));
		return EXIT_FAILURE;
	}

	do {
		char buffer[8192];

		if (read(fd, buffer, sizeof(buffer)) < 0) {
			pr_err(stderr, "%s: read failed: errno=%d (%s)\n",
				name, errno, strerror(errno));
			(void)close(fd);
			return EXIT_FAILURE;
		}
		(*counter)++;
	} while (opt_do_run && (!max_ops || *counter < max_ops));
	(void)close(fd);

	return EXIT_SUCCESS;
#else
	(void)counter;
	(void)instance;
	(void)max_ops;
	(void)name;

	return EXIT_SUCCESS;
#endif
}

/*
 *  stress_float
 *	stress floating point math operations
 */
static int stress_float(
	uint64_t *const counter,
	const uint32_t instance,
	const uint64_t max_ops,
	const char *name)
{
	(void)instance;

	do {
		uint32_t i;
		double a = 0.18728, b, c, d;
		struct timespec clk;

		if (clock_gettime(CLOCK_REALTIME, &clk) < 0) {
			pr_dbg(stderr, "%s: cannot get start seet from clock_gettime: errno=%d, %s\n",
				name, errno, strerror(errno));
			return EXIT_FAILURE;
		}
		b = clk.tv_nsec;
		c = clk.tv_sec;

		for (i = 0; i < 10000; i++) {
			a = a + b;
			b = a * c;
			c = a - b;
			d = a / b;
			a = c / 0.1923;
			b = c + a;
			c = b * 3.12;
			d = d + b + sin(a);
			a = (b + c) / c;
			b = b * c;
			c = c + 1.0;
			d = d - sin(c);
			a = a * cos(b);
			b = b + cos(c);
			c = sin(a) / 2.344;
			b = d - 1.0;
		}
		double_put(a, b, c, d);

		(*counter)++;
	} while (opt_do_run && (!max_ops || *counter < max_ops));

	return EXIT_SUCCESS;
}

/*
 *  stress_int
 *	stress integer operations
 */
static int stress_int(
	uint64_t *const counter,
	const uint32_t instance,
	const uint64_t max_ops,
	const char *name)
{
	(void)instance;

	do {
		uint32_t i;
		uint64_t a = mwc(), b = mwc();
		struct timespec clk;

		if (clock_gettime(CLOCK_REALTIME, &clk) < 0) {
			pr_dbg(stderr, "%s: cannot get start seet from clock_gettime: errno=%d, %s\n",
				name, errno, strerror(errno));
			return EXIT_FAILURE;
		}
		a ^= (uint64_t)clk.tv_nsec;
		b ^= (uint64_t)clk.tv_sec;

		for (i = 0; i < 10000; i++) {
			a += b;
			b ^= a;
			a >>= 1;
			b <<= 2;
			b -= a;
			a ^= ~0;
			b ^= ~0xf0f0f0f0f0f0f0f0ULL;
			a *= 3;
			b *= 7;
			a += 2;
			b -= 3;
			a /= 77;
			b /= 3;
			a <<= 1;
			b <<= 2;
			a |= 1;
			b |= 3;
			a *= mwc();
			b ^= mwc();
			a += mwc();
			b -= mwc();
			a /= 7;
			b /= 9;
			a |= 0x1000100010001000ULL;
			b &= 0xffeffffefebefffeULL;
		}
		uint64_put(a, b);

		(*counter)++;
	} while (opt_do_run && (!max_ops || *counter < max_ops));

	return EXIT_SUCCESS;
}

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
			sem_wait(&sem);
			sem_post(&sem);
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
			(*counter)++;
		}
		for (i = 0; i < STRESS_FD_MAX; i++) {
			if (fds[i] < 0)
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
#endif

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
#if _POSIX_C_SOURCE >= 199309L
	pid_t pid;
	struct sigaction new_action;

	new_action.sa_handler = stress_sigqhandler;
	sigemptyset(&new_action.sa_mask);
	new_action.sa_flags = 0;
	if (sigaction(SIGUSR1, &new_action, NULL) < 0) {
		pr_err(stderr, "%s: sigaction failed: errno=%d (%s)\n",
			name, errno, strerror(errno));
		return EXIT_FAILURE;
	}

	pid = fork();
	if (pid < 0) {
		pr_dbg(stderr, "%s: fork failed, errno=%d (%s)\n",
			name, errno, strerror(errno));
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
#else
	(void)counter;
	(void)instance;
	(void)max_ops;
	(void)name;

	return EXIT_SUCCESS;
#endif
}

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
		(void)sleep(0);
		(*counter)++;
	} while (opt_do_run && (!max_ops || *counter < max_ops));

	return EXIT_SUCCESS;
}

/* stress tests */
static const func stress_funcs[] = {
	stress_iosync,
	stress_cpu,
	stress_vm,
	stress_io,
	stress_fork,
	stress_ctxt,
	stress_pipe,
	stress_cache,
	stress_socket,
	stress_yield,
	stress_fallocate,
	stress_flock,
	stress_affinity,
	stress_timer,
	stress_dentry,
	stress_urandom,
	stress_float,
	stress_int,
	stress_semaphore,
	stress_open,
	stress_sigq,
	stress_poll,
	/* Add new stress tests here */
};

/*
 *  version()
 *	print program version info
 */
static inline void version(void)
{
	printf(APP_NAME ", version " VERSION "\n");
}

static const help_t help[] = {
	{ "?,-h",	"help",			"show help" },
#if defined (__linux__)
	{ NULL,		"affinity N",		"start N workers that rapidly change CPU affinity" },
	{ NULL, 	"affinity-ops N",   	"stop when N affinity bogo operations completed" },
#endif
	{ "a N",	"all N",		"start N workers of each stress test" },
	{ "b N",	"backoff N",		"wait of N microseconds before work starts" },
	{ "c N",	"cpu N",		"start N workers spinning on sqrt(rand())" },
	{ "l P",	"cpu-load P",		"load CPU by P %%, 0=sleep, 100=full load (see -c)" },
	{ NULL,		"cpu-ops N",		"stop when N cpu bogo operations completed" },
	{ "C N",	"cache N",		"start N CPU cache thrashing workers" },
	{ NULL,		"cache-ops N",		"stop when N cache bogo operations completed" },
	{ "D N",	"dentry N",		"start N dentry thrashing processes" },
	{ NULL,		"dentry-ops N",		"stop when N dentry bogo operations completed" },
	{ NULL,		"dentries N",		"create N dentries per iteration" },
	{ "d N",	"hdd N",		"start N workers spinning on write()/unlink()" },
	{ NULL,		"hdd-bytes N",		"write N bytes per hdd worker (default is 1GB)" },
	{ NULL,		"hdd-noclean",		"do not unlink files created by hdd workers" },
	{ NULL,		"hdd-ops N",		"stop when N hdd bogo operations completed" },
	{ NULL,		"fallocate N",		"start N workers fallocating 16MB files" },
	{ NULL,		"fallocate-ops N",	"stop when N fallocate bogo operations completed" },
	{ NULL,		"float N",		"start N workers performing floating point operations" },
	{ NULL, 	"float-ops N",		"stop when N float bogo operations completed" },
	{ NULL,		"flock N",		"start N workers locking a single file" },
	{ NULL,		"flock-ops N",		"stop when N flock bogo operations completed" },
	{ "f N",	"fork N",		"start N workers spinning on fork() and exit()" },
	{ NULL,		"fork-ops N",		"stop when N fork bogo operations completed" },
	{ NULL,		"int N",		"start N workers performing integer operations" },
	{ NULL,		"int-ops N",		"stop when N int bogo operations completed" },
	{ "i N",	"io N",			"start N workers spinning on sync()" },
	{ NULL,		"io-ops N",		"stop when N io bogo operations completed" },
#if defined (__linux__)
	{ NULL,		"ionice-class C",	"specify ionice class (idle, besteffort, realtime)" },
	{ NULL,		"ionice-level L",	"specify ionice level (0 max, 7 min)" },
#endif
	{ "k",		"keep-name",		"keep stress process names to be 'stress-ng'" },
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
	{ "q",		"quiet",		"quiet output" },
	{ "r",		"random N",		"start N random workers" },
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
	{ "s N",	"switch N",		"start N workers doing rapid context switches" },
	{ NULL,		"switch-ops N",		"stop when N context switch bogo operations completed" },
	{ "S N",	"sock N",		"start N workers doing socket activity" },
	{ NULL,		"sock-ops N",		"stop when N socket bogo operations completed" },
	{ NULL,		"sock-port P",		"use socket ports P to P + number of workers - 1" },
	{ "t N",	"timeout N",		"timeout after N seconds" },
#if defined (__linux__)
	{ "T N",	"timer N",		"start N workers producing timer events" },
	{ NULL,		"timer-ops N",		"stop when N timer bogo events completed" },
	{ NULL,		"timer-freq F",		"run timer(s) at F Hz, range 1000 to 1000000000" },
	{ "u N",	"urandom N",		"start M workers reading /dev/urandom" },
	{ NULL,		"urandom-ops N",	"start when N urandom bogo read operations completed" },
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
	printf(	"\nUsage: " APP_NAME " [OPTION [ARG]]\n");
	for (i = 0; help[i].description; i++) {
		char opt_s[10] = "";

		if (help[i].opt_s)
			snprintf(opt_s, sizeof(opt_s), "-%s,", help[i].opt_s);
		printf(" %-6s--%-16s%s\n", opt_s, help[i].opt_l, help[i].description);
	}
	printf("\nExample " APP_NAME " --cpu 8 --io 4 --vm 2 --vm-bytes 128M --fork 4 --timeout 10s\n\n"
	       "Note: Sizes can be suffixed with B,K,M,G and times with s,m,h,d,y\n");
	exit(EXIT_SUCCESS);
}

static const struct option long_options[] = {
	{ "help",	0,	0,	'?' },
	{ "version",	0,	0,	'V' },
	{ "verbose",	0,	0,	'v' },
	{ "quiet",	0,	0,	'q' },
	{ "dry-run",	0,	0,	'n' },
	{ "timeout",	1,	0,	't' },
	{ "backoff",	1,	0,	'b' },
	{ "cpu",	1,	0,	'c' },
	{ "io",		1,	0,	'i' },
	{ "vm",		1,	0,	'm' },
	{ "fork",	1,	0,	'f' },
	{ "switch",	1,	0,	's' },
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
	{ "hdd",	1,	0,	'd' },
	{ "hdd-bytes",	1,	0,	OPT_HDD_BYTES },
	{ "hdd-noclean",0,	0,	OPT_HDD_NOCLEAN },
	{ "metrics",	0,	0,	'M' },
	{ "cpu-ops",	1,	0,	OPT_CPU_OPS },
	{ "io-ops",	1,	0,	OPT_IOSYNC_OPS },
	{ "vm-ops",	1,	0,	OPT_VM_OPS },
	{ "hdd-ops",	1,	0,	OPT_HDD_OPS },
	{ "fork-ops",	1,	0,	OPT_FORK_OPS },
	{ "switch-ops",	1,	0,	OPT_CTXT_OPS },
	{ "cpu-load",	1,	0,	'l' },
	{ "pipe",	1,	0,	'p' },
	{ "pipe-ops",	1,	0,	OPT_PIPE_OPS },
	{ "cache",	1,	0, 	'C' },
	{ "cache-ops",	1,	0,	OPT_CACHE_OPS },
#if _POSIX_C_SOURCE >= 199309L
	{ "sigq",	1,	0,	OPT_SIGQUEUE },
	{ "sigq-ops",	1,	0,	OPT_SIGQUEUE_OPS },
#endif
	{ "sock",	1,	0,	'S' },
	{ "sock-ops",	1,	0,	OPT_SOCKET_OPS },
	{ "sock-port",	1,	0,	OPT_SOCKET_PORT },
	{ "all",	1,	0,	'a' },
#if defined (__linux__)
	{ "sched",	1,	0,	OPT_SCHED },
	{ "sched-prio",	1,	0,	OPT_SCHED_PRIO },
	{ "ionice-class",1,	0,	OPT_IONICE_CLASS },
	{ "ionice-level",1,	0,	OPT_IONICE_LEVEL },
	{ "affinity",	1,	0,	OPT_AFFINITY },
	{ "affinity-ops",1,	0,	OPT_AFFINITY_OPS },
	{ "timer",	1,	0,	'T' },
	{ "timer-ops",	1,	0,	OPT_TIMER_OPS },
	{ "timer-freq",	1,	0,	OPT_TIMER_FREQ },
	{ "urandom",	1,	0,	'u' },
	{ "urandom-ops",1,	0,	OPT_URANDOM_OPS },
#endif
#if defined (_POSIX_PRIORITY_SCHEDULING)
	{ "yield",	1,	0,	'y' },
	{ "yield-ops",	1,	0,	OPT_YIELD_OPS },
#endif
#if _XOPEN_SOURCE >= 600 || _POSIX_C_SOURCE >= 200112L
	{ "fallocate",	1,	0,	'F' },
	{ "fallocate-ops",1,	0,	OPT_FALLOCATE_OPS },
#endif
	{ "flock",	1,	0,	OPT_FLOCK },
	{ "flock-ops",	1,	0,	OPT_FLOCK_OPS },
	{ "dentry",	1,	0,	'D' },
	{ "dentry-ops",	1,	0,	OPT_DENTRY_OPS },
	{ "dentries",	1,	0,	OPT_DENTRIES },
	{ "float",	1,	0,	OPT_FLOAT },
	{ "float-ops",	1,	0,	OPT_FLOAT_OPS },
	{ "int",	1,	0,	OPT_INT },
	{ "int-ops",	1,	0,	OPT_INT_OPS },
	{ "sem",	1,	0,	OPT_SEMAPHORE },
	{ "sem-ops",	1,	0,	OPT_SEMAPHORE_OPS },
	{ "open",	1,	0,	'o' },
	{ "open-ops",	1,	0,	OPT_OPEN_OPS },
	{ "random",	1,	0,	'r' },
	{ "keep-name",	0,	0,	'k' },
	{ "poll",	1,	0,	'P' },
	{ "poll-ops",	1,	0,	OPT_POLL_OPS },
	{ NULL,		0, 	0, 	0 }
};

/*
 *  send_alarm()
 * 	kill tasks using SIGALRM
 */
static void send_alarm(void)
{
	int i;

	for (i = 0; i < STRESS_MAX; i++) {
		int j;
		for (j = 0; j < started_procs[i]; j++) {
			(void)kill(procs[i][j].pid, SIGALRM);
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
	send_alarm();
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

	for (;;) {
		int c;
		int option_index;

		if ((c = getopt_long(argc, argv, "?hMVvqnt:b:c:i:m:d:f:s:l:p:P:C:S:a:y:F:D:T:u:o:r:k",
			long_options, &option_index)) == -1)
			break;
		switch (c) {
		case 'a':
			opt_flags |= OPT_FLAGS_SET;
			val = opt_long("-a", optarg);
			check_value("all", val);
			for (i = 0; i < STRESS_MAX; i++)
				num_procs[i] = val;
			break;
		case 'r':
			opt_flags |= OPT_FLAGS_RANDOM;
			opt_random = opt_long("-r", optarg);
			check_value("random", opt_random);
			break;
		case 'k':
			opt_flags |= OPT_FLAGS_KEEP_NAME;
			break;
		case '?':
		case 'h':
			usage();
		case 'V':
			version();
			exit(EXIT_SUCCESS);
		case 'v':
			opt_flags |= PR_ALL;
			break;
		case 'q':
			opt_flags &= ~(PR_ALL);
			break;
		case 'n':
			opt_flags |= OPT_FLAGS_DRY_RUN;
			break;
		case 't':
			opt_timeout = get_uint64_time(optarg);
			break;
		case 'b':
			opt_backoff = opt_long("backoff", optarg);
			break;
		case 'c':
			opt_flags |= OPT_FLAGS_SET;
			num_procs[STRESS_CPU] = opt_long("cpu", optarg);
			check_value(stressors[STRESS_CPU].label, num_procs[STRESS_CPU]);
			break;
		case 'i':
			opt_flags |= OPT_FLAGS_SET;
			num_procs[STRESS_IOSYNC] = opt_long("io", optarg);
			check_value(stressors[STRESS_IOSYNC].label, num_procs[STRESS_IOSYNC]);
			break;
		case 'm':
			opt_flags |= OPT_FLAGS_SET;
			num_procs[STRESS_VM] = opt_long("vm", optarg);
			check_value(stressors[STRESS_VM].label, num_procs[STRESS_VM]);
			break;
		case 'd':
			opt_flags |= OPT_FLAGS_SET;
			num_procs[STRESS_HDD] = opt_long("hdd", optarg);
			check_value(stressors[STRESS_HDD].label, num_procs[STRESS_HDD]);
			break;
		case 'D':
			opt_flags |= OPT_FLAGS_SET;
			num_procs[STRESS_DENTRY] = opt_long("dentry", optarg);
			check_value(stressors[STRESS_DENTRY].label, num_procs[STRESS_DENTRY]);
			break;
		case 'f':
			opt_flags |= OPT_FLAGS_SET;
			num_procs[STRESS_FORK] = opt_long("fork", optarg);
			check_value(stressors[STRESS_FORK].label, num_procs[STRESS_FORK]);
			break;
		case 's':
			opt_flags |= OPT_FLAGS_SET;
			num_procs[STRESS_CTXT] = opt_long("switch", optarg);
			check_value(stressors[STRESS_CTXT].label, num_procs[STRESS_CTXT]);
			break;
		case 'p':
			opt_flags |= OPT_FLAGS_SET;
			num_procs[STRESS_PIPE] = opt_long("pipe", optarg);
			check_value(stressors[STRESS_PIPE].label, num_procs[STRESS_PIPE]);
			break;
#if _XOPEN_SOURCE >= 600 || _POSIX_C_SOURCE >= 200112L
		case 'F':
			opt_flags |= OPT_FLAGS_SET;
			num_procs[STRESS_FALLOCATE] = opt_long("fallocate", optarg);
			check_value(stressors[STRESS_FALLOCATE].label, num_procs[STRESS_FALLOCATE]);
			break;
#endif
#if defined (_POSIX_PRIORITY_SCHEDULING)
		case 'y':
			opt_flags |= OPT_FLAGS_SET;
			num_procs[STRESS_YIELD] = opt_long("yield", optarg);
			check_value(stressors[STRESS_YIELD].label, num_procs[STRESS_YIELD]);
			break;
#endif
		case 'l':
			opt_cpu_load = opt_long("cpu load", optarg);
			if ((opt_cpu_load < 0) || (opt_cpu_load > 100)) {
				fprintf(stderr, "CPU load must in the range 0 to 100.\n");
				exit(EXIT_FAILURE);
			}
			break;
		case 'C':
			opt_flags |= OPT_FLAGS_SET;
			num_procs[STRESS_CACHE] = opt_long("cache", optarg);
			check_value(stressors[STRESS_CACHE].label, num_procs[STRESS_CACHE]);
			break;
		case 'S':
			opt_flags |= OPT_FLAGS_SET;
			num_procs[STRESS_SOCKET] = opt_long("socket", optarg);
			check_value(stressors[STRESS_SOCKET].label, num_procs[STRESS_SOCKET]);
			break;
		case OPT_FLOCK:
			opt_flags |= OPT_FLAGS_SET;
			num_procs[STRESS_FLOCK] = opt_long("flock", optarg);
			check_value(stressors[STRESS_FLOCK].label, num_procs[STRESS_FLOCK]);
			break;
		case OPT_FLOAT:
			opt_flags |= OPT_FLAGS_SET;
			num_procs[STRESS_FLOAT] = opt_long("float", optarg);
			check_value(stressors[STRESS_FLOAT].label, num_procs[STRESS_FLOAT]);
			break;
		case OPT_INT:
			opt_flags |= OPT_FLAGS_SET;
			num_procs[STRESS_INT] = opt_long("int", optarg);
			check_value(stressors[STRESS_INT].label, num_procs[STRESS_INT]);
			break;
		case OPT_SEMAPHORE:
			opt_flags |= OPT_FLAGS_SET;
			num_procs[STRESS_SEMAPHORE] = opt_long("sem", optarg);
			check_value(stressors[STRESS_SEMAPHORE].label, num_procs[STRESS_SEMAPHORE]);
			break;
		case 'o':
			opt_flags |= OPT_FLAGS_SET;
			num_procs[STRESS_OPEN] = opt_long("open", optarg);
			check_value(stressors[STRESS_OPEN].label, num_procs[STRESS_OPEN]);
			break;
		case 'P':
			opt_flags |= OPT_FLAGS_SET;
			num_procs[STRESS_POLL] = opt_long("poll", optarg);
			check_value(stressors[STRESS_POLL].label, num_procs[STRESS_POLL]);
			break;
#if defined(__linux__)
		case OPT_AFFINITY:
			opt_flags |= OPT_FLAGS_SET;
			num_procs[STRESS_AFFINITY] = opt_long("affinity", optarg);
			check_value(stressors[STRESS_AFFINITY].label, num_procs[STRESS_AFFINITY]);
			break;
		case 'T':
			opt_flags |= OPT_FLAGS_SET;
			num_procs[STRESS_TIMER] = opt_long("timer", optarg);
			check_value(stressors[STRESS_TIMER].label, num_procs[STRESS_TIMER]);
			break;
		case 'u':
			opt_flags |= OPT_FLAGS_SET;
			num_procs[STRESS_URANDOM] = opt_long("urandom", optarg);
			check_value(stressors[STRESS_URANDOM].label, num_procs[STRESS_URANDOM]);
			break;
#endif
#if  _POSIX_C_SOURCE >= 199309L
		case OPT_SIGQUEUE:
			opt_flags |= OPT_FLAGS_SET;
			num_procs[STRESS_SIGQUEUE] = opt_long("sigq", optarg);
			check_value(stressors[STRESS_SIGQUEUE].label, num_procs[STRESS_SIGQUEUE]);
			break;
#endif
		case 'M':
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
		case OPT_CPU_OPS:
			opt_ops[STRESS_CPU] = get_uint64(optarg);
			check_range("cpu-ops", opt_ops[STRESS_CPU], 1000, 100000000);
			break;
		case OPT_IOSYNC_OPS:
			opt_ops[STRESS_IOSYNC] = get_uint64(optarg);
			check_range("io-ops", opt_ops[STRESS_IOSYNC], 1000, 100000000);
			break;
		case OPT_VM_OPS:
			opt_ops[STRESS_VM] = get_uint64(optarg);
			check_range("vm-ops", opt_ops[STRESS_VM], 100, 100000000);
			break;
		case OPT_HDD_OPS:
			opt_ops[STRESS_HDD] = get_uint64(optarg);
			check_range("hdd-ops", opt_ops[STRESS_HDD], 1000, 100000000);
			break;
		case OPT_DENTRY_OPS:
			opt_ops[STRESS_DENTRY] = get_uint64(optarg);
			check_range("dentry-ops", opt_ops[STRESS_DENTRY], 1, 100000000);
			break;
		case OPT_DENTRIES:
			opt_dentries = get_uint64(optarg);
			check_range("dentries", opt_dentries, 1, 100000000);
			break;
		case OPT_FORK_OPS:
			opt_ops[STRESS_FORK] = get_uint64(optarg);
			check_range("fork-ops", opt_ops[STRESS_FORK], 1000, 100000000);
			break;
		case OPT_CTXT_OPS:
			opt_ops[STRESS_CTXT] = get_uint64(optarg);
			check_range("switch-ops", opt_ops[STRESS_CTXT], 1000, 100000000);
			break;
		case OPT_PIPE_OPS:
			opt_ops[STRESS_PIPE] = get_uint64(optarg);
			check_range("pipe-ops", opt_ops[STRESS_PIPE], 1000, 100000000);
			break;
		case OPT_CACHE_OPS:
			opt_ops[STRESS_CACHE] = get_uint64(optarg);
			check_range("cache-ops", opt_ops[STRESS_CACHE], 1000, 100000000);
			break;
#if _XOPEN_SOURCE >= 600 || _POSIX_C_SOURCE >= 200112L
		case OPT_FALLOCATE_OPS:
			opt_ops[STRESS_FALLOCATE] = get_uint64(optarg);
			check_range("fallocate-ops", opt_ops[STRESS_FALLOCATE], 1000, 100000000);
			break;
#endif
#if defined (_POSIX_PRIORITY_SCHEDULING)
		case OPT_YIELD_OPS:
			opt_ops[STRESS_YIELD] = get_uint64(optarg);
			check_range("yield-ops", opt_ops[STRESS_YIELD], 1000, 100000000);
			break;
#endif
		case OPT_FLOCK_OPS:
			opt_ops[STRESS_FLOCK] = get_uint64(optarg);
			check_range("flock-ops", opt_ops[STRESS_FLOCK], 1000, 100000000);
			break;
		case OPT_FLOAT_OPS:
			opt_ops[STRESS_FLOAT] = get_uint64(optarg);
			check_range("float-ops", opt_ops[STRESS_FLOAT], 1000, 100000000);
			break;
		case OPT_INT_OPS:
			opt_ops[STRESS_INT] = get_uint64(optarg);
			check_range("int-ops", opt_ops[STRESS_INT], 1000, 100000000);
			break;
		case OPT_SEMAPHORE_OPS:
			opt_ops[STRESS_SEMAPHORE] = get_uint64(optarg);
			check_range("semaphore-ops", opt_ops[STRESS_SEMAPHORE], 1000, 100000000);
			break;
		case OPT_OPEN_OPS:
			opt_ops[STRESS_OPEN] = get_uint64(optarg);
			check_range("open-ops", opt_ops[STRESS_OPEN], 1000, 100000000);
			break;
		case OPT_POLL_OPS:
			opt_ops[STRESS_POLL] = get_uint64(optarg);
			check_range("poll-ops", opt_ops[STRESS_POLL], 1000, 100000000);
			break;
#if defined(__linux__)
		case OPT_AFFINITY_OPS:
			opt_ops[STRESS_AFFINITY] = get_uint64(optarg);
			check_range("affinity-ops", opt_ops[STRESS_AFFINITY], 1000, 100000000);
			break;
		case OPT_TIMER_OPS:
			opt_ops[STRESS_TIMER] = get_uint64(optarg);
			check_range("timer-ops", opt_ops[STRESS_TIMER], 1000, 100000000);
			break;
		case OPT_TIMER_FREQ:
			opt_timer_freq = get_uint64(optarg);
			check_range("timer-freq", opt_timer_freq, 1000, 100000000);
			break;
		case OPT_URANDOM_OPS:
			opt_ops[STRESS_URANDOM] = get_uint64(optarg);
			check_range("urandom-ops", opt_ops[STRESS_URANDOM], 1000, 100000000);
			break;
#endif
#if  _POSIX_C_SOURCE >= 199309L
		case OPT_SIGQUEUE_OPS:
			opt_ops[STRESS_SIGQUEUE] = get_uint64(optarg);
			check_range("sigq-ops", opt_ops[STRESS_SIGQUEUE], 1000, 100000000);
			break;
#endif
		case OPT_SOCKET_OPS:
			opt_ops[STRESS_SOCKET] = get_uint64(optarg);
			check_range("sock-ops", opt_ops[STRESS_SOCKET], 1000, 100000000);
			break;
		case OPT_SOCKET_PORT:
			opt_socket_port = get_uint64(optarg);
			check_range("sock-port", opt_socket_port, 1024, 65536 - num_procs[STRESS_SOCKET]);
			break;
#if defined (__linux__)
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
		default:
			printf("Unknown option\n");
			exit(EXIT_FAILURE);
		}
	}

	if (num_procs[STRESS_SEMAPHORE]) {
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
		fprintf(stdout, " %" PRId32 " %s%c", num_procs[i], stressors[i].label,
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
	if (num_procs[STRESS_CACHE]) {
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
					send_alarm();
					goto out;
				case 0:
					/* Child */
					mwc_reseed();
					snprintf(name, sizeof(name), APP_NAME "-%s", stressors[i].name);
#if defined (__linux__)
					set_iopriority(opt_ionice_class, opt_ionice_level);
#endif
					set_proc_name(name);
					pr_dbg(stderr, "%s: started on pid [%d] (instance %" PRIu32 ")\n",
						name, getpid(), j);

					if (stress_sethandler(name) < 0)
						exit(EXIT_FAILURE);

					(void)alarm(opt_timeout);
					(void)usleep(opt_backoff * n_procs);
					if (!(opt_flags & OPT_FLAGS_DRY_RUN))
						rc = stress_funcs[i](counters + (i * max) + j, j, opt_ops[i], name);
					pr_dbg(stderr, "%s: exited on pid [%d] (instance %" PRIu32 ")\n",
						name, getpid(), j);
					exit(rc);
				default:
					procs[i][j].pid = pid;
					procs[i][j].start = time_now() +
						((double)(opt_backoff * n_procs) / 1000000.0);
					started_procs[i]++;
					break;
				}
			}
		}
	}
	pr_dbg(stderr, "%d processes running\n", n_procs);

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
			send_alarm();
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
				stressors[i].label, total, total_time,
				total_time > 0.0 ? (double)total / total_time : 0.0);
		}
	}
out:
	free_procs();

	if (num_procs[STRESS_SEMAPHORE]) {
		if (sem_destroy(&sem) < 0) {
			pr_err(stderr, "Semaphore destroy failed: errno=%d (%s)\n",
				errno, strerror(errno));
		}
	}
	(void)shm_unlink(shm_name);
	exit(EXIT_SUCCESS);
}
