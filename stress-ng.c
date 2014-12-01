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
#include <string.h>
#include <unistd.h>
#include <inttypes.h>
#include <signal.h>
#include <fcntl.h>
#include <getopt.h>
#include <errno.h>
#include <semaphore.h>

#include <sys/mman.h>
#include <sys/types.h>
#include <sys/times.h>
#include <sys/wait.h>

#include "stress-ng.h"

/* Various option settings and flags */
const char *app_name = "stress-ng";		/* Name of application */
sem_t	 sem;					/* stress_semaphore sem */
bool	 sem_ok = false;			/* stress_semaphore init ok */
shared_t *shared;				/* shared memory */
uint64_t opt_dentries = DEFAULT_DENTRIES;	/* dentries per loop */
uint64_t opt_ops[STRESS_MAX];			/* max number of bogo ops */
uint64_t opt_vm_hang = DEFAULT_VM_HANG;		/* VM delay */
uint64_t opt_hdd_bytes = DEFAULT_HDD_BYTES;	/* HDD size in byts */
uint64_t opt_hdd_write_size = DEFAULT_HDD_WRITE_SIZE;
uint64_t opt_sendfile_size = DEFAULT_SENDFILE_SIZE;	/* sendfile size */
uint64_t opt_seek_size = DEFAULT_SEEK_SIZE;	/* seek file size */
uint64_t opt_timeout = 0;			/* timeout in seconds */
uint64_t mwc_z = MWC_SEED_Z, mwc_w = MWC_SEED_W;/* random number vals */
uint64_t opt_qsort_size = 256 * 1024;		/* Default qsort size */
uint64_t opt_bsearch_size = 65536;		/* Default bsearch size */
uint64_t opt_tsearch_size = 65536;		/* Default tsearch size */
uint64_t opt_lsearch_size = 8192;		/* Default lsearch size */
uint64_t opt_hsearch_size = 8192;		/* Default hsearch size */
uint64_t opt_bigheap_growth = 16 * 4096;	/* Amount big heap grows */
uint64_t opt_fork_max = DEFAULT_FORKS;		/* Number of fork stress processes */
uint64_t opt_vfork_max = DEFAULT_FORKS;		/* Number of vfork stress processes */
uint64_t opt_sequential = DEFAULT_SEQUENTIAL;	/* Number of sequential iterations */
uint64_t opt_aio_requests = DEFAULT_AIO_REQUESTS;/* Number of async I/O requests */
int64_t  opt_backoff = DEFAULT_BACKOFF;		/* child delay */
int32_t  started_procs[STRESS_MAX];		/* number of processes per stressor */
int32_t  opt_flags = PR_ERROR | PR_INFO | OPT_FLAGS_MMAP_MADVISE;
						/* option flags */
int32_t  opt_cpu_load = 100;			/* CPU max load */
uint32_t opt_class = 0;				/* Which kind of class is specified */
stress_cpu_stressor_info_t *opt_cpu_stressor;	/* Default stress CPU method */
stress_vm_stressor_info_t *opt_vm_stressor;	/* Default stress VM method */
size_t   opt_vm_bytes = DEFAULT_VM_BYTES;	/* VM bytes */
int      opt_vm_flags = 0;			/* VM mmap flags */
size_t   opt_mmap_bytes = DEFAULT_MMAP_BYTES;	/* MMAP size */
#if defined (__linux__)
uint64_t opt_timer_freq = 1000000;		/* timer frequency (Hz) */
#endif
int      opt_sched = UNDEFINED;			/* sched policy */
int      opt_sched_priority = UNDEFINED;	/* sched priority */
int      opt_ionice_class = UNDEFINED;		/* ionice class */
int      opt_ionice_level = UNDEFINED;		/* ionice level */
int      opt_socket_port = 5000;		/* Default socket port */
long int opt_nprocessors_online;		/* Number of processors online */
char     *opt_fstat_dir = "/dev";		/* Default fstat directory */
volatile bool opt_do_run = true;		/* false to exit stressor */
volatile bool opt_sigint = false;		/* true if stopped by SIGINT */
proc_info_t *procs[STRESS_MAX];			/* per process info */

/*
 *  Attempt to catch a range of signals so
 *  we can clean up rather than leave
 *  cruft everywhere.
 */
static const int signals[] = {
	/* POSIX.1-1990 */
#ifdef SIGHUP
	SIGHUP,
#endif
#ifdef SIGINT
	SIGINT,
#endif
#ifdef SIGQUIT
	SIGQUIT,
#endif
#ifdef SIGILL
	SIGILL,
#endif
#ifdef SIGABRT
	SIGABRT,
#endif
#ifdef SIGFPE
	SIGFPE,
#endif
#ifdef SIGSEGV
	SIGSEGV,
#endif
#ifdef SIGTERM
	SIGTERM,
#endif
#ifdef SIGUSR1
	SIGUSR1,
#endif
#ifdef SIGUSR2
	SIGUSR2,
	/* POSIX.1-2001 */
#endif
#ifdef SIGBUS
	SIGBUS,
#endif
#ifdef SIGXCPU
	SIGXCPU,
#endif
#ifdef SIGXFSZ
	SIGXFSZ,
#endif
	/* Linux various */
#ifdef SIGIOT
	SIGIOT,
#endif
#ifdef SIGSTKFLT
	SIGSTKFLT,
#endif
#ifdef SIGPWR
	SIGPWR,
#endif
#ifdef SIGINFO
	SIGINFO,
#endif
#ifdef SIGVTALRM
	SIGVTALRM,
#endif
	-1,
};


#define STRESSOR(lower_name, upper_name, class)	\
	{					\
		stress_ ## lower_name,		\
		STRESS_ ## upper_name,		\
		OPT_ ## upper_name,		\
		OPT_ ## upper_name  ## _OPS,	\
		# lower_name,			\
		class				\
	}

/* Human readable stress test names */
static const stress_t stressors[] = {
#if defined(__linux__)
	STRESSOR(affinity, AFFINITY, CLASS_SCHEDULER),
#endif
#if defined(__linux__)
	STRESSOR(aio, AIO, CLASS_IO | CLASS_INTERRUPT | CLASS_OS),
#endif
	STRESSOR(bigheap, BIGHEAP, CLASS_OS | CLASS_VM),
	STRESSOR(bsearch, BSEARCH, CLASS_CPU_CACHE | CLASS_CPU | CLASS_MEMORY),
	STRESSOR(cache, CACHE, CLASS_CPU_CACHE),
#if _POSIX_C_SOURCE >= 199309L
	STRESSOR(clock, CLOCK, CLASS_INTERRUPT | CLASS_OS),
#endif
	STRESSOR(cpu, CPU, CLASS_CPU),
	STRESSOR(dentry, DENTRY, CLASS_IO | CLASS_OS),
	STRESSOR(dir, DIR, CLASS_IO | CLASS_OS),
#if defined(__linux__)
	STRESSOR(eventfd, EVENTFD, CLASS_IO | CLASS_SCHEDULER | CLASS_OS),
#endif
#if _XOPEN_SOURCE >= 600 || _POSIX_C_SOURCE >= 200112L
	STRESSOR(fallocate, FALLOCATE, CLASS_IO | CLASS_OS),
#endif
	STRESSOR(fault, FAULT, CLASS_INTERRUPT | CLASS_SCHEDULER | CLASS_OS),
	STRESSOR(flock, FLOCK, CLASS_IO | CLASS_OS),
	STRESSOR(fork, FORK, CLASS_SCHEDULER | CLASS_OS),
	STRESSOR(fstat, FSTAT, CLASS_IO | CLASS_OS),
#if defined(__linux__)
	STRESSOR(futex, FUTEX, CLASS_SCHEDULER | CLASS_OS),
#endif
	STRESSOR(get, GET, CLASS_OS),
	STRESSOR(hdd, HDD, CLASS_IO | CLASS_OS),
	STRESSOR(hsearch, HSEARCH, CLASS_CPU_CACHE | CLASS_CPU | CLASS_MEMORY),
	STRESSOR(iosync, IOSYNC, CLASS_IO | CLASS_OS),
#if defined(__linux__)
	STRESSOR(inotify, INOTIFY, CLASS_SCHEDULER | CLASS_OS),
#endif
	STRESSOR(kill, KILL, CLASS_INTERRUPT | CLASS_SCHEDULER | CLASS_OS),
	STRESSOR(link, LINK, CLASS_IO | CLASS_OS),
	STRESSOR(lsearch, LSEARCH, CLASS_CPU_CACHE | CLASS_CPU | CLASS_MEMORY),
	STRESSOR(memcpy, MEMCPY, CLASS_CPU_CACHE | CLASS_MEMORY),
	STRESSOR(mmap, MMAP, CLASS_VM | CLASS_IO | CLASS_OS),
#if !defined(__gnu_hurd__)
	STRESSOR(msg, MSG, CLASS_SCHEDULER | CLASS_OS),
#endif
	STRESSOR(nice, NICE, CLASS_SCHEDULER | CLASS_OS),
	STRESSOR(null, NULL, CLASS_IO | CLASS_MEMORY | CLASS_OS),
	STRESSOR(open, OPEN, CLASS_IO | CLASS_OS),
	STRESSOR(pipe, PIPE, CLASS_IO | CLASS_MEMORY | CLASS_OS),
	STRESSOR(poll, POLL, CLASS_SCHEDULER | CLASS_OS),
#if defined (__linux__)
	STRESSOR(procfs, PROCFS, CLASS_IO | CLASS_OS),
#endif
	STRESSOR(qsort, QSORT, CLASS_CPU_CACHE | CLASS_CPU | CLASS_MEMORY),
#if defined(STRESS_X86)
	STRESSOR(rdrand, RDRAND, CLASS_CPU),
#endif
	STRESSOR(rename, RENAME, CLASS_IO | CLASS_OS),
	STRESSOR(seek, SEEK, CLASS_IO | CLASS_OS),
	STRESSOR(semaphore, SEMAPHORE, CLASS_OS | CLASS_SCHEDULER),
#if defined(__linux__)
	STRESSOR(sendfile, SENDFILE, CLASS_IO | CLASS_OS),
#endif
	STRESSOR(sigfpe, SIGFPE, CLASS_OS),
#if _POSIX_C_SOURCE >= 199309L && !defined(__gnu_hurd__)
	STRESSOR(sigq, SIGQUEUE, CLASS_INTERRUPT | CLASS_OS),
#endif
	STRESSOR(sigsegv, SIGSEGV, CLASS_OS),
	STRESSOR(socket, SOCKET, CLASS_NETWORK | CLASS_OS),
	STRESSOR(stack, STACK, CLASS_CPU | CLASS_MEMORY),
	STRESSOR(switch, SWITCH, CLASS_SCHEDULER | CLASS_OS),
	STRESSOR(symlink, SYMLINK, CLASS_IO | CLASS_OS),
	STRESSOR(sysinfo, SYSINFO, CLASS_OS),
#if defined(__linux__)
	STRESSOR(timer, TIMER, CLASS_INTERRUPT | CLASS_OS),
#endif
	STRESSOR(tsearch, TSEARCH, CLASS_CPU_CACHE | CLASS_CPU | CLASS_MEMORY),
#if defined(__linux__) || defined(__gnu_hurd__)
	STRESSOR(urandom, URANDOM, CLASS_IO | CLASS_OS),
#endif
	STRESSOR(utime, UTIME, CLASS_IO | CLASS_OS),
#if  _BSD_SOURCE || \
    (_XOPEN_SOURCE >= 500 || _XOPEN_SOURCE && _XOPEN_SOURCE_EXTENDED) && \
    !(_POSIX_C_SOURCE >= 200809L || _XOPEN_SOURCE >= 700)
	STRESSOR(vfork, VFORK, CLASS_SCHEDULER | CLASS_OS),
#endif
	STRESSOR(vm, VM, CLASS_IO | CLASS_VM | CLASS_MEMORY | CLASS_OS),
#if !defined(__gnu_hurd__)
	STRESSOR(wait, WAIT, CLASS_SCHEDULER | CLASS_OS),
#endif
#if defined (_POSIX_PRIORITY_SCHEDULING)
	STRESSOR(yield, YIELD, CLASS_SCHEDULER | CLASS_OS),
#endif
	STRESSOR(zero, ZERO, CLASS_IO | CLASS_MEMORY | CLASS_OS),
	/* Add new stress tests here */
	{ stress_noop, STRESS_MAX, 0, 0, NULL, 0 }
};

/* Different stress classes */
static const class_t classes[] = {
	{ CLASS_CPU,		"cpu" },
	{ CLASS_CPU_CACHE,	"cpu-cache" },
	{ CLASS_IO,		"io" },
	{ CLASS_INTERRUPT,	"interrupt" },
	{ CLASS_MEMORY,		"memory" },
	{ CLASS_NETWORK,	"network" },
	{ CLASS_OS,		"os" },
	{ CLASS_SCHEDULER,	"scheduler" },
	{ CLASS_VM,		"vm" },
	{ 0,			NULL }
};

static const struct option long_options[] = {
	{ "affinity",	1,	0,	OPT_AFFINITY },
	{ "affinity-ops",1,	0,	OPT_AFFINITY_OPS },
#if defined(__linux__)
	{ "aio",	1,	0,	OPT_AIO },
	{ "aio-ops",	1,	0,	OPT_AIO_OPS },
	{ "aio-requests",1,	0,	OPT_AIO_REQUESTS },
#endif
	{ "all",	1,	0,	OPT_ALL },
	{ "backoff",	1,	0,	OPT_BACKOFF },
	{ "bigheap",	1,	0,	OPT_BIGHEAP },
	{ "bigheap-ops",1,	0,	OPT_BIGHEAP_OPS },
	{ "bigheap-growth",1,	0,	OPT_BIGHEAP_GROWTH },
	{ "bsearch",	1,	0,	OPT_BSEARCH },
	{ "bsearch-ops",1,	0,	OPT_BSEARCH_OPS },
	{ "bsearch-size",1,	0,	OPT_BSEARCH_SIZE },
	{ "cache",	1,	0, 	OPT_CACHE },
	{ "cache-ops",	1,	0,	OPT_CACHE_OPS },
	{ "class",	1,	0,	OPT_CLASS },
#if _POSIX_C_SOURCE >= 199309L
	{ "clock",	1,	0,	OPT_CLOCK },
	{ "clock-ops",	1,	0,	OPT_CLOCK_OPS },
#endif
	{ "cpu",	1,	0,	OPT_CPU },
	{ "cpu-ops",	1,	0,	OPT_CPU_OPS },
	{ "cpu-load",	1,	0,	OPT_CPU_LOAD },
	{ "cpu-method",	1,	0,	OPT_CPU_METHOD },
	{ "dentry",	1,	0,	OPT_DENTRY },
	{ "dentry-ops",	1,	0,	OPT_DENTRY_OPS },
	{ "dentries",	1,	0,	OPT_DENTRIES },
	{ "dir",	1,	0,	OPT_DIR },
	{ "dir-ops",	1,	0,	OPT_DIR_OPS },
	{ "dry-run",	0,	0,	OPT_DRY_RUN },
#if defined (__linux__)
	{ "eventfd",	1,	0,	OPT_EVENTFD },
	{ "eventfd-ops",1,	0,	OPT_EVENTFD_OPS },
#endif
#if _XOPEN_SOURCE >= 600 || _POSIX_C_SOURCE >= 200112L
	{ "fallocate",	1,	0,	OPT_FALLOCATE },
	{ "fallocate-ops",1,	0,	OPT_FALLOCATE_OPS },
#endif
	{ "fault",	1,	0,	OPT_FAULT },
	{ "fault-ops",	1,	0,	OPT_FAULT_OPS },
	{ "flock",	1,	0,	OPT_FLOCK },
	{ "flock-ops",	1,	0,	OPT_FLOCK_OPS },
	{ "fork",	1,	0,	OPT_FORK },
	{ "fork-ops",	1,	0,	OPT_FORK_OPS },
	{ "fork-max",	1,	0,	OPT_FORK_MAX },
	{ "fstat",	1,	0,	OPT_FSTAT },
	{ "fstat-ops",	1,	0,	OPT_FSTAT_OPS },
	{ "fstat-dir",	1,	0,	OPT_FSTAT_DIR },
	{ "futex",	1,	0,	OPT_FUTEX },
	{ "futex-ops",	1,	0,	OPT_FUTEX_OPS },
	{ "get",	1,	0,	OPT_GET },
	{ "get-ops",	1,	0,	OPT_GET_OPS },
	{ "hdd",	1,	0,	OPT_HDD },
	{ "hdd-ops",	1,	0,	OPT_HDD_OPS },
	{ "hdd-bytes",	1,	0,	OPT_HDD_BYTES },
	{ "hdd-write-size", 1,	0,	OPT_HDD_WRITE_SIZE },
	{ "help",	0,	0,	OPT_QUERY },
	{ "hsearch",	1,	0,	OPT_HSEARCH },
	{ "hsearch-ops",1,	0,	OPT_HSEARCH_OPS },
	{ "hsearch-size",1,	0,	OPT_HSEARCH_SIZE },
#if defined (__linux__)
	{ "inotify",	1,	0,	OPT_INOTIFY },
	{ "inotify-ops",1,	0,	OPT_INOTIFY_OPS },
#endif
	{ "io",		1,	0,	OPT_IOSYNC },
	{ "io-ops",	1,	0,	OPT_IOSYNC_OPS },
#if defined (__linux__)
	{ "ionice-class",1,	0,	OPT_IONICE_CLASS },
	{ "ionice-level",1,	0,	OPT_IONICE_LEVEL },
#endif
	{ "keep-name",	0,	0,	OPT_KEEP_NAME },
	{ "kill",	1,	0,	OPT_KILL },
	{ "kill-ops",	1,	0,	OPT_KILL_OPS },
	{ "link",	1,	0,	OPT_LINK },
	{ "link-ops",	1,	0,	OPT_LINK_OPS },
	{ "lsearch",	1,	0,	OPT_LSEARCH },
	{ "lsearch-ops",1,	0,	OPT_LSEARCH_OPS },
	{ "lsearch-size",1,	0,	OPT_LSEARCH_SIZE },
	{ "memcpy",	1,	0,	OPT_MEMCPY },
	{ "memcpy-ops",	1,	0,	OPT_MEMCPY_OPS },
	{ "mmap",	1,	0,	OPT_MMAP },
	{ "mmap-ops",	1,	0,	OPT_MMAP_OPS },
	{ "mmap-bytes",	1,	0,	OPT_MMAP_BYTES },
	{ "metrics",	0,	0,	OPT_METRICS },
	{ "metrics-brief",0,	0,	OPT_METRICS_BRIEF },
#if !defined(__gnu_hurd__)
	{ "msg",	1,	0,	OPT_MSG },
	{ "msg-ops",	1,	0,	OPT_MSG_OPS },
#endif
	{ "nice",	1,	0,	OPT_NICE },
	{ "nice-ops",	1,	0,	OPT_NICE_OPS },
	{ "no-madvise",	0,	0,	OPT_NO_MADVISE },
	{ "null",	1,	0,	OPT_NULL },
	{ "null-ops",	1,	0,	OPT_NULL_OPS },
	{ "open",	1,	0,	OPT_OPEN },
	{ "open-ops",	1,	0,	OPT_OPEN_OPS },
	{ "page-in",	0,	0,	OPT_PAGE_IN },
	{ "pipe",	1,	0,	OPT_PIPE },
	{ "pipe-ops",	1,	0,	OPT_PIPE_OPS },
	{ "poll",	1,	0,	OPT_POLL },
	{ "poll-ops",	1,	0,	OPT_POLL_OPS },
#if defined (__linux__)
	{ "procfs",	1,	0,	OPT_PROCFS },
	{ "procfs-ops",	1,	0,	OPT_PROCFS_OPS },
#endif
	{ "qsort",	1,	0,	OPT_QSORT },
	{ "qsort-ops",	1,	0,	OPT_QSORT_OPS },
	{ "qsort-size",	1,	0,	OPT_QSORT_INTEGERS },
	{ "quiet",	0,	0,	OPT_QUIET },
	{ "random",	1,	0,	OPT_RANDOM },
#if defined(STRESS_X86)
	{ "rdrand",	1,	0,	OPT_RDRAND },
	{ "rdrand-ops",	1,	0,	OPT_RDRAND_OPS },
#endif
	{ "rename",	1,	0,	OPT_RENAME },
	{ "rename-ops",	1,	0,	OPT_RENAME_OPS },
	{ "sched",	1,	0,	OPT_SCHED },
	{ "sched-prio",	1,	0,	OPT_SCHED_PRIO },
	{ "seek",	1,	0,	OPT_SEEK },
	{ "seek-ops",	1,	0,	OPT_SEEK_OPS },
	{ "seek-size",	1,	0,	OPT_SEEK_SIZE },
	{ "sem",	1,	0,	OPT_SEMAPHORE },
	{ "sem-ops",	1,	0,	OPT_SEMAPHORE_OPS },
#if defined(__linux__)
	{ "sendfile",	1,	0,	OPT_SENDFILE },
	{ "sendfile-ops",1,	0,	OPT_SENDFILE_OPS },
	{ "sendfile-size",1,	0,	OPT_SENDFILE_SIZE },
#endif
	{ "sequential",	1,	0,	OPT_SEQUENTIAL },
	{ "sigfpe",	1,	0,	OPT_SIGFPE },
	{ "sigfpe-ops",	1,	0,	OPT_SIGFPE_OPS },
	{ "sigsegv",	1,	0,	OPT_SIGSEGV },
	{ "sigsegv-ops",1,	0,	OPT_SIGSEGV_OPS },
#if _POSIX_C_SOURCE >= 199309L && !defined(__gnu_hurd__)
	{ "sigq",	1,	0,	OPT_SIGQUEUE },
	{ "sigq-ops",	1,	0,	OPT_SIGQUEUE_OPS },
#endif
	{ "sock",	1,	0,	OPT_SOCKET },
	{ "sock-ops",	1,	0,	OPT_SOCKET_OPS },
	{ "sock-port",	1,	0,	OPT_SOCKET_PORT },
	{ "stack",	1,	0,	OPT_STACK},
	{ "stack-ops",	1,	0,	OPT_STACK_OPS },
	{ "switch",	1,	0,	OPT_SWITCH },
	{ "switch-ops",	1,	0,	OPT_SWITCH_OPS },
	{ "symlink",	1,	0,	OPT_SYMLINK },
	{ "symlink-ops",1,	0,	OPT_SYMLINK_OPS },
	{ "sysinfo",	1,	0,	OPT_SYSINFO },
	{ "sysinfo-ops",1,	0,	OPT_SYSINFO_OPS },
	{ "timeout",	1,	0,	OPT_TIMEOUT },
#if defined (__linux__)
	{ "timer",	1,	0,	OPT_TIMER },
	{ "timer-ops",	1,	0,	OPT_TIMER_OPS },
	{ "timer-freq",	1,	0,	OPT_TIMER_FREQ },
#endif
	{ "tsearch",	1,	0,	OPT_TSEARCH },
	{ "tsearch-ops",1,	0,	OPT_TSEARCH_OPS },
	{ "tsearch-size",1,	0,	OPT_TSEARCH_SIZE },
	{ "times",	0,	0,	OPT_TIMES },
	{ "utime",	1,	0,	OPT_UTIME },
	{ "utime-ops",	1,	0,	OPT_UTIME_OPS },
	{ "utime-fsync",0,	0,	OPT_UTIME_FSYNC },
#if defined (__linux__) || defined(__gnu_hurd__)
	{ "urandom",	1,	0,	OPT_URANDOM },
	{ "urandom-ops",1,	0,	OPT_URANDOM_OPS },
#endif
	{ "verbose",	0,	0,	OPT_VERBOSE },
	{ "verify",	0,	0,	OPT_VERIFY },
	{ "version",	0,	0,	OPT_VERSION },
#if  _BSD_SOURCE || \
    (_XOPEN_SOURCE >= 500 || _XOPEN_SOURCE && _XOPEN_SOURCE_EXTENDED) && \
    !(_POSIX_C_SOURCE >= 200809L || _XOPEN_SOURCE >= 700)
	{ "vfork",	1,	0,	OPT_VFORK },
	{ "vfork-ops",	1,	0,	OPT_VFORK_OPS },
	{ "vfork-max",	1,	0,	OPT_VFORK_MAX },
#endif
	{ "vm",		1,	0,	OPT_VM },
	{ "vm-bytes",	1,	0,	OPT_VM_BYTES },
	{ "vm-hang",	1,	0,	OPT_VM_HANG },
	{ "vm-keep",	0,	0,	OPT_VM_KEEP },
#ifdef MAP_POPULATE
	{ "vm-populate",0,	0,	OPT_VM_MMAP_POPULATE },
#endif
#ifdef MAP_LOCKED
	{ "vm-locked",	0,	0,	OPT_VM_MMAP_LOCKED },
#endif
	{ "vm-ops",	1,	0,	OPT_VM_OPS },
	{ "vm-method",	1,	0,	OPT_VM_METHOD },
#if !defined(__gnu_hurd__)
	{ "wait",	1,	0,	OPT_WAIT },
	{ "wait-ops",	1,	0,	OPT_WAIT_OPS },
#endif
#if defined (_POSIX_PRIORITY_SCHEDULING)
	{ "yield",	1,	0,	OPT_YIELD },
	{ "yield-ops",	1,	0,	OPT_YIELD_OPS },
#endif
	{ "zero",	1,	0,	OPT_ZERO },
	{ "zero-ops",	1,	0,	OPT_ZERO_OPS },
	{ NULL,		0, 	0, 	0 }
};

static const help_t help[] = {
	{ "?,-h",	"help",			"show help" },
#if defined (__linux__)
	{ NULL,		"affinity N",		"start N workers that rapidly change CPU affinity" },
	{ NULL, 	"affinity-ops N",   	"stop when N affinity bogo operations completed" },
#endif
#if defined (__linux__)
	{ NULL,		"aio N",		"start N workers that issue async I/O requests" },
	{ NULL,		"aio-ops N",		"stop when N bogo async I/O requests completed" },
	{ NULL,		"aio-requests N",	"number of async I/O requests per worker" },
#endif
	{ "a N",	"all N",		"start N workers of each stress test" },
	{ "b N",	"backoff N",		"wait of N microseconds before work starts" },
	{ "B N",	"bigheap N",		"start N workers that grow the heap using calloc()" },
	{ NULL,		"bigheap-ops N",	"stop when N bogo bigheap operations completed" },
	{ NULL, 	"bigheap-growth N",	"grow heap by N bytes per iteration" },
	{ NULL,		"bsearch",		"start N workers that exercise a binary search" },
	{ NULL,		"bsearch-ops",		"stop when N binary search bogo operations completed" },
	{ NULL,		"bsearch-size",		"number of 32 bit integers to bsearch" },
	{ "C N",	"cache N",		"start N CPU cache thrashing workers" },
	{ NULL,		"cache-ops N",		"stop when N cache bogo operations completed" },
	{ NULL,		"class name",		"specify a class of stressors, use with --sequential" },
#if _POSIX_C_SOURCE >= 199309L
	{ NULL,		"clock N",		"start N workers thrashing clocks and POSIX timers" },
	{ NULL,		"clock-ops N",		"stop clock workers after N bogo operations" },
#endif
	{ "c N",	"cpu N",		"start N workers spinning on sqrt(rand())" },
	{ NULL,		"cpu-ops N",		"stop when N cpu bogo operations completed" },
	{ "l P",	"cpu-load P",		"load CPU by P %%, 0=sleep, 100=full load (see -c)" },
	{ NULL,		"cpu-method m",		"specify stress cpu method m, default is all" },
	{ "D N",	"dentry N",		"start N dentry thrashing processes" },
	{ NULL,		"dentry-ops N",		"stop when N dentry bogo operations completed" },
	{ NULL,		"dentries N",		"create N dentries per iteration" },
	{ NULL,		"dir N",		"start N directory thrashing processes" },
	{ NULL,		"dir-ops N",		"stop when N directory bogo operations completed" },
	{ "n",		"dry-run",		"do not run" },
#if defined (__linux__)
	{ NULL,		"eventfd N",		"start N workers stressing eventfd read/writes" },
	{ NULL,		"eventfd-ops N",	"stop eventfd workers after N bogo operations" },
#endif
#if _XOPEN_SOURCE >= 600 || _POSIX_C_SOURCE >= 200112L
	{ NULL,		"fallocate N",		"start N workers fallocating 16MB files" },
	{ NULL,		"fallocate-ops N",	"stop when N fallocate bogo operations completed" },
#endif
	{ NULL,		"fault N",		"start N workers producing page faults" },
	{ NULL,		"fault-ops N",		"stop when N page fault bogo operations completed" },
	{ NULL,		"flock N",		"start N workers locking a single file" },
	{ NULL,		"flock-ops N",		"stop when N flock bogo operations completed" },
	{ "f N",	"fork N",		"start N workers spinning on fork() and exit()" },
	{ NULL,		"fork-ops N",		"stop when N fork bogo operations completed" },
	{ NULL,		"fork-max P",		"create P processes per iteration, default is 1" },
	{ NULL,		"fstat N",		"start N workers exercising fstat on files" },
	{ NULL,		"fstat-ops N",		"stop when N fstat bogo operations completed" },
	{ NULL,		"fstat-dir path",	"fstat files in the specified directory" },
#if defined (__linux__)
	{ NULL,		"futex N",		"start N workers exercising a fast mutex" },
	{ NULL,		"futex-ops N",		"stop when N fast mutex bogo operations completed" },
#endif
	{ NULL,		"get N",		"start N workers exercising the get*() system calls" },
	{ NULL,		"get-ops N",		"stop when N get bogo operations completed" },
	{ "d N",	"hdd N",		"start N workers spinning on write()/unlink()" },
	{ NULL,		"hdd-ops N",		"stop when N hdd bogo operations completed" },
	{ NULL,		"hdd-bytes N",		"write N bytes per hdd worker (default is 1GB)" },
	{ NULL,		"hdd-write-size N",	"set the default write size to N bytes" },
	{ NULL,		"hsearch",		"start N workers that exercise a hash table search" },
	{ NULL,		"hsearch-ops",		"stop when N hash search bogo operations completed" },
	{ NULL,		"hsearch-size",		"number of integers to insert into hash table" },
#if defined (__linux__)
	{ NULL,		"inotify N",		"start N workers exercising inotify events" },
	{ NULL,		"inotify-ops N",	"stop inotify workers after N bogo operations" },
#endif
	{ "i N",	"io N",			"start N workers spinning on sync()" },
	{ NULL,		"io-ops N",		"stop when N io bogo operations completed" },
#if defined (__linux__)
	{ NULL,		"ionice-class C",	"specify ionice class (idle, besteffort, realtime)" },
	{ NULL,		"ionice-level L",	"specify ionice level (0 max, 7 min)" },
#endif
	{ "k",		"keep-name",		"keep stress process names to be 'stress-ng'" },
	{ NULL,		"kill N",		"start N workers killing with SIGUSR1" },
	{ NULL,		"kill-ops N",		"stop when N kill bogo operations completed" },
	{ NULL,		"link N",		"start N workers creating hard links" },
	{ NULL,		"link-ops N",		"stop when N link bogo operations completed" },
	{ NULL,		"lsearch",		"start N workers that exercise a linear search" },
	{ NULL,		"lsearch-ops",		"stop when N linear search bogo operations completed" },
	{ NULL,		"lsearch-size",		"number of 32 bit integers to lsearch" },
	{ "M",		"metrics",		"print pseudo metrics of activity" },
	{ NULL,		"metrics-brief",	"enable metrics and only show non-zero results" },
	{ NULL,		"memcpy N",		"start N workers performing memory copies" },
	{ NULL,		"memcpy-ops N",		"stop when N memcpy bogo operations completed" },
	{ NULL,		"mmap N",		"start N workers stressing mmap and munmap" },
	{ NULL,		"mmap-ops N",		"stop when N mmap bogo operations completed" },
	{ NULL,		"mmap-bytes N",		"mmap and munmap N bytes for each stress iteration" },
	{ NULL,		"msg N",		"start N workers passing messages using System V messages" },
	{ NULL,		"msg-ops N",		"stop msg workers after N bogo messages completed" },
	{ NULL,		"nice N",		"start N workers that randomly re-adjust nice levels" },
	{ NULL,		"nice-ops N",		"stop when N nice bogo operations completed" },
	{ NULL,		"no-madvise",		"don't use random madvise options for each mmap" },
	{ NULL,		"null N",		"start N workers writing to /dev/null" },
	{ NULL,		"null-ops N",		"stop when N /dev/null bogo write operations completed" },
	{ "o",		"open N",		"start N workers exercising open/close" },
	{ NULL,		"open-ops N",		"stop when N open/close bogo operations completed" },
	{ NULL,		"page-in",		"touch allocated pages that are not in core" },
	{ "p N",	"pipe N",		"start N workers exercising pipe I/O" },
	{ NULL,		"pipe-ops N",		"stop when N pipe I/O bogo operations completed" },
	{ "P N",	"poll N",		"start N workers exercising zero timeout polling" },
	{ NULL,		"poll-ops N",		"stop when N poll bogo operations completed" },
#if defined (__linux__)
	{ NULL,		"procfs N",		"start N workers reading portions of /proc" },
	{ NULL,		"procfs-ops N",		"stop procfs workers after N bogo read operations" },
#endif
	{ "Q",		"qsort N",		"start N workers exercising qsort on 32 bit random integers" },
	{ NULL,		"qsort-ops N",		"stop when N qsort bogo operations completed" },
	{ NULL,		"qsort-size N",		"number of 32 bit integers to sort" },
	{ "q",		"quiet",		"quiet output" },
	{ "r",		"random N",		"start N random workers" },
#if defined(STRESS_X86)
	{ NULL,		"rdrand N",		"start N workers exercising rdrand instruction" },
	{ NULL,		"rdrand-ops N",		"stop when N rdrand bogo operations completed" },
#endif
	{ "R",		"rename N",		"start N workers exercising file renames" },
	{ NULL,		"rename-ops N",		"stop when N rename bogo operations completed" },
	{ NULL,		"sched type",		"set scheduler type" },
	{ NULL,		"sched-prio N",		"set scheduler priority level N" },
	{ NULL,		"seek N",		"start N workers performing random seek r/w IO" },
	{ NULL,		"seek-ops N",		"stop when N seek bogo operations completed" },
	{ NULL,		"seek-size N",		"length of file to do random I/O upon" },
	{ NULL,		"sem N",		"start N workers doing semaphore operations" },
	{ NULL,		"sem-ops N",		"stop when N semaphore bogo operations completed" },
#if defined (__linux__)
	{ NULL,		"sendfile N",		"start N workers exercising sendfile" },
	{ NULL,		"sendfile-ops N",	"stop after N bogo sendfile operations" },
	{ NULL,		"sendfile-size N",	"size of data to be sent with sendfile" },
#endif
	{ NULL,		"sequential N",		"run all stressors one by one, invoking N of them" },
	{ NULL,		"sigfpe N",		"start N workers generating floating point math faults" },
	{ NULL,		"sigfpe-ops N",		"stop when N bogo floating point math faults completed" },
#if _POSIX_C_SOURCE >= 199309L
	{ NULL,		"sigq N",		"start N workers sending sigqueue signals" },
	{ NULL,		"sigq-ops N",		"stop when N siqqueue bogo operations completed" },
#endif
	{ NULL,		"sigsegv N",		"start N workers generating segmentation faults" },
	{ NULL,		"sigsegv-ops N",	"stop when N bogo segmentation faults completed" },
	{ "S N",	"sock N",		"start N workers doing socket activity" },
	{ NULL,		"sock-ops N",		"stop when N socket bogo operations completed" },
	{ NULL,		"sock-port P",		"use socket ports P to P + number of workers - 1" },
	{ NULL,		"stack N",		"start N workers generating stack overflows" },
	{ NULL,		"stack-ops N",		"stop when N bogo stack overflows completed" },
	{ "s N",	"switch N",		"start N workers doing rapid context switches" },
	{ NULL,		"switch-ops N",		"stop when N context switch bogo operations completed" },
	{ NULL,		"symlink N",		"start N workers creating symbolic links" },
	{ NULL,		"symlink-ops N",	"stop when N symbolic link bogo operations completed" },
	{ NULL,		"sysinfo N",		"start N workers reading system information" },
	{ NULL,		"sysinfo-ops N",	"stop when sysinfo bogo operations completed" },
	{ "t N",	"timeout N",		"timeout after N seconds" },
#if defined (__linux__)
	{ "T N",	"timer N",		"start N workers producing timer events" },
	{ NULL,		"timer-ops N",		"stop when N timer bogo events completed" },
	{ NULL,		"timer-freq F",		"run timer(s) at F Hz, range 1000 to 1000000000" },
#endif
	{ NULL,		"tsearch",		"start N workers that exercise a tree search" },
	{ NULL,		"tsearch-ops",		"stop when N tree search bogo operations completed" },
	{ NULL,		"tsearch-size",		"number of 32 bit integers to tsearch" },
	{ NULL,		"times",		"show run time summary at end of the run" },
#if defined(__linux__) || defined(__gnu_hurd__)
	{ "u N",	"urandom N",		"start N workers reading /dev/urandom" },
	{ NULL,		"urandom-ops N",	"stop when N urandom bogo read operations completed" },
#endif
	{ NULL,		"utime N",		"start N workers updating file timestamps" },
	{ NULL,		"utime-ops N",		"stop after N utime bogo operations completed" },
	{ NULL,		"utime-fsync",		"force utime meta data sync to the file system" },
	{ "v",		"verbose",		"verbose output" },
	{ NULL,		"verify",		"verify results (not available on all tests)" },
	{ "V",		"version",		"show version" },
#if  _BSD_SOURCE || \
    (_XOPEN_SOURCE >= 500 || _XOPEN_SOURCE && _XOPEN_SOURCE_EXTENDED) && \
    !(_POSIX_C_SOURCE >= 200809L || _XOPEN_SOURCE >= 700)
	{ NULL,		"vfork N",		"start N workers spinning on vfork() and exit()" },
	{ NULL,		"vfork-ops N",		"stop when N vfork bogo operations completed" },
	{ NULL,		"vfork-max P",		"create P processes per iteration, default is 1" },
#endif
	{ "m N",	"vm N",			"start N workers spinning on anonymous mmap" },
	{ NULL,		"vm-bytes N",		"allocate N bytes per vm worker (default 256MB)" },
	{ NULL,		"vm-hang N",		"sleep N seconds before freeing memory" },
	{ NULL,		"vm-keep",		"redirty memory instead of reallocating" },
	{ NULL,		"vm-ops N",		"stop when N vm bogo operations completed" },
#ifdef MAP_LOCKED
	{ NULL,		"vm-locked",		"lock the pages of the mapped region into memory" },
#endif
	{ NULL,		"vm-method m",		"specify stress vm method m, default is all" },
#ifdef MAP_POPULATE
	{ NULL,		"vm-populate",		"populate (prefault) page tables for a mapping" },
#endif
#if !defined(__gnu_hurd__)
	{ NULL,		"wait N",		"start N workers waiting on child being stop/resumed" },
	{ NULL,		"wait-ops N",		"stop when N bogo wait operations completed" },
#endif
#if defined(_POSIX_PRIORITY_SCHEDULING)
	{ "y N",	"yield N",		"start N workers doing sched_yield() calls" },
	{ NULL,		"yield-ops N",		"stop when N bogo yield operations completed" },
#endif
	{ NULL,		"zero N",		"start N workers reading /dev/zero" },
	{ NULL,		"zero-ops N",		"stop when N /dev/zero bogo read operations completed" },
	{ NULL,		NULL,			NULL }
};

static uint32_t get_class(const char *str)
{
	int i;

	for (i = 0; classes[i].class; i++)
		if (!strcmp(classes[i].name, str))
			return classes[i].class;

	return 0;
}

/*
 *  Catch signals and set flag to break out of stress loops
 */
static void stress_sigint_handler(int dummy)
{
	(void)dummy;
	opt_sigint = true;
	opt_do_run = false;
}

static void stress_sigalrm_handler(int dummy)
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

	new_action.sa_handler = stress_sigint_handler;
	sigemptyset(&new_action.sa_mask);
	new_action.sa_flags = 0;

	if (sigaction(SIGINT, &new_action, NULL) < 0) {
		pr_failed_err(stress, "sigaction");
		return -1;
	}

	new_action.sa_handler = stress_sigalrm_handler;
	sigemptyset(&new_action.sa_mask);
	new_action.sa_flags = 0;

	if (sigaction(SIGALRM, &new_action, NULL) < 0) {
		pr_failed_err(stress, "sigaction");
		return -1;
	}
	return 0;
}

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
	printf("\nExample: %s --cpu 8 --io 4 --vm 2 --vm-bytes 128M --fork 4 --timeout 10s\n\n"
	       "Note: Sizes can be suffixed with B,K,M,G and times with s,m,h,d,y\n", app_name);
	exit(EXIT_SUCCESS);
}

/*
 *  opt_name()
 *	find name associated with an option value
 */
static const char *opt_name(int opt_val)
{
	int i;

	for (i = 0; long_options[i].name; i++)
		if (long_options[i].val == opt_val)
			return long_options[i].name;

	return "<unknown>";
}

/*
 *  proc_finished()
 *	mark a process as complete
 */
static inline void proc_finished(proc_info_t *proc)
{
	proc->pid = 0;
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
 *  wait_procs()
 * 	wait for procs
 */
static void wait_procs(bool *success)
{
	int i;

	for (i = 0; i < STRESS_MAX; i++) {
		int j;
		for (j = 0; j < started_procs[i]; j++) {
			pid_t pid;
redo:
			pid = procs[i][j].pid;
			if (pid) {
				int status, ret;

				ret = waitpid(pid, &status, 0);
				if (ret > 0) {
					if (WEXITSTATUS(status)) {
						pr_err(stderr, "Process %d terminated with an error, exit status=%d\n",
							ret, WEXITSTATUS(status));
						*success = false;
					}
					proc_finished(&procs[i][j]);
					pr_dbg(stderr, "process [%d] terminated\n", ret);
				} else if (ret == -1) {
					/* Somebody interrupted the wait */
					if (errno == EINTR)
						goto redo;
					/* This child did not exist, mark it done anyhow */
					if (errno == ECHILD)
						proc_finished(&procs[i][j]);
				}
			}
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
 *  opt_long()
 *	parse long int option, check for invalid values
 */
static long int opt_long(const char *opt, const char *str)
{
	long int val;
	char c;
	bool found = false;

	for (c = '0'; c <= '9'; c++) {
		if (strchr(str, c)) {
			found = true;
			break;
		}
	}
	if (!found) {
		fprintf(stderr, "Given value %s is not a valid decimal for the %s option\n",
			str, opt);
		exit(EXIT_FAILURE);
	}

	errno = 0;
	val = strtol(str, NULL, 10);
	if (errno) {
		fprintf(stderr, "Invalid value for the %s option\n", opt);
		exit(EXIT_FAILURE);
	}

	return val;
}

/*
 *  free_procs()
 *	free proc info in procs table
 */
static void free_procs(void)
{
	int32_t i;

	for (i = 0; i < STRESS_MAX; i++)
		free(procs[i]);
}

/*
 *  stress_run ()
 *	kick off and run stressors
 */
void stress_run(
	const int total_procs,
	const int32_t max_procs,
	const int32_t num_procs[],
	proc_stats_t stats[],
	double *duration,
	bool *success
)
{
	double time_start, time_finish;
	int32_t n_procs, i, j, n;

	time_start = time_now();
	pr_dbg(stderr, "starting processes\n");
	for (n_procs = 0; n_procs < total_procs; n_procs++) {
		for (i = 0; i < STRESS_MAX; i++) {
			if (time_now() - time_start > opt_timeout)
				goto abort;

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
					lock_mem_current();
					mwc_reseed();
					set_oom_adjustment(name, false);
					set_coredump(name);
					snprintf(name, sizeof(name), "%s-%s", app_name, stressors[i].name);
					set_iopriority(opt_ionice_class, opt_ionice_level);
					set_proc_name(name);
					pr_dbg(stderr, "%s: started [%d] (instance %" PRIu32 ")\n",
						name, getpid(), j);

					n = (i * max_procs) + j;
					stats[n].start = stats[n].finish = time_now();

					(void)usleep(opt_backoff * n_procs);
					if (opt_do_run && !(opt_flags & OPT_FLAGS_DRY_RUN))
						rc = stressors[i].stress_func(&stats[n].counter, j, opt_ops[i], name);
					stats[n].finish = time_now();
					if (times(&stats[n].tms) == (clock_t)-1) {
						pr_dbg(stderr, "times failed: errno=%d (%s)\n",
							errno, strerror(errno));
					}
					pr_dbg(stderr, "%s: exited [%d] (instance %" PRIu32 ")\n",
						name, getpid(), j);
					exit(rc);
				default:
					procs[i][j].pid = pid;
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

abort:
	pr_dbg(stderr, "%d processes running\n", n_procs);

wait_for_procs:
	wait_procs(success);
	time_finish = time_now();

	*duration += time_finish - time_start;
}

int main(int argc, char **argv)
{
	double duration = 0.0;
	int32_t val, opt_random = 0, i, j;
	int32_t	num_procs[STRESS_MAX];
	int32_t total_procs = 0, max_procs = 0;
	size_t len;
	bool success = true, previous = false;
	struct sigaction new_action;
	long int ticks_per_sec;

	memset(num_procs, 0, sizeof(num_procs));
	memset(opt_ops, 0, sizeof(opt_ops));
	mwc_reseed();

	opt_cpu_stressor = stress_cpu_find_by_name("all");
	opt_vm_stressor = stress_vm_find_by_name("all");

	if ((opt_nprocessors_online = sysconf(_SC_NPROCESSORS_ONLN)) < 0) {
		pr_err(stderr, "sysconf failed, number of cpus online unknown: errno=%d: (%s)\n",
			errno, strerror(errno));
		exit(EXIT_FAILURE);
	}
	ticks_per_sec = sysconf(_SC_CLK_TCK);
	if (ticks_per_sec < 0) {
		pr_err(stderr, "sysconf failed, clock ticks per second unknown: errno=%d (%s)\n",
			errno, strerror(errno));
		exit(EXIT_FAILURE);
	}

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
				if (num_procs[id] <= 0)
					num_procs[id] = opt_nprocessors_online;
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
			if (val <= 0)
				val = opt_nprocessors_online;
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
		case OPT_VM_METHOD:
			opt_vm_stressor = stress_vm_find_by_name(optarg);
			if (!opt_vm_stressor) {
				stress_vm_stressor_info_t *info = vm_methods;

				fprintf(stderr, "vm-method must be one of:");
				for (info = vm_methods; info->func; info++)
					fprintf(stderr, " %s", info->name);
				fprintf(stderr, "\n");

				exit(EXIT_FAILURE);
			}
			break;
		case OPT_HDD_BYTES:
			opt_hdd_bytes =  get_uint64_byte(optarg);
			check_range("hdd-bytes", opt_hdd_bytes, MIN_HDD_BYTES, MAX_HDD_BYTES);
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
		case OPT_SCHED:
			opt_sched = get_opt_sched(optarg);
			break;
		case OPT_SCHED_PRIO:
			opt_sched_priority = get_int(optarg);
			break;
#if defined (__linux__)
		case OPT_TIMER_FREQ:
			opt_timer_freq = get_uint64(optarg);
			check_range("timer-freq", opt_timer_freq, 1000, 100000000);
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
			opt_qsort_size = get_uint64_byte(optarg);
			check_range("qsort-size", opt_qsort_size, 1 * KB, 64 * MB);
			break;
		case OPT_UTIME_FSYNC:
			opt_flags |= OPT_FLAGS_UTIME_FSYNC;
			break;
		case OPT_FSTAT_DIR:
			opt_fstat_dir = optarg;
			break;
		case OPT_METRICS_BRIEF:
			opt_flags |= (OPT_FLAGS_METRICS_BRIEF | OPT_FLAGS_METRICS);
			break;
		case OPT_VERIFY:
			opt_flags |= (OPT_FLAGS_VERIFY | PR_FAIL);
			break;
		case OPT_BIGHEAP_GROWTH:
			opt_bigheap_growth = get_uint64_byte(optarg);
			check_range("bigheap-growth", opt_bigheap_growth, 4 * KB, 64 * MB);
			break;
		case OPT_FORK_MAX:
			opt_fork_max = get_uint64_byte(optarg);
			check_range("fork-max", opt_fork_max, DEFAULT_FORKS_MIN, DEFAULT_FORKS_MAX);
			break;
#if  _BSD_SOURCE || \
    (_XOPEN_SOURCE >= 500 || _XOPEN_SOURCE && _XOPEN_SOURCE_EXTENDED) && \
    !(_POSIX_C_SOURCE >= 200809L || _XOPEN_SOURCE >= 700)
		case OPT_VFORK_MAX:
			opt_vfork_max = get_uint64_byte(optarg);
			check_range("vfork-max", opt_vfork_max, DEFAULT_FORKS_MIN, DEFAULT_FORKS_MAX);
			break;
#endif
		case OPT_SEQUENTIAL:
			opt_sequential = get_uint64_byte(optarg);
			if (opt_sequential <= 0)
				opt_sequential = opt_nprocessors_online;
			check_range("sequential", opt_sequential, DEFAULT_SEQUENTIAL_MIN, DEFAULT_SEQUENTIAL_MAX);
			break;
#if defined (__linux__)
		case OPT_SENDFILE_SIZE:
			opt_sendfile_size = get_uint64_byte(optarg);
			check_range("sendfile-size", opt_sendfile_size, 1 * KB, 1 * GB);
			break;
#endif
		case OPT_SEEK_SIZE:
			opt_seek_size = get_uint64_byte(optarg);
			check_range("seek-size", opt_seek_size, MIN_SEEK_SIZE, MAX_SEEK_SIZE);
			break;
		case OPT_NO_MADVISE:
			opt_flags &= ~OPT_FLAGS_MMAP_MADVISE;
			break;
		case OPT_PAGE_IN:
			opt_flags |= OPT_FLAGS_MMAP_MINCORE;
			break;
		case OPT_TIMES:
			opt_flags |= OPT_FLAGS_TIMES;
			break;
		case OPT_BSEARCH_SIZE:
			opt_bsearch_size = get_uint64_byte(optarg);
			check_range("bsearch-size", opt_bsearch_size, 1 * KB, 4 * MB);
			break;
		case OPT_TSEARCH_SIZE:
			opt_tsearch_size = get_uint64_byte(optarg);
			check_range("tsearch-size", opt_tsearch_size, 1 * KB, 4 * MB);
			break;
		case OPT_LSEARCH_SIZE:
			opt_lsearch_size = get_uint64_byte(optarg);
			check_range("lsearch-size", opt_lsearch_size, 1 * KB, 4 * MB);
			break;
		case OPT_HSEARCH_SIZE:
			opt_hsearch_size = get_uint64_byte(optarg);
			check_range("hsearch-size", opt_hsearch_size, 1 * KB, 4 * MB);
			break;
#if defined(__linux__)
		case OPT_AIO_REQUESTS:
			opt_aio_requests = get_uint64(optarg);
			check_range("aio-requests", opt_aio_requests, MIN_AIO_REQUESTS, MAX_AIO_REQUESTS);
			break;
#endif
		case OPT_CLASS:
			opt_class = get_class(optarg);
			if (!opt_class) {
				int i;

				fprintf(stderr, "Unknown class: '%s', available classes:", optarg);
				for (i = 0; classes[i].class; i++)
					fprintf(stderr, " %s", classes[i].name);
				fprintf(stderr, "\n");
				exit(EXIT_FAILURE);
			}
			break;
		default:
			printf("Unknown option\n");
			exit(EXIT_FAILURE);
		}
	}

	if (opt_class && !opt_sequential) {
		fprintf(stderr, "class option is only used with sequential option\n");
		exit(EXIT_FAILURE);
	}

	pr_dbg(stderr, "%ld processors online\n", opt_nprocessors_online);

	if (opt_flags & OPT_FLAGS_RANDOM) {
		int32_t n = opt_random;

		if (opt_flags & OPT_FLAGS_SET) {
			pr_err(stderr, "Cannot specify random option with "
				"other stress processes selected\n");
			exit(EXIT_FAILURE);
		}
		/* create n randomly chosen stressors */
		while (n > 0) {
			int32_t rnd = mwc() % ((opt_random >> 5) + 2);
			if (rnd > n)
				rnd = n;
			n -= rnd;
			num_procs[mwc() % STRESS_MAX] += rnd;
		}
	}

	if (num_procs[stress_info_index(STRESS_SEMAPHORE)] || opt_sequential) {
		/* create a mutex */
		if (sem_init(&sem, 1, 1) < 0) {
			if (opt_sequential) {
				pr_inf(stderr, "Semaphore init failed: errno=%d: (%s), "
					"skipping semaphore stressor\n",
					errno, strerror(errno));
			} else {
				pr_err(stderr, "Semaphore init failed: errno=%d: (%s)\n",
					errno, strerror(errno));
				exit(EXIT_FAILURE);
			}
		} else
			sem_ok = true;
	}

	set_oom_adjustment("main", false);
	set_coredump("main");
	set_sched(opt_sched, opt_sched_priority);
	set_iopriority(opt_ionice_class, opt_ionice_level);
	lock_mem_current();

	for (i = 0; signals[i] != -1; i++) {
		new_action.sa_handler = handle_sigint;
		sigemptyset(&new_action.sa_mask);
		new_action.sa_flags = 0;
		if (sigaction(signals[i], &new_action, NULL) < 0) {
			pr_err(stderr, "stress_ng: sigaction failed: errno=%d (%s)\n",
				errno, strerror(errno));
			exit(EXIT_FAILURE);
		}
	}

	/* Share bogo ops between processes equally */
	for (i = 0; i < STRESS_MAX; i++) {
		opt_ops[i] = num_procs[i] ? opt_ops[i] / num_procs[i] : 0;
		total_procs += num_procs[i];
	}

	if (opt_sequential) {
		if (total_procs) {
			pr_err(stderr, "sequential option cannot be specified with other stressors enabled\n");
			free_procs();
			exit(EXIT_FAILURE);
		}
		for (i = 0; i < STRESS_MAX; i++) {
			opt_ops[i] = 0;
			num_procs[i] = opt_class ? (stressors[i].class & opt_class ? opt_sequential : 0) : opt_sequential;
		}
		if (opt_timeout == 0) {
			opt_timeout = 60; 
			pr_inf(stdout, "defaulting to a %" PRIu64 " second run per stressor\n", opt_timeout);
		}
	} else {
		if (!total_procs) {
			pr_err(stderr, "No stress workers specified\n");
			free_procs();
			exit(EXIT_FAILURE);
		}
		if (opt_timeout == 0) {
			opt_timeout = DEFAULT_TIMEOUT; 
			pr_inf(stdout, "defaulting to a %" PRIu64 " second run per stressor\n", opt_timeout);
		}
	}

	for (i = 0; i < STRESS_MAX; i++) {
		if (max_procs < num_procs[i])
			max_procs = num_procs[i];
		procs[i] = calloc(num_procs[i], sizeof(proc_info_t));
		if (procs[i] == NULL) {
			pr_err(stderr, "cannot allocate procs\n");
			free_procs();
			exit(EXIT_FAILURE);
		}
	}

	pr_inf(stdout, "dispatching hogs:");
	for (i = 0; i < STRESS_MAX; i++) {
		if (num_procs[i]) {
			fprintf(stdout, "%s %" PRId32 " %s",
				previous ? "," : "",
				num_procs[i], stressors[i].name);
			previous = true;
		}
	}
	fprintf(stdout, "\n");
	fflush(stdout);

	len = sizeof(shared_t) + (sizeof(proc_stats_t) * STRESS_MAX * max_procs);
	shared = mmap(NULL, len, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANON, -1, 0);
	if (shared == MAP_FAILED) {
		pr_err(stderr, "Cannot mmap to shared memory region: errno=%d (%s)\n",
			errno, strerror(errno));
		free_procs();
		exit(EXIT_FAILURE);
	}

	memset(shared, 0, len);
	memset(started_procs, 0, sizeof(num_procs));

	if (opt_sequential) {
		/*
		 *  Step through each stressor one by one
		 */
		memset(num_procs, 0, sizeof(num_procs));
		for (i = 0; opt_do_run && i < STRESS_MAX; i++) {
			opt_ops[i] = 0;
			num_procs[i] = opt_class ? (stressors[i].class & opt_class ? opt_sequential : 0) : opt_sequential;
			if (num_procs[i]) {
				stress_run(opt_sequential, opt_sequential, num_procs, shared->stats, &duration, &success);
				num_procs[i] = 0;
			}
		}
	} else {
		/*
		 *  Run all stressors in parallel
		 */
		stress_run(total_procs, max_procs, num_procs, shared->stats, &duration, &success);
	}

	pr_inf(stdout, "%s run completed in %.2fs\n",
		success ? "successful" : "unsuccessful",
		duration);

	if (opt_flags & OPT_FLAGS_METRICS) {
		pr_inf(stdout, "%-12s %9.9s %9.9s %9.9s %9.9s %12s %12s\n",
			"stressor", "bogo ops", "real time", "usr time", "sys time", "bogo ops/s", "bogo ops/s");
		pr_inf(stdout, "%-12s %9.9s %9.9s %9.9s %9.9s %12s %12s\n",
			"", "", "(secs) ", "(secs) ", "(secs) ", "(real time)", "(usr+sys time)");
		for (i = 0; i < STRESS_MAX; i++) {
			uint64_t c_total = 0, u_total = 0, s_total = 0, us_total;
			double   r_total = 0.0;
			int32_t  n = (i * max_procs);

			for (j = 0; j < started_procs[i]; j++, n++) {
				c_total += shared->stats[n].counter;
				u_total += shared->stats[n].tms.tms_utime + 
					   shared->stats[n].tms.tms_cutime;
				s_total += shared->stats[n].tms.tms_stime + 
					   shared->stats[n].tms.tms_cstime;
				r_total += shared->stats[n].finish - shared->stats[n].start;
			}
			/* Total usr + sys time of all procs */
			us_total = u_total + s_total;
			/* Real time in terms of average wall clock time of all procs */
			r_total = started_procs[i] ? r_total / (double)started_procs[i] : 0.0;

			if ((opt_flags & OPT_FLAGS_METRICS_BRIEF) && (c_total == 0))
				continue;
			pr_inf(stdout, "%-12s %9" PRIu64 " %9.2f %9.2f %9.2f %12.2f %12.2f\n",
				stressors[i].name,
				c_total,				 /* op count */
				r_total,	 			/* average real (wall) clock time */
				(double)u_total / (double)ticks_per_sec, /* actual user time */
				(double)s_total / (double)ticks_per_sec, /* actual system time */
				r_total > 0.0 ? (double)c_total / r_total : 0.0,
				us_total > 0 ? (double)c_total / ((double)us_total / (double)ticks_per_sec) : 0.0);
		}
	}
	free_procs();

	if (num_procs[stress_info_index(STRESS_SEMAPHORE)]) {
		if (sem_destroy(&sem) < 0) {
			pr_err(stderr, "Semaphore destroy failed: errno=%d (%s)\n",
				errno, strerror(errno));
		}
	}
	(void)munmap(shared, len);

	if (opt_flags & OPT_FLAGS_TIMES) {
		struct tms buf;
		double total_cpu_time = opt_nprocessors_online * duration;

		if (times(&buf) == (clock_t)-1) {
			pr_err(stderr, "cannot get run time information: errno=%d (%s)\n",
				errno, strerror(errno));
			exit(EXIT_FAILURE);
		}

		pr_inf(stdout, "for a %.2fs run time:\n", duration);
		pr_inf(stdout, "  %8.2fs available CPU time\n",
			total_cpu_time);
			
		pr_inf(stdout, "  %8.2fs user time   (%6.2f%%)\n",
			(float)buf.tms_cutime / (float)ticks_per_sec,
			100.0 * ((float)buf.tms_cutime / (float)ticks_per_sec) / total_cpu_time);
		pr_inf(stdout, "  %8.2fs system time (%6.2f%%)\n",
			(float)buf.tms_cstime / (float)ticks_per_sec,
			100.0 * ((float)buf.tms_cstime / (float)ticks_per_sec) / total_cpu_time);
		pr_inf(stdout, "  %8.2fs total time  (%6.2f%%)\n",
			((float)buf.tms_cutime + (float)buf.tms_cstime) / (float)ticks_per_sec,
			100.0 * (((float)buf.tms_cutime + (float)buf.tms_cstime) / (float)ticks_per_sec) / total_cpu_time);
	}
	exit(EXIT_SUCCESS);
}
