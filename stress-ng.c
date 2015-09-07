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
#include <syslog.h>

#include <sys/mman.h>
#include <sys/types.h>
#include <sys/times.h>
#include <sys/wait.h>
#include <sys/resource.h>

#include "stress-ng.h"

#if defined(STRESS_AFFINITY)
#include <sched.h>
#endif

static proc_info_t procs[STRESS_MAX]; 		/* Per stressor process information */

/* Various option settings and flags */
int32_t opt_sequential = DEFAULT_SEQUENTIAL;	/* Number of sequential workers */
int32_t opt_all = 0;				/* Number of concurrent workers */
uint64_t opt_timeout = 0;			/* timeout in seconds */
uint64_t opt_flags = PR_ERROR | PR_INFO | OPT_FLAGS_MMAP_MADVISE;
volatile bool opt_do_run = true;		/* false to exit stressor */
volatile bool opt_do_wait = true;		/* false to exit run waiter loop */
volatile bool opt_sigint = false;		/* true if stopped by SIGINT */

/* Scheduler options */

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
#if defined(STRESS_AFFINITY)
	STRESSOR(affinity, AFFINITY, CLASS_SCHEDULER),
#endif
#if defined(STRESS_AIO)
	STRESSOR(aio, AIO, CLASS_IO | CLASS_INTERRUPT | CLASS_OS),
#endif
#if defined(STRESS_AIO_LINUX)
	STRESSOR(aio_linux, AIO_LINUX, CLASS_IO | CLASS_INTERRUPT | CLASS_OS),
#endif
	STRESSOR(bigheap, BIGHEAP, CLASS_OS | CLASS_VM),
	STRESSOR(brk, BRK, CLASS_OS | CLASS_VM),
	STRESSOR(bsearch, BSEARCH, CLASS_CPU_CACHE | CLASS_CPU | CLASS_MEMORY),
	STRESSOR(cache, CACHE, CLASS_CPU_CACHE),
	STRESSOR(chdir, CHDIR, CLASS_FILESYSTEM | CLASS_OS),
	STRESSOR(chmod, CHMOD, CLASS_FILESYSTEM | CLASS_OS),
#if defined(STRESS_CLOCK)
	STRESSOR(clock, CLOCK, CLASS_INTERRUPT | CLASS_OS),
#endif
#if defined(STRESS_CLONE)
	STRESSOR(clone, CLONE, CLASS_SCHEDULER | CLASS_OS),
#endif
#if defined(STRESS_CONTEXT)
	STRESSOR(context, CONTEXT, CLASS_MEMORY | CLASS_CPU),
#endif
	STRESSOR(cpu, CPU, CLASS_CPU),
	STRESSOR(crypt, CRYPT, CLASS_CPU),
	STRESSOR(dentry, DENTRY, CLASS_FILESYSTEM | CLASS_OS),
	STRESSOR(dir, DIR, CLASS_FILESYSTEM | CLASS_OS),
	STRESSOR(dup, DUP, CLASS_FILESYSTEM | CLASS_OS),
#if defined(STRESS_EPOLL)
	STRESSOR(epoll, EPOLL, CLASS_NETWORK | CLASS_OS),
#endif
#if defined(STRESS_EVENTFD)
	STRESSOR(eventfd, EVENTFD, CLASS_FILESYSTEM | CLASS_SCHEDULER | CLASS_OS),
#endif
#if defined(STRESS_FALLOCATE)
	STRESSOR(fallocate, FALLOCATE, CLASS_FILESYSTEM | CLASS_OS),
#endif
	STRESSOR(fault, FAULT, CLASS_INTERRUPT | CLASS_SCHEDULER | CLASS_OS),
	STRESSOR(fcntl, FCNTL, CLASS_FILESYSTEM | CLASS_OS),
	STRESSOR(fifo, FIFO, CLASS_PIPE_IO | CLASS_OS | CLASS_SCHEDULER),
	STRESSOR(flock, FLOCK, CLASS_FILESYSTEM | CLASS_OS),
	STRESSOR(fork, FORK, CLASS_SCHEDULER | CLASS_OS),
	STRESSOR(fstat, FSTAT, CLASS_FILESYSTEM | CLASS_OS),
#if defined(STRESS_FUTEX)
	STRESSOR(futex, FUTEX, CLASS_SCHEDULER | CLASS_OS),
#endif
	STRESSOR(get, GET, CLASS_OS),
#if defined(STRESS_GETRANDOM)
	STRESSOR(getrandom, GETRANDOM, CLASS_OS | CLASS_CPU),
#endif
	STRESSOR(hdd, HDD, CLASS_IO | CLASS_OS),
	STRESSOR(hsearch, HSEARCH, CLASS_CPU_CACHE | CLASS_CPU | CLASS_MEMORY),
#if defined(STRESS_ICACHE)
	STRESSOR(icache, ICACHE, CLASS_CPU_CACHE),
#endif
	STRESSOR(iosync, IOSYNC, CLASS_FILESYSTEM | CLASS_OS),
#if defined(STRESS_INOTIFY)
	STRESSOR(inotify, INOTIFY, CLASS_FILESYSTEM | CLASS_SCHEDULER | CLASS_OS),
#endif
	STRESSOR(itimer, ITIMER, CLASS_INTERRUPT | CLASS_OS),
#if defined(STRESS_KCMP)
	STRESSOR(kcmp, KCMP, CLASS_OS),
#endif
#if defined(STRESS_KEY)
	STRESSOR(key, KEY, CLASS_OS),
#endif
	STRESSOR(kill, KILL, CLASS_INTERRUPT | CLASS_SCHEDULER | CLASS_OS),
#if defined(STRESS_LEASE)
	STRESSOR(lease, LEASE, CLASS_FILESYSTEM | CLASS_OS),
#endif
	STRESSOR(link, LINK, CLASS_FILESYSTEM | CLASS_OS),
#if defined(STRESS_LOCKF)
	STRESSOR(lockf, LOCKF, CLASS_FILESYSTEM | CLASS_OS),
#endif
	STRESSOR(longjmp, LONGJMP, CLASS_CPU),
	STRESSOR(lsearch, LSEARCH, CLASS_CPU_CACHE | CLASS_CPU | CLASS_MEMORY),
	STRESSOR(malloc, MALLOC, CLASS_CPU_CACHE | CLASS_MEMORY | CLASS_VM | CLASS_OS),
	STRESSOR(matrix, MATRIX, CLASS_CPU | CLASS_CPU_CACHE | CLASS_MEMORY | CLASS_CPU),
	STRESSOR(memcpy, MEMCPY, CLASS_CPU_CACHE | CLASS_MEMORY),
#if defined(STRESS_MEMFD)
	STRESSOR(memfd, MEMFD, CLASS_OS | CLASS_MEMORY),
#endif
#if defined(STRESS_MINCORE)
	STRESSOR(mincore, MINCORE, CLASS_OS | CLASS_MEMORY),
#endif
	STRESSOR(mlock, MLOCK, CLASS_VM | CLASS_OS),
	STRESSOR(mmap, MMAP, CLASS_VM | CLASS_OS),
#if defined(STRESS_MMAPFORK)
	STRESSOR(mmapfork, MMAPFORK, CLASS_SCHEDULER | CLASS_VM | CLASS_OS),
#endif
	STRESSOR(mmapmany, MMAPMANY, CLASS_VM | CLASS_OS),
#if defined(STRESS_MREMAP)
	STRESSOR(mremap, MREMAP, CLASS_VM | CLASS_OS),
#endif
#if defined(STRESS_MSG)
	STRESSOR(msg, MSG, CLASS_SCHEDULER | CLASS_OS),
#endif
#if defined(STRESS_MQ)
	STRESSOR(mq, MQ, CLASS_SCHEDULER | CLASS_OS),
#endif
	STRESSOR(nice, NICE, CLASS_SCHEDULER | CLASS_OS),
	STRESSOR(null, NULL, CLASS_DEV | CLASS_MEMORY | CLASS_OS),
#if defined(STRESS_NUMA)
	STRESSOR(numa, NUMA, CLASS_CPU | CLASS_MEMORY | CLASS_OS),
#endif
	STRESSOR(open, OPEN, CLASS_FILESYSTEM | CLASS_OS),
	STRESSOR(pipe, PIPE, CLASS_PIPE_IO | CLASS_MEMORY | CLASS_OS),
	STRESSOR(poll, POLL, CLASS_SCHEDULER | CLASS_OS),
#if defined(STRESS_PROCFS)
	STRESSOR(procfs, PROCFS, CLASS_FILESYSTEM | CLASS_OS),
#endif
	STRESSOR(pthread, PTHREAD, CLASS_SCHEDULER | CLASS_OS),
#if defined(STRESS_PTRACE)
	STRESSOR(ptrace, PTRACE, CLASS_OS),
#endif
	STRESSOR(qsort, QSORT, CLASS_CPU_CACHE | CLASS_CPU | CLASS_MEMORY),
#if defined(STRESS_QUOTA)
	STRESSOR(quota, QUOTA, CLASS_OS),
#endif
#if defined(STRESS_RDRAND)
	STRESSOR(rdrand, RDRAND, CLASS_CPU),
#endif
#if defined(STRESS_READAHEAD)
	STRESSOR(readahead, READAHEAD, CLASS_IO | CLASS_OS),
#endif
	STRESSOR(rename, RENAME, CLASS_FILESYSTEM | CLASS_OS),
#if defined(STRESS_RLIMIT)
	STRESSOR(rlimit, RLIMIT, CLASS_OS),
#endif
	STRESSOR(seek, SEEK, CLASS_IO | CLASS_OS),
#if defined(STRESS_SEMAPHORE_POSIX)
	STRESSOR(sem_posix, SEMAPHORE_POSIX, CLASS_OS | CLASS_SCHEDULER),
#endif
#if defined(STRESS_SEMAPHORE_SYSV)
	STRESSOR(sem_sysv, SEMAPHORE_SYSV, CLASS_OS | CLASS_SCHEDULER),
#endif
	STRESSOR(shm_sysv, SHM_SYSV, CLASS_VM | CLASS_OS),
#if defined(STRESS_SENDFILE)
	STRESSOR(sendfile, SENDFILE, CLASS_PIPE_IO | CLASS_OS),
#endif
#if defined(STRESS_SIGFD)
	STRESSOR(sigfd, SIGFD, CLASS_INTERRUPT | CLASS_OS),
#endif
	STRESSOR(sigfpe, SIGFPE, CLASS_INTERRUPT | CLASS_OS),
	STRESSOR(sigpending, SIGPENDING, CLASS_INTERRUPT | CLASS_OS),
#if defined(STRESS_SIGQUEUE)
	STRESSOR(sigq, SIGQUEUE, CLASS_INTERRUPT | CLASS_OS),
#endif
	STRESSOR(sigsegv, SIGSEGV, CLASS_INTERRUPT | CLASS_OS),
	STRESSOR(sigsuspend, SIGSUSPEND, CLASS_INTERRUPT | CLASS_OS),
	STRESSOR(socket, SOCKET, CLASS_NETWORK | CLASS_OS),
	STRESSOR(socket_pair, SOCKET_PAIR, CLASS_NETWORK | CLASS_OS),
#if defined(STRESS_SPLICE)
	STRESSOR(splice, SPLICE, CLASS_PIPE_IO | CLASS_OS),
#endif
	STRESSOR(stack, STACK, CLASS_VM | CLASS_MEMORY),
	STRESSOR(str, STR, CLASS_CPU | CLASS_CPU_CACHE | CLASS_MEMORY),
	STRESSOR(switch, SWITCH, CLASS_SCHEDULER | CLASS_OS),
	STRESSOR(symlink, SYMLINK, CLASS_FILESYSTEM | CLASS_OS),
	STRESSOR(sysinfo, SYSINFO, CLASS_OS),
#if defined(STRESS_SYSFS)
	STRESSOR(sysfs, SYSFS, CLASS_OS),
#endif
#if defined(STRESS_TEE)
	STRESSOR(tee, TEE, CLASS_PIPE_IO | CLASS_OS | CLASS_SCHEDULER),
#endif
#if defined(STRESS_TIMER)
	STRESSOR(timer, TIMER, CLASS_INTERRUPT | CLASS_OS),
#endif
#if defined(STRESS_TIMERFD)
	STRESSOR(timerfd, TIMERFD, CLASS_INTERRUPT | CLASS_OS),
#endif
	STRESSOR(tsearch, TSEARCH, CLASS_CPU_CACHE | CLASS_CPU | CLASS_MEMORY),
	STRESSOR(udp, UDP, CLASS_NETWORK | CLASS_OS),
#if defined(STRESS_UDP_FLOOD)
	STRESSOR(udp_flood, UDP_FLOOD, CLASS_NETWORK | CLASS_OS),
#endif
#if defined(STRESS_URANDOM)
	STRESSOR(urandom, URANDOM, CLASS_DEV | CLASS_OS),
#endif
	STRESSOR(utime, UTIME, CLASS_FILESYSTEM | CLASS_OS),
#if defined(STRESS_VECMATH)
	STRESSOR(vecmath, VECMATH, CLASS_CPU | CLASS_CPU_CACHE),
#endif
#if defined(STRESS_VFORK)
	STRESSOR(vfork, VFORK, CLASS_SCHEDULER | CLASS_OS),
#endif
	STRESSOR(vm, VM, CLASS_VM | CLASS_MEMORY | CLASS_OS),
#if defined(STRESS_VM_RW)
	STRESSOR(vm_rw, VM_RW, CLASS_VM | CLASS_MEMORY | CLASS_OS),
#endif
#if defined(STRESS_VM_SPLICE)
	STRESSOR(vm_splice, VM_SPLICE, CLASS_VM | CLASS_PIPE_IO | CLASS_OS),
#endif
#if defined(STRESS_WAIT)
	STRESSOR(wait, WAIT, CLASS_SCHEDULER | CLASS_OS),
#endif
	STRESSOR(wcs, WCS, CLASS_CPU | CLASS_CPU_CACHE | CLASS_MEMORY),
#if defined(STRESS_XATTR)
	STRESSOR(xattr, XATTR, CLASS_FILESYSTEM | CLASS_OS),
#endif
#if defined(STRESS_YIELD)
	STRESSOR(yield, YIELD, CLASS_SCHEDULER | CLASS_OS),
#endif
	STRESSOR(zero, ZERO, CLASS_DEV | CLASS_MEMORY | CLASS_OS),
	STRESSOR(zombie, ZOMBIE, CLASS_SCHEDULER | CLASS_OS),
	{ stress_noop, STRESS_MAX, 0, 0, NULL, 0 }
};

STRESS_ASSERT(SIZEOF_ARRAY(stressors) != STRESS_MAX)

/* Different stress classes */
static const class_t classes[] = {
	{ CLASS_CPU,		"cpu" },
	{ CLASS_CPU_CACHE,	"cpu-cache" },
	{ CLASS_DEV,		"device" },
	{ CLASS_IO,		"io" },
	{ CLASS_INTERRUPT,	"interrupt" },
	{ CLASS_FILESYSTEM,	"filesystem" },
	{ CLASS_MEMORY,		"memory" },
	{ CLASS_NETWORK,	"network" },
	{ CLASS_OS,		"os" },
	{ CLASS_PIPE_IO,	"pipe" },
	{ CLASS_SCHEDULER,	"scheduler" },
	{ CLASS_VM,		"vm" },
	{ 0,			NULL }
};

static const struct option long_options[] = {
#if defined(STRESS_AFFINITY)
	{ "affinity",	1,	0,	OPT_AFFINITY },
	{ "affinity-ops",1,	0,	OPT_AFFINITY_OPS },
	{ "affinity-rand",0,	0,	OPT_AFFINITY_RAND },
#endif
	{ "aggressive",	0,	0,	OPT_AGGRESSIVE },
#if defined(STRESS_AIO)
	{ "aio",	1,	0,	OPT_AIO },
	{ "aio-ops",	1,	0,	OPT_AIO_OPS },
	{ "aio-requests",1,	0,	OPT_AIO_REQUESTS },
#endif
#if defined(STRESS_AIO_LINUX)
	{ "aiol",	1,	0,	OPT_AIO_LINUX },
	{ "aiol-ops",	1,	0,	OPT_AIO_LINUX_OPS },
	{ "aiol-requests",1,	0,	OPT_AIO_LINUX_REQUESTS },
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
	{ "cache-prefetch",0,	0,	OPT_CACHE_PREFETCH },
	{ "cache-flush",0,	0,	OPT_CACHE_FLUSH },
	{ "cache-fence",0,	0,	OPT_CACHE_FENCE },
	{ "chdir",	1,	0, 	OPT_CHDIR },
	{ "chdir-ops",	1,	0, 	OPT_CHDIR_OPS },
	{ "chmod",	1,	0, 	OPT_CHMOD },
	{ "chmod-ops",	1,	0,	OPT_CHMOD_OPS },
	{ "class",	1,	0,	OPT_CLASS },
#if defined(STRESS_CLOCK)
	{ "clock",	1,	0,	OPT_CLOCK },
	{ "clock-ops",	1,	0,	OPT_CLOCK_OPS },
#endif
#if defined(STRESS_CLONE)
	{ "clone",	1,	0,	OPT_CLONE },
	{ "clone-ops",	1,	0,	OPT_CLONE_OPS },
	{ "clone-max",	1,	0,	OPT_CLONE_MAX },
#endif
#if defined(STRESS_CONTEXT)
	{ "context",	1,	0,	OPT_CONTEXT },
	{ "context-ops",1,	0,	OPT_CONTEXT_OPS },
#endif
	{ "cpu",	1,	0,	OPT_CPU },
	{ "cpu-ops",	1,	0,	OPT_CPU_OPS },
	{ "cpu-load",	1,	0,	OPT_CPU_LOAD },
	{ "cpu-load-slice",1,	0,	OPT_CPU_LOAD_SLICE },
	{ "cpu-method",	1,	0,	OPT_CPU_METHOD },
	{ "crypt",	1,	0,	OPT_CRYPT },
	{ "crypt-ops",	1,	0,	OPT_CRYPT_OPS },
	{ "dentry",	1,	0,	OPT_DENTRY },
	{ "dentry-ops",	1,	0,	OPT_DENTRY_OPS },
	{ "dentries",	1,	0,	OPT_DENTRIES },
	{ "dentry-order",1,	0,	OPT_DENTRY_ORDER },
	{ "dir",	1,	0,	OPT_DIR },
	{ "dir-ops",	1,	0,	OPT_DIR_OPS },
	{ "dry-run",	0,	0,	OPT_DRY_RUN },
	{ "dup",	1,	0,	OPT_DUP },
	{ "dup-ops",	1,	0,	OPT_DUP_OPS },
#if defined(STRESS_EPOLL)
	{ "epoll",	1,	0,	OPT_EPOLL },
	{ "epoll-ops",	1,	0,	OPT_EPOLL_OPS },
	{ "epoll-port",	1,	0,	OPT_EPOLL_PORT },
	{ "epoll-domain",1,	0,	OPT_EPOLL_DOMAIN },
#endif
#if defined(STRESS_EVENTFD)
	{ "eventfd",	1,	0,	OPT_EVENTFD },
	{ "eventfd-ops",1,	0,	OPT_EVENTFD_OPS },
#endif
	{ "exclude",	1,	0,	OPT_EXCLUDE },
#if defined(STRESS_FALLOCATE)
	{ "fallocate",	1,	0,	OPT_FALLOCATE },
	{ "fallocate-ops",1,	0,	OPT_FALLOCATE_OPS },
	{ "fallocate-bytes",1,	0,	OPT_FALLOCATE_BYTES },
#endif
	{ "fault",	1,	0,	OPT_FAULT },
	{ "fault-ops",	1,	0,	OPT_FAULT_OPS },
	{ "fcntl",	1,	0,	OPT_FCNTL},
	{ "fcntl-ops",	1,	0,	OPT_FCNTL_OPS },
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
#if defined(STRESS_FUTEX)
	{ "futex",	1,	0,	OPT_FUTEX },
	{ "futex-ops",	1,	0,	OPT_FUTEX_OPS },
#endif
	{ "get",	1,	0,	OPT_GET },
	{ "get-ops",	1,	0,	OPT_GET_OPS },
#if defined(STRESS_GETRANDOM)
	{ "getrandom",	1,	0,	OPT_GETRANDOM },
	{ "getrandom-ops",1,	0,	OPT_GETRANDOM_OPS },
#endif
	{ "hdd",	1,	0,	OPT_HDD },
	{ "hdd-ops",	1,	0,	OPT_HDD_OPS },
	{ "hdd-bytes",	1,	0,	OPT_HDD_BYTES },
	{ "hdd-write-size", 1,	0,	OPT_HDD_WRITE_SIZE },
	{ "hdd-opts",	1,	0,	OPT_HDD_OPTS },
	{ "help",	0,	0,	OPT_HELP },
	{ "hsearch",	1,	0,	OPT_HSEARCH },
	{ "hsearch-ops",1,	0,	OPT_HSEARCH_OPS },
	{ "hsearch-size",1,	0,	OPT_HSEARCH_SIZE },
#if defined(STRESS_ICACHE)
	{ "icache",	1,	0,	OPT_ICACHE },
	{ "icache-ops",	1,	0,	OPT_ICACHE_OPS },
#endif
#if defined(STRESS_INOTIFY)
	{ "inotify",	1,	0,	OPT_INOTIFY },
	{ "inotify-ops",1,	0,	OPT_INOTIFY_OPS },
#endif
	{ "io",		1,	0,	OPT_IOSYNC },
	{ "io-ops",	1,	0,	OPT_IOSYNC_OPS },
#if defined(STRESS_IONICE)
	{ "ionice-class",1,	0,	OPT_IONICE_CLASS },
	{ "ionice-level",1,	0,	OPT_IONICE_LEVEL },
#endif
	{ "itimer",	1,	0,	OPT_ITIMER },
	{ "itimer-ops",	1,	0,	OPT_ITIMER_OPS },
	{ "itimer-freq",1,	0,	OPT_ITIMER_FREQ },
#if defined(STRESS_KCMP)
	{ "kcmp",	1,	0,	OPT_KCMP },
	{ "kcmp-ops",	1,	0,	OPT_KCMP_OPS },
#endif
#if defined(STRESS_KEY)
	{ "key",	1,	0,	OPT_KEY },
	{ "key-ops",	1,	0,	OPT_KEY_OPS },
#endif
	{ "keep-name",	0,	0,	OPT_KEEP_NAME },
	{ "kill",	1,	0,	OPT_KILL },
	{ "kill-ops",	1,	0,	OPT_KILL_OPS },
#if defined(STRESS_LEASE)
	{ "lease",	1,	0,	OPT_LEASE },
	{ "lease-ops",	1,	0,	OPT_LEASE_OPS },
	{ "lease-breakers",1,	0,	OPT_LEASE_BREAKERS },
#endif
	{ "link",	1,	0,	OPT_LINK },
	{ "link-ops",	1,	0,	OPT_LINK_OPS },
#if defined(STRESS_LOCKF)
	{ "lockf",	1,	0,	OPT_LOCKF },
	{ "lockf-ops",	1,	0,	OPT_LOCKF_OPS },
	{ "lockf-nonblock", 0,	0,	OPT_LOCKF_NONBLOCK },
#endif
	{ "log-brief",	0,	0,	OPT_LOG_BRIEF },
	{ "longjmp",	1,	0,	OPT_LONGJMP },
	{ "longjmp-ops",1,	0,	OPT_LONGJMP_OPS },
	{ "lsearch",	1,	0,	OPT_LSEARCH },
	{ "lsearch-ops",1,	0,	OPT_LSEARCH_OPS },
	{ "lsearch-size",1,	0,	OPT_LSEARCH_SIZE },
	{ "malloc",	1,	0,	OPT_MALLOC },
	{ "malloc-bytes",1,	0,	OPT_MALLOC_BYTES },
	{ "malloc-max",	1,	0,	OPT_MALLOC_MAX },
	{ "malloc-ops",	1,	0,	OPT_MALLOC_OPS },
#if defined(STRESS_MALLOPT)
	{ "malloc-thresh",1,	0,	OPT_MALLOC_THRESHOLD },
#endif
	{ "matrix",	1,	0,	OPT_MATRIX },
	{ "matrix-ops",	1,	0,	OPT_MATRIX_OPS },
	{ "matrix-method",1,	0,	OPT_MATRIX_METHOD },
	{ "matrix-size",1,	0,	OPT_MATRIX_SIZE },
	{ "maximize",	0,	0,	OPT_MAXIMIZE },
	{ "memcpy",	1,	0,	OPT_MEMCPY },
	{ "memcpy",	1,	0,	OPT_MEMCPY },
	{ "memcpy",	1,	0,	OPT_MEMCPY },
	{ "memcpy-ops",	1,	0,	OPT_MEMCPY_OPS },
#if defined(STRESS_MEMFD)
	{ "memfd",	1,	0,	OPT_MEMFD },
	{ "memfd-ops",	1,	0,	OPT_MEMFD_OPS },
#endif
	{ "metrics",	0,	0,	OPT_METRICS },
	{ "metrics-brief",0,	0,	OPT_METRICS_BRIEF },
#if defined(STRESS_MINCORE)
	{ "mincore",	1,	0,	OPT_MINCORE },
	{ "mincore-ops",1,	0,	OPT_MINCORE_OPS },
	{ "mincore-random",0,	0,	OPT_MINCORE_RAND },
#endif
	{ "minimize",	0,	0,	OPT_MINIMIZE },
#if defined(STRESS_MLOCK)
	{ "mlock",	1,	0,	OPT_MLOCK },
	{ "mlock-ops",	1,	0,	OPT_MLOCK_OPS },
#endif
	{ "mmap",	1,	0,	OPT_MMAP },
	{ "mmap-ops",	1,	0,	OPT_MMAP_OPS },
	{ "mmap-async",	0,	0,	OPT_MMAP_ASYNC },
	{ "mmap-bytes",	1,	0,	OPT_MMAP_BYTES },
	{ "mmap-file",	0,	0,	OPT_MMAP_FILE },
	{ "mmap-mprotect",0,	0,	OPT_MMAP_MPROTECT },
#if defined(STRESS_MMAPFORK)
	{ "mmapfork",	1,	0,	OPT_MMAPFORK },
	{ "mmapfork-ops",1,	0,	OPT_MMAPFORK_OPS },
#endif
	{ "mmapmany",	1,	0,	OPT_MMAPMANY },
	{ "mmapmany-ops",1,	0,	OPT_MMAPMANY_OPS },
#if defined(STRESS_MREMAP)
	{ "mremap",	1,	0,	OPT_MREMAP },
	{ "mremap-ops",	1,	0,	OPT_MREMAP_OPS },
	{ "mremap-bytes",1,	0,	OPT_MREMAP_BYTES },
#endif
#if defined(STRESS_MSG)
	{ "msg",	1,	0,	OPT_MSG },
	{ "msg-ops",	1,	0,	OPT_MSG_OPS },
#endif
#if defined(STRESS_MQ)
	{ "mq",		1,	0,	OPT_MQ },
	{ "mq-ops",	1,	0,	OPT_MQ_OPS },
	{ "mq-size",	1,	0,	OPT_MQ_SIZE },
#endif
	{ "nice",	1,	0,	OPT_NICE },
	{ "nice-ops",	1,	0,	OPT_NICE_OPS },
	{ "no-madvise",	0,	0,	OPT_NO_MADVISE },
	{ "null",	1,	0,	OPT_NULL },
	{ "null-ops",	1,	0,	OPT_NULL_OPS },
#if defined(STRESS_NUMA)
	{ "numa",	1,	0,	OPT_NUMA },
	{ "numa-ops",	1,	0,	OPT_NUMA_OPS },
#endif
	{ "open",	1,	0,	OPT_OPEN },
	{ "open-ops",	1,	0,	OPT_OPEN_OPS },
#if defined(STRESS_PAGE_IN)
	{ "page-in",	0,	0,	OPT_PAGE_IN },
#endif
#if defined(STRESS_PERF_STATS)
	{ "perf",	0,	0,	OPT_PERF_STATS },
#endif
	{ "pipe",	1,	0,	OPT_PIPE },
	{ "pipe-ops",	1,	0,	OPT_PIPE_OPS },
	{ "poll",	1,	0,	OPT_POLL },
	{ "poll-ops",	1,	0,	OPT_POLL_OPS },
#if defined(STRESS_PROCFS)
	{ "procfs",	1,	0,	OPT_PROCFS },
	{ "procfs-ops",	1,	0,	OPT_PROCFS_OPS },
#endif
	{ "pthread",	1,	0,	OPT_PTHREAD },
	{ "pthread-ops",1,	0,	OPT_PTHREAD_OPS },
	{ "pthread-max",1,	0,	OPT_PTHREAD_MAX },
#if defined(STRESS_PTRACE)
	{ "ptrace",	1,	0,	OPT_PTRACE },
	{ "ptrace-ops",1,	0,	OPT_PTRACE_OPS },
#endif
	{ "qsort",	1,	0,	OPT_QSORT },
	{ "qsort-ops",	1,	0,	OPT_QSORT_OPS },
	{ "qsort-size",	1,	0,	OPT_QSORT_INTEGERS },
	{ "quiet",	0,	0,	OPT_QUIET },
#if defined(STRESS_QUOTA)
	{ "quota",	1,	0,	OPT_QUOTA },
	{ "quota-ops",	1,	0,	OPT_QUOTA_OPS },
#endif
	{ "random",	1,	0,	OPT_RANDOM },
#if defined(STRESS_RDRAND)
	{ "rdrand",	1,	0,	OPT_RDRAND },
	{ "rdrand-ops",	1,	0,	OPT_RDRAND_OPS },
#endif
#if defined(STRESS_READAHEAD)
	{ "readahead",	1,	0,	OPT_READAHEAD },
	{ "readahead-ops",1,	0,	OPT_READAHEAD_OPS },
	{ "readahead-bytes",1,	0,	OPT_READAHEAD_BYTES },
#endif
	{ "rename",	1,	0,	OPT_RENAME },
	{ "rename-ops",	1,	0,	OPT_RENAME_OPS },
#if defined(STRESS_RLIMIT)
	{ "rlimit",	1,	0,	OPT_RLIMIT },
	{ "rlimit-ops",	1,	0,	OPT_RLIMIT_OPS },
#endif
	{ "sched",	1,	0,	OPT_SCHED },
	{ "sched-prio",	1,	0,	OPT_SCHED_PRIO },
	{ "seek",	1,	0,	OPT_SEEK },
	{ "seek-ops",	1,	0,	OPT_SEEK_OPS },
	{ "seek-size",	1,	0,	OPT_SEEK_SIZE },
#if defined(STRESS_SEMAPHORE_POSIX)
	{ "sem",	1,	0,	OPT_SEMAPHORE_POSIX },
	{ "sem-ops",	1,	0,	OPT_SEMAPHORE_POSIX_OPS },
	{ "sem-procs",	1,	0,	OPT_SEMAPHORE_POSIX_PROCS },
#endif
#if defined(STRESS_SEMAPHORE_SYSV)
	{ "sem-sysv",	1,	0,	OPT_SEMAPHORE_SYSV },
	{ "sem-sysv-ops",1,	0,	OPT_SEMAPHORE_SYSV_OPS },
	{ "sem-sysv-procs",1,	0,	OPT_SEMAPHORE_SYSV_PROCS },
#endif
#if defined(STRESS_SENDFILE)
	{ "sendfile",	1,	0,	OPT_SENDFILE },
	{ "sendfile-ops",1,	0,	OPT_SENDFILE_OPS },
	{ "sendfile-size",1,	0,	OPT_SENDFILE_SIZE },
#endif
	{ "sequential",	1,	0,	OPT_SEQUENTIAL },
	{ "shm-sysv",	1,	0,	OPT_SHM_SYSV },
	{ "shm-sysv-ops",1,	0,	OPT_SHM_SYSV_OPS },
	{ "shm-sysv-bytes",1,	0,	OPT_SHM_SYSV_BYTES },
	{ "shm-sysv-segs",1,	0,	OPT_SHM_SYSV_SEGMENTS },
#if defined(STRESS_SIGFD)
	{ "sigfd",	1,	0,	OPT_SIGFD },
	{ "sigfd-ops",	1,	0,	OPT_SIGFD_OPS },
#endif
	{ "sigfpe",	1,	0,	OPT_SIGFPE },
	{ "sigfpe-ops",	1,	0,	OPT_SIGFPE_OPS },
	{ "sigsegv",	1,	0,	OPT_SIGSEGV },
	{ "sigsegv-ops",1,	0,	OPT_SIGSEGV_OPS },
	{ "sigsuspend",	1,	0,	OPT_SIGSUSPEND},
	{ "sigsuspend-ops",1,	0,	OPT_SIGSUSPEND_OPS},
	{ "sigpending",	1,	0,	OPT_SIGPENDING},
	{ "sigpending-ops",1,	0,	OPT_SIGPENDING_OPS },
#if defined(STRESS_SIGQUEUE)
	{ "sigq",	1,	0,	OPT_SIGQUEUE },
	{ "sigq-ops",	1,	0,	OPT_SIGQUEUE_OPS },
#endif
	{ "sock",	1,	0,	OPT_SOCKET },
	{ "sock-domain",1,	0,	OPT_SOCKET_DOMAIN },
	{ "sock-ops",	1,	0,	OPT_SOCKET_OPS },
	{ "sock-port",	1,	0,	OPT_SOCKET_PORT },
	{ "sockpair",	1,	0,	OPT_SOCKET_PAIR },
	{ "sockpair-ops",1,	0,	OPT_SOCKET_PAIR_OPS },
#if defined(STRESS_SPLICE)
	{ "splice",	1,	0,	OPT_SPLICE },
	{ "splice-bytes",1,	0,	OPT_SPLICE_BYTES },
	{ "splice-ops",	1,	0,	OPT_SPLICE_OPS },
#endif
	{ "stack",	1,	0,	OPT_STACK},
	{ "stack-fill",	0,	0,	OPT_STACK_FILL },
	{ "stack-ops",	1,	0,	OPT_STACK_OPS },
	{ "str",	1,	0,	OPT_STR},
	{ "str-ops",	1,	0,	OPT_STR_OPS },
	{ "str-method",	1,	0,	OPT_STR_METHOD },
	{ "switch",	1,	0,	OPT_SWITCH },
	{ "switch-ops",	1,	0,	OPT_SWITCH_OPS },
	{ "symlink",	1,	0,	OPT_SYMLINK },
	{ "symlink-ops",1,	0,	OPT_SYMLINK_OPS },
	{ "sysinfo",	1,	0,	OPT_SYSINFO },
	{ "sysinfo-ops",1,	0,	OPT_SYSINFO_OPS },
#if defined(STRESS_SYSFS)
	{ "sysfs",	1,	0,	OPT_SYSFS },
	{ "sysfs-ops",1,	0,	OPT_SYSFS_OPS },
#endif
	{ "syslog",	0,	0,	OPT_SYSLOG },
	{ "timeout",	1,	0,	OPT_TIMEOUT },
#if defined(STRESS_TEE)
	{ "tee",	1,	0,	OPT_TEE },
	{ "tee-ops",	1,	0,	OPT_TEE_OPS },
#endif
#if defined(STRESS_TIMER)
	{ "timer",	1,	0,	OPT_TIMER },
	{ "timer-ops",	1,	0,	OPT_TIMER_OPS },
	{ "timer-freq",	1,	0,	OPT_TIMER_FREQ },
	{ "timer-rand", 0,	0,	OPT_TIMER_RAND },
#endif
#if defined(STRESS_TIMERFD)
	{ "timerfd",	1,	0,	OPT_TIMERFD },
	{ "timerfd-ops",1,	0,	OPT_TIMERFD_OPS },
	{ "timerfd-freq",1,	0,	OPT_TIMERFD_FREQ },
	{ "timerfd-rand",0,	0,	OPT_TIMERFD_RAND },
#endif
	{ "tsearch",	1,	0,	OPT_TSEARCH },
	{ "tsearch-ops",1,	0,	OPT_TSEARCH_OPS },
	{ "tsearch-size",1,	0,	OPT_TSEARCH_SIZE },
	{ "times",	0,	0,	OPT_TIMES },
#if defined(STRESS_THERMAL_ZONES)
	{ "tz",		0,	0,	OPT_THERMAL_ZONES },
#endif
	{ "udp",	1,	0,	OPT_UDP },
	{ "udp-domain",1,	0,	OPT_UDP_DOMAIN },
	{ "udp-ops",	1,	0,	OPT_UDP_OPS },
	{ "udp-port",	1,	0,	OPT_UDP_PORT },
#if defined(STRESS_UDP_FLOOD)
	{ "udp-flood",	1,	0,	OPT_UDP_FLOOD },
	{ "udp-flood-domain",1,	0,	OPT_UDP_FLOOD_DOMAIN },
	{ "udp-flood-ops",1,	0,	OPT_UDP_FLOOD_OPS },
#endif
	{ "utime",	1,	0,	OPT_UTIME },
	{ "utime-ops",	1,	0,	OPT_UTIME_OPS },
	{ "utime-fsync",0,	0,	OPT_UTIME_FSYNC },
#if defined(STRESS_URANDOM)
	{ "urandom",	1,	0,	OPT_URANDOM },
	{ "urandom-ops",1,	0,	OPT_URANDOM_OPS },
#endif
#if defined(STRESS_VECMATH)
	{ "vecmath",	1,	0,	OPT_VECMATH },
	{ "vecmath-ops",1,	0,	OPT_VECMATH_OPS },
#endif
	{ "verbose",	0,	0,	OPT_VERBOSE },
	{ "verify",	0,	0,	OPT_VERIFY },
	{ "version",	0,	0,	OPT_VERSION },
#if defined(STRESS_VFORK)
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
#if defined(STRESS_VM_RW)
	{ "vm-rw",	1,	0,	OPT_VM_RW },
	{ "vm-rw-bytes",1,	0,	OPT_VM_RW_BYTES },
	{ "vm-rw-ops",	1,	0,	OPT_VM_RW_OPS },
#endif
#if defined(STRESS_VM_SPLICE)
	{ "vm-splice",	1,	0,	OPT_VM_SPLICE },
	{ "vm-splice-bytes",1,	0,	OPT_VM_SPLICE_BYTES },
	{ "vm-splice-ops",1,	0,	OPT_VM_SPLICE_OPS },
#endif
	{ "wcs",	1,	0,	OPT_WCS},
	{ "wcs-ops",	1,	0,	OPT_WCS_OPS },
	{ "wcs-method",	1,	0,	OPT_WCS_METHOD },
#if defined(STRESS_WAIT)
	{ "wait",	1,	0,	OPT_WAIT },
	{ "wait-ops",	1,	0,	OPT_WAIT_OPS },
#endif
#if defined(STRESS_XATTR)
	{ "xattr",	1,	0,	OPT_XATTR },
	{ "xattr-ops",	1,	0,	OPT_XATTR_OPS },
#endif
	{ "yaml",	1,	0,	OPT_YAML },
#if defined(STRESS_YIELD)
	{ "yield",	1,	0,	OPT_YIELD },
	{ "yield-ops",	1,	0,	OPT_YIELD_OPS },
#endif
	{ "zero",	1,	0,	OPT_ZERO },
	{ "zero-ops",	1,	0,	OPT_ZERO_OPS },
	{ "zombie",	1,	0,	OPT_ZOMBIE },
	{ "zombie-ops",	1,	0,	OPT_ZOMBIE_OPS },
	{ "zombie-max",	1,	0,	OPT_ZOMBIE_MAX },
	{ NULL,		0, 	0, 	0 }
};

/*
 *  Generic help options
 */
static const help_t help_generic[] = {
	{ NULL,		"aggressive",		"enable all aggressive options" },
	{ "a N",	"all N",		"start N workers of each stress test" },
	{ "b N",	"backoff N",		"wait of N microseconds before work starts" },
	{ NULL,		"class name",		"specify a class of stressors, use with --sequential" },
	{ "n",		"dry-run",		"do not run" },
	{ "h",		"help",			"show help" },
	{ "k",		"keep-name",		"keep stress worker names to be 'stress-ng'" },
	{ NULL,		"log-brief",		"less verbose log messages" },
	{ NULL,		"maximize",		"enable maximum stress options" },
	{ "M",		"metrics",		"print pseudo metrics of activity" },
	{ NULL,		"metrics-brief",	"enable metrics and only show non-zero results" },
	{ NULL,		"minimize",		"enable minimal stress options" },
	{ NULL,		"no-madvise",		"don't use random madvise options for each mmap" },
#if defined(STRESS_PAGE_IN)
	{ NULL,		"page-in",		"touch allocated pages that are not in core" },
#endif
#if defined(STRESS_PERF_STATS)
	{ NULL,		"perf",			"display perf statistics" },
#endif
	{ "q",		"quiet",		"quiet output" },
	{ "r",		"random N",		"start N random workers" },
	{ NULL,		"sched type",		"set scheduler type" },
	{ NULL,		"sched-prio N",		"set scheduler priority level N" },
	{ NULL,		"sequential N",		"run all stressors one by one, invoking N of them" },
	{ NULL,		"syslog",		"log messages to the syslog" },
	{ "t N",	"timeout N",		"timeout after N seconds" },
	{ NULL,		"times",		"show run time summary at end of the run" },
#if defined(STRESS_THERMAL_ZONES)
	{ NULL,		"tz",			"collect temperatures from thermal zones (Linux only)" },
#endif
	{ "v",		"verbose",		"verbose output" },
	{ NULL,		"verify",		"verify results (not available on all tests)" },
	{ "V",		"version",		"show version" },
	{ "Y",		"yaml",			"output results to YAML formatted filed" },
	{ "x",		"exclude",		"list of stressors to exclude (not run)" },
	{ NULL,		NULL,			NULL }
};

/*
 *  Stress test specific options
 */
static const help_t help_stressors[] = {
#if defined(STRESS_AFFINITY)
	{ NULL,		"affinity N",		"start N workers that rapidly change CPU affinity" },
	{ NULL, 	"affinity-ops N",   	"stop when N affinity bogo operations completed" },
	{ NULL, 	"affinity-rand",   	"change affinity randomly rather than sequentially" },
#endif
#if defined(STRESS_AIO)
	{ NULL,		"aio N",		"start N workers that issue async I/O requests" },
	{ NULL,		"aio-ops N",		"stop when N bogo async I/O requests completed" },
	{ NULL,		"aio-requests N",	"number of async I/O requests per worker" },
#endif
#if defined(STRESS_AIO_LINUX)
	{ NULL,		"aiol N",		"start N workers that issue async I/O requests via Linux aio" },
	{ NULL,		"aiol-ops N",		"stop when N bogo Linux aio async I/O requests completed" },
	{ NULL,		"aiol-requests N",	"number of Linux aio async I/O requests per worker" },
#endif
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
	{ NULL,		"cache-ops N",		"stop when N cache bogo operations completed" },
	{ NULL,		"cache-prefetch",	"prefetch on memory reads/writes" },
	{ NULL,		"cache-flush",		"flush cache after every memory write (x86 only)" },
	{ NULL,		"cache-fence",		"serialize stores (x86 only)" },
	{ NULL,		"chdir N",		"start N workers thrashing chdir on many paths" },
	{ NULL,		"chdir-ops N",		"stop chdir workers after N bogo chdir operations" },
	{ NULL,		"chmod N",		"start N workers thrashing chmod file mode bits " },
	{ NULL,		"chmod-ops N",		"stop chmod workers after N bogo operations" },
#if defined(STRESS_CLOCK)
	{ NULL,		"clock N",		"start N workers thrashing clocks and POSIX timers" },
	{ NULL,		"clock-ops N",		"stop clock workers after N bogo operations" },
#endif
#if defined(STRESS_CLONE)
	{ NULL,		"clone N",		"start N workers that rapidly create and reap clones" },
	{ NULL,		"clone-ops N",		"stop when N bogo clone operations completed" },
	{ NULL,		"clone-max N",		"set upper limit of N clones per worker" },
#endif
#if defined(STRESS_CONTEXT)
	{ NULL,		"context N",		"start N workers exercising user context" },
	{ NULL,		"context-ops N",	"stop context workers after N bogo operations" },
#endif
	{ "c N",	"cpu N",		"start N workers spinning on sqrt(rand())" },
	{ NULL,		"cpu-ops N",		"stop when N cpu bogo operations completed" },
	{ "l P",	"cpu-load P",		"load CPU by P %%, 0=sleep, 100=full load (see -c)" },
	{ NULL,		"cpu-load-slice S",	"specify time slice during busy load" },
	{ NULL,		"cpu-method m",		"specify stress cpu method m, default is all" },
	{ NULL,		"crypt N",		"start N workers performing password encryption" },
	{ NULL,		"crypt-ops N",		"stop when N bogo crypt operations completed" },
	{ "D N",	"dentry N",		"start N dentry thrashing stressors" },
	{ NULL,		"dentry-ops N",		"stop when N dentry bogo operations completed" },
	{ NULL,		"dentry-order O",	"specify dentry unlink order (reverse, forward, stride)" },
	{ NULL,		"dentries N",		"create N dentries per iteration" },
	{ NULL,		"dir N",		"start N directory thrashing stressors" },
	{ NULL,		"dir-ops N",		"stop when N directory bogo operations completed" },
	{ NULL,		"dup N",		"start N workers exercising dup/close" },
	{ NULL,		"dup-ops N",		"stop when N dup/close bogo operations completed" },
#if defined(STRESS_EPOLL)
	{ NULL,		"epoll N",		"start N workers doing epoll handled socket activity" },
	{ NULL,		"epoll-ops N",		"stop when N epoll bogo operations completed" },
	{ NULL,		"epoll-port P",		"use socket ports P upwards" },
	{ NULL,		"epoll-domain D",	"specify socket domain, default is unix" },
#endif
#if defined(STRESS_EVENTFD)
	{ NULL,		"eventfd N",		"start N workers stressing eventfd read/writes" },
	{ NULL,		"eventfd-ops N",	"stop eventfd workers after N bogo operations" },
#endif
#if defined(STRESS_FALLOCATE)
	{ NULL,		"fallocate N",		"start N workers fallocating 16MB files" },
	{ NULL,		"fallocate-ops N",	"stop when N fallocate bogo operations completed" },
	{ NULL,		"fallocate-bytes N",	"specify size of file to allocate" },
#endif
	{ NULL,		"fault N",		"start N workers producing page faults" },
	{ NULL,		"fault-ops N",		"stop when N page fault bogo operations completed" },
	{ NULL,		"fifo N",		"start N workers exercising fifo I/O" },
	{ NULL,		"fifo-ops N",		"stop when N fifo bogo operations completed" },
	{ NULL,		"fifo-readers N",	"number of fifo reader stessors to start" },
	{ NULL,		"fcntl N",		"start N workers exercising fcntl commands" },
	{ NULL,		"fcntl-ops N",		"stop when N fcntl bogo operations completed" },
	{ NULL,		"flock N",		"start N workers locking a single file" },
	{ NULL,		"flock-ops N",		"stop when N flock bogo operations completed" },
	{ "f N",	"fork N",		"start N workers spinning on fork() and exit()" },
	{ NULL,		"fork-ops N",		"stop when N fork bogo operations completed" },
	{ NULL,		"fork-max P",		"create P workers per iteration, default is 1" },
	{ NULL,		"fstat N",		"start N workers exercising fstat on files" },
	{ NULL,		"fstat-ops N",		"stop when N fstat bogo operations completed" },
	{ NULL,		"fstat-dir path",	"fstat files in the specified directory" },
#if defined(STRESS_FUTEX)
	{ NULL,		"futex N",		"start N workers exercising a fast mutex" },
	{ NULL,		"futex-ops N",		"stop when N fast mutex bogo operations completed" },
#endif
	{ NULL,		"get N",		"start N workers exercising the get*() system calls" },
	{ NULL,		"get-ops N",		"stop when N get bogo operations completed" },
#if defined(STRESS_GETRANDOM)
	{ NULL,		"getrandom N",		"start N workers fetching random data via getrandom()" },
	{ NULL,		"getrandom-ops N",	"stop when N getrandom bogo operations completed" },
#endif
	{ "d N",	"hdd N",		"start N workers spinning on write()/unlink()" },
	{ NULL,		"hdd-ops N",		"stop when N hdd bogo operations completed" },
	{ NULL,		"hdd-bytes N",		"write N bytes per hdd worker (default is 1GB)" },
	{ NULL,		"hdd-opts list",	"specify list of various stressor options" },
	{ NULL,		"hdd-write-size N",	"set the default write size to N bytes" },
	{ NULL,		"hsearch",		"start N workers that exercise a hash table search" },
	{ NULL,		"hsearch-ops",		"stop when N hash search bogo operations completed" },
	{ NULL,		"hsearch-size",		"number of integers to insert into hash table" },
#if defined(STRESS_ICACHE)
	{ NULL,		"icache N",		"start N CPU instruction cache thrashing workers" },
	{ NULL,		"icache-ops N",		"stop when N icache bogo operations completed" },
#endif
#if defined(STRESS_INOTIFY)
	{ NULL,		"inotify N",		"start N workers exercising inotify events" },
	{ NULL,		"inotify-ops N",	"stop inotify workers after N bogo operations" },
#endif
	{ "i N",	"io N",			"start N workers spinning on sync()" },
	{ NULL,		"io-ops N",		"stop when N io bogo operations completed" },
#if defined(STRESS_IONICE)
	{ NULL,		"ionice-class C",	"specify ionice class (idle, besteffort, realtime)" },
	{ NULL,		"ionice-level L",	"specify ionice level (0 max, 7 min)" },
#endif
	{ NULL,		"itimer N",		"start N workers exercising interval timers" },
	{ NULL,		"itimer-ops N",		"stop when N interval timer bogo ops completed" },
#if defined(STRESS_KCMP)
	{ NULL,		"kcmp N",		"start N workers exercising kcmp" },
	{ NULL,		"kcmp-ops N",		"stop when N kcmp bogo operations completed" },
#endif
#if defined(STRESS_KEY)
	{ NULL,		"key N",		"start N workers exercising key operations" },
	{ NULL,		"key-ops N",		"stop when N key bogo operations completed" },
#endif
	{ NULL,		"kill N",		"start N workers killing with SIGUSR1" },
	{ NULL,		"kill-ops N",		"stop when N kill bogo operations completed" },
#if defined(STRESS_LEASE)
	{ NULL,		"lease N",		"start N workers holding and breaking a lease" },
	{ NULL,		"lease-ops N",		"stop when N lease bogo operations completed" },
	{ NULL,		"lease-breakers N",	"number of lease breaking workers to start" },
#endif
	{ NULL,		"link N",		"start N workers creating hard links" },
	{ NULL,		"link-ops N",		"stop when N link bogo operations completed" },
#if defined(STRESS_LOCKF)
	{ NULL,		"lockf N",		"start N workers locking a single file via lockf" },
	{ NULL,		"lockf-ops N",		"stop when N lockf bogo operations completed" },
	{ NULL,		"lockf-nonblock",	"don't block if lock cannot be obtained, re-try" },
#endif
	{ NULL,		"longjmp N",		"start N workers exercising setjmp/longjmp" },
	{ NULL,		"longjmp-ops N",	"stop when N longjmp bogo operations completed" },
	{ NULL,		"lsearch",		"start N workers that exercise a linear search" },
	{ NULL,		"lsearch-ops",		"stop when N linear search bogo operations completed" },
	{ NULL,		"lsearch-size",		"number of 32 bit integers to lsearch" },
	{ NULL,		"malloc N",		"start N workers exercising malloc/realloc/free" },
	{ NULL,		"malloc-bytes N",	"allocate up to N bytes per allocation" },
	{ NULL,		"malloc-max N",		"keep up to N allocations at a time" },
	{ NULL,		"malloc-ops N",		"stop when N malloc bogo operations completed" },
#if defined(STRESS_MALLOPT)
	{ NULL,		"malloc-thresh N",	"threshold where malloc uses mmap instead of sbrk" },
#endif
	{ NULL,		"matrix N",		"start N workers exercising matrix operations" },
	{ NULL,		"matrix-ops N",		"stop when N maxtrix bogo operations completed" },
	{ NULL,		"matrix-method m",	"specify matrix stress method m, default is all" },
	{ NULL,		"matrix-size N",	"specify the size of the N x N matrix" },
	{ NULL,		"memcpy N",		"start N workers performing memory copies" },
	{ NULL,		"memcpy-ops N",		"stop when N memcpy bogo operations completed" },
#if defined(STRESS_MEMFD)
	{ NULL,		"memfd N",		"start N workers allocating memory with memfd_create" },
	{ NULL,		"memfd-ops N",		"stop when N memfd bogo operations completed" },
#endif
#if defined(STRESS_MINCORE)
	{ NULL,		"mincore N",		"start N workers exercising mincore" },
	{ NULL,		"mincore-ops N",	"stop when N mincore bogo operations completed" },
	{ NULL,		"mincore-random",	"randomly select pages rather than linear scan" },
#endif
#if defined(STRESS_MLOCK)
	{ NULL,		"mlock N",		"start N workers exercising mlock/munlock" },
	{ NULL,		"mlock-ops N",		"stop when N mlock bogo operations completed" },
#endif
	{ NULL,		"mmap N",		"start N workers stressing mmap and munmap" },
	{ NULL,		"mmap-ops N",		"stop when N mmap bogo operations completed" },
	{ NULL,		"mmap-async",		"using asynchronous msyncs for file based mmap" },
	{ NULL,		"mmap-bytes N",		"mmap and munmap N bytes for each stress iteration" },
	{ NULL,		"mmap-file",		"mmap onto a file using synchronous msyncs" },
	{ NULL,		"mmap-mprotect",	"enable mmap mprotect stressing" },
#if defined(STRESS_MMAPFORK)
	{ NULL,		"mmapfork N",		"start N workers stressing many forked mmaps/munmaps" },
	{ NULL,		"mmapfork-ops N",	"stop when N mmapfork bogo operations completed" },
#endif
	{ NULL,		"mmapmany N",		"start N workers stressing many mmaps and munmaps" },
	{ NULL,		"mmapmany-ops N",	"stop when N mmapmany bogo operations completed" },
#if defined(STRESS_MREMAP)
	{ NULL,		"mremap N",		"start N workers stressing mremap" },
	{ NULL,		"mremap-ops N",		"stop when N mremap bogo operations completed" },
	{ NULL,		"mremap-bytes N",	"mremap N bytes maximum for each stress iteration" },
#endif
#if defined(STRESS_MSG)
	{ NULL,		"msg N",		"start N workers stressing System V messages" },
	{ NULL,		"msg-ops N",		"stop msg workers after N bogo messages completed" },
#endif
#if defined(STRESS_MQ)
	{ NULL,		"mq N",			"start N workers passing messages using POSIX messages" },
	{ NULL,		"mq-ops N",		"stop mq workers after N bogo messages completed" },
	{ NULL,		"mq-size N",		"specify the size of the POSIX message queue" },
#endif
	{ NULL,		"nice N",		"start N workers that randomly re-adjust nice levels" },
	{ NULL,		"nice-ops N",		"stop when N nice bogo operations completed" },
	{ NULL,		"null N",		"start N workers writing to /dev/null" },
	{ NULL,		"null-ops N",		"stop when N /dev/null bogo write operations completed" },
#if defined(STRESS_NUMA)
	{ NULL,		"numa N",		"start N workers stressing NUMA interfaces" },
	{ NULL,		"numa-ops N",		"stop when N NUMA bogo operations completed" },
#endif
	{ "o",		"open N",		"start N workers exercising open/close" },
	{ NULL,		"open-ops N",		"stop when N open/close bogo operations completed" },
	{ "p N",	"pipe N",		"start N workers exercising pipe I/O" },
	{ NULL,		"pipe-ops N",		"stop when N pipe I/O bogo operations completed" },
	{ "P N",	"poll N",		"start N workers exercising zero timeout polling" },
	{ NULL,		"poll-ops N",		"stop when N poll bogo operations completed" },
#if defined(STRESS_PROCFS)
	{ NULL,		"procfs N",		"start N workers reading portions of /proc" },
	{ NULL,		"procfs-ops N",		"stop procfs workers after N bogo read operations" },
#endif
	{ NULL,		"pthread N",		"start N workers that create multiple threads" },
	{ NULL,		"pthread-ops N",	"stop pthread workers after N bogo threads created" },
	{ NULL,		"pthread-max P",	"create P threads at a time by each worker" },
#if defined(STRESS_PTRACE)
	{ NULL,		"ptrace N",		"start N workers that trace a child using ptrace" },
	{ NULL,		"ptrace-ops N",		"stop ptrace workers after N system calls are traced" },
#endif
	{ "Q",		"qsort N",		"start N workers qsorting 32 bit random integers" },
	{ NULL,		"qsort-ops N",		"stop when N qsort bogo operations completed" },
	{ NULL,		"qsort-size N",		"number of 32 bit integers to sort" },
#if defined(STRESS_QUOTA)
	{ NULL,		"quota N",		"start N workers exercising quotactl commands" },
	{ NULL,		"quota -ops N",		"stop when N quotactl bogo operations completed" },
#endif
#if defined(STRESS_RDRAND)
	{ NULL,		"rdrand N",		"start N workers exercising rdrand (x86 only)" },
	{ NULL,		"rdrand-ops N",		"stop when N rdrand bogo operations completed" },
#endif
#if defined(STRESS_READAHEAD)
	{ NULL,		"readahead N",		"start N workers exercising file readahead" },
	{ NULL,		"readahead-bytes N",	"size of file to readahead on (default is 1GB)" },
	{ NULL,		"readahead-ops N",	"stop when N readahead bogo operations completed" },
#endif
	{ "R",		"rename N",		"start N workers exercising file renames" },
	{ NULL,		"rename-ops N",		"stop when N rename bogo operations completed" },
#if defined(STRESS_RLIMIT)
	{ NULL,		"rlimit N",		"start N workers that exceed rlimits" },
	{ NULL,		"rlimit-ops N",		"stop when N rlimit bogo operations completed" },
#endif
	{ NULL,		"seek N",		"start N workers performing random seek r/w IO" },
	{ NULL,		"seek-ops N",		"stop when N seek bogo operations completed" },
	{ NULL,		"seek-size N",		"length of file to do random I/O upon" },
#if defined(STRESS_SEMAPHORE_POSIX)
	{ NULL,		"sem N",		"start N workers doing semaphore operations" },
	{ NULL,		"sem-ops N",		"stop when N semaphore bogo operations completed" },
	{ NULL,		"sem-procs N",		"number of processes to start per worker" },
#endif
#if defined(STRESS_SEMAPHORE_SYSV)
	{ NULL,		"sem-sysv N",		"start N workers doing System V semaphore operations" },
	{ NULL,		"sem-sysv-ops N",	"stop when N System V sem bogo operations completed" },
	{ NULL,		"sem-sysv-procs N",	"number of processes to start per worker" },
#endif
#if defined(STRESS_SENDFILE)
	{ NULL,		"sendfile N",		"start N workers exercising sendfile" },
	{ NULL,		"sendfile-ops N",	"stop after N bogo sendfile operations" },
	{ NULL,		"sendfile-size N",	"size of data to be sent with sendfile" },
#endif
	{ NULL,		"shm-sysv N",		"start N workers that exercise System V shared memory" },
	{ NULL,		"shm-sysv-ops N",	"stop after N shared memory bogo operations" },
	{ NULL,		"shm-sysv-bytes N",	"allocate and free N bytes of shared memory per loop" },
	{ NULL,		"shm-sysv-segs N",	"allocate N shared memory segments per iteration" },
#if defined(STRESS_SIGFD)
	{ NULL,		"sigfd N",		"start N workers reading signals via signalfd reads " },
	{ NULL,		"sigfd-ops N",		"stop when N bogo signalfd reads completed" },
#endif
	{ NULL,		"sigfpe N",		"start N workers generating floating point math faults" },
	{ NULL,		"sigfpe-ops N",		"stop when N bogo floating point math faults completed" },
	{ NULL,		"sigpending N",		"start N workers exercising sigpending" },
	{ NULL,		"sigpending-ops N",	"stop when N sigpending bogo operations completed" },
#if defined(STRESS_SIGQUEUE)
	{ NULL,		"sigq N",		"start N workers sending sigqueue signals" },
	{ NULL,		"sigq-ops N",		"stop when N siqqueue bogo operations completed" },
#endif
	{ NULL,		"sigsegv N",		"start N workers generating segmentation faults" },
	{ NULL,		"sigsegv-ops N",	"stop when N bogo segmentation faults completed" },
	{ NULL,		"sigsuspend N",		"start N workers exercising sigsuspend" },
	{ NULL,		"sigsuspend-ops N",	"stop when N bogo sigsuspend wakes completed" },
	{ "S N",	"sock N",		"start N workers exercising socket I/O" },
	{ NULL,		"sock-ops N",		"stop when N socket bogo operations completed" },
	{ NULL,		"sock-port P",		"use socket ports P to P + number of workers - 1" },
	{ NULL,		"sock-domain D",	"specify socket domain, default is ipv4" },
	{ NULL,		"sockpair N",		"start N workers exercising socket pair I/O activity" },
	{ NULL,		"sockpair-ops N",	"stop when N socket pair bogo operations completed" },
#if defined(STRESS_SPLICE)
	{ NULL,		"splice N",		"start N workers reading/writing using splice" },
	{ NULL,		"splice-ops N",		"stop when N bogo splice operations completed" },
	{ NULL,		"splice-bytes N",	"number of bytes to transfer per splice call" },
#endif
	{ NULL,		"stack N",		"start N workers generating stack overflows" },
	{ NULL,		"stack-ops N",		"stop when N bogo stack overflows completed" },
	{ NULL,		"stack-fill",		"fill stack, touches all new pages " },
	{ NULL,		"str N",		"start N workers exercising lib C string functions" },
	{ NULL,		"str-method func",	"specify the string function to stress" },
	{ NULL,		"str-ops N",		"stop when N bogo string operations completed" },
	{ "s N",	"switch N",		"start N workers doing rapid context switches" },
	{ NULL,		"switch-ops N",		"stop when N context switch bogo operations completed" },
	{ NULL,		"symlink N",		"start N workers creating symbolic links" },
	{ NULL,		"symlink-ops N",	"stop when N symbolic link bogo operations completed" },
	{ NULL,		"sysinfo N",		"start N workers reading system information" },
	{ NULL,		"sysinfo-ops N",	"stop when sysinfo bogo operations completed" },
#if defined(STRESS_SYSFS)
	{ NULL,		"sysfs N",		"start N workers reading files from /sys" },
	{ NULL,		"sysfs-ops N",		"stop when sysfs bogo operations completed" },
#endif
#if defined(STRESS_TEE)
	{ NULL,		"tee N",		"start N workers exercising the tee system call" },
	{ NULL,		"tee-ops N",		"stop after N tee bogo operations completed" },
#endif
#if defined(STRESS_TIMER)
	{ "T N",	"timer N",		"start N workers producing timer events" },
	{ NULL,		"timer-ops N",		"stop when N timer bogo events completed" },
	{ NULL,		"timer-freq F",		"run timer(s) at F Hz, range 1 to 1000000000" },
	{ NULL,		"timer-rand",		"enable random timer frequency" },
#endif
#if defined(STRESS_TIMERFD)
	{ NULL,		"timerfd N",		"start N workers producing timerfd events" },
	{ NULL,		"timerfd-ops N",	"stop when N timerfd bogo events completed" },
	{ NULL,		"timerfd-freq F",	"run timer(s) at F Hz, range 1 to 1000000000" },
	{ NULL,		"timerfd-rand",		"enable random timerfd frequency" },
#endif
	{ NULL,		"tsearch",		"start N workers that exercise a tree search" },
	{ NULL,		"tsearch-ops",		"stop when N tree search bogo operations completed" },
	{ NULL,		"tsearch-size",		"number of 32 bit integers to tsearch" },
	{ NULL,		"udp N",		"start N workers performing UDP send/receives " },
	{ NULL,		"udp-ops N",		"stop when N udp bogo operations completed" },
	{ NULL,		"udp-port P",		"use ports P to P + number of workers - 1" },
	{ NULL,		"udp-domain D",		"specify domain, default is ipv4" },
#if defined(STRESS_UDP_FLOOD)
	{ NULL,		"udp-flood N",		"start N workers that performs a UDP flood attack" },
	{ NULL,		"udp-flood-ops N",	"stop when N udp flood bogo operations completed" },
	{ NULL,		"udp-flood-domain D",	"specify domain, default is ipv4" },
#endif
#if defined(STRESS_URANDOM)
	{ "u N",	"urandom N",		"start N workers reading /dev/urandom" },
	{ NULL,		"urandom-ops N",	"stop when N urandom bogo read operations completed" },
#endif
	{ NULL,		"utime N",		"start N workers updating file timestamps" },
	{ NULL,		"utime-ops N",		"stop after N utime bogo operations completed" },
	{ NULL,		"utime-fsync",		"force utime meta data sync to the file system" },
#if defined(STRESS_VECMATH)
	{ NULL,		"vecmath N",		"start N workers performing vector math ops" },
	{ NULL,		"vecmath-ops N",	"stop after N vector math bogo operations completed" },
#endif
#if defined(STRESS_VFORK)
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
#if defined(STRESS_VM_RW)
	{ NULL,		"vm-rw N",		"start N vm read/write process_vm* copy workers" },
	{ NULL,		"vm-rw-bytes N",	"transfer N bytes of memory per bogo operation" },
	{ NULL,		"vm-rw-ops N",		"stop after N vm process_vm* copy bogo operations" },
#endif
#if defined(STRESS_VM_SPLICE)
	{ NULL,		"vm-splice N",		"start N workers reading/writing using vmsplice" },
	{ NULL,		"vm-splice-ops N",	"stop when N bogo splice operations completed" },
	{ NULL,		"vm-splice-bytes N",	"number of bytes to transfer per vmsplice call" },
#endif
	{ NULL,		"wcs N",		"start N workers on lib C wide character string functions" },
	{ NULL,		"wcs-method func",	"specify the wide character string function to stress" },
	{ NULL,		"wcs-ops N",		"stop when N bogo wide character string ops completed" },
#if defined(STRESS_WAIT)
	{ NULL,		"wait N",		"start N workers waiting on child being stop/resumed" },
	{ NULL,		"wait-ops N",		"stop when N bogo wait operations completed" },
#endif
#if defined(STRESS_YIELD)
	{ "y N",	"yield N",		"start N workers doing sched_yield() calls" },
	{ NULL,		"yield-ops N",		"stop when N bogo yield operations completed" },
#endif
#if defined(STRESS_XATTR)
	{ NULL,		"xattr N",		"start N workers stressing file extended attributes" },
	{ NULL,		"xattr-ops N",		"stop when N bogo xattr operations completed" },
#endif
	{ NULL,		"zero N",		"start N workers reading /dev/zero" },
	{ NULL,		"zero-ops N",		"stop when N /dev/zero bogo read operations completed" },
	{ NULL,		"zombie N",		"start N workers that rapidly create and reap zombies" },
	{ NULL,		"zombie-ops N",		"stop when N bogo zombie fork operations completed" },
	{ NULL,		"zombie-max N",		"set upper limit of N zombies per worker" },
	{ NULL,		NULL,			NULL }
};

/*
 *  stressor_id_find()
 *  	Find index into stressors by id
 */
static inline int32_t stressor_id_find(const stress_id id)
{
	int32_t i;

	for (i = 0; stressors[i].name; i++) {
		if (stressors[i].id == id)
			break;
	}

	return i;       /* End of array is a special "NULL" entry */
}

/*
 *  stressor_name_find()
 *  	Find index into stressors by name
 */
static inline int32_t stressor_name_find(const char *name)
{
	int32_t i;

	for (i = 0; stressors[i].name; i++) {
		if (!strcmp(stressors[i].name, name))
			break;
	}

	return i;       /* End of array is a special "NULL" entry */
}


/*
 *   stressor_instances()
 *	return the number of instances for a specific stress test
 */
int stressor_instances(const stress_id id)
{
	int32_t i = stressor_id_find(id);

	return procs[i].num_procs;
}


/*
 *  get_class()
 *	parse for allowed class types, return bit mask of types, 0 if error
 */
static uint32_t get_class(char *const class_str)
{
	char *str, *token;
	uint32_t class = 0;

	for (str = class_str; (token = strtok(str, ",")) != NULL; str = NULL) {
		int i;
		uint32_t cl = 0;

		for (i = 0; classes[i].class; i++) {
			if (!strcmp(classes[i].name, token)) {
				cl = classes[i].class;
				break;
			}
		}
		if (!cl) {
			fprintf(stderr, "Unknown class: '%s', available classes:", token);
			for (i = 0; classes[i].class; i++)
				fprintf(stderr, " %s", classes[i].name);
			fprintf(stderr, "\n");
			return 0;
		}
		class |= cl;
	}
	return class;
}

static int stress_exclude(char *const opt_exclude)
{
	char *str, *token;

	if (!opt_exclude)
		return 0;

	for (str = opt_exclude; (token = strtok(str, ",")) != NULL; str = NULL) {
		uint32_t i = stressor_name_find(token);
		if (!stressors[i].name) {
			fprintf(stderr, "Unknown stressor: '%s', invalid exclude option\n", token);
			return -1;
		}
		procs[i].exclude = true;
		procs[i].num_procs = 0;
	}
	return 0;
}

/*
 *  Catch signals and set flag to break out of stress loops
 */
static void MLOCKED stress_sigint_handler(int dummy)
{
	(void)dummy;
	opt_sigint = true;
	opt_do_run = false;
	opt_do_wait = false;
}

static void MLOCKED stress_sigalrm_child_handler(int dummy)
{
	(void)dummy;
	opt_do_run = false;
}

static void MLOCKED stress_sigalrm_parent_handler(int dummy)
{
	(void)dummy;
	opt_do_wait = false;
}

/*
 *  stress_sethandler()
 *	set signal handler to catch SIGINT and SIGALRM
 */
static int stress_sethandler(const char *stress, const bool child)
{
	struct sigaction new_action;

	new_action.sa_handler = stress_sigint_handler;
	sigemptyset(&new_action.sa_mask);
	new_action.sa_flags = 0;

	if (sigaction(SIGINT, &new_action, NULL) < 0) {
		pr_failed_err(stress, "sigaction");
		return -1;
	}

	new_action.sa_handler = child ?
		stress_sigalrm_child_handler :
		stress_sigalrm_parent_handler;
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
 *  usage_help()
 *	show generic help information
 */
static void usage_help(const help_t help_info[])
{
	size_t i;

	for (i = 0; help_info[i].description; i++) {
		char opt_s[10] = "";

		if (help_info[i].opt_s)
			snprintf(opt_s, sizeof(opt_s), "-%s,", help_info[i].opt_s);
		printf("%-6s--%-19s%s\n", opt_s,
			help_info[i].opt_l, help_info[i].description);
	}
}


/*
 *  usage()
 *	print some help
 */
static void usage(void)
{
	version();
	printf("\nUsage: %s [OPTION [ARG]]\n", app_name);
	printf("\nGeneral control options:\n");
	usage_help(help_generic);
	printf("\nStressor specific options:\n");
	usage_help(help_stressors);
	printf("\nExample: %s --cpu 8 --io 4 --vm 2 --vm-bytes 128M --fork 4 --timeout 10s\n\n"
	       "Note: Sizes can be suffixed with B,K,M,G and times with s,m,h,d,y\n", app_name);
	exit(EXIT_SUCCESS);
}

/*
 *  opt_name()
 *	find name associated with an option value
 */
static const char *opt_name(const int opt_val)
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
static void kill_procs(const int sig)
{
	static int count = 0;
	int i, signum = sig;

	/* multiple calls will always fallback to SIGKILL */
	count++;
	if (count > 5)
		signum = SIGKILL;

	for (i = 0; i < STRESS_MAX; i++) {
		int j;

		for (j = 0; j < procs[i].started_procs; j++) {
			if (procs[i].pids[j])
				(void)kill(procs[i].pids[j], signum);
		}
	}
}

/*
 *  wait_procs()
 * 	wait for procs
 */
static void MLOCKED wait_procs(bool *success)
{
	int i;

#if defined(STRESS_AFFINITY)
	/*
	 *  On systems that support changing CPU affinity
	 *  we keep on moving processed between processors
	 *  to impact on memory locality (e.g. NUMA) to
	 *  try to thrash the system when in aggressive mode
	 */
	if (opt_flags & OPT_FLAGS_AGGRESSIVE) {
		unsigned long int cpu = 0;
		const long ticks_per_sec = stress_get_ticks_per_second() * 5;
		const unsigned long usec_sleep = ticks_per_sec ? 1000000 / ticks_per_sec : 1000000 / 250;

		while (opt_do_wait) {
			const unsigned long cpus = stress_get_processors_configured();

			for (i = 0; i < STRESS_MAX; i++) {
				int j;

				for (j = 0; j < procs[i].started_procs; j++) {
					const pid_t pid = procs[i].pids[j];
					if (pid) {
						unsigned long int cpu_num = mwc32() % cpus;
						cpu_set_t mask;

						CPU_ZERO(&mask);
						CPU_SET(cpu_num, &mask);
						if (sched_setaffinity(pid, sizeof(mask), &mask) < 0)
							goto do_wait;
					}
				}
			}
			usleep(usec_sleep);
			cpu++;
		}
	}
do_wait:
#endif
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
						pr_err(stderr, "Process %d (stress-ng-%s) terminated with an error, exit status=%d\n",
							ret, stressors[i].name, WEXITSTATUS(status));
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
static void MLOCKED handle_sigint(int dummy)
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
static void MLOCKED stress_run(
	const int total_procs,
	const int32_t max_procs,
	const uint64_t opt_backoff,
	const int32_t opt_ionice_class,
	const int32_t opt_ionice_level,
	proc_stats_t stats[],
	double *duration,
	bool *success
)
{
	double time_start, time_finish;
	int32_t n_procs, i, j, n;

	opt_do_wait = true;
	time_start = time_now();
	pr_dbg(stderr, "starting stressors\n");
	for (n_procs = 0; n_procs < total_procs; n_procs++) {
		for (i = 0; i < STRESS_MAX; i++) {
			if (time_now() - time_start > opt_timeout)
				goto abort;

			j = procs[i].started_procs;
			if (j < procs[i].num_procs) {
				int rc = EXIT_SUCCESS;
				pid_t pid;
				char name[64];
again:
				if (!opt_do_run)
					break;
				pid = fork();
				switch (pid) {
				case -1:
					if (errno == EAGAIN) {
						usleep(100000);
						goto again;
					}
					pr_err(stderr, "Cannot fork: errno=%d (%s)\n",
						errno, strerror(errno));
					kill_procs(SIGALRM);
					goto wait_for_procs;
				case 0:
					/* Child */
					free_procs();
					if (stress_sethandler(name, true) < 0)
						exit(EXIT_FAILURE);

					(void)alarm(opt_timeout);
					mwc_reseed();
					snprintf(name, sizeof(name), "%s-%s", app_name,
						munge_underscore((char *)stressors[i].name));
					set_oom_adjustment(name, false);
					set_coredump(name);
					set_max_limits();
					set_iopriority(opt_ionice_class, opt_ionice_level);
					set_proc_name(name);

					pr_dbg(stderr, "%s: started [%d] (instance %" PRIu32 ")\n",
						name, getpid(), j);

					n = (i * max_procs) + j;
					stats[n].start = stats[n].finish = time_now();
#if defined(STRESS_PERF_STATS)
					if (opt_flags & OPT_FLAGS_PERF_STATS)
						(void)perf_open(&stats[n].sp);
#endif
					(void)usleep(opt_backoff * n_procs);
#if defined(STRESS_PERF_STATS)
					if (opt_flags & OPT_FLAGS_PERF_STATS)
						(void)perf_enable(&stats[n].sp);
#endif
					if (opt_do_run && !(opt_flags & OPT_FLAGS_DRY_RUN))
						rc = stressors[i].stress_func(&stats[n].counter, j, procs[i].bogo_ops, name);
#if defined(STRESS_PERF_STATS)
					if (opt_flags & OPT_FLAGS_PERF_STATS) {
						(void)perf_disable(&stats[n].sp);
						(void)perf_close(&stats[n].sp);
					}
#endif
#if defined(STRESS_THERMAL_ZONES)
					if (opt_flags & OPT_FLAGS_THERMAL_ZONES)
						(void)tz_get_temperatures(&shared->tz_info, &stats[n].tz);
#endif

					stats[n].finish = time_now();
					if (times(&stats[n].tms) == (clock_t)-1) {
						pr_dbg(stderr, "times failed: errno=%d (%s)\n",
							errno, strerror(errno));
					}
					pr_dbg(stderr, "%s: exited [%d] (instance %" PRIu32 ")\n",
						name, getpid(), j);
#if defined(STRESS_THERMAL_ZONES)
					tz_free(&shared->tz_info);
#endif
					exit(rc);
				default:
					if (pid > -1) {
						procs[i].pids[j] = pid;
						procs[i].started_procs++;
					}

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
	(void)stress_sethandler("stress-ng", false);
	(void)alarm(opt_timeout);

abort:
	pr_dbg(stderr, "%d stressors spawned\n", n_procs);

wait_for_procs:
	wait_procs(success);
	time_finish = time_now();

	*duration += time_finish - time_start;
}

/*
 *  show_hogs()
 *	show names of stressors that are going to be run
 */
static int show_hogs(const uint32_t opt_class)
{
	char *newstr, *str = NULL;
	ssize_t len = 0;
	char buffer[64];
	bool previous = false;
	int i;

	for (i = 0; i < STRESS_MAX; i++) {
		int32_t n;

		if (procs[i].exclude) {
			n = 0;
		} else {
			if (opt_flags & OPT_FLAGS_SEQUENTIAL) {
				if (opt_class) {
					n = (stressors[i].class & opt_class) ?  opt_sequential : 0;
				} else {
					n = opt_sequential;
				}
			} else {
				n = procs[i].num_procs;
			}
		}
		if (n) {
			ssize_t buffer_len;

			buffer_len = snprintf(buffer, sizeof(buffer), "%s %" PRId32 " %s",
				previous ? "," : "", n,
				munge_underscore((char *)stressors[i].name));
			previous = true;
			if (buffer_len >= 0) {
				newstr = realloc(str, len + buffer_len + 1);
				if (!newstr) {
					pr_err(stderr, "Cannot allocate temporary buffer\n");
					free(str);
					return -1;
				}
				str = newstr;
				strncpy(str + len, buffer, buffer_len + 1);
			}
			len += buffer_len;
		}
	}
	pr_inf(stdout, "dispatching hogs:%s\n", str ? str : "");
	free(str);
	fflush(stdout);

	return 0;
}

/*
 *  metrics_dump()
 *	output metrics
 */
static void metrics_dump(
	FILE *yaml,
	const int32_t max_procs,
	const int32_t ticks_per_sec)
{
	int32_t i;

	pr_inf(stdout, "%-12s %9.9s %9.9s %9.9s %9.9s %12s %12s\n",
		"stressor", "bogo ops", "real time", "usr time", "sys time", "bogo ops/s", "bogo ops/s");
	pr_inf(stdout, "%-12s %9.9s %9.9s %9.9s %9.9s %12s %12s\n",
		"", "", "(secs) ", "(secs) ", "(secs) ", "(real time)", "(usr+sys time)");
	pr_yaml(yaml, "metrics:\n");

	for (i = 0; i < STRESS_MAX; i++) {
		uint64_t c_total = 0, u_total = 0, s_total = 0, us_total;
		double   r_total = 0.0;
		int32_t  j, n = (i * max_procs);
		char *munged = munge_underscore((char *)stressors[i].name);
		double u_time, s_time, bogo_rate_r_time, bogo_rate;

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
		r_total = procs[i].started_procs ?
			r_total / (double)procs[i].started_procs : 0.0;

		if ((opt_flags & OPT_FLAGS_METRICS_BRIEF) && (c_total == 0))
			continue;

		u_time = (ticks_per_sec > 0) ? (double)u_total / (double)ticks_per_sec : 0.0;
		s_time = (ticks_per_sec > 0) ? (double)s_total / (double)ticks_per_sec : 0.0;
		bogo_rate_r_time = (r_total > 0.0) ? (double)c_total / r_total : 0.0;
		bogo_rate = (us_total > 0) ? (double)c_total / ((double)us_total / (double)ticks_per_sec) : 0.0;

		pr_inf(stdout, "%-12s %9" PRIu64 " %9.2f %9.2f %9.2f %12.2f %12.2f\n",
			munged,			/* stress test name */
			c_total,		/* op count */
			r_total,	 	/* average real (wall) clock time */
			u_time, 		/* actual user time */
			s_time,			/* actual system time */
			bogo_rate_r_time,	/* bogo ops on wall clock time */
			bogo_rate);		/* bogo ops per second */

		pr_yaml(yaml, "    - stressor: %s\n", munged);
		pr_yaml(yaml, "      bogo-ops: %" PRIu64 "\n", c_total);
		pr_yaml(yaml, "      bogo-ops-per-second-usr-sys-time: %f\n", bogo_rate);
		pr_yaml(yaml, "      bogo-ops-per-second-real-time: %f\n", bogo_rate_r_time);
		pr_yaml(yaml, "      wall-clock-time: %f\n", r_total);
		pr_yaml(yaml, "      user-time: %f\n", u_time);
		pr_yaml(yaml, "      system-time: %f\n", s_time);
		pr_yaml(yaml, "\n");
	}
}

/*
 *  times_dump()
 *	output the run times
 */
static void times_dump(
	FILE *yaml,
	const int32_t ticks_per_sec,
	const double duration)
{
	struct tms buf;
	double total_cpu_time = stress_get_processors_configured() * duration;
	double u_time, s_time, t_time, u_pc, s_pc, t_pc;

	if (times(&buf) == (clock_t)-1) {
		pr_err(stderr, "cannot get run time information: errno=%d (%s)\n",
			errno, strerror(errno));
		return;
	}
	u_time = (float)buf.tms_cutime / (float)ticks_per_sec;
	s_time = (float)buf.tms_cstime / (float)ticks_per_sec;
	t_time = ((float)buf.tms_cutime + (float)buf.tms_cstime) / (float)ticks_per_sec;
	u_pc = (total_cpu_time > 0.0) ? 100.0 * u_time / total_cpu_time : 0.0;
	s_pc = (total_cpu_time > 0.0) ? 100.0 * s_time / total_cpu_time : 0.0;
	t_pc = (total_cpu_time > 0.0) ? 100.0 * t_time / total_cpu_time : 0.0;

	pr_inf(stdout, "for a %.2fs run time:\n", duration);
	pr_inf(stdout, "  %8.2fs available CPU time\n",
		total_cpu_time);
	pr_inf(stdout, "  %8.2fs user time   (%6.2f%%)\n", u_time, u_pc);
	pr_inf(stdout, "  %8.2fs system time (%6.2f%%)\n", s_time, s_pc);
	pr_inf(stdout, "  %8.2fs total time  (%6.2f%%)\n", t_time, t_pc);

	pr_yaml(yaml, "times:\n");
	pr_yaml(yaml, "      run-time: %f\n", duration);
	pr_yaml(yaml, "      available-cpu-time: %f\n", total_cpu_time);
	pr_yaml(yaml, "      user-time: %f\n", u_time);
	pr_yaml(yaml, "      system-time: %f\n", s_time);
	pr_yaml(yaml, "      total-time: %f\n", t_time);
	pr_yaml(yaml, "      user-time-percent: %f\n", u_pc);
	pr_yaml(yaml, "      system-time-percent: %f\n", s_pc);
	pr_yaml(yaml, "      total-time-percent: %f\n", t_pc);
}

int main(int argc, char **argv)
{
	double duration = 0.0;			/* stressor run time in secs */
	size_t len;
	bool success = true;
	struct sigaction new_action;
	struct rlimit limit;
	char *opt_exclude = NULL;		/* List of stressors to exclude */
	char *yamlfile = NULL;			/* YAML filename */
	FILE *yaml = NULL;			/* YAML output file */
	int64_t opt_backoff = DEFAULT_BACKOFF;	/* child delay */
	int32_t id;				/* stressor id */
	int32_t ticks_per_sec;			/* clock ticks per second (jiffies) */
	int32_t opt_sched = UNDEFINED;		/* sched policy */
	int32_t opt_sched_priority = UNDEFINED;	/* sched priority */
	int32_t opt_ionice_class = UNDEFINED;	/* ionice class */
	int32_t opt_ionice_level = UNDEFINED;	/* ionice level */
	uint32_t opt_class = 0;			/* Which kind of class is specified */
	int32_t opt_random = 0, i;
	int32_t total_procs = 0, max_procs = 0;

	memset(procs, 0, sizeof(procs));
	mwc_reseed();

	(void)stress_get_pagesize();
	(void)stress_set_cpu_method("all");
	(void)stress_set_str_method("all");
	(void)stress_set_wcs_method("all");
	(void)stress_set_matrix_method("all");
	(void)stress_set_vm_method("all");

	if (stress_get_processors_configured() < 0) {
		pr_err(stderr, "sysconf failed, number of cpus configured unknown: errno=%d: (%s)\n",
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
		stress_id s_id;
next_opt:
		if ((c = getopt_long(argc, argv, "?hMVvqnt:b:c:i:m:d:f:s:l:p:P:C:S:a:y:F:D:T:u:o:r:B:R:k:Y:x:",
			long_options, &option_index)) == -1)
			break;

		for (s_id = 0; stressors[s_id].id != STRESS_MAX; s_id++) {
			if (stressors[s_id].short_getopt == c) {
				const char *name = opt_name(c);

				opt_flags |= OPT_FLAGS_SET;
				procs[s_id].num_procs = opt_long(name, optarg);
				if (procs[s_id].num_procs <= 0)
					procs[s_id].num_procs = stress_get_processors_configured();
				check_value(name, procs[s_id].num_procs);

				goto next_opt;
			}
			if (stressors[s_id].op == (stress_op)c) {
				procs[s_id].bogo_ops = get_uint64(optarg);
				check_range(opt_name(c), procs[s_id].bogo_ops,
					MIN_OPS, MAX_OPS);
				goto next_opt;
			}
		}

		switch (c) {
#if defined(STRESS_AIO)
		case OPT_AIO_REQUESTS:
			stress_set_aio_requests(optarg);
			break;
#endif
#if defined(STRESS_AIO_LINUX)
		case OPT_AIO_LINUX_REQUESTS:
			stress_set_aio_linux_requests(optarg);
			break;
#endif
		case OPT_ALL:
			opt_flags |= (OPT_FLAGS_SET | OPT_FLAGS_ALL);
			opt_all = opt_long("-a", optarg);
			if (opt_all <= 0)
				opt_all = stress_get_processors_configured();
			check_value("all", opt_all);
			break;
#if defined(STRESS_AFFINITY)
		case OPT_AFFINITY_RAND:
			opt_flags |= OPT_FLAGS_AFFINITY_RAND;
			break;
#endif
		case OPT_AGGRESSIVE:
			opt_flags |= OPT_FLAGS_AGGRESSIVE_MASK;
			break;
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
		case OPT_CACHE_PREFETCH:
			opt_flags |= OPT_FLAGS_CACHE_PREFETCH;
			break;
		case OPT_CACHE_FLUSH:
			opt_flags |= OPT_FLAGS_CACHE_FLUSH;
			break;
		case OPT_CACHE_FENCE:
			opt_flags |= OPT_FLAGS_CACHE_FENCE;
			break;
		case OPT_CLASS:
			opt_class = get_class(optarg);
			if (!opt_class)
				exit(EXIT_FAILURE);
			break;
#if defined(STRESS_CLONE)
		case OPT_CLONE_MAX:
			stress_set_clone_max(optarg);
			break;
#endif
		case OPT_CPU_LOAD:
			stress_set_cpu_load(optarg);
			break;
		case OPT_CPU_LOAD_SLICE:
			stress_set_cpu_load_slice(optarg);
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
#if defined(STRESS_EPOLL)
		case OPT_EPOLL_DOMAIN:
			if (stress_set_epoll_domain(optarg) < 0)
				exit(EXIT_FAILURE);
			break;
		case OPT_EPOLL_PORT:
			stress_set_epoll_port(optarg);
			break;
#endif
		case OPT_EXCLUDE:
			opt_exclude = optarg;
			break;
#if defined(STRESS_FALLOCATE)
		case OPT_FALLOCATE_BYTES:
			stress_set_fallocate_bytes(optarg);
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
#if defined(STRESS_IONICE)
		case OPT_IONICE_CLASS:
			opt_ionice_class = get_opt_ionice_class(optarg);
			break;
		case OPT_IONICE_LEVEL:
			opt_ionice_level = get_int32(optarg);
			break;
#endif
		case OPT_ITIMER_FREQ:
			stress_set_itimer_freq(optarg);
			break;
		case OPT_KEEP_NAME:
			opt_flags |= OPT_FLAGS_KEEP_NAME;
			break;
#if defined(STRESS_LEASE)
		case OPT_LEASE_BREAKERS:
			stress_set_lease_breakers(optarg);
			break;
#endif
#if defined(STRESS_LOCKF)
		case OPT_LOCKF_NONBLOCK:
			opt_flags |= OPT_FLAGS_LOCKF_NONBLK;
			break;
#endif
		case OPT_LOG_BRIEF:
			opt_flags |= OPT_FLAGS_LOG_BRIEF;
			break;
		case OPT_LSEARCH_SIZE:
			stress_set_lsearch_size(optarg);
			break;
		case OPT_MALLOC_BYTES:
			stress_set_malloc_bytes(optarg);
			break;
		case OPT_MALLOC_MAX:
			stress_set_malloc_max(optarg);
			break;
#if defined(STRESS_MALLOPT)
		case OPT_MALLOC_THRESHOLD:
			stress_set_malloc_threshold(optarg);
			break;
#endif
		case OPT_MATRIX_METHOD:
			if (stress_set_matrix_method(optarg) < 0)
				exit(EXIT_FAILURE);
			break;
		case OPT_MATRIX_SIZE:
			stress_set_matrix_size(optarg);
			break;
		case OPT_MAXIMIZE:
			opt_flags |= OPT_FLAGS_MAXIMIZE;
			break;
		case OPT_METRICS:
			opt_flags |= OPT_FLAGS_METRICS;
			break;
		case OPT_METRICS_BRIEF:
			opt_flags |= (OPT_FLAGS_METRICS_BRIEF | OPT_FLAGS_METRICS);
			break;
#if defined(STRESS_MINCORE)
		case OPT_MINCORE_RAND:
			opt_flags |= OPT_FLAGS_MINCORE_RAND;
			break;
#endif
		case OPT_MINIMIZE:
			opt_flags |= OPT_FLAGS_MINIMIZE;
			break;
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
#if defined(STRESS_MREMAP)
		case OPT_MREMAP_BYTES:
			stress_set_mremap_bytes(optarg);
			break;
#endif
#if defined(STRESS_MQ)
		case OPT_MQ_SIZE:
			stress_set_mq_size(optarg);
			break;
#endif
		case OPT_NO_MADVISE:
			opt_flags &= ~OPT_FLAGS_MMAP_MADVISE;
			break;
#if defined(STRESS_PAGE_IN)
		case OPT_PAGE_IN:
			opt_flags |= OPT_FLAGS_MMAP_MINCORE;
			break;
#endif
#if defined(STRESS_PERF_STATS)
		case OPT_PERF_STATS:
			opt_flags |= OPT_FLAGS_PERF_STATS;
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
			opt_random = get_uint64(optarg);
			if (opt_random <= 0)
				opt_random = stress_get_processors_configured();
			check_value("random", opt_random);
			break;
#if defined(STRESS_READAHEAD)
		case OPT_READAHEAD_BYTES:
			stress_set_readahead_bytes(optarg);
			break;
#endif
		case OPT_SCHED:
			opt_sched = get_opt_sched(optarg);
			break;
		case OPT_SCHED_PRIO:
			opt_sched_priority = get_int32(optarg);
			break;
		case OPT_SEEK_SIZE:
			stress_set_seek_size(optarg);
			break;
#if defined(STRESS_SEMAPHORE_POSIX)
		case OPT_SEMAPHORE_POSIX_PROCS:
			stress_set_semaphore_posix_procs(optarg);
			break;
#endif
#if defined(STRESS_SEMAPHORE_SYSV)
		case OPT_SEMAPHORE_SYSV_PROCS:
			stress_set_semaphore_sysv_procs(optarg);
			break;
#endif
#if defined(STRESS_SENDFILE)
		case OPT_SENDFILE_SIZE:
			stress_set_sendfile_size(optarg);
			break;
#endif
		case OPT_SEQUENTIAL:
			opt_flags |= OPT_FLAGS_SEQUENTIAL;
			opt_sequential = get_uint64(optarg);
			if (opt_sequential <= 0)
				opt_sequential = stress_get_processors_configured();
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
#if defined(STRESS_SPLICE)
		case OPT_SPLICE_BYTES:
			stress_set_splice_bytes(optarg);
			break;
#endif
		case OPT_STACK_FILL:
			opt_flags |= OPT_FLAGS_STACK_FILL;
			break;
		case OPT_STR_METHOD:
			if (stress_set_str_method(optarg) < 0)
				exit(EXIT_FAILURE);
			break;
		case OPT_SYSLOG:
			opt_flags |= OPT_FLAGS_SYSLOG;
			break;
		case OPT_TIMEOUT:
			opt_timeout = get_uint64_time(optarg);
			break;
#if defined(STRESS_TIMER)
		case OPT_TIMER_FREQ:
			stress_set_timer_freq(optarg);
			break;
		case OPT_TIMER_RAND:
			opt_flags |= OPT_FLAGS_TIMER_RAND;
			break;
#endif
#if defined(STRESS_TIMERFD)
		case OPT_TIMERFD_FREQ:
			stress_set_timerfd_freq(optarg);
			break;
		case OPT_TIMERFD_RAND:
			opt_flags |= OPT_FLAGS_TIMERFD_RAND;
			break;
#endif
		case OPT_TIMES:
			opt_flags |= OPT_FLAGS_TIMES;
			break;
		case OPT_TSEARCH_SIZE:
			stress_set_tsearch_size(optarg);
			break;
#if defined(STRESS_THERMAL_ZONES)
		case OPT_THERMAL_ZONES:
			opt_flags |= OPT_FLAGS_THERMAL_ZONES;
			break;
#endif
		case OPT_UDP_DOMAIN:
			if (stress_set_udp_domain(optarg) < 0)
				exit(EXIT_FAILURE);
			break;
		case OPT_UDP_PORT:
			stress_set_udp_port(optarg);
			break;
#if defined(STRESS_UDP_FLOOD)
		case OPT_UDP_FLOOD_DOMAIN:
			if (stress_set_udp_flood_domain(optarg) < 0)
				exit(EXIT_FAILURE);
			break;
#endif
		case OPT_UTIME_FSYNC:
			opt_flags |= OPT_FLAGS_UTIME_FSYNC;
			break;
		case OPT_VERBOSE:
			opt_flags |= PR_ALL;
			break;
#if defined(STRESS_VFORK)
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
			opt_flags |= OPT_FLAGS_VM_KEEP;
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
#if defined(STRESS_VM_RW)
		case OPT_VM_RW_BYTES:
			stress_set_vm_rw_bytes(optarg);
			break;
#endif
#if defined(STRESS_VM_SPLICE)
		case OPT_VM_SPLICE_BYTES:
			stress_set_vm_splice_bytes(optarg);
			break;
#endif
		case OPT_WCS_METHOD:
			if (stress_set_wcs_method(optarg) < 0)
				exit(EXIT_FAILURE);
			break;
		case OPT_YAML:
			yamlfile = optarg;
			break;
		case OPT_ZOMBIE_MAX:
			stress_set_zombie_max(optarg);
			break;
		default:
			printf("Unknown option (%d)\n",c);
			exit(EXIT_FAILURE);
		}
	}
	if (stress_exclude(opt_exclude) < 0)
		exit(EXIT_FAILURE);
	if ((opt_flags & (OPT_FLAGS_SEQUENTIAL | OPT_FLAGS_ALL)) ==
	    (OPT_FLAGS_SEQUENTIAL | OPT_FLAGS_ALL)) {
		fprintf(stderr, "cannot invoke --sequential and --all options together\n");
		exit(EXIT_FAILURE);
	}
	if (opt_class && !(opt_flags & (OPT_FLAGS_SEQUENTIAL | OPT_FLAGS_ALL))) {
		fprintf(stderr, "class option is only used with --sequential or --all options\n");
		exit(EXIT_FAILURE);
	}
	if (opt_flags & OPT_SYSLOG)
		openlog("stress-ng", 0, LOG_USER);

	pr_dbg(stderr, "%ld processors online, %ld processors configured\n",
		stress_get_processors_online(),
		stress_get_processors_configured());

	if ((opt_flags & OPT_FLAGS_MINMAX_MASK) == OPT_FLAGS_MINMAX_MASK) {
		fprintf(stderr, "maximize and minimize cannot be used together\n");
		exit(EXIT_FAILURE);
	}
#if defined(STRESS_RDRAND)
	id = stressor_id_find(STRESS_RDRAND);
	if ((procs[id].num_procs || (opt_flags & OPT_FLAGS_SEQUENTIAL)) &&
	    (stress_rdrand_supported() < 0))
		procs[id].num_procs = 0;
#endif
	if (opt_flags & OPT_FLAGS_RANDOM) {
		int32_t n = opt_random;

		if (opt_flags & OPT_FLAGS_SET) {
			pr_err(stderr, "Cannot specify random option with "
				"other stress processes selected\n");
			exit(EXIT_FAILURE);
		}
		/* create n randomly chosen stressors */
		while (n > 0) {
			int32_t rnd = mwc32() % ((opt_random >> 5) + 2);
			int32_t i = mwc32() % STRESS_MAX;

			if (!procs[i].exclude) {
				if (rnd > n)
					rnd = n;
				procs[i].num_procs += rnd;
				n -= rnd;
			}
		}
	}

#if defined(STRESS_PERF_STATS)
	if (opt_flags & OPT_FLAGS_PERF_STATS)
		perf_init();
#endif
	stress_cwd_readwriteable();
	set_oom_adjustment("main", false);
	set_coredump("main");
	set_sched(opt_sched, opt_sched_priority);
	set_iopriority(opt_ionice_class, opt_ionice_level);

#if defined(MLOCKED_SECTION)
	{
		extern void *__start_mlocked;
		extern void *__stop_mlocked;

		stress_mlock_region(&__start_mlocked, &__stop_mlocked);
	}
#endif
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

	if (opt_flags & OPT_FLAGS_SEQUENTIAL) {
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
	} else if (opt_flags & OPT_FLAGS_ALL) {
		if (total_procs) {
			pr_err(stderr, "the all option cannot be specified with other stressors enabled\n");
			free_procs();
			exit(EXIT_FAILURE);
		}
		if (opt_timeout == 0) {
			opt_timeout = DEFAULT_TIMEOUT;
			pr_inf(stdout, "defaulting to a %" PRIu64 " second run per stressor\n", opt_timeout);
		}

		for (i = 0; i < STRESS_MAX; i++) {
			if (!procs[i].exclude)
				procs[i].num_procs = opt_class ?
					(stressors[i].class & opt_class ?
						opt_all : 0) : opt_all;
			total_procs += procs[i].num_procs;
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
	} else {
		if (!total_procs) {
			pr_err(stderr, "No stress workers\n");
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
	if (show_hogs(opt_class) < 0) {
		free_procs();
		exit(EXIT_FAILURE);
	}
	len = sizeof(shared_t) + (sizeof(proc_stats_t) * STRESS_MAX * max_procs);
	shared = mmap(NULL, len, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANON, -1, 0);
	if (shared == MAP_FAILED) {
		pr_err(stderr, "Cannot mmap to shared memory region: errno=%d (%s)\n",
			errno, strerror(errno));
		free_procs();
		exit(EXIT_FAILURE);
	}
	memset(shared, 0, len);
	pthread_spin_init(&shared->perf.lock, 0);
	pthread_spin_init(&shared->sigsuspend.lock, 0);

#if defined(STRESS_THERMAL_ZONES)
	if (opt_flags & OPT_FLAGS_THERMAL_ZONES)
		tz_init(&shared->tz_info);
#endif
#if defined(STRESS_SEMAPHORE_POSIX)
	id = stressor_id_find(STRESS_SEMAPHORE_POSIX);
	if (procs[id].num_procs || (opt_flags & OPT_FLAGS_SEQUENTIAL))
		stress_semaphore_posix_init();
#endif
#if defined(STRESS_SEMAPHORE_SYSV)
	id = stressor_id_find(STRESS_SEMAPHORE_SYSV);
	if (procs[id].num_procs || (opt_flags & OPT_FLAGS_SEQUENTIAL))
		stress_semaphore_sysv_init();
#endif
	if (opt_flags & OPT_FLAGS_SEQUENTIAL) {
		/*
		 *  Step through each stressor one by one
		 */
		for (i = 0; opt_do_run && i < STRESS_MAX; i++) {
			int32_t j;

			for (j = 0; opt_do_run && j < STRESS_MAX; j++)
				procs[j].num_procs = 0;
			if (!procs[i].exclude) {
				procs[i].num_procs = opt_class ?
					(stressors[i].class & opt_class ?
						opt_sequential : 0) : opt_sequential;
				if (procs[i].num_procs)
					stress_run(opt_sequential, opt_sequential,
						opt_backoff, opt_ionice_class, opt_ionice_level,
						shared->stats, &duration, &success);
			}
		}
	} else {
		/*
		 *  Run all stressors in parallel
		 */
		stress_run(total_procs, max_procs,
			opt_backoff, opt_ionice_class, opt_ionice_level,
			shared->stats, &duration, &success);
	}
	pr_inf(stdout, "%s run completed in %.2fs%s\n",
		success ? "successful" : "unsuccessful",
		duration, duration_to_str(duration));
	if (yamlfile) {
		yaml = fopen(yamlfile, "w");
		if (!yaml)
			pr_err(stdout, "Cannot output YAML data to %s\n", yamlfile);

		pr_yaml(yaml, "---\n");
		pr_yaml_runinfo(yaml);
	}
	if (opt_flags & OPT_FLAGS_METRICS)
		metrics_dump(yaml, max_procs, ticks_per_sec);
#if defined(STRESS_PERF_STATS)
	if (opt_flags & OPT_FLAGS_PERF_STATS)
		perf_stat_dump(yaml, stressors, procs, max_procs, duration);
#endif
#if defined(STRESS_THERMAL_ZONES)
	if (opt_flags & OPT_FLAGS_THERMAL_ZONES) {
		tz_dump(yaml, shared, stressors, procs, max_procs);
		tz_free(&shared->tz_info);
	}
#endif
	if (opt_flags & OPT_FLAGS_TIMES)
		times_dump(yaml, ticks_per_sec, duration);
	free_procs();

#if defined(STRESS_SEMAPHORE_POSIX)
	stress_semaphore_posix_destroy();
#endif
#if defined(STRESS_SEMAPHORE_SYSV)
	stress_semaphore_sysv_destroy();
#endif
	(void)munmap(shared, len);
	if (opt_flags & OPT_SYSLOG)
		closelog();
	if (yaml) {
		pr_yaml(yaml, "...\n");
		fclose(yaml);
	}
	exit(EXIT_SUCCESS);
}
