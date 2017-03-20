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
#include "stress-ng.h"

#include <getopt.h>
#include <semaphore.h>
#include <syslog.h>

#if defined(__linux__)
#include <sys/utsname.h>
#include <sys/sysinfo.h>
#endif

typedef struct {
	const stress_id str_id;
	int (*func_supported)(void);
} unsupported_t;

typedef struct {
	const stress_id str_id;
	void (*func_limited)(uint64_t max);
} proc_limited_t;

typedef struct {
	const stress_id str_id;
	const uint64_t opt_flag;
	void (*func)(void);
} proc_helper_t;

static proc_info_t procs[STRESS_MAX]; 		/* Per stressor process information */

/* Various option settings and flags */
static int32_t opt_all = 0;			/* Number of concurrent workers */
static volatile bool wait_flag = true;		/* false to exit run waiter loop */

/* Globals */
int32_t g_opt_sequential = DEFAULT_SEQUENTIAL;	/* Number of sequential workers */
uint64_t g_opt_timeout = 0;			/* timeout in seconds */
uint64_t g_opt_flags = PR_ERROR | PR_INFO | OPT_FLAGS_MMAP_MADVISE;
volatile bool g_keep_stressing_flag = true;	/* false to exit stressor */
volatile bool g_caught_sigint = false;		/* true if stopped by SIGINT */
pid_t g_pgrp;					/* process group leader */
const char *g_app_name = "stress-ng";		/* Name of application */
shared_t *g_shared;				/* shared memory */

/*
 *  stressors to be run-time checked to see if they are supported
 *  on the platform.
 */
static const unsupported_t unsupported[] = {
	{ STRESS_APPARMOR,	stress_apparmor_supported },
	{ STRESS_CHROOT,	stress_chroot_supported },
	{ STRESS_FANOTIFY,	stress_fanotify_supported },
	{ STRESS_ICMP_FLOOD,	stress_icmp_flood_supported },
	{ STRESS_NETLINK_PROC,	stress_netlink_proc_supported },
	{ STRESS_RDRAND,	stress_rdrand_supported },
	{ STRESS_TSC,		stress_tsc_supported }
};

/*
 *  stressors to be limited to a maximum process threshold
 */
#if defined(RLIMIT_NPROC)
static const proc_limited_t proc_limited[] = {
	{ STRESS_PTHREAD,	stress_adjust_pthread_max },
	{ STRESS_SLEEP,		stress_adjust_sleep_max }
};
#endif

/*
 *  stressors that have explicit init requirements
 */
static const proc_helper_t proc_init[] = {
	{ STRESS_SEMAPHORE_POSIX,	OPT_FLAGS_SEQUENTIAL, stress_semaphore_posix_init },
	{ STRESS_SEMAPHORE_SYSV,	OPT_FLAGS_SEQUENTIAL, stress_semaphore_sysv_init }
};

/*
 *  stressors that have explicit destroy requirements
 */
static const proc_helper_t proc_destroy[] = {
	{ STRESS_SEMAPHORE_POSIX,	OPT_FLAGS_SEQUENTIAL, stress_semaphore_posix_destroy },
	{ STRESS_SEMAPHORE_SYSV,	OPT_FLAGS_SEQUENTIAL, stress_semaphore_sysv_destroy }
};

/*
 *  Attempt to catch a range of signals so
 *  we can clean up rather than leave
 *  cruft everywhere.
 */
static const int signals[] = {
	/* POSIX.1-1990 */
#if defined(SIGHUP)
	SIGHUP,
#endif
#if defined(SIGINT)
	SIGINT,
#endif
#if defined(SIGQUIT)
	SIGQUIT,
#endif
#if defined(SIGABRT)
	SIGABRT,
#endif
#if defined(SIGFPE)
	SIGFPE,
#endif
#if defined(SIGTERM)
	SIGTERM,
#endif
#if defined(SIGXCPU)
	SIGXCPU,
#endif
#if defined(SIGXFSZ)
	SIGXFSZ,
#endif
	/* Linux various */
#if defined(SIGIOT)
	SIGIOT,
#endif
#if defined(SIGSTKFLT)
	SIGSTKFLT,
#endif
#if defined(SIGPWR)
	SIGPWR,
#endif
#if defined(SIGINFO)
	SIGINFO,
#endif
#if defined(SIGVTALRM)
	SIGVTALRM,
#endif
	-1,
};

#define STRESSOR(lower_name, upper_name, class)	\
{						\
	stress_ ## lower_name,			\
	STRESS_ ## upper_name,			\
	OPT_ ## upper_name,			\
	OPT_ ## upper_name  ## _OPS,		\
	# lower_name,				\
	class					\
}

/* Human readable stress test names */
static const stress_t stressors[] = {
	STRESSOR(af_alg, AF_ALG, CLASS_CPU | CLASS_OS),
	STRESSOR(affinity, AFFINITY, CLASS_SCHEDULER),
	STRESSOR(aio, AIO, CLASS_IO | CLASS_INTERRUPT | CLASS_OS),
	STRESSOR(aiol, AIO_LINUX, CLASS_IO | CLASS_INTERRUPT | CLASS_OS),
	STRESSOR(apparmor, APPARMOR, CLASS_OS | CLASS_SECURITY),
	STRESSOR(atomic, ATOMIC, CLASS_CPU | CLASS_MEMORY),
	STRESSOR(bigheap, BIGHEAP, CLASS_OS | CLASS_VM),
	STRESSOR(bind_mount, BIND_MOUNT, CLASS_FILESYSTEM | CLASS_OS | CLASS_PATHOLOGICAL),
	STRESSOR(brk, BRK, CLASS_OS | CLASS_VM),
	STRESSOR(bsearch, BSEARCH, CLASS_CPU_CACHE | CLASS_CPU | CLASS_MEMORY),
	STRESSOR(cache, CACHE, CLASS_CPU_CACHE),
	STRESSOR(cap, CAP, CLASS_OS),
	STRESSOR(chdir, CHDIR, CLASS_FILESYSTEM | CLASS_OS),
	STRESSOR(chmod, CHMOD, CLASS_FILESYSTEM | CLASS_OS),
	STRESSOR(chown, CHOWN, CLASS_FILESYSTEM | CLASS_OS),
	STRESSOR(chroot, CHROOT, CLASS_OS),
	STRESSOR(clock, CLOCK, CLASS_INTERRUPT | CLASS_OS),
	STRESSOR(clone, CLONE, CLASS_SCHEDULER | CLASS_OS),
	STRESSOR(context, CONTEXT, CLASS_MEMORY | CLASS_CPU),
	STRESSOR(copy_file, COPY_FILE, CLASS_FILESYSTEM | CLASS_OS),
	STRESSOR(cpu, CPU, CLASS_CPU),
	STRESSOR(cpu_online, CPU_ONLINE, CLASS_CPU | CLASS_OS),
	STRESSOR(crypt, CRYPT, CLASS_CPU),
	STRESSOR(daemon, DAEMON, CLASS_SCHEDULER | CLASS_OS),
	STRESSOR(dccp, DCCP, CLASS_NETWORK | CLASS_OS),
	STRESSOR(dentry, DENTRY, CLASS_FILESYSTEM | CLASS_OS),
	STRESSOR(dir, DIR, CLASS_FILESYSTEM | CLASS_OS),
	STRESSOR(dirdeep, DIRDEEP, CLASS_FILESYSTEM | CLASS_OS),
	STRESSOR(dnotify, DNOTIFY, CLASS_FILESYSTEM | CLASS_SCHEDULER | CLASS_OS),
	STRESSOR(dup, DUP, CLASS_FILESYSTEM | CLASS_OS),
	STRESSOR(epoll, EPOLL, CLASS_NETWORK | CLASS_OS),
	STRESSOR(eventfd, EVENTFD, CLASS_FILESYSTEM | CLASS_SCHEDULER | CLASS_OS),
	STRESSOR(exec, EXEC, CLASS_SCHEDULER | CLASS_OS),
	STRESSOR(fallocate, FALLOCATE, CLASS_FILESYSTEM | CLASS_OS),
	STRESSOR(fanotify, FANOTIFY, CLASS_FILESYSTEM | CLASS_SCHEDULER | CLASS_OS),
	STRESSOR(fault, FAULT, CLASS_INTERRUPT | CLASS_SCHEDULER | CLASS_OS),
	STRESSOR(fcntl, FCNTL, CLASS_FILESYSTEM | CLASS_OS),
	STRESSOR(fiemap, FIEMAP, CLASS_FILESYSTEM | CLASS_OS),
	STRESSOR(fifo, FIFO, CLASS_PIPE_IO | CLASS_OS | CLASS_SCHEDULER),
	STRESSOR(filename, FILENAME, CLASS_FILESYSTEM | CLASS_OS),
	STRESSOR(flock, FLOCK, CLASS_FILESYSTEM | CLASS_OS),
	STRESSOR(fork, FORK, CLASS_SCHEDULER | CLASS_OS),
	STRESSOR(fp_error, FP_ERROR, CLASS_CPU),
	STRESSOR(fstat, FSTAT, CLASS_FILESYSTEM | CLASS_OS),
	STRESSOR(full, FULL, CLASS_DEV | CLASS_MEMORY | CLASS_OS),
	STRESSOR(futex, FUTEX, CLASS_SCHEDULER | CLASS_OS),
	STRESSOR(get, GET, CLASS_OS),
	STRESSOR(getdent, GETDENT, CLASS_FILESYSTEM | CLASS_OS),
	STRESSOR(getrandom, GETRANDOM, CLASS_OS | CLASS_CPU),
	STRESSOR(handle, HANDLE, CLASS_FILESYSTEM | CLASS_OS),
	STRESSOR(hdd, HDD, CLASS_IO | CLASS_OS),
	STRESSOR(heapsort, HEAPSORT, CLASS_CPU_CACHE | CLASS_CPU | CLASS_MEMORY),
	STRESSOR(hsearch, HSEARCH, CLASS_CPU_CACHE | CLASS_CPU | CLASS_MEMORY),
	STRESSOR(icache, ICACHE, CLASS_CPU_CACHE),
	STRESSOR(icmp_flood, ICMP_FLOOD, CLASS_OS | CLASS_NETWORK),
	STRESSOR(inotify, INOTIFY, CLASS_FILESYSTEM | CLASS_SCHEDULER | CLASS_OS),
	STRESSOR(io, IOSYNC, CLASS_FILESYSTEM | CLASS_OS),
	STRESSOR(iomix, IOMIX, CLASS_FILESYSTEM | CLASS_OS),
	STRESSOR(ioprio, IOPRIO, CLASS_FILESYSTEM | CLASS_OS),
	STRESSOR(itimer, ITIMER, CLASS_INTERRUPT | CLASS_OS),
	STRESSOR(kcmp, KCMP, CLASS_OS),
	STRESSOR(key, KEY, CLASS_OS),
	STRESSOR(kill, KILL, CLASS_INTERRUPT | CLASS_SCHEDULER | CLASS_OS),
	STRESSOR(klog, KLOG, CLASS_OS),
	STRESSOR(lease, LEASE, CLASS_FILESYSTEM | CLASS_OS),
	STRESSOR(link, LINK, CLASS_FILESYSTEM | CLASS_OS),
	STRESSOR(locka, LOCKA, CLASS_FILESYSTEM | CLASS_OS),
	STRESSOR(lockbus, LOCKBUS, CLASS_CPU_CACHE | CLASS_MEMORY),
	STRESSOR(lockf, LOCKF, CLASS_FILESYSTEM | CLASS_OS),
	STRESSOR(lockofd, LOCKOFD, CLASS_FILESYSTEM | CLASS_OS),
	STRESSOR(longjmp, LONGJMP, CLASS_CPU),
	STRESSOR(lsearch, LSEARCH, CLASS_CPU_CACHE | CLASS_CPU | CLASS_MEMORY),
	STRESSOR(madvise, MADVISE, CLASS_VM | CLASS_OS),
	STRESSOR(malloc, MALLOC, CLASS_CPU_CACHE | CLASS_MEMORY | CLASS_VM | CLASS_OS),
	STRESSOR(matrix, MATRIX, CLASS_CPU | CLASS_CPU_CACHE | CLASS_MEMORY | CLASS_CPU),
	STRESSOR(membarrier, MEMBARRIER, CLASS_CPU_CACHE | CLASS_MEMORY),
	STRESSOR(memcpy, MEMCPY, CLASS_CPU_CACHE | CLASS_MEMORY),
	STRESSOR(memfd, MEMFD, CLASS_OS | CLASS_MEMORY),
	STRESSOR(mergesort, MERGESORT, CLASS_CPU_CACHE | CLASS_CPU | CLASS_MEMORY),
	STRESSOR(mincore, MINCORE, CLASS_OS | CLASS_MEMORY),
	STRESSOR(mknod, MKNOD, CLASS_FILESYSTEM | CLASS_OS),
	STRESSOR(mlock, MLOCK, CLASS_VM | CLASS_OS),
	STRESSOR(mmap, MMAP, CLASS_VM | CLASS_OS),
	STRESSOR(mmapfork, MMAPFORK, CLASS_SCHEDULER | CLASS_VM | CLASS_OS),
	STRESSOR(mmapmany, MMAPMANY, CLASS_VM | CLASS_OS),
	STRESSOR(mq, MQ, CLASS_SCHEDULER | CLASS_OS),
	STRESSOR(mremap, MREMAP, CLASS_VM | CLASS_OS),
	STRESSOR(msg, MSG, CLASS_SCHEDULER | CLASS_OS),
	STRESSOR(msync, MSYNC, CLASS_VM | CLASS_OS),
	STRESSOR(netlink_proc, NETLINK_PROC, CLASS_SCHEDULER | CLASS_OS),
	STRESSOR(nice, NICE, CLASS_SCHEDULER | CLASS_OS),
	STRESSOR(nop, NOP, CLASS_CPU),
	STRESSOR(null, NULL, CLASS_DEV | CLASS_MEMORY | CLASS_OS),
	STRESSOR(numa, NUMA, CLASS_CPU | CLASS_MEMORY | CLASS_OS),
	STRESSOR(oom_pipe, OOM_PIPE, CLASS_MEMORY | CLASS_OS),
	STRESSOR(opcode, OPCODE, CLASS_CPU | CLASS_OS),
	STRESSOR(open, OPEN, CLASS_FILESYSTEM | CLASS_OS),
	STRESSOR(personality, PERSONALITY, CLASS_OS),
	STRESSOR(pipe, PIPE, CLASS_PIPE_IO | CLASS_MEMORY | CLASS_OS),
	STRESSOR(poll, POLL, CLASS_SCHEDULER | CLASS_OS),
	STRESSOR(procfs, PROCFS, CLASS_FILESYSTEM | CLASS_OS),
	STRESSOR(pthread, PTHREAD, CLASS_SCHEDULER | CLASS_OS),
	STRESSOR(ptrace, PTRACE, CLASS_OS),
	STRESSOR(pty, PTY, CLASS_OS),
	STRESSOR(qsort, QSORT, CLASS_CPU_CACHE | CLASS_CPU | CLASS_MEMORY),
	STRESSOR(quota, QUOTA, CLASS_OS),
	STRESSOR(rdrand, RDRAND, CLASS_CPU),
	STRESSOR(readahead, READAHEAD, CLASS_IO | CLASS_OS),
	STRESSOR(remap, REMAP_FILE_PAGES, CLASS_MEMORY | CLASS_OS),
	STRESSOR(rename, RENAME, CLASS_FILESYSTEM | CLASS_OS),
	STRESSOR(resources, RESOURCES, CLASS_MEMORY | CLASS_OS),
	STRESSOR(rlimit, RLIMIT, CLASS_OS),
	STRESSOR(rmap, RMAP, CLASS_OS | CLASS_MEMORY),
	STRESSOR(rtc, RTC, CLASS_OS),
	STRESSOR(schedpolicy, SCHEDPOLICY, CLASS_INTERRUPT | CLASS_SCHEDULER | CLASS_OS),
	STRESSOR(sctp, SCTP, CLASS_NETWORK),
	STRESSOR(seal, SEAL, CLASS_OS),
	STRESSOR(seccomp, SECCOMP, CLASS_OS),
	STRESSOR(seek, SEEK, CLASS_IO | CLASS_OS),
	STRESSOR(sem, SEMAPHORE_POSIX, CLASS_OS | CLASS_SCHEDULER),
	STRESSOR(sem_sysv, SEMAPHORE_SYSV, CLASS_OS | CLASS_SCHEDULER),
	STRESSOR(sendfile, SENDFILE, CLASS_PIPE_IO | CLASS_OS),
	STRESSOR(shm, SHM_POSIX, CLASS_VM | CLASS_OS),
	STRESSOR(shm_sysv, SHM_SYSV, CLASS_VM | CLASS_OS),
	STRESSOR(sigfd, SIGFD, CLASS_INTERRUPT | CLASS_OS),
	STRESSOR(sigfpe, SIGFPE, CLASS_INTERRUPT | CLASS_OS),
	STRESSOR(sigpending, SIGPENDING, CLASS_INTERRUPT | CLASS_OS),
	STRESSOR(sigq, SIGQUEUE, CLASS_INTERRUPT | CLASS_OS),
	STRESSOR(sigsegv, SIGSEGV, CLASS_INTERRUPT | CLASS_OS),
	STRESSOR(sigsuspend, SIGSUSPEND, CLASS_INTERRUPT | CLASS_OS),
	STRESSOR(sleep, SLEEP, CLASS_INTERRUPT | CLASS_SCHEDULER | CLASS_OS),
	STRESSOR(sock, SOCKET, CLASS_NETWORK | CLASS_OS),
	STRESSOR(sockfd, SOCKET_FD, CLASS_NETWORK | CLASS_OS),
	STRESSOR(sockpair, SOCKET_PAIR, CLASS_NETWORK | CLASS_OS),
	STRESSOR(spawn, SPAWN, CLASS_SCHEDULER | CLASS_OS),
	STRESSOR(splice, SPLICE, CLASS_PIPE_IO | CLASS_OS),
	STRESSOR(stack, STACK, CLASS_VM | CLASS_MEMORY),
	STRESSOR(stackmmap, STACKMMAP, CLASS_VM | CLASS_MEMORY),
	STRESSOR(str, STR, CLASS_CPU | CLASS_CPU_CACHE | CLASS_MEMORY),
	STRESSOR(stream, STREAM, CLASS_CPU | CLASS_CPU_CACHE | CLASS_MEMORY),
	STRESSOR(switch, SWITCH, CLASS_SCHEDULER | CLASS_OS),
	STRESSOR(symlink, SYMLINK, CLASS_FILESYSTEM | CLASS_OS),
	STRESSOR(sync_file, SYNC_FILE, CLASS_IO | CLASS_FILESYSTEM | CLASS_OS),
	STRESSOR(sysfs, SYSFS, CLASS_OS),
	STRESSOR(sysinfo, SYSINFO, CLASS_OS),
	STRESSOR(tee, TEE, CLASS_PIPE_IO | CLASS_OS | CLASS_SCHEDULER),
	STRESSOR(timer, TIMER, CLASS_INTERRUPT | CLASS_OS),
	STRESSOR(timerfd, TIMERFD, CLASS_INTERRUPT | CLASS_OS),
	STRESSOR(tlb_shootdown, TLB_SHOOTDOWN, CLASS_OS | CLASS_MEMORY),
	STRESSOR(tmpfs, TMPFS, CLASS_MEMORY | CLASS_VM | CLASS_OS),
	STRESSOR(tsc, TSC, CLASS_CPU),
	STRESSOR(tsearch, TSEARCH, CLASS_CPU_CACHE | CLASS_CPU | CLASS_MEMORY),
	STRESSOR(udp, UDP, CLASS_NETWORK | CLASS_OS),
	STRESSOR(udp_flood, UDP_FLOOD, CLASS_NETWORK | CLASS_OS),
	STRESSOR(unshare, UNSHARE, CLASS_OS),
	STRESSOR(urandom, URANDOM, CLASS_DEV | CLASS_OS),
	STRESSOR(userfaultfd, USERFAULTFD, CLASS_VM | CLASS_OS),
	STRESSOR(utime, UTIME, CLASS_FILESYSTEM | CLASS_OS),
	STRESSOR(vecmath, VECMATH, CLASS_CPU | CLASS_CPU_CACHE),
	STRESSOR(vfork, VFORK, CLASS_SCHEDULER | CLASS_OS),
	STRESSOR(vforkmany, VFORKMANY, CLASS_SCHEDULER | CLASS_OS),
	STRESSOR(vm, VM, CLASS_VM | CLASS_MEMORY | CLASS_OS),
	STRESSOR(vm_rw, VM_RW, CLASS_VM | CLASS_MEMORY | CLASS_OS),
	STRESSOR(vm_splice, VM_SPLICE, CLASS_VM | CLASS_PIPE_IO | CLASS_OS),
	STRESSOR(wait, WAIT, CLASS_SCHEDULER | CLASS_OS),
	STRESSOR(wcs, WCS, CLASS_CPU | CLASS_CPU_CACHE | CLASS_MEMORY),
	STRESSOR(xattr, XATTR, CLASS_FILESYSTEM | CLASS_OS),
	STRESSOR(yield, YIELD, CLASS_SCHEDULER | CLASS_OS),
	STRESSOR(zero, ZERO, CLASS_DEV | CLASS_MEMORY | CLASS_OS),
	STRESSOR(zlib, ZLIB, CLASS_CPU | CLASS_CPU_CACHE | CLASS_MEMORY),
	STRESSOR(zombie, ZOMBIE, CLASS_SCHEDULER | CLASS_OS),
	{ NULL, STRESS_MAX, 0, 0, NULL, 0 }
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
	{ "af-alg",	1,	0,	OPT_AF_ALG },
	{ "af-alg-ops",	1,	0,	OPT_AF_ALG_OPS },
	{ "affinity",	1,	0,	OPT_AFFINITY },
	{ "affinity-ops",1,	0,	OPT_AFFINITY_OPS },
	{ "affinity-rand",0,	0,	OPT_AFFINITY_RAND },
	{ "aggressive",	0,	0,	OPT_AGGRESSIVE },
	{ "aio",	1,	0,	OPT_AIO },
	{ "aio-ops",	1,	0,	OPT_AIO_OPS },
	{ "aio-requests",1,	0,	OPT_AIO_REQUESTS },
	{ "aiol",	1,	0,	OPT_AIO_LINUX },
	{ "aiol-ops",	1,	0,	OPT_AIO_LINUX_OPS },
	{ "aiol-requests",1,	0,	OPT_AIO_LINUX_REQUESTS },
	{ "all",	1,	0,	OPT_ALL },
	{ "apparmor",	1,	0,	OPT_APPARMOR },
	{ "apparmor-ops",1,	0,	OPT_APPARMOR_OPS },
	{ "atomic",	1,	0,	OPT_ATOMIC },
	{ "atomic-ops",	1,	0,	OPT_ATOMIC_OPS },
	{ "backoff",	1,	0,	OPT_BACKOFF },
	{ "bigheap",	1,	0,	OPT_BIGHEAP },
	{ "bigheap-ops",1,	0,	OPT_BIGHEAP_OPS },
	{ "bigheap-growth",1,	0,	OPT_BIGHEAP_GROWTH },
	{ "bind-mount",	1,	0,	OPT_BIND_MOUNT },
	{ "bind-mount-ops",1,	0,	OPT_BIND_MOUNT_OPS },
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
	{ "cap",	1,	0, 	OPT_CAP },
	{ "cap-ops",	1,	0, 	OPT_CAP_OPS },
	{ "chdir",	1,	0, 	OPT_CHDIR },
	{ "chdir-ops",	1,	0, 	OPT_CHDIR_OPS },
	{ "chmod",	1,	0, 	OPT_CHMOD },
	{ "chmod-ops",	1,	0,	OPT_CHMOD_OPS },
	{ "chown",	1,	0, 	OPT_CHOWN},
	{ "chown-ops",	1,	0,	OPT_CHOWN_OPS },
	{ "chroot",	1,	0, 	OPT_CHROOT},
	{ "chroot-ops",	1,	0,	OPT_CHROOT_OPS },
	{ "class",	1,	0,	OPT_CLASS },
	{ "clock",	1,	0,	OPT_CLOCK },
	{ "clock-ops",	1,	0,	OPT_CLOCK_OPS },
	{ "clone",	1,	0,	OPT_CLONE },
	{ "clone-ops",	1,	0,	OPT_CLONE_OPS },
	{ "clone-max",	1,	0,	OPT_CLONE_MAX },
	{ "context",	1,	0,	OPT_CONTEXT },
	{ "context-ops",1,	0,	OPT_CONTEXT_OPS },
	{ "copy-file",	1,	0,	OPT_COPY_FILE },
	{ "copy-file-ops", 1,	0,	OPT_COPY_FILE_OPS },
	{ "copy-file-bytes", 1, 0,	OPT_COPY_FILE_BYTES },
	{ "cpu",	1,	0,	OPT_CPU },
	{ "cpu-ops",	1,	0,	OPT_CPU_OPS },
	{ "cpu-load",	1,	0,	OPT_CPU_LOAD },
	{ "cpu-load-slice",1,	0,	OPT_CPU_LOAD_SLICE },
	{ "cpu-method",	1,	0,	OPT_CPU_METHOD },
	{ "cpu-online",	1,	0,	OPT_CPU_ONLINE },
	{ "cpu-online-ops",1,	0,	OPT_CPU_ONLINE_OPS },
	{ "crypt",	1,	0,	OPT_CRYPT },
	{ "crypt-ops",	1,	0,	OPT_CRYPT_OPS },
	{ "daemon",	1,	0,	OPT_DAEMON },
	{ "daemon-ops",	1,	0,	OPT_DAEMON_OPS },
	{ "dccp",	1,	0,	OPT_DCCP },
	{ "dccp-domain",1,	0,	OPT_DCCP_DOMAIN },
	{ "dccp-ops",	1,	0,	OPT_DCCP_OPS },
	{ "dccp-opts",	1,	0,	OPT_DCCP_OPTS },
	{ "dccp-port",	1,	0,	OPT_DCCP_PORT },
	{ "dentry",	1,	0,	OPT_DENTRY },
	{ "dentry-ops",	1,	0,	OPT_DENTRY_OPS },
	{ "dentries",	1,	0,	OPT_DENTRIES },
	{ "dentry-order",1,	0,	OPT_DENTRY_ORDER },
	{ "dir",	1,	0,	OPT_DIR },
	{ "dir-ops",	1,	0,	OPT_DIR_OPS },
	{ "dirdeep",	1,	0,	OPT_DIRDEEP },
	{ "dirdeep-ops",1,	0,	OPT_DIRDEEP_OPS },
	{ "dry-run",	0,	0,	OPT_DRY_RUN },
	{ "dnotify",	1,	0,	OPT_DNOTIFY },
	{ "dnotify-ops",1,	0,	OPT_DNOTIFY_OPS },
	{ "dup",	1,	0,	OPT_DUP },
	{ "dup-ops",	1,	0,	OPT_DUP_OPS },
	{ "epoll",	1,	0,	OPT_EPOLL },
	{ "epoll-ops",	1,	0,	OPT_EPOLL_OPS },
	{ "epoll-port",	1,	0,	OPT_EPOLL_PORT },
	{ "epoll-domain",1,	0,	OPT_EPOLL_DOMAIN },
	{ "eventfd",	1,	0,	OPT_EVENTFD },
	{ "eventfd-ops",1,	0,	OPT_EVENTFD_OPS },
	{ "exclude",	1,	0,	OPT_EXCLUDE },
	{ "exec",	1,	0,	OPT_EXEC },
	{ "exec-ops",	1,	0,	OPT_EXEC_OPS },
	{ "exec-max",	1,	0,	OPT_EXEC_MAX },
	{ "fallocate",	1,	0,	OPT_FALLOCATE },
	{ "fallocate-ops",1,	0,	OPT_FALLOCATE_OPS },
	{ "fallocate-bytes",1,	0,	OPT_FALLOCATE_BYTES },
	{ "fault",	1,	0,	OPT_FAULT },
	{ "fault-ops",	1,	0,	OPT_FAULT_OPS },
	{ "fcntl",	1,	0,	OPT_FCNTL},
	{ "fcntl-ops",	1,	0,	OPT_FCNTL_OPS },
	{ "fiemap",	1,	0,	OPT_FIEMAP },
	{ "fiemap-ops",	1,	0,	OPT_FIEMAP_OPS },
	{ "fiemap-bytes",1,	0,	OPT_FIEMAP_BYTES },
	{ "fifo",	1,	0,	OPT_FIFO },
	{ "fifo-ops",	1,	0,	OPT_FIFO_OPS },
	{ "fifo-readers",1,	0,	OPT_FIFO_READERS },
	{ "filename",	1,	0,	OPT_FILENAME },
	{ "filename-ops",1,	0,	OPT_FILENAME_OPS },
	{ "filename-opts",1,	0,	OPT_FILENAME_OPTS },
	{ "flock",	1,	0,	OPT_FLOCK },
	{ "flock-ops",	1,	0,	OPT_FLOCK_OPS },
	{ "fanotify",	1,	0,	OPT_FANOTIFY },
	{ "fanotify-ops",1,	0,	OPT_FANOTIFY_OPS },
	{ "fork",	1,	0,	OPT_FORK },
	{ "fork-ops",	1,	0,	OPT_FORK_OPS },
	{ "fork-max",	1,	0,	OPT_FORK_MAX },
	{ "fp-error",	1,	0,	OPT_FP_ERROR},
	{ "fp-error-ops",1,	0,	OPT_FP_ERROR_OPS },
	{ "fstat",	1,	0,	OPT_FSTAT },
	{ "fstat-ops",	1,	0,	OPT_FSTAT_OPS },
	{ "fstat-dir",	1,	0,	OPT_FSTAT_DIR },
	{ "full",	1,	0,	OPT_FULL },
	{ "full-ops",	1,	0,	OPT_FULL_OPS },
	{ "futex",	1,	0,	OPT_FUTEX },
	{ "futex-ops",	1,	0,	OPT_FUTEX_OPS },
	{ "get",	1,	0,	OPT_GET },
	{ "get-ops",	1,	0,	OPT_GET_OPS },
	{ "getrandom",	1,	0,	OPT_GETRANDOM },
	{ "getrandom-ops",1,	0,	OPT_GETRANDOM_OPS },
	{ "getdent",	1,	0,	OPT_GETDENT },
	{ "getdent-ops",1,	0,	OPT_GETDENT_OPS },
	{ "handle",	1,	0,	OPT_HANDLE },
	{ "handle-ops",	1,	0,	OPT_HANDLE_OPS },
	{ "hdd",	1,	0,	OPT_HDD },
	{ "hdd-ops",	1,	0,	OPT_HDD_OPS },
	{ "hdd-bytes",	1,	0,	OPT_HDD_BYTES },
	{ "hdd-write-size", 1,	0,	OPT_HDD_WRITE_SIZE },
	{ "hdd-opts",	1,	0,	OPT_HDD_OPTS },
	{ "heapsort",	1,	0,	OPT_HEAPSORT },
	{ "heapsort-ops",1,	0,	OPT_HEAPSORT_OPS },
	{ "heapsort-size",1,	0,	OPT_HEAPSORT_INTEGERS },
	{ "help",	0,	0,	OPT_HELP },
	{ "hsearch",	1,	0,	OPT_HSEARCH },
	{ "hsearch-ops",1,	0,	OPT_HSEARCH_OPS },
	{ "hsearch-size",1,	0,	OPT_HSEARCH_SIZE },
	{ "icache",	1,	0,	OPT_ICACHE },
	{ "icache-ops",	1,	0,	OPT_ICACHE_OPS },
	{ "icmp-flood",	1,	0,	OPT_ICMP_FLOOD },
	{ "icmp-flood-ops",1,	0,	OPT_ICMP_FLOOD_OPS },
	{ "ignite-cpu",	0,	0, 	OPT_IGNITE_CPU },
	{ "inotify",	1,	0,	OPT_INOTIFY },
	{ "inotify-ops",1,	0,	OPT_INOTIFY_OPS },
	{ "io",		1,	0,	OPT_IOSYNC },
	{ "io-ops",	1,	0,	OPT_IOSYNC_OPS },
	{ "iomix",	1,	0,	OPT_IOMIX },
	{ "iomix-bytes",1,	0,	OPT_IOMIX_BYTES },
	{ "iomix-ops",	1,	0,	OPT_IOMIX_OPS },
	{ "ionice-class",1,	0,	OPT_IONICE_CLASS },
	{ "ionice-level",1,	0,	OPT_IONICE_LEVEL },
	{ "ioprio",	1,	0,	OPT_IOPRIO },
	{ "ioprio-ops",	1,	0,	OPT_IOPRIO_OPS },
	{ "itimer",	1,	0,	OPT_ITIMER },
	{ "itimer-ops",	1,	0,	OPT_ITIMER_OPS },
	{ "itimer-freq",1,	0,	OPT_ITIMER_FREQ },
	{ "kcmp",	1,	0,	OPT_KCMP },
	{ "kcmp-ops",	1,	0,	OPT_KCMP_OPS },
	{ "key",	1,	0,	OPT_KEY },
	{ "key-ops",	1,	0,	OPT_KEY_OPS },
	{ "keep-name",	0,	0,	OPT_KEEP_NAME },
	{ "kill",	1,	0,	OPT_KILL },
	{ "kill-ops",	1,	0,	OPT_KILL_OPS },
	{ "klog",	1,	0,	OPT_KLOG },
	{ "klog-ops",	1,	0,	OPT_KLOG_OPS },
	{ "lease",	1,	0,	OPT_LEASE },
	{ "lease-ops",	1,	0,	OPT_LEASE_OPS },
	{ "lease-breakers",1,	0,	OPT_LEASE_BREAKERS },
	{ "link",	1,	0,	OPT_LINK },
	{ "link-ops",	1,	0,	OPT_LINK_OPS },
	{ "locka",	1,	0,	OPT_LOCKA },
	{ "locka-ops",	1,	0,	OPT_LOCKA_OPS },
	{ "lockbus",	1,	0,	OPT_LOCKBUS },
	{ "lockbus-ops",1,	0,	OPT_LOCKBUS_OPS },
	{ "lockf",	1,	0,	OPT_LOCKF },
	{ "lockf-ops",	1,	0,	OPT_LOCKF_OPS },
	{ "lockf-nonblock", 0,	0,	OPT_LOCKF_NONBLOCK },
	{ "lockofd",	1,	0,	OPT_LOCKOFD },
	{ "lockofd-ops",1,	0,	OPT_LOCKOFD_OPS },
	{ "log-brief",	0,	0,	OPT_LOG_BRIEF },
	{ "log-file",	1,	0,	OPT_LOG_FILE },
	{ "longjmp",	1,	0,	OPT_LONGJMP },
	{ "longjmp-ops",1,	0,	OPT_LONGJMP_OPS },
	{ "lsearch",	1,	0,	OPT_LSEARCH },
	{ "lsearch-ops",1,	0,	OPT_LSEARCH_OPS },
	{ "lsearch-size",1,	0,	OPT_LSEARCH_SIZE },
	{ "madvise",	1,	0,	OPT_MADVISE },
	{ "madvise-ops",1,	0,	OPT_MADVISE_OPS },
	{ "malloc",	1,	0,	OPT_MALLOC },
	{ "malloc-bytes",1,	0,	OPT_MALLOC_BYTES },
	{ "malloc-max",	1,	0,	OPT_MALLOC_MAX },
	{ "malloc-ops",	1,	0,	OPT_MALLOC_OPS },
	{ "malloc-thresh",1,	0,	OPT_MALLOC_THRESHOLD },
	{ "matrix",	1,	0,	OPT_MATRIX },
	{ "matrix-ops",	1,	0,	OPT_MATRIX_OPS },
	{ "matrix-method",1,	0,	OPT_MATRIX_METHOD },
	{ "matrix-size",1,	0,	OPT_MATRIX_SIZE },
	{ "maximize",	0,	0,	OPT_MAXIMIZE },
	{ "membarrier",	1,	0,	OPT_MEMBARRIER },
	{ "membarrier-ops",1,	0,	OPT_MEMBARRIER_OPS },
	{ "memcpy",	1,	0,	OPT_MEMCPY },
	{ "memcpy",	1,	0,	OPT_MEMCPY },
	{ "memcpy",	1,	0,	OPT_MEMCPY },
	{ "memcpy-ops",	1,	0,	OPT_MEMCPY_OPS },
	{ "memfd",	1,	0,	OPT_MEMFD },
	{ "memfd-ops",	1,	0,	OPT_MEMFD_OPS },
	{ "memfd-bytes",1,	0,	OPT_MEMFD_BYTES },
	{ "mergesort",	1,	0,	OPT_MERGESORT },
	{ "mergesort-ops",1,	0,	OPT_MERGESORT_OPS },
	{ "mergesort-size",1,	0,	OPT_MERGESORT_INTEGERS },
	{ "metrics",	0,	0,	OPT_METRICS },
	{ "metrics-brief",0,	0,	OPT_METRICS_BRIEF },
	{ "mincore",	1,	0,	OPT_MINCORE },
	{ "mincore-ops",1,	0,	OPT_MINCORE_OPS },
	{ "mincore-random",0,	0,	OPT_MINCORE_RAND },
	{ "minimize",	0,	0,	OPT_MINIMIZE },
	{ "mknod",	1,	0,	OPT_MKNOD },
	{ "mknod-ops",	1,	0,	OPT_MKNOD_OPS },
	{ "mlock",	1,	0,	OPT_MLOCK },
	{ "mlock-ops",	1,	0,	OPT_MLOCK_OPS },
	{ "mmap",	1,	0,	OPT_MMAP },
	{ "mmap-ops",	1,	0,	OPT_MMAP_OPS },
	{ "mmap-async",	0,	0,	OPT_MMAP_ASYNC },
	{ "mmap-bytes",	1,	0,	OPT_MMAP_BYTES },
	{ "mmap-file",	0,	0,	OPT_MMAP_FILE },
	{ "mmap-mprotect",0,	0,	OPT_MMAP_MPROTECT },
	{ "mmapfork",	1,	0,	OPT_MMAPFORK },
	{ "mmapfork-ops",1,	0,	OPT_MMAPFORK_OPS },
	{ "mmapmany",	1,	0,	OPT_MMAPMANY },
	{ "mmapmany-ops",1,	0,	OPT_MMAPMANY_OPS },
	{ "mq",		1,	0,	OPT_MQ },
	{ "mq-ops",	1,	0,	OPT_MQ_OPS },
	{ "mq-size",	1,	0,	OPT_MQ_SIZE },
	{ "mremap",	1,	0,	OPT_MREMAP },
	{ "mremap-ops",	1,	0,	OPT_MREMAP_OPS },
	{ "mremap-bytes",1,	0,	OPT_MREMAP_BYTES },
	{ "msg",	1,	0,	OPT_MSG },
	{ "msg-ops",	1,	0,	OPT_MSG_OPS },
	{ "msync",	1,	0,	OPT_MSYNC },
	{ "msync-ops",	1,	0,	OPT_MSYNC_OPS },
	{ "msync-bytes",1,	0,	OPT_MSYNC_BYTES },
	{ "netlink-proc",1,	0,	OPT_NETLINK_PROC },
	{ "netlink-proc-ops",1,	0,	OPT_NETLINK_PROC_OPS },
	{ "nice",	1,	0,	OPT_NICE },
	{ "nice-ops",	1,	0,	OPT_NICE_OPS },
	{ "no-madvise",	0,	0,	OPT_NO_MADVISE },
	{ "no-rand-seed", 0,	0,	OPT_NO_RAND_SEED },
	{ "nop",	1,	0,	OPT_NOP },
	{ "nop-ops",	1,	0,	OPT_NOP_OPS },
	{ "null",	1,	0,	OPT_NULL },
	{ "null-ops",	1,	0,	OPT_NULL_OPS },
	{ "numa",	1,	0,	OPT_NUMA },
	{ "numa-ops",	1,	0,	OPT_NUMA_OPS },
	{ "oom-pipe",	1,	0,	OPT_OOM_PIPE },
	{ "oom-pipe-ops",1,	0,	OPT_OOM_PIPE_OPS },
	{ "opcode",	1,	0,	OPT_OPCODE },
	{ "opcode-ops",	1,	0,	OPT_OPCODE_OPS },
	{ "open",	1,	0,	OPT_OPEN },
	{ "open-ops",	1,	0,	OPT_OPEN_OPS },
	{ "page-in",	0,	0,	OPT_PAGE_IN },
	{ "pathological",0,	0,	OPT_PATHOLOGICAL },
	{ "perf",	0,	0,	OPT_PERF_STATS },
	{ "personality",1,	0,	OPT_PERSONALITY },
	{ "personality-ops",1,	0,	OPT_PERSONALITY_OPS },
	{ "pipe",	1,	0,	OPT_PIPE },
	{ "pipe-ops",	1,	0,	OPT_PIPE_OPS },
	{ "pipe-data-size",1,	0,	OPT_PIPE_DATA_SIZE },
#if defined(F_SETPIPE_SZ)
	{ "pipe-size",	1,	0,	OPT_PIPE_SIZE },
#endif
	{ "poll",	1,	0,	OPT_POLL },
	{ "poll-ops",	1,	0,	OPT_POLL_OPS },
	{ "procfs",	1,	0,	OPT_PROCFS },
	{ "procfs-ops",	1,	0,	OPT_PROCFS_OPS },
	{ "pthread",	1,	0,	OPT_PTHREAD },
	{ "pthread-ops",1,	0,	OPT_PTHREAD_OPS },
	{ "pthread-max",1,	0,	OPT_PTHREAD_MAX },
	{ "ptrace",	1,	0,	OPT_PTRACE },
	{ "ptrace-ops",1,	0,	OPT_PTRACE_OPS },
	{ "pty",	1,	0,	OPT_PTY },
	{ "pty-ops",	1,	0,	OPT_PTY_OPS },
	{ "qsort",	1,	0,	OPT_QSORT },
	{ "qsort-ops",	1,	0,	OPT_QSORT_OPS },
	{ "qsort-size",	1,	0,	OPT_QSORT_INTEGERS },
	{ "quiet",	0,	0,	OPT_QUIET },
	{ "quota",	1,	0,	OPT_QUOTA },
	{ "quota-ops",	1,	0,	OPT_QUOTA_OPS },
	{ "random",	1,	0,	OPT_RANDOM },
	{ "rdrand",	1,	0,	OPT_RDRAND },
	{ "rdrand-ops",	1,	0,	OPT_RDRAND_OPS },
	{ "readahead",	1,	0,	OPT_READAHEAD },
	{ "readahead-ops",1,	0,	OPT_READAHEAD_OPS },
	{ "readahead-bytes",1,	0,	OPT_READAHEAD_BYTES },
	{ "remap",	1,	0,	OPT_REMAP_FILE_PAGES },
	{ "remap-ops",	1,	0,	OPT_REMAP_FILE_PAGES_OPS },
	{ "rename",	1,	0,	OPT_RENAME },
	{ "rename-ops",	1,	0,	OPT_RENAME_OPS },
	{ "resources",	1,	0,	OPT_RESOURCES },
	{ "resources-ops",1,	0,	OPT_RESOURCES_OPS },
	{ "rlimit",	1,	0,	OPT_RLIMIT },
	{ "rlimit-ops",	1,	0,	OPT_RLIMIT_OPS },
	{ "rmap",	1,	0,	OPT_RMAP },
	{ "rmap-ops",	1,	0,	OPT_RMAP_OPS },
	{ "rtc",	1,	0,	OPT_RTC },
	{ "rtc-ops",	1,	0,	OPT_RTC_OPS },
	{ "sched",	1,	0,	OPT_SCHED },
	{ "sched-prio",	1,	0,	OPT_SCHED_PRIO },
	{ "schedpolicy",1,	0,	OPT_SCHEDPOLICY },
	{ "schedpolicy-ops",1,	0,	OPT_SCHEDPOLICY_OPS },
	{ "sctp",	1,	0,	OPT_SCTP },
	{ "sctp-ops",	1,	0,	OPT_SCTP_OPS },
	{ "sctp-domain",1,	0,	OPT_SCTP_DOMAIN },
	{ "sctp-port",	1,	0,	OPT_SCTP_PORT },
	{ "seal",	1,	0,	OPT_SEAL },
	{ "seal-ops",	1,	0,	OPT_SEAL_OPS },
	{ "seccomp",	1,	0,	OPT_SECCOMP },
	{ "seccomp-ops",1,	0,	OPT_SECCOMP_OPS },
	{ "seek",	1,	0,	OPT_SEEK },
	{ "seek-ops",	1,	0,	OPT_SEEK_OPS },
	{ "seek-punch",	0,	0,	OPT_SEEK_PUNCH  },
	{ "seek-size",	1,	0,	OPT_SEEK_SIZE },
	{ "sem",	1,	0,	OPT_SEMAPHORE_POSIX },
	{ "sem-ops",	1,	0,	OPT_SEMAPHORE_POSIX_OPS },
	{ "sem-procs",	1,	0,	OPT_SEMAPHORE_POSIX_PROCS },
	{ "sem-sysv",	1,	0,	OPT_SEMAPHORE_SYSV },
	{ "sem-sysv-ops",1,	0,	OPT_SEMAPHORE_SYSV_OPS },
	{ "sem-sysv-procs",1,	0,	OPT_SEMAPHORE_SYSV_PROCS },
	{ "sendfile",	1,	0,	OPT_SENDFILE },
	{ "sendfile-ops",1,	0,	OPT_SENDFILE_OPS },
	{ "sendfile-size",1,	0,	OPT_SENDFILE_SIZE },
	{ "sequential",	1,	0,	OPT_SEQUENTIAL },
	{ "shm",	1,	0,	OPT_SHM_POSIX },
	{ "shm-ops",	1,	0,	OPT_SHM_POSIX_OPS },
	{ "shm-bytes",	1,	0,	OPT_SHM_POSIX_BYTES },
	{ "shm-objs",	1,	0,	OPT_SHM_POSIX_OBJECTS },
	{ "shm-sysv",	1,	0,	OPT_SHM_SYSV },
	{ "shm-sysv-ops",1,	0,	OPT_SHM_SYSV_OPS },
	{ "shm-sysv-bytes",1,	0,	OPT_SHM_SYSV_BYTES },
	{ "shm-sysv-segs",1,	0,	OPT_SHM_SYSV_SEGMENTS },
	{ "sigfd",	1,	0,	OPT_SIGFD },
	{ "sigfd-ops",	1,	0,	OPT_SIGFD_OPS },
	{ "sigfpe",	1,	0,	OPT_SIGFPE },
	{ "sigfpe-ops",	1,	0,	OPT_SIGFPE_OPS },
	{ "sigsegv",	1,	0,	OPT_SIGSEGV },
	{ "sigsegv-ops",1,	0,	OPT_SIGSEGV_OPS },
	{ "sigsuspend",	1,	0,	OPT_SIGSUSPEND},
	{ "sigsuspend-ops",1,	0,	OPT_SIGSUSPEND_OPS},
	{ "sigpending",	1,	0,	OPT_SIGPENDING},
	{ "sigpending-ops",1,	0,	OPT_SIGPENDING_OPS },
	{ "sigq",	1,	0,	OPT_SIGQUEUE },
	{ "sigq-ops",	1,	0,	OPT_SIGQUEUE_OPS },
	{ "sleep",	1,	0,	OPT_SLEEP },
	{ "sleep-ops",	1,	0,	OPT_SLEEP_OPS },
	{ "sleep-max",	1,	0,	OPT_SLEEP_MAX },
	{ "sock",	1,	0,	OPT_SOCKET },
	{ "sock-domain",1,	0,	OPT_SOCKET_DOMAIN },
	{ "sock-nodelay",0,	0,	OPT_SOCKET_NODELAY },
	{ "sock-ops",	1,	0,	OPT_SOCKET_OPS },
	{ "sock-opts",	1,	0,	OPT_SOCKET_OPTS },
	{ "sock-port",	1,	0,	OPT_SOCKET_PORT },
	{ "sock-type",	1,	0,	OPT_SOCKET_TYPE },
	{ "sockfd",	1,	0,	OPT_SOCKET_FD },
	{ "sockfd-ops",1,	0,	OPT_SOCKET_FD_OPS },
	{ "sockfd-port",1,	0,	OPT_SOCKET_FD_PORT },
	{ "sockpair",	1,	0,	OPT_SOCKET_PAIR },
	{ "sockpair-ops",1,	0,	OPT_SOCKET_PAIR_OPS },
	{ "spawn",	1,	0,	OPT_SPAWN },
	{ "spawn-ops",	1,	0,	OPT_SPAWN_OPS },
	{ "splice",	1,	0,	OPT_SPLICE },
	{ "splice-bytes",1,	0,	OPT_SPLICE_BYTES },
	{ "splice-ops",	1,	0,	OPT_SPLICE_OPS },
	{ "stack",	1,	0,	OPT_STACK},
	{ "stack-fill",	0,	0,	OPT_STACK_FILL },
	{ "stack-ops",	1,	0,	OPT_STACK_OPS },
	{ "stackmmap",	1,	0,	OPT_STACKMMAP },
	{ "stackmmap-ops",1,	0,	OPT_STACKMMAP_OPS },
	{ "str",	1,	0,	OPT_STR },
	{ "str-ops",	1,	0,	OPT_STR_OPS },
	{ "str-method",	1,	0,	OPT_STR_METHOD },
	{ "stressors",	0,	0,	OPT_STRESSORS },
	{ "stream",	1,	0,	OPT_STREAM },
	{ "stream-ops",	1,	0,	OPT_STREAM_OPS },
	{ "stream-l3-size" ,1,	0,	OPT_STREAM_L3_SIZE },
	{ "switch",	1,	0,	OPT_SWITCH },
	{ "switch-ops",	1,	0,	OPT_SWITCH_OPS },
	{ "symlink",	1,	0,	OPT_SYMLINK },
	{ "symlink-ops",1,	0,	OPT_SYMLINK_OPS },
	{ "sync-file",	1,	0,	OPT_SYNC_FILE },
	{ "sync-file-ops", 1,	0,	OPT_SYNC_FILE_OPS },
	{ "sync-file-bytes", 1,	0,	OPT_SYNC_FILE_BYTES },
	{ "sysfs",	1,	0,	OPT_SYSFS },
	{ "sysfs-ops",1,	0,	OPT_SYSFS_OPS },
	{ "sysinfo",	1,	0,	OPT_SYSINFO },
	{ "sysinfo-ops",1,	0,	OPT_SYSINFO_OPS },
	{ "syslog",	0,	0,	OPT_SYSLOG },
	{ "taskset",	1,	0,	OPT_TASKSET },
	{ "tee",	1,	0,	OPT_TEE },
	{ "tee-ops",	1,	0,	OPT_TEE_OPS },
	{ "temp-path",	1,	0,	OPT_TEMP_PATH },
	{ "timeout",	1,	0,	OPT_TIMEOUT },
	{ "timer",	1,	0,	OPT_TIMER },
	{ "timer-ops",	1,	0,	OPT_TIMER_OPS },
	{ "timer-freq",	1,	0,	OPT_TIMER_FREQ },
	{ "timer-rand", 0,	0,	OPT_TIMER_RAND },
	{ "timerfd",	1,	0,	OPT_TIMERFD },
	{ "timerfd-ops",1,	0,	OPT_TIMERFD_OPS },
	{ "timerfd-freq",1,	0,	OPT_TIMERFD_FREQ },
	{ "timerfd-rand",0,	0,	OPT_TIMERFD_RAND },
	{ "timer-slack",1,	0,	OPT_TIMER_SLACK },
	{ "tlb-shootdown",1,	0,	OPT_TLB_SHOOTDOWN },
	{ "tlb-shootdown-ops",1,0,	OPT_TLB_SHOOTDOWN_OPS },
	{ "tmpfs",	1,	0,	OPT_TMPFS },
	{ "tmpfs-ops",	1,	0,	OPT_TMPFS_OPS },
	{ "tsc",	1,	0,	OPT_TSC },
	{ "tsc-ops",	1,	0,	OPT_TSC_OPS },
	{ "tsearch",	1,	0,	OPT_TSEARCH },
	{ "tsearch-ops",1,	0,	OPT_TSEARCH_OPS },
	{ "tsearch-size",1,	0,	OPT_TSEARCH_SIZE },
	{ "thrash",	0,	0,	OPT_THRASH },
	{ "times",	0,	0,	OPT_TIMES },
	{ "tz",		0,	0,	OPT_THERMAL_ZONES },
	{ "udp",	1,	0,	OPT_UDP },
	{ "udp-ops",	1,	0,	OPT_UDP_OPS },
	{ "udp-domain",1,	0,	OPT_UDP_DOMAIN },
	{ "udp-lite",	0,	0,	OPT_UDP_LITE },
	{ "udp-port",	1,	0,	OPT_UDP_PORT },
	{ "udp-flood",	1,	0,	OPT_UDP_FLOOD },
	{ "udp-flood-domain",1,	0,	OPT_UDP_FLOOD_DOMAIN },
	{ "udp-flood-ops",1,	0,	OPT_UDP_FLOOD_OPS },
	{ "userfaultfd",1,	0,	OPT_USERFAULTFD },
	{ "userfaultfd-ops",1,	0,	OPT_USERFAULTFD_OPS },
	{ "userfaultfd-bytes",1,0,	OPT_USERFAULTFD_BYTES },
	{ "utime",	1,	0,	OPT_UTIME },
	{ "utime-ops",	1,	0,	OPT_UTIME_OPS },
	{ "utime-fsync",0,	0,	OPT_UTIME_FSYNC },
	{ "unshare",	1,	0,	OPT_UNSHARE },
	{ "unshare-ops",1,	0,	OPT_UNSHARE_OPS },
	{ "urandom",	1,	0,	OPT_URANDOM },
	{ "urandom-ops",1,	0,	OPT_URANDOM_OPS },
	{ "vecmath",	1,	0,	OPT_VECMATH },
	{ "vecmath-ops",1,	0,	OPT_VECMATH_OPS },
	{ "verbose",	0,	0,	OPT_VERBOSE },
	{ "verify",	0,	0,	OPT_VERIFY },
	{ "version",	0,	0,	OPT_VERSION },
	{ "vfork",	1,	0,	OPT_VFORK },
	{ "vfork-ops",	1,	0,	OPT_VFORK_OPS },
	{ "vfork-max",	1,	0,	OPT_VFORK_MAX },
	{ "vforkmany",	1,	0,	OPT_VFORKMANY },
	{ "vforkmany-ops", 1,	0,	OPT_VFORKMANY_OPS },
	{ "vm",		1,	0,	OPT_VM },
	{ "vm-bytes",	1,	0,	OPT_VM_BYTES },
	{ "vm-hang",	1,	0,	OPT_VM_HANG },
	{ "vm-keep",	0,	0,	OPT_VM_KEEP },
#if defined(MAP_POPULATE)
	{ "vm-populate",0,	0,	OPT_VM_MMAP_POPULATE },
#endif
#if defined(MAP_LOCKED)
	{ "vm-locked",	0,	0,	OPT_VM_MMAP_LOCKED },
#endif
	{ "vm-ops",	1,	0,	OPT_VM_OPS },
	{ "vm-method",	1,	0,	OPT_VM_METHOD },
	{ "vm-rw",	1,	0,	OPT_VM_RW },
	{ "vm-rw-bytes",1,	0,	OPT_VM_RW_BYTES },
	{ "vm-rw-ops",	1,	0,	OPT_VM_RW_OPS },
	{ "vm-splice",	1,	0,	OPT_VM_SPLICE },
	{ "vm-splice-bytes",1,	0,	OPT_VM_SPLICE_BYTES },
	{ "vm-splice-ops",1,	0,	OPT_VM_SPLICE_OPS },
	{ "wait",	1,	0,	OPT_WAIT },
	{ "wait-ops",	1,	0,	OPT_WAIT_OPS },
	{ "wcs",	1,	0,	OPT_WCS},
	{ "wcs-ops",	1,	0,	OPT_WCS_OPS },
	{ "wcs-method",	1,	0,	OPT_WCS_METHOD },
	{ "xattr",	1,	0,	OPT_XATTR },
	{ "xattr-ops",	1,	0,	OPT_XATTR_OPS },
	{ "yaml",	1,	0,	OPT_YAML },
	{ "yield",	1,	0,	OPT_YIELD },
	{ "yield-ops",	1,	0,	OPT_YIELD_OPS },
	{ "zero",	1,	0,	OPT_ZERO },
	{ "zero-ops",	1,	0,	OPT_ZERO_OPS },
	{ "zlib",	1,	0,	OPT_ZLIB },
	{ "zlib-ops",	1,	0,	OPT_ZLIB_OPS },
	{ "zlib-method",1,	0,	OPT_ZLIB_METHOD },
	{ "zombie",	1,	0,	OPT_ZOMBIE },
	{ "zombie-ops",	1,	0,	OPT_ZOMBIE_OPS },
	{ "zombie-max",	1,	0,	OPT_ZOMBIE_MAX },
	{ NULL,		0,	0,	0 }
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
	{ NULL,		"no-rand-seed",		"seed random numbers with the same constant" },
	{ NULL,		"page-in",		"touch allocated pages that are not in core" },
	{ NULL,		"pathological",		"enable stressors that are known to hang a machine" },
#if defined(STRESS_PERF_STATS)
	{ NULL,		"perf",			"display perf statistics" },
#endif
	{ "q",		"quiet",		"quiet output" },
	{ "r",		"random N",		"start N random workers" },
	{ NULL,		"sched type",		"set scheduler type" },
	{ NULL,		"sched-prio N",		"set scheduler priority level N" },
	{ NULL,		"sequential N",		"run all stressors one by one, invoking N of them" },
	{ NULL,		"stressors",		"show available stress tests" },
	{ NULL,		"syslog",		"log messages to the syslog" },
	{ NULL,		"taskset",		"use specific CPUs (set CPU affinity)" },
	{ NULL,		"temp-path",		"specify path for temporary directories and files" },
	{ NULL,		"thrash",		"force all pages in causing swap thrashing" },
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
	{ NULL,		"affinity N",		"start N workers that rapidly change CPU affinity" },
	{ NULL, 	"affinity-ops N",   	"stop after N affinity bogo operations" },
	{ NULL, 	"affinity-rand",   	"change affinity randomly rather than sequentially" },
	{ NULL,		"af-alg N",		"start N workers that stress AF_ALG socket domain" },
	{ NULL,		"af-alg-ops N",		"stop after N af-alg bogo operations" },
	{ NULL,		"aio N",		"start N workers that issue async I/O requests" },
	{ NULL,		"aio-ops N",		"stop after N bogo async I/O requests" },
	{ NULL,		"aio-requests N",	"number of async I/O requests per worker" },
	{ NULL,		"aiol N",		"start N workers that exercise Linux async I/O" },
	{ NULL,		"aiol-ops N",		"stop after N bogo Linux aio async I/O requests" },
	{ NULL,		"aiol-requests N",	"number of Linux aio async I/O requests per worker" },
	{ NULL,		"apparmor",		"start N workers exercising AppArmor interfaces" },
	{ NULL,		"apparmor-ops",		"stop after N bogo AppArmor worker bogo operations" },
	{ NULL,		"atomic",		"start N workers exercising GCC atomic operations" },
	{ NULL,		"atomic-ops",		"stop after N bogo atomic bogo operations" },
	{ "B N",	"bigheap N",		"start N workers that grow the heap using calloc()" },
	{ NULL,		"bigheap-ops N",	"stop after N bogo bigheap operations" },
	{ NULL, 	"bigheap-growth N",	"grow heap by N bytes per iteration" },
	{ NULL,		"bind-mount N",		"start N workers exercising bind mounts" },
	{ NULL,		"bind-mount-ops N",	"stop after N bogo bind mount operations" },
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
	{ NULL,		"cap N",		"start N workers exercsing capget" },
	{ NULL,		"cap-ops N",		"stop cap workers after N bogo capget operations" },
	{ NULL,		"chdir N",		"start N workers thrashing chdir on many paths" },
	{ NULL,		"chdir-ops N",		"stop chdir workers after N bogo chdir operations" },
	{ NULL,		"chmod N",		"start N workers thrashing chmod file mode bits " },
	{ NULL,		"chmod-ops N",		"stop chmod workers after N bogo operations" },
	{ NULL,		"chown N",		"start N workers thrashing chown file ownership" },
	{ NULL,		"chown-ops N",		"stop chown workers after N bogo operations" },
	{ NULL,		"chroot N",		"start N workers thrashing chroot" },
	{ NULL,		"chroot-ops N",		"stop chhroot workers after N bogo operations" },
	{ NULL,		"clock N",		"start N workers thrashing clocks and POSIX timers" },
	{ NULL,		"clock-ops N",		"stop clock workers after N bogo operations" },
	{ NULL,		"clone N",		"start N workers that rapidly create and reap clones" },
	{ NULL,		"clone-ops N",		"stop after N bogo clone operations" },
	{ NULL,		"clone-max N",		"set upper limit of N clones per worker" },
	{ NULL,		"context N",		"start N workers exercising user context" },
	{ NULL,		"context-ops N",	"stop context workers after N bogo operations" },
	{ NULL,		"copy-file N",		"start N workers that copy file data" },
	{ NULL,		"copy-file-ops N",	"stop after N copy bogo operations" },
	{ NULL,		"copy-file-bytes N",	"specify size of file to be copied" },
	{ "c N",	"cpu N",		"start N workers spinning on sqrt(rand())" },
	{ NULL,		"cpu-ops N",		"stop after N cpu bogo operations" },
	{ "l P",	"cpu-load P",		"load CPU by P %%, 0=sleep, 100=full load (see -c)" },
	{ NULL,		"cpu-load-slice S",	"specify time slice during busy load" },
	{ NULL,		"cpu-method m",		"specify stress cpu method m, default is all" },
	{ NULL,		"cpu-online N",		"start N workers offlining/onlining the CPUs" },
	{ NULL,		"cpu-online-ops N",	"stop after N offline/online operations" },
	{ NULL,		"crypt N",		"start N workers performing password encryption" },
	{ NULL,		"crypt-ops N",		"stop after N bogo crypt operations" },
	{ NULL,		"daemon N",		"start N workers creating multiple daemons" },
	{ NULL,		"daemon-ops N",		"stop when N daemons have been created" },
	{ NULL,		"dccp N",		"start N workers exercising network DCCP I/O" },
	{ NULL,		"dccp-domain D",	"specify DCCP domain, default is ipv4" },
	{ NULL,		"dccp-ops N",		"stop after N DCCP  bogo operations" },
	{ NULL,		"dccp-opts option",	"DCCP data send options [send|sendmsg|sendmmsg]" },
	{ NULL,		"dccp-port P",		"use DCCP ports P to P + number of workers - 1" },
	{ "D N",	"dentry N",		"start N dentry thrashing stressors" },
	{ NULL,		"dentry-ops N",		"stop after N dentry bogo operations" },
	{ NULL,		"dentry-order O",	"specify unlink order (reverse, forward, stride)" },
	{ NULL,		"dentries N",		"create N dentries per iteration" },
	{ NULL,		"dir N",		"start N directory thrashing stressors" },
	{ NULL,		"dir-ops N",		"stop after N directory bogo operations" },
	{ NULL,		"dirdeep N",		"start N directory depth stressors" },
	{ NULL,		"dirdeep-ops N",	"stop after N directory depth bogo operations" },
	{ NULL,		"dnotify N",		"start N workers exercising dnotify events" },
	{ NULL,		"dnotify-ops N",	"stop dnotify workers after N bogo operations" },
	{ NULL,		"dup N",		"start N workers exercising dup/close" },
	{ NULL,		"dup-ops N",		"stop after N dup/close bogo operations" },
	{ NULL,		"epoll N",		"start N workers doing epoll handled socket activity" },
	{ NULL,		"epoll-ops N",		"stop after N epoll bogo operations" },
	{ NULL,		"epoll-port P",		"use socket ports P upwards" },
	{ NULL,		"epoll-domain D",	"specify socket domain, default is unix" },
	{ NULL,		"eventfd N",		"start N workers stressing eventfd read/writes" },
	{ NULL,		"eventfd-ops N",	"stop eventfd workers after N bogo operations" },
	{ NULL,		"exec N",		"start N workers spinning on fork() and exec()" },
	{ NULL,		"exec-ops N",		"stop after N exec bogo operations" },
	{ NULL,		"exec-max P",		"create P workers per iteration, default is 1" },
	{ NULL,		"fallocate N",		"start N workers fallocating 16MB files" },
	{ NULL,		"fallocate-ops N",	"stop after N fallocate bogo operations" },
	{ NULL,		"fallocate-bytes N",	"specify size of file to allocate" },
	{ NULL,		"fanotify N",		"start N workers exercising fanotify events" },
	{ NULL,		"fanotify-ops N",	"stop fanotify workers after N bogo operations" },
	{ NULL,		"fault N",		"start N workers producing page faults" },
	{ NULL,		"fault-ops N",		"stop after N page fault bogo operations" },
	{ NULL,		"fcntl N",		"start N workers exercising fcntl commands" },
	{ NULL,		"fcntl-ops N",		"stop after N fcntl bogo operations" },
	{ NULL,		"fiemap N",		"start N workers exercising the FIEMAP ioctl" },
	{ NULL,		"fiemap-ops N",		"stop after N FIEMAP ioctl bogo operations" },
	{ NULL,		"fiemap-bytes N",	"specify size of file to fiemap" },
	{ NULL,		"fifo N",		"start N workers exercising fifo I/O" },
	{ NULL,		"fifo-ops N",		"stop after N fifo bogo operations" },
	{ NULL,		"fifo-readers N",	"number of fifo reader stessors to start" },
	{ NULL,		"filename N",		"start N workers exercising filenames" },
	{ NULL,		"filename-ops N",	"stop after N filename bogo operations" },
	{ NULL,		"filename-opts opt",	"specify allowed filename options" },
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
	{ NULL,		"full N",		"start N workers exercising /dev/full" },
	{ NULL,		"full-ops N",		"stop after N /dev/full bogo I/O operations" },
	{ NULL,		"futex N",		"start N workers exercising a fast mutex" },
	{ NULL,		"futex-ops N",		"stop after N fast mutex bogo operations" },
	{ NULL,		"get N",		"start N workers exercising the get*() system calls" },
	{ NULL,		"get-ops N",		"stop after N get bogo operations" },
	{ NULL,		"getdent N",		"start N workers reading directories using getdents" },
	{ NULL,		"getdent-ops N",	"stop after N getdents bogo operations" },
	{ NULL,		"getrandom N",		"start N workers fetching random data via getrandom()" },
	{ NULL,		"getrandom-ops N",	"stop after N getrandom bogo operations" },
	{ NULL,		"handle N",		"start N workers exercising name_to_handle_at" },
	{ NULL,		"handle-ops N",		"stop after N handle bogo operations" },
	{ "d N",	"hdd N",		"start N workers spinning on write()/unlink()" },
	{ NULL,		"hdd-ops N",		"stop after N hdd bogo operations" },
	{ NULL,		"hdd-bytes N",		"write N bytes per hdd worker (default is 1GB)" },
	{ NULL,		"hdd-opts list",	"specify list of various stressor options" },
	{ NULL,		"hdd-write-size N",	"set the default write size to N bytes" },
	{ NULL,		"heapsort N",		"start N workers heap sorting 32 bit random integers" },
	{ NULL,		"heapsort-ops N",	"stop after N heap sort bogo operations" },
	{ NULL,		"heapsort-size N",	"number of 32 bit integers to sort" },
	{ NULL,		"hsearch N",		"start N workers that exercise a hash table search" },
	{ NULL,		"hsearch-ops N",	"stop afer N hash search bogo operations" },
	{ NULL,		"hsearch-size N",	"number of integers to insert into hash table" },
	{ NULL,		"icache N",		"start N CPU instruction cache thrashing workers" },
	{ NULL,		"icache-ops N",		"stop after N icache bogo operations" },
	{ NULL,		"icmp-flood N",		"start N ICMP packet flood workers" },
	{ NULL,		"icmp-flood-ops N",	"stop after N ICMP bogo operations (ICMP packets)" },
	{ NULL,		"inotify N",		"start N workers exercising inotify events" },
	{ NULL,		"inotify-ops N",	"stop inotify workers after N bogo operations" },
	{ "i N",	"io N",			"start N workers spinning on sync()" },
	{ NULL,		"io-ops N",		"stop sync I/O after N io bogo operations" },
	{ NULL,		"iomix N",		"start N workers that have a mix of I/O operations" },
	{ NULL,		"iomix-bytes N",	"write N bytes per iomix worker (default is 1GB)" },
	{ NULL,		"iomix-ops N",		"stop iomix workers after N iomix bogo operations" },
	{ NULL,		"ionice-class C",	"specify ionice class (idle, besteffort, realtime)" },
	{ NULL,		"ionice-level L",	"specify ionice level (0 max, 7 min)" },
	{ NULL,		"ioprio N",		"start N workers exercising set/get iopriority" },
	{ NULL,		"ioprio-ops N",		"stop after N io bogo iopriority operations" },
	{ NULL,		"itimer N",		"start N workers exercising interval timers" },
	{ NULL,		"itimer-ops N",		"stop after N interval timer bogo operations" },
	{ NULL,		"kcmp N",		"start N workers exercising kcmp" },
	{ NULL,		"kcmp-ops N",		"stop after N kcmp bogo operations" },
	{ NULL,		"key N",		"start N workers exercising key operations" },
	{ NULL,		"key-ops N",		"stop after N key bogo operations" },
	{ NULL,		"kill N",		"start N workers killing with SIGUSR1" },
	{ NULL,		"kill-ops N",		"stop after N kill bogo operations" },
	{ NULL,		"klog N",		"start N workers exercising kernel syslog interface" },
	{ NULL,		"klog -ops N",		"stop after N klog bogo operations" },
	{ NULL,		"lease N",		"start N workers holding and breaking a lease" },
	{ NULL,		"lease-ops N",		"stop after N lease bogo operations" },
	{ NULL,		"lease-breakers N",	"number of lease breaking workers to start" },
	{ NULL,		"link N",		"start N workers creating hard links" },
	{ NULL,		"link-ops N",		"stop after N link bogo operations" },
	{ NULL,		"lockbus N",		"start N workers locking a memory increment" },
	{ NULL,		"lockbus-ops N",	"stop after N lockbus bogo operations" },
	{ NULL,		"locka N",		"start N workers locking a file via advisory locks" },
	{ NULL,		"locka-ops N",		"stop after N locka bogo operations" },
	{ NULL,		"lockf N",		"start N workers locking a single file via lockf" },
	{ NULL,		"lockf-ops N",		"stop after N lockf bogo operations" },
	{ NULL,		"lockf-nonblock",	"don't block if lock cannot be obtained, re-try" },
	{ NULL,		"lockofd N",		"start N workers using open file description locking" },
	{ NULL,		"lockofd-ops N",	"stop after N lockofd bogo operations" },
	{ NULL,		"longjmp N",		"start N workers exercising setjmp/longjmp" },
	{ NULL,		"longjmp-ops N",	"stop after N longjmp bogo operations" },
	{ NULL,		"lsearch N",		"start N workers that exercise a linear search" },
	{ NULL,		"lsearch-ops N",	"stop after N linear search bogo operations" },
	{ NULL,		"lsearch-size N",	"number of 32 bit integers to lsearch" },
	{ NULL,		"madvise N",		"start N workers exercising madvise on memory" },
	{ NULL,		"madvise-ops N",	"stop after N bogo madvise operations" },
	{ NULL,		"malloc N",		"start N workers exercising malloc/realloc/free" },
	{ NULL,		"malloc-bytes N",	"allocate up to N bytes per allocation" },
	{ NULL,		"malloc-max N",		"keep up to N allocations at a time" },
	{ NULL,		"malloc-ops N",		"stop after N malloc bogo operations" },
	{ NULL,		"malloc-thresh N",	"threshold where malloc uses mmap instead of sbrk" },
	{ NULL,		"matrix N",		"start N workers exercising matrix operations" },
	{ NULL,		"matrix-ops N",		"stop after N maxtrix bogo operations" },
	{ NULL,		"matrix-method m",	"specify matrix stress method m, default is all" },
	{ NULL,		"matrix-size N",	"specify the size of the N x N matrix" },
	{ NULL,		"membarrier N",		"start N workers performing membarrier system calls" },
	{ NULL,		"membarrier-ops N",	"stop after N membarrier bogo operations" },
	{ NULL,		"memcpy N",		"start N workers performing memory copies" },
	{ NULL,		"memcpy-ops N",		"stop after N memcpy bogo operations" },
	{ NULL,		"memfd N",		"start N workers allocating memory with memfd_create" },
	{ NULL,		"memfd-bytes N",	"allocate N bytes for each stress iteration" },
	{ NULL,		"memfd-ops N",		"stop after N memfd bogo operations" },
	{ NULL,		"mergesort N",		"start N workers merge sorting 32 bit random integers" },
	{ NULL,		"mergesort-ops N",	"stop after N merge sort bogo operations" },
	{ NULL,		"mergesort-size N",	"number of 32 bit integers to sort" },
	{ NULL,		"mincore N",		"start N workers exercising mincore" },
	{ NULL,		"mincore-ops N",	"stop after N mincore bogo operations" },
	{ NULL,		"mincore-random",	"randomly select pages rather than linear scan" },
	{ NULL,		"mknod N",		"start N workers that exercise mknod" },
	{ NULL,		"mknod-ops N",		"stop after N mknod bogo operations" },
	{ NULL,		"mlock N",		"start N workers exercising mlock/munlock" },
	{ NULL,		"mlock-ops N",		"stop after N mlock bogo operations" },
	{ NULL,		"mmap N",		"start N workers stressing mmap and munmap" },
	{ NULL,		"mmap-ops N",		"stop after N mmap bogo operations" },
	{ NULL,		"mmap-async",		"using asynchronous msyncs for file based mmap" },
	{ NULL,		"mmap-bytes N",		"mmap and munmap N bytes for each stress iteration" },
	{ NULL,		"mmap-file",		"mmap onto a file using synchronous msyncs" },
	{ NULL,		"mmap-mprotect",	"enable mmap mprotect stressing" },
	{ NULL,		"mmapfork N",		"start N workers stressing many forked mmaps/munmaps" },
	{ NULL,		"mmapfork-ops N",	"stop after N mmapfork bogo operations" },
	{ NULL,		"mmapmany N",		"start N workers stressing many mmaps and munmaps" },
	{ NULL,		"mmapmany-ops N",	"stop after N mmapmany bogo operations" },
	{ NULL,		"mremap N",		"start N workers stressing mremap" },
	{ NULL,		"mremap-ops N",		"stop after N mremap bogo operations" },
	{ NULL,		"mremap-bytes N",	"mremap N bytes maximum for each stress iteration" },
	{ NULL,		"msg N",		"start N workers stressing System V messages" },
	{ NULL,		"msg-ops N",		"stop msg workers after N bogo messages" },
	{ NULL,		"msync N",		"start N workers syncing mmap'd data with msync" },
	{ NULL,		"msync-ops N",		"stop msync workers after N bogo msyncs" },
	{ NULL,		"msync-bytes N",	"size of file and memory mapped region to msync" },
	{ NULL,		"mq N",			"start N workers passing messages using POSIX messages" },
	{ NULL,		"mq-ops N",		"stop mq workers after N bogo messages" },
	{ NULL,		"mq-size N",		"specify the size of the POSIX message queue" },
	{ NULL,		"netlink-proc N",	"start N workers exercising netlink process events" },
	{ NULL,		"netlink-proc-ops N",	"stop netlink-proc workers after N bogo events" },
	{ NULL,		"nice N",		"start N workers that randomly re-adjust nice levels" },
	{ NULL,		"nice-ops N",		"stop after N nice bogo operations" },
	{ NULL,		"nop N",		"start N workers that burn cycles with no-ops" },
	{ NULL,		"nop-ops N",		"stop after N nop bogo no-op operations" },
	{ NULL,		"null N",		"start N workers writing to /dev/null" },
	{ NULL,		"null-ops N",		"stop after N /dev/null bogo write operations" },
	{ NULL,		"numa N",		"start N workers stressing NUMA interfaces" },
	{ NULL,		"numa-ops N",		"stop after N NUMA bogo operations" },
	{ NULL,		"oom-pipe N",		"start N workers exercising large pipes" },
	{ NULL,		"oom-pipe-ops N",	"stop after N oom-pipe bogo operations" },
	{ NULL,		"opcode N",		"start N workers exercising random opcodes" },
	{ NULL,		"opcode-ops N",		"stop after N opcode bogo operations" },
	{ "o",		"open N",		"start N workers exercising open/close" },
	{ NULL,		"open-ops N",		"stop after N open/close bogo operations" },
	{ NULL,		"personality N",	"start N workers that change their personality" },
	{ NULL,		"personality-ops N",	"stop after N bogo personality calls" },
	{ "p N",	"pipe N",		"start N workers exercising pipe I/O" },
	{ NULL,		"pipe-ops N",		"stop after N pipe I/O bogo operations" },
	{ NULL,		"pipe-data-size N",	"set pipe size of each pipe write to N bytes" },
#if defined(F_SETPIPE_SZ)
	{ NULL,		"pipe-size N",		"set pipe size to N bytes" },
#endif
	{ "P N",	"poll N",		"start N workers exercising zero timeout polling" },
	{ NULL,		"poll-ops N",		"stop after N poll bogo operations" },
	{ NULL,		"procfs N",		"start N workers reading portions of /proc" },
	{ NULL,		"procfs-ops N",		"stop procfs workers after N bogo read operations" },
	{ NULL,		"pthread N",		"start N workers that create multiple threads" },
	{ NULL,		"pthread-ops N",	"stop pthread workers after N bogo threads created" },
	{ NULL,		"pthread-max P",	"create P threads at a time by each worker" },
	{ NULL,		"ptrace N",		"start N workers that trace a child using ptrace" },
	{ NULL,		"ptrace-ops N",		"stop ptrace workers after N system calls are traced" },
	{ NULL,		"pty N",		"start N workers that exercise pseudoterminals" },
	{ NULL,		"pty-ops N",		"stop pty workers after N pty bogo operations" },
	{ "Q",		"qsort N",		"start N workers qsorting 32 bit random integers" },
	{ NULL,		"qsort-ops N",		"stop after N qsort bogo operations" },
	{ NULL,		"qsort-size N",		"number of 32 bit integers to sort" },
	{ NULL,		"quota N",		"start N workers exercising quotactl commands" },
	{ NULL,		"quota -ops N",		"stop after N quotactl bogo operations" },
	{ NULL,		"rdrand N",		"start N workers exercising rdrand (x86 only)" },
	{ NULL,		"rdrand-ops N",		"stop after N rdrand bogo operations" },
	{ NULL,		"readahead N",		"start N workers exercising file readahead" },
	{ NULL,		"readahead-bytes N",	"size of file to readahead on (default is 1GB)" },
	{ NULL,		"readahead-ops N",	"stop after N readahead bogo operations" },
	{ NULL,		"remap N",		"start N workers exercising page remappings" },
	{ NULL,		"remap-ops N",		"stop after N remapping bogo operations" },
	{ "R",		"rename N",		"start N workers exercising file renames" },
	{ NULL,		"rename-ops N",		"stop after N rename bogo operations" },
	{ NULL,		"resources N",		"start N workers consuming system resources" },
	{ NULL,		"resources-ops N",	"stop after N resource bogo operations" },
	{ NULL,		"rlimit N",		"start N workers that exceed rlimits" },
	{ NULL,		"rlimit-ops N",		"stop after N rlimit bogo operations" },
	{ NULL,		"rmap N",		"start N workers that stress reverse mappings" },
	{ NULL,		"rmap-ops N",		"stop after N rmap bogo operations" },
	{ NULL,		"rtc N",		"start N workers that exercise the RTC interfaces" },
	{ NULL,		"rtc-ops N",		"stop after N RTC bogo operations" },
	{ NULL,		"schedpolicy N",	"start N workers that exercise scheduling policy" },
	{ NULL,		"schedpolicy-ops N",	"stop after N scheduling policy bogo operations" },
	{ NULL,		"sctp N",		"start N workers performing SCTP send/receives " },
	{ NULL,		"sctp-ops N",		"stop after N SCTP bogo operations" },
	{ NULL,		"sctp-domain D",	"specify sctp domain, default is ipv4" },
	{ NULL,		"sctp-port P",		"use SCTP ports P to P + number of workers - 1" },
	{ NULL,		"seal N",		"start N workers performing fcntl SEAL commands" },
	{ NULL,		"seal-ops N",		"stop after N SEAL bogo operations" },
	{ NULL,		"seccomp N",		"start N workers performing seccomp call filtering" },
	{ NULL,		"seccomp-ops N",	"stop after N seccomp bogo operations" },
	{ NULL,		"seek N",		"start N workers performing random seek r/w IO" },
	{ NULL,		"seek-ops N",		"stop after N seek bogo operations" },
	{ NULL,		"seek-punch",		"punch random holes in file to stress extents" },
	{ NULL,		"seek-size N",		"length of file to do random I/O upon" },
	{ NULL,		"sem N",		"start N workers doing semaphore operations" },
	{ NULL,		"sem-ops N",		"stop after N semaphore bogo operations" },
	{ NULL,		"sem-procs N",		"number of processes to start per worker" },
	{ NULL,		"sem-sysv N",		"start N workers doing System V semaphore operations" },
	{ NULL,		"sem-sysv-ops N",	"stop after N System V sem bogo operations" },
	{ NULL,		"sem-sysv-procs N",	"number of processes to start per worker" },
	{ NULL,		"sendfile N",		"start N workers exercising sendfile" },
	{ NULL,		"sendfile-ops N",	"stop after N bogo sendfile operations" },
	{ NULL,		"sendfile-size N",	"size of data to be sent with sendfile" },
	{ NULL,		"shm N",		"start N workers that exercise POSIX shared memory" },
	{ NULL,		"shm-ops N",		"stop after N POSIX shared memory bogo operations" },
	{ NULL,		"shm-bytes N",		"allocate/free N bytes of POSIX shared memory" },
	{ NULL,		"shm-segs N",		"allocate N POSIX shared memory segments per iteration" },
	{ NULL,		"shm-sysv N",		"start N workers that exercise System V shared memory" },
	{ NULL,		"shm-sysv-ops N",	"stop after N shared memory bogo operations" },
	{ NULL,		"shm-sysv-bytes N",	"allocate and free N bytes of shared memory per loop" },
	{ NULL,		"shm-sysv-segs N",	"allocate N shared memory segments per iteration" },
	{ NULL,		"sigfd N",		"start N workers reading signals via signalfd reads " },
	{ NULL,		"sigfd-ops N",		"stop after N bogo signalfd reads" },
	{ NULL,		"sigfpe N",		"start N workers generating floating point math faults" },
	{ NULL,		"sigfpe-ops N",		"stop after N bogo floating point math faults" },
	{ NULL,		"sigpending N",		"start N workers exercising sigpending" },
	{ NULL,		"sigpending-ops N",	"stop after N sigpending bogo operations" },
	{ NULL,		"sigq N",		"start N workers sending sigqueue signals" },
	{ NULL,		"sigq-ops N",		"stop after N siqqueue bogo operations" },
	{ NULL,		"sigsegv N",		"start N workers generating segmentation faults" },
	{ NULL,		"sigsegv-ops N",	"stop after N bogo segmentation faults" },
	{ NULL,		"sigsuspend N",		"start N workers exercising sigsuspend" },
	{ NULL,		"sigsuspend-ops N",	"stop after N bogo sigsuspend wakes" },
	{ NULL,		"sleep N",		"start N workers performing various duration sleeps" },
	{ NULL,		"sleep-ops N",		"stop after N bogo sleep operations" },
	{ NULL,		"sleep-max P",		"create P threads at a time by each worker" },
	{ "S N",	"sock N",		"start N workers exercising socket I/O" },
	{ NULL,		"sock-domain D",	"specify socket domain, default is ipv4" },
	{ NULL,		"sock-nodelay",		"disable Nagle algorithm, send data immediately" },
	{ NULL,		"sock-ops N",		"stop after N socket bogo operations" },
	{ NULL,		"sock-opts option",	"socket options [send|sendmsg|sendmmsg]" },
	{ NULL,		"sock-port P",		"use socket ports P to P + number of workers - 1" },
	{ NULL,		"sock-type T",		"socket type (stream, seqpacket)" },
	{ NULL,		"sockfd N",		"start N workers sending file descriptors over sockets" },
	{ NULL,		"sockfd-ops N",		"stop after N sockfd bogo operations" },
	{ NULL,		"sockfd-port P",	"use socket fd ports P to P + number of workers - 1" },
	{ NULL,		"sockpair N",		"start N workers exercising socket pair I/O activity" },
	{ NULL,		"sockpair-ops N",	"stop after N socket pair bogo operations" },
	{ NULL,		"spawn",		"start N workers spawning stress-ng using posix_spawn" },
	{ NULL,		"spawn-ops N",		"stop after N spawn bogo operations" },
	{ NULL,		"splice N",		"start N workers reading/writing using splice" },
	{ NULL,		"splice-ops N",		"stop after N bogo splice operations" },
	{ NULL,		"splice-bytes N",	"number of bytes to transfer per splice call" },
	{ NULL,		"stack N",		"start N workers generating stack overflows" },
	{ NULL,		"stack-ops N",		"stop after N bogo stack overflows" },
	{ NULL,		"stack-fill",		"fill stack, touches all new pages " },
	{ NULL,		"stackmmap N",		"start N workers exercising a filebacked stack" },
	{ NULL,		"stackmmap-ops N",	"stop after N bogo stackmmap operations" },
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
	{ NULL,		"sync-file N",		"start N workers exercise sync_file_range" },
	{ NULL,		"sync-file-ops N",	"stop after N sync_file_range bogo operations" },
	{ NULL,		"sync-file-bytes N",	"size of file to be sync'd" },
	{ NULL,		"sysinfo N",		"start N workers reading system information" },
	{ NULL,		"sysinfo-ops N",	"stop after sysinfo bogo operations" },
	{ NULL,		"sysfs N",		"start N workers reading files from /sys" },
	{ NULL,		"sysfs-ops N",		"stop after sysfs bogo operations" },
	{ NULL,		"tee N",		"start N workers exercising the tee system call" },
	{ NULL,		"tee-ops N",		"stop after N tee bogo operations" },
	{ "T N",	"timer N",		"start N workers producing timer events" },
	{ NULL,		"timer-ops N",		"stop after N timer bogo events" },
	{ NULL,		"timer-freq F",		"run timer(s) at F Hz, range 1 to 1000000000" },
	{ NULL,		"timer-rand",		"enable random timer frequency" },
	{ NULL,		"timerfd N",		"start N workers producing timerfd events" },
	{ NULL,		"timerfd-ops N",	"stop after N timerfd bogo events" },
	{ NULL,		"timerfd-freq F",	"run timer(s) at F Hz, range 1 to 1000000000" },
	{ NULL,		"timerfd-rand",		"enable random timerfd frequency" },
	{ NULL,		"tlb-shootdown N",	"start N workers that force TLB shootdowns" },
	{ NULL,		"tlb-shootdown-ops N",	"stop after N TLB shootdown bogo ops" },
	{ NULL,		"tmpfs N",		"start N workers mmap'ing a file on tmpfs" },
	{ NULL,		"tmpfs-ops N",		"stop after N tmpfs bogo ops" },
	{ NULL,		"tsc N",		"start N workers reading the TSC (x86 only)" },
	{ NULL,		"tsc-ops N",		"stop after N TSC bogo operations" },
	{ NULL,		"tsearch N",		"start N workers that exercise a tree search" },
	{ NULL,		"tsearch-ops N",	"stop after N tree search bogo operations" },
	{ NULL,		"tsearch-size N",	"number of 32 bit integers to tsearch" },
	{ NULL,		"udp N",		"start N workers performing UDP send/receives " },
	{ NULL,		"udp-ops N",		"stop after N udp bogo operations" },
	{ NULL,		"udp-domain D",		"specify domain, default is ipv4" },
	{ NULL,		"udp-lite",		"use the UDP-Lite (RFC 3828) protocol" },
	{ NULL,		"udp-port P",		"use ports P to P + number of workers - 1" },
	{ NULL,		"udp-flood N",		"start N workers that performs a UDP flood attack" },
	{ NULL,		"udp-flood-ops N",	"stop after N udp flood bogo operations" },
	{ NULL,		"udp-flood-domain D",	"specify domain, default is ipv4" },
	{ NULL,		"unshare N",		"start N workers exercising resource unsharing" },
	{ NULL,		"unshare-ops N",	"stop after N bogo unshare operations" },
	{ "u N",	"urandom N",		"start N workers reading /dev/urandom" },
	{ NULL,		"urandom-ops N",	"stop after N urandom bogo read operations" },
	{ NULL,		"userfaultfd N",	"start N page faulting workers with userspace handling" },
	{ NULL,		"userfaultfd-ops N",	"stop after N page faults have been handled" },
	{ NULL,		"utime N",		"start N workers updating file timestamps" },
	{ NULL,		"utime-ops N",		"stop after N utime bogo operations" },
	{ NULL,		"utime-fsync",		"force utime meta data sync to the file system" },
	{ NULL,		"vecmath N",		"start N workers performing vector math ops" },
	{ NULL,		"vecmath-ops N",	"stop after N vector math bogo operations" },
	{ NULL,		"vfork N",		"start N workers spinning on vfork() and exit()" },
	{ NULL,		"vfork-ops N",		"stop after N vfork bogo operations" },
	{ NULL,		"vfork-max P",		"create P processes per iteration, default is 1" },
	{ NULL,		"vforkmany N",		"start N workers spawning many vfork children" },
	{ NULL,		"vforkmany-ops N",	"stop after spawning N vfork children" },
	{ "m N",	"vm N",			"start N workers spinning on anonymous mmap" },
	{ NULL,		"vm-bytes N",		"allocate N bytes per vm worker (default 256MB)" },
	{ NULL,		"vm-hang N",		"sleep N seconds before freeing memory" },
	{ NULL,		"vm-keep",		"redirty memory instead of reallocating" },
	{ NULL,		"vm-ops N",		"stop after N vm bogo operations" },
#if defined(MAP_LOCKED)
	{ NULL,		"vm-locked",		"lock the pages of the mapped region into memory" },
#endif
	{ NULL,		"vm-method m",		"specify stress vm method m, default is all" },
#if defined(MAP_POPULATE)
	{ NULL,		"vm-populate",		"populate (prefault) page tables for a mapping" },
#endif
	{ NULL,		"vm-rw N",		"start N vm read/write process_vm* copy workers" },
	{ NULL,		"vm-rw-bytes N",	"transfer N bytes of memory per bogo operation" },
	{ NULL,		"vm-rw-ops N",		"stop after N vm process_vm* copy bogo operations" },
	{ NULL,		"vm-splice N",		"start N workers reading/writing using vmsplice" },
	{ NULL,		"vm-splice-ops N",	"stop after N bogo splice operations" },
	{ NULL,		"vm-splice-bytes N",	"number of bytes to transfer per vmsplice call" },
	{ NULL,		"wait N",		"start N workers waiting on child being stop/resumed" },
	{ NULL,		"wait-ops N",		"stop after N bogo wait operations" },
	{ NULL,		"wcs N",		"start N workers on lib C wide char string functions" },
	{ NULL,		"wcs-method func",	"specify the wide character string function to stress" },
	{ NULL,		"wcs-ops N",		"stop after N bogo wide character string operations" },
	{ NULL,		"xattr N",		"start N workers stressing file extended attributes" },
	{ NULL,		"xattr-ops N",		"stop after N bogo xattr operations" },
	{ "y N",	"yield N",		"start N workers doing sched_yield() calls" },
	{ NULL,		"yield-ops N",		"stop after N bogo yield operations" },
	{ NULL,		"zero N",		"start N workers reading /dev/zero" },
	{ NULL,		"zero-ops N",		"stop after N /dev/zero bogo read operations" },
	{ NULL,		"zlib N",		"start N workers compressing data with zlib" },
	{ NULL,		"zlib-ops N",		"stop after N zlib bogo compression operations" },
	{ NULL,		"zlib-method m",	"specify stress zlib random data generation method" },
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

	return i;	/* End of array is a special "NULL" entry */
}

/*
 *  stressor_name_find()
 *  	Find index into stressors by name
 */
static inline int32_t stressor_name_find(const char *name)
{
	int32_t i;
	const char *tmp = munge_underscore(name);
	const size_t len = strlen(tmp) + 1;
	char munged_name[len];

	strncpy(munged_name, tmp, len);

	for (i = 0; stressors[i].name; i++) {
		const char *munged_stressor_name = munge_underscore(stressors[i].name);

		if (!strcmp(munged_stressor_name, munged_name))
			break;
	}

	return i;	/* End of array is a special "NULL" entry */
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

static uint32_t get_class_id(char *const str)
{
	size_t i;

	for (i = 0; classes[i].class; i++) {
		if (!strcmp(classes[i].name, str))
			return classes[i].class;
	}
	return 0;
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
		uint32_t cl = get_class_id(token);
		if (!cl) {
			size_t i;
			size_t len = strlen(token);

			if ((len > 1) && (token[len - 1] == '?')) {
				token[len - 1] = '\0';

				cl = get_class_id(token);
				if (cl) {
					size_t j;

					(void)printf("class '%s' stressors:", token);
					for (j = 0; stressors[j].name; j++) {
						if (stressors[j].class & cl)
							(void)printf(" %s", stressors[j].name);
					}
					(void)printf("\n");
					return 0;
				}
			}
			(void)fprintf(stderr, "Unknown class: '%s', available classes:", token);
			for (i = 0; classes[i].class; i++)
				(void)fprintf(stderr, " %s", classes[i].name);
			(void)fprintf(stderr, "\n\n");

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
			(void)fprintf(stderr, "Unknown stressor: '%s', invalid exclude option\n", token);
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
	g_caught_sigint = true;
	g_keep_stressing_flag = false;
	wait_flag = false;

	(void)kill(-getpid(), SIGALRM);
}

static void MLOCKED stress_sigalrm_child_handler(int dummy)
{
	(void)dummy;
	g_keep_stressing_flag = false;
}

static void MLOCKED stress_sigalrm_parent_handler(int dummy)
{
	(void)dummy;
	wait_flag = false;
}

#if defined(SIGUSR2)
/*
 *  stress_stats_handler()
 *	dump current system stats
 */
static void MLOCKED stress_stats_handler(int dummy)
{
	static char buffer[80];
	char *ptr = buffer;
	int ret;
	double min1, min5, min15;
	size_t shmall, freemem, totalmem;

	(void)dummy;

	*ptr = '\0';

	if (stress_get_load_avg(&min1, &min5, &min15) == 0) {
		ret = snprintf(ptr, sizeof(buffer),
			"Load Avg: %.2f %.2f %.2f, ",
			min1, min5, min15);
		if (ret > 0)
			ptr += ret;
	}
	stress_get_memlimits(&shmall, &freemem, &totalmem);

	(void)snprintf(ptr, buffer - ptr,
		"MemFree: %zu MB, MemTotal: %zu MB",
		freemem / (size_t)MB, totalmem / (size_t)MB);
	/* Really shouldn't do this in a signal handler */
	(void)fprintf(stdout, "%s\n", buffer);
	(void)fflush(stdout);
}
#endif

/*
 *  stress_set_handler()
 *	set signal handler to catch SIGINT, SIGALRM, SIGHUP
 */
static int stress_set_handler(const char *stress, const bool child)
{
	if (stress_sighandler(stress, SIGINT, stress_sigint_handler, NULL) < 0)
		return -1;
	if (stress_sighandler(stress, SIGHUP, stress_sigint_handler, NULL) < 0)
		return -1;
#if defined(SIGUSR2)
	if (!child) {
		if (stress_sighandler(stress, SIGUSR2, stress_stats_handler, NULL) < 0)
			return -1;
	}
#endif
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
	(void)printf("%s, version " VERSION "\n", g_app_name);
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
			(void)snprintf(opt_s, sizeof(opt_s), "-%s,", help_info[i].opt_s);
		(void)printf("%-6s--%-19s%s\n", opt_s,
			help_info[i].opt_l, help_info[i].description);
	}
}

/*
 *  show_stressors()
 *	show stressor names
 */
static inline void show_stressors(void)
{
	size_t i;

	for (i = 0; stressors[i].name; i++)
		(void)printf("%s%s", i ? " " : "",
			munge_underscore(stressors[i].name));
	putchar('\n');
}

/*
 *  usage()
 *	print some help
 */
static void usage(void)
{
	version();
	(void)printf("\nUsage: %s [OPTION [ARG]]\n", g_app_name);
	(void)printf("\nGeneral control options:\n");
	usage_help(help_generic);
	(void)printf("\nStressor specific options:\n");
	usage_help(help_stressors);
	(void)printf("\nExample: %s --cpu 8 --io 4 --vm 2 --vm-bytes 128M --fork 4 --timeout 10s\n\n"
	       "Note: Sizes can be suffixed with B,K,M,G and times with s,m,h,d,y\n", g_app_name);
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
static void stress_get_processors(int32_t *count)
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

	(void)killpg(g_pgrp, sig);

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

	if (g_opt_flags & OPT_FLAGS_IGNITE_CPU)
		ignite_cpu_start();

#if defined(__linux__) && NEED_GLIBC(2,3,0)
	/*
	 *  On systems that support changing CPU affinity
	 *  we keep on moving processed between processors
	 *  to impact on memory locality (e.g. NUMA) to
	 *  try to thrash the system when in aggressive mode
	 */
	if (g_opt_flags & OPT_FLAGS_AGGRESSIVE) {
		cpu_set_t proc_mask;
		unsigned long int cpu = 0;
		const uint32_t ticks_per_sec = stress_get_ticks_per_second() * 5;
		const useconds_t usec_sleep = ticks_per_sec ? 1000000 / ticks_per_sec : 1000000 / 250;

		while (wait_flag) {
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
			(void)shim_usleep(usec_sleep);
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

						pr_dbg("process %d (stress-ng-%s) terminated on signal: %d (%s)\n",
							ret, stressors[i].name, WTERMSIG(status), signame);
#else
						pr_dbg("process %d (stress-ng-%s) terminated on signal: %d\n",
							ret, stressors[i].name, WTERMSIG(status));
#endif
#else
						pr_dbg("process %d (stress-ng-%s) terminated on signal\n",
							ret, stressors[i].name);
#endif
						*success = false;
					}
					switch (WEXITSTATUS(status)) {
					case EXIT_SUCCESS:
						break;
					case EXIT_NO_RESOURCE:
						pr_err("process [%d] (stress-ng-%s) aborted early, out of system resources\n",
							ret, stressors[i].name);
						*resource_success = false;
						break;
					case EXIT_NOT_IMPLEMENTED:
						break;
					default:
						pr_err("process %d (stress-ng-%s) terminated with an error, exit status=%d\n",
							ret, stressors[i].name, WEXITSTATUS(status));
						*success = false;
						break;
					}
					proc_finished(&procs[i].pids[j]);
					pr_dbg("process [%d] terminated\n", ret);
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
	if (g_opt_flags & OPT_FLAGS_IGNITE_CPU)
		ignite_cpu_stop();
}


/*
 *  handle_sigint()
 *	catch SIGINT
 */
static void MLOCKED handle_sigint(int dummy)
{
	(void)dummy;

	g_keep_stressing_flag = false;
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

	wait_flag = true;
	time_start = time_now();
	pr_dbg("starting stressors\n");
	for (n_procs = 0; n_procs < total_procs; n_procs++) {
		for (i = 0; i < STRESS_MAX; i++) {
			if (time_now() - time_start > g_opt_timeout)
				goto abort;

			j = procs[i].started_procs;
			if (j < procs[i].num_procs) {
				int rc = EXIT_SUCCESS;
				pid_t pid;
				char name[64];
again:
				if (!g_keep_stressing_flag)
					break;
				pid = fork();
				switch (pid) {
				case -1:
					if (errno == EAGAIN) {
						(void)shim_usleep(100000);
						goto again;
					}
					pr_err("Cannot fork: errno=%d (%s)\n",
						errno, strerror(errno));
					kill_procs(SIGALRM);
					goto wait_for_procs;
				case 0:
					/* Child */
					(void)setpgid(0, g_pgrp);
					free_procs();
					if (stress_set_handler(name, true) < 0)
						exit(EXIT_FAILURE);
					stress_parent_died_alarm();
					stress_process_dumpable(false);
					if (g_opt_flags & OPT_FLAGS_TIMER_SLACK)
						stress_set_timer_slack();

					(void)alarm(g_opt_timeout);
					mwc_reseed();
					(void)snprintf(name, sizeof(name), "%s-%s", g_app_name,
						munge_underscore(stressors[i].name));
					set_oom_adjustment(name, false);
					set_max_limits();
					set_iopriority(opt_ionice_class, opt_ionice_level);
					set_proc_name(name);

					pr_dbg("%s: started [%d] (instance %" PRIu32 ")\n",
						name, (int)getpid(), j);

					n = (i * max_procs) + j;
					stats[n].start = stats[n].finish = time_now();
#if defined(STRESS_PERF_STATS)
					if (g_opt_flags & OPT_FLAGS_PERF_STATS)
						(void)perf_open(&stats[n].sp);
#endif
					(void)shim_usleep(opt_backoff * n_procs);
#if defined(STRESS_PERF_STATS)
					if (g_opt_flags & OPT_FLAGS_PERF_STATS)
						(void)perf_enable(&stats[n].sp);
#endif
					if (g_keep_stressing_flag && !(g_opt_flags & OPT_FLAGS_DRY_RUN)) {
						const args_t args = {
							&stats[n].counter,
							name,
							procs[i].bogo_ops,
							j,
							getpid(),
							getppid(),
							stress_get_pagesize(),
						};

						rc = stressors[i].stress_func(&args);
						stats[n].run_ok = (rc == EXIT_SUCCESS);
					}
#if defined(STRESS_PERF_STATS)
					if (g_opt_flags & OPT_FLAGS_PERF_STATS) {
						(void)perf_disable(&stats[n].sp);
						(void)perf_close(&stats[n].sp);
					}
#endif
#if defined(STRESS_THERMAL_ZONES)
					if (g_opt_flags & OPT_FLAGS_THERMAL_ZONES)
						(void)tz_get_temperatures(&g_shared->tz_info, &stats[n].tz);
#endif

					stats[n].finish = time_now();
					if (times(&stats[n].tms) == (clock_t)-1) {
						pr_dbg("times failed: errno=%d (%s)\n",
							errno, strerror(errno));
					}
					pr_dbg("%s: exited [%d] (instance %" PRIu32 ")\n",
						name, (int)getpid(), j);
#if defined(STRESS_THERMAL_ZONES)
					tz_free(&g_shared->tz_info);
#endif
					stress_cache_free();
					exit(rc);
				default:
					if (pid > -1) {
						(void)setpgid(pid, g_pgrp);
						procs[i].pids[j] = pid;
						procs[i].started_procs++;
					}

					/* Forced early abort during startup? */
					if (!g_keep_stressing_flag) {
						pr_dbg("abort signal during startup, cleaning up\n");
						kill_procs(SIGALRM);
						goto wait_for_procs;
					}
					break;
				}
			}
		}
	}
	(void)stress_set_handler("stress-ng", false);
	(void)alarm(g_opt_timeout);

abort:
	pr_dbg("%d stressor%s spawned\n", n_procs,
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
			if (g_opt_flags & OPT_FLAGS_SEQUENTIAL) {
				if (opt_class) {
					n = (stressors[i].class & opt_class) ?  g_opt_sequential : 0;
				} else {
					n = g_opt_sequential;
				}
			} else {
				n = procs[i].num_procs;
			}
		}
		if (n) {
			ssize_t buffer_len;

			buffer_len = snprintf(buffer, sizeof(buffer), "%s %" PRId32 " %s",
				previous ? "," : "", n,
				munge_underscore(stressors[i].name));
			previous = true;
			if (buffer_len >= 0) {
				newstr = realloc(str, len + buffer_len + 1);
				if (!newstr) {
					pr_err("Cannot allocate temporary buffer\n");
					free(str);
					return -1;
				}
				str = newstr;
				strncpy(str + len, buffer, buffer_len + 1);
			}
			len += buffer_len;
		}
	}
	pr_inf("dispatching hogs:%s\n", str ? str : "");
	free(str);
	(void)fflush(stdout);

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

	pr_inf("%-13s %9.9s %9.9s %9.9s %9.9s %12s %12s\n",
		"stressor", "bogo ops", "real time", "usr time", "sys time", "bogo ops/s", "bogo ops/s");
	pr_inf("%-13s %9.9s %9.9s %9.9s %9.9s %12s %12s\n",
		"", "", "(secs) ", "(secs) ", "(secs) ", "(real time)", "(usr+sys time)");
	pr_yaml(yaml, "metrics:\n");

	for (i = 0; i < STRESS_MAX; i++) {
		uint64_t c_total = 0, u_total = 0, s_total = 0, us_total;
		double   r_total = 0.0;
		int32_t  j, n = (i * max_procs);
		char *munged = munge_underscore(stressors[i].name);
		double u_time, s_time, bogo_rate_r_time, bogo_rate;
		bool run_ok = false;

		for (j = 0; j < procs[i].started_procs; j++, n++) {
			run_ok  |= g_shared->stats[n].run_ok;
			c_total += g_shared->stats[n].counter;
			u_total += g_shared->stats[n].tms.tms_utime +
				   g_shared->stats[n].tms.tms_cutime;
			s_total += g_shared->stats[n].tms.tms_stime +
				   g_shared->stats[n].tms.tms_cstime;
			r_total += g_shared->stats[n].finish -
				   g_shared->stats[n].start;
		}
		/* Total usr + sys time of all procs */
		us_total = u_total + s_total;
		/* Real time in terms of average wall clock time of all procs */
		r_total = procs[i].started_procs ?
			r_total / (double)procs[i].started_procs : 0.0;

		if ((g_opt_flags & OPT_FLAGS_METRICS_BRIEF) && (c_total == 0) && (!run_ok))
			continue;

		u_time = (ticks_per_sec > 0) ? (double)u_total / (double)ticks_per_sec : 0.0;
		s_time = (ticks_per_sec > 0) ? (double)s_total / (double)ticks_per_sec : 0.0;
		bogo_rate_r_time = (r_total > 0.0) ? (double)c_total / r_total : 0.0;
		bogo_rate = (us_total > 0) ? (double)c_total / ((double)us_total / (double)ticks_per_sec) : 0.0;

		pr_inf("%-13s %9" PRIu64 " %9.2f %9.2f %9.2f %12.2f %12.2f\n",
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
		pr_err("cannot get run time information: errno=%d (%s)\n",
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

	pr_inf("for a %.2fs run time:\n", duration);
	pr_inf("  %8.2fs available CPU time\n",
		total_cpu_time);
	pr_inf("  %8.2fs user time   (%6.2f%%)\n", u_time, u_pc);
	pr_inf("  %8.2fs system time (%6.2f%%)\n", s_time, s_pc);
	pr_inf("  %8.2fs total time  (%6.2f%%)\n", t_time, t_pc);

	if (!rc) {
		pr_inf("load average: %.2f %.2f %.2f\n",
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
static void log_args(int argc, char **argv)
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
static void log_system_info(void)
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

/*
 *  stress_map_shared()
 *	mmap shared region
 */
static inline void stress_map_shared(const size_t len)
{
	g_shared = (shared_t *)mmap(NULL, len, PROT_READ | PROT_WRITE,
		MAP_SHARED | MAP_ANON, -1, 0);
	if (g_shared == MAP_FAILED) {
		pr_err("Cannot mmap to shared memory region: errno=%d (%s)\n",
			errno, strerror(errno));
		free_procs();
		exit(EXIT_FAILURE);
	}
	memset(g_shared, 0, len);
	g_shared->length = len;
}

/*
 *  stress_unmap_shared()
 *	unmap shared region
 */
void stress_unmap_shared(void)
{
	(void)munmap((void *)g_shared, g_shared->length);
}

/*
 *  exclude_unsupported()
 *	tag stressor proc count to be excluded
 */
static inline void exclude_unsupported(void)
{
	size_t i;

	for (i = 0; i < SIZEOF_ARRAY(unsupported); i++) {
		int32_t id = stressor_id_find(unsupported[i].str_id);

		if ((procs[id].num_procs || (g_opt_flags & OPT_FLAGS_SEQUENTIAL)) &&
		    (unsupported[i].func_supported() < 0)) {
			procs[id].num_procs = 0;
			procs[id].exclude = true;
		}
	}
}

/*
 *  set_proc_limits()
 *	set maximum number of processes for specific stressors
 */
static void set_proc_limits(void)
{
#if defined(RLIMIT_NPROC)
	size_t i;

	for (i = 0; i < SIZEOF_ARRAY(proc_limited); i++) {
		int32_t id = stressor_id_find(proc_limited[i].str_id);
		struct rlimit limit;

		if (procs[id].num_procs &&
		    (getrlimit(RLIMIT_NPROC, &limit) == 0)) {
			uint64_t max = (uint64_t)limit.rlim_cur / procs[id].num_procs;

			proc_limited->func_limited(max);
		}
	}
#endif
}

/*
 *  proc_helper()
 *	perform init/destroy on stressor helper funcs
 */
static void proc_helper(const proc_helper_t *helpers, const size_t n)
{
	size_t i;

	for (i = 0; i < n; i++) {
		int32_t id = stressor_id_find(helpers[i].str_id);

		if (procs[id].num_procs || (g_opt_flags & helpers[i].opt_flag))
			helpers[i].func();
	}
}

/*
 *  exclude_pathological()
 *	Disable pathological stressors if user has not explicitly
 *	request them to be used. Let's play safe.
 */
static inline void exclude_pathological(void)
{
	if (!(g_opt_flags & OPT_FLAGS_PATHOLOGICAL)) {
		size_t i;

		for (i = 0; i < STRESS_MAX; i++) {
			if (stressors[i].class & CLASS_PATHOLOGICAL) {
				if (procs[i].num_procs > 0) {
					pr_inf("disabled '%s' as it "
						"may hang the machine "
						"(enable it with the "
						"--pathological option)\n",
						munge_underscore(stressors[i].name));
				}
				procs[i].num_procs = 0;
				procs[i].exclude = true;
			}
		}
	}
}

static inline void set_random_stressors(const int32_t opt_random)
{
	if (g_opt_flags & OPT_FLAGS_RANDOM) {
		int32_t n = opt_random;

		if (g_opt_flags & OPT_FLAGS_SET) {
			(void)fprintf(stderr, "Cannot specify random option with "
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
#if defined(HAVE_LIB_Z)
	(void)stress_set_zlib_method("random");
#endif

	g_pgrp = getpid();

	if (stress_get_processors_configured() < 0) {
		pr_err("sysconf failed, number of cpus configured unknown: errno=%d: (%s)\n",
			errno, strerror(errno));
		exit(EXIT_FAILURE);
	}
	ticks_per_sec = stress_get_ticks_per_second();
	if (ticks_per_sec < 0) {
		pr_err("sysconf failed, clock ticks per second unknown: errno=%d (%s)\n",
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

				g_opt_flags |= OPT_FLAGS_SET;
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
		case OPT_AIO_REQUESTS:
			stress_set_aio_requests(optarg);
			break;
		case OPT_AIO_LINUX_REQUESTS:
			stress_set_aio_linux_requests(optarg);
			break;
		case OPT_ALL:
			g_opt_flags |= (OPT_FLAGS_SET | OPT_FLAGS_ALL);
			opt_all = get_int32(optarg);
			stress_get_processors(&opt_all);
			check_value("all", opt_all);
			break;
		case OPT_AFFINITY_RAND:
			g_opt_flags |= OPT_FLAGS_AFFINITY_RAND;
			break;
		case OPT_AGGRESSIVE:
			g_opt_flags |= OPT_FLAGS_AGGRESSIVE_MASK;
			break;
		case OPT_BACKOFF:
			opt_backoff = get_uint64(optarg);
			break;
		case OPT_BIGHEAP_GROWTH:
			stress_set_bigheap_growth(optarg);
			break;
		case OPT_BRK_NOTOUCH:
			g_opt_flags |= OPT_FLAGS_BRK_NOTOUCH;
			break;
		case OPT_BSEARCH_SIZE:
			stress_set_bsearch_size(optarg);
			break;
		case OPT_CACHE_PREFETCH:
			g_opt_flags |= OPT_FLAGS_CACHE_PREFETCH;
			break;
		case OPT_CACHE_FLUSH:
			g_opt_flags |= OPT_FLAGS_CACHE_FLUSH;
			break;
		case OPT_CACHE_FENCE:
			g_opt_flags |= OPT_FLAGS_CACHE_FENCE;
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
			g_opt_flags |= OPT_FLAGS_CACHE_NOAFF;
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
		case OPT_CLONE_MAX:
			stress_set_clone_max(optarg);
			break;
		case OPT_COPY_FILE_BYTES:
			stress_set_copy_file_bytes(optarg);
			break;
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
			g_opt_flags |= OPT_FLAGS_DRY_RUN;
			break;
		case OPT_DCCP_DOMAIN:
			if (stress_set_dccp_domain(optarg) < 0)
				exit(EXIT_FAILURE);
			break;
		case OPT_DCCP_OPTS:
			if (stress_set_dccp_opts(optarg) < 0)
				exit(EXIT_FAILURE);
			break;
		case OPT_DCCP_PORT:
			stress_set_dccp_port(optarg);
			break;
		case OPT_DENTRIES:
			stress_set_dentries(optarg);
			break;
		case OPT_DENTRY_ORDER:
			if (stress_set_dentry_order(optarg) < 0)
				exit(EXIT_FAILURE);
			break;
		case OPT_EPOLL_DOMAIN:
			if (stress_set_epoll_domain(optarg) < 0)
				exit(EXIT_FAILURE);
			break;
		case OPT_EPOLL_PORT:
			stress_set_epoll_port(optarg);
			break;
		case OPT_EXCLUDE:
			opt_exclude = optarg;
			break;
		case OPT_EXEC_MAX:
			stress_set_exec_max(optarg);
			break;
		case OPT_FALLOCATE_BYTES:
			stress_set_fallocate_bytes(optarg);
			break;
		case OPT_FIEMAP_BYTES:
			stress_set_fiemap_size(optarg);
			break;
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
		case OPT_HEAPSORT_INTEGERS:
			stress_set_heapsort_size(optarg);
			break;
		case OPT_HSEARCH_SIZE:
			stress_set_hsearch_size(optarg);
			break;
		case OPT_IGNITE_CPU:
			g_opt_flags |= OPT_FLAGS_IGNITE_CPU;
			break;
		case OPT_IOMIX_BYTES:
			stress_set_iomix_bytes(optarg);
			break;
		case OPT_IONICE_CLASS:
			opt_ionice_class = get_opt_ionice_class(optarg);
			break;
		case OPT_IONICE_LEVEL:
			opt_ionice_level = get_int32(optarg);
			break;
		case OPT_ITIMER_FREQ:
			stress_set_itimer_freq(optarg);
			break;
		case OPT_KEEP_NAME:
			g_opt_flags |= OPT_FLAGS_KEEP_NAME;
			break;
		case OPT_LEASE_BREAKERS:
			stress_set_lease_breakers(optarg);
			break;
		case OPT_LOCKF_NONBLOCK:
			g_opt_flags |= OPT_FLAGS_LOCKF_NONBLK;
			break;
		case OPT_LOG_BRIEF:
			g_opt_flags |= OPT_FLAGS_LOG_BRIEF;
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
		case OPT_MALLOC_THRESHOLD:
			stress_set_malloc_threshold(optarg);
			break;
		case OPT_MATRIX_METHOD:
			if (stress_set_matrix_method(optarg) < 0)
				exit(EXIT_FAILURE);
			break;
		case OPT_MATRIX_SIZE:
			stress_set_matrix_size(optarg);
			break;
		case OPT_MAXIMIZE:
			g_opt_flags |= OPT_FLAGS_MAXIMIZE;
			break;
		case OPT_MEMFD_BYTES:
			stress_set_memfd_bytes(optarg);
			break;
		case OPT_METRICS:
			g_opt_flags |= OPT_FLAGS_METRICS;
			break;
		case OPT_METRICS_BRIEF:
			g_opt_flags |= (OPT_FLAGS_METRICS_BRIEF | OPT_FLAGS_METRICS);
			break;
		case OPT_MERGESORT_INTEGERS:
			stress_set_mergesort_size(optarg);
			break;
		case OPT_MINCORE_RAND:
			g_opt_flags |= OPT_FLAGS_MINCORE_RAND;
			break;
		case OPT_MINIMIZE:
			g_opt_flags |= OPT_FLAGS_MINIMIZE;
			break;
		case OPT_MMAP_ASYNC:
			g_opt_flags |= (OPT_FLAGS_MMAP_FILE | OPT_FLAGS_MMAP_ASYNC);
			break;
		case OPT_MMAP_BYTES:
			stress_set_mmap_bytes(optarg);
			break;
		case OPT_MMAP_FILE:
			g_opt_flags |= OPT_FLAGS_MMAP_FILE;
			break;
		case OPT_MMAP_MPROTECT:
			g_opt_flags |= OPT_FLAGS_MMAP_MPROTECT;
			break;
		case OPT_MREMAP_BYTES:
			stress_set_mremap_bytes(optarg);
			break;
		case OPT_MSYNC_BYTES:
			stress_set_msync_bytes(optarg);
			break;
		case OPT_MQ_SIZE:
			stress_set_mq_size(optarg);
			break;
		case OPT_NO_MADVISE:
			g_opt_flags &= ~OPT_FLAGS_MMAP_MADVISE;
			break;
		case OPT_NO_RAND_SEED:
			g_opt_flags |= OPT_FLAGS_NO_RAND_SEED;
			break;
		case OPT_PAGE_IN:
			g_opt_flags |= OPT_FLAGS_MMAP_MINCORE;
			break;
		case OPT_PATHOLOGICAL:
			g_opt_flags |= OPT_FLAGS_PATHOLOGICAL;
			break;
#if defined(STRESS_PERF_STATS)
		case OPT_PERF_STATS:
			g_opt_flags |= OPT_FLAGS_PERF_STATS;
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
		case OPT_PTHREAD_MAX:
			stress_set_pthread_max(optarg);
			break;
		case OPT_QSORT_INTEGERS:
			stress_set_qsort_size(optarg);
			break;
		case OPT_QUERY:
			(void)printf("Try '%s --help' for more information.\n", g_app_name);
			exit(EXIT_FAILURE);
			break;
		case OPT_QUIET:
			g_opt_flags &= ~(PR_ALL);
			break;
		case OPT_RANDOM:
			g_opt_flags |= OPT_FLAGS_RANDOM;
			opt_random = get_int32(optarg);
			stress_get_processors(&opt_random);
			check_value("random", opt_random);
			break;
		case OPT_READAHEAD_BYTES:
			stress_set_readahead_bytes(optarg);
			break;
		case OPT_SCHED:
			opt_sched = get_opt_sched(optarg);
			break;
		case OPT_SCHED_PRIO:
			opt_sched_priority = get_int32(optarg);
			break;
		case OPT_SCTP_PORT:
			stress_set_sctp_port(optarg);
			break;
		case OPT_SCTP_DOMAIN:
			if (stress_set_sctp_domain(optarg) < 0)
				exit(EXIT_FAILURE);
			break;
		case OPT_SEEK_PUNCH:
			g_opt_flags |= OPT_FLAGS_SEEK_PUNCH;
			break;
		case OPT_SEEK_SIZE:
			stress_set_seek_size(optarg);
			break;
		case OPT_SEMAPHORE_POSIX_PROCS:
			stress_set_semaphore_posix_procs(optarg);
			break;
		case OPT_SEMAPHORE_SYSV_PROCS:
			stress_set_semaphore_sysv_procs(optarg);
			break;
		case OPT_SENDFILE_SIZE:
			stress_set_sendfile_size(optarg);
			break;
		case OPT_SEQUENTIAL:
			g_opt_flags |= OPT_FLAGS_SEQUENTIAL;
			g_opt_sequential = get_int32(optarg);
			stress_get_processors(&g_opt_sequential);
			check_range("sequential", g_opt_sequential,
				MIN_SEQUENTIAL, MAX_SEQUENTIAL);
			break;
		case OPT_SHM_POSIX_BYTES:
			stress_set_shm_posix_bytes(optarg);
			break;
		case OPT_SHM_POSIX_OBJECTS:
			stress_set_shm_posix_objects(optarg);
			break;
		case OPT_SHM_SYSV_BYTES:
			stress_set_shm_sysv_bytes(optarg);
			break;
		case OPT_SHM_SYSV_SEGMENTS:
			stress_set_shm_sysv_segments(optarg);
			break;
		case OPT_SLEEP_MAX:
			stress_set_sleep_max(optarg);
			break;
		case OPT_SOCKET_DOMAIN:
			if (stress_set_socket_domain(optarg) < 0)
				exit(EXIT_FAILURE);
			break;
		case OPT_SOCKET_NODELAY:
			g_opt_flags |= OPT_FLAGS_SOCKET_NODELAY;
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
		case OPT_SOCKET_FD_PORT:
			stress_set_socket_fd_port(optarg);
			break;
		case OPT_SPLICE_BYTES:
			stress_set_splice_bytes(optarg);
			break;
		case OPT_STACK_FILL:
			g_opt_flags |= OPT_FLAGS_STACK_FILL;
			break;
		case OPT_STR_METHOD:
			if (stress_set_str_method(optarg) < 0)
				exit(EXIT_FAILURE);
			break;
		case OPT_STREAM_L3_SIZE:
			stress_set_stream_L3_size(optarg);
			break;
		case OPT_STRESSORS:
			show_stressors();
			exit(EXIT_SUCCESS);
		case OPT_SYNC_FILE_BYTES:
			stress_set_sync_file_bytes(optarg);
			break;
		case OPT_SYSLOG:
			g_opt_flags |= OPT_FLAGS_SYSLOG;
			break;
		case OPT_TASKSET:
			if (set_cpu_affinity(optarg) < 0)
				exit(EXIT_FAILURE);
			break;
		case OPT_THRASH:
			g_opt_flags |= OPT_FLAGS_THRASH;
			break;
		case OPT_TEMP_PATH:
			if (stress_set_temp_path(optarg) < 0)
				exit(EXIT_FAILURE);
			break;
		case OPT_TIMEOUT:
			g_opt_timeout = get_uint64_time(optarg);
			break;
		case OPT_TIMER_FREQ:
			stress_set_timer_freq(optarg);
			break;
		case OPT_TIMER_RAND:
			g_opt_flags |= OPT_FLAGS_TIMER_RAND;
			break;
		case OPT_TIMERFD_FREQ:
			stress_set_timerfd_freq(optarg);
			break;
		case OPT_TIMERFD_RAND:
			g_opt_flags |= OPT_FLAGS_TIMERFD_RAND;
			break;
		case OPT_TIMER_SLACK:
			g_opt_flags |= OPT_FLAGS_TIMER_SLACK;
			stress_set_timer_slack_ns(optarg);
			break;
		case OPT_TIMES:
			g_opt_flags |= OPT_FLAGS_TIMES;
			break;
		case OPT_TSEARCH_SIZE:
			stress_set_tsearch_size(optarg);
			break;
		case OPT_THERMAL_ZONES:
			g_opt_flags |= OPT_FLAGS_THERMAL_ZONES;
			break;
		case OPT_UDP_DOMAIN:
			if (stress_set_udp_domain(optarg) < 0)
				exit(EXIT_FAILURE);
			break;
		case OPT_UDP_PORT:
			stress_set_udp_port(optarg);
			break;
		case OPT_UDP_LITE:
			g_opt_flags |= OPT_FLAGS_UDP_LITE;
			break;
		case OPT_UDP_FLOOD_DOMAIN:
			if (stress_set_udp_flood_domain(optarg) < 0)
				exit(EXIT_FAILURE);
			break;
		case OPT_USERFAULTFD_BYTES:
			stress_set_userfaultfd_bytes(optarg);
			break;
		case OPT_UTIME_FSYNC:
			g_opt_flags |= OPT_FLAGS_UTIME_FSYNC;
			break;
		case OPT_VERBOSE:
			g_opt_flags |= PR_ALL;
			break;
		case OPT_VFORK_MAX:
			stress_set_vfork_max(optarg);
			break;
		case OPT_VERIFY:
			g_opt_flags |= (OPT_FLAGS_VERIFY | PR_FAIL);
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
			g_opt_flags |= OPT_FLAGS_VM_KEEP;
			break;
		case OPT_VM_METHOD:
			if (stress_set_vm_method(optarg) < 0)
				exit(EXIT_FAILURE);
			break;
#if defined(MAP_LOCKED)
		case OPT_VM_MMAP_LOCKED:
			stress_set_vm_flags(MAP_LOCKED);
			break;
#endif
#if defined(MAP_POPULATE)
		case OPT_VM_MMAP_POPULATE:
			stress_set_vm_flags(MAP_POPULATE);
			break;
#endif
		case OPT_VM_RW_BYTES:
			stress_set_vm_rw_bytes(optarg);
			break;
		case OPT_VM_SPLICE_BYTES:
			stress_set_vm_splice_bytes(optarg);
			break;
		case OPT_WCS_METHOD:
			if (stress_set_wcs_method(optarg) < 0)
				exit(EXIT_FAILURE);
			break;
		case OPT_YAML:
			yamlfile = optarg;
			break;
		case OPT_ZLIB_METHOD:
#if defined(HAVE_LIB_Z)
			if (stress_set_zlib_method(optarg) < 0)
				exit(EXIT_FAILURE);
#endif
			break;
		case OPT_ZOMBIE_MAX:
			stress_set_zombie_max(optarg);
			break;
		default:
			(void)printf("Unknown option (%d)\n",c);
			exit(EXIT_FAILURE);
		}
	}
	if (stress_exclude(opt_exclude) < 0)
		exit(EXIT_FAILURE);
	if ((g_opt_flags & (OPT_FLAGS_SEQUENTIAL | OPT_FLAGS_ALL)) ==
	    (OPT_FLAGS_SEQUENTIAL | OPT_FLAGS_ALL)) {
		(void)fprintf(stderr, "cannot invoke --sequential and --all "
			"options together\n");
		exit(EXIT_FAILURE);
	}
	if (opt_class && !(g_opt_flags & (OPT_FLAGS_SEQUENTIAL | OPT_FLAGS_ALL))) {
		(void)fprintf(stderr, "class option is only used with "
			"--sequential or --all options\n");
		exit(EXIT_FAILURE);
	}
	if (logfile)
		pr_openlog(logfile);
	openlog("stress-ng", 0, LOG_USER);
	log_args(argc, argv);
	log_system_info();
	log_system_mem_info();

	pr_dbg("%" PRId32 " processors online, %" PRId32
		" processors configured\n",
		stress_get_processors_online(),
		stress_get_processors_configured());

	if ((g_opt_flags & OPT_FLAGS_MINMAX_MASK) == OPT_FLAGS_MINMAX_MASK) {
		(void)fprintf(stderr, "maximize and minimize cannot be used together\n");
		exit(EXIT_FAILURE);
	}

	exclude_unsupported();
	exclude_pathological();
	set_random_stressors(opt_random);

#if defined(STRESS_PERF_STATS)
	if (g_opt_flags & OPT_FLAGS_PERF_STATS)
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

	if (g_opt_flags & OPT_FLAGS_SEQUENTIAL) {
		if (total_procs) {
			pr_err("sequential option cannot be specified "
				"with other stressors enabled\n");
			free_procs();
			exit(EXIT_FAILURE);
		}
		if (g_opt_timeout == 0) {
			g_opt_timeout = 60;
			pr_inf("defaulting to a %" PRIu64
				" second run per stressor\n", g_opt_timeout);
		}

		/* Sequential mode has no bogo ops threshold */
		for (i = 0; i < STRESS_MAX; i++) {
			procs[i].bogo_ops = 0;
			procs[i].pids = calloc(g_opt_sequential, sizeof(pid_t));
			if (!procs[i].pids) {
				pr_err("cannot allocate pid list\n");
				free_procs();
				exit(EXIT_FAILURE);
			}
		}
		max_procs = g_opt_sequential;
	} else if (g_opt_flags & OPT_FLAGS_ALL) {
		if (total_procs) {
			pr_err("the all option cannot be specified "
				"with other stressors enabled\n");
			free_procs();
			exit(EXIT_FAILURE);
		}
		if (g_opt_timeout == 0) {
			g_opt_timeout = DEFAULT_TIMEOUT;
			pr_inf("defaulting to a %" PRIu64
				" second run per stressor\n", g_opt_timeout);
		}

		for (i = 0; i < STRESS_MAX; i++) {
			if (!procs[i].exclude)
				procs[i].num_procs = opt_class ?
					((stressors[i].class & opt_class) ?
						opt_all : 0) : opt_all;
			total_procs += procs[i].num_procs;
			if (max_procs < procs[i].num_procs)
				max_procs = procs[i].num_procs;
			if (procs[i].num_procs) {
				procs[i].pids = calloc(procs[i].num_procs, sizeof(pid_t));
				if (!procs[i].pids) {
					pr_err("cannot allocate pid list\n");
					free_procs();
					exit(EXIT_FAILURE);
				}
			}
		}
	} else {
		if (!total_procs) {
			pr_err("No stress workers\n");
			free_procs();
			exit(EXIT_FAILURE);
		}
		if (g_opt_timeout == 0) {
			g_opt_timeout = DEFAULT_TIMEOUT;
			pr_inf("defaulting to a %" PRIu64
				" second run per stressor\n", g_opt_timeout);
		}

		/* Share bogo ops between processes equally, rounding up if nonzero bogo_ops */
		for (i = 0; i < STRESS_MAX; i++) {
			procs[i].bogo_ops = procs[i].num_procs ?
				(procs[i].bogo_ops + (procs[i].num_procs - 1)) / procs[i].num_procs : 0;
			procs[i].pids = NULL;

			if (max_procs < procs[i].num_procs)
				max_procs = procs[i].num_procs;
			if (procs[i].num_procs) {
				procs[i].pids = calloc(procs[i].num_procs, sizeof(pid_t));
				if (!procs[i].pids) {
					pr_err("cannot allocate pid list\n");
					free_procs();
					exit(EXIT_FAILURE);
				}
			}
		}
	}

	set_proc_limits();

	if (show_hogs(opt_class) < 0) {
		free_procs();
		exit(EXIT_FAILURE);
	}
	len = sizeof(shared_t) + (sizeof(proc_stats_t) * STRESS_MAX * max_procs);
	stress_map_shared(len);
#if defined(STRESS_PERF_STATS)
	pthread_spin_init(&g_shared->perf.lock, 0);
#endif
#if defined(HAVE_LIB_PTHREAD)
	pthread_spin_init(&g_shared->warn_once.lock, 0);
#endif

	/*
	 *  Allocate shared cache memory
	 */
	g_shared->mem_cache_level = mem_cache_level;
	g_shared->mem_cache_ways = mem_cache_ways;
	if (stress_cache_alloc("cache allocate") < 0) {
		stress_unmap_shared();
		free_procs();
		exit(EXIT_FAILURE);
	}

#if defined(STRESS_THERMAL_ZONES)
	if (g_opt_flags & OPT_FLAGS_THERMAL_ZONES)
		tz_init(&g_shared->tz_info);
#endif

	proc_helper(proc_init, SIZEOF_ARRAY(proc_init));
	if (g_opt_flags & OPT_FLAGS_THRASH)
		thrash_start();

	if (g_opt_flags & OPT_FLAGS_SEQUENTIAL) {
		/*
		 *  Step through each stressor one by one
		 */
		for (i = 0; g_keep_stressing_flag && i < STRESS_MAX; i++) {
			int32_t j;

			for (j = 0; g_keep_stressing_flag && j < STRESS_MAX; j++)
				procs[j].num_procs = 0;
			if (!procs[i].exclude) {
				procs[i].num_procs = opt_class ?
					((stressors[i].class & opt_class) ?
						g_opt_sequential : 0) : g_opt_sequential;
				if (procs[i].num_procs)
					stress_run(g_opt_sequential,
						g_opt_sequential,
						opt_backoff,
						opt_ionice_class,
						opt_ionice_level,
						g_shared->stats,
						&duration,
						&success,
						&resource_success);
			}
		}
	} else {
		/*
		 *  Run all stressors in parallel
		 */
		stress_run(total_procs, max_procs,
			opt_backoff, opt_ionice_class, opt_ionice_level,
			g_shared->stats, &duration, &success, &resource_success);
	}

	if (g_opt_flags & OPT_FLAGS_THRASH)
		thrash_stop();

	pr_inf("%s run completed in %.2fs%s\n",
		success ? "successful" : "unsuccessful",
		duration, duration_to_str(duration));
	if (yamlfile) {
		yaml = fopen(yamlfile, "w");
		if (!yaml)
			pr_err("Cannot output YAML data to %s\n",
				yamlfile);

		pr_yaml(yaml, "---\n");
		pr_yaml_runinfo(yaml);
	}
	if (g_opt_flags & OPT_FLAGS_METRICS)
		metrics_dump(yaml, max_procs, ticks_per_sec);
#if defined(STRESS_PERF_STATS)
	if (g_opt_flags & OPT_FLAGS_PERF_STATS)
		perf_stat_dump(yaml, stressors, procs, max_procs, duration);
#endif
#if defined(STRESS_THERMAL_ZONES)
	if (g_opt_flags & OPT_FLAGS_THERMAL_ZONES) {
		tz_dump(yaml, stressors, procs, max_procs);
		tz_free(&g_shared->tz_info);
	}
#endif
	if (g_opt_flags & OPT_FLAGS_TIMES)
		times_dump(yaml, ticks_per_sec, duration);
	free_procs();

	proc_helper(proc_destroy, SIZEOF_ARRAY(proc_destroy));
	stress_cache_free();
	stress_unmap_shared();
	closelog();
	pr_closelog();
	if (yaml) {
		pr_yaml(yaml, "...\n");
		(void)fclose(yaml);
	}

	if (!success)
		exit(EXIT_NOT_SUCCESS);
	if (!resource_success)
		exit(EXIT_NO_RESOURCE);
	exit(EXIT_SUCCESS);
}
