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

#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <sys/prctl.h>

#define APP_NAME		"stress-ng"

#define STRESS_HDD_BUF_SIZE	(64 * 1024)

/* Option bit masks */
#define OPT_FLAGS_NO_CLEAN	0x00000001
#define OPT_FLAGS_DRY_RUN	0x00000002
#define OPT_FLAGS_METRICS	0x00000004
#define OPT_FLAGS_VM_KEEP	0x00000008

/* debug output bitmasks */
#define PR_ERR			0x00010000
#define PR_INF			0x00020000
#define PR_DBG			0x00040000
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

#define CTXT_STOP		'X'
#define PIPE_STOP		'S'

#define DIV_OPS_BY_PROCS(opt, nproc) opt = (nproc == 0) ? 0 : opt / nproc;

/* Stress tests */
enum {
	STRESS_IOSYNC	= 0,
	STRESS_CPU,
	STRESS_VM,
	STRESS_HDD,
	STRESS_FORK,
	STRESS_CTXT,
	STRESS_PIPE,
	/* Add new stress tests here */
	STRESS_MAX
};

/* Command line long options */
enum {
	OPT_VM_BYTES = 1,
	OPT_VM_STRIDE,
	OPT_VM_HANG,
	OPT_VM_KEEP,
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
};

typedef void (*func)(uint64_t *counter);

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
static int32_t	opt_flags = PR_ERR | PR_INF;		/* option flags */
static size_t	opt_vm_bytes = DEFAULT_VM_BYTES;	/* VM bytes */
static size_t	opt_vm_stride = DEFAULT_VM_STRIDE;	/* VM stride */
static uint64_t	opt_vm_hang = DEFAULT_VM_HANG;		/* VM delay */
static uint64_t	opt_hdd_bytes = DEFAULT_HDD_BYTES;	/* HDD size in byts */
static uint64_t	opt_timeout = DEFAULT_TIMEOUT;		/* timeout in seconds */
static int64_t	opt_backoff = DEFAULT_BACKOFF;		/* child delay */
static uint64_t	opt_cpu_ops = 0;			/* CPU bogo ops max */
static uint64_t	opt_iosync_ops = 0;			/* IO sync bogo ops */
static uint64_t	opt_vm_ops = 0;				/* VM bogo ops max */
static uint64_t	opt_hdd_ops = 0;			/* HDD bogo ops max */
static uint64_t opt_fork_ops = 0;			/* fork bogo ops max */
static uint64_t opt_ctxt_ops = 0;			/* context ops max */
static uint64_t opt_pipe_ops = 0;			/* pipe ops max */
static int32_t  opt_cpu_load = 100;			/* CPU max load */

/* Human readable stress test names */
static const char *const stressors[] = {
	"I/O-Sync",
	"CPU-compute",
	"VM-mmap",
	"HDD-Write",
	"Fork",
	"Context-switch",
	"Pipe",
	/* Add new stress tests here */
};

#if defined (__linux__)
/* Set process name, we don't care if it fails */
#define set_proc_name(name) (void)prctl(PR_SET_NAME, name)
#else
#define set_proc_name(name)
#endif

/*
 *  timeval_to_double()
 *      convert timeval to seconds as a double
 */
static inline double timeval_to_double(const struct timeval *tv)
{
	return (double)tv->tv_sec + ((double)tv->tv_usec / 1000000.0);
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
	char buf[4096];
	int ret = 0;

	va_start(ap, fmt);
	if (opt_flags & flag) {
		int n = snprintf(buf, sizeof(buf), APP_NAME ": %s: [%i] ",
			type, getpid());
		ret = vsnprintf(buf + n, sizeof(buf) -n, fmt, ap);
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
			" allowed: %" PRId64 " ..% " PRId64 "\n",
			val, opt, lo, hi);
		exit(EXIT_FAILURE);
	}
}

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
	const scale_t const scales[],
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

/* size in bytes, K bytes, M bytes or G bytes */
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

/* time in seconds, minutes, hours, days or years */
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
static void stress_iosync(uint64_t *const counter)
{
	set_proc_name(APP_NAME "-iosync");
	pr_dbg(stderr, "stress_iosync: started on pid [%d]\n", getpid());

	do {
		sync();
		(*counter)++;
	} while (!opt_iosync_ops || *counter < opt_iosync_ops);

	exit(EXIT_SUCCESS);
}

/*
 *  stress_cpu()
 *	stress CPU by doing floating point math ops
 */
static void stress_cpu(uint64_t *const counter)
{
	set_proc_name(APP_NAME "-cpu");
	pr_dbg(stderr, "stress_cpu: started on pid [%d]\n", getpid());

	srand(0x1234);

	/*
	 * Normal use case, 100% load, simple spinning on CPU
	 */
	if (opt_cpu_load == 100) {
		do {
			int i;
			for (i = 0; i < 16384; i++)
				sqrt((double)rand());
			(*counter)++;
		} while (!opt_cpu_ops || *counter < opt_cpu_ops);
		exit(EXIT_SUCCESS);
	}

	/*
	 * It is unlikely, but somebody may request to do a zero
	 * load stress test(!)
	 */
	if (opt_cpu_load == 0) {
		sleep((int)opt_timeout);
		exit(EXIT_SUCCESS);
	}

	/*
	 * More complex percentage CPU utilisation.  This is
	 * not intended to be 100% accurate timing, it is good
	 * enough for most purposes.
	 */
	do {
		int i, j;
		double t, delay;
		struct timeval tv1, tv2;

		gettimeofday(&tv1, NULL);
		for (j = 0; j < 64; j++) {
			for (i = 0; i < 16384; i++)
				sqrt((double)rand());
			(*counter)++;
		}
		gettimeofday(&tv2, NULL);
		t = timeval_to_double(&tv2) - timeval_to_double(&tv1);
		/* Must not calculate this with zero % load */
		delay = t * (((100.0 / (double) opt_cpu_load)) - 1.0);

		tv1.tv_sec = delay;
		tv1.tv_usec = (delay - tv1.tv_sec) * 1000000.0;
		select(0, NULL, NULL, NULL, &tv1);
	} while (!opt_cpu_ops || *counter < opt_cpu_ops);
	exit(EXIT_SUCCESS);
}

/*
 *  stress_vm()
 *	stress virtual memory
 */
static void stress_vm(uint64_t *const counter)
{
	uint8_t *buf = NULL;
	uint8_t	val = 0;
	size_t	i;
	const bool keep = (opt_flags & OPT_FLAGS_VM_KEEP);

	set_proc_name(APP_NAME "-vm");
	pr_dbg(stderr, "stress_vm: started on pid [%d]\n", getpid());

	do {
		const uint8_t gray_code = (val >> 1) ^ val;
		val++;

		if (!keep || (keep && buf == NULL)) {
			buf = mmap(NULL, opt_vm_bytes, PROT_READ | PROT_WRITE,
				MAP_SHARED | MAP_ANONYMOUS, -1, 0);

			if (buf == MAP_FAILED)
				continue;	/* Try again */
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
				pr_err(stderr, "stress_vm: detected memory error, offset : %zd, got: %x\n",
					i, *(buf + i));
				exit(EXIT_FAILURE);
			}
		}

		if (!keep)
			munmap(buf, opt_vm_bytes);

		(*counter)++;
	} while (!opt_vm_ops || *counter < opt_vm_ops);

	exit(EXIT_SUCCESS);
}

/*
 *  stress_io
 *	stress I/O via writes
 */
static void stress_io(uint64_t *const counter)
{
	uint8_t *buf;
	uint64_t i;
	const pid_t pid = getpid();

	set_proc_name(APP_NAME "-io");
	pr_dbg(stderr, "stress_io: started on pid [%d]\n", getpid());

	if ((buf = malloc(STRESS_HDD_BUF_SIZE)) == NULL) {
		pr_err(stderr, "stress_io: cannot allocate buffer\n");
		exit(EXIT_FAILURE);
	}

	for (i = 0; i < STRESS_HDD_BUF_SIZE; i++)
		buf[i] = rand();

	do {
		int fd;
		char filename[64];

		snprintf(filename, sizeof(filename), "./stress-ng-%i.XXXXXXX", pid);

		(void)umask(0077);
		if ((fd = mkstemp(filename)) < 0) {
			pr_err(stderr, "stress_io: mkstemp failed\n");
			exit(EXIT_FAILURE);
		}
		if (!(opt_flags & OPT_FLAGS_NO_CLEAN))
			(void)unlink(filename);

		for (i = 0; i < opt_hdd_bytes; i += STRESS_HDD_BUF_SIZE) {
			if (write(fd, buf, STRESS_HDD_BUF_SIZE) < 0) {
				pr_err(stderr, "stress_io: write error\n");
				exit(EXIT_FAILURE);
			}
			(*counter)++;
			if (opt_hdd_ops && *counter >= opt_hdd_ops)
				break;
		}
		(void)close(fd);
		if ((!opt_flags & OPT_FLAGS_NO_CLEAN))
			(void)unlink(filename);
	} while (!opt_hdd_ops || *counter < opt_hdd_ops);

	free(buf);
	exit(EXIT_SUCCESS);
}

/*
 *  stress_fork()
 *	stress by forking and exiting
 */
static void stress_fork(uint64_t *const counter)
{
	set_proc_name(APP_NAME "-fork");
	pr_dbg(stderr, "stress_fork: started on pid [%d]\n", getpid());

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
	} while (!opt_fork_ops || *counter < opt_fork_ops);

	exit(EXIT_SUCCESS);
}

/*
 *  stress_ctxt
 *	stress by heavy context switching
 */
static void stress_ctxt(uint64_t *const counter)
{
	set_proc_name(APP_NAME "-ctxt");
	pr_dbg(stderr, "stress_ctxt: started on pid [%d]\n", getpid());

	pid_t pid;
	int pipefds[2];

	if (pipe(pipefds) < 0) {
		pr_dbg(stderr, "stress_ctxt: pipe failed, errno=%d [%d]\n", errno, getpid());
		exit(0);
	}

	pid = fork();
	if (pid < 0) {
		close(pipefds[0]);
		close(pipefds[1]);
		exit(EXIT_FAILURE);
	} else if (pid == 0) {
		/* Child, immediately exit */
		close(pipefds[1]);

		for (;;) {
			char ch;

			for (;;) {
				if (read(pipefds[0], &ch, sizeof(ch)) <= 0) {
					pr_dbg(stderr, "stress_ctxt: read failed, errno=%d [%d]\n", errno, getpid());
					break;
				}
				if (ch == CTXT_STOP)
					break;
			}
			close(pipefds[0]);
			exit(EXIT_SUCCESS);
		}
	} else {
		char ch = '_';

		/* Parent */
		close(pipefds[0]);

		do {
			if (write(pipefds[1],  &ch, sizeof(ch)) < 0) {
				pr_dbg(stderr, "stress_ctxt: write failed, errno=%d [%d]\n", errno, getpid());
				break;
			}
			(*counter)++;
		} while (!opt_ctxt_ops || *counter < opt_ctxt_ops);

		ch = CTXT_STOP;
		if (write(pipefds[1],  &ch, sizeof(ch)) <= 0)
			pr_dbg(stderr, "stress_ctxt: termination write failed, errno=%d [%d]\n", errno, getpid());
		kill(pid, SIGKILL);
	}
	exit(EXIT_SUCCESS);
}

#ifndef PIPE_BUF
#define PIPE_BUF 512
#endif

/*
 *  stress_pipe
 *	stress by heavy pipe I/O
 */
static void stress_pipe(uint64_t *const counter)
{
	set_proc_name(APP_NAME "-pipe");
	pr_dbg(stderr, "stress_pipe: started on pid [%d]\n", getpid());

	pid_t pid;
	int pipefds[2];

	if (pipe(pipefds) < 0) {
		pr_dbg(stderr, "stress_pipe: pipe failed, errno=%d [%d]\n", errno, getpid());
		exit(0);
	}

	pid = fork();
	if (pid < 0) {
		close(pipefds[0]);
		close(pipefds[1]);
		exit(EXIT_FAILURE);
	} else if (pid == 0) {
		/* Child, immediately exit */
		close(pipefds[1]);

		for (;;) {
			char buf[PIPE_BUF];

			for (;;) {
				if (read(pipefds[0], buf, sizeof(buf)) <= 0) {
					pr_dbg(stderr, "stress_pipe: read failed, errno=%d [%d]\n", errno, getpid());
					break;
				}
				if (buf[0] == PIPE_STOP)
					break;
			}
			close(pipefds[0]);
			exit(EXIT_SUCCESS);
		}
	} else {
		char buf[PIPE_BUF];

		memset(buf, '0', sizeof(buf));

		/* Parent */
		close(pipefds[0]);

		do {
			if (write(pipefds[1], buf, sizeof(buf)) < 0) {
				pr_dbg(stderr, "stress_pipe: write failed, errno=%d [%d]\n", errno, getpid());
				break;
			}
			(*counter)++;
		} while (!opt_pipe_ops || *counter < opt_pipe_ops);

		memset(buf, PIPE_STOP, sizeof(buf));
		if (write(pipefds[1], buf, sizeof(buf)) <= 0)
			pr_dbg(stderr, "stress_pipe: termination write failed, errno=%d [%d]\n", errno, getpid());
		kill(pid, SIGKILL);
	}
	exit(EXIT_SUCCESS);
}

/* stress tests */
static const func child_funcs[] = {
	stress_iosync,
	stress_cpu,
	stress_vm,
	stress_io,
	stress_fork,
	stress_ctxt,
	stress_pipe,
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

/*
 *  usage()
 *	print some help
 */
static void usage(void)
{
	version();
	printf("\nUsage: stress-ng [OPTION [ARG]]\n");
	printf(" -?, --help          show help\n");
	printf("     --version       show version\n");
	printf(" -v, --verbose       verbose output\n");
	printf(" -q, --quiet         quiet output\n");
	printf(" -n, --dry-run       don't run\n");
	printf(" -t, --timeout N     timeout after N seconds\n");
	printf(" -b, --backoff N     wait of N microseconds before work starts\n");
	printf(" -c, --cpu N         start N workers spinning on sqrt(rand())\n");
	printf(" -l, --cpu-load P    load CPU by P %%, 0 to sleep, 100 is fully loaded\n");
	printf(" -i, --io N          start N workers spinning on sync()\n");
	printf(" -m, --vm N          start N workers spinning on anonymous mmap\n");
	printf("     --vm-bytes N    allocate N bytes per vm worker (default 256MB)\n");
	printf("     --vm-stride N   touch a byte every N bytes (default 4K)\n");
	printf("     --vm-hang N     sleep N seconds before freeing memory\n");
	printf("     --vm-keep       redirty memory instead of reallocating\n");
	printf(" -d, --hdd N         start N workers spinning on write()/unlink()\n");
	printf("     --hdd-bytes N   write N bytes per hdd worker (default is 1GB)\n");
	printf("     --hdd-noclean   do not unlink files created by hdd workers\n");
	printf(" -f, --fork N        start N workers spinning on fork() and exit()\n");
	printf(" -s, --switch N      start N workers doing rapid context switches\n");
	printf(" -p, --pipe N        start N workers exercising pipe I/O\n");
	printf("     --metrics       print pseudo metrics of activity\n");
	printf("     --cpu-ops N     stop when N cpu bogo operations completed\n");
	printf("     --io-ops N      stop when N io bogo operations completed\n");
	printf("     --vm-ops N      stop when N vm bogo operations completed\n");
	printf("     --hdd-ops N     stop when N hdd bogo operations completed\n");
	printf("     --fork-ops N    stop when N fork bogo operations completed\n");
	printf("     --switch-ops N  stop when N context switch bogo operations completed\n");
	printf("     --pipe-ops N    stop when N pipe I/O bogo operations completed\n\n");
	printf("Example " APP_NAME " --cpu 8 --io 4 --vm 2 --vm-bytes 128M --fork 4 --timeout 10s\n\n");
	printf("Note: Sizes can be suffixed with B,K,M,G and times with s,m,h,d,y\n");
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
	{ "hdd",	1,	0,	'd' },
	{ "hdd-bytes",	1,	0,	OPT_HDD_BYTES },
	{ "hdd-noclean",0,	0,	OPT_HDD_NOCLEAN },
	{ "metrics",	0,	0,	OPT_METRICS },
	{ "cpu-ops",	1,	0,	OPT_CPU_OPS },
	{ "io-ops",	1,	0,	OPT_IOSYNC_OPS },
	{ "vm-ops",	1,	0,	OPT_VM_OPS },
	{ "hdd-ops",	1,	0,	OPT_HDD_OPS },
	{ "fork-ops",	1,	0,	OPT_FORK_OPS },
	{ "switch-ops",	1,	0,	OPT_CTXT_OPS },
	{ "cpu-load",	1,	0,	'l' },
	{ "pipe",	1,	0,	'p' },
	{ "pipe-ops",	1,	0,	OPT_PIPE_OPS },
	{ NULL,		0, 	0, 	0 }
};

/*
 *  time_now()
 *	time in seconds as a double
 */
static inline double time_now(void)
{
	struct timeval now;
	gettimeofday(&now, NULL);

	return (double)now.tv_sec + ((double)now.tv_usec / 1000000.0);
}

/*
 *  handle_sigint()
 *	catch SIGINT
 */
static void handle_sigint(int dummy)
{
	(void)dummy;
}

/*
 *  send_alarm()
 * 	kill tasks using SIGALRM
 */
static void send_alarm(
	proc_info_t *const procs[STRESS_MAX],
	const int const started_procs[STRESS_MAX])
{
	int i, j;

	for (i = 0; i < STRESS_MAX; i++) {
		for (j = 0; j < started_procs[i]; j++) {
			kill(procs[i][j].pid, SIGALRM);
		}
	}
}

/*
 *  proc_finished()
 *	mark a process as complete
 */
static void proc_finished(
	const pid_t pid,
	proc_info_t *const procs[STRESS_MAX],
	const int started_procs[STRESS_MAX])
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

int main(int argc, char **argv)
{
	proc_info_t *procs[STRESS_MAX];
	int32_t	num_procs[STRESS_MAX];
	int32_t	started_procs[STRESS_MAX];
	int32_t n_procs, total_procs = 0;
	int32_t max = 0;
	int32_t i, j;
	int	fd;
	double duration;
	size_t len;
	uint64_t *counters;
	struct sigaction new_action, old_action;
	double time_start, time_finish;
	char shm_name[64];
	bool success = true;

	memset(started_procs, 0, sizeof(num_procs));
	memset(num_procs, 0, sizeof(num_procs));

	for (;;) {
		int c;
		int option_index;

		if ((c = getopt_long(argc, argv, "?Vvqnt:b:c:i:m:d:f:s:l:p:",
			long_options, &option_index)) == -1)
			break;
		switch (c) {
		case '?':
			usage();
		case 'V':
			version();
			exit(EXIT_SUCCESS);
		case 'v':
			opt_flags = PR_ALL;
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
			num_procs[STRESS_CPU] = opt_long("cpu", optarg);
			check_value("CPU", num_procs[STRESS_CPU]);
			break;
		case 'i':
			num_procs[STRESS_IOSYNC] = opt_long("io", optarg);
			check_value("IO sync", num_procs[STRESS_IOSYNC]);
			break;
		case 'm':
			num_procs[STRESS_VM] = opt_long("vm", optarg);
			check_value("VM", num_procs[STRESS_VM]);
			break;
		case 'd':
			num_procs[STRESS_HDD] = opt_long("hdd", optarg);
			check_value("HDD", num_procs[STRESS_HDD]);
			break;
		case 'f':
			num_procs[STRESS_FORK] = opt_long("fork", optarg);
			check_value("Forks", num_procs[STRESS_FORK]);
			break;
		case 's':
			num_procs[STRESS_CTXT] = opt_long("switch", optarg);
			check_value("Context-Switches", num_procs[STRESS_CTXT]);
			break;
		case 'p':
			num_procs[STRESS_PIPE] = opt_long("pipe", optarg);
			check_value("Pipe", num_procs[STRESS_PIPE]);
			break;
		case 'l':
			opt_cpu_load = opt_long("cpu load", optarg);
			if ((opt_cpu_load < 0) || (opt_cpu_load > 100)) {
				fprintf(stderr, "CPU load must in the range 0 to 100.\n");
				exit(EXIT_FAILURE);
			}
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
		case OPT_HDD_BYTES:
			opt_hdd_bytes =  get_uint64_byte(optarg);
			check_range("hdd-bytes", opt_hdd_bytes, MIN_HDD_BYTES, MAX_HDD_BYTES);
			break;
		case OPT_HDD_NOCLEAN:
			opt_flags |= OPT_FLAGS_NO_CLEAN;
			break;
		case OPT_METRICS:
			opt_flags |= OPT_FLAGS_METRICS;
			break;
		case OPT_CPU_OPS:
			opt_cpu_ops = get_uint64(optarg);
			check_range("cpu-ops", opt_cpu_ops, 1000, 100000000);
			break;
		case OPT_IOSYNC_OPS:
			opt_iosync_ops = get_uint64(optarg);
			check_range("io-ops", opt_iosync_ops, 1000, 100000000);
			break;
		case OPT_VM_OPS:
			opt_vm_ops = get_uint64(optarg);
			check_range("vm-ops", opt_vm_ops, 100, 100000000);
			break;
		case OPT_HDD_OPS:
			opt_hdd_ops = get_uint64(optarg);
			check_range("hdd-ops", opt_hdd_ops, 1000, 100000000);
			break;
		case OPT_FORK_OPS:
			opt_fork_ops = get_uint64(optarg);
			check_range("fork-ops", opt_fork_ops, 1000, 100000000);
			break;
		case OPT_CTXT_OPS:
			opt_ctxt_ops = get_uint64(optarg);
			check_range("switch-ops", opt_ctxt_ops, 1000, 100000000);
			break;
		case OPT_PIPE_OPS:
			opt_pipe_ops = get_uint64(optarg);
			check_range("pipe-ops", opt_pipe_ops, 1000, 100000000);
			break;
		default:
			printf("Unknown option\n");
			exit(EXIT_FAILURE);
		}
	}

	DIV_OPS_BY_PROCS(opt_cpu_ops, num_procs[STRESS_CPU]);
	DIV_OPS_BY_PROCS(opt_iosync_ops, num_procs[STRESS_IOSYNC]);
	DIV_OPS_BY_PROCS(opt_vm_ops, num_procs[STRESS_VM]);
	DIV_OPS_BY_PROCS(opt_hdd_ops, num_procs[STRESS_HDD]);
	DIV_OPS_BY_PROCS(opt_fork_ops, num_procs[STRESS_FORK]);
	DIV_OPS_BY_PROCS(opt_ctxt_ops, num_procs[STRESS_CTXT]);
	DIV_OPS_BY_PROCS(opt_pipe_ops, num_procs[STRESS_PIPE]);

	new_action.sa_handler = handle_sigint;
	sigemptyset(&new_action.sa_mask);
	new_action.sa_flags = 0;
	sigaction(SIGINT, &new_action, &old_action);

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
		exit(EXIT_FAILURE);
	}

	pr_inf(stdout, "dispatching hogs: "
		"%" PRId32 " cpu, %" PRId32 " io, %" PRId32 " vm, %" PRId32 " hdd, %" PRId32 " fork, %" PRId32 " ctxtsw, %" PRId32 " pipe\n",
		num_procs[STRESS_CPU],
		num_procs[STRESS_IOSYNC],
		num_procs[STRESS_VM],
		num_procs[STRESS_HDD],
		num_procs[STRESS_FORK],
		num_procs[STRESS_CTXT],
		num_procs[STRESS_PIPE]);

	snprintf(shm_name, sizeof(shm_name) - 1, "stress_ng_%d", getpid());
	(void)shm_unlink(shm_name);

	if ((fd = shm_open(shm_name, O_RDWR | O_CREAT, 0)) < 0) {
		pr_err(stderr, "Cannot open shared memory region\n");
		exit(EXIT_FAILURE);
	}

	len = sizeof(uint64_t) * STRESS_MAX * max;
	if (ftruncate(fd, len) < 0) {
		pr_err(stderr, "Cannot resize shared memory region\n");
		(void)close(fd);
		(void)shm_unlink(shm_name);
		exit(EXIT_FAILURE);
	}
	counters = mmap(NULL, len, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	if (counters == MAP_FAILED) {
		pr_err(stderr, "Cannot mmap to shared memory region\n");
		(void)close(fd);
		(void)shm_unlink(shm_name);
		exit(EXIT_FAILURE);
	}
	(void)close(fd);
	memset(counters, 0, len);

	time_start = time_now();
	pr_dbg(stderr, "starting processes\n");
	for (n_procs = 0; n_procs < total_procs; n_procs++) {
		for (i = 0; i < STRESS_MAX; i++) {
			j = started_procs[i];
			if (j < num_procs[i]) {
				int pid = fork();
				switch (pid) {
				case -1:
					pr_err(stderr, "Cannot fork\n");
					send_alarm(procs, started_procs);
					goto out;
				case 0:
					/* Child */
					(void)alarm(opt_timeout);
					(void)usleep(opt_backoff * n_procs);
					if (!(opt_flags & OPT_FLAGS_DRY_RUN))
						child_funcs[i](counters + (i * max) + j);
					exit(0);
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

	while (n_procs) {
		int pid, status;

		if ((pid = wait(&status)) > 0) {
			if (WEXITSTATUS(status)) {
				pr_err(stderr, "Process %d terminated with an error\n", status);
				success = false;
			}
			proc_finished(pid, procs, started_procs);
			pr_dbg(stderr, "process [%d] terminated\n", pid);
			n_procs--;
		} else if (pid == -1) {
			send_alarm(procs, started_procs);
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
				stressors[i], total, total_time,
				total_time > 0.0 ? (double)total / total_time : 0.0);
		}
	}
out:
	(void)shm_unlink(shm_name);
	exit(EXIT_SUCCESS);
}
