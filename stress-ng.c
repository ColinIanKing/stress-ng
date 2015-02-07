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
#include <sys/resource.h>

#include "stress-ng.h"

static proc_info_t procs[STRESS_MAX]; 		/* Per stressor process information */

/* Various option settings and flags */
uint64_t opt_sequential = DEFAULT_SEQUENTIAL;	/* Number of sequential iterations */
static int64_t opt_backoff = DEFAULT_BACKOFF;	/* child delay */
static uint32_t opt_class = 0;			/* Which kind of class is specified */
uint64_t opt_timeout = 0;			/* timeout in seconds */
int32_t  opt_flags = PR_ERROR | PR_INFO | OPT_FLAGS_MMAP_MADVISE;
volatile bool opt_do_run = true;		/* false to exit stressor */
volatile bool opt_sigint = false;		/* true if stopped by SIGINT */

/* Scheduler options */
static int opt_sched = UNDEFINED;		/* sched policy */
static int opt_sched_priority = UNDEFINED;	/* sched priority */
static int opt_ionice_class = UNDEFINED;	/* ionice class */
static int opt_ionice_level = UNDEFINED;	/* ionice level */

const char *app_name = "stress-ng";		/* Name of application */
shared_t *shared;				/* shared memory */

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
#ifdef SIGABRT
	SIGABRT,
#endif
#ifdef SIGFPE
	SIGFPE,
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
	STRESSOR(brk, BRK, CLASS_MEMORY | CLASS_OS),
	STRESSOR(bsearch, BSEARCH, CLASS_CPU_CACHE | CLASS_CPU | CLASS_MEMORY),
	STRESSOR(cache, CACHE, CLASS_CPU_CACHE),
	STRESSOR(chmod, CHMOD, CLASS_IO | CLASS_OS),
#if _POSIX_C_SOURCE >= 199309L
	STRESSOR(clock, CLOCK, CLASS_INTERRUPT | CLASS_OS),
#endif
	STRESSOR(cpu, CPU, CLASS_CPU),
	STRESSOR(dentry, DENTRY, CLASS_IO | CLASS_OS),
	STRESSOR(dir, DIR, CLASS_IO | CLASS_OS),
	STRESSOR(dup, DUP, CLASS_IO | CLASS_OS),
#if defined(__linux__)
	STRESSOR(epoll, EPOLL, CLASS_NETWORK | CLASS_OS),
#endif
#if defined(__linux__)
	STRESSOR(eventfd, EVENTFD, CLASS_IO | CLASS_SCHEDULER | CLASS_OS),
#endif
#if _XOPEN_SOURCE >= 600 || _POSIX_C_SOURCE >= 200112L
	STRESSOR(fallocate, FALLOCATE, CLASS_IO | CLASS_OS),
#endif
	STRESSOR(fault, FAULT, CLASS_INTERRUPT | CLASS_SCHEDULER | CLASS_OS),
	STRESSOR(fifo, FIFO, CLASS_IO | CLASS_OS | CLASS_SCHEDULER),
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
#if defined(F_SETLEASE) && defined(F_WRLCK) && defined(F_UNLCK)
	STRESSOR(lease, LEASE, CLASS_IO | CLASS_OS),
#endif
	STRESSOR(link, LINK, CLASS_IO | CLASS_OS),
#if _BSD_SOURCE || _SVID_SOURCE || _XOPEN_SOURCE >= 500 || \
     (_XOPEN_SOURCE && _XOPEN_SOURCE_EXTENDED)
	STRESSOR(lockf, LOCKF, CLASS_IO | CLASS_OS),
#endif
	STRESSOR(lsearch, LSEARCH, CLASS_CPU_CACHE | CLASS_CPU | CLASS_MEMORY),
	STRESSOR(malloc, MALLOC, CLASS_CPU_CACHE | CLASS_MEMORY | CLASS_OS),
	STRESSOR(memcpy, MEMCPY, CLASS_CPU_CACHE | CLASS_MEMORY),
#if (_BSD_SOURCE || _SVID_SOURCE) && !defined(__gnu_hurd__)
	STRESSOR(mincore, MINCORE, CLASS_OS | CLASS_MEMORY),
#endif
	STRESSOR(mmap, MMAP, CLASS_VM | CLASS_IO | CLASS_OS),
#if defined(__linux__)
	STRESSOR(mremap, MREMAP, CLASS_VM | CLASS_OS),
#endif
#if !defined(__gnu_hurd__)
	STRESSOR(msg, MSG, CLASS_SCHEDULER | CLASS_OS),
#endif
#if defined(__linux__)
	STRESSOR(mq, MQ, CLASS_SCHEDULER | CLASS_OS),
#endif
	STRESSOR(nice, NICE, CLASS_SCHEDULER | CLASS_OS),
	STRESSOR(null, NULL, CLASS_IO | CLASS_MEMORY | CLASS_OS),
	STRESSOR(open, OPEN, CLASS_IO | CLASS_OS),
	STRESSOR(pipe, PIPE, CLASS_IO | CLASS_MEMORY | CLASS_OS),
	STRESSOR(poll, POLL, CLASS_SCHEDULER | CLASS_OS),
#if defined (__linux__)
	STRESSOR(procfs, PROCFS, CLASS_IO | CLASS_OS),
#endif
	STRESSOR(pthread, PTHREAD, CLASS_SCHEDULER | CLASS_OS),
	STRESSOR(qsort, QSORT, CLASS_CPU_CACHE | CLASS_CPU | CLASS_MEMORY),
#if defined(STRESS_X86)
	STRESSOR(rdrand, RDRAND, CLASS_CPU),
#endif
	STRESSOR(rename, RENAME, CLASS_IO | CLASS_OS),
	STRESSOR(seek, SEEK, CLASS_IO | CLASS_OS),
	STRESSOR(sem_posix, SEMAPHORE_POSIX, CLASS_OS | CLASS_SCHEDULER),
#if !defined(__gnu_hurd__)
	STRESSOR(sem_sysv, SEMAPHORE_SYSV, CLASS_OS | CLASS_SCHEDULER),
#endif
	STRESSOR(shm_sysv, SHM_SYSV, CLASS_VM | CLASS_OS),
#if defined(__linux__)
	STRESSOR(sendfile, SENDFILE, CLASS_IO | CLASS_OS),
#endif
#if defined(__linux__)
	STRESSOR(sigfd, SIGFD, CLASS_INTERRUPT | CLASS_OS),
#endif
	STRESSOR(sigfpe, SIGFPE, CLASS_OS),
#if _POSIX_C_SOURCE >= 199309L && !defined(__gnu_hurd__)
	STRESSOR(sigq, SIGQUEUE, CLASS_INTERRUPT | CLASS_OS),
#endif
	STRESSOR(sigsegv, SIGSEGV, CLASS_OS),
	STRESSOR(socket, SOCKET, CLASS_NETWORK | CLASS_OS),
#if defined(__linux__)
	STRESSOR(splice, SPLICE, CLASS_IO | CLASS_OS),
#endif
	STRESSOR(stack, STACK, CLASS_CPU | CLASS_MEMORY),
	STRESSOR(switch, SWITCH, CLASS_SCHEDULER | CLASS_OS),
	STRESSOR(symlink, SYMLINK, CLASS_IO | CLASS_OS),
	STRESSOR(sysinfo, SYSINFO, CLASS_OS),
#if defined(__linux__)
	STRESSOR(timer, TIMER, CLASS_INTERRUPT | CLASS_OS),
#endif
	STRESSOR(tsearch, TSEARCH, CLASS_CPU_CACHE | CLASS_CPU | CLASS_MEMORY),
	STRESSOR(udp, UDP, CLASS_IO | CLASS_OS),
#if defined(__linux__) || defined(__gnu_hurd__)
	STRESSOR(urandom, URANDOM, CLASS_IO | CLASS_OS),
#endif
	STRESSOR(utime, UTIME, CLASS_NETWORK | CLASS_OS),
#if defined(STRESS_VECTOR)
	STRESSOR(vecmath, VECMATH, CLASS_CPU),
#endif
#if  _BSD_SOURCE || \
    (_XOPEN_SOURCE >= 500 || _XOPEN_SOURCE && _XOPEN_SOURCE_EXTENDED) && \
    !(_POSIX_C_SOURCE >= 200809L || _XOPEN_SOURCE >= 700)
	STRESSOR(vfork, VFORK, CLASS_SCHEDULER | CLASS_OS),
#endif
	STRESSOR(vm, VM, CLASS_IO | CLASS_VM | CLASS_MEMORY | CLASS_OS),
#if defined (__linux__)
	STRESSOR(vm_rw, VM_RW, CLASS_VM | CLASS_MEMORY | CLASS_OS),
#endif
#if defined(__linux__)
	STRESSOR(vm_splice, VM_SPLICE, CLASS_VM | CLASS_IO | CLASS_OS),
#endif
#if !defined(__gnu_hurd__) && !defined(__NetBSD__)
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
#if defined(__linux__)
	{ "affinity",	1,	0,	OPT_AFFINITY },
	{ "affinity-ops",1,	0,	OPT_AFFINITY_OPS },
	{ "affinity-rand",0,	0,	OPT_AFFINITY_RAND },
#endif
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
	{ "brk",	1,	0,	OPT_BRK },
	{ "brk-ops",	1,	0,	OPT_BRK_OPS },
	{ "brk-notouch",0,	0,	OPT_BRK_NOTOUCH },
	{ "bsearch",	1,	0,	OPT_BSEARCH },
	{ "bsearch-ops",1,	0,	OPT_BSEARCH_OPS },
	{ "bsearch-size",1,	0,	OPT_BSEARCH_SIZE },
	{ "cache",	1,	0, 	OPT_CACHE },
	{ "cache-ops",	1,	0,	OPT_CACHE_OPS },
	{ "chmod",	1,	0, 	OPT_CHMOD },
	{ "chmod-ops",	1,	0,	OPT_CHMOD_OPS },
	{ "cache-flush",0,	0,	OPT_CACHE_FLUSH },
	{ "cache-fence",0,	0,	OPT_CACHE_FENCE },
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
	{ "dentry-order",1,	0,	OPT_DENTRY_ORDER },
	{ "dir",	1,	0,	OPT_DIR },
	{ "dir-ops",	1,	0,	OPT_DIR_OPS },
	{ "dry-run",	0,	0,	OPT_DRY_RUN },
	{ "dup",	1,	0,	OPT_DUP },
	{ "dup-ops",	1,	0,	OPT_DUP_OPS },
#if defined (__linux__)
	{ "epoll",	1,	0,	OPT_EPOLL },
	{ "epoll-ops",	1,	0,	OPT_EPOLL_OPS },
	{ "epoll-port",	1,	0,	OPT_EPOLL_PORT },
	{ "epoll-domain",1,	0,	OPT_EPOLL_DOMAIN },
#endif
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
	{ "fifo",	1,	0,	OPT_FIFO },
	{ "fifo-ops",	1,	0,	OPT_FIFO_OPS },
	{ "fifo-readers",1,	0,	OPT_FIFO_READERS },
	{ "flock",	1,	0,	OPT_FLOCK },
	{ "flock-ops",	1,	0,	OPT_FLOCK_OPS },
	{ "fork",	1,	0,	OPT_FORK },
	{ "fork-ops",	1,	0,	OPT_FORK_OPS },
	{ "fork-max",	1,	0,	OPT_FORK_MAX },
	{ "fstat",	1,	0,	OPT_FSTAT },
	{ "fstat-ops",	1,	0,	OPT_FSTAT_OPS },
	{ "fstat-dir",	1,	0,	OPT_FSTAT_DIR },
#if defined(__linux__)
	{ "futex",	1,	0,	OPT_FUTEX },
	{ "futex-ops",	1,	0,	OPT_FUTEX_OPS },
#endif
	{ "get",	1,	0,	OPT_GET },
	{ "get-ops",	1,	0,	OPT_GET_OPS },
	{ "hdd",	1,	0,	OPT_HDD },
	{ "hdd-ops",	1,	0,	OPT_HDD_OPS },
	{ "hdd-bytes",	1,	0,	OPT_HDD_BYTES },
	{ "hdd-write-size", 1,	0,	OPT_HDD_WRITE_SIZE },
	{ "hdd-opts",	1,	0,	OPT_HDD_OPTS },
	{ "help",	0,	0,	OPT_HELP },
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
#if defined(F_SETLEASE) && defined(F_WRLCK) && defined(F_UNLCK)
	{ "lease",	1,	0,	OPT_LEASE },
	{ "lease-ops",	1,	0,	OPT_LEASE_OPS },
	{ "lease-breakers",1,	0,	OPT_LEASE_BREAKERS },
#endif
	{ "link",	1,	0,	OPT_LINK },
	{ "link-ops",	1,	0,	OPT_LINK_OPS },
#if _BSD_SOURCE || _SVID_SOURCE || _XOPEN_SOURCE >= 500 || \
     (_XOPEN_SOURCE && _XOPEN_SOURCE_EXTENDED)
	{ "lockf",	1,	0,	OPT_LOCKF },
	{ "lockf-ops",	1,	0,	OPT_LOCKF_OPS },
	{ "lockf-nonblock", 0,	0,	OPT_LOCKF_NONBLOCK },
#endif
	{ "lsearch",	1,	0,	OPT_LSEARCH },
	{ "lsearch-ops",1,	0,	OPT_LSEARCH_OPS },
	{ "lsearch-size",1,	0,	OPT_LSEARCH_SIZE },
	{ "malloc",	1,	0,	OPT_MALLOC },
	{ "malloc-bytes",1,	0,	OPT_MALLOC_BYTES },
	{ "malloc-max",	1,	0,	OPT_MALLOC_MAX },
	{ "malloc-ops",	1,	0,	OPT_MALLOC_OPS },
	{ "memcpy",	1,	0,	OPT_MEMCPY },
	{ "memcpy",	1,	0,	OPT_MEMCPY },
	{ "memcpy",	1,	0,	OPT_MEMCPY },
	{ "memcpy-ops",	1,	0,	OPT_MEMCPY_OPS },
	{ "metrics",	0,	0,	OPT_METRICS },
	{ "metrics-brief",0,	0,	OPT_METRICS_BRIEF },
#if (_BSD_SOURCE || _SVID_SOURCE) && !defined(__gnu_hurd__)
	{ "mincore",	1,	0,	OPT_MINCORE },
	{ "mincore-ops",1,	0,	OPT_MINCORE_OPS },
	{ "mincore-random",0,	0,	OPT_MINCORE_RAND },
#endif
	{ "mmap",	1,	0,	OPT_MMAP },
	{ "mmap-ops",	1,	0,	OPT_MMAP_OPS },
	{ "mmap-async",	0,	0,	OPT_MMAP_ASYNC },
	{ "mmap-bytes",	1,	0,	OPT_MMAP_BYTES },
	{ "mmap-file",	0,	0,	OPT_MMAP_FILE },
	{ "mmap-mprotect",0,	0,	OPT_MMAP_MPROTECT },
#if defined(__linux__)
	{ "mremap",	1,	0,	OPT_MREMAP },
	{ "mremap-ops",	1,	0,	OPT_MREMAP_OPS },
	{ "mremap-bytes",1,	0,	OPT_MREMAP_BYTES },
#endif
#if !defined(__gnu_hurd__)
	{ "msg",	1,	0,	OPT_MSG },
	{ "msg-ops",	1,	0,	OPT_MSG_OPS },
#endif
#if defined(__linux__)
	{ "mq",		1,	0,	OPT_MQ },
	{ "mq-ops",	1,	0,	OPT_MQ_OPS },
	{ "mq-size",	1,	0,	OPT_MQ_SIZE },
#endif
	{ "nice",	1,	0,	OPT_NICE },
	{ "nice-ops",	1,	0,	OPT_NICE_OPS },
	{ "no-madvise",	0,	0,	OPT_NO_MADVISE },
	{ "null",	1,	0,	OPT_NULL },
	{ "null-ops",	1,	0,	OPT_NULL_OPS },
	{ "open",	1,	0,	OPT_OPEN },
	{ "open-ops",	1,	0,	OPT_OPEN_OPS },
#if (_BSD_SOURCE || _SVID_SOURCE) && !defined(__gnu_hurd__)
	{ "page-in",	0,	0,	OPT_PAGE_IN },
#endif
	{ "pipe",	1,	0,	OPT_PIPE },
	{ "pipe-ops",	1,	0,	OPT_PIPE_OPS },
	{ "poll",	1,	0,	OPT_POLL },
	{ "poll-ops",	1,	0,	OPT_POLL_OPS },
#if defined (__linux__)
	{ "procfs",	1,	0,	OPT_PROCFS },
	{ "procfs-ops",	1,	0,	OPT_PROCFS_OPS },
#endif
	{ "pthread",	1,	0,	OPT_PTHREAD },
	{ "pthread-ops",1,	0,	OPT_PTHREAD_OPS },
	{ "pthread-max",1,	0,	OPT_PTHREAD_MAX },
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
	{ "sem",	1,	0,	OPT_SEMAPHORE_POSIX },
	{ "sem-ops",	1,	0,	OPT_SEMAPHORE_POSIX_OPS },
	{ "sem-procs",	1,	0,	OPT_SEMAPHORE_POSIX_PROCS },
#if !defined(__gnu_hurd__)
	{ "sem-sysv",	1,	0,	OPT_SEMAPHORE_SYSV },
	{ "sem-sysv-ops",1,	0,	OPT_SEMAPHORE_SYSV_OPS },
	{ "sem-sysv-procs",1,	0,	OPT_SEMAPHORE_SYSV_PROCS },
#endif
#if defined(__linux__)
	{ "sendfile",	1,	0,	OPT_SENDFILE },
	{ "sendfile-ops",1,	0,	OPT_SENDFILE_OPS },
	{ "sendfile-size",1,	0,	OPT_SENDFILE_SIZE },
#endif
	{ "sequential",	1,	0,	OPT_SEQUENTIAL },
	{ "shm-sysv",	1,	0,	OPT_SHM_SYSV },
	{ "shm-sysv-ops",1,	0,	OPT_SHM_SYSV_OPS },
	{ "shm-sysv-bytes",1,	0,	OPT_SHM_SYSV_BYTES },
	{ "shm-sysv-segs",1,	0,	OPT_SHM_SYSV_SEGMENTS },
#if defined(__linux__)
	{ "sigfd",	1,	0,	OPT_SIGFD },
	{ "sigfd-ops",	1,	0,	OPT_SIGFD_OPS },
#endif
	{ "sigfpe",	1,	0,	OPT_SIGFPE },
	{ "sigfpe-ops",	1,	0,	OPT_SIGFPE_OPS },
	{ "sigsegv",	1,	0,	OPT_SIGSEGV },
	{ "sigsegv-ops",1,	0,	OPT_SIGSEGV_OPS },
#if _POSIX_C_SOURCE >= 199309L && !defined(__gnu_hurd__)
	{ "sigq",	1,	0,	OPT_SIGQUEUE },
	{ "sigq-ops",	1,	0,	OPT_SIGQUEUE_OPS },
#endif
	{ "sock",	1,	0,	OPT_SOCKET },
	{ "sock-domain",1,	0,	OPT_SOCKET_DOMAIN },
	{ "sock-ops",	1,	0,	OPT_SOCKET_OPS },
	{ "sock-port",	1,	0,	OPT_SOCKET_PORT },
#if defined (__linux__)
	{ "splice",	1,	0,	OPT_SPLICE },
	{ "splice-bytes",1,	0,	OPT_SPLICE_BYTES },
	{ "splice-ops",	1,	0,	OPT_SPLICE_OPS },
#endif
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
	{ "udp",	1,	0,	OPT_UDP },
	{ "udp-domain",1,	0,	OPT_UDP_DOMAIN },
	{ "udp-ops",	1,	0,	OPT_UDP_OPS },
	{ "udp-port",	1,	0,	OPT_UDP_PORT },
	{ "utime",	1,	0,	OPT_UTIME },
	{ "utime-ops",	1,	0,	OPT_UTIME_OPS },
	{ "utime-fsync",0,	0,	OPT_UTIME_FSYNC },
#if defined (__linux__) || defined(__gnu_hurd__)
	{ "urandom",	1,	0,	OPT_URANDOM },
	{ "urandom-ops",1,	0,	OPT_URANDOM_OPS },
#endif
#if defined(STRESS_VECTOR)
	{ "vecmath",	1,	0,	OPT_VECMATH },
	{ "vecmath-ops",1,	0,	OPT_VECMATH_OPS },
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
#if defined (__linux__)
	{ "vm-rw",	1,	0,	OPT_VM_RW },
	{ "vm-rw-bytes",1,	0,	OPT_VM_RW_BYTES },
	{ "vm-rw-ops",	1,	0,	OPT_VM_RW_OPS },
#endif
#if defined (__linux__)
	{ "vm-splice",	1,	0,	OPT_VM_SPLICE },
	{ "vm-splice-bytes",1,	0,	OPT_VM_SPLICE_BYTES },
	{ "vm-splice-ops",1,	0,	OPT_VM_SPLICE_OPS },
#endif
#if !defined(__gnu_hurd__) && !defined(__NetBSD__)
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
	{ "-h",		"help",			"show help" },
#if defined (__linux__)
	{ NULL,		"affinity N",		"start N workers that rapidly change CPU affinity" },
	{ NULL, 	"affinity-ops N",   	"stop when N affinity bogo operations completed" },
	{ NULL, 	"affinity-rand",   	"change affinity randomly rather than sequentially" },
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
	{ NULL,		"brk N",		"start N workers performing rapid brk calls" },
	{ NULL,		"brk-ops N",		"stop when N brk bogo operations completed" },
	{ NULL,		"brk-notouch",		"don't touch (page in) new data segment page" },
	{ NULL,		"bsearch",		"start N workers that exercise a binary search" },
	{ NULL,		"bsearch-ops",		"stop when N binary search bogo operations completed" },
	{ NULL,		"bsearch-size",		"number of 32 bit integers to bsearch" },
	{ "C N",	"cache N",		"start N CPU cache thrashing workers" },
	{ NULL,		"cache-ops N",		"stop when N cache bogo operations completed (x86 only)" },
	{ NULL,		"cache-flush",		"flush cache after every memory write (x86 only)" },
	{ NULL,		"cache-fence",		"serialize stores" },
	{ NULL,		"class name",		"specify a class of stressors, use with --sequential" },
	{ NULL,		"chmod N",		"start N workers thrashing chmod file mode bits " },
	{ NULL,		"chmod-ops N",		"stop chmod workers after N bogo operations" },
#if _POSIX_C_SOURCE >= 199309L
	{ NULL,		"clock N",		"start N workers thrashing clocks and POSIX timers" },
	{ NULL,		"clock-ops N",		"stop clock workers after N bogo operations" },
#endif
	{ "c N",	"cpu N",		"start N workers spinning on sqrt(rand())" },
	{ NULL,		"cpu-ops N",		"stop when N cpu bogo operations completed" },
	{ "l P",	"cpu-load P",		"load CPU by P %%, 0=sleep, 100=full load (see -c)" },
	{ NULL,		"cpu-method m",		"specify stress cpu method m, default is all" },
	{ "D N",	"dentry N",		"start N dentry thrashing stressors" },
	{ NULL,		"dentry-ops N",		"stop when N dentry bogo operations completed" },
	{ NULL,		"dentry-order O",	"specify dentry unlink order (reverse, forward, stride)" },
	{ NULL,		"dentries N",		"create N dentries per iteration" },
	{ NULL,		"dir N",		"start N directory thrashing stressors" },
	{ NULL,		"dir-ops N",		"stop when N directory bogo operations completed" },
	{ "n",		"dry-run",		"do not run" },
	{ NULL,		"dup N",		"start N workers exercising dup/close" },
	{ NULL,		"dup-ops N",		"stop when N dup/close bogo operations completed" },
#if defined (__linux__)
	{ NULL,		"epoll N",		"start N workers doing epoll handled socket activity" },
	{ NULL,		"epoll-ops N",		"stop when N epoll bogo operations completed" },
	{ NULL,		"epoll-port P",		"use socket ports P upwards" },
	{ NULL,		"epoll-domain D",	"specify socket domain, default is unix" },
#endif
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
	{ NULL,		"fifo N",		"start N workers exercising fifo I/O" },
	{ NULL,		"fifo-ops N",		"stop when N fifo bogo operations completed" },
	{ NULL,		"fifo-readers N",	"number of fifo reader stessors to start" },
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
	{ NULL,		"hdd-opts list",	"specify list of various stressor options" },
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
#if defined(F_SETLEASE) && defined(F_WRLCK) && defined(F_UNLCK)
	{ NULL,		"lease N",		"start N workers holding and breaking a lease" },
	{ NULL,		"lease-ops N",		"stop when N lease bogo operations completed" },
	{ NULL,		"lease-breakers N",	"number of lease breaking processes to start" },
#endif
	{ NULL,		"link N",		"start N workers creating hard links" },
	{ NULL,		"link-ops N",		"stop when N link bogo operations completed" },
#if _BSD_SOURCE || _SVID_SOURCE || _XOPEN_SOURCE >= 500 || \
     (_XOPEN_SOURCE && _XOPEN_SOURCE_EXTENDED)
	{ NULL,		"lockf N",		"start N workers locking a single file via lockf" },
	{ NULL,		"lockf-ops N",		"stop when N lockf bogo operations completed" },
	{ NULL,		"lockf-nonblock",	"don't block if lock cannot be obtained, re-try" },
#endif
	{ NULL,		"lsearch",		"start N workers that exercise a linear search" },
	{ NULL,		"lsearch-ops",		"stop when N linear search bogo operations completed" },
	{ NULL,		"lsearch-size",		"number of 32 bit integers to lsearch" },
	{ NULL,		"malloc N",		"start N workers exercising malloc/realloc/free" },
	{ NULL,		"malloc-bytes N",	"allocate up to N bytes per allocation" },
	{ NULL,		"malloc-max N",		"keep up to N allocations at a time" },
	{ NULL,		"malloc-ops N",		"stop when N malloc bogo operations completed" },
	{ "M",		"metrics",		"print pseudo metrics of activity" },
	{ NULL,		"metrics-brief",	"enable metrics and only show non-zero results" },
	{ NULL,		"memcpy N",		"start N workers performing memory copies" },
	{ NULL,		"memcpy-ops N",		"stop when N memcpy bogo operations completed" },
#if (_BSD_SOURCE || _SVID_SOURCE) && !defined(__gnu_hurd__)
	{ NULL,		"mincore N",		"start N workers exercising mincore" },
	{ NULL,		"mincore-ops N",	"stop when N mimcore bogo operations completed" },
	{ NULL,		"mincore-random",	"randomly select pages rather than linear scan" },
#endif
	{ NULL,		"mmap N",		"start N workers stressing mmap and munmap" },
	{ NULL,		"mmap-ops N",		"stop when N mmap bogo operations completed" },
	{ NULL,		"mmap-async",		"using asynchronous msyncs for file based mmap" },
	{ NULL,		"mmap-bytes N",		"mmap and munmap N bytes for each stress iteration" },
	{ NULL,		"mmap-file",		"mmap onto a file using synchronous msyncs" },
	{ NULL,		"mmap-mprotect",	"enable mmap mprotect stressing" },
#if defined(__linux__)
	{ NULL,		"mremap N",		"start N workers stressing mremap" },
	{ NULL,		"mremap-ops N",		"stop when N mremap bogo operations completed" },
	{ NULL,		"mremap-bytes N",	"mremap N bytes maximum for each stress iteration" },
#endif
	{ NULL,		"msg N",		"start N workers passing messages using System V messages" },
	{ NULL,		"msg-ops N",		"stop msg workers after N bogo messages completed" },
#if defined(__linux__)
	{ NULL,		"mq N",			"start N workers passing messages using POSIX messages" },
	{ NULL,		"mq-ops N",		"stop mq workers after N bogo messages completed" },
	{ NULL,		"mq-size N",		"specify the size of the POSIX message queue" },
#endif
	{ NULL,		"nice N",		"start N workers that randomly re-adjust nice levels" },
	{ NULL,		"nice-ops N",		"stop when N nice bogo operations completed" },
	{ NULL,		"no-madvise",		"don't use random madvise options for each mmap" },
	{ NULL,		"null N",		"start N workers writing to /dev/null" },
	{ NULL,		"null-ops N",		"stop when N /dev/null bogo write operations completed" },
	{ "o",		"open N",		"start N workers exercising open/close" },
	{ NULL,		"open-ops N",		"stop when N open/close bogo operations completed" },
#if (_BSD_SOURCE || _SVID_SOURCE) && !defined(__gnu_hurd__)
	{ NULL,		"page-in",		"touch allocated pages that are not in core" },
#endif
	{ "p N",	"pipe N",		"start N workers exercising pipe I/O" },
	{ NULL,		"pipe-ops N",		"stop when N pipe I/O bogo operations completed" },
	{ "P N",	"poll N",		"start N workers exercising zero timeout polling" },
	{ NULL,		"poll-ops N",		"stop when N poll bogo operations completed" },
#if defined (__linux__)
	{ NULL,		"procfs N",		"start N workers reading portions of /proc" },
	{ NULL,		"procfs-ops N",		"stop procfs workers after N bogo read operations" },
#endif
	{ NULL,		"pthread N",		"start N workers that create multiple threads" },
	{ NULL,		"pthread-ops N",	"stop pthread workers after N bogo threads created" },
	{ NULL,		"pthread-max P",	"create P threads at a time by each worker" },
	{ "Q",		"qsort N",		"start N workers exercising qsort on 32 bit random integers" },
	{ NULL,		"qsort-ops N",		"stop when N qsort bogo operations completed" },
	{ NULL,		"qsort-size N",		"number of 32 bit integers to sort" },
	{ "q",		"quiet",		"quiet output" },
	{ "r",		"random N",		"start N random workers" },
#if defined(STRESS_X86)
	{ NULL,		"rdrand N",		"start N workers exercising rdrand instruction (x86 only)" },
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
	{ NULL,		"sem-procs N",		"number of processes to start per worker" },
#if !defined(__gnu_hurd__)
	{ NULL,		"sem-sysv N",		"start N workers doing System V semaphore operations" },
	{ NULL,		"sem-sysv-ops N",	"stop when N System V semaphore bogo operations completed" },
	{ NULL,		"sem-sysv-procs N",	"number of processes to start per worker" },
#endif
#if defined (__linux__)
	{ NULL,		"sendfile N",		"start N workers exercising sendfile" },
	{ NULL,		"sendfile-ops N",	"stop after N bogo sendfile operations" },
	{ NULL,		"sendfile-size N",	"size of data to be sent with sendfile" },
#endif
	{ NULL,		"sequential N",		"run all stressors one by one, invoking N of them" },
	{ NULL,		"shm-sysv N",		"start N workers that exercise System V shared memory" },
	{ NULL,		"shm-sysv-ops N",	"stop after N shared memory bogo operations" },
	{ NULL,		"shm-sysv-bytes N",	"allocate and free N bytes of shared memory per iteration" },
	{ NULL,		"shm-sysv-segs N",	"allocate N shared memory segments per iteration" },
#if defined (__linux__)
	{ NULL,		"sigfd N",		"start N workers reading signals via signalfd reads " },
	{ NULL,		"sigfd-ops N",		"stop when N bogo signalfd reads completed" },
#endif
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
	{ NULL,		"sock-domain D",	"specify socket domain, default is ipv4" },
#if defined (__linux__)
	{ NULL,		"splice N",		"start N workers reading/writing using splice" },
	{ NULL,		"splice-ops N",		"stop when N bogo splice operations completed" },
	{ NULL,		"splice-bytes N",	"number of bytes to transfer per splice call" },
#endif
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
	{ NULL,		"udp N",		"start N workers performing UDP send/receives " },
	{ NULL,		"udp-ops N",		"stop when N udp bogo operations completed" },
	{ NULL,		"udp-port P",		"use ports P to P + number of workers - 1" },
	{ NULL,		"udo-domain D",		"specify domain, default is ipv4" },
#if defined(__linux__) || defined(__gnu_hurd__)
	{ "u N",	"urandom N",		"start N workers reading /dev/urandom" },
	{ NULL,		"urandom-ops N",	"stop when N urandom bogo read operations completed" },
#endif
	{ NULL,		"utime N",		"start N workers updating file timestamps" },
	{ NULL,		"utime-ops N",		"stop after N utime bogo operations completed" },
	{ NULL,		"utime-fsync",		"force utime meta data sync to the file system" },
#if defined(STRESS_VECTOR)
	{ NULL,		"vecmath N",		"start N workers performing vector math ops" },
	{ NULL,		"vecmath-ops N",	"stop after N vector math bogo operations completed" },
#endif
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
#if defined (__linux__)
	{ NULL,		"vm-rw N",		"start N vm read/write process_vm* copy workers" },
	{ NULL,		"vm-rw-bytes N",	"transfer N bytes of memory per bogo operation" },
	{ NULL,		"vm-rw-ops N",		"stop after N vm process_vm* copy bogo operations" },
#endif
#if defined (__linux__)
	{ NULL,		"vm-splice N",		"start N workers reading/writing using vmsplice" },
	{ NULL,		"vm-splice-ops N",	"stop when N bogo splice operations completed" },
	{ NULL,		"vm-splice-bytes N",	"number of bytes to transfer per vmsplice call" },
#endif
#if !defined(__gnu_hurd__) && !defined(__NetBSD__)
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

/*
 *  stressor_id_find()
 *  	Find index into stressors by id
 */
static inline int stressor_id_find(const stress_id id)
{
	int i;

	for (i = 0; stressors[i].name; i++)
		if (stressors[i].id == id)
			break;

	return i;       /* End of array is a special "NULL" entry */
}

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
		printf(" %-6s--%-17s%s\n", opt_s,
			help[i].opt_l, help[i].description);
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
static inline void proc_finished(pid_t *pid)
{
	*pid = 0;
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
		for (j = 0; j < procs[i].started_procs; j++) {
			if (procs[i].pids[j])
				(void)kill(procs[i].pids[j], sig);
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
		for (j = 0; j < procs[i].started_procs; j++) {
			pid_t pid;
redo:
			pid = procs[i].pids[j];
			if (pid) {
				int status, ret;

				ret = waitpid(pid, &status, 0);
				if (ret > 0) {
					if (WEXITSTATUS(status)) {
						pr_err(stderr, "Process %d terminated with an error, exit status=%d\n",
							ret, WEXITSTATUS(status));
						*success = false;
					}
					proc_finished(&procs[i].pids[j]);
					pr_dbg(stderr, "process [%d] terminated\n", ret);
				} else if (ret == -1) {
					/* Somebody interrupted the wait */
					if (errno == EINTR)
						goto redo;
					/* This child did not exist, mark it done anyhow */
					if (errno == ECHILD)
						proc_finished(&procs[i].pids[j]);
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
long int opt_long(const char *opt, const char *str)
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
		free(procs[i].pids);
}

/*
 *  stress_run ()
 *	kick off and run stressors
 */
void stress_run(
	const int total_procs,
	const int32_t max_procs,
	proc_stats_t stats[],
	double *duration,
	bool *success
)
{
	double time_start, time_finish;
	int32_t n_procs, i, j, n;

	time_start = time_now();
	pr_dbg(stderr, "starting stressors\n");
	for (n_procs = 0; n_procs < total_procs; n_procs++) {
		for (i = 0; i < STRESS_MAX; i++) {
			if (time_now() - time_start > opt_timeout)
				goto abort;

			j = procs[i].started_procs;
			if (j < procs[i].num_procs) {
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
					free_procs();
					if (stress_sethandler(name) < 0)
						exit(EXIT_FAILURE);

					(void)alarm(opt_timeout);
					mwc_reseed();
					set_oom_adjustment(name, false);
					set_coredump(name);
					set_max_limits();
					snprintf(name, sizeof(name), "%s-%s", app_name,
						munge_underscore((char *)stressors[i].name));
					set_iopriority(opt_ionice_class, opt_ionice_level);
					set_proc_name(name);
					pr_dbg(stderr, "%s: started [%d] (instance %" PRIu32 ")\n",
						name, getpid(), j);

					n = (i * max_procs) + j;
					stats[n].start = stats[n].finish = time_now();

					(void)usleep(opt_backoff * n_procs);
					if (opt_do_run && !(opt_flags & OPT_FLAGS_DRY_RUN))
						rc = stressors[i].stress_func(&stats[n].counter, j, procs[i].bogo_ops, name);
					stats[n].finish = time_now();
					if (times(&stats[n].tms) == (clock_t)-1) {
						pr_dbg(stderr, "times failed: errno=%d (%s)\n",
							errno, strerror(errno));
					}
					pr_dbg(stderr, "%s: exited [%d] (instance %" PRIu32 ")\n",
						name, getpid(), j);
					exit(rc);
				default:
					procs[i].pids[j] = pid;
					procs[i].started_procs++;

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
	pr_dbg(stderr, "%d stressors running\n", n_procs);

wait_for_procs:
	wait_procs(success);
	time_finish = time_now();

	*duration += time_finish - time_start;
}

int main(int argc, char **argv)
{
	double duration = 0.0;
	int32_t val, opt_random = 0, i, j;
	int32_t total_procs = 0, max_procs = 0;
	size_t len;
	bool success = true, previous = false;
	struct sigaction new_action;
	long int ticks_per_sec;
	struct rlimit limit;
	int id;

	memset(procs, 0, sizeof(procs));
	mwc_reseed();

	(void)stress_get_pagesize();
	(void)stress_set_cpu_method("all");
	(void)stress_set_vm_method("all");

	if (stress_get_processors_online() < 0) {
		pr_err(stderr, "sysconf failed, number of cpus online unknown: errno=%d: (%s)\n",
			errno, strerror(errno));
		exit(EXIT_FAILURE);
	}
	ticks_per_sec = stress_get_ticks_per_second();
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
				procs[id].num_procs = opt_long(name, optarg);
				if (procs[id].num_procs <= 0)
					procs[id].num_procs = stress_get_processors_online();
				check_value(name, procs[id].num_procs);
				goto next_opt;
			}
			if (stressors[id].op == (stress_op)c) {
				procs[id].bogo_ops = get_uint64(optarg);
				check_range(opt_name(c), procs[id].bogo_ops,
					MIN_OPS, MAX_OPS);
				goto next_opt;
			}
		}

		switch (c) {
#if defined(__linux__)
		case OPT_AIO_REQUESTS:
			stress_set_aio_requests(optarg);
			break;
#endif
		case OPT_ALL:
			opt_flags |= OPT_FLAGS_SET;
			val = opt_long("-a", optarg);
			if (val <= 0)
				val = stress_get_processors_online();
			check_value("all", val);
			for (i = 0; i < STRESS_MAX; i++)
				procs[i].num_procs = val;
			break;
#if defined(__linux__)
		case OPT_AFFINITY_RAND:
			opt_flags |= OPT_FLAGS_AFFINITY_RAND;
			break;
#endif
		case OPT_BACKOFF:
			opt_backoff = opt_long("backoff", optarg);
			break;
		case OPT_BIGHEAP_GROWTH:
			stress_set_bigheap_growth(optarg);
			break;
		case OPT_BRK_NOTOUCH:
			opt_flags |= OPT_FLAGS_BRK_NOTOUCH;
			break;
		case OPT_BSEARCH_SIZE:
			stress_set_bsearch_size(optarg);
			break;
		case OPT_CACHE_FLUSH:
			opt_flags |= OPT_FLAGS_CACHE_FLUSH;
			break;
		case OPT_CACHE_FENCE:
			opt_flags |= OPT_FLAGS_CACHE_FENCE;
			break;
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
		case OPT_CPU_LOAD:
			stress_set_cpu_load(optarg);
			break;
		case OPT_CPU_METHOD:
			if (stress_set_cpu_method(optarg) < 0)
				exit(EXIT_FAILURE);
			break;
		case OPT_DRY_RUN:
			opt_flags |= OPT_FLAGS_DRY_RUN;
			break;
		case OPT_DENTRIES:
			stress_set_dentries(optarg);
			break;
		case OPT_DENTRY_ORDER:
			if (stress_set_dentry_order(optarg) < 0)
				exit(EXIT_FAILURE);
			break;
#if defined (__linux__)
		case OPT_EPOLL_DOMAIN:
			if (stress_set_epoll_domain(optarg) < 0)
				exit(EXIT_FAILURE);
			break;
		case OPT_EPOLL_PORT:
			stress_set_epoll_port(optarg);
			break;
#endif
		case OPT_FIFO_READERS:
			stress_set_fifo_readers(optarg);
			break;
		case OPT_FORK_MAX:
			stress_set_fork_max(optarg);
			break;
		case OPT_FSTAT_DIR:
			stress_set_fstat_dir(optarg);
			break;
		case OPT_HELP:
			usage();
			break;
		case OPT_HDD_BYTES:
			stress_set_hdd_bytes(optarg);
			break;
		case OPT_HDD_OPTS:
			if (stress_hdd_opts(optarg) < 0)
				exit(EXIT_FAILURE);
			break;
		case OPT_HDD_WRITE_SIZE:
			stress_set_hdd_write_size(optarg);
			break;
		case OPT_HSEARCH_SIZE:
			stress_set_hsearch_size(optarg);
			break;
#if defined (__linux__)
		case OPT_IONICE_CLASS:
			opt_ionice_class = get_opt_ionice_class(optarg);
			break;
		case OPT_IONICE_LEVEL:
			opt_ionice_level = get_int(optarg);
			break;
#endif
		case OPT_KEEP_NAME:
			opt_flags |= OPT_FLAGS_KEEP_NAME;
			break;
#if defined(F_SETLEASE) && defined(F_WRLCK) && defined(F_UNLCK)
		case OPT_LEASE_BREAKERS:
			stress_set_lease_breakers(optarg);
			break;
#endif
#if _BSD_SOURCE || _SVID_SOURCE || _XOPEN_SOURCE >= 500 || \
     (_XOPEN_SOURCE && _XOPEN_SOURCE_EXTENDED)
		case OPT_LOCKF_NONBLOCK:
			opt_flags |= OPT_FLAGS_LOCKF_NONBLK;
			break;
#endif
		case OPT_LSEARCH_SIZE:
			stress_set_lsearch_size(optarg);
			break;
		case OPT_MALLOC_BYTES:
			stress_set_malloc_bytes(optarg);
			break;
		case OPT_MALLOC_MAX:
			stress_set_malloc_max(optarg);
			break;
		case OPT_METRICS:
			opt_flags |= OPT_FLAGS_METRICS;
			break;
		case OPT_METRICS_BRIEF:
			opt_flags |= (OPT_FLAGS_METRICS_BRIEF | OPT_FLAGS_METRICS);
			break;
#if (_BSD_SOURCE || _SVID_SOURCE) && !defined(__gnu_hurd__)
		case OPT_MINCORE_RAND:
			opt_flags |= OPT_FLAGS_MINCORE_RAND;
			break;
#endif
		case OPT_MMAP_ASYNC:
			opt_flags |= (OPT_FLAGS_MMAP_FILE | OPT_FLAGS_MMAP_ASYNC);
			break;
		case OPT_MMAP_BYTES:
			stress_set_mmap_bytes(optarg);
			break;
		case OPT_MMAP_FILE:
			opt_flags |= OPT_FLAGS_MMAP_FILE;
			break;
		case OPT_MMAP_MPROTECT:
			opt_flags |= OPT_FLAGS_MMAP_MPROTECT;
			break;
#if defined(__linux__)
		case OPT_MREMAP_BYTES:
			stress_set_mremap_bytes(optarg);
			break;
#endif
#if defined(__linux__)
		case OPT_MQ_SIZE:
			stress_set_mq_size(optarg);
			break;
#endif
		case OPT_NO_MADVISE:
			opt_flags &= ~OPT_FLAGS_MMAP_MADVISE;
			break;
#if (_BSD_SOURCE || _SVID_SOURCE) && !defined(__gnu_hurd__)
		case OPT_PAGE_IN:
			opt_flags |= OPT_FLAGS_MMAP_MINCORE;
			break;
#endif
		case OPT_PTHREAD_MAX:
			stress_set_pthread_max(optarg);
			break;
		case OPT_QSORT_INTEGERS:
			stress_set_qsort_size(optarg);
			break;
		case OPT_QUERY:
			printf("Try '%s --help' for more information.\n", app_name);
			exit(EXIT_FAILURE);
			break;
		case OPT_QUIET:
			opt_flags &= ~(PR_ALL);
			break;
		case OPT_RANDOM:
			opt_flags |= OPT_FLAGS_RANDOM;
			opt_random = opt_long("-r", optarg);
			check_value("random", opt_random);
			break;
		case OPT_SCHED:
			opt_sched = get_opt_sched(optarg);
			break;
		case OPT_SCHED_PRIO:
			opt_sched_priority = get_int(optarg);
			break;
		case OPT_SEEK_SIZE:
			stress_set_seek_size(optarg);
			break;
		case OPT_SEMAPHORE_POSIX_PROCS:
			stress_set_semaphore_posix_procs(optarg);
			break;
#if !defined(__gnu_hurd__)
		case OPT_SEMAPHORE_SYSV_PROCS:
			stress_set_semaphore_sysv_procs(optarg);
			break;
#endif
#if defined (__linux__)
		case OPT_SENDFILE_SIZE:
			stress_set_sendfile_size(optarg);
			break;
#endif
		case OPT_SEQUENTIAL:
			opt_sequential = get_uint64_byte(optarg);
			if (opt_sequential <= 0)
				opt_sequential = stress_get_processors_online();
			check_range("sequential", opt_sequential,
				MIN_SEQUENTIAL, MAX_SEQUENTIAL);
			break;
		case OPT_SHM_SYSV_BYTES:
			stress_set_shm_sysv_bytes(optarg);
			break;
		case OPT_SHM_SYSV_SEGMENTS:
			stress_set_shm_sysv_segments(optarg);
			break;
		case OPT_SOCKET_DOMAIN:
			if (stress_set_socket_domain(optarg) < 0)
				exit(EXIT_FAILURE);
			break;
		case OPT_SOCKET_PORT:
			stress_set_socket_port(optarg);
			break;
#if defined (__linux__)
		case OPT_SPLICE_BYTES:
			stress_set_splice_bytes(optarg);
			break;
#endif
		case OPT_TIMEOUT:
			opt_timeout = get_uint64_time(optarg);
			break;
#if defined (__linux__)
		case OPT_TIMER_FREQ:
			stress_set_timer_freq(optarg);
			break;
#endif
		case OPT_TIMES:
			opt_flags |= OPT_FLAGS_TIMES;
			break;
		case OPT_TSEARCH_SIZE:
			stress_set_tsearch_size(optarg);
			break;
		case OPT_UDP_DOMAIN:
			if (stress_set_udp_domain(optarg) < 0)
				exit(EXIT_FAILURE);
			break;
		case OPT_UDP_PORT:
			stress_set_udp_port(optarg);
			break;
		case OPT_UTIME_FSYNC:
			opt_flags |= OPT_FLAGS_UTIME_FSYNC;
			break;
		case OPT_VERBOSE:
			opt_flags |= PR_ALL;
			break;
#if  _BSD_SOURCE || \
    (_XOPEN_SOURCE >= 500 || _XOPEN_SOURCE && _XOPEN_SOURCE_EXTENDED) && \
    !(_POSIX_C_SOURCE >= 200809L || _XOPEN_SOURCE >= 700)
		case OPT_VFORK_MAX:
			stress_set_vfork_max(optarg);
			break;
#endif
		case OPT_VERIFY:
			opt_flags |= (OPT_FLAGS_VERIFY | PR_FAIL);
			break;
		case OPT_VERSION:
			version();
			exit(EXIT_SUCCESS);
		case OPT_VM_BYTES:
			stress_set_vm_bytes(optarg);
			break;
		case OPT_VM_HANG:
			stress_set_vm_hang(optarg);
			break;
		case OPT_VM_KEEP:
			stress_set_vm_flags(OPT_FLAGS_VM_KEEP);
			break;
		case OPT_VM_METHOD:
			if (stress_set_vm_method(optarg) < 0)
				exit(EXIT_FAILURE);
			break;
#ifdef MAP_LOCKED
		case OPT_VM_MMAP_LOCKED:
			stress_set_vm_flags(MAP_LOCKED);
			break;
#endif
#ifdef MAP_POPULATE
		case OPT_VM_MMAP_POPULATE:
			stress_set_vm_flags(MAP_POPULATE);
			break;
#endif
#if defined (__linux__)
		case OPT_VM_RW_BYTES:
			stress_set_vm_rw_bytes(optarg);
			break;
#endif
#if defined (__linux__)
		case OPT_VM_SPLICE_BYTES:
			stress_set_vm_splice_bytes(optarg);
			break;
#endif
		default:
			printf("Unknown option\n");
			exit(EXIT_FAILURE);
		}
	}

	if (opt_class && !opt_sequential) {
		fprintf(stderr, "class option is only used with sequential option\n");
		exit(EXIT_FAILURE);
	}

	pr_dbg(stderr, "%ld processors online\n", stress_get_processors_online());

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
			procs[mwc() % STRESS_MAX].num_procs += rnd;
		}
	}

	set_oom_adjustment("main", false);
	set_coredump("main");
	set_sched(opt_sched, opt_sched_priority);
	set_iopriority(opt_ionice_class, opt_ionice_level);

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

	for (i = 0; i < STRESS_MAX; i++)
		total_procs += procs[i].num_procs;

	if (opt_sequential) {
		if (total_procs) {
			pr_err(stderr, "sequential option cannot be specified with other stressors enabled\n");
			free_procs();
			exit(EXIT_FAILURE);
		}
		if (opt_timeout == 0) {
			opt_timeout = 60;
			pr_inf(stdout, "defaulting to a %" PRIu64 " second run per stressor\n", opt_timeout);
		}

		/* Sequential mode has no bogo ops threshold */
		for (i = 0; i < STRESS_MAX; i++) {
			procs[i].bogo_ops = 0;
			procs[i].pids = calloc(opt_sequential, sizeof(pid_t));
			if (!procs[i].pids) {
				pr_err(stderr, "cannot allocate pid list\n");
				free_procs();
				exit(EXIT_FAILURE);
			}
		}
		max_procs = opt_sequential;
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

		/* Share bogo ops between processes equally */
		for (i = 0; i < STRESS_MAX; i++) {
			procs[i].bogo_ops = procs[i].num_procs ?
				procs[i].bogo_ops / procs[i].num_procs : 0;
			procs[i].pids = NULL;

			if (max_procs < procs[i].num_procs)
				max_procs = procs[i].num_procs;
			if (procs[i].num_procs) {
				procs[i].pids = calloc(procs[i].num_procs, sizeof(pid_t));
				if (!procs[i].pids) {
					pr_err(stderr, "cannot allocate pid list\n");
					free_procs();
					exit(EXIT_FAILURE);
				}
			}
		}
	}

	id = stressor_id_find(STRESS_PTHREAD);
	if (procs[id].num_procs &&
	    (getrlimit(RLIMIT_NPROC, &limit) == 0)) {
		uint64_t max = (uint64_t)limit.rlim_cur / procs[id].num_procs;
		stress_adjust_ptread_max(max);
	}

	pr_inf(stdout, "dispatching hogs:");
	for (i = 0; i < STRESS_MAX; i++) {
		if (procs[i].num_procs) {
			fprintf(stdout, "%s %" PRId32 " %s",
				previous ? "," : "",
				procs[i].num_procs, stressors[i].name);
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

	id = stressor_id_find(STRESS_SEMAPHORE_POSIX);
	if (procs[id].num_procs || opt_sequential)
		stress_semaphore_posix_init();

#if !defined(__gnu_hurd__)
	id = stressor_id_find(STRESS_SEMAPHORE_SYSV);
	if (procs[id].num_procs || opt_sequential)
		stress_semaphore_sysv_init();
#endif

	if (opt_sequential) {
		/*
		 *  Step through each stressor one by one
		 */
		for (i = 0; opt_do_run && i < STRESS_MAX; i++) {
			int32_t j;

			for (j = 0; opt_do_run && j < STRESS_MAX; j++)
				procs[i].num_procs = 0;
			procs[i].num_procs = opt_class ?
				(stressors[i].class & opt_class ?
					opt_sequential : 0) : opt_sequential;
			if (procs[i].num_procs)
				stress_run(opt_sequential, opt_sequential, shared->stats, &duration, &success);
		}
	} else {
		/*
		 *  Run all stressors in parallel
		 */
		stress_run(total_procs, max_procs, shared->stats, &duration, &success);
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

			for (j = 0; j < procs[i].started_procs; j++, n++) {
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
			r_total = procs[i].started_procs ? r_total / (double)procs[i].started_procs : 0.0;

			if ((opt_flags & OPT_FLAGS_METRICS_BRIEF) && (c_total == 0))
				continue;
			pr_inf(stdout, "%-12s %9" PRIu64 " %9.2f %9.2f %9.2f %12.2f %12.2f\n",
				munge_underscore((char *)stressors[i].name),
				c_total,				 /* op count */
				r_total,	 			/* average real (wall) clock time */
				(double)u_total / (double)ticks_per_sec, /* actual user time */
				(double)s_total / (double)ticks_per_sec, /* actual system time */
				r_total > 0.0 ? (double)c_total / r_total : 0.0,
				us_total > 0 ? (double)c_total / ((double)us_total / (double)ticks_per_sec) : 0.0);
		}
	}
	free_procs();

	stress_semaphore_posix_destroy();
#if !defined(__gnu_hurd__)
	stress_semaphore_sysv_destroy();
#endif
	(void)munmap(shared, len);

	if (opt_flags & OPT_FLAGS_TIMES) {
		struct tms buf;
		double total_cpu_time = stress_get_processors_online() * duration;

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
			100.0 * ((float)buf.tms_cutime /
				(float)ticks_per_sec) / total_cpu_time);
		pr_inf(stdout, "  %8.2fs system time (%6.2f%%)\n",
			(float)buf.tms_cstime / (float)ticks_per_sec,
			100.0 * ((float)buf.tms_cstime /
				(float)ticks_per_sec) / total_cpu_time);
		pr_inf(stdout, "  %8.2fs total time  (%6.2f%%)\n",
			((float)buf.tms_cutime + (float)buf.tms_cstime) /
				(float)ticks_per_sec,
			100.0 * (((float)buf.tms_cutime +
				  (float)buf.tms_cstime) /
				(float)ticks_per_sec) / total_cpu_time);
	}
	exit(EXIT_SUCCESS);
}
