/*
 * Copyright (C) 2013-2018 Canonical, Ltd.
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
#include <syslog.h>

#if defined(HAVE_UNAME)
#include <sys/utsname.h>
#endif
#if defined(__linux__)
#include <sys/sysinfo.h>
#endif

/* Help information for options */
typedef struct {
	const char *opt_s;		/* short option */
	const char *opt_l;		/* long option */
	const char *description;	/* description */
} help_t;

typedef struct {
	const int opt;			/* optarg option */
	const uint64_t opt_flag;	/* global options flag bit setting */
} opt_flag_t;

typedef struct {
	const int opt;				/* optarg option*/
	int (*opt_set_func)(const char *optarg); /* function to set it */
} opt_set_func_t;

/* Per stressor process information */
static proc_info_t *procs_head, *procs_tail;
proc_info_t *proc_current;

/* Various option settings and flags */
static volatile bool wait_flag = true;		/* false = exit run wait loop */
static int terminate_signum;			/* signal sent to process */

/* Globals */
int32_t g_opt_sequential = DEFAULT_SEQUENTIAL;	/* # of sequential stressors */
int32_t g_opt_parallel = DEFAULT_PARALLEL;	/* # of parallel stressors */
uint64_t g_opt_timeout = TIMEOUT_NOT_SET;	/* timeout in seconds */
uint64_t g_opt_flags = PR_ERROR | PR_INFO | OPT_FLAGS_MMAP_MADVISE;
volatile bool g_keep_stressing_flag = true;	/* false to exit stressor */
volatile bool g_caught_sigint = false;		/* true if stopped by SIGINT */
pid_t g_pgrp;					/* process group leader */
const char *g_app_name = "stress-ng";		/* Name of application */
shared_t *g_shared;				/* shared memory */
jmp_buf g_error_env;				/* parsing error env */
put_val_t g_put_val;				/* sync data to somewhere */
bool g_unsupported = false;			/* true if stressors are unsupported */

/*
 *  optarg option to global setting option flags
 */
static const opt_flag_t opt_flags[] = {
	{ OPT_abort,		OPT_FLAGS_ABORT },
	{ OPT_affinity_rand,	OPT_FLAGS_AFFINITY_RAND },
	{ OPT_aggressive,	OPT_FLAGS_AGGRESSIVE_MASK },
	{ OPT_brk_notouch, 	OPT_FLAGS_BRK_NOTOUCH },
	{ OPT_cache_prefetch,	OPT_FLAGS_CACHE_PREFETCH },
	{ OPT_cache_flush,	OPT_FLAGS_CACHE_FLUSH },
	{ OPT_cache_fence,	OPT_FLAGS_CACHE_FENCE },
	{ OPT_cache_no_affinity, OPT_FLAGS_CACHE_NOAFF },
	{ OPT_cpu_online_all,	OPT_FLAGS_CPU_ONLINE_ALL },
	{ OPT_dry_run,		OPT_FLAGS_DRY_RUN },
	{ OPT_ignite_cpu,	OPT_FLAGS_IGNITE_CPU },
	{ OPT_keep_name, 	OPT_FLAGS_KEEP_NAME },
	{ OPT_lockf_nonblock,	OPT_FLAGS_LOCKF_NONBLK },
	{ OPT_log_brief,	OPT_FLAGS_LOG_BRIEF },
	{ OPT_maximize,		OPT_FLAGS_MAXIMIZE },
	{ OPT_metrics,		OPT_FLAGS_METRICS },
	{ OPT_metrics_brief,	OPT_FLAGS_METRICS_BRIEF | OPT_FLAGS_METRICS },
	{ OPT_mincore_rand, 	OPT_FLAGS_MINCORE_RAND },
	{ OPT_minimize,		OPT_FLAGS_MINIMIZE },
	{ OPT_mmap_async,	OPT_FLAGS_MMAP_FILE | OPT_FLAGS_MMAP_ASYNC },
	{ OPT_mmap_file,	OPT_FLAGS_MMAP_FILE },
	{ OPT_mmap_mprotect,	OPT_FLAGS_MMAP_MPROTECT },
	{ OPT_no_rand_seed,	OPT_FLAGS_NO_RAND_SEED },
	{ OPT_oomable,		OPT_FLAGS_OOMABLE },
	{ OPT_page_in,		OPT_FLAGS_MMAP_MINCORE },
	{ OPT_pathological,	OPT_FLAGS_PATHOLOGICAL },
#if defined(STRESS_PERF_STATS)
	{ OPT_perf_stats,	OPT_FLAGS_PERF_STATS },
#endif
	{ OPT_seek_punch,	OPT_FLAGS_SEEK_PUNCH },
	{ OPT_stack_fill,	OPT_FLAGS_STACK_FILL },
	{ OPT_sock_nodelay,	OPT_FLAGS_SOCKET_NODELAY },
	{ OPT_syslog,		OPT_FLAGS_SYSLOG },
	{ OPT_thrash, 		OPT_FLAGS_THRASH },
	{ OPT_timer_rand,	OPT_FLAGS_TIMER_RAND },
	{ OPT_timer_slack,	OPT_FLAGS_TIMER_SLACK },
	{ OPT_timerfd_rand,	OPT_FLAGS_TIMERFD_RAND },
	{ OPT_times,		OPT_FLAGS_TIMES },
	{ OPT_timestamp,	OPT_FLAGS_TIMESTAMP },
	{ OPT_thermal_zones,	OPT_FLAGS_THERMAL_ZONES },
	{ OPT_udp_lite,		OPT_FLAGS_UDP_LITE },
	{ OPT_utime_fsync,	OPT_FLAGS_UTIME_FSYNC },
	{ OPT_verbose,		PR_ALL },
	{ OPT_verify,		OPT_FLAGS_VERIFY | PR_FAIL },
	{ OPT_vm_keep,		OPT_FLAGS_VM_KEEP },
};

/*
 *  optarg option to stressor setting options
 */
static const opt_set_func_t opt_set_funcs[] = {
	{ OPT_aio_requests,		stress_set_aio_requests },
	{ OPT_aiol_requests,		stress_set_aio_linux_requests },
	{ OPT_bigheap_growth,		stress_set_bigheap_growth },
	{ OPT_bsearch_size,		stress_set_bsearch_size },
	{ OPT_chdir_dirs,		stress_set_chdir_dirs },
	{ OPT_clone_max,		stress_set_clone_max },
	{ OPT_copy_file_bytes,		stress_set_copy_file_bytes },
	{ OPT_cpu_load,			stress_set_cpu_load },
	{ OPT_cpu_load_slice,		stress_set_cpu_load_slice },
	{ OPT_cpu_method,		stress_set_cpu_method },
	{ OPT_cyclic_dist,		stress_set_cyclic_dist },
	{ OPT_cyclic_method,		stress_set_cyclic_method },
	{ OPT_cyclic_policy,		stress_set_cyclic_policy },
	{ OPT_cyclic_prio, 		stress_set_cyclic_prio },
	{ OPT_cyclic_sleep,		stress_set_cyclic_sleep },
	{ OPT_dccp_domain,		stress_set_dccp_domain },
	{ OPT_dccp_opts,		stress_set_dccp_opts },
	{ OPT_dccp_port, 		stress_set_dccp_port },
	{ OPT_dentries,			stress_set_dentries },
	{ OPT_dentry_order,		stress_set_dentry_order },
	{ OPT_dir_dirs,			stress_set_dir_dirs },
	{ OPT_dirdeep_dirs, 		stress_set_dirdeep_dirs },
	{ OPT_dirdeep_inodes, 		stress_set_dirdeep_inodes },
	{ OPT_epoll_domain,		stress_set_epoll_domain },
	{ OPT_epoll_port, 		stress_set_epoll_port },
	{ OPT_exec_max,			stress_set_exec_max },
	{ OPT_fallocate_bytes,		stress_set_fallocate_bytes },
	{ OPT_fiemap_bytes,		stress_set_fiemap_bytes },
	{ OPT_fifo_readers,		stress_set_fifo_readers },
	{ OPT_filename_opts,		stress_set_filename_opts },
	{ OPT_fork_max,			stress_set_fork_max },
	{ OPT_fstat_dir,		stress_set_fstat_dir },
	{ OPT_funccall_method,		stress_set_funccall_method },
	{ OPT_hdd_bytes,		stress_set_hdd_bytes },
	{ OPT_hdd_opts,			stress_set_hdd_opts },
	{ OPT_hdd_write_size,		stress_set_hdd_write_size },
	{ OPT_heapsort_integers,	stress_set_heapsort_size },
	{ OPT_hsearch_size,		stress_set_hsearch_size },
	{ OPT_iomix_bytes,		stress_set_iomix_bytes },
	{ OPT_ioport_opts,		stress_set_ioport_opts },
	{ OPT_itimer_freq,		stress_set_itimer_freq },
	{ OPT_lease_breakers,		stress_set_lease_breakers },
	{ OPT_lsearch_size,		stress_set_lsearch_size },
	{ OPT_malloc_bytes,		stress_set_malloc_bytes },
	{ OPT_malloc_max,		stress_set_malloc_max },
	{ OPT_malloc_threshold,		stress_set_malloc_threshold },
	{ OPT_matrix_method,		stress_set_matrix_method },
	{ OPT_matrix_size,		stress_set_matrix_size },
	{ OPT_matrix_yx,		stress_set_matrix_yx },
	{ OPT_memfd_bytes,		stress_set_memfd_bytes },
	{ OPT_memfd_fds,		stress_set_memfd_fds },
	{ OPT_memrate_bytes,		stress_set_memrate_bytes },
	{ OPT_memrate_rd_mbs,		stress_set_memrate_rd_mbs },
	{ OPT_memrate_wr_mbs,		stress_set_memrate_wr_mbs },
	{ OPT_memthrash_method,		stress_set_memthrash_method },
	{ OPT_mergesort_integers,	stress_set_mergesort_size },
	{ OPT_mmap_bytes,		stress_set_mmap_bytes },
	{ OPT_mq_size,			stress_set_mq_size },
	{ OPT_mremap_bytes,		stress_set_mremap_bytes },
	{ OPT_msync_bytes,		stress_set_msync_bytes },
	{ OPT_pipe_data_size,		stress_set_pipe_data_size },
#if defined(F_SETPIPE_SZ)
	{ OPT_pipe_size,		stress_set_pipe_size },
#endif
	{ OPT_pthread_max,		stress_set_pthread_max },
	{ OPT_pty_max,			stress_set_pty_max },
	{ OPT_qsort_integers,		stress_set_qsort_size },
	{ OPT_radixsort_size,		stress_set_radixsort_size },
	{ OPT_rawdev_method,		stress_set_rawdev_method },
	{ OPT_readahead_bytes,		stress_set_readahead_bytes },
	{ OPT_revio_bytes,		stress_set_revio_bytes },
	{ OPT_revio_opts,		stress_set_revio_opts },
	{ OPT_sctp_port,		stress_set_sctp_port },
	{ OPT_seek_size,		stress_set_seek_size },
	{ OPT_sem_procs,		stress_set_semaphore_posix_procs },
	{ OPT_sem_sysv_procs,		stress_set_semaphore_sysv_procs },
	{ OPT_sendfile_size,		stress_set_sendfile_size },
	{ OPT_shm_bytes,		stress_set_shm_posix_bytes },
	{ OPT_shm_objects,		stress_set_shm_posix_objects },
	{ OPT_shm_bytes,		stress_set_shm_sysv_bytes },
	{ OPT_shm_sysv_segments,	stress_set_shm_sysv_segments },
	{ OPT_sleep_max,		stress_set_sleep_max },
	{ OPT_sock_domain,		stress_set_socket_domain },
	{ OPT_sock_opts,		stress_set_socket_opts },
	{ OPT_sock_type,		stress_set_socket_type },
	{ OPT_stream_madvise,		stress_set_stream_madvise },
	{ OPT_str_method,		stress_set_str_method },
	{ OPT_taskset,			stress_set_cpu_affinity },
	{ OPT_temp_path,		stress_set_temp_path },
	{ OPT_timer_freq,		stress_set_timer_freq },
	{ OPT_timer_slack,		stress_set_timer_slack_ns },
	{ OPT_timerfd_freq,		stress_set_timerfd_freq },
	{ OPT_tree_method,		stress_set_tree_method },
	{ OPT_tree_size,		stress_set_tree_size },
	{ OPT_tsearch_size,		stress_set_tsearch_size },
	{ OPT_udp_domain,		stress_set_udp_domain },
	{ OPT_udp_port,			stress_set_udp_port },
	{ OPT_udp_flood_domain,		stress_set_udp_flood_domain },
	{ OPT_userfaultfd_bytes,	stress_set_userfaultfd_bytes },
	{ OPT_vfork_max,		stress_set_vfork_max },
	{ OPT_sctp_domain,		stress_set_sctp_domain },
	{ OPT_sock_port,		stress_set_socket_port },
	{ OPT_sockfd_port,		stress_set_socket_fd_port },
	{ OPT_splice_bytes,		stress_set_splice_bytes },
	{ OPT_stream_index,		stress_set_stream_index },
	{ OPT_stream_l3_size,		stress_set_stream_L3_size },
	{ OPT_sync_file_bytes,		stress_set_sync_file_bytes },
	{ OPT_vm_addr_method,		stress_set_vm_addr_method },
	{ OPT_vm_bytes,			stress_set_vm_bytes },
	{ OPT_vm_hang,			stress_set_vm_hang },
	{ OPT_vm_madvise,		stress_set_vm_madvise },
	{ OPT_vm_method,		stress_set_vm_method },
	{ OPT_vm_rw_bytes,		stress_set_vm_rw_bytes },
	{ OPT_vm_splice_bytes,		stress_set_vm_splice_bytes },
	{ OPT_wcs_method,		stress_set_wcs_method },
#if defined(HAVE_LIB_Z)
	{ OPT_zlib_level,		stress_set_zlib_level },
	{ OPT_zlib_method,		stress_set_zlib_method },
#endif
	{ OPT_zombie_max,		stress_set_zombie_max },
};

/*
 *  Attempt to catch a range of signals so
 *  we can clean up rather than leave
 *  cruft everywhere.
 */
static const int terminate_signals[] = {
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

#define STRESSOR(name)			\
{					\
	&stress_ ## name ## _info,	\
	STRESS_ ## name,		\
	OPT_ ## name,			\
	OPT_ ## name  ## _ops,		\
	# name				\
}

/*
 *  Human readable stress test names.
 */
static const stress_t stressors[] = {
	STRESSOR(access),
	STRESSOR(af_alg),
	STRESSOR(affinity),
	STRESSOR(aio),
	STRESSOR(aiol),
	STRESSOR(apparmor),
	STRESSOR(atomic),
	STRESSOR(bad_altstack),
	STRESSOR(bigheap),
	STRESSOR(bind_mount),
	STRESSOR(branch),
	STRESSOR(brk),
	STRESSOR(bsearch),
	STRESSOR(cache),
	STRESSOR(cap),
	STRESSOR(chdir),
	STRESSOR(chmod),
	STRESSOR(chown),
	STRESSOR(chroot),
	STRESSOR(clock),
	STRESSOR(clone),
	STRESSOR(context),
	STRESSOR(copy_file),
	STRESSOR(cpu),
	STRESSOR(cpu_online),
	STRESSOR(crypt),
	STRESSOR(cyclic),
	STRESSOR(daemon),
	STRESSOR(dccp),
	STRESSOR(dentry),
	STRESSOR(dev),
	STRESSOR(dev_shm),
	STRESSOR(dir),
	STRESSOR(dirdeep),
	STRESSOR(dnotify),
	STRESSOR(dup),
	STRESSOR(dynlib),
	STRESSOR(efivar),
	STRESSOR(enosys),
	STRESSOR(epoll),
	STRESSOR(eventfd),
	STRESSOR(exec),
	STRESSOR(fallocate),
	STRESSOR(fanotify),
	STRESSOR(fault),
	STRESSOR(fcntl),
	STRESSOR(fiemap),
	STRESSOR(fifo),
	STRESSOR(file_ioctl),
	STRESSOR(filename),
	STRESSOR(flock),
	STRESSOR(fork),
	STRESSOR(fp_error),
	STRESSOR(fstat),
	STRESSOR(full),
	STRESSOR(funccall),
	STRESSOR(futex),
	STRESSOR(get),
	STRESSOR(getdent),
	STRESSOR(getrandom),
	STRESSOR(handle),
	STRESSOR(hdd),
	STRESSOR(heapsort),
	STRESSOR(hrtimers),
	STRESSOR(hsearch),
	STRESSOR(icache),
	STRESSOR(icmp_flood),
	STRESSOR(inode_flags),
	STRESSOR(inotify),
	STRESSOR(io),
	STRESSOR(iomix),
	STRESSOR(ioport),
	STRESSOR(ioprio),
	STRESSOR(itimer),
	STRESSOR(kcmp),
	STRESSOR(key),
	STRESSOR(kill),
	STRESSOR(klog),
	STRESSOR(lease),
	STRESSOR(link),
	STRESSOR(locka),
	STRESSOR(lockbus),
	STRESSOR(lockf),
	STRESSOR(lockofd),
	STRESSOR(longjmp),
	STRESSOR(lsearch),
	STRESSOR(madvise),
	STRESSOR(malloc),
	STRESSOR(matrix),
	STRESSOR(mcontend),
	STRESSOR(membarrier),
	STRESSOR(memcpy),
	STRESSOR(memfd),
	STRESSOR(memrate),
	STRESSOR(memthrash),
	STRESSOR(mergesort),
	STRESSOR(mincore),
	STRESSOR(mknod),
	STRESSOR(mlock),
	STRESSOR(mmap),
	STRESSOR(mmapaddr),
	STRESSOR(mmapfixed),
	STRESSOR(mmapfork),
	STRESSOR(mmapmany),
	STRESSOR(mq),
	STRESSOR(mremap),
	STRESSOR(msg),
	STRESSOR(msync),
	STRESSOR(netdev),
	STRESSOR(netlink_proc),
	STRESSOR(nice),
	STRESSOR(nop),
	STRESSOR(null),
	STRESSOR(numa),
	STRESSOR(oom_pipe),
	STRESSOR(opcode),
	STRESSOR(open),
	STRESSOR(personality),
	STRESSOR(physpage),
	STRESSOR(pipe),
	STRESSOR(pkey),
	STRESSOR(poll),
	STRESSOR(prctl),
	STRESSOR(procfs),
	STRESSOR(pthread),
	STRESSOR(ptrace),
	STRESSOR(pty),
	STRESSOR(qsort),
	STRESSOR(quota),
	STRESSOR(radixsort),
	STRESSOR(rawdev),
	STRESSOR(rdrand),
	STRESSOR(readahead),
	STRESSOR(remap),
	STRESSOR(rename),
	STRESSOR(resources),
	STRESSOR(revio),
	STRESSOR(rlimit),
	STRESSOR(rmap),
	STRESSOR(rtc),
	STRESSOR(schedpolicy),
	STRESSOR(sctp),
	STRESSOR(seal),
	STRESSOR(seccomp),
	STRESSOR(seek),
	STRESSOR(sem),
	STRESSOR(sem_sysv),
	STRESSOR(sendfile),
	STRESSOR(set),
	STRESSOR(shm),
	STRESSOR(shm_sysv),
	STRESSOR(sigfd),
	STRESSOR(sigfpe),
	STRESSOR(sigio),
	STRESSOR(sigpending),
	STRESSOR(sigpipe),
	STRESSOR(sigq),
	STRESSOR(sigrt),
	STRESSOR(sigsegv),
	STRESSOR(sigsuspend),
	STRESSOR(sleep),
	STRESSOR(sock),
	STRESSOR(sockdiag),
	STRESSOR(sockfd),
	STRESSOR(sockpair),
	STRESSOR(softlockup),
	STRESSOR(spawn),
	STRESSOR(splice),
	STRESSOR(stack),
	STRESSOR(stackmmap),
	STRESSOR(str),
	STRESSOR(stream),
	STRESSOR(swap),
	STRESSOR(switch),
	STRESSOR(symlink),
	STRESSOR(sync_file),
	STRESSOR(sysbadaddr),
	STRESSOR(sysinfo),
	STRESSOR(sysfs),
	STRESSOR(tee),
	STRESSOR(timer),
	STRESSOR(timerfd),
	STRESSOR(tlb_shootdown),
	STRESSOR(tmpfs),
	STRESSOR(tree),
	STRESSOR(tsc),
	STRESSOR(tsearch),
	STRESSOR(udp),
	STRESSOR(udp_flood),
	STRESSOR(unshare),
	STRESSOR(urandom),
	STRESSOR(userfaultfd),
	STRESSOR(utime),
	STRESSOR(vecmath),
	STRESSOR(vfork),
	STRESSOR(vforkmany),
	STRESSOR(vm),
	STRESSOR(vm_addr),
	STRESSOR(vm_rw),
	STRESSOR(vm_segv),
	STRESSOR(vm_splice),
	STRESSOR(wait),
	STRESSOR(watchdog),
	STRESSOR(wcs),
	STRESSOR(xattr),
	STRESSOR(yield),
	STRESSOR(zero),
	STRESSOR(zlib),
	STRESSOR(zombie),
	{ NULL, STRESS_MAX, 0, 0, NULL }
};

STRESS_ASSERT(SIZEOF_ARRAY(stressors) != STRESS_MAX)

/*
 *  Different stress classes
 */
static const class_info_t classes[] = {
	{ CLASS_CPU_CACHE,	"cpu-cache" },
	{ CLASS_CPU,		"cpu" },
	{ CLASS_DEV,		"device" },
	{ CLASS_FILESYSTEM,	"filesystem" },
	{ CLASS_INTERRUPT,	"interrupt" },
	{ CLASS_IO,		"io" },
	{ CLASS_MEMORY,		"memory" },
	{ CLASS_NETWORK,	"network" },
	{ CLASS_OS,		"os" },
	{ CLASS_PIPE_IO,	"pipe" },
	{ CLASS_SCHEDULER,	"scheduler" },
	{ CLASS_SECURITY,	"security" },
	{ CLASS_VM,		"vm" },
};

/*
 *  command line options
 */
static const struct option long_options[] = {
	{ "abort",	0,	0,	OPT_abort },
	{ "access",	1,	0,	OPT_access },
	{ "access-ops",	1,	0,	OPT_access_ops },
	{ "af-alg",	1,	0,	OPT_af_alg },
	{ "af-alg-ops",	1,	0,	OPT_af_alg_ops },
	{ "af-alg",	1,	0,	OPT_af_alg },
	{ "af-alg-ops",	1,	0,	OPT_af_alg_ops },
	{ "affinity",	1,	0,	OPT_affinity },
	{ "affinity-ops",1,	0,	OPT_affinity_ops },
	{ "affinity-rand",0,	0,	OPT_affinity_rand },
	{ "aggressive",	0,	0,	OPT_aggressive },
	{ "aio",	1,	0,	OPT_aio },
	{ "aio-ops",	1,	0,	OPT_aio_ops },
	{ "aio-requests",1,	0,	OPT_aio_requests },
	{ "aiol",	1,	0,	OPT_aiol},
	{ "aiol-ops",	1,	0,	OPT_aiol_ops },
	{ "aiol-requests",1,	0,	OPT_aiol_requests },
	{ "all",	1,	0,	OPT_all },
	{ "apparmor",	1,	0,	OPT_apparmor },
	{ "apparmor-ops",1,	0,	OPT_apparmor_ops },
	{ "atomic",	1,	0,	OPT_atomic },
	{ "atomic-ops",	1,	0,	OPT_atomic_ops },
	{ "bad-altstack",1,	0,	OPT_bad_altstack },
	{ "bad-altstack-ops",1,	0,	OPT_bad_altstack_ops },
	{ "backoff",	1,	0,	OPT_backoff },
	{ "bigheap",	1,	0,	OPT_bigheap },
	{ "bigheap-ops",1,	0,	OPT_bigheap_ops },
	{ "bigheap-growth",1,	0,	OPT_bigheap_growth },
	{ "bind-mount",	1,	0,	OPT_bind_mount },
	{ "bind-mount-ops",1,	0,	OPT_bind_mount_ops },
	{ "branch",	1,	0,	OPT_branch },
	{ "branch-ops",	1,	0,	OPT_branch_ops },
	{ "brk",	1,	0,	OPT_brk },
	{ "brk-ops",	1,	0,	OPT_brk_ops },
	{ "brk-notouch",0,	0,	OPT_brk_notouch },
	{ "bsearch",	1,	0,	OPT_bsearch },
	{ "bsearch-ops",1,	0,	OPT_bsearch_ops },
	{ "bsearch-size",1,	0,	OPT_bsearch_size },
	{ "cache",	1,	0, 	OPT_cache },
	{ "cache-ops",	1,	0,	OPT_cache_ops },
	{ "cache-prefetch",0,	0,	OPT_cache_prefetch },
	{ "cache-flush",0,	0,	OPT_cache_flush },
	{ "cache-fence",0,	0,	OPT_cache_fence },
	{ "cache-level",1,	0,	OPT_cache_level},
	{ "cache-ways",1,	0,	OPT_cache_ways},
	{ "cache-no-affinity",0,0,	OPT_cache_no_affinity },
	{ "cap",	1,	0, 	OPT_cap },
	{ "cap-ops",	1,	0, 	OPT_cap_ops },
	{ "chdir",	1,	0, 	OPT_chdir },
	{ "chdir-ops",	1,	0, 	OPT_chdir_ops },
	{ "chdir-dirs",	1,	0,	OPT_chdir_dirs },
	{ "chmod",	1,	0, 	OPT_chmod },
	{ "chmod-ops",	1,	0,	OPT_chmod_ops },
	{ "chown",	1,	0, 	OPT_chown},
	{ "chown-ops",	1,	0,	OPT_chown_ops },
	{ "chroot",	1,	0, 	OPT_chroot},
	{ "chroot-ops",	1,	0,	OPT_chroot_ops },
	{ "class",	1,	0,	OPT_class },
	{ "clock",	1,	0,	OPT_clock },
	{ "clock-ops",	1,	0,	OPT_clock_ops },
	{ "clone",	1,	0,	OPT_clone },
	{ "clone-ops",	1,	0,	OPT_clone_ops },
	{ "clone-max",	1,	0,	OPT_clone_max },
	{ "context",	1,	0,	OPT_context },
	{ "context-ops",1,	0,	OPT_context_ops },
	{ "copy-file",	1,	0,	OPT_copy_file },
	{ "copy-file-ops", 1,	0,	OPT_copy_file_ops },
	{ "copy-file-bytes", 1, 0,	OPT_copy_file_bytes },
	{ "cpu",	1,	0,	OPT_cpu },
	{ "cpu-ops",	1,	0,	OPT_cpu_ops },
	{ "cpu-load",	1,	0,	OPT_cpu_load },
	{ "cpu-load-slice",1,	0,	OPT_cpu_load_slice },
	{ "cpu-method",	1,	0,	OPT_cpu_method },
	{ "cpu-online",	1,	0,	OPT_cpu_online },
	{ "cpu-online-ops",1,	0,	OPT_cpu_online_ops },
	{ "cpu-online-all", 0,	0,	OPT_cpu_online_all },
	{ "crypt",	1,	0,	OPT_crypt },
	{ "crypt-ops",	1,	0,	OPT_crypt_ops },
	{ "cyclic",	1,	0,	OPT_cyclic },
	{ "cyclic-dist",1,	0,	OPT_cyclic_dist },
	{ "cyclic-method",1,	0,	OPT_cyclic_method },
	{ "cyclic-ops",1,	0,	OPT_cyclic_ops },
	{ "cyclic-policy",1,	0,	OPT_cyclic_policy },
	{ "cyclic-prio",1,	0,	OPT_cyclic_prio },
	{ "cyclic-sleep",1,	0,	OPT_cyclic_sleep },
	{ "daemon",	1,	0,	OPT_daemon },
	{ "daemon-ops",	1,	0,	OPT_daemon_ops },
	{ "dccp",	1,	0,	OPT_dccp },
	{ "dccp-domain",1,	0,	OPT_dccp_domain },
	{ "dccp-ops",	1,	0,	OPT_dccp_ops },
	{ "dccp-opts",	1,	0,	OPT_dccp_opts },
	{ "dccp-port",	1,	0,	OPT_dccp_port },
	{ "dentry",	1,	0,	OPT_dentry },
	{ "dentry-ops",	1,	0,	OPT_dentry_ops },
	{ "dentries",	1,	0,	OPT_dentries },
	{ "dentry-order",1,	0,	OPT_dentry_order },
	{ "dev",	1,	0,	OPT_dev },
	{ "dev-ops",	1,	0,	OPT_dev_ops },
	{ "dev-shm",	1,	0,	OPT_dev_shm },
	{ "dev-shm-ops",1,	0,	OPT_dev_shm_ops },
	{ "dir",	1,	0,	OPT_dir },
	{ "dir-ops",	1,	0,	OPT_dir_ops },
	{ "dir-dirs",	1,	0,	OPT_dir_dirs },
	{ "dirdeep",	1,	0,	OPT_dirdeep },
	{ "dirdeep-ops",1,	0,	OPT_dirdeep_ops },
	{ "dirdeep-dirs",1,	0,	OPT_dirdeep_dirs },
	{ "dirdeep-inodes",1,	0,	OPT_dirdeep_inodes },
	{ "dry-run",	0,	0,	OPT_dry_run },
	{ "dnotify",	1,	0,	OPT_dnotify },
	{ "dnotify-ops",1,	0,	OPT_dnotify_ops },
	{ "dup",	1,	0,	OPT_dup },
	{ "dup-ops",	1,	0,	OPT_dup_ops },
	{ "dynlib",	1,	0,	OPT_dynlib },
	{ "dyblib-ops",	1,	0,	OPT_dynlib_ops },
	{ "efivar",	1,	0,	OPT_efivar },
	{ "efivar-ops",	1,	0,	OPT_efivar_ops },
	{ "enosys",	1,	0,	OPT_enosys },
	{ "enosys-ops",	1,	0,	OPT_enosys_ops },
	{ "epoll",	1,	0,	OPT_epoll },
	{ "epoll-ops",	1,	0,	OPT_epoll_ops },
	{ "epoll-port",	1,	0,	OPT_epoll_port },
	{ "epoll-domain",1,	0,	OPT_epoll_domain },
	{ "eventfd",	1,	0,	OPT_eventfd },
	{ "eventfd-ops",1,	0,	OPT_eventfd_ops },
	{ "exclude",	1,	0,	OPT_exclude },
	{ "exec",	1,	0,	OPT_exec },
	{ "exec-ops",	1,	0,	OPT_exec_ops },
	{ "exec-max",	1,	0,	OPT_exec_max },
	{ "fallocate",	1,	0,	OPT_fallocate },
	{ "fallocate-ops",1,	0,	OPT_fallocate_ops },
	{ "fallocate-bytes",1,	0,	OPT_fallocate_bytes },
	{ "fault",	1,	0,	OPT_fault },
	{ "fault-ops",	1,	0,	OPT_fault_ops },
	{ "fcntl",	1,	0,	OPT_fcntl},
	{ "fcntl-ops",	1,	0,	OPT_fcntl_ops },
	{ "fiemap",	1,	0,	OPT_fiemap },
	{ "fiemap-ops",	1,	0,	OPT_fiemap_ops },
	{ "fiemap-bytes",1,	0,	OPT_fiemap_bytes },
	{ "fifo",	1,	0,	OPT_fifo },
	{ "fifo-ops",	1,	0,	OPT_fifo_ops },
	{ "fifo-readers",1,	0,	OPT_fifo_readers },
	{ "file-ioctl",	1,	0,	OPT_file_ioctl },
	{ "file-ioctl-ops",1,	0,	OPT_file_ioctl_ops },
	{ "filename",	1,	0,	OPT_filename },
	{ "filename-ops",1,	0,	OPT_filename_ops },
	{ "filename-opts",1,	0,	OPT_filename_opts },
	{ "flock",	1,	0,	OPT_flock },
	{ "flock-ops",	1,	0,	OPT_flock_ops },
	{ "fanotify",	1,	0,	OPT_fanotify },
	{ "fanotify-ops",1,	0,	OPT_fanotify_ops },
	{ "fork",	1,	0,	OPT_fork },
	{ "fork-ops",	1,	0,	OPT_fork_ops },
	{ "fork-max",	1,	0,	OPT_fork_max },
	{ "fp-error",	1,	0,	OPT_fp_error},
	{ "fp-error-ops",1,	0,	OPT_fp_error_ops },
	{ "fstat",	1,	0,	OPT_fstat },
	{ "fstat-ops",	1,	0,	OPT_fstat_ops },
	{ "fstat-dir",	1,	0,	OPT_fstat_dir },
	{ "full",	1,	0,	OPT_full },
	{ "full-ops",	1,	0,	OPT_full_ops },
	{ "funccall",	1,	0,	OPT_funccall },
	{ "funccall-ops",1,	0,	OPT_funccall_ops },
	{ "funccall-method",1,	0,	OPT_funccall_method },
	{ "futex",	1,	0,	OPT_futex },
	{ "futex-ops",	1,	0,	OPT_futex_ops },
	{ "get",	1,	0,	OPT_get },
	{ "get-ops",	1,	0,	OPT_get_ops },
	{ "getrandom",	1,	0,	OPT_getrandom },
	{ "getrandom-ops",1,	0,	OPT_getrandom_ops },
	{ "getdent",	1,	0,	OPT_getdent },
	{ "getdent-ops",1,	0,	OPT_getdent_ops },
	{ "handle",	1,	0,	OPT_handle },
	{ "handle-ops",	1,	0,	OPT_handle_ops },
	{ "hdd",	1,	0,	OPT_hdd },
	{ "hdd-ops",	1,	0,	OPT_hdd_ops },
	{ "hdd-bytes",	1,	0,	OPT_hdd_bytes },
	{ "hdd-write-size", 1,	0,	OPT_hdd_write_size },
	{ "hdd-opts",	1,	0,	OPT_hdd_opts },
	{ "heapsort",	1,	0,	OPT_heapsort },
	{ "heapsort-ops",1,	0,	OPT_heapsort_ops },
	{ "heapsort-size",1,	0,	OPT_heapsort_integers },
	{ "hrtimers",	1,	0,	OPT_hrtimers },
	{ "hrtimers-ops",1,	0,	OPT_hrtimers_ops },
	{ "help",	0,	0,	OPT_help },
	{ "hsearch",	1,	0,	OPT_hsearch },
	{ "hsearch-ops",1,	0,	OPT_hsearch_ops },
	{ "hsearch-size",1,	0,	OPT_hsearch_size },
	{ "icache",	1,	0,	OPT_icache },
	{ "icache-ops",	1,	0,	OPT_icache_ops },
	{ "icmp-flood",	1,	0,	OPT_icmp_flood },
	{ "icmp-flood-ops",1,	0,	OPT_icmp_flood_ops },
	{ "ignite-cpu",	0,	0, 	OPT_ignite_cpu },
	{ "inode-flags",1,	0,	OPT_inode_flags },
	{ "inode-flags-ops",1,	0,	OPT_inode_flags_ops },
	{ "inotify",	1,	0,	OPT_inotify },
	{ "inotify-ops",1,	0,	OPT_inotify_ops },
	{ "io",		1,	0,	OPT_io },
	{ "io-ops",	1,	0,	OPT_io_ops },
	{ "iomix",	1,	0,	OPT_iomix },
	{ "iomix-bytes",1,	0,	OPT_iomix_bytes },
	{ "iomix-ops",	1,	0,	OPT_iomix_ops },
	{ "ionice-class",1,	0,	OPT_ionice_class },
	{ "ionice-level",1,	0,	OPT_ionice_level },
	{ "ioport",	1,	0,	OPT_ioport },
	{ "ioport-ops",	1,	0,	OPT_ioport_ops },
	{ "ioport-opts",1,	0,	OPT_ioport_opts },
	{ "ioprio",	1,	0,	OPT_ioprio },
	{ "ioprio-ops",	1,	0,	OPT_ioprio_ops },
	{ "itimer",	1,	0,	OPT_itimer },
	{ "itimer-ops",	1,	0,	OPT_itimer_ops },
	{ "itimer-freq",1,	0,	OPT_itimer_freq },
	{ "job",	1,	0,	OPT_job },
	{ "kcmp",	1,	0,	OPT_kcmp },
	{ "kcmp-ops",	1,	0,	OPT_kcmp_ops },
	{ "key",	1,	0,	OPT_key },
	{ "key-ops",	1,	0,	OPT_key_ops },
	{ "keep-name",	0,	0,	OPT_keep_name },
	{ "kill",	1,	0,	OPT_kill },
	{ "kill-ops",	1,	0,	OPT_kill_ops },
	{ "klog",	1,	0,	OPT_klog },
	{ "klog-ops",	1,	0,	OPT_klog_ops },
	{ "lease",	1,	0,	OPT_lease },
	{ "lease-ops",	1,	0,	OPT_lease_ops },
	{ "lease-breakers",1,	0,	OPT_lease_breakers },
	{ "link",	1,	0,	OPT_link },
	{ "link-ops",	1,	0,	OPT_link_ops },
	{ "locka",	1,	0,	OPT_locka },
	{ "locka-ops",	1,	0,	OPT_locka_ops },
	{ "lockbus",	1,	0,	OPT_lockbus },
	{ "lockbus-ops",1,	0,	OPT_lockbus_ops },
	{ "lockf",	1,	0,	OPT_lockf },
	{ "lockf-ops",	1,	0,	OPT_lockf_ops },
	{ "lockf-nonblock", 0,	0,	OPT_lockf_nonblock },
	{ "lockofd",	1,	0,	OPT_lockofd },
	{ "lockofd-ops",1,	0,	OPT_lockofd_ops },
	{ "log-brief",	0,	0,	OPT_log_brief },
	{ "log-file",	1,	0,	OPT_log_file },
	{ "longjmp",	1,	0,	OPT_longjmp },
	{ "longjmp-ops",1,	0,	OPT_longjmp_ops },
	{ "lsearch",	1,	0,	OPT_lsearch },
	{ "lsearch-ops",1,	0,	OPT_lsearch_ops },
	{ "lsearch-size",1,	0,	OPT_lsearch_size },
	{ "madvise",	1,	0,	OPT_madvise },
	{ "madvise-ops",1,	0,	OPT_madvise_ops },
	{ "malloc",	1,	0,	OPT_malloc },
	{ "malloc-bytes",1,	0,	OPT_malloc_bytes },
	{ "malloc-max",	1,	0,	OPT_malloc_max },
	{ "malloc-ops",	1,	0,	OPT_malloc_ops },
	{ "malloc-thresh",1,	0,	OPT_malloc_threshold },
	{ "matrix",	1,	0,	OPT_matrix },
	{ "matrix-ops",	1,	0,	OPT_matrix_ops },
	{ "matrix-method",1,	0,	OPT_matrix_method },
	{ "matrix-size",1,	0,	OPT_matrix_size },
	{ "matrix-yx",	0,	0,	OPT_matrix_yx },
	{ "maximize",	0,	0,	OPT_maximize },
	{ "mcontend",	1,	0,	OPT_mcontend },
	{ "mcontend-ops",1,	0,	OPT_mcontend_ops },
	{ "membarrier",	1,	0,	OPT_membarrier },
	{ "membarrier-ops",1,	0,	OPT_membarrier_ops },
	{ "memcpy",	1,	0,	OPT_memcpy },
	{ "memcpy-ops",	1,	0,	OPT_memcpy_ops },
	{ "memfd",	1,	0,	OPT_memfd },
	{ "memfd-ops",	1,	0,	OPT_memfd_ops },
	{ "memfd-bytes",1,	0,	OPT_memfd_bytes },
	{ "memfd-fds",	1,	0,	OPT_memfd_fds },
	{ "memrate",	1,	0,	OPT_memrate },
	{ "memrate-ops",1,	0,	OPT_memrate_ops },
	{ "memrate-rd-mbs",1,	0,	OPT_memrate_rd_mbs },
	{ "memrate-wr-mbs",1,	0,	OPT_memrate_wr_mbs },
	{ "memrate-bytes",1,	0,	OPT_memrate_bytes },
	{ "memthrash",	1,	0,	OPT_memthrash },
	{ "memthrash-ops",1,	0,	OPT_memthrash_ops },
	{ "memthrash-method",1,	0,	OPT_memthrash_method },
	{ "mergesort",	1,	0,	OPT_mergesort },
	{ "mergesort-ops",1,	0,	OPT_mergesort_ops },
	{ "mergesort-size",1,	0,	OPT_mergesort_integers },
	{ "metrics",	0,	0,	OPT_metrics },
	{ "metrics-brief",0,	0,	OPT_metrics_brief },
	{ "mincore",	1,	0,	OPT_mincore },
	{ "mincore-ops",1,	0,	OPT_mincore_ops },
	{ "mincore-random",0,	0,	OPT_mincore_rand },
	{ "minimize",	0,	0,	OPT_minimize },
	{ "mknod",	1,	0,	OPT_mknod },
	{ "mknod-ops",	1,	0,	OPT_mknod_ops },
	{ "mlock",	1,	0,	OPT_mlock },
	{ "mlock-ops",	1,	0,	OPT_mlock_ops },
	{ "mmap",	1,	0,	OPT_mmap },
	{ "mmap-ops",	1,	0,	OPT_mmap_ops },
	{ "mmap-async",	0,	0,	OPT_mmap_async },
	{ "mmap-bytes",	1,	0,	OPT_mmap_bytes },
	{ "mmap-file",	0,	0,	OPT_mmap_file },
	{ "mmap-mprotect",0,	0,	OPT_mmap_mprotect },
	{ "mmapaddr",	1,	0,	OPT_mmapaddr },
	{ "mmapaddr-ops",1,	0,	OPT_mmapaddr_ops },
	{ "mmapfixed",	1,	0,	OPT_mmapfixed},
	{ "mmapfixed-ops",1,	0,	OPT_mmapfixed_ops },
	{ "mmapfork",	1,	0,	OPT_mmapfork },
	{ "mmapfork-ops",1,	0,	OPT_mmapfork_ops },
	{ "mmapmany",	1,	0,	OPT_mmapmany },
	{ "mmapmany-ops",1,	0,	OPT_mmapmany_ops },
	{ "mq",		1,	0,	OPT_mq },
	{ "mq-ops",	1,	0,	OPT_mq_ops },
	{ "mq-size",	1,	0,	OPT_mq_size },
	{ "mremap",	1,	0,	OPT_mremap },
	{ "mremap-ops",	1,	0,	OPT_mremap_ops },
	{ "mremap-bytes",1,	0,	OPT_mremap_bytes },
	{ "msg",	1,	0,	OPT_msg },
	{ "msg-ops",	1,	0,	OPT_msg_ops },
	{ "msync",	1,	0,	OPT_msync },
	{ "msync-ops",	1,	0,	OPT_msync_ops },
	{ "msync-bytes",1,	0,	OPT_msync_bytes },
	{ "netdev",	1,	0,	OPT_netdev },
	{ "netdev-ops",1,	0,	OPT_netdev_ops },
	{ "netlink-proc",1,	0,	OPT_netlink_proc },
	{ "netlink-proc-ops",1,	0,	OPT_netlink_proc_ops },
	{ "nice",	1,	0,	OPT_nice },
	{ "nice-ops",	1,	0,	OPT_nice_ops },
	{ "no-madvise",	0,	0,	OPT_no_madvise },
	{ "no-rand-seed", 0,	0,	OPT_no_rand_seed },
	{ "nop",	1,	0,	OPT_nop },
	{ "nop-ops",	1,	0,	OPT_nop_ops },
	{ "null",	1,	0,	OPT_null },
	{ "null-ops",	1,	0,	OPT_null_ops },
	{ "numa",	1,	0,	OPT_numa },
	{ "numa-ops",	1,	0,	OPT_numa_ops },
	{ "oomable",	0,	0,	OPT_oomable },
	{ "oom-pipe",	1,	0,	OPT_oom_pipe },
	{ "oom-pipe-ops",1,	0,	OPT_oom_pipe_ops },
	{ "opcode",	1,	0,	OPT_opcode },
	{ "opcode-ops",	1,	0,	OPT_opcode_ops },
	{ "open",	1,	0,	OPT_open },
	{ "open-ops",	1,	0,	OPT_open_ops },
	{ "page-in",	0,	0,	OPT_page_in },
	{ "parallel",	1,	0,	OPT_all },
	{ "pathological",0,	0,	OPT_pathological },
#if defined(STRESS_PERF_STATS)
	{ "perf",	0,	0,	OPT_perf_stats },
#endif
	{ "personality",1,	0,	OPT_personality },
	{ "personality-ops",1,	0,	OPT_personality_ops },
	{ "physpage",	1,	0,	OPT_physpage },
	{ "physpage-ops",1,	0,	OPT_physpage_ops },
	{ "pipe",	1,	0,	OPT_pipe },
	{ "pipe-ops",	1,	0,	OPT_pipe_ops },
	{ "pipe-data-size",1,	0,	OPT_pipe_data_size },
#if defined(F_SETPIPE_SZ)
	{ "pipe-size",	1,	0,	OPT_pipe_size },
#endif
	{ "pkey",	1,	0,	OPT_pkey },
	{ "pkey-ops",	1,	0,	OPT_pkey_ops },
	{ "poll",	1,	0,	OPT_poll },
	{ "poll-ops",	1,	0,	OPT_poll_ops },
	{ "prctl",	1,	0,	OPT_prctl },
	{ "prctl-ops",	1,	0,	OPT_prctl_ops },
	{ "procfs",	1,	0,	OPT_procfs },
	{ "procfs-ops",	1,	0,	OPT_procfs_ops },
	{ "pthread",	1,	0,	OPT_pthread },
	{ "pthread-ops",1,	0,	OPT_pthread_ops },
	{ "pthread-max",1,	0,	OPT_pthread_max },
	{ "ptrace",	1,	0,	OPT_ptrace },
	{ "ptrace-ops",1,	0,	OPT_ptrace_ops },
	{ "pty",	1,	0,	OPT_pty },
	{ "pty-ops",	1,	0,	OPT_pty_ops },
	{ "pty-max",	1,	0,	OPT_pty_max },
	{ "qsort",	1,	0,	OPT_qsort },
	{ "qsort-ops",	1,	0,	OPT_qsort_ops },
	{ "qsort-size",	1,	0,	OPT_qsort_integers },
	{ "quiet",	0,	0,	OPT_quiet },
	{ "quota",	1,	0,	OPT_quota },
	{ "quota-ops",	1,	0,	OPT_quota_ops },
	{ "radixsort",	1,	0,	OPT_radixsort },
	{ "radixsort-ops",1,	0,	OPT_radixsort_ops },
	{ "radixsort-size",1,	0,	OPT_radixsort_size },
	{ "rawdev",	1,	0,	OPT_rawdev },
	{ "rawdev-ops",1,	0,	OPT_rawdev_ops },
	{ "rawdev-method",1,	0,	OPT_rawdev_method },
	{ "random",	1,	0,	OPT_random },
	{ "rdrand",	1,	0,	OPT_rdrand },
	{ "rdrand-ops",	1,	0,	OPT_rdrand_ops },
	{ "readahead",	1,	0,	OPT_readahead },
	{ "readahead-ops",1,	0,	OPT_readahead_ops },
	{ "readahead-bytes",1,	0,	OPT_readahead_bytes },
	{ "remap",	1,	0,	OPT_remap },
	{ "remap-ops",	1,	0,	OPT_remap_ops },
	{ "rename",	1,	0,	OPT_rename },
	{ "rename-ops",	1,	0,	OPT_rename_ops },
	{ "resources",	1,	0,	OPT_resources },
	{ "resources-ops",1,	0,	OPT_resources_ops },
	{ "revio",	1,	0,	OPT_revio },
	{ "revio-ops",	1,	0,	OPT_revio_ops },
	{ "revio-opts",	1,	0,	OPT_revio_opts },
	{ "revio-bytes",1,	0,	OPT_revio_bytes },
	{ "rlimit",	1,	0,	OPT_rlimit },
	{ "rlimit-ops",	1,	0,	OPT_rlimit_ops },
	{ "rmap",	1,	0,	OPT_rmap },
	{ "rmap-ops",	1,	0,	OPT_rmap_ops },
	{ "rtc",	1,	0,	OPT_rtc },
	{ "rtc-ops",	1,	0,	OPT_rtc_ops },
	{ "sched",	1,	0,	OPT_sched },
	{ "sched-prio",	1,	0,	OPT_sched_prio },
	{ "schedpolicy",1,	0,	OPT_schedpolicy },
	{ "schedpolicy-ops",1,	0,	OPT_schedpolicy_ops },
	{ "sctp",	1,	0,	OPT_sctp },
	{ "sctp-ops",	1,	0,	OPT_sctp_ops },
	{ "sctp-domain",1,	0,	OPT_sctp_domain },
	{ "sctp-port",	1,	0,	OPT_sctp_port },
	{ "seal",	1,	0,	OPT_seal },
	{ "seal-ops",	1,	0,	OPT_seal_ops },
	{ "seccomp",	1,	0,	OPT_seccomp },
	{ "seccomp-ops",1,	0,	OPT_seccomp_ops },
	{ "seek",	1,	0,	OPT_seek },
	{ "seek-ops",	1,	0,	OPT_seek_ops },
	{ "seek-punch",	0,	0,	OPT_seek_punch  },
	{ "seek-size",	1,	0,	OPT_seek_size },
	{ "sem",	1,	0,	OPT_sem },
	{ "sem-ops",	1,	0,	OPT_sem_ops },
	{ "sem-procs",	1,	0,	OPT_sem_procs },
	{ "sem-sysv",	1,	0,	OPT_sem_sysv },
	{ "sem-sysv-ops",1,	0,	OPT_sem_sysv_ops },
	{ "sem-sysv-procs",1,	0,	OPT_sem_sysv_procs },
	{ "sendfile",	1,	0,	OPT_sendfile },
	{ "sendfile-ops",1,	0,	OPT_sendfile_ops },
	{ "sendfile-size",1,	0,	OPT_sendfile_size },
	{ "sequential",	1,	0,	OPT_sequential },
	{ "set",	1,	0,	OPT_set },
	{ "set-ops",	1,	0,	OPT_set_ops },
	{ "shm",	1,	0,	OPT_shm },
	{ "shm-ops",	1,	0,	OPT_shm_ops },
	{ "shm-bytes",	1,	0,	OPT_shm_bytes },
	{ "shm-objs",	1,	0,	OPT_shm_objects },
	{ "shm-sysv",	1,	0,	OPT_shm_sysv },
	{ "shm-sysv-ops",1,	0,	OPT_shm_sysv_ops },
	{ "shm-sysv-bytes",1,	0,	OPT_shm_sysv_bytes },
	{ "shm-sysv-segs",1,	0,	OPT_shm_sysv_segments },
	{ "sigfd",	1,	0,	OPT_sigfd },
	{ "sigfd-ops",	1,	0,	OPT_sigfd_ops },
	{ "sigio",	1,	0,	OPT_sigio },
	{ "sigio-ops",	1,	0,	OPT_sigio_ops },
	{ "sigfpe",	1,	0,	OPT_sigfpe },
	{ "sigfpe-ops",	1,	0,	OPT_sigfpe_ops },
	{ "sigpending",	1,	0,	OPT_sigpending},
	{ "sigpending-ops",1,	0,	OPT_sigpending_ops },
	{ "sigpipe",	1,	0,	OPT_sigpipe },
	{ "sigpipe-ops",1,	0,	OPT_sigpipe_ops },
	{ "sigq",	1,	0,	OPT_sigq },
	{ "sigq-ops",	1,	0,	OPT_sigq_ops },
	{ "sigrt",	1,	0,	OPT_sigrt },
	{ "sigrt-ops",	1,	0,	OPT_sigrt_ops },
	{ "sigsegv",	1,	0,	OPT_sigsegv },
	{ "sigsegv-ops",1,	0,	OPT_sigsegv_ops },
	{ "sigsuspend",	1,	0,	OPT_sigsuspend},
	{ "sigsuspend-ops",1,	0,	OPT_sigsuspend_ops},
	{ "sleep",	1,	0,	OPT_sleep },
	{ "sleep-ops",	1,	0,	OPT_sleep_ops },
	{ "sleep-max",	1,	0,	OPT_sleep_max },
	{ "sock",	1,	0,	OPT_sock },
	{ "sock-domain",1,	0,	OPT_sock_domain },
	{ "sock-nodelay",0,	0,	OPT_sock_nodelay },
	{ "sock-ops",	1,	0,	OPT_sock_ops },
	{ "sock-opts",	1,	0,	OPT_sock_opts },
	{ "sock-port",	1,	0,	OPT_sock_port },
	{ "sock-type",	1,	0,	OPT_sock_type },
	{ "sockdiag",	1,	0,	OPT_sockdiag },
	{ "sockdiag-ops",1,	0,	OPT_sockdiag_ops },
	{ "sockfd",	1,	0,	OPT_sockfd },
	{ "sockfd-ops",1,	0,	OPT_sockfd_ops },
	{ "sockfd-port",1,	0,	OPT_sockfd_port },
	{ "sockpair",	1,	0,	OPT_sockpair },
	{ "sockpair-ops",1,	0,	OPT_sockpair_ops },
	{ "softlockup",	1,	0,	OPT_softlockup },
	{ "softlockup-ops",1,	0,	OPT_softlockup_ops },
	{ "spawn",	1,	0,	OPT_spawn },
	{ "spawn-ops",	1,	0,	OPT_spawn_ops },
	{ "splice",	1,	0,	OPT_splice },
	{ "splice-bytes",1,	0,	OPT_splice_bytes },
	{ "splice-ops",	1,	0,	OPT_splice_ops },
	{ "stack",	1,	0,	OPT_stack},
	{ "stack-fill",	0,	0,	OPT_stack_fill },
	{ "stack-ops",	1,	0,	OPT_stack_ops },
	{ "stackmmap",	1,	0,	OPT_stackmmap },
	{ "stackmmap-ops",1,	0,	OPT_stackmmap_ops },
	{ "str",	1,	0,	OPT_str },
	{ "str-ops",	1,	0,	OPT_str_ops },
	{ "str-method",	1,	0,	OPT_str_method },
	{ "stressors",	0,	0,	OPT_stressors },
	{ "stream",	1,	0,	OPT_stream },
	{ "stream-ops",	1,	0,	OPT_stream_ops },
	{ "stream-index",1,	0,	OPT_stream_index },
	{ "stream-l3-size",1,	0,	OPT_stream_l3_size },
	{ "stream-madvise",1,	0,	OPT_stream_madvise },
	{ "swap",	1,	0,	OPT_swap },
	{ "swap-ops",	1,	0,	OPT_swap_ops },
	{ "switch",	1,	0,	OPT_switch },
	{ "switch-ops",	1,	0,	OPT_switch_ops },
	{ "symlink",	1,	0,	OPT_symlink },
	{ "symlink-ops",1,	0,	OPT_symlink_ops },
	{ "sync-file",	1,	0,	OPT_sync_file },
	{ "sync-file-ops", 1,	0,	OPT_sync_file_ops },
	{ "sync-file-bytes", 1,	0,	OPT_sync_file_bytes },
	{ "sysbadaddr",	1,	0,	OPT_sysbadaddr },
	{ "sysbadaddr-ops",1,	0,	OPT_sysbadaddr_ops },
	{ "sysfs",	1,	0,	OPT_sysfs },
	{ "sysfs-ops",1,	0,	OPT_sysfs_ops },
	{ "sysinfo",	1,	0,	OPT_sysinfo },
	{ "sysinfo-ops",1,	0,	OPT_sysinfo_ops },
	{ "syslog",	0,	0,	OPT_syslog },
	{ "taskset",	1,	0,	OPT_taskset },
	{ "tee",	1,	0,	OPT_tee },
	{ "tee-ops",	1,	0,	OPT_tee_ops },
	{ "temp-path",	1,	0,	OPT_temp_path },
	{ "timeout",	1,	0,	OPT_timeout },
	{ "timer",	1,	0,	OPT_timer },
	{ "timer-ops",	1,	0,	OPT_timer_ops },
	{ "timer-freq",	1,	0,	OPT_timer_freq },
	{ "timer-rand", 0,	0,	OPT_timer_rand },
	{ "timerfd",	1,	0,	OPT_timerfd },
	{ "timerfd-ops",1,	0,	OPT_timerfd_ops },
	{ "timerfd-freq",1,	0,	OPT_timerfd_freq },
	{ "timerfd-rand",0,	0,	OPT_timerfd_rand },
	{ "timer-slack",1,	0,	OPT_timer_slack },
	{ "tlb-shootdown",1,	0,	OPT_tlb_shootdown },
	{ "tlb-shootdown-ops",1,0,	OPT_tlb_shootdown_ops },
	{ "tmpfs",	1,	0,	OPT_tmpfs },
	{ "tmpfs-ops",	1,	0,	OPT_tmpfs_ops },
	{ "tree",	1,	0,	OPT_tree },
	{ "tree-ops",	1,	0,	OPT_tree_ops },
	{ "tree-method",1,	0,	OPT_tree_method },
	{ "tree-size",	1,	0,	OPT_tree_size },
	{ "tsc",	1,	0,	OPT_tsc },
	{ "tsc-ops",	1,	0,	OPT_tsc_ops },
	{ "tsearch",	1,	0,	OPT_tsearch },
	{ "tsearch-ops",1,	0,	OPT_tsearch_ops },
	{ "tsearch-size",1,	0,	OPT_tsearch_size },
	{ "thrash",	0,	0,	OPT_thrash },
	{ "times",	0,	0,	OPT_times },
	{ "timestamp",	0,	0,	OPT_timestamp },
	{ "tz",		0,	0,	OPT_thermal_zones },
	{ "udp",	1,	0,	OPT_udp },
	{ "udp-ops",	1,	0,	OPT_udp_ops },
	{ "udp-domain",1,	0,	OPT_udp_domain },
	{ "udp-lite",	0,	0,	OPT_udp_lite },
	{ "udp-port",	1,	0,	OPT_udp_port },
	{ "udp-flood",	1,	0,	OPT_udp_flood },
	{ "udp-flood-domain",1,	0,	OPT_udp_flood_domain },
	{ "udp-flood-ops",1,	0,	OPT_udp_flood_ops },
	{ "userfaultfd",1,	0,	OPT_userfaultfd },
	{ "userfaultfd-ops",1,	0,	OPT_userfaultfd_ops },
	{ "userfaultfd-bytes",1,0,	OPT_userfaultfd_bytes },
	{ "utime",	1,	0,	OPT_utime },
	{ "utime-ops",	1,	0,	OPT_utime_ops },
	{ "utime-fsync",0,	0,	OPT_utime_fsync },
	{ "unshare",	1,	0,	OPT_unshare },
	{ "unshare-ops",1,	0,	OPT_unshare_ops },
	{ "urandom",	1,	0,	OPT_urandom },
	{ "urandom-ops",1,	0,	OPT_urandom_ops },
	{ "vecmath",	1,	0,	OPT_vecmath },
	{ "vecmath-ops",1,	0,	OPT_vecmath_ops },
	{ "verbose",	0,	0,	OPT_verbose },
	{ "verify",	0,	0,	OPT_verify },
	{ "version",	0,	0,	OPT_version },
	{ "vfork",	1,	0,	OPT_vfork },
	{ "vfork-ops",	1,	0,	OPT_vfork_ops },
	{ "vfork-max",	1,	0,	OPT_vfork_max },
	{ "vforkmany",	1,	0,	OPT_vforkmany },
	{ "vforkmany-ops", 1,	0,	OPT_vforkmany_ops },
	{ "vm",		1,	0,	OPT_vm },
	{ "vm-bytes",	1,	0,	OPT_vm_bytes },
	{ "vm-hang",	1,	0,	OPT_vm_hang },
	{ "vm-keep",	0,	0,	OPT_vm_keep },
#if defined(MAP_POPULATE)
	{ "vm-populate",0,	0,	OPT_vm_mmap_populate },
#endif
#if defined(MAP_LOCKED)
	{ "vm-locked",	0,	0,	OPT_vm_mmap_locked },
#endif
	{ "vm-ops",	1,	0,	OPT_vm_ops },
	{ "vm-madvise",	1,	0,	OPT_vm_madvise },
	{ "vm-method",	1,	0,	OPT_vm_method },
	{ "vm-addr",	1,	0,	OPT_vm_addr },
	{ "vm-addr-ops",1,	0,	OPT_vm_addr_ops },
	{ "vm-addr-method",1,	0,	OPT_vm_addr_method },
	{ "vm-rw",	1,	0,	OPT_vm_rw },
	{ "vm-rw-bytes",1,	0,	OPT_vm_rw_bytes },
	{ "vm-rw-ops",	1,	0,	OPT_vm_rw_ops },
	{ "vm-segv",	1,	0,	OPT_vm_segv },
	{ "vm-segv-ops",1,	0,	OPT_vm_segv_ops },
	{ "vm-splice",	1,	0,	OPT_vm_splice },
	{ "vm-splice-bytes",1,	0,	OPT_vm_splice_bytes },
	{ "vm-splice-ops",1,	0,	OPT_vm_splice_ops },
	{ "wait",	1,	0,	OPT_wait },
	{ "wait-ops",	1,	0,	OPT_wait_ops },
	{ "watchdog",	1,	0,	OPT_watchdog },
	{ "watchdog-ops",1,	0,	OPT_watchdog_ops },
	{ "wcs",	1,	0,	OPT_wcs},
	{ "wcs-ops",	1,	0,	OPT_wcs_ops },
	{ "wcs-method",	1,	0,	OPT_wcs_method },
	{ "xattr",	1,	0,	OPT_xattr },
	{ "xattr-ops",	1,	0,	OPT_xattr_ops },
	{ "yaml",	1,	0,	OPT_yaml },
	{ "yield",	1,	0,	OPT_yield },
	{ "yield-ops",	1,	0,	OPT_yield_ops },
	{ "zero",	1,	0,	OPT_zero },
	{ "zero-ops",	1,	0,	OPT_zero_ops },
	{ "zlib",	1,	0,	OPT_zlib },
	{ "zlib-ops",	1,	0,	OPT_zlib_ops },
	{ "zlib-method",1,	0,	OPT_zlib_method },
	{ "zlib-level",	1,	0,	OPT_zlib_level },
	{ "zombie",	1,	0,	OPT_zombie },
	{ "zombie-ops",	1,	0,	OPT_zombie_ops },
	{ "zombie-max",	1,	0,	OPT_zombie_max },
	{ NULL,		0,	0,	0 }
};

/*
 *  Generic help options
 */
static const help_t help_generic[] = {
	{ NULL,		"abort",		"abort all stressors if any stressor fails" },
	{ NULL,		"aggressive",		"enable all aggressive options" },
	{ "a N",	"all N",		"start N workers of each stress test" },
	{ "b N",	"backoff N",		"wait of N microseconds before work starts" },
	{ NULL,		"class name",		"specify a class of stressors, use with --sequential" },
	{ "n",		"dry-run",		"do not run" },
	{ "h",		"help",			"show help" },
	{ NULL,		"ignite-cpu",		"alter kernel controls to make CPU run hot" },
	{ NULL,		"ionice-class C",	"specify ionice class (idle, besteffort, realtime)" },
	{ NULL,		"ionice-level L",	"specify ionice level (0 max, 7 min)" },
	{ "j",		"job jobfile",		"run the named jobfile" },
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
	{ NULL,		"parallel N",		"synonym for 'all N'" },
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
	{ "t N",	"timeout T",		"timeout after T seconds" },
	{ NULL,		"timer-slack",		"enable timer slack mode" },
	{ NULL,		"times",		"show run time summary at end of the run" },
	{ NULL,		"timestamp",		"timestamp log output " },
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
	{ NULL,		"access N",		"start N workers that stress file access permissions" },
	{ NULL,		"access-ops N",		"stop after N file access bogo operations" },
	{ NULL,		"af-alg N",		"start N workers that stress AF_ALG socket domain" },
	{ NULL,		"af-alg-ops N",		"stop after N af-alg bogo operations" },
	{ NULL,		"affinity N",		"start N workers that rapidly change CPU affinity" },
	{ NULL, 	"affinity-ops N",   	"stop after N affinity bogo operations" },
	{ NULL, 	"affinity-rand",   	"change affinity randomly rather than sequentially" },
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
	{ NULL,		"bad-altstack N",	"start N workers exercising bad signal stacks" },
	{ NULL,		"bad-altstack-ops N",	"stop after N bogo signal stack SIGSEGVs" },
	{ "B N",	"bigheap N",		"start N workers that grow the heap using calloc()" },
	{ NULL,		"bigheap-ops N",	"stop after N bogo bigheap operations" },
	{ NULL, 	"bigheap-growth N",	"grow heap by N bytes per iteration" },
	{ NULL,		"bind-mount N",		"start N workers exercising bind mounts" },
	{ NULL,		"bind-mount-ops N",	"stop after N bogo bind mount operations" },
	{ NULL,		"branch N",		"start N workers that force branch misprediction" },
	{ NULL,		"branch-ops N",		"stop after N branch misprediction branches" },
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
	{ NULL,		"chdir-dirs N",		"select number of directories to exercise chdir on" },
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
	{ NULL,		"cpu-method M",		"specify stress cpu method M, default is all" },
	{ NULL,		"cpu-online N",		"start N workers offlining/onlining the CPUs" },
	{ NULL,		"cpu-online-ops N",	"stop after N offline/online operations" },
	{ NULL,		"crypt N",		"start N workers performing password encryption" },
	{ NULL,		"crypt-ops N",		"stop after N bogo crypt operations" },
	{ NULL,		"daemon N",		"start N workers creating multiple daemons" },
	{ NULL,		"cyclic N",		"start N cyclic real time benchmark stressors" },
	{ NULL,		"cyclic-ops N",		"stop after N cyclic timing cycles" },
	{ NULL,		"cyclic-method M",	"specify cyclic method M, default is clock_ns" },
	{ NULL,		"cyclic-dist N",	"calculate distribution of interval N nanosecs" },
	{ NULL,		"cyclic-policy P",	"used rr or fifo scheduling policy" },
	{ NULL,		"cyclic-prio N",	"real time scheduling priority 1..100" },
	{ NULL,		"cyclic-sleep N",	"sleep time of real time timer in nanosecs" },
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
	{ NULL,		"dev",			"start N device entry thrashing stressors" },
	{ NULL,		"dev-ops",		"stop after N device thrashing bogo ops" },
	{ NULL,		"dev-shm",		"start N /dev/shm file and mmap stressors" },
	{ NULL,		"dev-shm-ops",		"stop after N /dev/shm bogo ops" },
	{ NULL,		"dir N",		"start N directory thrashing stressors" },
	{ NULL,		"dir-ops N",		"stop after N directory bogo operations" },
	{ NULL,		"dir-dirs N",		"select number of directories to exercise dir on" },
	{ NULL,		"dirdeep N",		"start N directory depth stressors" },
	{ NULL,		"dirdeep-ops N",	"stop after N directory depth bogo operations" },
	{ NULL,		"dirdeep-dirs N",	"create N directories per level" },
	{ NULL,		"dirdeep-inodes N",	"create a maximum N inodes (N can also be %)" },
	{ NULL,		"dnotify N",		"start N workers exercising dnotify events" },
	{ NULL,		"dnotify-ops N",	"stop dnotify workers after N bogo operations" },
	{ NULL,		"dup N",		"start N workers exercising dup/close" },
	{ NULL,		"dup-ops N",		"stop after N dup/close bogo operations" },
	{ NULL,		"dynlib N",		"start N workers exercising dlopen/dlclose" },
	{ NULL,		"dynlib-ops N",		"stop after N dlopen/dlclose bogo operations" },
	{ NULL,		"efivar N",		"start N workers that read EFI variables" },
	{ NULL,		"efivar-ops N",		"stop after N EFI variable bogo read operations" },
	{ NULL,		"enosys N",		"start N workers that call non-existent system calls" },
	{ NULL,		"enosys-ops N",		"stop after N enosys bogo operations" },
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
	{ NULL,		"file-ioctl N",		"start N workers exercising file specific ioctls" },
	{ NULL,		"file-ioctl-ops N",	"stop after N file ioctl bogo operations" },
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
	{ NULL,		"funccall N",		"start N workers exercising 1 to 9 arg functions" },
	{ NULL,		"funccall-ops N",	"stop after N function call bogo operations" },
	{ NULL,		"funccall-method M",	"select function call method M" },
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
	{ NULL,		"hrtimers N",		"start N workers that exercise high resolution timers" },
	{ NULL,		"hrtimers-ops N",	"stop after N bogo high-res timer bogo operations" },
	{ NULL,		"hsearch N",		"start N workers that exercise a hash table search" },
	{ NULL,		"hsearch-ops N",	"stop after N hash search bogo operations" },
	{ NULL,		"hsearch-size N",	"number of integers to insert into hash table" },
	{ NULL,		"icache N",		"start N CPU instruction cache thrashing workers" },
	{ NULL,		"icache-ops N",		"stop after N icache bogo operations" },
	{ NULL,		"icmp-flood N",		"start N ICMP packet flood workers" },
	{ NULL,		"icmp-flood-ops N",	"stop after N ICMP bogo operations (ICMP packets)" },
	{ NULL,		"inode-flags N",	"start N workers exercising various inode flags" },
	{ NULL,		"inode-flags-ops N",	"stop inode-flags workers after N bogo operations" },
	{ NULL,		"inotify N",		"start N workers exercising inotify events" },
	{ NULL,		"inotify-ops N",	"stop inotify workers after N bogo operations" },
	{ "i N",	"io N",			"start N workers spinning on sync()" },
	{ NULL,		"io-ops N",		"stop sync I/O after N io bogo operations" },
	{ NULL,		"iomix N",		"start N workers that have a mix of I/O operations" },
	{ NULL,		"iomix-bytes N",	"write N bytes per iomix worker (default is 1GB)" },
	{ NULL,		"iomix-ops N",		"stop iomix workers after N iomix bogo operations" },
	{ NULL,		"ioport N",		"start N workers exercising port I/O" },
	{ NULL,		"ioport-ops N",		"stop ioport workers after N port bogo operations" },
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
	{ NULL,		"klog-ops N",		"stop after N klog bogo operations" },
	{ NULL,		"lease N",		"start N workers holding and breaking a lease" },
	{ NULL,		"lease-ops N",		"stop after N lease bogo operations" },
	{ NULL,		"lease-breakers N",	"number of lease breaking workers to start" },
	{ NULL,		"link N",		"start N workers creating hard links" },
	{ NULL,		"link-ops N",		"stop after N link bogo operations" },
	{ NULL,		"locka N",		"start N workers locking a file via advisory locks" },
	{ NULL,		"locka-ops N",		"stop after N locka bogo operations" },
	{ NULL,		"lockbus N",		"start N workers locking a memory increment" },
	{ NULL,		"lockbus-ops N",	"stop after N lockbus bogo operations" },
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
	{ NULL,		"matrix-method M",	"specify matrix stress method M, default is all" },
	{ NULL,		"matrix-size N",	"specify the size of the N x N matrix" },
	{ NULL,		"matrix-yx",		"matrix operation is y by x instread of x by y" },
	{ NULL,		"mcontend N",		"start N workers that produce memory contention" },
	{ NULL,		"mcontend-ops N",	"stop memory contention workers after N bogo-ops" },
	{ NULL,		"membarrier N",		"start N workers performing membarrier system calls" },
	{ NULL,		"membarrier-ops N",	"stop after N membarrier bogo operations" },
	{ NULL,		"memcpy N",		"start N workers performing memory copies" },
	{ NULL,		"memcpy-ops N",		"stop after N memcpy bogo operations" },
	{ NULL,		"memfd N",		"start N workers allocating memory with memfd_create" },
	{ NULL,		"memfd-bytes N",	"allocate N bytes for each stress iteration" },
	{ NULL,		"memfd-fds N",		"number of memory fds to open per stressors" },
	{ NULL,		"memfd-ops N",		"stop after N memfd bogo operations" },
	{ NULL,		"memrate N",		"start N workers exercised memory read/writes" },
	{ NULL,		"memrate-ops",		"stop after N memrate bogo operations" },
	{ NULL,		"memrate-bytes N",	"size of memory buffer being exercised" },
	{ NULL,		"memrate-rd-mbs N",	"read rate from buffer in megabytes per second" },
	{ NULL,		"memrate-wr-mbs N",	"write rate to buffer in megabytes per second" },
	{ NULL,		"memthrash N",		"start N workers thrashing a 16MB memory buffer" },
	{ NULL,		"memthrash-ops N",	"stop after N memthrash bogo operations" },
	{ NULL,		"memthrash-method M",	"specify memthrash method M, default is all" },
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
	{ NULL,		"mmapaddr N",		"start N workers stressing mmap with random addresses" },
	{ NULL,		"mmapaddr-ops N",	"stop after N mmapaddr bogo operations" },
	{ NULL,		"mmapfixed N",		"start N workers stressing mmap with fixed mappings" },
	{ NULL,		"mmapfixed-ops N",	"stop after N mmapfixed bogo operations" },
	{ NULL,		"mmapfork N",		"start N workers stressing many forked mmaps/munmaps" },
	{ NULL,		"mmapfork-ops N",	"stop after N mmapfork bogo operations" },
	{ NULL,		"mmapmany N",		"start N workers stressing many mmaps and munmaps" },
	{ NULL,		"mmapmany-ops N",	"stop after N mmapmany bogo operations" },
	{ NULL,		"mq N",			"start N workers passing messages using POSIX messages" },
	{ NULL,		"mq-ops N",		"stop mq workers after N bogo messages" },
	{ NULL,		"mq-size N",		"specify the size of the POSIX message queue" },
	{ NULL,		"mremap N",		"start N workers stressing mremap" },
	{ NULL,		"mremap-ops N",		"stop after N mremap bogo operations" },
	{ NULL,		"mremap-bytes N",	"mremap N bytes maximum for each stress iteration" },
	{ NULL,		"msg N",		"start N workers stressing System V messages" },
	{ NULL,		"msg-ops N",		"stop msg workers after N bogo messages" },
	{ NULL,		"msync N",		"start N workers syncing mmap'd data with msync" },
	{ NULL,		"msync-ops N",		"stop msync workers after N bogo msyncs" },
	{ NULL,		"msync-bytes N",	"size of file and memory mapped region to msync" },
	{ NULL,		"netdev N",		"start N workers exercising netdevice ioctls" },
	{ NULL,		"netdev-ops N",		"stop netdev workers after N bogo operations" },
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
	{ NULL,		"physpage N",		"start N workers performing physical page lookup" },
	{ NULL,		"physpage-ops N",	"stop after N physical page bogo operations" },
	{ "p N",	"pipe N",		"start N workers exercising pipe I/O" },
	{ NULL,		"pipe-ops N",		"stop after N pipe I/O bogo operations" },
	{ NULL,		"pipe-data-size N",	"set pipe size of each pipe write to N bytes" },
#if defined(F_SETPIPE_SZ)
	{ NULL,		"pipe-size N",		"set pipe size to N bytes" },
#endif
	{ NULL,		"pkey",			"start N workers exercising pkey_mprotect" },
	{ NULL,		"pkey-ops",		"stop after N bogo pkey_mprotect bogo operations" },
	{ "P N",	"poll N",		"start N workers exercising zero timeout polling" },
	{ NULL,		"poll-ops N",		"stop after N poll bogo operations" },
	{ NULL,		"prctl N",		"start N workers exercising prctl(2)" },
	{ NULL,		"prctl-ops N",		"stop prctl workers after N bogo prctl operations" },
	{ NULL,		"procfs N",		"start N workers reading portions of /proc" },
	{ NULL,		"procfs-ops N",		"stop procfs workers after N bogo read operations" },
	{ NULL,		"pthread N",		"start N workers that create multiple threads" },
	{ NULL,		"pthread-ops N",	"stop pthread workers after N bogo threads created" },
	{ NULL,		"pthread-max P",	"create P threads at a time by each worker" },
	{ NULL,		"ptrace N",		"start N workers that trace a child using ptrace" },
	{ NULL,		"ptrace-ops N",		"stop ptrace workers after N system calls are traced" },
	{ NULL,		"pty N",		"start N workers that exercise pseudoterminals" },
	{ NULL,		"pty-ops N",		"stop pty workers after N pty bogo operations" },
	{ NULL,		"pty-max N",		"attempt to open a maximum of N ptys" },
	{ "Q",		"qsort N",		"start N workers qsorting 32 bit random integers" },
	{ NULL,		"qsort-ops N",		"stop after N qsort bogo operations" },
	{ NULL,		"qsort-size N",		"number of 32 bit integers to sort" },
	{ NULL,		"quota N",		"start N workers exercising quotactl commands" },
	{ NULL,		"quota-ops N",		"stop after N quotactl bogo operations" },
	{ NULL,		"radixsort N",		"start N workers radix sorting random strings" },
	{ NULL,		"radixsort-ops N",	"stop after N radixsort bogo operations" },
	{ NULL,		"radixsort-size N",	"number of strings to sort" },
	{ NULL,		"rawdev N",		"start N workers that read a raw device" },
	{ NULL,		"rawdev-ops N",		"stop after N rawdev read operations" },
	{ NULL,		"rawdev-method M",	"specify the rawdev reead method to use" },
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
	{ NULL,		"revio N",		"start N workers performing reverse I/O" },
	{ NULL,		"revio-ops N",		"stop after N revio bogo operations" },
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
	{ NULL,		"set N",		"start N workers exercising the set*() system calls" },
	{ NULL,		"set-ops N",		"stop after N set bogo operations" },
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
	{ NULL,		"sigio N",		"start N workers that exercise SIGIO signals" },
	{ NULL,		"sigio-ops N",		"stop after N bogo sigio signals" },
	{ NULL,		"sigpending N",		"start N workers exercising sigpending" },
	{ NULL,		"sigpending-ops N",	"stop after N sigpending bogo operations" },
	{ NULL,		"sigpipe N",		"start N workers exercising SIGPIPE" },
	{ NULL,		"sigpipe-ops N",	"stop after N SIGPIPE bogo operations" },
	{ NULL,		"sigq N",		"start N workers sending sigqueue signals" },
	{ NULL,		"sigq-ops N",		"stop after N siqqueue bogo operations" },
	{ NULL,		"sigrt N",		"start N workers sending real time signals" },
	{ NULL,		"sigrt-ops N",		"stop after N real time signal bogo operations" },
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
	{ NULL,		"sockdiag N",		"start N workers exercising sockdiag netlink" },
	{ NULL,		"sockdiag-ops N",	"stop sockdiag workers after N bogo messages" },
	{ NULL,		"sockfd N",		"start N workers sending file descriptors over sockets" },
	{ NULL,		"sockfd-ops N",		"stop after N sockfd bogo operations" },
	{ NULL,		"sockfd-port P",	"use socket fd ports P to P + number of workers - 1" },
	{ NULL,		"sockpair N",		"start N workers exercising socket pair I/O activity" },
	{ NULL,		"sockpair-ops N",	"stop after N socket pair bogo operations" },
	{ NULL,		"softlockup N",		"start N workers that cause softlockups" },
	{ NULL,		"softlockup-ops N",	"stop after N softlockup bogo operations" },
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
	{ NULL,		"stream-index",		"specify number of indices into the data (0..3)" },
	{ NULL,		"stream-l3-size N",	"specify the L3 cache size of the CPU" },
	{ NULL,		"stream-madvise M",	"specify mmap'd stream buffer madvise advice" },
	{ NULL,		"swap N",		"start N workers exercising swapon/swapoff" },
	{ NULL,		"swap-ops N",		"stop after N swapon/swapoff operations" },
	{ "s N",	"switch N",		"start N workers doing rapid context switches" },
	{ NULL,		"switch-ops N",		"stop after N context switch bogo operations" },
	{ NULL,		"symlink N",		"start N workers creating symbolic links" },
	{ NULL,		"symlink-ops N",	"stop after N symbolic link bogo operations" },
	{ NULL,		"sync-file N",		"start N workers exercise sync_file_range" },
	{ NULL,		"sync-file-ops N",	"stop after N sync_file_range bogo operations" },
	{ NULL,		"sync-file-bytes N",	"size of file to be sync'd" },
	{ NULL,		"sysinfo N",		"start N workers reading system information" },
	{ NULL,		"sysinfo-ops N",	"stop after sysinfo bogo operations" },
	{ NULL,		"sysbadaddr N",		"start N workers that pass bad addresses to syscalls" },
	{ NULL,		"sysbaddaddr-ops N",	"stop after N sysbadaddr bogo syscalls" },
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
	{ NULL,		"tree N",		"start N workers that exercise tree structures" },
	{ NULL,		"tree-ops N",		"stop after N bogo tree operations" },
	{ NULL,		"tree-method M",	"select tree method, all,avl,binary,rb,splay" },
	{ NULL,		"tree-size N",		"N is the number of items in the tree" },
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
	{ NULL,		"vm-madvise M",		"specify mmap'd vm buffer madvise advice" },
	{ NULL,		"vm-method M",		"specify stress vm method M, default is all" },
#if defined(MAP_POPULATE)
	{ NULL,		"vm-populate",		"populate (prefault) page tables for a mapping" },
#endif
	{ NULL,		"vm-addr N",		"start N vm address exercising workers" },
	{ NULL,		"vm-addr-ops N",	"stop after N vm address bogo operations" },
	{ NULL,		"vm-rw N",		"start N vm read/write process_vm* copy workers" },
	{ NULL,		"vm-rw-bytes N",	"transfer N bytes of memory per bogo operation" },
	{ NULL,		"vm-rw-ops N",		"stop after N vm process_vm* copy bogo operations" },
	{ NULL,		"vm-segv N",		"start N workers that unmap their address space" },
	{ NULL,		"vm-segv-ops N",	"stop after N vm-segv unmap'd SEGV faults" },
	{ NULL,		"vm-splice N",		"start N workers reading/writing using vmsplice" },
	{ NULL,		"vm-splice-ops N",	"stop after N bogo splice operations" },
	{ NULL,		"vm-splice-bytes N",	"number of bytes to transfer per vmsplice call" },
	{ NULL,		"wait N",		"start N workers waiting on child being stop/resumed" },
	{ NULL,		"wait-ops N",		"stop after N bogo wait operations" },
	{ NULL,		"watchdog N",		"start N workers that exercise /dev/watchdog" },
	{ NULL,		"watchdog-ops N",	"stop after N bogo watchdog operations" },
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
	{ NULL,		"zlib-level L",		"specify zlib compressession level 0=fast, 9=best" },
	{ NULL,		"zlib-method M",	"specify zlib random data generation method M" },
	{ NULL,		"zombie N",		"start N workers that rapidly create and reap zombies" },
	{ NULL,		"zombie-ops N",		"stop after N bogo zombie fork operations" },
	{ NULL,		"zombie-max N",		"set upper limit of N zombies per worker" },
	{ NULL,		NULL,			NULL }
};

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

	(void)shim_strlcpy(munged_name, tmp, len);

	for (i = 0; stressors[i].name; i++) {
		const char *munged_stressor_name =
			munge_underscore(stressors[i].name);

		if (!strcmp(munged_stressor_name, munged_name))
			break;
	}

	return i;	/* End of array is a special "NULL" entry */
}

/*
 *  remove_proc()
 *	remove proc from proc list
 */
static void remove_proc(proc_info_t *pi)
{
	if (procs_head == pi) {
		procs_head = pi->next;
		if (pi->next)
			pi->next->prev = pi->prev;
	} else {
		if (pi->prev)
			pi->prev->next = pi->next;
	}

	if (procs_tail == pi) {
		procs_tail = pi->prev;
		if (pi->prev)
			pi->prev->next = pi->next;
	} else {
		if (pi->next)
			pi->next->prev = pi->prev;
	}
}

/*
 *  get_class_id()
 *	find the class id of a given class name
 */
static uint32_t get_class_id(char *const str)
{
	size_t i;

	for (i = 0; i < SIZEOF_ARRAY(classes); i++) {
		if (!strcmp(classes[i].name, str))
			return classes[i].class;
	}
	return 0;
}

/*
 *  get_class()
 *	parse for allowed class types, return bit mask of types, 0 if error
 */
static int get_class(char *const class_str, uint32_t *class)
{
	char *str, *token;
	int ret = 0;

	*class = 0;
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

					(void)printf("class '%s' stressors:",
						token);
					for (j = 0; stressors[j].name; j++) {
						if (stressors[j].info->class & cl)
							(void)printf(" %s", munge_underscore(stressors[j].name));
					}
					(void)printf("\n");
					return 1;
				}
			}
			(void)fprintf(stderr, "Unknown class: '%s', "
				"available classes:", token);
			for (i = 0; i < SIZEOF_ARRAY(classes); i++)
				(void)fprintf(stderr, " %s", classes[i].name);
			(void)fprintf(stderr, "\n\n");
			return -1;
		}
		*class |= cl;
	}
	return ret;
}

/*
 *  stress_exclude()
 *  	parse -x --exlude exclude list
 */
static int stress_exclude(void)
{
	char *str, *token, *opt_exclude;

	if (!get_setting("exclude", &opt_exclude))
		return 0;

	for (str = opt_exclude; (token = strtok(str, ",")) != NULL; str = NULL) {
		stress_id_t id;
		proc_info_t *pi = procs_head;
		uint32_t i = stressor_name_find(token);

		if (!stressors[i].name) {
			(void)fprintf(stderr, "Unknown stressor: '%s', "
				"invalid exclude option\n", token);
			return -1;
		}
		id = stressors[i].id;

		while (pi) {
			proc_info_t *next = pi->next;

			if (pi->stressor->id == id)
				remove_proc(pi);
			pi = next;
		}
	}
	return 0;
}

/*
 *  stress_sigint_handler()
 *	catch signals and set flag to break out of stress loops
 */
static void MLOCKED_TEXT stress_sigint_handler(int dummy)
{
	(void)dummy;
	g_caught_sigint = true;
	g_keep_stressing_flag = false;
	wait_flag = false;

	(void)kill(-getpid(), SIGALRM);
}

/*
 *  stress_sigalrm_parent_handler()
 *	handle signal in parent process, don't block on waits
 */
static void MLOCKED_TEXT stress_sigalrm_parent_handler(int dummy)
{
	(void)dummy;
	wait_flag = false;
}

#if defined(SIGUSR2)
/*
 *  stress_stats_handler()
 *	dump current system stats
 */
static void MLOCKED_TEXT stress_stats_handler(int dummy)
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
		if (stress_sighandler(stress, SIGUSR2,
			stress_stats_handler, NULL) < 0) {
			return -1;
		}
	}
#endif
	if (stress_sighandler(stress, SIGALRM,
	    child ? stress_handle_stop_stressing :
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
			(void)snprintf(opt_s, sizeof(opt_s), "-%s,",
					help_info[i].opt_s);
		(void)printf("%-6s--%-19s%s\n", opt_s,
			help_info[i].opt_l, help_info[i].description);
	}
}

/*
 *  show_stressor_names()
 *	show stressor names
 */
static inline void show_stressor_names(void)
{
	size_t i;

	for (i = 0; stressors[i].name; i++)
		(void)printf("%s%s", i ? " " : "",
			munge_underscore(stressors[i].name));
	(void)putchar('\n');
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
	(void)printf("\nExample: %s --cpu 8 --io 4 --vm 2 --vm-bytes 128M "
		"--fork 4 --timeout 10s\n\n"
		"Note: Sizes can be suffixed with B,K,M,G and times with "
		"s,m,h,d,y\n", g_app_name);
	free_settings();
	exit(EXIT_SUCCESS);
}

/*
 *  opt_name()
 *	find name associated with an option value
 */
static const char *opt_name(const int opt_val)
{
	size_t i;

	for (i = 0; long_options[i].name; i++)
		if (long_options[i].val == opt_val)
			return long_options[i].name;

	return "<unknown>";
}

/*
 *  stress_get_processors()
 *	get number of processors, set count if <=0 as:
 *		count = 0 -> number of CPUs in system
 *		count < 0 -> number of CPUs online
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
	int signum = sig;
	proc_info_t *pi;

	/* multiple calls will always fallback to SIGKILL */
	count++;
	if (count > 5) {
		pr_dbg("killing process group %d with SIGKILL\n", (int)g_pgrp);
		signum = SIGKILL;
	}

	(void)killpg(g_pgrp, sig);

	for (pi = procs_head; pi; pi = pi->next) {
		int32_t i;

		for (i = 0; i < pi->started_procs; i++) {
			if (pi->pids[i])
				(void)kill(pi->pids[i], signum);
		}
	}
}

/*
 *  str_exitstatus()
 *	map stress-ng exit status returns into text
 */
static char *str_exitstatus(const int status)
{
	switch (status) {
	case EXIT_SUCCESS:
		return "success";
	case EXIT_FAILURE:
		return "stress-ng core failure";
	case EXIT_NOT_SUCCESS:
		return "stressor failed";
	case EXIT_NO_RESOURCE:
		return "no resource(s)";
	case EXIT_NOT_IMPLEMENTED:
		return "not implemented";
	case EXIT_SIGNALED:
		return "killed by signal";
	default:
		return "unknown";
	}
}

/*
 *  wait_procs()
 * 	wait for procs
 */
static void MLOCKED_TEXT wait_procs(
	proc_info_t *procs_list,
	bool *success,
	bool *resource_success)
{
	proc_info_t *pi;

	if (g_opt_flags & OPT_FLAGS_IGNITE_CPU)
		ignite_cpu_start();

#if defined(__linux__) && NEED_GLIBC(2,3,0)
	/*
	 *  On systems that support changing CPU affinity
	 *  we keep on moving processes between processors
	 *  to impact on memory locality (e.g. NUMA) to
	 *  try to thrash the system when in aggressive mode
	 */
	if (g_opt_flags & OPT_FLAGS_AGGRESSIVE) {
		cpu_set_t proc_mask;
		unsigned long int cpu = 0;
		const uint32_t ticks_per_sec =
			stress_get_ticks_per_second() * 5;
		const useconds_t usec_sleep =
			ticks_per_sec ? 1000000 / ticks_per_sec : 1000000 / 250;

		while (wait_flag) {
			const int32_t cpus = stress_get_processors_configured();

			/*
			 *  If we can't get the mask, then don't do
			 *  any affinity twiddling
			 */
			if (sched_getaffinity(0, sizeof(proc_mask), &proc_mask) < 0)
				goto do_wait;
			if (!CPU_COUNT(&proc_mask))	/* Highly unlikely */
				goto do_wait;

			for (pi = procs_list; pi; pi = pi->next) {
				int32_t j;

				for (j = 0; j < pi->started_procs; j++) {
					const pid_t pid = pi->pids[j];
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
	for (pi = procs_list; pi; pi = pi->next) {
		int32_t j;

		for (j = 0; j < pi->started_procs; j++) {
			pid_t pid;
redo:
			pid = pi->pids[j];
			if (pid) {
				int status, ret;
				bool do_abort = false;
				const char *stressor_name = munge_underscore(pi->stressor->name);

				ret = waitpid(pid, &status, 0);
				if (ret > 0) {
					if (WIFSIGNALED(status)) {
#if defined(WTERMSIG)
#if NEED_GLIBC(2,1,0)
						const char *signame = strsignal(WTERMSIG(status));

						pr_dbg("process [%d] (stress-ng-%s) terminated on signal: %d (%s)\n",
							ret, stressor_name,
							WTERMSIG(status), signame);
#else
						pr_dbg("process [%d] (stress-ng-%s) terminated on signal: %d\n",
							ret, stressor_name,
							WTERMSIG(status));
#endif
#else
						pr_dbg("process [%d] (stress-ng-%s) terminated on signal\n",
							ret, stressor_name));
#endif
						/*
						 *  If the stressor got killed by OOM or SIGKILL
						 *  then somebody outside of our control nuked it
						 *  so don't necessarily flag that up as a direct
						 *  failure.
						 */
						if (process_oomed(ret)) {
							pr_dbg("process [%d] (stress-ng-%s) was killed by the OOM killer\n",
								ret, stressor_name);
						} else if (WTERMSIG(status) == SIGKILL) {
							pr_dbg("process [%d] (stress-ng-%s) was possibly killed by the OOM killer\n",
								ret, stressor_name);
						} else {
							*success = false;
						}
					}
					switch (WEXITSTATUS(status)) {
					case EXIT_SUCCESS:
						break;
					case EXIT_NO_RESOURCE:
						pr_err("process [%d] (stress-ng-%s) aborted early, out of system resources\n",
							ret, stressor_name);
						*resource_success = false;
						do_abort = true;
						break;
					case EXIT_NOT_IMPLEMENTED:
						do_abort = true;
						break;
					case EXIT_BY_SYS_EXIT:
						pr_dbg("process [%d] (stress-ng-%s) aborted via exit() which was not expected\n",
							ret, stressor_name);
						do_abort = true;
						break;
					default:
						pr_err("process %d (stress-ng-%s) terminated with an error, exit status=%d (%s)\n",
							ret, stressor_name, WEXITSTATUS(status),
							str_exitstatus(WEXITSTATUS(status)));
						*success = false;
						do_abort = true;
						break;
					}
					if ((g_opt_flags & OPT_FLAGS_ABORT) && do_abort) {
						g_keep_stressing_flag = false;
						wait_flag = false;
						kill_procs(SIGALRM);
					}

					proc_finished(&pi->pids[j]);
					pr_dbg("process [%d] terminated\n", ret);
				} else if (ret == -1) {
					/* Somebody interrupted the wait */
					if (errno == EINTR)
						goto redo;
					/* This child did not exist, mark it done anyhow */
					if (errno == ECHILD)
						proc_finished(&pi->pids[j]);
				}
			}
		}
	}
	if (g_opt_flags & OPT_FLAGS_IGNITE_CPU)
		ignite_cpu_stop();
}

/*
 *  handle_terminate()
 *	catch terminating signals
 */
static void MLOCKED_TEXT handle_terminate(int signum)
{
	terminate_signum = signum;
	g_keep_stressing_flag = false;
	kill_procs(SIGALRM);
}

/*
 *  get_proc()
 *	return nth proc from list
 */
static proc_info_t *get_nth_proc(const uint32_t n)
{
	proc_info_t *pi = procs_head;
	uint32_t i;

	for (i = 0; pi && (i < n); i++)
		pi = pi->next;

	return pi;
}

/*
 *  get_num_procs()
 *	return number of procs in proc list
 */
static uint32_t get_num_procs(void)
{
	uint32_t n = 0;
	proc_info_t *pi;

	for (pi = procs_head; pi; pi = pi->next)
		n++;

	return n;
}

/*
 *  free_procs()
 *	free proc info in procs table
 */
static void free_procs(void)
{
	proc_info_t *pi = procs_head;

	while (pi) {
		proc_info_t *next = pi->next;

		free(pi->pids);
		free(pi->stats);
		free(pi);

		pi = next;
	}

	procs_head = NULL;
	procs_tail = NULL;
}

/*
 *  get_total_num_procs()
 *	deterimine number of runnable procs from list
 */
static uint32_t get_total_num_procs(proc_info_t *procs_list)
{
	uint32_t total_num_procs = 0;
	proc_info_t *pi;

	for (pi = procs_list; pi; pi = pi->next)
		total_num_procs += pi->num_procs;

	return total_num_procs;
}

/*
 *  stress_child_atexit(void)
 *	handle unexpected exit() call in child stressor
 */
static void stress_child_atexit(void)
{
	_exit(EXIT_BY_SYS_EXIT);
}

/*
 *  stress_run ()
 *	kick off and run stressors
 */
static void MLOCKED_TEXT stress_run(
	proc_info_t *procs_list,
	double *duration,
	bool *success,
	bool *resource_success
)
{
	double time_start, time_finish;
	int32_t n_procs, j;
	const int32_t total_procs = get_total_num_procs(procs_list);

	wait_flag = true;
	time_start = time_now();
	pr_dbg("starting stressors\n");
	for (n_procs = 0; n_procs < total_procs; n_procs++) {
		for (proc_current = procs_list; proc_current; proc_current = proc_current->next) {
			if (g_opt_timeout && (time_now() - time_start > g_opt_timeout))
				goto abort;

			j = proc_current->started_procs;

			if (j < proc_current->num_procs) {
				int rc = EXIT_SUCCESS;
				pid_t pid;
				char name[64];
				int64_t backoff = DEFAULT_BACKOFF;
				int32_t ionice_class = UNDEFINED;
				int32_t ionice_level = UNDEFINED;

				(void)get_setting("backoff", &backoff);
				(void)get_setting("ionice-class", &ionice_class);
				(void)get_setting("ionice-level", &ionice_level);

				proc_stats_t *stats = proc_current->stats[j];
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
					(void)atexit(stress_child_atexit);
					(void)setpgid(0, g_pgrp);
					if (stress_set_handler(name, true) < 0) {
						rc = EXIT_FAILURE;
						goto child_exit;
					}
					stress_parent_died_alarm();
					stress_process_dumpable(false);
					if (g_opt_flags & OPT_FLAGS_TIMER_SLACK)
						stress_set_timer_slack();

					if (g_opt_timeout)
						(void)alarm(g_opt_timeout);
					mwc_reseed();
					(void)snprintf(name, sizeof(name), "%s-%s", g_app_name,
						munge_underscore(proc_current->stressor->name));
					set_oom_adjustment(name, false);
					set_max_limits();
					set_iopriority(ionice_class, ionice_level);
					set_proc_name(name);

					pr_dbg("%s: started [%d] (instance %" PRIu32 ")\n",
						name, (int)getpid(), j);

					stats->start = stats->finish = time_now();
#if defined(STRESS_PERF_STATS)
					if (g_opt_flags & OPT_FLAGS_PERF_STATS)
						(void)perf_open(&stats->sp);
#endif
					(void)shim_usleep(backoff * n_procs);
#if defined(STRESS_PERF_STATS)
					if (g_opt_flags & OPT_FLAGS_PERF_STATS)
						(void)perf_enable(&stats->sp);
#endif
					if (g_keep_stressing_flag && !(g_opt_flags & OPT_FLAGS_DRY_RUN)) {
						const args_t args = {
							.counter = &stats->counter,
							.name = name,
							.max_ops = proc_current->bogo_ops,
							.instance = j,
							.num_instances = proc_current->num_procs,
							.pid = getpid(),
							.ppid = getppid(),
							.page_size = stress_get_pagesize(),
						};

						rc = proc_current->stressor->info->stressor(&args);
						pr_fail_check(&rc);
						stats->run_ok = (rc == EXIT_SUCCESS);
					}
#if defined(STRESS_PERF_STATS)
					if (g_opt_flags & OPT_FLAGS_PERF_STATS) {
						(void)perf_disable(&stats->sp);
						(void)perf_close(&stats->sp);
					}
#endif
#if defined(STRESS_THERMAL_ZONES)
					if (g_opt_flags & OPT_FLAGS_THERMAL_ZONES)
						(void)tz_get_temperatures(&g_shared->tz_info, &stats->tz);
#endif

					stats->finish = time_now();
					if (times(&stats->tms) == (clock_t)-1) {
						pr_dbg("times failed: errno=%d (%s)\n",
							errno, strerror(errno));
					}
					pr_dbg("%s: exited [%d] (instance %" PRIu32 ")\n",
						name, (int)getpid(), j);

child_exit:
					free_procs();
					stress_cache_free();
					free_settings();
					if ((rc != 0) && (g_opt_flags & OPT_FLAGS_ABORT)) {
						g_keep_stressing_flag = false;
						wait_flag = false;
						(void)kill(getppid(), SIGALRM);
					}
					if (terminate_signum)
						rc = EXIT_SIGNALED;
					_exit(rc);
				default:
					if (pid > -1) {
						(void)setpgid(pid, g_pgrp);
						proc_current->pids[j] = pid;
						proc_current->started_procs++;
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
	if (g_opt_timeout)
		(void)alarm(g_opt_timeout);

abort:
	pr_dbg("%d stressor%s spawned\n", n_procs,
		n_procs == 1 ? "" : "s");

wait_for_procs:
	wait_procs(procs_list, success, resource_success);
	time_finish = time_now();

	*duration += time_finish - time_start;
}

/*
 *  show_stressors()
 *	show names of stressors that are going to be run
 */
static int show_stressors(void)
{
	char *newstr, *str = NULL;
	ssize_t len = 0;
	char buffer[64];
	bool previous = false;
	proc_info_t *pi;

	for (pi = procs_head; pi; pi = pi->next) {
		const int32_t n = pi->num_procs;

		if (n) {
			ssize_t buffer_len;

			buffer_len = snprintf(buffer, sizeof(buffer),
					"%s %" PRId32 " %s",
					previous ? "," : "", n,
					munge_underscore(pi->stressor->name));
			previous = true;
			if (buffer_len >= 0) {
				newstr = realloc(str, len + buffer_len + 1);
				if (!newstr) {
					pr_err("Cannot allocate temporary buffer\n");
					free(str);
					return -1;
				}
				str = newstr;
				(void)shim_strlcpy(str + len, buffer, buffer_len + 1);
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
	const int32_t ticks_per_sec)
{
	proc_info_t *pi;

	pr_inf("%-13s %9.9s %9.9s %9.9s %9.9s %12s %12s\n",
		"stressor", "bogo ops", "real time", "usr time",
		"sys time", "bogo ops/s", "bogo ops/s");
	pr_inf("%-13s %9.9s %9.9s %9.9s %9.9s %12s %12s\n",
		"", "", "(secs) ", "(secs) ", "(secs) ", "(real time)",
		"(usr+sys time)");
	pr_yaml(yaml, "metrics:\n");

	for (pi = procs_head; pi; pi = pi->next) {
		uint64_t c_total = 0, u_total = 0, s_total = 0, us_total;
		double   r_total = 0.0;
		int32_t  j;
		char *munged = munge_underscore(pi->stressor->name);
		double u_time, s_time, bogo_rate_r_time, bogo_rate;
		bool run_ok = false;

		for (j = 0; j < pi->started_procs; j++) {
			const proc_stats_t *const stats = pi->stats[j];

			run_ok  |= stats->run_ok;
			c_total += stats->counter;
			u_total += stats->tms.tms_utime +
				   stats->tms.tms_cutime;
			s_total += stats->tms.tms_stime +
				   stats->tms.tms_cstime;
			r_total += stats->finish - stats->start;
		}
		/* Total usr + sys time of all procs */
		us_total = u_total + s_total;
		/* Real time in terms of average wall clock time of all procs */
		r_total = pi->started_procs ?
			r_total / (double)pi->started_procs : 0.0;

		if ((g_opt_flags & OPT_FLAGS_METRICS_BRIEF) &&
		    (c_total == 0) && (!run_ok))
			continue;

		u_time = (ticks_per_sec > 0) ? (double)u_total / (double)ticks_per_sec : 0.0;
		s_time = (ticks_per_sec > 0) ? (double)s_total / (double)ticks_per_sec : 0.0;
		bogo_rate_r_time = (r_total > 0.0) ? (double)c_total / r_total : 0.0;
		bogo_rate = (us_total > 0) ? (double)c_total / ((double)us_total / (double)ticks_per_sec) : 0.0;

		pr_inf("%-13s %9" PRIu64 " %9.2f %9.2f %9.2f %12.2f %12.2f\n",
			munged,		/* stress test name */
			c_total,	/* op count */
			r_total,	/* average real (wall) clock time */
			u_time, 	/* actual user time */
			s_time,		/* actual system time */
			bogo_rate_r_time, /* bogo ops on wall clock time */
			bogo_rate);	/* bogo ops per second */

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
	size_t i, len, arglen[argc];
	char *buf;

	for (len = 0, i = 0; i < (size_t)argc; i++) {
		arglen[i] = strlen(argv[i]);
		len += arglen[i] + 1;
	}

	buf = calloc(len, sizeof(*buf));
	if (!buf)
		return;

	for (len = 0, i = 0; i < (size_t)argc; i++) {
		if (i) {
			(void)shim_strlcat(buf + len, " ", 1);
			len++;
		}
		(void)shim_strlcat(buf + len, argv[i], arglen[i]);
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
#if defined(HAVE_UNAME)
	struct utsname buf;

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
	(void)memset(g_shared, 0, len);
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

	for (i = 0; i < SIZEOF_ARRAY(stressors); i++) {
		if (stressors[i].info && stressors[i].info->supported) {
			proc_info_t *pi = procs_head;
			stress_id_t id = stressors[i].id;

			while (pi) {
				proc_info_t *next = pi->next;

				if ((pi->stressor->id == id) &&
				    pi->num_procs &&
				    (stressors[i].info->supported() < 0)) {
					remove_proc(pi);
					g_unsupported = true;
				}
				pi = next;
			}
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
	proc_info_t *pi;
	struct rlimit limit;

	if (getrlimit(RLIMIT_NPROC, &limit) < 0)
		return;

	for (pi = procs_head; pi; pi = pi->next) {
		size_t i;

		for (i = 0; i < SIZEOF_ARRAY(stressors); i++) {
			if (stressors[i].info &&
			    stressors[i].info->set_limit &&
			    (stressors[i].id == pi->stressor->id) &&
			    pi->num_procs) {
				const uint64_t max = (uint64_t)limit.rlim_cur / pi->num_procs;

				stressors[i].info->set_limit(max);
			}
		}
	}
#endif
}

/*
 *  find_proc_info()
 *	find proc info that is associated with a specific
 *	stressor.  If it does not exist, create a new one
 *	and return that. Terminate if out of memory.
 */
static proc_info_t *find_proc_info(const stress_t *stressor)
{
	proc_info_t *pi;

#if 0
	/* Scan backwards in time to find last matching stressor */
	for (pi = procs_tail; pi; pi = pi->prev) {
		if (pi->stressor == stressor)
			return pi;
	}
#endif

	pi = calloc(1, sizeof(*pi));
	if (!pi) {
		(void)fprintf(stderr, "Cannot allocate stressor state info\n");
		exit(EXIT_FAILURE);
	}

	pi->stressor = stressor;

	/* Add to end of procs list */
	if (procs_tail)
		procs_tail->next = pi;
	else
		procs_head = pi;
	pi->prev = procs_tail;
	procs_tail = pi;

	return pi;
}

/*
 *  stressors_init()
 *	initialize any stressors that will be used
 */
static void stressors_init(void)
{
	proc_info_t *pi;

	for (pi = procs_head; pi; pi = pi->next) {
		size_t i;

		for (i = 0; i < SIZEOF_ARRAY(stressors); i++) {
			if (stressors[i].info &&
			    stressors[i].info->init &&
			    stressors[i].id == pi->stressor->id)
				stressors[i].info->init();
		}
	}
}

/*
 *  stressors_deinit()
 *	de-initialize any stressors that will be used
 */
static void stressors_deinit(void)
{
	proc_info_t *pi;

	for (pi = procs_head; pi; pi = pi->next) {
		size_t i;

		for (i = 0; i < SIZEOF_ARRAY(stressors); i++) {
			if (stressors[i].info &&
			    stressors[i].info->deinit &&
			    stressors[i].id == pi->stressor->id)
				stressors[i].info->deinit();
		}
	}
}


/*
 *  stessor_set_defaults()
 *	set up stressor default settings that can be overridden
 *	by user later on
 */
static inline void stressor_set_defaults(void)
{
	size_t i;

	for (i = 0; i < SIZEOF_ARRAY(stressors); i++) {
		if (stressors[i].info && stressors[i].info->set_default) {
			stressors[i].info->set_default();
		}
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
		proc_info_t *pi = procs_head;

		while (pi) {
			proc_info_t *next = pi->next;

			if (pi->stressor->info->class & CLASS_PATHOLOGICAL) {
				if (pi->num_procs > 0) {
					pr_inf("disabled '%s' as it "
						"may hang or reboot the machine "
						"(enable it with the "
						"--pathological option)\n",
						munge_underscore(pi->stressor->name));
				}
				remove_proc(pi);
			}
			pi = next;
		}
	}
}

/*
 *  setup_stats_buffers()
 *	setup the stats data from the shared memory
 */
static inline void setup_stats_buffers(void)
{
	proc_info_t *pi;
	proc_stats_t *stats = g_shared->stats;

	for (pi = procs_head; pi; pi = pi->next) {
		int32_t j;

		for (j = 0; j < pi->num_procs; j++, stats++)
			pi->stats[j] = stats;
	}
}

/*
 *  set_random_stressors()
 *	select stressors at random
 */
static inline void set_random_stressors(void)
{
	int32_t opt_random = 0;

	(void)get_setting("random", &opt_random);

	if (g_opt_flags & OPT_FLAGS_RANDOM) {
		int32_t n = opt_random;
		int32_t n_procs = get_num_procs();

		if (g_opt_flags & OPT_FLAGS_SET) {
			(void)fprintf(stderr, "Cannot specify random "
				"option with other stress processes "
				"selected\n");
			exit(EXIT_FAILURE);
		}

		if (!n_procs)
			n_procs = 1;

		/* create n randomly chosen stressors */
		while (n > 0) {
			int32_t rnd = mwc32() % ((opt_random >> 5) + 2);
			int32_t i = mwc32() % n_procs;
			proc_info_t *pi = get_nth_proc(i);

			if (!pi)
				continue;

			if (rnd > n)
				rnd = n;
			pi->num_procs += rnd;
			n -= rnd;
		}
	}
}

/*
 *  enable_all_stressors()
 *	enable all the stressors
 */
static void enable_all_stressors(const uint32_t instances)
{
	size_t i;

	/* Don't enable all if some stressors are set */
	if (g_opt_flags & OPT_FLAGS_SET)
		return;

	for (i = 0; i < STRESS_MAX; i++) {
		proc_info_t *pi = find_proc_info(&stressors[i]);

		if (!pi) {
			(void)fprintf(stderr, "Cannot allocate stressor state info\n");
			exit(EXIT_FAILURE);
		}
		pi->num_procs = instances;
	}
}

/*
 *  enable_classes()
 *	enable stressors based on class
 */
static void enable_classes(const uint32_t class)
{
	size_t i;

	if (!class)
		return;

	/* This indicates some stressors are set */
	g_opt_flags |= OPT_FLAGS_SET;

	for (i = 0; stressors[i].id != STRESS_MAX; i++) {
		if (stressors[i].info->class & class) {
			proc_info_t *pi = find_proc_info(&stressors[i]);

			if (g_opt_flags & OPT_FLAGS_SEQUENTIAL)
				pi->num_procs = g_opt_sequential;
			if (g_opt_flags & OPT_FLAGS_ALL)
				pi->num_procs = g_opt_parallel;
		}
	}
}


/*
 *  parse_opts
 *	parse argv[] and set stress-ng options accordingly
 */
int parse_opts(int argc, char **argv, const bool jobmode)
{
	optind = 0;

	for (;;) {
		int64_t i64;
		int32_t i32;
		uint32_t u32;
		int16_t i16;
		int c, option_index, ret;
		size_t i;

		opterr = 0;
next_opt:
		if ((c = getopt_long(argc, argv, "?khMVvqnt:b:c:i:j:m:d:f:s:l:p:P:C:S:a:y:F:D:T:u:o:r:B:R:Y:x:",
			long_options, &option_index)) == -1) {
			break;
		}

		for (i = 0; stressors[i].id != STRESS_MAX; i++) {
			if (stressors[i].short_getopt == c) {
				const char *name = opt_name(c);
				proc_info_t *pi = find_proc_info(&stressors[i]);
				proc_current = pi;

				g_opt_flags |= OPT_FLAGS_SET;
				pi->num_procs = get_int32(optarg);
				stress_get_processors(&pi->num_procs);
				check_value(name, pi->num_procs);

				goto next_opt;
			}
			if (stressors[i].op == (stress_op_t)c) {
				uint64_t bogo_ops;

				bogo_ops = get_uint64(optarg);
				check_range(opt_name(c), bogo_ops,
					MIN_OPS, MAX_OPS);
				/* We don't need to set this, but it may be useful */
				set_setting(opt_name(c), TYPE_ID_UINT64, &bogo_ops);
				if (proc_current)
					proc_current->bogo_ops = bogo_ops;
				goto next_opt;
			}
		}

		for (i = 0; i < SIZEOF_ARRAY(opt_flags); i++) {
			if (c == opt_flags[i].opt) {
				g_opt_flags |= opt_flags[i].opt_flag;
				goto next_opt;
			}
		}
		for (i = 0; i < SIZEOF_ARRAY(opt_set_funcs); i++) {
			if (c == opt_set_funcs[i].opt) {
				ret = opt_set_funcs[i].opt_set_func(optarg);
				if (ret < 0)
					return EXIT_FAILURE;
				goto next_opt;
			}
		}

		switch (c) {
		case OPT_all:
			g_opt_flags |= OPT_FLAGS_ALL;
			g_opt_parallel = get_int32(optarg);
			stress_get_processors(&g_opt_parallel);
			check_value("all", g_opt_parallel);
			break;
		case OPT_backoff:
			i64 = (int64_t)get_uint64(optarg);
			set_setting_global("backoff", TYPE_ID_INT64, &i64);
			break;
		case OPT_cache_level:
			/*
			 * Note: Overly high values will be caught in the
			 * caching code.
			 */
			i16 = atoi(optarg);
			if ((i16 <= 0) || (i16 > 3))
				i16 = DEFAULT_CACHE_LEVEL;
			set_setting("cache-level", TYPE_ID_INT16, &i16);
			break;
		case OPT_cache_ways:
			u32 = get_uint32(optarg);
			set_setting("cache-ways", TYPE_ID_UINT32, &u32);
			break;
		case OPT_class:
			ret = get_class(optarg, &u32);
			if (ret < 0)
				return EXIT_FAILURE;
			else if (ret > 0)
				exit(EXIT_SUCCESS);
			else {
				set_setting("class", TYPE_ID_UINT32, &u32);
				enable_classes(u32);
			}
			break;
		case OPT_exclude:
			set_setting_global("exclude", TYPE_ID_STR, (void *)optarg);
			break;
		case OPT_help:
			usage();
			break;
		case OPT_ionice_class:
			i32 = get_opt_ionice_class(optarg);
			set_setting("ionice-class", TYPE_ID_INT32, &i32);
			break;
		case OPT_ionice_level:
			i32 = get_int32(optarg);
			set_setting("ionice-level", TYPE_ID_INT32, &i32);
			break;
		case OPT_job:
			set_setting_global("job", TYPE_ID_STR, (void *)optarg);
			break;
		case OPT_log_file:
			set_setting_global("log-file", TYPE_ID_STR, (void *)optarg);
			break;
		case OPT_no_madvise:
			g_opt_flags &= ~OPT_FLAGS_MMAP_MADVISE;
			break;
		case OPT_query:
			if (!jobmode) {
				(void)printf("%s: unrecognized option '%s'\n", g_app_name, argv[optind - 1]);
				(void)printf("Try '%s --help' for more information.\n", g_app_name);
			}
			return EXIT_FAILURE;
			break;
		case OPT_quiet:
			g_opt_flags &= ~(PR_ALL);
			break;
		case OPT_random:
			g_opt_flags |= OPT_FLAGS_RANDOM;
			i32 = get_int32(optarg);
			check_value("random", i32);
			stress_get_processors(&i32);
			set_setting("random", TYPE_ID_INT32, &i32);
			break;
		case OPT_sched:
			i32 = get_opt_sched(optarg);
			set_setting_global("sched", TYPE_ID_INT32, &i32);
			break;
		case OPT_sched_prio:
			i32 = get_int32(optarg);
			set_setting_global("sched-prio", TYPE_ID_INT32, &i32);
			break;
		case OPT_sequential:
			g_opt_flags |= OPT_FLAGS_SEQUENTIAL;
			g_opt_sequential = get_int32(optarg);
			stress_get_processors(&g_opt_sequential);
			check_range("sequential", g_opt_sequential,
				MIN_SEQUENTIAL, MAX_SEQUENTIAL);
			break;
		case OPT_stressors:
			show_stressor_names();
			exit(EXIT_SUCCESS);
		case OPT_timeout:
			g_opt_timeout = get_uint64_time(optarg);
			break;
		case OPT_version:
			version();
			exit(EXIT_SUCCESS);
#if defined(MAP_LOCKED)
		case OPT_vm_mmap_locked:
			stress_set_vm_flags(MAP_LOCKED);
			break;
#endif
#if defined(MAP_POPULATE)
		case OPT_vm_mmap_populate:
			stress_set_vm_flags(MAP_POPULATE);
			break;
#endif
		case OPT_yaml:
			set_setting_global("yaml", TYPE_ID_STR, (void *)optarg);
			break;
		default:
			if (!jobmode)
				(void)printf("Unknown option (%d)\n",c);
			return EXIT_FAILURE;
		}
	}
	return EXIT_SUCCESS;
}

/*
 *  alloc_proc_resources()
 *	allocate array of pids based on n pids required
 */
static void alloc_proc_resources(pid_t **pids, proc_stats_t ***stats, size_t n)
{
	*pids = calloc(n, sizeof(pid_t));
	if (!*pids) {
		pr_err("cannot allocate pid list\n");
		free_procs();
		exit(EXIT_FAILURE);
	}

	*stats = calloc(n, sizeof(proc_stats_t *));
	if (!*stats) {
		pr_err("cannot allocate stats list\n");
		free(*pids);
		*pids = NULL;
		free_procs();
		exit(EXIT_FAILURE);
	}
}

/*
 *  set_default_timeout()
 *	set timeout to a default value if not already set
 */
static void set_default_timeout(const uint64_t timeout)
{
	if (g_opt_timeout == TIMEOUT_NOT_SET) {
		g_opt_timeout = timeout;
		pr_inf("defaulting to a %" PRIu64 " second%s run per stressor\n",
			g_opt_timeout,
			duration_to_str((double)g_opt_timeout));
	}
}

/*
 *  stress_setup_sequential()
 *	setup for sequential --seq mode stressors
 */
static void stress_setup_sequential(const uint32_t class)
{
	proc_info_t *pi;

	set_default_timeout(60);

	for (pi = procs_head; pi; pi = pi->next) {
		if (pi->stressor->info->class & class)
			pi->num_procs = g_opt_sequential;
		alloc_proc_resources(&pi->pids, &pi->stats, pi->num_procs);
	}
}

/*
 *  stress_setup_parallel()
 *	setup for parallel mode stressors
 */
static void stress_setup_parallel(const uint32_t class)
{
	proc_info_t *pi;

	set_default_timeout(DEFAULT_TIMEOUT);

	for (pi = procs_head; pi; pi = pi->next) {
		if (pi->stressor->info->class & class)
			pi->num_procs = g_opt_parallel;
		/*
		 * Share bogo ops between processes equally, rounding up
		 * if nonzero bogo_ops
		 */
		pi->bogo_ops = pi->num_procs ?
			(pi->bogo_ops + (pi->num_procs - 1)) / pi->num_procs : 0;
		if (pi->num_procs)
			alloc_proc_resources(&pi->pids, &pi->stats, pi->num_procs);
	}
}

/*
 *  stress_run_sequential()
 *	run stressors sequentially
 */
static inline void stress_run_sequential(
	double *duration,
	bool *success,
	bool *resource_success)
{
	proc_info_t *pi;

	/*
	 *  Step through each stressor one by one
	 */
	for (pi = procs_head; pi && g_keep_stressing_flag; pi = pi->next) {
		proc_info_t *next = pi->next;

		pi->next = NULL;
		stress_run(pi, duration, success, resource_success);
		pi->next = next;

	}
}

/*
 *  stress_run_parallel()
 *	run stressors in parallel
 */
static inline void stress_run_parallel(
	double *duration,
	bool *success,
	bool *resource_success)
{
	/*
	 *  Run all stressors in parallel
	 */
	stress_run(procs_head, duration, success, resource_success);
}

/*
 *  stress_mlock_executable()
 *	try to mlock image into memory so it
 *	won't get swapped out
 */
static inline void stress_mlock_executable(void)
{
#if defined(MLOCKED_SECTION)
	extern void *__start_mlocked_text;
	extern void *__stop_mlocked_text;
	extern void *__start_mlocked_data;
	extern void *__stop_mlocked_data;

	stress_mlock_region(&__start_mlocked_text, &__stop_mlocked_text);
	stress_mlock_region(&__start_mlocked_data, &__stop_mlocked_data);
#endif
}

int main(int argc, char **argv)
{
	double duration = 0.0;			/* stressor run time in secs */
	size_t len;
	bool success = true, resource_success = true;
	FILE *yaml;				/* YAML output file */
	char *yaml_filename;			/* YAML file name */
	char *log_filename;			/* log filename */
	char *job_filename = NULL;		/* job filename */
	int32_t ticks_per_sec;			/* clock ticks per second (jiffies) */
	int32_t sched = UNDEFINED;		/* scheduler type */
	int32_t sched_prio = UNDEFINED;		/* scheduler priority */
	int32_t ionice_class = UNDEFINED;	/* ionice class */
	int32_t ionice_level = UNDEFINED;	/* ionice level */
	int32_t i;
	uint32_t class = 0;
	const uint32_t cpus_online = stress_get_processors_online();
	const uint32_t cpus_configured = stress_get_processors_configured();
	int ret;

	if (setjmp(g_error_env) == 1)
		exit(EXIT_FAILURE);

	yaml = NULL;

	/* --exec stressor uses this to exec itself and then exit early */
	if ((argc == 2) && !strcmp(argv[1], "--exec-exit"))
		exit(EXIT_SUCCESS);

	procs_head = NULL;
	procs_tail = NULL;
	mwc_reseed();

	(void)stress_get_pagesize();
	stressor_set_defaults();
	g_pgrp = getpid();

	if (stress_get_processors_configured() < 0) {
		pr_err("sysconf failed, number of cpus configured "
			"unknown: errno=%d: (%s)\n",
			errno, strerror(errno));
		exit(EXIT_FAILURE);
	}
	ticks_per_sec = stress_get_ticks_per_second();
	if (ticks_per_sec < 0) {
		pr_err("sysconf failed, clock ticks per second "
			"unknown: errno=%d (%s)\n",
			errno, strerror(errno));
		exit(EXIT_FAILURE);
	}

	ret = parse_opts(argc, argv, false);
	if (ret != EXIT_SUCCESS)
		exit(ret);


	/*
	 *  Sanity check minimize/maximize options
	 */
	if ((g_opt_flags & OPT_FLAGS_MINMAX_MASK) == OPT_FLAGS_MINMAX_MASK) {
		(void)fprintf(stderr, "maximize and minimize cannot "
			"be used together\n");
		exit(EXIT_FAILURE);
	}

	/*
	 *  Sanity check seq/all settings
	 */
	if ((g_opt_flags & (OPT_FLAGS_SEQUENTIAL | OPT_FLAGS_ALL)) ==
	    (OPT_FLAGS_SEQUENTIAL | OPT_FLAGS_ALL)) {
		(void)fprintf(stderr, "cannot invoke --sequential and --all "
			"options together\n");
		exit(EXIT_FAILURE);
	}
	if (class &&
	    !(g_opt_flags & (OPT_FLAGS_SEQUENTIAL | OPT_FLAGS_ALL))) {
		(void)fprintf(stderr, "class option is only used with "
			"--sequential or --all options\n");
		exit(EXIT_FAILURE);
	}

	/*
	 *  Setup logging
	 */
	if (get_setting("log-file", &log_filename))
		pr_openlog(log_filename);
	openlog("stress-ng", 0, LOG_USER);
	log_args(argc, argv);
	log_system_info();
	log_system_mem_info();

	pr_dbg("%" PRId32 " processor%s online, %" PRId32
		" processor%s configured\n",
		cpus_online, cpus_online == 1 ? "" : "s",
		cpus_configured, cpus_configured == 1 ? "" : "s");

	/*
	 *  These two options enable all the stressors
	 */
	if (g_opt_flags & OPT_FLAGS_SEQUENTIAL)
		enable_all_stressors(g_opt_sequential);
	if (g_opt_flags & OPT_FLAGS_ALL)
		enable_all_stressors(g_opt_parallel);

	/*
	 *  Discard stressors that we can't run
	 */
	exclude_unsupported();
	exclude_pathological();
	/*
	 *  Throw away excluded stressors
	 */
	if (stress_exclude() < 0)
		exit(EXIT_FAILURE);

	/*
	 *  Setup random stressors if requested
	 */
	set_random_stressors();

#if defined(STRESS_PERF_STATS)
	if (g_opt_flags & OPT_FLAGS_PERF_STATS)
		perf_init();
#endif

	/*
	 *  Setup running environment
	 */
	stress_process_dumpable(false);
	stress_cwd_readwriteable();
	set_oom_adjustment("main", false);

	/*
	 *  Get various user defined settings
	 */
	(void)get_setting("sched", &sched);
	(void)get_setting("sched-prio", &sched_prio);
	if (stress_set_sched(getpid(), sched, sched_prio, false) < 0)
		exit(EXIT_FAILURE);
	(void)get_setting("ionice-class", &ionice_class);
	(void)get_setting("ionice-level", &ionice_level);
	set_iopriority(ionice_class, ionice_level);

	stress_mlock_executable();

	/*
	 *  Enable signal handers
	 */
	for (i = 0; terminate_signals[i] != -1; i++) {
		if (stress_sighandler("stress-ng", terminate_signals[i], handle_terminate, NULL) < 0)
			exit(EXIT_FAILURE);
	}

	/*
	 *  Load in job file options
	 */
	(void)get_setting("job", &job_filename);
	if (parse_jobfile(argc, argv, job_filename) < 0)
		exit(EXIT_FAILURE);

	/*
	 *  Setup stressor proc info
	 */
	if (g_opt_flags & OPT_FLAGS_SEQUENTIAL) {
		stress_setup_sequential(class);
	} else {
		stress_setup_parallel(class);
	}

	set_proc_limits();

	if (!procs_head) {
		pr_err("No stress workers invoked%s\n",
			g_unsupported ? " (one or more were unsupported)" : "");
		free_procs();
		/*
		 *  If some stressors were given but marked as
		 *  unsupported then this is not an error.
		 */
		exit(g_unsupported ? EXIT_SUCCESS : EXIT_FAILURE);
	}

	/*
	 *  Show the stressors we're going to run
	 */
	if (show_stressors() < 0) {
		free_procs();
		exit(EXIT_FAILURE);
	}

	/*
	 *  Allocate shared memory segment for shared data
	 *  across all the child stressors
	 */
	len = sizeof(shared_t) + (sizeof(proc_stats_t) * get_total_num_procs(procs_head));
	stress_map_shared(len);

	/*
	 *  Setup spinlocks
	 */
#if defined(STRESS_PERF_STATS)
	shim_pthread_spin_init(&g_shared->perf.lock, 0);
#endif
#if defined(HAVE_LIB_PTHREAD)
	shim_pthread_spin_init(&g_shared->warn_once.lock, 0);
#endif

	/*
	 *  Assign procs with shared stats memory
	 */
	setup_stats_buffers();

	/*
	 *  Allocate shared cache memory
	 */
	g_shared->mem_cache_level = DEFAULT_CACHE_LEVEL;
	(void)get_setting("cache-level", &g_shared->mem_cache_level);
	g_shared->mem_cache_ways = 0;
	(void)get_setting("cache-ways", &g_shared->mem_cache_ways);
	if (stress_cache_alloc("cache allocate") < 0) {
		stress_unmap_shared();
		free_procs();
		exit(EXIT_FAILURE);
	}

#if defined(STRESS_THERMAL_ZONES)
	/*
	 *  Setup thermal zone data
	 */
	if (g_opt_flags & OPT_FLAGS_THERMAL_ZONES)
		tz_init(&g_shared->tz_info);
#endif

	stressors_init();

	/* Start thrasher process if required */
	if (g_opt_flags & OPT_FLAGS_THRASH)
		thrash_start();

	if (g_opt_flags & OPT_FLAGS_SEQUENTIAL) {
		stress_run_sequential(&duration,
			&success, &resource_success);
	} else {
		stress_run_parallel(&duration,
			&success, &resource_success);
	}

	/* Stop thasher process */
	if (g_opt_flags & OPT_FLAGS_THRASH)
		thrash_stop();

	pr_inf("%s run completed in %.2fs%s\n",
		success ? "successful" : "unsuccessful",
		duration, duration_to_str(duration));

	/*
	 *  Save results to YAML file
	 */
	if (get_setting("yaml", &yaml_filename)) {
		yaml= fopen(yaml_filename, "w");
		if (!yaml)
			pr_err("Cannot output YAML data to %s\n", yaml_filename);

		pr_yaml(yaml, "---\n");
		pr_yaml_runinfo(yaml);
	}

	/*
	 *  Dump metrics
	 */
	if (g_opt_flags & OPT_FLAGS_METRICS)
		metrics_dump(yaml, ticks_per_sec);

#if defined(STRESS_PERF_STATS)
	/*
	 *  Dump perf statistics
	 */
	if (g_opt_flags & OPT_FLAGS_PERF_STATS)
		perf_stat_dump(yaml, procs_head, duration);
#endif

#if defined(STRESS_THERMAL_ZONES)
	/*
	 *  Dump thermal zone measurements
	 */
	if (g_opt_flags & OPT_FLAGS_THERMAL_ZONES) {
		tz_dump(yaml, procs_head);
		tz_free(&g_shared->tz_info);
	}
#endif
	/*
	 *  Dump run times
	 */
	if (g_opt_flags & OPT_FLAGS_TIMES)
		times_dump(yaml, ticks_per_sec, duration);

	/*
	 *  Tidy up
	 */
	free_procs();
	stressors_deinit();
	stress_cache_free();
	stress_unmap_shared();
	free_settings();

	/*
	 *  Close logs
	 */
	closelog();
	pr_closelog();
	if (yaml) {
		pr_yaml(yaml, "...\n");
		(void)fclose(yaml);
	}

	/*
	 *  Done!
	 */
	if (!success)
		exit(EXIT_NOT_SUCCESS);
	if (!resource_success)
		exit(EXIT_NO_RESOURCE);
	exit(EXIT_SUCCESS);
}
