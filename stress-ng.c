/*
 * Copyright (C) 2013-2016 Canonical, Ltd.
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
#if defined(__linux__)
#include <sys/utsname.h>
#include <sys/sysinfo.h>
#endif
#if defined(__sun__)
#include <alloca.h>
#endif

#include "stress-ng.h"

#if defined(__linux__) && NEED_GLIBC(2,3,0)
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
pid_t pgrp;					/* proceess group leader */


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
#if defined(STRESS_AF_ALG)
	STRESSOR(af_alg, AF_ALG, CLASS_CPU | CLASS_OS),
#endif
#if defined(STRESS_AIO)
	STRESSOR(aio, AIO, CLASS_IO | CLASS_INTERRUPT | CLASS_OS),
#endif
#if defined(STRESS_AIO_LINUX)
	STRESSOR(aiol, AIO_LINUX, CLASS_IO | CLASS_INTERRUPT | CLASS_OS),
#endif
#if defined(STRESS_APPARMOR)
	STRESSOR(apparmor, APPARMOR, CLASS_OS | CLASS_SECURITY),
#endif
#if defined(STRESS_ATOMIC)
	STRESSOR(atomic, ATOMIC, CLASS_CPU | CLASS_MEMORY),
#endif
	STRESSOR(bigheap, BIGHEAP, CLASS_OS | CLASS_VM),
#if defined(STRESS_BIND_MOUNT)
	STRESSOR(bind_mount, BIND_MOUNT, CLASS_FILESYSTEM | CLASS_OS | CLASS_PATHOLOGICAL),
#endif
	STRESSOR(brk, BRK, CLASS_OS | CLASS_VM),
	STRESSOR(bsearch, BSEARCH, CLASS_CPU_CACHE | CLASS_CPU | CLASS_MEMORY),
	STRESSOR(cache, CACHE, CLASS_CPU_CACHE),
#if defined(STRESS_CAP)
	STRESSOR(cap, CAP, CLASS_OS),
#endif
	STRESSOR(chdir, CHDIR, CLASS_FILESYSTEM | CLASS_OS),
	STRESSOR(chmod, CHMOD, CLASS_FILESYSTEM | CLASS_OS),
	STRESSOR(chown, CHOWN, CLASS_FILESYSTEM | CLASS_OS),
#if defined(STRESS_CLOCK)
	STRESSOR(clock, CLOCK, CLASS_INTERRUPT | CLASS_OS),
#endif
#if defined(STRESS_CLONE)
	STRESSOR(clone, CLONE, CLASS_SCHEDULER | CLASS_OS),
#endif
#if defined(STRESS_CONTEXT)
	STRESSOR(context, CONTEXT, CLASS_MEMORY | CLASS_CPU),
#endif
#if defined(STRESS_COPY_FILE)
	STRESSOR(copy_file, COPY_FILE, CLASS_FILESYSTEM | CLASS_OS),
#endif
	STRESSOR(cpu, CPU, CLASS_CPU),
#if defined(STRESS_CPU_ONLINE)
	STRESSOR(cpu_online, CPU_ONLINE, CLASS_CPU | CLASS_OS),
#endif
#if defined(STRESS_CRYPT)
	STRESSOR(crypt, CRYPT, CLASS_CPU),
#endif
	STRESSOR(daemon, DAEMON, CLASS_SCHEDULER | CLASS_OS),
	STRESSOR(dentry, DENTRY, CLASS_FILESYSTEM | CLASS_OS),
	STRESSOR(dir, DIR, CLASS_FILESYSTEM | CLASS_OS),
	STRESSOR(dup, DUP, CLASS_FILESYSTEM | CLASS_OS),
#if defined(STRESS_EPOLL)
	STRESSOR(epoll, EPOLL, CLASS_NETWORK | CLASS_OS),
#endif
#if defined(STRESS_EVENTFD)
	STRESSOR(eventfd, EVENTFD, CLASS_FILESYSTEM | CLASS_SCHEDULER | CLASS_OS),
#endif
#if defined(STRESS_EXEC)
	STRESSOR(exec, EXEC, CLASS_SCHEDULER | CLASS_OS),
#endif
#if defined(STRESS_FALLOCATE)
	STRESSOR(fallocate, FALLOCATE, CLASS_FILESYSTEM | CLASS_OS),
#endif
	STRESSOR(fault, FAULT, CLASS_INTERRUPT | CLASS_SCHEDULER | CLASS_OS),
	STRESSOR(fcntl, FCNTL, CLASS_FILESYSTEM | CLASS_OS),
#if defined(STRESS_FIEMAP)
	STRESSOR(fiemap, FIEMAP, CLASS_FILESYSTEM | CLASS_OS),
#endif
	STRESSOR(fifo, FIFO, CLASS_PIPE_IO | CLASS_OS | CLASS_SCHEDULER),
	STRESSOR(filename, FILENAME, CLASS_FILESYSTEM | CLASS_OS),
	STRESSOR(flock, FLOCK, CLASS_FILESYSTEM | CLASS_OS),
	STRESSOR(fork, FORK, CLASS_SCHEDULER | CLASS_OS),
	STRESSOR(fp_error, FP_ERROR, CLASS_CPU),
	STRESSOR(fstat, FSTAT, CLASS_FILESYSTEM | CLASS_OS),
#if defined(STRESS_FULL)
	STRESSOR(full, FULL, CLASS_DEV | CLASS_MEMORY | CLASS_OS),
#endif
#if defined(STRESS_FUTEX)
	STRESSOR(futex, FUTEX, CLASS_SCHEDULER | CLASS_OS),
#endif
	STRESSOR(get, GET, CLASS_OS),
#if defined(STRESS_GETRANDOM)
	STRESSOR(getrandom, GETRANDOM, CLASS_OS | CLASS_CPU),
#endif
#if defined(STRESS_GETDENT)
	STRESSOR(getdent, GETDENT, CLASS_FILESYSTEM | CLASS_OS),
#endif
#if defined(STRESS_HANDLE)
	STRESSOR(handle, HANDLE, CLASS_FILESYSTEM | CLASS_OS),
#endif
	STRESSOR(hdd, HDD, CLASS_IO | CLASS_OS),
#if defined(STRESS_HEAPSORT)
	STRESSOR(heapsort, HEAPSORT, CLASS_CPU_CACHE | CLASS_CPU | CLASS_MEMORY),
#endif
	STRESSOR(hsearch, HSEARCH, CLASS_CPU_CACHE | CLASS_CPU | CLASS_MEMORY),
#if defined(STRESS_ICACHE)
	STRESSOR(icache, ICACHE, CLASS_CPU_CACHE),
#endif
	STRESSOR(io, IOSYNC, CLASS_FILESYSTEM | CLASS_OS),
#if defined(STRESS_IOPRIO)
	STRESSOR(ioprio, IOPRIO, CLASS_FILESYSTEM | CLASS_OS),
#endif
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
#if defined(STRESS_KLOG)
	STRESSOR(klog, KLOG, CLASS_OS),
#endif
#if defined(STRESS_LEASE)
	STRESSOR(lease, LEASE, CLASS_FILESYSTEM | CLASS_OS),
#endif
	STRESSOR(link, LINK, CLASS_FILESYSTEM | CLASS_OS),
#if defined(STRESS_LOCKBUS)
	STRESSOR(lockbus, LOCKBUS, CLASS_CPU_CACHE | CLASS_MEMORY),
#endif
#if defined(STRESS_LOCKA)
	STRESSOR(locka, LOCKA, CLASS_FILESYSTEM | CLASS_OS),
#endif
#if defined(STRESS_LOCKF)
	STRESSOR(lockf, LOCKF, CLASS_FILESYSTEM | CLASS_OS),
#endif
#if defined(STRESS_LOCKOFD)
	STRESSOR(lockofd, LOCKOFD, CLASS_FILESYSTEM | CLASS_OS),
#endif
	STRESSOR(longjmp, LONGJMP, CLASS_CPU),
	STRESSOR(lsearch, LSEARCH, CLASS_CPU_CACHE | CLASS_CPU | CLASS_MEMORY),
#if defined(STRESS_MADVISE)
	STRESSOR(madvise, MADVISE, CLASS_VM | CLASS_OS),
#endif
	STRESSOR(malloc, MALLOC, CLASS_CPU_CACHE | CLASS_MEMORY | CLASS_VM | CLASS_OS),
	STRESSOR(matrix, MATRIX, CLASS_CPU | CLASS_CPU_CACHE | CLASS_MEMORY | CLASS_CPU),
#if defined(STRESS_MEMBARRIER)
	STRESSOR(membarrier, MEMBARRIER, CLASS_CPU_CACHE | CLASS_MEMORY),
#endif
	STRESSOR(memcpy, MEMCPY, CLASS_CPU_CACHE | CLASS_MEMORY),
#if defined(STRESS_MEMFD)
	STRESSOR(memfd, MEMFD, CLASS_OS | CLASS_MEMORY),
#endif
#if defined(STRESS_MERGESORT)
	STRESSOR(mergesort, MERGESORT, CLASS_CPU_CACHE | CLASS_CPU | CLASS_MEMORY),
#endif
#if defined(STRESS_MINCORE)
	STRESSOR(mincore, MINCORE, CLASS_OS | CLASS_MEMORY),
#endif
	STRESSOR(mknod, MKNOD, CLASS_FILESYSTEM | CLASS_OS),
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
#if defined(STRESS_MSYNC)
	STRESSOR(msync, MSYNC, CLASS_VM | CLASS_OS),
#endif
#if defined(STRESS_MQ)
	STRESSOR(mq, MQ, CLASS_SCHEDULER | CLASS_OS),
#endif
	STRESSOR(nice, NICE, CLASS_SCHEDULER | CLASS_OS),
	STRESSOR(null, NULL, CLASS_DEV | CLASS_MEMORY | CLASS_OS),
#if defined(STRESS_NUMA)
	STRESSOR(numa, NUMA, CLASS_CPU | CLASS_MEMORY | CLASS_OS),
#endif
#if defined(STRESS_OOM_PIPE)
	STRESSOR(oom_pipe, OOM_PIPE, CLASS_MEMORY | CLASS_OS),
#endif
#if defined(STRESS_OPCODE)
	STRESSOR(opcode, OPCODE, CLASS_CPU | CLASS_OS),
#endif
	STRESSOR(open, OPEN, CLASS_FILESYSTEM | CLASS_OS),
#if defined(STRESS_PERSONALITY)
	STRESSOR(personality, PERSONALITY, CLASS_OS),
#endif
	STRESSOR(pipe, PIPE, CLASS_PIPE_IO | CLASS_MEMORY | CLASS_OS),
	STRESSOR(poll, POLL, CLASS_SCHEDULER | CLASS_OS),
#if defined(STRESS_PROCFS)
	STRESSOR(procfs, PROCFS, CLASS_FILESYSTEM | CLASS_OS),
#endif
#if defined(STRESS_PTHREAD)
	STRESSOR(pthread, PTHREAD, CLASS_SCHEDULER | CLASS_OS),
#endif
#if defined(STRESS_PTRACE)
	STRESSOR(ptrace, PTRACE, CLASS_OS),
#endif
#if defined(STRESS_PTY)
	STRESSOR(pty, PTY, CLASS_OS),
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
#if defined(STRESS_REMAP_FILE_PAGES)
	STRESSOR(remap, REMAP_FILE_PAGES, CLASS_MEMORY | CLASS_OS),
#endif
	STRESSOR(rename, RENAME, CLASS_FILESYSTEM | CLASS_OS),
#if defined(STRESS_RLIMIT)
	STRESSOR(rlimit, RLIMIT, CLASS_OS),
#endif
#if defined(STRESS_RTC)
	STRESSOR(rtc, RTC, CLASS_OS),
#endif
#if defined(STRESS_SEAL)
	STRESSOR(seal, SEAL, CLASS_OS),
#endif
#if defined(STRESS_SECCOMP)
	STRESSOR(seccomp, SECCOMP, CLASS_OS),
#endif
	STRESSOR(seek, SEEK, CLASS_IO | CLASS_OS),
#if defined(STRESS_SEMAPHORE_POSIX)
	STRESSOR(sem, SEMAPHORE_POSIX, CLASS_OS | CLASS_SCHEDULER),
#endif
#if defined(STRESS_SEMAPHORE_SYSV)
	STRESSOR(sem_sysv, SEMAPHORE_SYSV, CLASS_OS | CLASS_SCHEDULER),
#endif
#if defined(STRESS_SHM_POSIX)
	STRESSOR(shm, SHM_POSIX, CLASS_VM | CLASS_OS),
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
#if defined(STRESS_SLEEP)
	STRESSOR(sleep, SLEEP, CLASS_INTERRUPT | CLASS_SCHEDULER | CLASS_OS),
#endif
	STRESSOR(sock, SOCKET, CLASS_NETWORK | CLASS_OS),
#if defined(STRESS_SOCKET_FD)
	STRESSOR(sockfd, SOCKET_FD, CLASS_NETWORK | CLASS_OS),
#endif
	STRESSOR(sockpair, SOCKET_PAIR, CLASS_NETWORK | CLASS_OS),
#if defined(STRESS_SPAWN)
	STRESSOR(spawn, SPAWN, CLASS_SCHEDULER | CLASS_OS),
#endif
#if defined(STRESS_SPLICE)
	STRESSOR(splice, SPLICE, CLASS_PIPE_IO | CLASS_OS),
#endif
	STRESSOR(stack, STACK, CLASS_VM | CLASS_MEMORY),
#if defined(STRESS_STACKMMAP)
	STRESSOR(stackmmap, STACKMMAP, CLASS_VM | CLASS_MEMORY),
#endif
	STRESSOR(str, STR, CLASS_CPU | CLASS_CPU_CACHE | CLASS_MEMORY),
	STRESSOR(stream, STREAM, CLASS_CPU | CLASS_CPU_CACHE | CLASS_MEMORY),
	STRESSOR(switch, SWITCH, CLASS_SCHEDULER | CLASS_OS),
	STRESSOR(symlink, SYMLINK, CLASS_FILESYSTEM | CLASS_OS),
#if defined(STRESS_SYNC_FILE)
	STRESSOR(sync_file, SYNC_FILE, CLASS_IO | CLASS_FILESYSTEM | CLASS_OS),
#endif
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
#if defined(STRESS_TLB_SHOOTDOWN)
	STRESSOR(tlb_shootdown, TLB_SHOOTDOWN, CLASS_OS | CLASS_MEMORY),
#endif
#if defined(STRESS_TSC)
	STRESSOR(tsc, TSC, CLASS_CPU),
#endif
	STRESSOR(tsearch, TSEARCH, CLASS_CPU_CACHE | CLASS_CPU | CLASS_MEMORY),
	STRESSOR(udp, UDP, CLASS_NETWORK | CLASS_OS),
#if defined(STRESS_UDP_FLOOD)
	STRESSOR(udp_flood, UDP_FLOOD, CLASS_NETWORK | CLASS_OS),
#endif
#if defined(STRESS_UNSHARE)
	STRESSOR(unshare, UNSHARE, CLASS_OS),
#endif
#if defined(STRESS_URANDOM)
	STRESSOR(urandom, URANDOM, CLASS_DEV | CLASS_OS),
#endif
#if defined(STRESS_USERFAULTFD)
	STRESSOR(userfaultfd, USERFAULTFD, CLASS_VM | CLASS_OS),
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
#if defined(STRESS_ZLIB)
	STRESSOR(zlib, ZLIB, CLASS_CPU | CLASS_CPU_CACHE | CLASS_MEMORY),
#endif
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
	{ CLASS_SECURITY,	"security" },
	{ CLASS_VM,		"vm" },
	{ 0,			NULL }
};

static const struct option long_options[] = {
#if defined(STRESS_AFFINITY)
	{ "affinity",	1,	0,	OPT_AFFINITY },
	{ "affinity-ops",1,	0,	OPT_AFFINITY_OPS },
	{ "affinity-rand",0,	0,	OPT_AFFINITY_RAND },
#endif
#if defined(STRESS_AF_ALG)
	{ "af-alg",	1,	0,	OPT_AF_ALG },
	{ "af-alg-ops",	1,	0,	OPT_AF_ALG_OPS },
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
#if defined(STRESS_APPARMOR)
	{ "apparmor",	1,	0,	OPT_APPARMOR },
	{ "apparmor-ops",1,	0,	OPT_APPARMOR_OPS },
#endif
#if defined(STRESS_ATOMIC)
	{ "atomic",	1,	0,	OPT_ATOMIC },
	{ "atomic-ops",	1,	0,	OPT_ATOMIC_OPS },
#endif
	{ "backoff",	1,	0,	OPT_BACKOFF },
	{ "bigheap",	1,	0,	OPT_BIGHEAP },
	{ "bigheap-ops",1,	0,	OPT_BIGHEAP_OPS },
	{ "bigheap-growth",1,	0,	OPT_BIGHEAP_GROWTH },
#if defined(STRESS_BIND_MOUNT)
	{ "bind-mount",	1,	0,	OPT_BIND_MOUNT },
	{ "bind-mount-ops",1,	0,	OPT_BIND_MOUNT_OPS },
#endif
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
	{ "cache-level",1,	0,	OPT_CACHE_LEVEL},
	{ "cache-ways",1,	0,	OPT_CACHE_WAYS},
	{ "cache-no-affinity",0,	0,	OPT_CACHE_NO_AFFINITY },
#if defined(STRESS_CAP)
	{ "cap",	1,	0, 	OPT_CAP },
	{ "cap-ops",	1,	0, 	OPT_CAP_OPS },
#endif
	{ "chdir",	1,	0, 	OPT_CHDIR },
	{ "chdir-ops",	1,	0, 	OPT_CHDIR_OPS },
	{ "chmod",	1,	0, 	OPT_CHMOD },
	{ "chmod-ops",	1,	0,	OPT_CHMOD_OPS },
	{ "chown",	1,	0, 	OPT_CHOWN},
	{ "chown-ops",	1,	0,	OPT_CHOWN_OPS },
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
#if defined(STRESS_COPY_FILE)
	{ "copy-file",	1,	0,	OPT_COPY_FILE },
	{ "copy-file-ops", 1,	0,	OPT_COPY_FILE_OPS },
	{ "copy-file-bytes", 1, 0,	OPT_COPY_FILE_BYTES },
#endif
	{ "cpu",	1,	0,	OPT_CPU },
	{ "cpu-ops",	1,	0,	OPT_CPU_OPS },
	{ "cpu-load",	1,	0,	OPT_CPU_LOAD },
	{ "cpu-load-slice",1,	0,	OPT_CPU_LOAD_SLICE },
	{ "cpu-method",	1,	0,	OPT_CPU_METHOD },
#if defined(STRESS_CPU_ONLINE)
	{ "cpu-online",	1,	0,	OPT_CPU_ONLINE },
	{ "cpu-online-ops",1,	0,	OPT_CPU_ONLINE_OPS },
#endif
#if defined(STRESS_CRYPT)
	{ "crypt",	1,	0,	OPT_CRYPT },
	{ "crypt-ops",	1,	0,	OPT_CRYPT_OPS },
#endif
	{ "daemon",	1,	0,	OPT_DAEMON },
	{ "daemon-ops",	1,	0,	OPT_DAEMON_OPS },
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
#if defined(STRESS_EXEC)
	{ "exec",	1,	0,	OPT_EXEC },
	{ "exec-ops",	1,	0,	OPT_EXEC_OPS },
	{ "exec-max",	1,	0,	OPT_EXEC_MAX },
#endif
#if defined(STRESS_FALLOCATE)
	{ "fallocate",	1,	0,	OPT_FALLOCATE },
	{ "fallocate-ops",1,	0,	OPT_FALLOCATE_OPS },
	{ "fallocate-bytes",1,	0,	OPT_FALLOCATE_BYTES },
#endif
	{ "fault",	1,	0,	OPT_FAULT },
	{ "fault-ops",	1,	0,	OPT_FAULT_OPS },
	{ "fcntl",	1,	0,	OPT_FCNTL},
	{ "fcntl-ops",	1,	0,	OPT_FCNTL_OPS },
#if defined(STRESS_FIEMAP)
	{ "fiemap",	1,	0,	OPT_FIEMAP },
	{ "fiemap-ops",	1,	0,	OPT_FIEMAP_OPS },
#endif
	{ "fifo",	1,	0,	OPT_FIFO },
	{ "fifo-ops",	1,	0,	OPT_FIFO_OPS },
	{ "fifo-readers",1,	0,	OPT_FIFO_READERS },
	{ "filename",	1,	0,	OPT_FILENAME },
	{ "filename-ops",1,	0,	OPT_FILENAME_OPS },
	{ "filename-opts",1,	0,	OPT_FILENAME_OPTS },
	{ "flock",	1,	0,	OPT_FLOCK },
	{ "flock-ops",	1,	0,	OPT_FLOCK_OPS },
	{ "fork",	1,	0,	OPT_FORK },
	{ "fork-ops",	1,	0,	OPT_FORK_OPS },
	{ "fork-max",	1,	0,	OPT_FORK_MAX },
	{ "fp-error",	1,	0,	OPT_FP_ERROR},
	{ "fp-error-ops",1,	0,	OPT_FP_ERROR_OPS },
	{ "fstat",	1,	0,	OPT_FSTAT },
	{ "fstat-ops",	1,	0,	OPT_FSTAT_OPS },
	{ "fstat-dir",	1,	0,	OPT_FSTAT_DIR },
#if defined(STRESS_FULL)
	{ "full",	1,	0,	OPT_FULL },
	{ "full-ops",	1,	0,	OPT_FULL_OPS },
#endif
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
#if defined(STRESS_GETDENT)
	{ "getdent",	1,	0,	OPT_GETDENT },
	{ "getdent-ops",1,	0,	OPT_GETDENT_OPS },
#endif
#if defined(STRESS_HANDLE)
	{ "handle",	1,	0,	OPT_HANDLE },
	{ "handle-ops",	1,	0,	OPT_HANDLE_OPS },
#endif
	{ "hdd",	1,	0,	OPT_HDD },
	{ "hdd-ops",	1,	0,	OPT_HDD_OPS },
	{ "hdd-bytes",	1,	0,	OPT_HDD_BYTES },
	{ "hdd-write-size", 1,	0,	OPT_HDD_WRITE_SIZE },
	{ "hdd-opts",	1,	0,	OPT_HDD_OPTS },
#if defined(STRESS_HEAPSORT)
	{ "heapsort",	1,	0,	OPT_HEAPSORT },
	{ "heapsort-ops",1,	0,	OPT_HEAPSORT_OPS },
	{ "heapsort-size",1,	0,	OPT_HEAPSORT_INTEGERS },
#endif
	{ "help",	0,	0,	OPT_HELP },
	{ "hsearch",	1,	0,	OPT_HSEARCH },
	{ "hsearch-ops",1,	0,	OPT_HSEARCH_OPS },
	{ "hsearch-size",1,	0,	OPT_HSEARCH_SIZE },
#if defined(STRESS_ICACHE)
	{ "icache",	1,	0,	OPT_ICACHE },
	{ "icache-ops",	1,	0,	OPT_ICACHE_OPS },
#endif
	{ "ignite-cpu",	0,	0, 	OPT_IGNITE_CPU },
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
#if defined(STRESS_IOPRIO)
	{ "ioprio",	1,	0,	OPT_IOPRIO },
	{ "ioprio-ops",	1,	0,	OPT_IOPRIO_OPS },
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
#if defined(STRESS_KLOG)
	{ "klog",	1,	0,	OPT_KLOG },
	{" klog-ops",	1,	0,	OPT_KLOG_OPS },
#endif
#if defined(STRESS_LEASE)
	{ "lease",	1,	0,	OPT_LEASE },
	{ "lease-ops",	1,	0,	OPT_LEASE_OPS },
	{ "lease-breakers",1,	0,	OPT_LEASE_BREAKERS },
#endif
	{ "link",	1,	0,	OPT_LINK },
	{ "link-ops",	1,	0,	OPT_LINK_OPS },
#if defined(STRESS_LOCKBUS)
	{ "lockbus",	1,	0,	OPT_LOCKBUS },
	{ "lockbus-ops",1,	0,	OPT_LOCKBUS_OPS },
#endif
#if defined(STRESS_LOCKA)
	{ "locka",	1,	0,	OPT_LOCKA },
	{ "locka-ops",	1,	0,	OPT_LOCKA_OPS },
#endif
#if defined(STRESS_LOCKF)
	{ "lockf",	1,	0,	OPT_LOCKF },
	{ "lockf-ops",	1,	0,	OPT_LOCKF_OPS },
	{ "lockf-nonblock", 0,	0,	OPT_LOCKF_NONBLOCK },
#endif
#if defined(STRESS_LOCKOFD)
	{ "lockofd",	1,	0,	OPT_LOCKOFD },
	{ "lockofd-ops",1,	0,	OPT_LOCKOFD_OPS },
#endif
	{ "log-brief",	0,	0,	OPT_LOG_BRIEF },
	{ "log-file",	1,	0,	OPT_LOG_FILE },
	{ "longjmp",	1,	0,	OPT_LONGJMP },
	{ "longjmp-ops",1,	0,	OPT_LONGJMP_OPS },
	{ "lsearch",	1,	0,	OPT_LSEARCH },
	{ "lsearch-ops",1,	0,	OPT_LSEARCH_OPS },
	{ "lsearch-size",1,	0,	OPT_LSEARCH_SIZE },
#if defined(STRESS_MADVISE)
	{ "madvise",	1,	0,	OPT_MADVISE },
	{ "madvise-ops",1,	0,	OPT_MADVISE_OPS },
#endif
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
#if defined(STRESS_MEMBARRIER)
	{ "membarrier",	1,	0,	OPT_MEMBARRIER },
	{ "membarrier-ops",1,	0,	OPT_MEMBARRIER_OPS },
#endif
	{ "memcpy",	1,	0,	OPT_MEMCPY },
	{ "memcpy",	1,	0,	OPT_MEMCPY },
	{ "memcpy",	1,	0,	OPT_MEMCPY },
	{ "memcpy-ops",	1,	0,	OPT_MEMCPY_OPS },
#if defined(STRESS_MEMFD)
	{ "memfd",	1,	0,	OPT_MEMFD },
	{ "memfd-ops",	1,	0,	OPT_MEMFD_OPS },
	{ "memfd-bytes",1,	0,	OPT_MEMFD_BYTES },
#endif
#if defined(STRESS_MERGESORT)
	{ "mergesort",	1,	0,	OPT_MERGESORT },
	{ "mergesort-ops",1,	0,	OPT_MERGESORT_OPS },
	{ "mergesort-size",1,	0,	OPT_MERGESORT_INTEGERS },
#endif
	{ "metrics",	0,	0,	OPT_METRICS },
	{ "metrics-brief",0,	0,	OPT_METRICS_BRIEF },
#if defined(STRESS_MINCORE)
	{ "mincore",	1,	0,	OPT_MINCORE },
	{ "mincore-ops",1,	0,	OPT_MINCORE_OPS },
	{ "mincore-random",0,	0,	OPT_MINCORE_RAND },
#endif
	{ "minimize",	0,	0,	OPT_MINIMIZE },
	{ "mknod",	1,	0,	OPT_MKNOD },
	{ "mknod-ops",	1,	0,	OPT_MKNOD_OPS },
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
#if defined(STRESS_MSYNC)
	{ "msync",	1,	0,	OPT_MSYNC },
	{ "msync-ops",	1,	0,	OPT_MSYNC_OPS },
	{ "msync-bytes",1,	0,	OPT_MSYNC_BYTES },
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
#if defined(STRESS_OOM_PIPE)
	{ "oom-pipe",	1,	0,	OPT_OOM_PIPE },
	{ "oom-pipe-ops",1,	0,	OPT_OOM_PIPE_OPS },
#endif
#if defined(STRESS_OPCODE)
	{ "opcode",	1,	0,	OPT_OPCODE },
	{ "opcode-ops",	1,	0,	OPT_OPCODE_OPS },
#endif
	{ "open",	1,	0,	OPT_OPEN },
	{ "open-ops",	1,	0,	OPT_OPEN_OPS },
#if defined(STRESS_PAGE_IN)
	{ "page-in",	0,	0,	OPT_PAGE_IN },
#endif
	{ "pathological",0,	0,	OPT_PATHOLOGICAL },
#if defined(STRESS_PERF_STATS)
	{ "perf",	0,	0,	OPT_PERF_STATS },
#endif
#if defined(STRESS_PERSONALITY)
	{ "personality",1,	0,	OPT_PERSONALITY },
	{ "personality-ops",1,	0,	OPT_PERSONALITY_OPS },
#endif
	{ "pipe",	1,	0,	OPT_PIPE },
	{ "pipe-ops",	1,	0,	OPT_PIPE_OPS },
	{ "pipe-data-size",1,	0,	OPT_PIPE_DATA_SIZE },
#if defined(F_SETPIPE_SZ)
	{ "pipe-size",	1,	0,	OPT_PIPE_SIZE },
#endif
	{ "poll",	1,	0,	OPT_POLL },
	{ "poll-ops",	1,	0,	OPT_POLL_OPS },
#if defined(STRESS_PROCFS)
	{ "procfs",	1,	0,	OPT_PROCFS },
	{ "procfs-ops",	1,	0,	OPT_PROCFS_OPS },
#endif
#if defined(STRESS_PTHREAD)
	{ "pthread",	1,	0,	OPT_PTHREAD },
	{ "pthread-ops",1,	0,	OPT_PTHREAD_OPS },
	{ "pthread-max",1,	0,	OPT_PTHREAD_MAX },
#endif
#if defined(STRESS_PTRACE)
	{ "ptrace",	1,	0,	OPT_PTRACE },
	{ "ptrace-ops",1,	0,	OPT_PTRACE_OPS },
#endif
#if defined(STRESS_PTY)
	{ "pty",	1,	0,	OPT_PTY },
	{ "pty-ops",	1,	0,	OPT_PTY_OPS },
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
#if defined(STRESS_REMAP_FILE_PAGES)
	{ "remap",	1,	0,	OPT_REMAP_FILE_PAGES },
	{ "remap-ops",	1,	0,	OPT_REMAP_FILE_PAGES_OPS },
#endif
	{ "rename",	1,	0,	OPT_RENAME },
	{ "rename-ops",	1,	0,	OPT_RENAME_OPS },
#if defined(STRESS_RLIMIT)
	{ "rlimit",	1,	0,	OPT_RLIMIT },
	{ "rlimit-ops",	1,	0,	OPT_RLIMIT_OPS },
#endif
#if defined(STRESS_RTC)
	{ "rtc",	1,	0,	OPT_RTC },
	{ "rtc-ops",	1,	0,	OPT_RTC_OPS },
#endif
	{ "sched",	1,	0,	OPT_SCHED },
	{ "sched-prio",	1,	0,	OPT_SCHED_PRIO },
#if defined(STRESS_SEAL)
	{ "seal",	1,	0,	OPT_SEAL },
	{ "seal-ops",	1,	0,	OPT_SEAL_OPS },
#endif
#if defined(STRESS_SECCOMP)
	{ "seccomp",	1,	0,	OPT_SECCOMP },
	{ "seccomp-ops",1,	0,	OPT_SECCOMP_OPS },
#endif
	{ "seek",	1,	0,	OPT_SEEK },
	{ "seek-ops",	1,	0,	OPT_SEEK_OPS },
#if defined(OPT_SEEK_PUNCH)
	{ "seek-punch",	0,	0,	OPT_SEEK_PUNCH  },
#endif
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
#if defined(STRESS_SHM_POSIX)
	{ "shm",	1,	0,	OPT_SHM_POSIX },
	{ "shm-ops",	1,	0,	OPT_SHM_POSIX_OPS },
	{ "shm-bytes",	1,	0,	OPT_SHM_POSIX_BYTES },
	{ "shm-objs",	1,	0,	OPT_SHM_POSIX_OBJECTS },
#endif
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
#if defined(STRESS_SLEEP)
	{ "sleep",	1,	0,	OPT_SLEEP },
	{ "sleep-ops",	1,	0,	OPT_SLEEP_OPS },
	{ "sleep-max",	1,	0,	OPT_SLEEP_MAX },
#endif
	{ "sock",	1,	0,	OPT_SOCKET },
	{ "sock-domain",1,	0,	OPT_SOCKET_DOMAIN },
	{ "sock-nodelay",0,	0,	OPT_SOCKET_NODELAY },
	{ "sock-ops",	1,	0,	OPT_SOCKET_OPS },
	{ "sock-opts",	1,	0,	OPT_SOCKET_OPTS },
	{ "sock-port",	1,	0,	OPT_SOCKET_PORT },
	{ "sock-type",	1,	0,	OPT_SOCKET_TYPE },
#if defined(STRESS_SOCKET_FD)
	{ "sockfd",	1,	0,	OPT_SOCKET_FD },
	{ "sockfd-ops",1,	0,	OPT_SOCKET_FD_OPS },
#endif
	{ "sockpair",	1,	0,	OPT_SOCKET_PAIR },
	{ "sockpair-ops",1,	0,	OPT_SOCKET_PAIR_OPS },
#if defined(STRESS_SPAWN)
	{ "spawn",	1,	0,	OPT_SPAWN },
	{ "spawn-ops",	1,	0,	OPT_SPAWN_OPS },
#endif
#if defined(STRESS_SPLICE)
	{ "splice",	1,	0,	OPT_SPLICE },
	{ "splice-bytes",1,	0,	OPT_SPLICE_BYTES },
	{ "splice-ops",	1,	0,	OPT_SPLICE_OPS },
#endif
	{ "stack",	1,	0,	OPT_STACK},
	{ "stack-fill",	0,	0,	OPT_STACK_FILL },
	{ "stack-ops",	1,	0,	OPT_STACK_OPS },
#if defined(STRESS_STACKMMAP)
	{ "stackmmap",	1,	0,	OPT_STACKMMAP },
	{ "stackmmap-ops",1,	0,	OPT_STACKMMAP_OPS },
#endif
	{ "str",	1,	0,	OPT_STR },
	{ "str-ops",	1,	0,	OPT_STR_OPS },
	{ "str-method",	1,	0,	OPT_STR_METHOD },
	{ "stream",	1,	0,	OPT_STREAM },
	{ "stream-ops",	1,	0,	OPT_STREAM_OPS },
	{ "stream-l3-size" ,1,	0,	OPT_STREAM_L3_SIZE },
	{ "switch",	1,	0,	OPT_SWITCH },
	{ "switch-ops",	1,	0,	OPT_SWITCH_OPS },
	{ "symlink",	1,	0,	OPT_SYMLINK },
	{ "symlink-ops",1,	0,	OPT_SYMLINK_OPS },
#if defined(STRESS_SYNC_FILE)
	{ "sync-file",	1,	0,	OPT_SYNC_FILE },
	{ "sync-file-ops", 1,	0,	OPT_SYNC_FILE_OPS },
	{ "sync-file-bytes", 1,	0,	OPT_SYNC_FILE_BYTES },
#endif
	{ "sysinfo",	1,	0,	OPT_SYSINFO },
	{ "sysinfo-ops",1,	0,	OPT_SYSINFO_OPS },
#if defined(STRESS_SYSFS)
	{ "sysfs",	1,	0,	OPT_SYSFS },
	{ "sysfs-ops",1,	0,	OPT_SYSFS_OPS },
#endif
	{ "syslog",	0,	0,	OPT_SYSLOG },
	{ "taskset",	1,	0,	OPT_TASKSET },
#if defined(STRESS_TEE)
	{ "tee",	1,	0,	OPT_TEE },
	{ "tee-ops",	1,	0,	OPT_TEE_OPS },
#endif
	{ "temp-path",	1,	0,	OPT_TEMP_PATH },
	{ "timeout",	1,	0,	OPT_TIMEOUT },
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
#if defined(PRCTL_TIMER_SLACK)
	{ "timer-slack",1,	0,	OPT_TIMER_SLACK },
#endif
#if defined(STRESS_TLB_SHOOTDOWN)
	{ "tlb-shootdown",1,	0,	OPT_TLB_SHOOTDOWN },
	{ "tlb-shootdown-ops",1,0,	OPT_TLB_SHOOTDOWN_OPS },
#endif
#if defined(STRESS_TSC)
	{ "tsc",	1,	0,	OPT_TSC },
	{ "tsc-ops",	1,	0,	OPT_TSC_OPS },
#endif
	{ "tsearch",	1,	0,	OPT_TSEARCH },
	{ "tsearch-ops",1,	0,	OPT_TSEARCH_OPS },
	{ "tsearch-size",1,	0,	OPT_TSEARCH_SIZE },
	{ "times",	0,	0,	OPT_TIMES },
#if defined(STRESS_THERMAL_ZONES)
	{ "tz",		0,	0,	OPT_THERMAL_ZONES },
#endif
	{ "udp",	1,	0,	OPT_UDP },
	{ "udp-ops",	1,	0,	OPT_UDP_OPS },
	{ "udp-domain",1,	0,	OPT_UDP_DOMAIN },
#if defined(OPT_UDP_LITE)
	{ "udp-lite",	0,	0,	OPT_UDP_LITE },
#endif
	{ "udp-port",	1,	0,	OPT_UDP_PORT },
#if defined(STRESS_UDP_FLOOD)
	{ "udp-flood",	1,	0,	OPT_UDP_FLOOD },
	{ "udp-flood-domain",1,	0,	OPT_UDP_FLOOD_DOMAIN },
	{ "udp-flood-ops",1,	0,	OPT_UDP_FLOOD_OPS },
#endif
#if defined(STRESS_USERFAULTFD)
	{ "userfaultfd",1,	0,	OPT_USERFAULTFD },
	{ "userfaultfd-ops",1,	0,	OPT_USERFAULTFD_OPS },
	{ "userfaultfd-bytes",1,0,	OPT_USERFAULTFD_BYTES },
#endif
	{ "utime",	1,	0,	OPT_UTIME },
	{ "utime-ops",	1,	0,	OPT_UTIME_OPS },
	{ "utime-fsync",0,	0,	OPT_UTIME_FSYNC },
#if defined(STRESS_UNSHARE)
	{ "unshare",	1,	0,	OPT_UNSHARE },
	{ "unshare-ops",1,	0,	OPT_UNSHARE_OPS },
#endif
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
#if defined(STRESS_ZLIB)
	{ "zlib",	1,	0,	OPT_ZLIB },
	{ "zlib-ops",	1,	0,	OPT_ZLIB_OPS },
#endif
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
	{ NULL,		"ignite-cpu",		"alter kernel controls to make CPU run hot" },
	{ "k",		"keep-name",		"keep stress worker names to be 'stress-ng'" },
	{ NULL,		"log-brief",		"less verbose log messages" },
	{ NULL,		"log-file filename",	"log messages to a log file" },
	{ NULL,		"maximize",		"enable maximum stress options" },
	{ "M",		"metrics",		"print pseudo metrics of activity" },
	{ NULL,		"metrics-brief",	"enable metrics and only show non-zero results" },
	{ NULL,		"minimize",		"enable minimal stress options" },
	{ NULL,		"no-madvise",		"don't use random madvise options for each mmap" },
#if defined(STRESS_PAGE_IN)
	{ NULL,		"page-in",		"touch allocated pages that are not in core" },
#endif
	{ NULL,		"pathological",		"enable stressors that are known to hang a machine" },
#if defined(STRESS_PERF_STATS)
	{ NULL,		"perf",			"display perf statistics" },
#endif
	{ "q",		"quiet",		"quiet output" },
	{ "r",		"random N",		"start N random workers" },
	{ NULL,		"sched type",		"set scheduler type" },
	{ NULL,		"sched-prio N",		"set scheduler priority level N" },
	{ NULL,		"sequential N",		"run all stressors one by one, invoking N of them" },
	{ NULL,		"syslog",		"log messages to the syslog" },
	{ NULL,		"taskset",		"use specific CPUs (set CPU affinity)" },
	{ NULL,		"temp-path",		"specify path for temporary directories and files" },
	{ "t N",	"timeout N",		"timeout after N seconds" },
	{ NULL,		"timer-slack",		"enable timer slack mode" },
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
	{ NULL, 	"affinity-ops N",   	"stop after N affinity bogo operations" },
	{ NULL, 	"affinity-rand",   	"change affinity randomly rather than sequentially" },
#endif
#if defined(STRESS_AF_ALG)
	{ NULL,		"af-alg N",		"start N workers that stress AF_ALG socket domain" },
	{ NULL,		"af-alg-ops N",		"stop after N af-alg bogo operations" },
#endif
#if defined(STRESS_AIO)
	{ NULL,		"aio N",		"start N workers that issue async I/O requests" },
	{ NULL,		"aio-ops N",		"stop after N bogo async I/O requests" },
	{ NULL,		"aio-requests N",	"number of async I/O requests per worker" },
#endif
#if defined(STRESS_AIO_LINUX)
	{ NULL,		"aiol N",		"start N workers that issue async I/O requests via Linux aio" },
	{ NULL,		"aiol-ops N",		"stop after N bogo Linux aio async I/O requests" },
	{ NULL,		"aiol-requests N",	"number of Linux aio async I/O requests per worker" },
#endif
#if defined(STRESS_APPARMOR)
	{ NULL,		"apparmor",		"start N workers exercising AppArmor interfaces" },
	{ NULL,		"apparmor-ops",		"stop after N bogo AppArmor worker bogo operations" },
#endif
#if defined(STRESS_ATOMIC)
	{ NULL,		"atomic",		"start N workers exercising GCC atomic operations" },
	{ NULL,		"atomic-ops",		"stop after N bogo atomic bogo operations" },
#endif
	{ "B N",	"bigheap N",		"start N workers that grow the heap using calloc()" },
	{ NULL,		"bigheap-ops N",	"stop after N bogo bigheap operations" },
	{ NULL, 	"bigheap-growth N",	"grow heap by N bytes per iteration" },
#if defined(STRESS_BIND_MOUNT)
	{ NULL,		"bind-mount N",		"start N workers exercising bind mounts" },
	{ NULL,		"bind-mount-ops N",	"stop after N bogo bind mount operations" },
#endif
	{ NULL,		"brk N",		"start N workers performing rapid brk calls" },
	{ NULL,		"brk-ops N",		"stop after N brk bogo operations" },
	{ NULL,		"brk-notouch",		"don't touch (page in) new data segment page" },
	{ NULL,		"bsearch N",		"start N workers that exercise a binary search" },
	{ NULL,		"bsearch-ops N",	"stop after N binary search bogo operations" },
	{ NULL,		"bsearch-size N",	"number of 32 bit integers to bsearch" },
	{ "C N",	"cache N",		"start N CPU cache thrashing workers" },
	{ NULL,		"cache-ops N",		"stop after N cache bogo operations" },
	{ NULL,		"cache-prefetch",	"prefetch on memory reads/writes" },
	{ NULL,		"cache-flush",		"flush cache after every memory write (x86 only)" },
	{ NULL,		"cache-fence",		"serialize stores" },
	{ NULL,		"cache-level N",	"only exercise specified cache" },
	{ NULL,		"cache-ways N",		"only fill specified number of cache ways" },
#if defined(STRESS_CAP)
	{ NULL,		"cap N",		"start N workers exercsing capget" },
	{ NULL,		"cap-ops N",		"stop cap workers after N bogo capget operations" },
#endif
	{ NULL,		"chdir N",		"start N workers thrashing chdir on many paths" },
	{ NULL,		"chdir-ops N",		"stop chdir workers after N bogo chdir operations" },
	{ NULL,		"chmod N",		"start N workers thrashing chmod file mode bits " },
	{ NULL,		"chmod-ops N",		"stop chmod workers after N bogo operations" },
	{ NULL,		"chown N",		"start N workers thrashing chown file ownership" },
	{ NULL,		"chown-ops N",		"stop chown workers after N bogo operations" },
#if defined(STRESS_CLOCK)
	{ NULL,		"clock N",		"start N workers thrashing clocks and POSIX timers" },
	{ NULL,		"clock-ops N",		"stop clock workers after N bogo operations" },
#endif
#if defined(STRESS_CLONE)
	{ NULL,		"clone N",		"start N workers that rapidly create and reap clones" },
	{ NULL,		"clone-ops N",		"stop after N bogo clone operations" },
	{ NULL,		"clone-max N",		"set upper limit of N clones per worker" },
#endif
#if defined(STRESS_CONTEXT)
	{ NULL,		"context N",		"start N workers exercising user context" },
	{ NULL,		"context-ops N",	"stop context workers after N bogo operations" },
#endif
#if defined(STRESS_COPY_FILE)
	{ NULL,		"copy-file N",		"start N workers that copy file data" },
	{ NULL,		"copy-file-ops N",	"stop after N copy bogo operations" },
	{ NULL,		"copy-file-bytes N",	"specify size of file to be copied" },
#endif
	{ "c N",	"cpu N",		"start N workers spinning on sqrt(rand())" },
	{ NULL,		"cpu-ops N",		"stop after N cpu bogo operations" },
	{ "l P",	"cpu-load P",		"load CPU by P %%, 0=sleep, 100=full load (see -c)" },
	{ NULL,		"cpu-load-slice S",	"specify time slice during busy load" },
	{ NULL,		"cpu-method m",		"specify stress cpu method m, default is all" },
#if defined(STRESS_CPU_ONLINE)
	{ NULL,		"cpu-online N",		"start N workers offlining/onlining the CPUs" },
	{ NULL,		"cpu-online-ops N",	"stop after N offline/online operations" },
#endif
#if defined(STRESS_CRYPT)
	{ NULL,		"crypt N",		"start N workers performing password encryption" },
	{ NULL,		"crypt-ops N",		"stop after N bogo crypt operations" },
#endif
	{ NULL,		"daemon N",		"start N workers creating multiple daemons" },
	{ NULL,		"daemon-ops N",		"stop when N daemons have been created" },
	{ "D N",	"dentry N",		"start N dentry thrashing stressors" },
	{ NULL,		"dentry-ops N",		"stop after N dentry bogo operations" },
	{ NULL,		"dentry-order O",	"specify dentry unlink order (reverse, forward, stride)" },
	{ NULL,		"dentries N",		"create N dentries per iteration" },
	{ NULL,		"dir N",		"start N directory thrashing stressors" },
	{ NULL,		"dir-ops N",		"stop after N directory bogo operations" },
	{ NULL,		"dup N",		"start N workers exercising dup/close" },
	{ NULL,		"dup-ops N",		"stop after N dup/close bogo operations" },
#if defined(STRESS_EPOLL)
	{ NULL,		"epoll N",		"start N workers doing epoll handled socket activity" },
	{ NULL,		"epoll-ops N",		"stop after N epoll bogo operations" },
	{ NULL,		"epoll-port P",		"use socket ports P upwards" },
	{ NULL,		"epoll-domain D",	"specify socket domain, default is unix" },
#endif
#if defined(STRESS_EVENTFD)
	{ NULL,		"eventfd N",		"start N workers stressing eventfd read/writes" },
	{ NULL,		"eventfd-ops N",	"stop eventfd workers after N bogo operations" },
#endif
#if defined(STRESS_EXEC)
	{ NULL,		"exec N",		"start N workers spinning on fork() and exec()" },
	{ NULL,		"exec-ops N",		"stop after N exec bogo operations" },
	{ NULL,		"exec-max P",		"create P workers per iteration, default is 1" },
#endif
#if defined(STRESS_FALLOCATE)
	{ NULL,		"fallocate N",		"start N workers fallocating 16MB files" },
	{ NULL,		"fallocate-ops N",	"stop after N fallocate bogo operations" },
	{ NULL,		"fallocate-bytes N",	"specify size of file to allocate" },
#endif
	{ NULL,		"fault N",		"start N workers producing page faults" },
	{ NULL,		"fault-ops N",		"stop after N page fault bogo operations" },
#if defined(STRESS_FIEMAP)
	{ NULL,		"fiemap N",		"start N workers exercising the FIEMAP ioctl" },
	{ NULL,		"fiemap-ops N",		"stop after N FIEMAP ioctl bogo operations" },
#endif
	{ NULL,		"fifo N",		"start N workers exercising fifo I/O" },
	{ NULL,		"fifo-ops N",		"stop after N fifo bogo operations" },
	{ NULL,		"fifo-readers N",	"number of fifo reader stessors to start" },
	{ NULL,		"filename N",		"start N workers exercising filenames" },
	{ NULL,		"filename-ops N",	"stop after N filename bogo operations" },
	{ NULL,		"filename-opts opt",	"specify allowed filename options" },
	{ NULL,		"fcntl N",		"start N workers exercising fcntl commands" },
	{ NULL,		"fcntl-ops N",		"stop after N fcntl bogo operations" },
	{ NULL,		"flock N",		"start N workers locking a single file" },
	{ NULL,		"flock-ops N",		"stop after N flock bogo operations" },
	{ "f N",	"fork N",		"start N workers spinning on fork() and exit()" },
	{ NULL,		"fork-ops N",		"stop after N fork bogo operations" },
	{ NULL,		"fork-max P",		"create P workers per iteration, default is 1" },
	{ NULL,		"fp-error N",		"start N workers exercising floating point errors" },
	{ NULL,		"fp-error-ops N",	"stop after N fp-error bogo operations" },
	{ NULL,		"fstat N",		"start N workers exercising fstat on files" },
	{ NULL,		"fstat-ops N",		"stop after N fstat bogo operations" },
	{ NULL,		"fstat-dir path",	"fstat files in the specified directory" },
#if defined(STRESS_FULL)
	{ NULL,		"full N",		"start N workers exercising /dev/full" },
	{ NULL,		"full-ops N",		"stop after N /dev/full bogo I/O operations" },
#endif
#if defined(STRESS_FUTEX)
	{ NULL,		"futex N",		"start N workers exercising a fast mutex" },
	{ NULL,		"futex-ops N",		"stop after N fast mutex bogo operations" },
#endif
	{ NULL,		"get N",		"start N workers exercising the get*() system calls" },
	{ NULL,		"get-ops N",		"stop after N get bogo operations" },
#if defined(STRESS_GETDENT)
	{ NULL,		"getdent N",		"start N workers reading directories using getdents" },
	{ NULL,		"getdent-ops N",	"stop after N getdents bogo operations" },
#endif
#if defined(STRESS_GETRANDOM)
	{ NULL,		"getrandom N",		"start N workers fetching random data via getrandom()" },
	{ NULL,		"getrandom-ops N",	"stop after N getrandom bogo operations" },
#endif
#if defined(STRESS_HANDLE)
	{ NULL,		"handle N",		"start N workers exercising name_to_handle_at" },
	{ NULL,		"handle-ops N",		"stop after N handle bogo operations" },
#endif
	{ "d N",	"hdd N",		"start N workers spinning on write()/unlink()" },
	{ NULL,		"hdd-ops N",		"stop after N hdd bogo operations" },
	{ NULL,		"hdd-bytes N",		"write N bytes per hdd worker (default is 1GB)" },
	{ NULL,		"hdd-opts list",	"specify list of various stressor options" },
	{ NULL,		"hdd-write-size N",	"set the default write size to N bytes" },
#if defined(STRESS_HEAPSORT)
	{ NULL,		"heapsort N",		"start N workers heap sorting 32 bit random integers" },
	{ NULL,		"heapsort-ops N",	"stop after N heap sort bogo operations" },
	{ NULL,		"heapsort-size N",	"number of 32 bit integers to sort" },
#endif
	{ NULL,		"hsearch N",		"start N workers that exercise a hash table search" },
	{ NULL,		"hsearch-ops N",	"stop afer N hash search bogo operations" },
	{ NULL,		"hsearch-size N",	"number of integers to insert into hash table" },
#if defined(STRESS_ICACHE)
	{ NULL,		"icache N",		"start N CPU instruction cache thrashing workers" },
	{ NULL,		"icache-ops N",		"stop after N icache bogo operations" },
#endif
#if defined(STRESS_INOTIFY)
	{ NULL,		"inotify N",		"start N workers exercising inotify events" },
	{ NULL,		"inotify-ops N",	"stop inotify workers after N bogo operations" },
#endif
	{ "i N",	"io N",			"start N workers spinning on sync()" },
	{ NULL,		"io-ops N",		"stop after N io bogo operations" },
#if defined(STRESS_IONICE)
	{ NULL,		"ionice-class C",	"specify ionice class (idle, besteffort, realtime)" },
	{ NULL,		"ionice-level L",	"specify ionice level (0 max, 7 min)" },
#endif
#if defined(STRESS_IOPRIO)
	{ NULL,		"ioprio N",		"start N workers exercising set/get iopriority" },
	{ NULL,		"ioprio-ops N",		"stop after N io bogo iopriority operations" },
#endif
	{ NULL,		"itimer N",		"start N workers exercising interval timers" },
	{ NULL,		"itimer-ops N",		"stop after N interval timer bogo operations" },
#if defined(STRESS_KCMP)
	{ NULL,		"kcmp N",		"start N workers exercising kcmp" },
	{ NULL,		"kcmp-ops N",		"stop after N kcmp bogo operations" },
#endif
#if defined(STRESS_KEY)
	{ NULL,		"key N",		"start N workers exercising key operations" },
	{ NULL,		"key-ops N",		"stop after N key bogo operations" },
#endif
	{ NULL,		"kill N",		"start N workers killing with SIGUSR1" },
	{ NULL,		"kill-ops N",		"stop after N kill bogo operations" },
#if defined(STRESS_KLOG)
	{ NULL,		"klog N",		"start N workers exercising kernel syslog interface" },
	{ NULL,		"klog -ops N",		"stop after N klog bogo operations" },
#endif
#if defined(STRESS_LEASE)
	{ NULL,		"lease N",		"start N workers holding and breaking a lease" },
	{ NULL,		"lease-ops N",		"stop after N lease bogo operations" },
	{ NULL,		"lease-breakers N",	"number of lease breaking workers to start" },
#endif
	{ NULL,		"link N",		"start N workers creating hard links" },
	{ NULL,		"link-ops N",		"stop after N link bogo operations" },
#if defined(STRESS_LOCKBUS)
	{ NULL,		"lockbus N",		"start N workers locking a memory increment" },
	{ NULL,		"lockbus-ops N",	"stop after N lockbus bogo operations" },
#endif
#if defined(STRESS_LOCKA)
	{ NULL,		"locka N",		"start N workers locking a single file via advisory locks" },
	{ NULL,		"locka-ops N",		"stop after N locka bogo operations" },
#endif
#if defined(STRESS_LOCKF)
	{ NULL,		"lockf N",		"start N workers locking a single file via lockf" },
	{ NULL,		"lockf-ops N",		"stop after N lockf bogo operations" },
	{ NULL,		"lockf-nonblock",	"don't block if lock cannot be obtained, re-try" },
#endif
#if defined(STRESS_LOCKOFD)
	{ NULL,		"lockofd N",		"start N workers locking with open file description locks" },
	{ NULL,		"lockofd-ops N",	"stop after N lockofd bogo operations" },
#endif
	{ NULL,		"longjmp N",		"start N workers exercising setjmp/longjmp" },
	{ NULL,		"longjmp-ops N",	"stop after N longjmp bogo operations" },
	{ NULL,		"lsearch N",		"start N workers that exercise a linear search" },
	{ NULL,		"lsearch-ops N",	"stop after N linear search bogo operations" },
	{ NULL,		"lsearch-size N",	"number of 32 bit integers to lsearch" },
#if defined(STRESS_MADVISE)
	{ NULL,		"madvise N",		"start N workers exercising madvise on memory" },
	{ NULL,		"madvise-ops N",	"stop after N bogo madvise operations" },
#endif
	{ NULL,		"malloc N",		"start N workers exercising malloc/realloc/free" },
	{ NULL,		"malloc-bytes N",	"allocate up to N bytes per allocation" },
	{ NULL,		"malloc-max N",		"keep up to N allocations at a time" },
	{ NULL,		"malloc-ops N",		"stop after N malloc bogo operations" },
#if defined(STRESS_MALLOPT)
	{ NULL,		"malloc-thresh N",	"threshold where malloc uses mmap instead of sbrk" },
#endif
	{ NULL,		"matrix N",		"start N workers exercising matrix operations" },
	{ NULL,		"matrix-ops N",		"stop after N maxtrix bogo operations" },
	{ NULL,		"matrix-method m",	"specify matrix stress method m, default is all" },
	{ NULL,		"matrix-size N",	"specify the size of the N x N matrix" },
#if defined(STRESS_MEMBARRIER)
	{ NULL,		"membarrier N",		"start N workers performing membarrier system calls" },
	{ NULL,		"membarrier-ops N",	"stop after N membarrier bogo operations" },
#endif
	{ NULL,		"memcpy N",		"start N workers performing memory copies" },
	{ NULL,		"memcpy-ops N",		"stop after N memcpy bogo operations" },
#if defined(STRESS_MEMFD)
	{ NULL,		"memfd N",		"start N workers allocating memory with memfd_create" },
	{ NULL,		"memfd-bytes N",	"allocate N bytes for each stress iteration" },
	{ NULL,		"memfd-ops N",		"stop after N memfd bogo operations" },
#endif
#if defined(STRESS_MERGESORT)
	{ NULL,		"mergesort N",		"start N workers merge sorting 32 bit random integers" },
	{ NULL,		"mergesort-ops N",	"stop after N merge sort bogo operations" },
	{ NULL,		"mergesort-size N",	"number of 32 bit integers to sort" },
#endif
#if defined(STRESS_MINCORE)
	{ NULL,		"mincore N",		"start N workers exercising mincore" },
	{ NULL,		"mincore-ops N",	"stop after N mincore bogo operations" },
	{ NULL,		"mincore-random",	"randomly select pages rather than linear scan" },
#endif
	{ NULL,		"mknod N",		"start N workers that exercise mknod" },
	{ NULL,		"mknod-ops N",		"stop after N mknod bogo operations" },
#if defined(STRESS_MLOCK)
	{ NULL,		"mlock N",		"start N workers exercising mlock/munlock" },
	{ NULL,		"mlock-ops N",		"stop after N mlock bogo operations" },
#endif
	{ NULL,		"mmap N",		"start N workers stressing mmap and munmap" },
	{ NULL,		"mmap-ops N",		"stop after N mmap bogo operations" },
	{ NULL,		"mmap-async",		"using asynchronous msyncs for file based mmap" },
	{ NULL,		"mmap-bytes N",		"mmap and munmap N bytes for each stress iteration" },
	{ NULL,		"mmap-file",		"mmap onto a file using synchronous msyncs" },
	{ NULL,		"mmap-mprotect",	"enable mmap mprotect stressing" },
#if defined(STRESS_MMAPFORK)
	{ NULL,		"mmapfork N",		"start N workers stressing many forked mmaps/munmaps" },
	{ NULL,		"mmapfork-ops N",	"stop after N mmapfork bogo operations" },
#endif
	{ NULL,		"mmapmany N",		"start N workers stressing many mmaps and munmaps" },
	{ NULL,		"mmapmany-ops N",	"stop after N mmapmany bogo operations" },
#if defined(STRESS_MREMAP)
	{ NULL,		"mremap N",		"start N workers stressing mremap" },
	{ NULL,		"mremap-ops N",		"stop after N mremap bogo operations" },
	{ NULL,		"mremap-bytes N",	"mremap N bytes maximum for each stress iteration" },
#endif
#if defined(STRESS_MSG)
	{ NULL,		"msg N",		"start N workers stressing System V messages" },
	{ NULL,		"msg-ops N",		"stop msg workers after N bogo messages" },
#endif
#if defined(STRESS_MSYNC)
	{ NULL,		"msync N",		"start N workers syncing mmap'd data with msync" },
	{ NULL,		"msync-ops N",		"stop msync workers after N bogo msyncs" },
	{ NULL,		"msync-bytes N",	"size of file and memory mapped region to msync" },
#endif
#if defined(STRESS_MQ)
	{ NULL,		"mq N",			"start N workers passing messages using POSIX messages" },
	{ NULL,		"mq-ops N",		"stop mq workers after N bogo messages" },
	{ NULL,		"mq-size N",		"specify the size of the POSIX message queue" },
#endif
	{ NULL,		"nice N",		"start N workers that randomly re-adjust nice levels" },
	{ NULL,		"nice-ops N",		"stop after N nice bogo operations" },
	{ NULL,		"null N",		"start N workers writing to /dev/null" },
	{ NULL,		"null-ops N",		"stop after N /dev/null bogo write operations" },
#if defined(STRESS_NUMA)
	{ NULL,		"numa N",		"start N workers stressing NUMA interfaces" },
	{ NULL,		"numa-ops N",		"stop after N NUMA bogo operations" },
#endif
#if defined(STRESS_OOM_PIPE)
	{ NULL,		"oom-pipe N",		"start N workers exercising large pipes" },
	{ NULL,		"oom-pipe-ops N",	"stop after N oom-pipe bogo operations" },
#endif
#if defined(STRESS_OPCODE)
	{ NULL,		"opcode N",		"start N workers exercising random opcodes" },
	{ NULL,		"opcode-ops N",		"stop after N opcode bogo operations" },
#endif
	{ "o",		"open N",		"start N workers exercising open/close" },
	{ NULL,		"open-ops N",		"stop after N open/close bogo operations" },
#if defined(STRESS_PERSONALITY)
	{ NULL,		"personality N",	"start N workers that change their personality" },
	{ NULL,		"personality-ops N",	"stop after N bogo personality calls" },
#endif
	{ "p N",	"pipe N",		"start N workers exercising pipe I/O" },
	{ NULL,		"pipe-ops N",		"stop after N pipe I/O bogo operations" },
	{ NULL,		"pipe-data-size N",	"set pipe size of each pipe write to N bytes" },
#if defined(F_SETPIPE_SZ)
	{ NULL,		"pipe-size N",		"set pipe size to N bytes" },
#endif
	{ "P N",	"poll N",		"start N workers exercising zero timeout polling" },
	{ NULL,		"poll-ops N",		"stop after N poll bogo operations" },
#if defined(STRESS_PROCFS)
	{ NULL,		"procfs N",		"start N workers reading portions of /proc" },
	{ NULL,		"procfs-ops N",		"stop procfs workers after N bogo read operations" },
#endif
#if defined(STRESS_PTHREAD)
	{ NULL,		"pthread N",		"start N workers that create multiple threads" },
	{ NULL,		"pthread-ops N",	"stop pthread workers after N bogo threads created" },
	{ NULL,		"pthread-max P",	"create P threads at a time by each worker" },
#endif
#if defined(STRESS_PTRACE)
	{ NULL,		"ptrace N",		"start N workers that trace a child using ptrace" },
	{ NULL,		"ptrace-ops N",		"stop ptrace workers after N system calls are traced" },
#endif
#if defined(STRESS_PTY)
	{ NULL,		"pty N",		"start N workers that exercise pseudoterminals" },
	{ NULL,		"pty-ops N",		"stop pty workers after N pty bogo operations" },
#endif
	{ "Q",		"qsort N",		"start N workers qsorting 32 bit random integers" },
	{ NULL,		"qsort-ops N",		"stop after N qsort bogo operations" },
	{ NULL,		"qsort-size N",		"number of 32 bit integers to sort" },
#if defined(STRESS_QUOTA)
	{ NULL,		"quota N",		"start N workers exercising quotactl commands" },
	{ NULL,		"quota -ops N",		"stop after N quotactl bogo operations" },
#endif
#if defined(STRESS_RDRAND)
	{ NULL,		"rdrand N",		"start N workers exercising rdrand (x86 only)" },
	{ NULL,		"rdrand-ops N",		"stop after N rdrand bogo operations" },
#endif
#if defined(STRESS_READAHEAD)
	{ NULL,		"readahead N",		"start N workers exercising file readahead" },
	{ NULL,		"readahead-bytes N",	"size of file to readahead on (default is 1GB)" },
	{ NULL,		"readahead-ops N",	"stop after N readahead bogo operations" },
#endif
#if defined(STRESS_REMAP_FILE_PAGES)
	{ NULL,		"remap N",		"start N workers exercising page remappings" },
	{ NULL,		"remap-ops N",		"stop after N remapping bogo operations" },
#endif
	{ "R",		"rename N",		"start N workers exercising file renames" },
	{ NULL,		"rename-ops N",		"stop after N rename bogo operations" },
#if defined(STRESS_RLIMIT)
	{ NULL,		"rlimit N",		"start N workers that exceed rlimits" },
	{ NULL,		"rlimit-ops N",		"stop after N rlimit bogo operations" },
#endif
#if defined(STRESS_RTC)
	{ NULL,		"rtc N",		"start N workers that exercise the RTC interfaces" },
	{ NULL,		"rtc-ops N",		"stop after N RTC bogo operations" },
#endif
#if defined(STRESS_SEAL)
	{ NULL,		"seal N",		"start N workers performing fcntl SEAL commands" },
	{ NULL,		"seal-ops N",		"stop after N SEAL bogo operations" },
#endif
#if defined(STRESS_SECCOMP)
	{ NULL,		"seccomp N",		"start N workers performing seccomp call filtering" },
	{ NULL,		"seccomp-ops N",	"stop after N seccomp bogo operations" },
#endif
	{ NULL,		"seek N",		"start N workers performing random seek r/w IO" },
	{ NULL,		"seek-ops N",		"stop after N seek bogo operations" },
#if defined(OPT_SEEK_PUNCH)
	{ NULL,		"seek-punch",		"punch random holes in file to stress extents" },
#endif
	{ NULL,		"seek-size N",		"length of file to do random I/O upon" },
#if defined(STRESS_SEMAPHORE_POSIX)
	{ NULL,		"sem N",		"start N workers doing semaphore operations" },
	{ NULL,		"sem-ops N",		"stop after N semaphore bogo operations" },
	{ NULL,		"sem-procs N",		"number of processes to start per worker" },
#endif
#if defined(STRESS_SEMAPHORE_SYSV)
	{ NULL,		"sem-sysv N",		"start N workers doing System V semaphore operations" },
	{ NULL,		"sem-sysv-ops N",	"stop after N System V sem bogo operations" },
	{ NULL,		"sem-sysv-procs N",	"number of processes to start per worker" },
#endif
#if defined(STRESS_SENDFILE)
	{ NULL,		"sendfile N",		"start N workers exercising sendfile" },
	{ NULL,		"sendfile-ops N",	"stop after N bogo sendfile operations" },
	{ NULL,		"sendfile-size N",	"size of data to be sent with sendfile" },
#endif
#if defined(STRESS_SHM_POSIX)
	{ NULL,		"shm N",		"start N workers that exercise POSIX shared memory" },
	{ NULL,		"shm-ops N",		"stop after N POSIX shared memory bogo operations" },
	{ NULL,		"shm-bytes N",		"allocate and free N bytes of POSIX shared memory per loop" },
	{ NULL,		"shm-segs N",		"allocate N POSIX shared memory segments per iteration" },
#endif
	{ NULL,		"shm-sysv N",		"start N workers that exercise System V shared memory" },
	{ NULL,		"shm-sysv-ops N",	"stop after N shared memory bogo operations" },
	{ NULL,		"shm-sysv-bytes N",	"allocate and free N bytes of shared memory per loop" },
	{ NULL,		"shm-sysv-segs N",	"allocate N shared memory segments per iteration" },
#if defined(STRESS_SIGFD)
	{ NULL,		"sigfd N",		"start N workers reading signals via signalfd reads " },
	{ NULL,		"sigfd-ops N",		"stop after N bogo signalfd reads" },
#endif
	{ NULL,		"sigfpe N",		"start N workers generating floating point math faults" },
	{ NULL,		"sigfpe-ops N",		"stop after N bogo floating point math faults" },
	{ NULL,		"sigpending N",		"start N workers exercising sigpending" },
	{ NULL,		"sigpending-ops N",	"stop after N sigpending bogo operations" },
#if defined(STRESS_SIGQUEUE)
	{ NULL,		"sigq N",		"start N workers sending sigqueue signals" },
	{ NULL,		"sigq-ops N",		"stop after N siqqueue bogo operations" },
#endif
	{ NULL,		"sigsegv N",		"start N workers generating segmentation faults" },
	{ NULL,		"sigsegv-ops N",	"stop after N bogo segmentation faults" },
	{ NULL,		"sigsuspend N",		"start N workers exercising sigsuspend" },
	{ NULL,		"sigsuspend-ops N",	"stop after N bogo sigsuspend wakes" },
#if defined(STRESS_SLEEP)
	{ NULL,		"sleep N",		"start N workers performing various duration sleeps" },
	{ NULL,		"sleep-ops N",		"stop after N bogo sleep operations" },
	{ NULL,		"sleep-max P",		"create P threads at a time by each worker" },
#endif
	{ "S N",	"sock N",		"start N workers exercising socket I/O" },
	{ NULL,		"sock-domain D",	"specify socket domain, default is ipv4" },
	{ NULL,		"sock-nodelay",		"disable Nagle algorithm, send data immediately" },
	{ NULL,		"sock-ops N",		"stop after N socket bogo operations" },
	{ NULL,		"sock-opts option",	"socket options [send|sendmsg|sendmmsg]" },
	{ NULL,		"sock-port P",		"use socket ports P to P + number of workers - 1" },
	{ NULL,		"sock-type T",		"socket type (stream, seqpacket)" },
#if defined(STRESS_SOCKET_FD)
	{ NULL,		"sockfd N",		"start N workers sending file descriptors over sockets" },
	{ NULL,		"sockfd-ops N",		"stop after N sockfd bogo operations" },
#endif
	{ NULL,		"sockpair N",		"start N workers exercising socket pair I/O activity" },
	{ NULL,		"sockpair-ops N",	"stop after N socket pair bogo operations" },
#if defined(STRESS_SPAWN)
	{ NULL,		"spawn",		"start N workers spawning stress-ng using posix_spawn" },
	{ NULL,		"spawn-ops N",		"stop after N spawn bogo operations" },
#endif
#if defined(STRESS_SPLICE)
	{ NULL,		"splice N",		"start N workers reading/writing using splice" },
	{ NULL,		"splice-ops N",		"stop after N bogo splice operations" },
	{ NULL,		"splice-bytes N",	"number of bytes to transfer per splice call" },
#endif
	{ NULL,		"stack N",		"start N workers generating stack overflows" },
	{ NULL,		"stack-ops N",		"stop after N bogo stack overflows" },
	{ NULL,		"stack-fill",		"fill stack, touches all new pages " },
#if defined(STRESS_STACKMMAP)
	{ NULL,		"stackmmap N",		"start N workers exercising a filebacked stack" },
	{ NULL,		"stackmmap-ops N",	"stop after N bogo stackmmap operations" },
#endif
	{ NULL,		"str N",		"start N workers exercising lib C string functions" },
	{ NULL,		"str-method func",	"specify the string function to stress" },
	{ NULL,		"str-ops N",		"stop after N bogo string operations" },
	{ NULL,		"stream N",		"start N workers exercising memory bandwidth" },
	{ NULL,		"stream-ops N",		"stop after N bogo stream operations" },
	{ NULL,		"stream-l3-size N",	"specify the L3 cache size of the CPU" },
	{ "s N",	"switch N",		"start N workers doing rapid context switches" },
	{ NULL,		"switch-ops N",		"stop after N context switch bogo operations" },
	{ NULL,		"symlink N",		"start N workers creating symbolic links" },
	{ NULL,		"symlink-ops N",	"stop after N symbolic link bogo operations" },
#if defined(STRESS_SYNC_FILE)
	{ NULL,		"sync-file N",		"start N workers exercise sync_file_range" },
	{ NULL,		"sync-file-ops N",	"stop after N sync_file_range bogo operations" },
	{ NULL,		"sync-file-bytes N",	"size of file to be sync'd" },
#endif
	{ NULL,		"sysinfo N",		"start N workers reading system information" },
	{ NULL,		"sysinfo-ops N",	"stop after sysinfo bogo operations" },
#if defined(STRESS_SYSFS)
	{ NULL,		"sysfs N",		"start N workers reading files from /sys" },
	{ NULL,		"sysfs-ops N",		"stop after sysfs bogo operations" },
#endif
#if defined(STRESS_TEE)
	{ NULL,		"tee N",		"start N workers exercising the tee system call" },
	{ NULL,		"tee-ops N",		"stop after N tee bogo operations" },
#endif
#if defined(STRESS_TIMER)
	{ "T N",	"timer N",		"start N workers producing timer events" },
	{ NULL,		"timer-ops N",		"stop after N timer bogo events" },
	{ NULL,		"timer-freq F",		"run timer(s) at F Hz, range 1 to 1000000000" },
	{ NULL,		"timer-rand",		"enable random timer frequency" },
#endif
#if defined(STRESS_TIMERFD)
	{ NULL,		"timerfd N",		"start N workers producing timerfd events" },
	{ NULL,		"timerfd-ops N",	"stop after N timerfd bogo events" },
	{ NULL,		"timerfd-freq F",	"run timer(s) at F Hz, range 1 to 1000000000" },
	{ NULL,		"timerfd-rand",		"enable random timerfd frequency" },
#endif
#if defined(STRESS_TLB_SHOOTDOWN)
	{ NULL,		"tlb-shootdown N",	"start N wrokers that force TLB shootdowns" },
	{ NULL,		"tlb-shootdown-opts N",	"stop after N TLB shootdown bogo ops" },
#endif
#if defined(STRESS_TSC)
	{ NULL,		"tsc N",		"start N workers reading the TSC (x86 only)" },
	{ NULL,		"tsc-ops N",		"stop after N TSC bogo operations" },
#endif
	{ NULL,		"tsearch N",		"start N workers that exercise a tree search" },
	{ NULL,		"tsearch-ops N",	"stop after N tree search bogo operations" },
	{ NULL,		"tsearch-size N",	"number of 32 bit integers to tsearch" },
	{ NULL,		"udp N",		"start N workers performing UDP send/receives " },
	{ NULL,		"udp-ops N",		"stop after N udp bogo operations" },
	{ NULL,		"udp-domain D",		"specify domain, default is ipv4" },
#if defined(OPT_UDP_LITE)
	{ NULL,		"udp-lite",		"use the UDP-Lite (RFC 3828) protocol" },
#endif
	{ NULL,		"udp-port P",		"use ports P to P + number of workers - 1" },
#if defined(STRESS_UDP_FLOOD)
	{ NULL,		"udp-flood N",		"start N workers that performs a UDP flood attack" },
	{ NULL,		"udp-flood-ops N",	"stop after N udp flood bogo operations" },
	{ NULL,		"udp-flood-domain D",	"specify domain, default is ipv4" },
#endif
#if defined(STRESS_UNSHARE)
	{ NULL,		"unshare N",		"start N workers exercising resource unsharing" },
	{ NULL,		"unshare-ops N",	"stop after N bogo unshare operations" },
#endif
#if defined(STRESS_URANDOM)
	{ "u N",	"urandom N",		"start N workers reading /dev/urandom" },
	{ NULL,		"urandom-ops N",	"stop after N urandom bogo read operations" },
#endif
#if defined(STRESS_USERFAULTFD)
	{ NULL,		"userfaultfd N",	"start N page faulting workers with userspace handling" },
	{ NULL,		"userfaultfd-ops N",	"stop after N page faults have been handled" },
#endif
	{ NULL,		"utime N",		"start N workers updating file timestamps" },
	{ NULL,		"utime-ops N",		"stop after N utime bogo operations" },
	{ NULL,		"utime-fsync",		"force utime meta data sync to the file system" },
#if defined(STRESS_VECMATH)
	{ NULL,		"vecmath N",		"start N workers performing vector math ops" },
	{ NULL,		"vecmath-ops N",	"stop after N vector math bogo operations" },
#endif
#if defined(STRESS_VFORK)
	{ NULL,		"vfork N",		"start N workers spinning on vfork() and exit()" },
	{ NULL,		"vfork-ops N",		"stop after N vfork bogo operations" },
	{ NULL,		"vfork-max P",		"create P processes per iteration, default is 1" },
#endif
	{ "m N",	"vm N",			"start N workers spinning on anonymous mmap" },
	{ NULL,		"vm-bytes N",		"allocate N bytes per vm worker (default 256MB)" },
	{ NULL,		"vm-hang N",		"sleep N seconds before freeing memory" },
	{ NULL,		"vm-keep",		"redirty memory instead of reallocating" },
	{ NULL,		"vm-ops N",		"stop after N vm bogo operations" },
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
	{ NULL,		"vm-splice-ops N",	"stop after N bogo splice operations" },
	{ NULL,		"vm-splice-bytes N",	"number of bytes to transfer per vmsplice call" },
#endif
	{ NULL,		"wcs N",		"start N workers on lib C wide character string functions" },
	{ NULL,		"wcs-method func",	"specify the wide character string function to stress" },
	{ NULL,		"wcs-ops N",		"stop after N bogo wide character string operations" },
#if defined(STRESS_WAIT)
	{ NULL,		"wait N",		"start N workers waiting on child being stop/resumed" },
	{ NULL,		"wait-ops N",		"stop after N bogo wait operations" },
#endif
#if defined(STRESS_YIELD)
	{ "y N",	"yield N",		"start N workers doing sched_yield() calls" },
	{ NULL,		"yield-ops N",		"stop after N bogo yield operations" },
#endif
#if defined(STRESS_XATTR)
	{ NULL,		"xattr N",		"start N workers stressing file extended attributes" },
	{ NULL,		"xattr-ops N",		"stop after N bogo xattr operations" },
#endif
	{ NULL,		"zero N",		"start N workers reading /dev/zero" },
	{ NULL,		"zero-ops N",		"stop after N /dev/zero bogo read operations" },
#if defined(STRESS_ZLIB)
	{ NULL,		"zlib N",		"start N workers compressing data with zlib" },
	{ NULL,		"zlib-ops N",		"stop after N zlib bogo compression operations" },
#endif
	{ NULL,		"zombie N",		"start N workers that rapidly create and reap zombies" },
	{ NULL,		"zombie-ops N",		"stop after N bogo zombie fork operations" },
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
	char *tmp, *munged_name;
	size_t len;

	tmp = munge_underscore((char *)name);
	len = strlen(tmp) + 1;

	munged_name = alloca(len);
	strncpy(munged_name, tmp, len);

	for (i = 0; stressors[i].name; i++) {
		const char *munged_stressor_name = munge_underscore((char *)stressors[i].name);

		if (!strcmp(munged_stressor_name, munged_name))
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

/*
 *  stress_exclude()
 *  	parse -x --exlude exclude list
 */
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

	kill(-getpid(), SIGALRM);
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
	if (stress_sighandler(stress, SIGINT, stress_sigint_handler, NULL) < 0)
		return -1;
	if (stress_sighandler(stress, SIGHUP, stress_sigint_handler, NULL) < 0)
		return -1;

	if (stress_sighandler(stress, SIGALRM,
	    child ?  stress_sigalrm_child_handler :
		     stress_sigalrm_parent_handler, NULL) < 0)
		return -1;
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
 *  stress_get_processors()
 *	get number of processors, set count if <=0 as:
 *		count = 0 -> number of CPUs in system
 *		count < 9 -> number of CPUs online
 */
void stress_get_processors(int32_t *count)
{
	if (*count == 0)
		*count = stress_get_processors_configured();
	else if (*count < 0)
		*count = stress_get_processors_online();
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

	killpg(pgrp, sig);

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
static void MLOCKED wait_procs(bool *success, bool *resource_success)
{
	int i;

	if (opt_flags & OPT_FLAGS_IGNITE_CPU)
		ignite_cpu_start();

#if defined(__linux__) && NEED_GLIBC(2,3,0)
	/*
	 *  On systems that support changing CPU affinity
	 *  we keep on moving processed between processors
	 *  to impact on memory locality (e.g. NUMA) to
	 *  try to thrash the system when in aggressive mode
	 */
	if (opt_flags & OPT_FLAGS_AGGRESSIVE) {
		cpu_set_t proc_mask;
		unsigned long int cpu = 0;
		const uint32_t ticks_per_sec = stress_get_ticks_per_second() * 5;
		const useconds_t usec_sleep = ticks_per_sec ? 1000000 / ticks_per_sec : 1000000 / 250;

		while (opt_do_wait) {
			const int32_t cpus = stress_get_processors_configured();

			/* If we can't get the mask, don't do affinity twiddling */
			if (sched_getaffinity(0, sizeof(proc_mask), &proc_mask) < 0)
				goto do_wait;
			if (!CPU_COUNT(&proc_mask))	/* Highly unlikely */
				goto do_wait;

			for (i = 0; i < STRESS_MAX; i++) {
				int j;

				for (j = 0; j < procs[i].started_procs; j++) {
					const pid_t pid = procs[i].pids[j];
					if (pid) {
						cpu_set_t mask;
						int32_t cpu_num;

						do {
							cpu_num = mwc32() % cpus;
						} while (!(CPU_ISSET(cpu_num, &proc_mask)));

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
					if (WIFSIGNALED(status)) {
#if defined(WTERMSIG)
#if NEED_GLIBC(2,1,0)
						const char *signame = strsignal(WTERMSIG(status));

						pr_dbg(stderr, "process %d (stress-ng-%s) terminated on signal: %d (%s)\n",
							ret, stressors[i].name, WTERMSIG(status), signame);
#else
						pr_dbg(stderr, "process %d (stress-ng-%s) terminated on signal: %d\n",
							ret, stressors[i].name, WTERMSIG(status));
#endif
#else
						pr_dbg(stderr, "process %d (stress-ng-%s) terminated on signal\n",
							ret, stressors[i].name);
#endif
						*success = false;
					}
					switch (WEXITSTATUS(status)) {
					case EXIT_SUCCESS:
						break;
					case EXIT_NO_RESOURCE:
						pr_err(stderr, "process %d (stress-ng-%s) aborted early, out of system resources\n",
							ret, stressors[i].name);
						*resource_success = false;
						break;
					default:
						pr_err(stderr, "process %d (stress-ng-%s) terminated with an error, exit status=%d\n",
							ret, stressors[i].name, WEXITSTATUS(status));
						*success = false;
						break;
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
	if (opt_flags & OPT_FLAGS_IGNITE_CPU)
		ignite_cpu_stop();
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
	bool *success,
	bool *resource_success
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
					setpgid(0, pgrp);
					free_procs();
					if (stress_sethandler(name, true) < 0)
						exit(EXIT_FAILURE);
					stress_parent_died_alarm();
					stress_process_dumpable(false);
					if (opt_flags & OPT_FLAGS_TIMER_SLACK)
						stress_set_timer_slack();

					(void)alarm(opt_timeout);
					mwc_reseed();
					snprintf(name, sizeof(name), "%s-%s", app_name,
						munge_underscore((char *)stressors[i].name));
					set_oom_adjustment(name, false);
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
						setpgid(pid, pgrp);
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
	pr_dbg(stderr, "%d stressor%s spawned\n", n_procs,
		n_procs == 1 ? "" : "s");

wait_for_procs:
	wait_procs(success, resource_success);
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

	pr_inf(stdout, "%-13s %9.9s %9.9s %9.9s %9.9s %12s %12s\n",
		"stressor", "bogo ops", "real time", "usr time", "sys time", "bogo ops/s", "bogo ops/s");
	pr_inf(stdout, "%-13s %9.9s %9.9s %9.9s %9.9s %12s %12s\n",
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

		pr_inf(stdout, "%-13s %9" PRIu64 " %9.2f %9.2f %9.2f %12.2f %12.2f\n",
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
	double min1, min5, min15;
	int rc;

	if (times(&buf) == (clock_t)-1) {
		pr_err(stderr, "cannot get run time information: errno=%d (%s)\n",
			errno, strerror(errno));
		return;
	}
	rc = stress_get_load_avg(&min1, &min5, &min15);

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

	if (!rc) {
		pr_inf(stdout, "load average: %.2f %.2f %.2f\n",
			min1, min5, min15);
	}

	pr_yaml(yaml, "times:\n");
	pr_yaml(yaml, "      run-time: %f\n", duration);
	pr_yaml(yaml, "      available-cpu-time: %f\n", total_cpu_time);
	pr_yaml(yaml, "      user-time: %f\n", u_time);
	pr_yaml(yaml, "      system-time: %f\n", s_time);
	pr_yaml(yaml, "      total-time: %f\n", t_time);
	pr_yaml(yaml, "      user-time-percent: %f\n", u_pc);
	pr_yaml(yaml, "      system-time-percent: %f\n", s_pc);
	pr_yaml(yaml, "      total-time-percent: %f\n", t_pc);
	if (!rc) {
		pr_yaml(yaml, "      load-average-1-minute: %f\n", min1);
		pr_yaml(yaml, "      load-average-5-minute: %f\n", min5);
		pr_yaml(yaml, "      load-average-15-minute: %f\n", min15);
	}
}

/*
 *  log_args()
 *	dump to syslog argv[]
 */
void log_args(int argc, char **argv)
{
	int i;
	size_t len, arglen[argc];
	char *buf;

	for (len = 0, i = 0; i < argc; i++) {
		arglen[i] = strlen(argv[i]);
		len += arglen[i] + 1;
	}

	buf = calloc(len, sizeof(char));
	if (!buf)
		return;

	for (len = 0, i = 0; i < argc; i++) {
		if (i) {
			strncat(buf + len, " ", 1);
			len++;
		}
		strncat(buf + len, argv[i], arglen[i]);
		len += arglen[i];
	}
	syslog(LOG_INFO, "invoked with '%s' by user %d", buf, getuid());
	free(buf);
}

/*
 *  log_system_mem_info()
 *	dump system memory info
 */
void log_system_mem_info(void)
{
#if defined(__linux__)
	struct sysinfo info;

	if (sysinfo(&info) == 0) {
		syslog(LOG_INFO, "memory (MB): total %.2f, "
			"free %.2f, "
			"shared %.2f, "
			"buffer %.2f, "
			"swap %.2f, "
			"free swap %.2f\n",
			(double)(info.totalram * info.mem_unit) / MB,
			(double)(info.freeram * info.mem_unit) / MB,
			(double)(info.sharedram * info.mem_unit) / MB,
			(double)(info.bufferram * info.mem_unit) / MB,
			(double)(info.totalswap * info.mem_unit) / MB,
			(double)(info.freeswap * info.mem_unit) / MB);
	}
#endif
}

/*
 *  log_system_info()
 *	dump system info
 */
void log_system_info(void)
{
#if defined(__linux__)
	struct utsname buf;
#endif
#if defined(__linux__)
	if (uname(&buf) == 0) {
		syslog(LOG_INFO, "system: '%s' %s %s %s %s\n",
			buf.nodename,
			buf.sysname,
			buf.release,
			buf.version,
			buf.machine);
	}
#endif
}

int main(int argc, char **argv)
{
	double duration = 0.0;			/* stressor run time in secs */
	size_t len;
	bool success = true, resource_success = true;
	char *opt_exclude = NULL;		/* List of stressors to exclude */
	char *yamlfile = NULL;			/* YAML filename */
	FILE *yaml = NULL;			/* YAML output file */
	char *logfile = NULL;			/* log filename */
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
	int mem_cache_level = DEFAULT_CACHE_LEVEL;
	int mem_cache_ways = 0;

	/* --exec stressor uses this to exec itself and then exit early */
	if ((argc == 2) && !strcmp(argv[1], "--exec-exit"))
		exit(EXIT_SUCCESS);

	memset(procs, 0, sizeof(procs));
	mwc_reseed();

	(void)stress_get_pagesize();
	(void)stress_set_cpu_method("all");
	(void)stress_set_str_method("all");
	(void)stress_set_wcs_method("all");
	(void)stress_set_matrix_method("all");
	(void)stress_set_vm_method("all");

	pgrp = getpid();

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
		if ((c = getopt_long(argc, argv, "?khMVvqnt:b:c:i:m:d:f:s:l:p:P:C:S:a:y:F:D:T:u:o:r:B:R:Y:x:",
			long_options, &option_index)) == -1)
			break;

		for (s_id = 0; stressors[s_id].id != STRESS_MAX; s_id++) {
			if (stressors[s_id].short_getopt == c) {
				const char *name = opt_name(c);

				opt_flags |= OPT_FLAGS_SET;
				procs[s_id].num_procs = get_int32(optarg);
				stress_get_processors(&procs[s_id].num_procs);
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
			opt_all = get_int32(optarg);
			stress_get_processors(&opt_all);
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
			opt_backoff = get_uint64(optarg);
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
		case OPT_CACHE_LEVEL:
			mem_cache_level = atoi(optarg);
			/* Overly high values will be caught in the
			 * caching code.
			 */
			if (mem_cache_level <= 0)
				mem_cache_level = DEFAULT_CACHE_LEVEL;
			break;
		case OPT_CACHE_NO_AFFINITY:
			opt_flags |= OPT_FLAGS_CACHE_NOAFF;
			break;
		case OPT_CACHE_WAYS:
			mem_cache_ways = atoi(optarg);
			if (mem_cache_ways <= 0)
				mem_cache_ways = 0;
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
#if defined(STRESS_COPY_FILE)
		case OPT_COPY_FILE_BYTES:
			stress_set_copy_file_bytes(optarg);
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
#if defined(STRESS_EXEC)
		case OPT_EXEC_MAX:
			stress_set_exec_max(optarg);
			break;
#endif
#if defined(STRESS_FALLOCATE)
		case OPT_FALLOCATE_BYTES:
			stress_set_fallocate_bytes(optarg);
			break;
#endif
		case OPT_FIFO_READERS:
			stress_set_fifo_readers(optarg);
			break;
		case OPT_FILENAME_OPTS:
			if (stress_filename_opts(optarg) < 0)
				exit(EXIT_FAILURE);
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
#if defined(STRESS_HEAPSORT)
		case OPT_HEAPSORT_INTEGERS:
			stress_set_heapsort_size(optarg);
			break;
#endif
		case OPT_HSEARCH_SIZE:
			stress_set_hsearch_size(optarg);
			break;
		case OPT_IGNITE_CPU:
			opt_flags |= OPT_FLAGS_IGNITE_CPU;
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
		case OPT_LOG_FILE:
			logfile = optarg;
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
#if defined(STRESS_MEMFD)
		case OPT_MEMFD_BYTES:
			stress_set_memfd_bytes(optarg);
			break;
#endif
		case OPT_METRICS:
			opt_flags |= OPT_FLAGS_METRICS;
			break;
		case OPT_METRICS_BRIEF:
			opt_flags |= (OPT_FLAGS_METRICS_BRIEF | OPT_FLAGS_METRICS);
			break;
#if defined(STRESS_MERGESORT)
		case OPT_MERGESORT_INTEGERS:
			stress_set_mergesort_size(optarg);
			break;
#endif
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
#if defined(STRESS_MSYNC)
		case OPT_MSYNC_BYTES:
			stress_set_msync_bytes(optarg);
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
		case OPT_PATHOLOGICAL:
			opt_flags |= OPT_FLAGS_PATHOLOGICAL;
			break;
#if defined(STRESS_PERF_STATS)
		case OPT_PERF_STATS:
			opt_flags |= OPT_FLAGS_PERF_STATS;
			break;
#endif
		case OPT_PIPE_DATA_SIZE:
			stress_set_pipe_data_size(optarg);
			break;
#if defined(F_SETPIPE_SZ)
		case OPT_PIPE_SIZE:
			stress_set_pipe_size(optarg);
			break;
#endif
#if defined(STRESS_PTHREAD)
		case OPT_PTHREAD_MAX:
			stress_set_pthread_max(optarg);
			break;
#endif
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
			opt_random = get_int32(optarg);
			stress_get_processors(&opt_random);
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
#if defined(OPT_SEEK_PUNCH)
		case OPT_SEEK_PUNCH:
			opt_flags |= OPT_FLAGS_SEEK_PUNCH;
			break;
#endif
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
			opt_sequential = get_int32(optarg);
			stress_get_processors(&opt_sequential);
			check_range("sequential", opt_sequential,
				MIN_SEQUENTIAL, MAX_SEQUENTIAL);
			break;
#if defined(STRESS_SHM_POSIX)
		case OPT_SHM_POSIX_BYTES:
			stress_set_shm_posix_bytes(optarg);
			break;
		case OPT_SHM_POSIX_OBJECTS:
			stress_set_shm_posix_objects(optarg);
			break;
#endif
		case OPT_SHM_SYSV_BYTES:
			stress_set_shm_sysv_bytes(optarg);
			break;
		case OPT_SHM_SYSV_SEGMENTS:
			stress_set_shm_sysv_segments(optarg);
			break;
#if defined(STRESS_SLEEP)
		case OPT_SLEEP_MAX:
			stress_set_sleep_max(optarg);
			break;
#endif
		case OPT_SOCKET_DOMAIN:
			if (stress_set_socket_domain(optarg) < 0)
				exit(EXIT_FAILURE);
			break;
		case OPT_SOCKET_NODELAY:
			opt_flags |= OPT_FLAGS_SOCKET_NODELAY;
			break;
		case OPT_SOCKET_OPTS:
			if (stress_set_socket_opts(optarg) < 0)
				exit(EXIT_FAILURE);
			break;
		case OPT_SOCKET_PORT:
			stress_set_socket_port(optarg);
			break;
		case OPT_SOCKET_TYPE:
			if (stress_set_socket_type(optarg) < 0)
				exit(EXIT_FAILURE);
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
		case OPT_STREAM_L3_SIZE:
			stress_set_stream_L3_size(optarg);
			break;
#if defined(STRESS_SYNC_FILE)
		case OPT_SYNC_FILE_BYTES:
			stress_set_sync_file_bytes(optarg);
			break;
#endif
		case OPT_SYSLOG:
			opt_flags |= OPT_FLAGS_SYSLOG;
			break;
		case OPT_TASKSET:
			if (set_cpu_affinity(optarg) < 0)
				exit(EXIT_FAILURE);
			break;
		case OPT_TEMP_PATH:
			if (stress_set_temp_path(optarg) < 0)
				exit(EXIT_FAILURE);
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
#if defined(PRCTL_TIMER_SLACK)
		case OPT_TIMER_SLACK:
			opt_flags |= OPT_FLAGS_TIMER_SLACK;
			stress_set_timer_slack_ns(optarg);
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
#if defined(OPT_UDP_LITE)
		case OPT_UDP_LITE:
#endif
			opt_flags |= OPT_FLAGS_UDP_LITE;
			break;
#if defined(STRESS_UDP_FLOOD)
		case OPT_UDP_FLOOD_DOMAIN:
			if (stress_set_udp_flood_domain(optarg) < 0)
				exit(EXIT_FAILURE);
			break;
#endif
#if defined(STRESS_USERFAULTFD)
		case OPT_USERFAULTFD_BYTES:
			stress_set_userfaultfd_bytes(optarg);
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
	if (logfile) 
		pr_openlog(logfile);
	openlog("stress-ng", 0, LOG_USER);
	log_args(argc, argv);
	log_system_info();
	log_system_mem_info();

	pr_dbg(stderr, "%" PRId32 " processors online, %" PRId32
		" processors configured\n",
		stress_get_processors_online(),
		stress_get_processors_configured());

	if ((opt_flags & OPT_FLAGS_MINMAX_MASK) == OPT_FLAGS_MINMAX_MASK) {
		fprintf(stderr, "maximize and minimize cannot be used together\n");
		exit(EXIT_FAILURE);
	}
#if defined(STRESS_RDRAND)
	id = stressor_id_find(STRESS_RDRAND);
	if ((procs[id].num_procs || (opt_flags & OPT_FLAGS_SEQUENTIAL)) &&
	    (stress_rdrand_supported() < 0)) {
		procs[id].num_procs = 0;
		procs[id].exclude = true;
	}
#endif
#if defined(STRESS_TSC)
	id = stressor_id_find(STRESS_TSC);
	if ((procs[id].num_procs || (opt_flags & OPT_FLAGS_SEQUENTIAL)) &&
	    (stress_tsc_supported() < 0)) {
		procs[id].num_procs = 0;
		procs[id].exclude = true;
	}
#endif
#if defined(STRESS_APPARMOR)
	id = stressor_id_find(STRESS_APPARMOR);
	if ((procs[id].num_procs || (opt_flags & OPT_FLAGS_SEQUENTIAL)) &&
	    (stress_apparmor_supported() < 0)) {
		procs[id].num_procs = 0;
		procs[id].exclude = true;
	}
#endif
	/*
	 *  Disable pathological stressors if user has not explicitly
	 *  request them to be used. Let's play safe.
	 */
	if (!(opt_flags & OPT_FLAGS_PATHOLOGICAL)) {
		for (i = 0; i < STRESS_MAX; i++) {
			if (stressors[i].class & CLASS_PATHOLOGICAL) {
				if (procs[i].num_procs > 0) {
					pr_inf(stderr, "disabled '%s' (enable it "
						"with --pathological option)\n",
						munge_underscore((char *)stressors[i].name));
				}
				procs[i].num_procs = 0;
				procs[i].exclude = true;
			}
		}
	}

	if (opt_flags & OPT_FLAGS_RANDOM) {
		int32_t n = opt_random;

		if (opt_flags & OPT_FLAGS_SET) {
			fprintf(stderr, "Cannot specify random option with "
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
	stress_process_dumpable(false);
	stress_cwd_readwriteable();
	set_oom_adjustment("main", false);
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
		if (stress_sighandler("stress-ng", signals[i], handle_sigint, NULL) < 0)
			exit(EXIT_FAILURE);
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
#if defined(STRESS_PTHREAD) && defined(RLIMIT_NPROC)
	{
		id = stressor_id_find(STRESS_PTHREAD);
		struct rlimit limit;
		if (procs[id].num_procs &&
		    (getrlimit(RLIMIT_NPROC, &limit) == 0)) {
			uint64_t max = (uint64_t)limit.rlim_cur / procs[id].num_procs;
			stress_adjust_pthread_max(max);
		}
	}
#endif
#if defined(STRESS_SLEEP) && defined(RLIMIT_NPROC)
	{
		id = stressor_id_find(STRESS_SLEEP);
		struct rlimit limit;
		if (procs[id].num_procs &&
		    (getrlimit(RLIMIT_NPROC, &limit) == 0)) {
			uint64_t max = (uint64_t)limit.rlim_cur / procs[id].num_procs;
			stress_adjust_sleep_max(max);
		}
	}
#endif
	if (show_hogs(opt_class) < 0) {
		free_procs();
		exit(EXIT_FAILURE);
	}
	len = sizeof(shared_t) + (sizeof(proc_stats_t) * STRESS_MAX * max_procs);
	shared = (shared_t *)mmap(NULL, len, PROT_READ | PROT_WRITE,
		MAP_SHARED | MAP_ANON, -1, 0);
	if (shared == MAP_FAILED) {
		pr_err(stderr, "Cannot mmap to shared memory region: errno=%d (%s)\n",
			errno, strerror(errno));
		free_procs();
		exit(EXIT_FAILURE);
	}
	memset(shared, 0, len);
#if defined(STRESS_PERF_STATS)
	pthread_spin_init(&shared->perf.lock, 0);
#endif

	/*
	 *  Allocate shared cache memory
	 */
	shared->mem_cache_level = mem_cache_level;
	shared->mem_cache_ways = mem_cache_ways;
	if (stress_cache_alloc("cache allocate") < 0) {
		(void)munmap((void *)shared, len);
		free_procs();
		exit(EXIT_FAILURE);
	}

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
						shared->stats, &duration, &success, &resource_success);
			}
		}
	} else {
		/*
		 *  Run all stressors in parallel
		 */
		stress_run(total_procs, max_procs,
			opt_backoff, opt_ionice_class, opt_ionice_level,
			shared->stats, &duration, &success, &resource_success);
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
	closelog();
	if (yaml) {
		pr_yaml(yaml, "...\n");
		fclose(yaml);
	}

	if (!success)
		exit(EXIT_NOT_SUCCESS);
	if (!resource_success)
		exit(EXIT_NO_RESOURCE);
	exit(EXIT_SUCCESS);
}
