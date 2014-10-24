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
#include <sys/wait.h>

#include "stress-ng.h"

/* Various option settings and flags */
const char *app_name = "stress-ng";		/* Name of application */
sem_t	 sem;					/* stress_semaphore sem */
bool	 sem_ok = false;			/* stress_semaphore init ok */
uint8_t  *mem_chunk;				/* Cache load shared memory */
uint64_t opt_dentries = DEFAULT_DENTRIES;	/* dentries per loop */
uint64_t opt_ops[STRESS_MAX];			/* max number of bogo ops */
uint64_t opt_vm_hang = DEFAULT_VM_HANG;		/* VM delay */
uint64_t opt_hdd_bytes = DEFAULT_HDD_BYTES;	/* HDD size in byts */
uint64_t opt_hdd_write_size = DEFAULT_HDD_WRITE_SIZE;
uint64_t opt_timeout = 0;			/* timeout in seconds */
uint64_t mwc_z = MWC_SEED_Z, mwc_w = MWC_SEED_W;/* random number vals */
uint64_t opt_qsort_size = 256 * 1024;		/* Default qsort size */
uint64_t opt_bigheap_growth = 16 * 4096;	/* Amount big heap grows */
uint64_t opt_fork_max = DEFAULT_FORKS;		/* Number of fork stress processes */
uint64_t opt_sequential = DEFAULT_SEQUENTIAL;	/* Number of sequention iterations */
int64_t  opt_backoff = DEFAULT_BACKOFF;		/* child delay */
int32_t  started_procs[STRESS_MAX];		/* number of processes per stressor */
int32_t  opt_flags = PR_ERROR | PR_INFO;	/* option flags */
int32_t  opt_cpu_load = 100;			/* CPU max load */
stress_cpu_stressor_info_t *opt_cpu_stressor;	/* Default stress CPU method */
size_t   opt_vm_bytes = DEFAULT_VM_BYTES;	/* VM bytes */
size_t   opt_vm_stride = DEFAULT_VM_STRIDE;	/* VM stride */
int      opt_vm_flags = 0;			/* VM mmap flags */
size_t   opt_mmap_bytes = DEFAULT_MMAP_BYTES;	/* MMAP size */
pid_t    socket_server, socket_client;		/* pids of socket client/servers */
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

/* Human readable stress test names */
static const stress_t stressors[] = {
#if defined(__linux__)
	{ stress_affinity, STRESS_AFFINITY, OPT_AFFINITY, OPT_AFFINITY_OPS,	"affinity" },
#endif
	{ stress_bigheap,STRESS_BIGHEAP,OPT_BIGHEAP,	OPT_BIGHEAP_OPS,	"bigheap" },
	{ stress_cache,  STRESS_CACHE,	OPT_CACHE,	OPT_CACHE_OPS,  	"cache" },
	{ stress_cpu,	 STRESS_CPU,	OPT_CPU,	OPT_CPU_OPS,		"cpu" },
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
#if !defined(__gnu_hurd__)
	{ stress_msg,	 STRESS_MSG,	OPT_MSG,	OPT_MSG_OPS,		"msg" },
#endif
	{ stress_nice,	 STRESS_NICE,	OPT_NICE,	OPT_NICE_OPS,		"nice" },
	{ stress_open,	 STRESS_OPEN,	OPT_OPEN,  	OPT_OPEN_OPS,		"open" },
	{ stress_pipe,	 STRESS_PIPE,	OPT_PIPE,	OPT_PIPE_OPS,   	"pipe" },
	{ stress_poll,	 STRESS_POLL,	OPT_POLL,	OPT_POLL_OPS,		"poll" },
	{ stress_qsort,	 STRESS_QSORT,	OPT_QSORT,	OPT_QSORT_OPS,		"qsort" },
#if defined(STRESS_X86)
	{ stress_rdrand, STRESS_RDRAND, OPT_RDRAND,	OPT_RDRAND_OPS,		"rdrand" },
#endif
	{ stress_rename, STRESS_RENAME, OPT_RENAME,	OPT_RENAME_OPS, 	"rename" },
	{ stress_semaphore, STRESS_SEMAPHORE, OPT_SEMAPHORE, OPT_SEMAPHORE_OPS, "semaphore" },
#if _POSIX_C_SOURCE >= 199309L && !defined(__gnu_hurd__)
	{ stress_sigq,	 STRESS_SIGQUEUE,OPT_SIGQUEUE, OPT_SIGQUEUE_OPS,	"sigq" },
#endif
	{ stress_sigfpe, STRESS_SIGFPE,	OPT_SIGFPE,	OPT_SIGFPE,		"sigfpe" },
	{ stress_sigsegv,STRESS_SIGSEGV,OPT_SIGSEGV,	OPT_SIGSEGV_OPS,	"sigsegv" },
	{ stress_socket, STRESS_SOCKET, OPT_SOCKET,	OPT_SOCKET_OPS, 	"socket" },
	{ stress_switch, STRESS_SWITCH,	OPT_SWITCH,	OPT_SWITCH_OPS,   	"switch" },
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
	{ "switch",	1,	0,	OPT_SWITCH },
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
	{ "metrics-brief",0,	0,	OPT_METRICS_BRIEF },
	{ "io-ops",	1,	0,	OPT_IOSYNC_OPS },
	{ "vm-ops",	1,	0,	OPT_VM_OPS },
	{ "hdd-ops",	1,	0,	OPT_HDD_OPS },
	{ "fork-ops",	1,	0,	OPT_FORK_OPS },
	{ "fork-max",	1,	0,	OPT_FORK_MAX },
	{ "switch-ops",	1,	0,	OPT_SWITCH_OPS },
	{ "pipe",	1,	0,	OPT_PIPE },
	{ "pipe-ops",	1,	0,	OPT_PIPE_OPS },
	{ "cache",	1,	0, 	OPT_CACHE },
	{ "cache-ops",	1,	0,	OPT_CACHE_OPS },
#if _POSIX_C_SOURCE >= 199309L && !defined(__gnu_hurd__)
	{ "sigq",	1,	0,	OPT_SIGQUEUE },
	{ "sigq-ops",	1,	0,	OPT_SIGQUEUE_OPS },
#endif
	{ "sock",	1,	0,	OPT_SOCKET },
	{ "sock-ops",	1,	0,	OPT_SOCKET_OPS },
	{ "sock-port",	1,	0,	OPT_SOCKET_PORT },
	{ "all",	1,	0,	OPT_ALL },
	{ "sched",	1,	0,	OPT_SCHED },
	{ "sched-prio",	1,	0,	OPT_SCHED_PRIO },
#if defined (__linux__)
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
	{ "bigheap-growth",1,	0,	OPT_BIGHEAP_GROWTH },
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
	{ "verify",	0,	0,	OPT_VERIFY },
#if !defined(__gnu_hurd__)
	{ "msg",	1,	0,	OPT_MSG },
	{ "msg-ops",	1,	0,	OPT_MSG_OPS },
#endif
	{ "nice",	1,	0,	OPT_NICE },
	{ "nice-ops",	1,	0,	OPT_NICE_OPS },
	{ "sigfpe",	1,	0,	OPT_SIGFPE },
	{ "sigfpe-ops",	1,	0,	OPT_SIGFPE_OPS },
#if defined(STRESS_X86)
	{ "rdrand",	1,	0,	OPT_RDRAND },
	{ "rdrand-ops",	1,	0,	OPT_RDRAND_OPS },
#endif
	{ "sequential",	1,	0,	OPT_SEQUENTIAL },
	{ NULL,		0, 	0, 	0 }
};

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
	{ NULL, 	"bigheap-growth N",	"grow heap by N bytes per iteration" },
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
	{ NULL,		"fork-max P",		"create P processes per iteration, default is 1" },
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
	{ NULL,		"msg N",		"start N workers passing messages using System V messages" },
	{ NULL,		"msg-ops N",		"stop msg workers after N bogo messages completed" },
	{ "M",		"metrics",		"print pseudo metrics of activity" },
	{ NULL,		"metrics-brief",	"enable metrics and only show non-zero results" },
	{ NULL,		"nice N",		"start N workers that randomly re-adjust nice levels" },
	{ NULL,		"nice-ops N",		"stop when N nice bogo operations completed" },
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
#if defined(STRESS_X86)
	{ NULL,		"rdrand N",		"start N workers exercising rdrand instruction" },
	{ NULL,		"rdrand-ops N",		"stop when N rdrand bogo operations completed" },
#endif
	{ "R",		"rename N",		"start N workers exercising file renames" },
	{ NULL,		"rename-ops N",		"stop when N rename bogo operations completed" },
	{ NULL,		"sched type",		"set scheduler type" },
	{ NULL,		"sched-prio N",		"set scheduler priority level N" },
	{ NULL,		"sem N",		"start N workers doing semaphore operations" },
	{ NULL,		"sem-ops N",		"stop when N semaphore bogo operations completed" },
	{ NULL,		"sequential N",		"run all stressors one by one, invoking N of them" },
#if _POSIX_C_SOURCE >= 199309L
	{ NULL,		"sigq N",		"start N workers sending sigqueue signals" },
	{ NULL,		"sigq-ops N",		"stop when N siqqueue bogo operations completed" },
#endif
	{ NULL,		"sigfpe N",		"start N workers generating floating point math faults" },
	{ NULL,		"sigfpe-ops N",		"stop when N bogo floating point math faults completed" },
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
	{ NULL,		"verify",		"verify results (not available on all tests)" },
	{ "V",		"version",		"show version" },
#if defined(_POSIX_PRIORITY_SCHEDULING)
	{ "y N",	"yield N",		"start N workers doing sched_yield() calls" },
	{ NULL,		"yield-ops N",		"stop when N bogo yield operations completed" },
#endif
	{ NULL,		NULL,			NULL }
};

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
	printf("\nExample %s --cpu 8 --io 4 --vm 2 --vm-bytes 128M --fork 4 --timeout 10s\n\n"
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
	const int32_t const num_procs[],
	uint64_t counters[],
	double *duration,
	bool *success
)
{
	double time_start, time_finish;
	int32_t n_procs, i, j;

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
					pr_dbg(stderr, "%s: started on pid [%d] (instance %" PRIu32 ")\n",
						name, getpid(), j);

					(void)usleep(opt_backoff * n_procs);
					if (opt_do_run && !(opt_flags & OPT_FLAGS_DRY_RUN))
						rc = stressors[i].stress_func(counters + (i * max_procs) + j, j, opt_ops[i], name);
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

abort:
	pr_dbg(stderr, "%d processes running\n", n_procs);

wait_for_procs:
	/* Wait for children to exit */
	while (n_procs) {
		int pid, status;

		if ((pid = wait(&status)) > 0) {
			if (WEXITSTATUS(status)) {
				pr_err(stderr, "Process %d terminated with an error, exit status=%d\n",
					pid, WEXITSTATUS(status));
				*success = false;
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

	*duration += time_finish - time_start;
}

int main(int argc, char **argv)
{
	uint64_t *counters;
	double duration = 0.0;
	int32_t val, opt_random = 0, i, j;
	int32_t	num_procs[STRESS_MAX];
	int32_t total_procs = 0, max_procs = 0;
	int	fd;
	size_t len;
	bool success = true;
	char shm_name[64];
	struct sigaction new_action;

	memset(num_procs, 0, sizeof(num_procs));
	memset(opt_ops, 0, sizeof(opt_ops));
	mwc_reseed();

	opt_cpu_stressor = stress_cpu_find_by_name("sqrt");
	if ((opt_nprocessors_online = sysconf(_SC_NPROCESSORS_ONLN)) < 0) {
		pr_err(stderr, "sysconf failed, number of cpus online unknown: errno=%d: (%s)\n",
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
			opt_qsort_size = get_uint64(optarg);
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
		case OPT_SEQUENTIAL:
			opt_sequential = get_uint64_byte(optarg);
			if (opt_sequential <= 0)
				opt_sequential = opt_nprocessors_online;
			check_range("sequential", opt_sequential, DEFAULT_SEQUENTIAL_MIN, DEFAULT_SEQUENTIAL_MAX);
			break;
		default:
			printf("Unknown option\n");
			exit(EXIT_FAILURE);
		}
	}

	pr_dbg(stderr, "%ld processors online\n", opt_nprocessors_online);

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

	set_oom_adjustment("main", false);
	set_coredump("main");
	set_sched(opt_sched, opt_sched_priority);
	set_iopriority(opt_ionice_class, opt_ionice_level);
	lock_mem_current();

	new_action.sa_handler = handle_sigint;
	sigemptyset(&new_action.sa_mask);
	new_action.sa_flags = 0;
	if (sigaction(SIGINT, &new_action, NULL) < 0) {
		pr_err(stderr, "stress_ng: sigaction failed: errno=%d (%s)\n",
			errno, strerror(errno));
		exit(EXIT_FAILURE);
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
			num_procs[i] = opt_sequential;
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
			exit(EXIT_FAILURE);
		}
	}

	pr_inf(stdout, "dispatching hogs:");
	for (i = 0; i < STRESS_MAX; i++) {
		fprintf(stdout, " %" PRId32 " %s%c", num_procs[i], stressors[i].name,
			i == STRESS_MAX - 1 ? '\n' : ',');
	}
	fflush(stdout);

	snprintf(shm_name, sizeof(shm_name) - 1, "/tmp/stress_ng_%d", getpid());
	(void)shm_unlink(shm_name);

	if ((fd = shm_open(shm_name, O_RDWR | O_CREAT, 0)) < 0) {
		pr_err(stderr, "Cannot open shared memory region: errno=%d (%s)\n",
			errno, strerror(errno));
		free_procs();
		exit(EXIT_FAILURE);
	}

	len = sizeof(uint64_t) * STRESS_MAX * max_procs;
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
	memset(started_procs, 0, sizeof(num_procs));

	if (opt_sequential) {
		/*
		 *  Step through each stressor one by one
		 */
		memset(num_procs, 0, sizeof(num_procs));
		for (i = 0; opt_do_run && i < STRESS_MAX; i++) {
			opt_ops[i] = 0;
			num_procs[i] = opt_sequential;
			stress_run(opt_sequential, opt_sequential, num_procs, counters, &duration, &success);
			num_procs[i] = 0;
		}
	} else {
		/*
		 *  Run all stressors in parallel
		 */
		stress_run(total_procs, max_procs, num_procs, counters, &duration, &success);
	}

	pr_inf(stdout, "%s run completed in %.2fs\n",
		success ? "successful" : "unsuccessful",
		duration);

	if (opt_flags & OPT_FLAGS_METRICS) {
		for (i = 0; i < STRESS_MAX; i++) {
			uint64_t total = 0;
			double   total_time = 0.0;

			for (j = 0; j < started_procs[i]; j++) {
				total += *(counters + (i * max_procs) + j);
				total_time += procs[i][j].finish - procs[i][j].start;
			}
			if ((opt_flags & OPT_FLAGS_METRICS_BRIEF) && (total == 0))
				continue;
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
