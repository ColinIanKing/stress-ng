/*
 * Copyright (C) 2013-2019 Canonical, Ltd.
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

typedef struct {
	const int opt;			/* optarg option */
	const uint64_t opt_flag;	/* global options flag bit setting */
} opt_flag_t;

/* Per stressor process information */
static proc_info_t *procs_head, *procs_tail;
proc_info_t *g_proc_current;

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
	{ OPT_aggressive,	OPT_FLAGS_AGGRESSIVE_MASK },
	{ OPT_cpu_online_all,	OPT_FLAGS_CPU_ONLINE_ALL },
	{ OPT_dry_run,		OPT_FLAGS_DRY_RUN },
	{ OPT_ignite_cpu,	OPT_FLAGS_IGNITE_CPU },
	{ OPT_keep_name, 	OPT_FLAGS_KEEP_NAME },
	{ OPT_log_brief,	OPT_FLAGS_LOG_BRIEF },
	{ OPT_maximize,		OPT_FLAGS_MAXIMIZE },
	{ OPT_metrics,		OPT_FLAGS_METRICS },
	{ OPT_metrics_brief,	OPT_FLAGS_METRICS_BRIEF | OPT_FLAGS_METRICS },
	{ OPT_minimize,		OPT_FLAGS_MINIMIZE },
	{ OPT_no_rand_seed,	OPT_FLAGS_NO_RAND_SEED },
	{ OPT_oomable,		OPT_FLAGS_OOMABLE },
	{ OPT_page_in,		OPT_FLAGS_MMAP_MINCORE },
	{ OPT_pathological,	OPT_FLAGS_PATHOLOGICAL },
#if defined(STRESS_PERF_STATS) && defined(HAVE_LINUX_PERF_EVENT_H)
	{ OPT_perf_stats,	OPT_FLAGS_PERF_STATS },
#endif
	{ OPT_sock_nodelay,	OPT_FLAGS_SOCKET_NODELAY },
#if defined(HAVE_SYSLOG_H)
	{ OPT_syslog,		OPT_FLAGS_SYSLOG },
#endif
	{ OPT_thrash, 		OPT_FLAGS_THRASH },
	{ OPT_timer_slack,	OPT_FLAGS_TIMER_SLACK },
	{ OPT_times,		OPT_FLAGS_TIMES },
	{ OPT_timestamp,	OPT_FLAGS_TIMESTAMP },
	{ OPT_thermal_zones,	OPT_FLAGS_THERMAL_ZONES },
	{ OPT_verbose,		PR_ALL },
	{ OPT_verify,		OPT_FLAGS_VERIFY | PR_FAIL },
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
};

static const int ignore_signals[] = {
#if defined(SIGUSR1)
	SIGUSR1,
#endif
#if defined(SIGUSR2)
	SIGUSR2,
#endif
};

/*
 *  Declaration of stress_*_info object
 */
#define STRESSOR_DECL(name)	\
	extern stressor_info_t stress_ ## name ## _info;

STRESSORS(STRESSOR_DECL)

/*
 *  Elements in stressor array
 */
#define STRESSOR_ELEM(name)		\
{					\
	&stress_ ## name ## _info,	\
	STRESS_ ## name,		\
	OPT_ ## name,			\
	OPT_ ## name  ## _ops,		\
	# name				\
},

/*
 *  Human readable stress test names.
 */
static const stress_t stressors[] = {
	STRESSORS(STRESSOR_ELEM)
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
	{ "close",	1,	0,	OPT_close },
	{ "close-ops",	1,	0,	OPT_close_ops },
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
	{ "funcret",	1,	0,	OPT_funcret },
	{ "funcret-ops",1,	0,	OPT_funcret_ops },
	{ "funcret-method",1,	0,	OPT_funcret_method },
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
	{ "idle-page",	1,	0,	OPT_idle_page },
	{ "idle-page-ops",1,	0,	OPT_idle_page_ops },
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
	{ "ipsec-mb",	1,	0,	OPT_ipsec_mb },
	{ "ipsec-mb-ops",1,	0,	OPT_ipsec_mb_ops },
	{ "itimer",	1,	0,	OPT_itimer },
	{ "itimer-ops",	1,	0,	OPT_itimer_ops },
	{ "itimer-freq",1,	0,	OPT_itimer_freq },
	{ "itimer-rand",0,	0,	OPT_itimer_rand },
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
	{ "loop",	1,	0,	OPT_loop },
	{ "loop-ops",	1,	0,	OPT_loop_ops },
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
	{ "matrix-3d",	1,	0,	OPT_matrix_3d },
	{ "matrix-3d-ops",1,	0,	OPT_matrix_3d_ops },
	{ "matrix-3d-method",1,	0,	OPT_matrix_3d_method },
	{ "matrix-3d-size",1,	0,	OPT_matrix_3d_size },
	{ "matrix-3d-zyx",0,	0,	OPT_matrix_3d_zyx },
	{ "maximize",	0,	0,	OPT_maximize },
	{ "mcontend",	1,	0,	OPT_mcontend },
	{ "mcontend-ops",1,	0,	OPT_mcontend_ops },
	{ "membarrier",	1,	0,	OPT_membarrier },
	{ "membarrier-ops",1,	0,	OPT_membarrier_ops },
	{ "memcpy",	1,	0,	OPT_memcpy },
	{ "memcpy-ops",	1,	0,	OPT_memcpy_ops },
	{ "memcpy-method",1,	0,	OPT_memcpy_method },
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
	{ "mlockmany",	1,	0,	OPT_mlockmany },
	{ "mlockmany-ops",1,	0,	OPT_mlockmany_ops },
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
	{ "msg-types",	1,	0,	OPT_msg_types },
	{ "msync",	1,	0,	OPT_msync },
	{ "msync-ops",	1,	0,	OPT_msync_ops },
	{ "msync-bytes",1,	0,	OPT_msync_bytes },
	{ "netdev",	1,	0,	OPT_netdev },
	{ "netdev-ops",1,	0,	OPT_netdev_ops },
	{ "netlink-proc",1,	0,	OPT_netlink_proc },
	{ "netlink-proc-ops",1,	0,	OPT_netlink_proc_ops },
	{ "netlink-task",1,	0,	OPT_netlink_task },
	{ "netlink-task-ops",1,	0,	OPT_netlink_task_ops },
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
	{ "opcode-method",1,	0,	OPT_opcode_method },
	{ "open",	1,	0,	OPT_open },
	{ "open-ops",	1,	0,	OPT_open_ops },
	{ "page-in",	0,	0,	OPT_page_in },
	{ "parallel",	1,	0,	OPT_all },
	{ "pathological",0,	0,	OPT_pathological },
#if defined(STRESS_PERF_STATS) && defined(HAVE_LINUX_PERF_EVENT_H)
	{ "perf",	0,	0,	OPT_perf_stats },
#endif
	{ "personality",1,	0,	OPT_personality },
	{ "personality-ops",1,	0,	OPT_personality_ops },
	{ "physpage",	1,	0,	OPT_physpage },
	{ "physpage-ops",1,	0,	OPT_physpage_ops },
	{ "pidfd",	1,	0,	OPT_pidfd },
	{ "pidfd-ops",	1,	0,	OPT_pidfd_ops },
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
	{ "rawsock",	1,	0,	OPT_rawsock },
	{ "rawsock-ops",1,	0,	OPT_rawsock_ops },
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
	{ "shellsort",	1,	0,	OPT_shellsort },
	{ "shellsort-ops",1,	0,	OPT_shellsort_ops },
	{ "shellsort-size",1,	0,	OPT_shellsort_integers },
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
	{ "switch-freq",1,	0,	OPT_switch_freq },
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
#if defined(HAVE_SYSLOG_H)
	{ "syslog",	0,	0,	OPT_syslog },
#endif
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
	{ "tmpfs-mmap-async",0,	0,	OPT_tmpfs_mmap_async },
	{ "tmpfs-mmap-file",0,	0,	OPT_tmpfs_mmap_file },
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
	{ "vdso",	1,	0,	OPT_vdso },
	{ "vdso-ops",	1,	0,	OPT_vdso_ops },
	{ "vdso-func",	1,	0,	OPT_vdso_func },
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
#if defined(STRESS_PERF_STATS) && defined(HAVE_LINUX_PERF_EVENT_H)
	{ NULL,		"perf",			"display perf statistics" },
#endif
	{ "q",		"quiet",		"quiet output" },
	{ "r",		"random N",		"start N random workers" },
	{ NULL,		"sched type",		"set scheduler type" },
	{ NULL,		"sched-prio N",		"set scheduler priority level N" },
	{ NULL,		"sequential N",		"run all stressors one by one, invoking N of them" },
	{ NULL,		"stressors",		"show available stress tests" },
#if defined(HAVE_SYSLOG_H)
	{ NULL,		"syslog",		"log messages to the syslog" },
#endif
	{ NULL,		"taskset",		"use specific CPUs (set CPU affinity)" },
	{ NULL,		"temp-path path",	"specify path for temporary directories and files" },
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
	{ "Y",		"yaml file",		"output results to YAML formatted filed" },
	{ "x",		"list",			"list of stressors to exclude (not run)" },
	{ NULL,		NULL,			NULL }
};

/*
 *  stressor_name_find()
 *  	Find index into stressors by name
 */
static inline int32_t stressor_name_find(const char *name)
{
	int32_t i;
	const char *tmp = stress_munge_underscore(name);
	const size_t len = strlen(tmp) + 1;
	char munged_name[len];

	(void)shim_strlcpy(munged_name, tmp, len);

	for (i = 0; stressors[i].name; i++) {
		const char *munged_stressor_name =
			stress_munge_underscore(stressors[i].name);

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
	free(pi);
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
							(void)printf(" %s", stress_munge_underscore(stressors[j].name));
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
static void MLOCKED_TEXT stress_sigint_handler(int signum)
{
	(void)signum;
	g_caught_sigint = true;
	g_keep_stressing_flag = false;
	wait_flag = false;

	(void)kill(-getpid(), SIGALRM);
}

/*
 *  stress_sigalrm_parent_handler()
 *	handle signal in parent process, don't block on waits
 */
static void MLOCKED_TEXT stress_sigalrm_parent_handler(int signum)
{
	(void)signum;
	wait_flag = false;
}

#if defined(SIGUSR2)
/*
 *  stress_stats_handler()
 *	dump current system stats
 */
static void MLOCKED_TEXT stress_stats_handler(int signum)
{
	static char buffer[80];
	char *ptr = buffer;
	int ret;
	double min1, min5, min15;
	size_t shmall, freemem, totalmem, freeswap;

	(void)signum;

	*ptr = '\0';

	if (stress_get_load_avg(&min1, &min5, &min15) == 0) {
		ret = snprintf(ptr, sizeof(buffer),
			"Load Avg: %.2f %.2f %.2f, ",
			min1, min5, min15);
		if (ret > 0)
			ptr += ret;
	}
	stress_get_memlimits(&shmall, &freemem, &totalmem, &freeswap);

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
	(void)printf("%s, version " VERSION " (%s, %s) \U0001F4BB\U0001F525\n",
		g_app_name, stress_get_compiler(), stress_get_uname_info());
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
 *  usage_help_stressors()
 *	show per stressor help information
 */
static void usage_help_stressors(void)
{
	size_t i;

	for (i = 0; stressors[i].id != STRESS_MAX; i++) {
		if (stressors[i].info->help)
			usage_help(stressors[i].info->help);
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
			stress_munge_underscore(stressors[i].name));
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
	usage_help_stressors();
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

#if defined(HAVE_SCHED_GETAFFINITY) && NEED_GLIBC(2,3,0)
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
				const char *stressor_name = stress_munge_underscore(pi->stressor->name);

				ret = shim_waitpid(pid, &status, 0);
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
							ret, stressor_name);
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

	switch(signum) {
	case SIGILL:
	case SIGSEGV:
	case SIGFPE:
	case SIGBUS:
		fprintf(stderr, "%s: info:  [%d] terminated with unexpected signal %s\n",
			g_app_name, (int)getpid(), stress_strsignal(signum));
		fflush(stderr);
		_exit(EXIT_SIGNALED);
		break;
	default:
		break;
	}
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
		for (g_proc_current = procs_list; g_proc_current; g_proc_current = g_proc_current->next) {
			if (g_opt_timeout && (time_now() - time_start > g_opt_timeout))
				goto abort;

			j = g_proc_current->started_procs;

			if (j < g_proc_current->num_procs) {
				int rc = EXIT_SUCCESS;
				pid_t pid;
				char name[64];
				int64_t backoff = DEFAULT_BACKOFF;
				int32_t ionice_class = UNDEFINED;
				int32_t ionice_level = UNDEFINED;

				(void)get_setting("backoff", &backoff);
				(void)get_setting("ionice-class", &ionice_class);
				(void)get_setting("ionice-level", &ionice_level);

				proc_stats_t *stats = g_proc_current->stats[j];
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
						stress_munge_underscore(g_proc_current->stressor->name));
					set_oom_adjustment(name, false);
					set_max_limits();
					set_iopriority(ionice_class, ionice_level);
					stress_set_proc_name(name);
					(void)umask(0077);

					pr_dbg("%s: started [%d] (instance %" PRIu32 ")\n",
						name, (int)getpid(), j);

					stats->start = stats->finish = time_now();
#if defined(STRESS_PERF_STATS) && defined(HAVE_LINUX_PERF_EVENT_H)
					if (g_opt_flags & OPT_FLAGS_PERF_STATS)
						(void)perf_open(&stats->sp);
#endif
					(void)shim_usleep(backoff * n_procs);
#if defined(STRESS_PERF_STATS) && defined(HAVE_LINUX_PERF_EVENT_H)
					if (g_opt_flags & OPT_FLAGS_PERF_STATS)
						(void)perf_enable(&stats->sp);
#endif
					if (g_keep_stressing_flag && !(g_opt_flags & OPT_FLAGS_DRY_RUN)) {
						const args_t args = {
							.counter = &stats->counter,
							.name = name,
							.max_ops = g_proc_current->bogo_ops,
							.instance = j,
							.num_instances = g_proc_current->num_procs,
							.pid = getpid(),
							.ppid = getppid(),
							.page_size = stress_get_pagesize(),
						};

						rc = g_proc_current->stressor->info->stressor(&args);
						pr_fail_check(&rc);
						stats->run_ok = (rc == EXIT_SUCCESS);
					}
#if defined(STRESS_PERF_STATS) && defined(HAVE_LINUX_PERF_EVENT_H)
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
						g_proc_current->pids[j] = pid;
						g_proc_current->started_procs++;
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
					stress_munge_underscore(pi->stressor->name));
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
		char *munged = stress_munge_underscore(pi->stressor->name);
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
#if defined(HAVE_SYSLOG_H)
	syslog(LOG_INFO, "invoked with '%s' by user %d", buf, getuid());
#endif
	free(buf);
}

/*
 *  log_system_mem_info()
 *	dump system memory info
 */
void log_system_mem_info(void)
{
#if defined(HAVE_SYS_SYSINFO_H) && \
    defined(HAVE_SYSINFO) && \
    defined(HAVE_SYSLOG_H)
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
#if defined(HAVE_UNAME) && 		\
    defined(HAVE_SYS_UTSNAME_H) &&	\
    defined(HAVE_SYSLOG_H)
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
 *	mmap shared region, with an extra page at the end
 *	that is marked read-only to stop accidental smashing
 *	from a run-away stack expansion
 */
static inline void stress_map_shared(const size_t len)
{
	const size_t page_size = stress_get_pagesize();
	const size_t sz = (len + (page_size << 1)) & ~(page_size - 1);
#if defined(HAVE_MPROTECT)
	void *last_page;
#endif

	g_shared = (shared_t *)mmap(NULL, sz, PROT_READ | PROT_WRITE,
		MAP_SHARED | MAP_ANON, -1, 0);
	if (g_shared == MAP_FAILED) {
		pr_err("Cannot mmap to shared memory region: errno=%d (%s)\n",
			errno, strerror(errno));
		free_procs();
		exit(EXIT_FAILURE);
	}

	/* Paraniod */
	(void)memset(g_shared, 0, sz);
	g_shared->length = sz;

#if defined(HAVE_MPROTECT)
	last_page = ((uint8_t *)g_shared) + sz - page_size;

	/* Make last page trigger a segfault if it is accessed */
	(void)mprotect(last_page, page_size, PROT_NONE);
#elif defined(HAVE_MREMAP) && defined(MAP_FIXED)
	{
		void *new_last_page;

		/* Try to remap last page with PROT_NONE */
		(void)munmap(last_page, page_size);
		new_last_page = mmap(last_page, page_size, PROT_NONE,
			MAP_SHARED | MAP_ANON | MAP_FIXED, -1, 0);

		/* Failed, retry read-only */
		if (new_last_page == MAP_FAILED)
			new_last_page = mmap(last_page, page_size, PROT_READ,
				MAP_SHARED | MAP_ANON | MAP_FIXED, -1, 0);
		/* Can't remap, bump length down a page */
		if (new_last_page == MAP_FAILED)
			g_shared->length -= sz;
	}
#endif
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
						stress_munge_underscore(pi->stressor->name));
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

		opterr = (!jobmode)? opterr: 0;
next_opt:
		if ((c = getopt_long(argc, argv, "?khMVvqnt:b:c:i:j:m:d:f:s:l:p:P:C:S:a:y:F:D:T:u:o:r:B:R:Y:x:",
			long_options, &option_index)) == -1) {
			break;
		}

		for (i = 0; stressors[i].id != STRESS_MAX; i++) {
			if (stressors[i].short_getopt == c) {
				const char *name = opt_name(c);
				proc_info_t *pi = find_proc_info(&stressors[i]);
				g_proc_current = pi;

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
				if (g_proc_current)
					g_proc_current->bogo_ops = bogo_ops;
				goto next_opt;
			}
			if (stressors[i].info->opt_set_funcs) {
				size_t j;
				const stressor_info_t *info = stressors[i].info;

				for (j = 0; info->opt_set_funcs[j].opt_set_func; j++) {
					if (info->opt_set_funcs[j].opt == c) {
						ret = info->opt_set_funcs[j].opt_set_func(optarg);
						if (ret < 0)
							return EXIT_FAILURE;
						goto next_opt;
					}
				}
			}
		}

		for (i = 0; i < SIZEOF_ARRAY(opt_flags); i++) {
			if (c == opt_flags[i].opt) {
				g_opt_flags |= opt_flags[i].opt_flag;
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
		case OPT_taskset:
			if (stress_set_cpu_affinity(optarg) < 0)
				exit(EXIT_FAILURE);
			break;
		case OPT_temp_path:
			if (stress_set_temp_path(optarg) < 0)
				exit(EXIT_FAILURE);
			break;
		case OPT_timeout:
			g_opt_timeout = get_uint64_time(optarg);
			break;
		case OPT_timer_slack:
			(void)stress_set_timer_slack_ns(optarg);
			break;
		case OPT_version:
			version();
			exit(EXIT_SUCCESS);
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
	size_t i;
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
	(void)get_setting("class", &class);
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
#if defined(HAVE_SYSLOG_H)
	openlog("stress-ng", 0, LOG_USER);
#endif
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

#if defined(STRESS_PERF_STATS) && defined(HAVE_LINUX_PERF_EVENT_H)
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
	for (i = 0; i < SIZEOF_ARRAY(terminate_signals); i++) {
		if (stress_sighandler("stress-ng", terminate_signals[i], handle_terminate, NULL) < 0)
			exit(EXIT_FAILURE);
	}
	/*
	 *  Ignore other signals
	 */
	for (i = 0; i < SIZEOF_ARRAY(ignore_signals); i++) {
		ret = stress_sighandler("stress-ng", ignore_signals[i], SIG_IGN, NULL);
		(void)ret;	/* We don't care if it fails */
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
#if defined(STRESS_PERF_STATS) && defined(HAVE_LINUX_PERF_EVENT_H)
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
		yaml = fopen(yaml_filename, "w");
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

#if defined(STRESS_PERF_STATS) && defined(HAVE_LINUX_PERF_EVENT_H)
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
	stressors_deinit();
	free_procs();
	stress_cache_free();
	stress_unmap_shared();
	free_settings();

	/*
	 *  Close logs
	 */
#if defined(HAVE_SYSLOG_H)
	closelog();
#endif
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
