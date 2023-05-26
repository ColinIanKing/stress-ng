/*
 * Copyright (C) 2013-2021 Canonical, Ltd.
 * Copyright (C) 2021-2023 Colin Ian King
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
 */
#include "stress-ng.h"
#include "core-builtin.h"
#include "core-ftrace.h"
#include "core-hash.h"
#include "core-perf.h"
#include "core-pragma.h"
#include "core-put.h"
#include "core-smart.h"
#include "core-stressors.h"
#include "core-syslog.h"
#include "core-thermal-zone.h"
#include "core-thrash.h"

#if defined(HAVE_SYS_UTSNAME_H)
#include <sys/utsname.h>
#endif

#if defined(HAVE_SYSLOG_H)
#include <syslog.h>
#endif

#if defined(HAVE_LINUX_FS_H)
#include <linux/fs.h>
#endif

#include <float.h>

/* Stress test classes */
typedef struct {
	stress_class_t class;		/* Class type bit mask */
	const char *name;		/* Name of class */
} stress_class_info_t;

typedef struct {
	const int opt;			/* optarg option */
	const uint64_t opt_flag;	/* global options flag bit setting */
} stress_opt_flag_t;

/* Per stressor information */
static stress_stressor_t *stressors_head, *stressors_tail;
stress_stressor_t *g_stressor_current;

/* Various option settings and flags */
static volatile bool wait_flag = true;		/* false = exit run wait loop */
static int terminate_signum;			/* signal sent to process */
static pid_t main_pid;				/* stress-ng main pid */

/* Globals */
int32_t g_opt_sequential = DEFAULT_SEQUENTIAL;	/* # of sequential stressors */
int32_t g_opt_parallel = DEFAULT_PARALLEL;	/* # of parallel stressors */
uint64_t g_opt_timeout = TIMEOUT_NOT_SET;	/* timeout in seconds */
uint64_t g_opt_flags = PR_ERROR | PR_INFO | OPT_FLAGS_MMAP_MADVISE;
volatile bool g_keep_stressing_flag = true;	/* false to exit stressor */
const char g_app_name[] = "stress-ng";		/* Name of application */
stress_shared_t *g_shared;			/* shared memory */
jmp_buf g_error_env;				/* parsing error env */
stress_put_val_t g_put_val;			/* sync data to somewhere */

#if defined(SA_SIGINFO)
typedef struct {
	int	code;
	pid_t	pid;
	uid_t	uid;
	struct timeval when;
	bool 	triggered;
} stress_sigalrm_info_t;

stress_sigalrm_info_t g_sigalrm_info;
#endif

static void stress_kill_stressors(const int sig, const bool force_sigkill);
/*
 *  optarg option to global setting option flags
 */
static const stress_opt_flag_t opt_flags[] = {
	{ OPT_abort,		OPT_FLAGS_ABORT },
	{ OPT_aggressive,	OPT_FLAGS_AGGRESSIVE_MASK },
	{ OPT_cpu_online_all,	OPT_FLAGS_CPU_ONLINE_ALL },
	{ OPT_dry_run,		OPT_FLAGS_DRY_RUN },
	{ OPT_ftrace,		OPT_FLAGS_FTRACE },
	{ OPT_ignite_cpu,	OPT_FLAGS_IGNITE_CPU },
	{ OPT_keep_files, 	OPT_FLAGS_KEEP_FILES },
	{ OPT_keep_name, 	OPT_FLAGS_KEEP_NAME },
	{ OPT_klog_check,	OPT_FLAGS_KLOG_CHECK },
	{ OPT_log_brief,	OPT_FLAGS_LOG_BRIEF },
	{ OPT_log_lockless,	OPT_FLAGS_LOG_LOCKLESS },
	{ OPT_maximize,		OPT_FLAGS_MAXIMIZE },
	{ OPT_metrics,		OPT_FLAGS_METRICS },
	{ OPT_metrics_brief,	OPT_FLAGS_METRICS_BRIEF | OPT_FLAGS_METRICS },
	{ OPT_minimize,		OPT_FLAGS_MINIMIZE },
	{ OPT_no_oom_adjust,	OPT_FLAGS_NO_OOM_ADJUST },
	{ OPT_no_rand_seed,	OPT_FLAGS_NO_RAND_SEED },
	{ OPT_oomable,		OPT_FLAGS_OOMABLE },
	{ OPT_oom_avoid,	OPT_FLAGS_OOM_AVOID },
	{ OPT_page_in,		OPT_FLAGS_MMAP_MINCORE },
	{ OPT_pathological,	OPT_FLAGS_PATHOLOGICAL },
#if defined(STRESS_PERF_STATS) && 	\
    defined(HAVE_LINUX_PERF_EVENT_H)
	{ OPT_perf_stats,	OPT_FLAGS_PERF_STATS },
#endif
	{ OPT_skip_silent,	OPT_FLAGS_SKIP_SILENT },
	{ OPT_smart,		OPT_FLAGS_SMART },
	{ OPT_sn,		OPT_FLAGS_SN },
	{ OPT_sock_nodelay,	OPT_FLAGS_SOCKET_NODELAY },
	{ OPT_stderr,		OPT_FLAGS_STDERR },
	{ OPT_stdout,		OPT_FLAGS_STDOUT },
#if defined(HAVE_SYSLOG_H)
	{ OPT_syslog,		OPT_FLAGS_SYSLOG },
#endif
	{ OPT_thrash, 		OPT_FLAGS_THRASH },
	{ OPT_times,		OPT_FLAGS_TIMES },
	{ OPT_timestamp,	OPT_FLAGS_TIMESTAMP },
	{ OPT_thermal_zones,	OPT_FLAGS_THERMAL_ZONES | OPT_FLAGS_TZ_INFO },
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
#if defined(SIGILL)
	SIGILL,
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
#if defined(SIGTTOU)
	SIGTTOU,
#endif
#if defined(SIGTTIN)
	SIGTTIN,
#endif
#if defined(SIGWINCH)
	SIGWINCH,
#endif
};

/* Stressor id values */
enum {
	STRESS_START = -1,
	STRESSORS(STRESSOR_ENUM)
	/* STRESS_MAX must be last one */
	STRESS_MAX
};

/* Stressor extern info structs */
STRESSORS(STRESSOR_INFO)

/*
 *  Human readable stress test names.
 */
static const stress_t stressors[] = {
	STRESSORS(STRESSOR_ELEM)
	{ NULL, STRESS_MAX, 0, OPT_undefined, NULL }
};

STRESS_ASSERT(SIZEOF_ARRAY(stressors) != STRESS_MAX)

/*
 *  Different stress classes
 */
static const stress_class_info_t classes[] = {
	{ CLASS_CPU_CACHE,	"cpu-cache" },
	{ CLASS_CPU,		"cpu" },
	{ CLASS_DEV,		"device" },
	{ CLASS_FILESYSTEM,	"filesystem" },
	{ CLASS_GPU,		"gpu" },
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
 *  GNU "long options" command line options
 */
static const struct option long_options[] = {
	{ "abort",		0,	0,	OPT_abort },
	{ "access",		1,	0,	OPT_access },
	{ "access-ops",		1,	0,	OPT_access_ops },
	{ "af-alg",		1,	0,	OPT_af_alg },
	{ "af-alg-dump",	0,	0,	OPT_af_alg_dump },
	{ "af-alg-ops",		1,	0,	OPT_af_alg_ops },
	{ "affinity",		1,	0,	OPT_affinity },
	{ "affinity-delay",	1,	0,	OPT_affinity_delay },
	{ "affinity-ops",	1,	0,	OPT_affinity_ops },
	{ "affinity-pin",	0,	0,	OPT_affinity_pin },
	{ "affinity-rand",	0,	0,	OPT_affinity_rand },
	{ "affinity-sleep",	1,	0,	OPT_affinity_sleep },
	{ "aggressive",		0,	0,	OPT_aggressive },
	{ "aio",		1,	0,	OPT_aio },
	{ "aio-ops",		1,	0,	OPT_aio_ops },
	{ "aio-requests",	1,	0,	OPT_aio_requests },
	{ "aiol",		1,	0,	OPT_aiol},
	{ "aiol-ops",		1,	0,	OPT_aiol_ops },
	{ "aiol-requests",	1,	0,	OPT_aiol_requests },
	{ "alarm",		1,	0,	OPT_alarm },
	{ "alarm-ops",		1,	0,	OPT_alarm_ops },
	{ "all",		1,	0,	OPT_all },
	{ "apparmor",		1,	0,	OPT_apparmor },
	{ "apparmor-ops",	1,	0,	OPT_apparmor_ops },
	{ "atomic",		1,	0,	OPT_atomic },
	{ "atomic-ops",		1,	0,	OPT_atomic_ops },
	{ "bad-altstack",	1,	0,	OPT_bad_altstack },
	{ "bad-altstack-ops",	1,	0,	OPT_bad_altstack_ops },
	{ "bad-ioctl",		1,	0,	OPT_bad_ioctl },
	{ "bad-ioctl-ops",	1,	0,	OPT_bad_ioctl_ops },
	{ "backoff",		1,	0,	OPT_backoff },
	{ "bigheap",		1,	0,	OPT_bigheap },
	{ "bigheap-growth",	1,	0,	OPT_bigheap_growth },
	{ "bigheap-ops",	1,	0,	OPT_bigheap_ops },
	{ "bind-mount",		1,	0,	OPT_bind_mount },
	{ "bind-mount-ops",	1,	0,	OPT_bind_mount_ops },
	{ "binderfs",		1,	0,	OPT_binderfs },
	{ "binderfs-opts",	1,	0,	OPT_binderfs_ops },
	{ "branch",		1,	0,	OPT_branch },
	{ "branch-ops",		1,	0,	OPT_branch_ops },
	{ "brk",		1,	0,	OPT_brk },
	{ "brk-mlock",		0,	0,	OPT_brk_mlock },
	{ "brk-notouch",	0,	0,	OPT_brk_notouch },
	{ "brk-ops",		1,	0,	OPT_brk_ops },
	{ "bsearch",		1,	0,	OPT_bsearch },
	{ "bsearch-ops",	1,	0,	OPT_bsearch_ops },
	{ "bsearch-size",	1,	0,	OPT_bsearch_size },
	{ "cache",		1,	0, 	OPT_cache },
	{ "cache-cldemote",	0,	0,	OPT_cache_cldemote },
	{ "cache-clflushopt",	0,	0,	OPT_cache_clflushopt },
	{ "cache-clwb",		0,	0,	OPT_cache_clwb },
	{ "cache-enable-all",	0,	0,	OPT_cache_enable_all },
	{ "cache-fence",	0,	0,	OPT_cache_fence },
	{ "cache-flush",	0,	0,	OPT_cache_flush },
	{ "cache-level",	1,	0,	OPT_cache_level },
	{ "cache-no-affinity",	0,	0,	OPT_cache_no_affinity },
	{ "cache-ops",		1,	0,	OPT_cache_ops },
	{ "cache-prefetch",	0,	0,	OPT_cache_prefetch },
	{ "cache-sfence",	0,	0,	OPT_cache_sfence },
	{ "cache-ways",		1,	0,	OPT_cache_ways },
	{ "cacheline",		1,	0, 	OPT_cacheline },
	{ "cacheline-affinity",	0,	0,	OPT_cacheline_affinity },
	{ "cacheline-method",	1,	0,	OPT_cacheline_method },
	{ "cacheline-ops",	1,	0,	OPT_cacheline_ops },
	{ "cap",		1,	0, 	OPT_cap },
	{ "cap-ops",		1,	0, 	OPT_cap_ops },
	{ "chattr",		1,	0, 	OPT_chattr },
	{ "chattr-ops",		1,	0,	OPT_chattr_ops },
	{ "chdir",		1,	0, 	OPT_chdir },
	{ "chdir-dirs",		1,	0,	OPT_chdir_dirs },
	{ "chdir-ops",		1,	0, 	OPT_chdir_ops },
	{ "chmod",		1,	0, 	OPT_chmod },
	{ "chmod-ops",		1,	0,	OPT_chmod_ops },
	{ "chown",		1,	0, 	OPT_chown},
	{ "chown-ops",		1,	0,	OPT_chown_ops },
	{ "chroot",		1,	0, 	OPT_chroot},
	{ "chroot-ops",		1,	0,	OPT_chroot_ops },
	{ "class",		1,	0,	OPT_class },
	{ "clock",		1,	0,	OPT_clock },
	{ "clock-ops",		1,	0,	OPT_clock_ops },
	{ "clone",		1,	0,	OPT_clone },
	{ "clone-max",		1,	0,	OPT_clone_max },
	{ "clone-ops",		1,	0,	OPT_clone_ops },
	{ "close",		1,	0,	OPT_close },
	{ "close-ops",		1,	0,	OPT_close_ops },
	{ "context",		1,	0,	OPT_context },
	{ "context-ops",	1,	0,	OPT_context_ops },
	{ "copy-file",		1,	0,	OPT_copy_file },
	{ "copy-file-bytes",	1,	0,	OPT_copy_file_bytes },
	{ "copy-file-ops",	1,	0,	OPT_copy_file_ops },
	{ "cpu",		1,	0,	OPT_cpu },
	{ "cpu-ops",		1,	0,	OPT_cpu_ops },
	{ "cpu-load",		1,	0,	OPT_cpu_load },
	{ "cpu-load-slice",	1,	0,	OPT_cpu_load_slice },
	{ "cpu-method",		1,	0,	OPT_cpu_method },
	{ "cpu-online",		1,	0,	OPT_cpu_online },
	{ "cpu-online-all",	0,	0,	OPT_cpu_online_all },
	{ "cpu-online-ops",	1,	0,	OPT_cpu_online_ops },
	{ "crypt",		1,	0,	OPT_crypt },
	{ "crypt-ops",		1,	0,	OPT_crypt_ops },
	{ "cyclic",		1,	0,	OPT_cyclic },
	{ "cyclic-dist",	1,	0,	OPT_cyclic_dist },
	{ "cyclic-method",	1,	0,	OPT_cyclic_method },
	{ "cyclic-ops",		1,	0,	OPT_cyclic_ops },
	{ "cyclic-policy",	1,	0,	OPT_cyclic_policy },
	{ "cyclic-prio",	1,	0,	OPT_cyclic_prio },
	{ "cyclic-samples",	1,	0,	OPT_cyclic_samples },
	{ "cyclic-sleep",	1,	0,	OPT_cyclic_sleep },
	{ "daemon",		1,	0,	OPT_daemon },
	{ "daemon-ops",		1,	0,	OPT_daemon_ops },
	{ "dccp",		1,	0,	OPT_dccp },
	{ "dccp-domain",	1,	0,	OPT_dccp_domain },
	{ "dccp-if",		1,	0,	OPT_dccp_if },
	{ "dccp-ops",		1,	0,	OPT_dccp_ops },
	{ "dccp-opts",		1,	0,	OPT_dccp_opts },
	{ "dccp-port",		1,	0,	OPT_dccp_port },
	{ "dekker",		1,	0,	OPT_dekker },
	{ "dekker-ops",		1,	0,	OPT_dekker_ops },
	{ "dentry",		1,	0,	OPT_dentry },
	{ "dentry-ops",		1,	0,	OPT_dentry_ops },
	{ "dentries",		1,	0,	OPT_dentries },
	{ "dentry-order",	1,	0,	OPT_dentry_order },
	{ "dev",		1,	0,	OPT_dev },
	{ "dev-file",		1,	0,	OPT_dev_file },
	{ "dev-ops",		1,	0,	OPT_dev_ops },
	{ "dev-shm",		1,	0,	OPT_dev_shm },
	{ "dev-shm-ops",	1,	0,	OPT_dev_shm_ops },
	{ "dir",		1,	0,	OPT_dir },
	{ "dir-dirs",		1,	0,	OPT_dir_dirs },
	{ "dir-ops",		1,	0,	OPT_dir_ops },
	{ "dirdeep",		1,	0,	OPT_dirdeep },
	{ "dirdeep-bytes",	1,	0,	OPT_dirdeep_bytes },
	{ "dirdeep-dirs",	1,	0,	OPT_dirdeep_dirs },
	{ "dirdeep-files",	1,	0,	OPT_dirdeep_files },
	{ "dirdeep-inodes",	1,	0,	OPT_dirdeep_inodes },
	{ "dirdeep-ops",	1,	0,	OPT_dirdeep_ops },
	{ "dirmany",		1,	0,	OPT_dirmany },
	{ "dirmany-bytes",	1,	0,	OPT_dirmany_bytes },
	{ "dirmany-ops",	1,	0,	OPT_dirmany_ops },
	{ "dry-run",		0,	0,	OPT_dry_run },
	{ "dnotify",		1,	0,	OPT_dnotify },
	{ "dnotify-ops",	1,	0,	OPT_dnotify_ops },
	{ "dup",		1,	0,	OPT_dup },
	{ "dup-ops",		1,	0,	OPT_dup_ops },
	{ "dynlib",		1,	0,	OPT_dynlib },
	{ "dynlib-ops",		1,	0,	OPT_dynlib_ops },
	{ "efivar",		1,	0,	OPT_efivar },
	{ "efivar-ops",		1,	0,	OPT_efivar_ops },
	{ "enosys",		1,	0,	OPT_enosys },
	{ "enosys-ops",		1,	0,	OPT_enosys_ops },
	{ "env",		1,	0,	OPT_env },
	{ "env-ops",		1,	0,	OPT_env_ops },
	{ "epoll",		1,	0,	OPT_epoll },
	{ "epoll-domain",	1,	0,	OPT_epoll_domain },
	{ "epoll-ops",		1,	0,	OPT_epoll_ops },
	{ "epoll-port",		1,	0,	OPT_epoll_port },
	{ "epoll-sockets",	1,	0,	OPT_epoll_sockets },
	{ "eventfd",		1,	0,	OPT_eventfd },
	{ "eventfd-nonblock",	0,	0,	OPT_eventfd_nonblock },
	{ "eventfd-ops",	1,	0,	OPT_eventfd_ops },
	{ "exclude",		1,	0,	OPT_exclude },
	{ "exec",		1,	0,	OPT_exec },
	{ "exec-fork-method",	1,	0,	OPT_exec_fork_method },
	{ "exec-max",		1,	0,	OPT_exec_max },
	{ "exec-method",	1,	0,	OPT_exec_method },
	{ "exec-no-pthread",	0,	0,	OPT_exec_no_pthread },
	{ "exec-ops",		1,	0,	OPT_exec_ops },
	{ "exit-group",		1,	0,	OPT_exit_group },
	{ "exit-group-ops",	1,	0,	OPT_exit_group_ops },
	{ "fallocate",		1,	0,	OPT_fallocate },
	{ "fallocate-bytes",	1,	0,	OPT_fallocate_bytes },
	{ "fallocate-ops",	1,	0,	OPT_fallocate_ops },
	{ "fanotify",		1,	0,	OPT_fanotify },
	{ "fanotify-ops",	1,	0,	OPT_fanotify_ops },
	{ "far-branch",		1,	0,	OPT_far_branch },
	{ "far-branch-ops",	1,	0,	OPT_far_branch_ops },
	{ "fault",		1,	0,	OPT_fault },
	{ "fault-ops",		1,	0,	OPT_fault_ops },
	{ "fcntl",		1,	0,	OPT_fcntl},
	{ "fcntl-ops",		1,	0,	OPT_fcntl_ops },
	{ "fiemap",		1,	0,	OPT_fiemap },
	{ "fiemap-bytes",	1,	0,	OPT_fiemap_bytes },
	{ "fiemap-ops",		1,	0,	OPT_fiemap_ops },
	{ "fifo",		1,	0,	OPT_fifo },
	{ "fifo-data-size",	1,	0,	OPT_fifo_data_size },
	{ "fifo-ops",		1,	0,	OPT_fifo_ops },
	{ "fifo-readers",	1,	0,	OPT_fifo_readers },
	{ "file-ioctl",		1,	0,	OPT_file_ioctl },
	{ "file-ioctl-ops",	1,	0,	OPT_file_ioctl_ops },
	{ "filename",		1,	0,	OPT_filename },
	{ "filename-ops",	1,	0,	OPT_filename_ops },
	{ "filename-opts",	1,	0,	OPT_filename_opts },
	{ "flock",		1,	0,	OPT_flock },
	{ "flock-ops",		1,	0,	OPT_flock_ops },
	{ "flushcache",		1,	0,	OPT_flushcache },
	{ "flushcache-ops",	1,	0,	OPT_flushcache_ops },
	{ "fma",		1,	0,	OPT_fma },
	{ "fma-ops",		1,	0,	OPT_fma_ops },
	{ "fork",		1,	0,	OPT_fork },
	{ "fork-max",		1,	0,	OPT_fork_max },
	{ "fork-ops",		1,	0,	OPT_fork_ops },
	{ "fork-vm",		0,	0,	OPT_fork_vm },
	{ "forkheavy",		1,	0,	OPT_forkheavy },
	{ "forkheavy-allocs",	1,	0,	OPT_forkheavy_allocs },
	{ "forkheavy-ops",	1,	0,	OPT_forkheavy_ops },
	{ "forkheavy-procs",	1,	0,	OPT_forkheavy_procs },
	{ "fp",			1,	0,	OPT_fp },
	{ "fp-method",		1,	0,	OPT_fp_method },
	{ "fp-ops",		1,	0,	OPT_fp_ops },
	{ "fp-error",		1,	0,	OPT_fp_error},
	{ "fp-error-ops",	1,	0,	OPT_fp_error_ops },
	{ "fpunch",		1,	0,	OPT_fpunch },
	{ "fpunch-ops",		1,	0,	OPT_fpunch_ops },
	{ "fsize",		1,	0,	OPT_fsize },
	{ "fsize-ops",		1,	0,	OPT_fsize_ops },
	{ "fstat",		1,	0,	OPT_fstat },
	{ "fstat-dir",		1,	0,	OPT_fstat_dir },
	{ "fstat-ops",		1,	0,	OPT_fstat_ops },
	{ "ftrace",		0,	0,	OPT_ftrace },
	{ "full",		1,	0,	OPT_full },
	{ "full-ops",		1,	0,	OPT_full_ops },
	{ "funccall",		1,	0,	OPT_funccall },
	{ "funccall-method",	1,	0,	OPT_funccall_method },
	{ "funccall-ops",	1,	0,	OPT_funccall_ops },
	{ "funcret",		1,	0,	OPT_funcret },
	{ "funcret-method",	1,	0,	OPT_funcret_method },
	{ "funcret-ops",	1,	0,	OPT_funcret_ops },
	{ "futex",		1,	0,	OPT_futex },
	{ "futex-ops",		1,	0,	OPT_futex_ops },
	{ "get",		1,	0,	OPT_get },
	{ "get-ops",		1,	0,	OPT_get_ops },
	{ "getrandom",		1,	0,	OPT_getrandom },
	{ "getrandom-ops",	1,	0,	OPT_getrandom_ops },
	{ "getdent",		1,	0,	OPT_getdent },
	{ "getdent-ops",	1,	0,	OPT_getdent_ops },
	{ "goto",		1,	0,	OPT_goto },
	{ "goto-direction", 	1,	0,	OPT_goto_direction },
	{ "goto-ops",		1,	0,	OPT_goto_ops },
	{ "gpu",		1,	0,	OPT_gpu },
	{ "gpu-devnode",	1,	0,	OPT_gpu_devnode },
	{ "gpu-frag",		1,	0,	OPT_gpu_frag },
	{ "gpu-ops",		1,	0,	OPT_gpu_ops },
	{ "gpu-tex-size",	1,	0,	OPT_gpu_size },
	{ "gpu-upload",		1,	0,	OPT_gpu_uploads },
	{ "gpu-xsize",		1,	0,	OPT_gpu_xsize },
	{ "gpu-ysize",		1,	0,	OPT_gpu_ysize },
	{ "handle",		1,	0,	OPT_handle },
	{ "handle-ops",		1,	0,	OPT_handle_ops },
	{ "hash",		1,	0,	OPT_hash },
	{ "hash-method",	1,	0,	OPT_hash_method },
	{ "hash-ops",		1,	0,	OPT_hash_ops },
	{ "hdd",		1,	0,	OPT_hdd },
	{ "hdd-bytes",		1,	0,	OPT_hdd_bytes },
	{ "hdd-ops",		1,	0,	OPT_hdd_ops },
	{ "hdd-opts",		1,	0,	OPT_hdd_opts },
	{ "hdd-write-size", 	1,	0,	OPT_hdd_write_size },
	{ "heapsort",		1,	0,	OPT_heapsort },
	{ "heapsort-ops",	1,	0,	OPT_heapsort_ops },
	{ "heapsort-size",	1,	0,	OPT_heapsort_integers },
	{ "hrtimers",		1,	0,	OPT_hrtimers },
	{ "hrtimers-adjust",	0,	0,	OPT_hrtimers_adjust },
	{ "hrtimers-ops",	1,	0,	OPT_hrtimers_ops },
	{ "help",		0,	0,	OPT_help },
	{ "hsearch",		1,	0,	OPT_hsearch },
	{ "hsearch-ops",	1,	0,	OPT_hsearch_ops },
	{ "hsearch-size",	1,	0,	OPT_hsearch_size },
	{ "icache",		1,	0,	OPT_icache },
	{ "icache-ops",		1,	0,	OPT_icache_ops },
	{ "icmp-flood",		1,	0,	OPT_icmp_flood },
	{ "icmp-flood-ops",	1,	0,	OPT_icmp_flood_ops },
	{ "idle-page",		1,	0,	OPT_idle_page },
	{ "idle-page-ops",	1,	0,	OPT_idle_page_ops },
	{ "ignite-cpu",		0,	0, 	OPT_ignite_cpu },
	{ "inode-flags",	1,	0,	OPT_inode_flags },
	{ "inode-flags-ops",	1,	0,	OPT_inode_flags_ops },
	{ "inotify",		1,	0,	OPT_inotify },
	{ "inotify-ops",	1,	0,	OPT_inotify_ops },
	{ "io",			1,	0,	OPT_io },
	{ "io-ops",		1,	0,	OPT_io_ops },
	{ "iomix",		1,	0,	OPT_iomix },
	{ "iomix-bytes",	1,	0,	OPT_iomix_bytes },
	{ "iomix-ops",		1,	0,	OPT_iomix_ops },
	{ "ionice-class",	1,	0,	OPT_ionice_class },
	{ "ionice-level",	1,	0,	OPT_ionice_level },
	{ "ioport",		1,	0,	OPT_ioport },
	{ "ioport-ops",		1,	0,	OPT_ioport_ops },
	{ "ioport-opts",	1,	0,	OPT_ioport_opts },
	{ "ioprio",		1,	0,	OPT_ioprio },
	{ "ioprio-ops",		1,	0,	OPT_ioprio_ops },
	{ "iostat",		1,	0,	OPT_iostat },
	{ "io-uring",		1,	0,	OPT_io_uring },
	{ "io-uring-ops",	1,	0,	OPT_io_uring_ops },
	{ "ipsec-mb",		1,	0,	OPT_ipsec_mb },
	{ "ipsec-mb-feature",	1,	0,	OPT_ipsec_mb_feature },
	{ "ipsec-mb-jobs",	1,	0,	OPT_ipsec_mb_jobs },
	{ "ipsec-mb-method",	1,	0,	OPT_ipsec_mb_method },
	{ "ipsec-mb-ops",	1,	0,	OPT_ipsec_mb_ops },
	{ "itimer",		1,	0,	OPT_itimer },
	{ "itimer-freq",	1,	0,	OPT_itimer_freq },
	{ "itimer-ops",		1,	0,	OPT_itimer_ops },
	{ "itimer-rand",	0,	0,	OPT_itimer_rand },
	{ "job",		1,	0,	OPT_job },
	{ "jpeg",		1,	0,	OPT_jpeg },
	{ "jpeg-height",	1,	0,	OPT_jpeg_height },
	{ "jpeg-image",		1,	0,	OPT_jpeg_image },
	{ "jpeg-ops",		1,	0,	OPT_jpeg_ops },
	{ "jpeg-quality",	1,	0,	OPT_jpeg_quality },
	{ "jpeg-width",		1,	0,	OPT_jpeg_width },
	{ "judy",		1,	0,	OPT_judy },
	{ "judy-ops",		1,	0,	OPT_judy_ops },
	{ "judy-size",		1,	0,	OPT_judy_size },
	{ "kcmp",		1,	0,	OPT_kcmp },
	{ "kcmp-ops",		1,	0,	OPT_kcmp_ops },
	{ "keep-files",		0,	0,	OPT_keep_files },
	{ "keep-name",		0,	0,	OPT_keep_name },
	{ "key",		1,	0,	OPT_key },
	{ "key-ops",		1,	0,	OPT_key_ops },
	{ "kill",		1,	0,	OPT_kill },
	{ "kill-ops",		1,	0,	OPT_kill_ops },
	{ "klog",		1,	0,	OPT_klog },
	{ "klog-check",		0,	0,	OPT_klog_check },
	{ "klog-ops",		1,	0,	OPT_klog_ops },
	{ "kvm",		1,	0,	OPT_kvm },
	{ "kvm-ops",		1,	0,	OPT_kvm_ops },
	{ "l1cache",		1,	0, 	OPT_l1cache },
	{ "l1cache-line-size",	1,	0,	OPT_l1cache_line_size },
	{ "l1cache-method",	1,	0,	OPT_l1cache_method },
	{ "l1cache-ops",	1,	0,	OPT_l1cache_ops },
	{ "l1cache-sets",	1,	0,	OPT_l1cache_sets},
	{ "l1cache-size",	1,	0,	OPT_l1cache_size },
	{ "l1cache-ways",	1,	0,	OPT_l1cache_ways},
	{ "landlock",		1,	0,	OPT_landlock },
	{ "landlock-ops",	1,	0,	OPT_landlock_ops },
	{ "lease",		1,	0,	OPT_lease },
	{ "lease-breakers",	1,	0,	OPT_lease_breakers },
	{ "lease-ops",		1,	0,	OPT_lease_ops },
	{ "link",		1,	0,	OPT_link },
	{ "link-ops",		1,	0,	OPT_link_ops },
	{ "link-sync",		0,	0,	OPT_link_sync },
	{ "list",		1,	0,	OPT_list },
	{ "list-method",	1,	0,	OPT_list_method },
	{ "list-ops",		1,	0,	OPT_list_ops },
	{ "list-size",		1,	0,	OPT_list_size },
	{ "llc-affinity",	1,	0,	OPT_llc_affinity },
	{ "llc-affinity-ops",	1,	0,	OPT_llc_affinity_ops },
	{ "loadavg",		1,	0,	OPT_loadavg },
	{ "loadavg-ops",	1,	0,	OPT_loadavg_ops },
	{ "loadavg-max",	1,	0,	OPT_loadavg_max },
	{ "locka",		1,	0,	OPT_locka },
	{ "locka-ops",		1,	0,	OPT_locka_ops },
	{ "lockbus",		1,	0,	OPT_lockbus },
	{ "lockbus-ops",	1,	0,	OPT_lockbus_ops },
	{ "lockbus-nosplit",	0,	0,	OPT_lockbus_nosplit },
	{ "lockf",		1,	0,	OPT_lockf },
	{ "lockf-nonblock", 	0,	0,	OPT_lockf_nonblock },
	{ "lockf-ops",		1,	0,	OPT_lockf_ops },
	{ "lockofd",		1,	0,	OPT_lockofd },
	{ "lockofd-ops",	1,	0,	OPT_lockofd_ops },
	{ "log-brief",		0,	0,	OPT_log_brief },
	{ "log-file",		1,	0,	OPT_log_file },
	{ "log-lockless",	0,	0,	OPT_log_lockless },
	{ "longjmp",		1,	0,	OPT_longjmp },
	{ "longjmp-ops",	1,	0,	OPT_longjmp_ops },
	{ "loop",		1,	0,	OPT_loop },
	{ "loop-ops",		1,	0,	OPT_loop_ops },
	{ "lsearch",		1,	0,	OPT_lsearch },
	{ "lsearch-ops",	1,	0,	OPT_lsearch_ops },
	{ "lsearch-size",	1,	0,	OPT_lsearch_size },
	{ "madvise",		1,	0,	OPT_madvise },
	{ "madvise-ops",	1,	0,	OPT_madvise_ops },
	{ "malloc",		1,	0,	OPT_malloc },
	{ "malloc-bytes",	1,	0,	OPT_malloc_bytes },
	{ "malloc-max",		1,	0,	OPT_malloc_max },
	{ "malloc-ops",		1,	0,	OPT_malloc_ops },
	{ "malloc-pthreads",	1,	0,	OPT_malloc_pthreads },
	{ "malloc-thresh",	1,	0,	OPT_malloc_threshold },
	{ "malloc-touch",	0,	0,	OPT_malloc_touch },
	{ "malloc-zerofree",	0,	0,	OPT_malloc_zerofree },
	{ "matrix",		1,	0,	OPT_matrix },
	{ "matrix-method",	1,	0,	OPT_matrix_method },
	{ "matrix-ops",		1,	0,	OPT_matrix_ops },
	{ "matrix-size",	1,	0,	OPT_matrix_size },
	{ "matrix-yx",		0,	0,	OPT_matrix_yx },
	{ "matrix-3d",		1,	0,	OPT_matrix_3d },
	{ "matrix-3d-method",	1,	0,	OPT_matrix_3d_method },
	{ "matrix-3d-ops",	1,	0,	OPT_matrix_3d_ops },
	{ "matrix-3d-size",	1,	0,	OPT_matrix_3d_size },
	{ "matrix-3d-zyx",	0,	0,	OPT_matrix_3d_zyx },
	{ "maximize",		0,	0,	OPT_maximize },
	{ "max-fd",		1,	0,	OPT_max_fd },
	{ "mbind",		1,	0,	OPT_mbind },
	{ "mcontend",		1,	0,	OPT_mcontend },
	{ "mcontend-ops",	1,	0,	OPT_mcontend_ops },
	{ "membarrier",		1,	0,	OPT_membarrier },
	{ "membarrier-ops",	1,	0,	OPT_membarrier_ops },
	{ "memcpy",		1,	0,	OPT_memcpy },
	{ "memcpy-method",	1,	0,	OPT_memcpy_method },
	{ "memcpy-ops",		1,	0,	OPT_memcpy_ops },
	{ "memfd",		1,	0,	OPT_memfd },
	{ "memfd-bytes",	1,	0,	OPT_memfd_bytes },
	{ "memfd-fds",		1,	0,	OPT_memfd_fds },
	{ "memfd-ops",		1,	0,	OPT_memfd_ops },
	{ "memhotplug",		1,	0,	OPT_memhotplug },
	{ "memhotplug-ops",	1,	0,	OPT_memhotplug_ops },
	{ "memrate",		1,	0,	OPT_memrate },
	{ "memrate-bytes",	1,	0,	OPT_memrate_bytes },
	{ "memrate-flush",	0,	0,	OPT_memrate_flush },
	{ "memrate-ops",	1,	0,	OPT_memrate_ops },
	{ "memrate-rd-mbs",	1,	0,	OPT_memrate_rd_mbs },
	{ "memrate-wr-mbs",	1,	0,	OPT_memrate_wr_mbs },
	{ "memthrash",		1,	0,	OPT_memthrash },
	{ "memthrash-method",	1,	0,	OPT_memthrash_method },
	{ "memthrash-ops",	1,	0,	OPT_memthrash_ops },
	{ "mergesort",		1,	0,	OPT_mergesort },
	{ "mergesort-ops",	1,	0,	OPT_mergesort_ops },
	{ "mergesort-size",	1,	0,	OPT_mergesort_integers },
	{ "metrics",		0,	0,	OPT_metrics },
	{ "metrics-brief",	0,	0,	OPT_metrics_brief },
	{ "mincore",		1,	0,	OPT_mincore },
	{ "mincore-ops",	1,	0,	OPT_mincore_ops },
	{ "mincore-random",	0,	0,	OPT_mincore_rand },
	{ "misaligned",		1,	0,	OPT_misaligned },
	{ "misaligned-method",	1,	0,	OPT_misaligned_method },
	{ "misaligned-ops",	1,	0,	OPT_misaligned_ops },
	{ "minimize",		0,	0,	OPT_minimize },
	{ "mknod",		1,	0,	OPT_mknod },
	{ "mknod-ops",		1,	0,	OPT_mknod_ops },
	{ "mlock",		1,	0,	OPT_mlock },
	{ "mlock-ops",		1,	0,	OPT_mlock_ops },
	{ "mlockmany",		1,	0,	OPT_mlockmany },
	{ "mlockmany-ops",	1,	0,	OPT_mlockmany_ops },
	{ "mlockmany-procs",	1,	0,	OPT_mlockmany_procs },
	{ "mmap",		1,	0,	OPT_mmap },
	{ "mmap-async",		0,	0,	OPT_mmap_async },
	{ "mmap-bytes",		1,	0,	OPT_mmap_bytes },
	{ "mmap-file",		0,	0,	OPT_mmap_file },
	{ "mmap-mprotect",	0,	0,	OPT_mmap_mprotect },
	{ "mmap-odirect",	0,	0,	OPT_mmap_odirect },
	{ "mmap-ops",		1,	0,	OPT_mmap_ops },
	{ "mmap-osync",		0,	0,	OPT_mmap_osync },
	{ "mmap-mmap2",		0,	0,	OPT_mmap_mmap2 },
	{ "mmapaddr",		1,	0,	OPT_mmapaddr },
	{ "mmapaddr-ops",	1,	0,	OPT_mmapaddr_ops },
	{ "mmapfixed",		1,	0,	OPT_mmapfixed},
	{ "mmapfixed-ops",	1,	0,	OPT_mmapfixed_ops },
	{ "mmapfork",		1,	0,	OPT_mmapfork },
	{ "mmapfork-ops",	1,	0,	OPT_mmapfork_ops },
	{ "mmaphuge",		1,	0,	OPT_mmaphuge },
	{ "mmaphuge-mmaps",	1,	0,	OPT_mmaphuge_mmaps },
	{ "mmaphuge-ops",	1,	0,	OPT_mmaphuge_ops },
	{ "mmapmany",		1,	0,	OPT_mmapmany },
	{ "mmapmany-ops",	1,	0,	OPT_mmapmany_ops },
	{ "module",		1,	0,	OPT_module},
	{ "module-ops",		1,	0,	OPT_module_ops },
	{ "module-name",	1,	0,	OPT_module_name},
	{ "module-no-modver",	1,	0,	OPT_module_no_modver},
	{ "module-no-vermag",	1,	0,	OPT_module_no_vermag,},
	{ "module-no-unload",	0,	0,	OPT_module_no_unload},
	{ "mprotect",		1,	0,	OPT_mprotect },
	{ "mprotect-ops",	1,	0,	OPT_mprotect_ops },
	{ "mq",			1,	0,	OPT_mq },
	{ "mq-ops",		1,	0,	OPT_mq_ops },
	{ "mq-size",		1,	0,	OPT_mq_size },
	{ "mremap",		1,	0,	OPT_mremap },
	{ "mremap-bytes",	1,	0,	OPT_mremap_bytes },
	{ "mremap-mlock",	0,	0,	OPT_mremap_mlock },
	{ "mremap-ops",		1,	0,	OPT_mremap_ops },
	{ "msg",		1,	0,	OPT_msg },
	{ "msg-bytes",		1,	0,	OPT_msg_bytes },
	{ "msg-ops",		1,	0,	OPT_msg_ops },
	{ "msg-types",		1,	0,	OPT_msg_types },
	{ "msync",		1,	0,	OPT_msync },
	{ "msync-bytes",	1,	0,	OPT_msync_bytes },
	{ "msync-ops",		1,	0,	OPT_msync_ops },
	{ "msyncmany",		1,	0,	OPT_msyncmany },
	{ "msyncmany-ops",	1,	0,	OPT_msyncmany_ops },
	{ "munmap",		1,	0,	OPT_munmap },
	{ "munmap-ops",		1,	0,	OPT_munmap_ops },
	{ "mutex",		1,	0,	OPT_mutex },
	{ "mutex-affinity",	0,	0,	OPT_mutex_affinity },
	{ "mutex-ops",		1,	0,	OPT_mutex_ops },
	{ "mutex-procs",	1,	0,	OPT_mutex_procs },
	{ "nanosleep",		1,	0,	OPT_nanosleep },
	{ "nanosleep-ops",	1,	0,	OPT_nanosleep_ops },
	{ "nanosleep-threads",	1,	0,	OPT_nanosleep_threads },
	{ "netdev",		1,	0,	OPT_netdev },
	{ "netdev-ops",		1,	0,	OPT_netdev_ops },
	{ "netlink-proc",	1,	0,	OPT_netlink_proc },
	{ "netlink-proc-ops",	1,	0,	OPT_netlink_proc_ops },
	{ "netlink-task",	1,	0,	OPT_netlink_task },
	{ "netlink-task-ops",	1,	0,	OPT_netlink_task_ops },
	{ "nice",		1,	0,	OPT_nice },
	{ "nice-ops",		1,	0,	OPT_nice_ops },
	{ "no-madvise",		0,	0,	OPT_no_madvise },
	{ "no-oom-adjust",	0,	0,	OPT_no_oom_adjust },
	{ "no-rand-seed", 	0,	0,	OPT_no_rand_seed },
	{ "nop",		1,	0,	OPT_nop },
	{ "nop-instr",		1,	0,	OPT_nop_instr },
	{ "nop-ops",		1,	0,	OPT_nop_ops },
	{ "null",		1,	0,	OPT_null },
	{ "null-ops",		1,	0,	OPT_null_ops },
	{ "numa",		1,	0,	OPT_numa },
	{ "numa-ops",		1,	0,	OPT_numa_ops },
	{ "oomable",		0,	0,	OPT_oomable },
	{ "oom-avoid",		0,	0,	OPT_oom_avoid },
	{ "oom-avoid-bytes",	1,	0,	OPT_oom_avoid_bytes },
	{ "oom-pipe",		1,	0,	OPT_oom_pipe },
	{ "oom-pipe-ops",	1,	0,	OPT_oom_pipe_ops },
	{ "opcode",		1,	0,	OPT_opcode },
	{ "opcode-method",	1,	0,	OPT_opcode_method },
	{ "opcode-ops",		1,	0,	OPT_opcode_ops },
	{ "open",		1,	0,	OPT_open },
	{ "open-fd",		0,	0,	OPT_open_fd },
	{ "open-max",		1,	0,	OPT_open_max },
	{ "open-ops",		1,	0,	OPT_open_ops },
	{ "page-in",		0,	0,	OPT_page_in },
	{ "pagemove",		1,	0,	OPT_pagemove },
	{ "pagemove-bytes",	1,	0,	OPT_pagemove_bytes },
	{ "pagemove-ops",	1,	0,	OPT_pagemove_ops },
	{ "pageswap",		1,	0,	OPT_pageswap },
	{ "pageswap-ops",	1,	0,	OPT_pageswap_ops },
	{ "parallel",		1,	0,	OPT_all },
	{ "pathological",	0,	0,	OPT_pathological },
	{ "pci",		1,	0,	OPT_pci},
	{ "pci-ops",		1,	0,	OPT_pci_ops },
#if defined(STRESS_PERF_STATS) && 	\
    defined(HAVE_LINUX_PERF_EVENT_H)
	{ "perf",		0,	0,	OPT_perf_stats },
#endif
	{ "personality",	1,	0,	OPT_personality },
	{ "personality-ops",	1,	0,	OPT_personality_ops },
	{ "peterson",		1,	0,	OPT_peterson },
	{ "peterson-ops",	1,	0,	OPT_peterson_ops },
	{ "physpage",		1,	0,	OPT_physpage },
	{ "physpage-ops",	1,	0,	OPT_physpage_ops },
	{ "pidfd",		1,	0,	OPT_pidfd },
	{ "pidfd-ops",		1,	0,	OPT_pidfd_ops },
	{ "ping-sock",		1,	0,	OPT_ping_sock },
	{ "ping-sock-ops",	1,	0,	OPT_ping_sock_ops },
	{ "pipe",		1,	0,	OPT_pipe },
	{ "pipe-data-size",	1,	0,	OPT_pipe_data_size },
	{ "pipe-ops",		1,	0,	OPT_pipe_ops },
#if defined(F_SETPIPE_SZ)
	{ "pipe-size",		1,	0,	OPT_pipe_size },
#endif
	{ "pipeherd",		1,	0,	OPT_pipeherd },
	{ "pipeherd-ops",	1,	0,	OPT_pipeherd_ops },
	{ "pipeherd-yield", 	0,	0,	OPT_pipeherd_yield },
	{ "pkey",		1,	0,	OPT_pkey },
	{ "pkey-ops",		1,	0,	OPT_pkey_ops },
	{ "plugin",		1,	0,	OPT_plugin },
	{ "plugin-method",	1,	0,	OPT_plugin_method },
	{ "plugin-ops",		1,	0,	OPT_plugin_ops },
	{ "plugin-so",		1,	0,	OPT_plugin_so },
	{ "poll",		1,	0,	OPT_poll },
	{ "poll-ops",		1,	0,	OPT_poll_ops },
	{ "poll-fds",		1,	0,	OPT_poll_fds },
	{ "prctl",		1,	0,	OPT_prctl },
	{ "prctl-ops",		1,	0,	OPT_prctl_ops },
	{ "prefetch",		1,	0,	OPT_prefetch },
	{ "prefetch-l3-size",	1,	0,	OPT_prefetch_l3_size },
	{ "prefetch-method",	1,	0,	OPT_prefetch_method },
	{ "prefetch-ops",	1,	0,	OPT_prefetch_ops },
	{ "priv-instr",		1,	0,	OPT_priv_instr },
	{ "priv-instr-ops",	1,	0,	OPT_priv_instr_ops },
	{ "procfs",		1,	0,	OPT_procfs },
	{ "procfs-ops",		1,	0,	OPT_procfs_ops },
	{ "pthread",		1,	0,	OPT_pthread },
	{ "pthread-max",	1,	0,	OPT_pthread_max },
	{ "pthread-ops",	1,	0,	OPT_pthread_ops },
	{ "ptrace",		1,	0,	OPT_ptrace },
	{ "ptrace-ops",		1,	0,	OPT_ptrace_ops },
	{ "pty",		1,	0,	OPT_pty },
	{ "pty-max",		1,	0,	OPT_pty_max },
	{ "pty-ops",		1,	0,	OPT_pty_ops },
	{ "qsort",		1,	0,	OPT_qsort },
	{ "qsort-method",	1,	0,	OPT_qsort_method },
	{ "qsort-ops",		1,	0,	OPT_qsort_ops },
	{ "qsort-size",		1,	0,	OPT_qsort_integers },
	{ "quiet",		0,	0,	OPT_quiet },
	{ "quota",		1,	0,	OPT_quota },
	{ "quota-ops",		1,	0,	OPT_quota_ops },
	{ "race-sched",		1,	0,	OPT_race_sched },
	{ "race-sched-ops",	1,	0,	OPT_race_sched_ops },
	{ "race-sched-method",	1,	0,	OPT_race_sched_method },
	{ "radixsort",		1,	0,	OPT_radixsort },
	{ "radixsort-ops",	1,	0,	OPT_radixsort_ops },
	{ "radixsort-size",	1,	0,	OPT_radixsort_size },
	{ "ramfs",		1,	0,	OPT_ramfs },
	{ "ramfs-fill",		0,	0,	OPT_ramfs_fill },
	{ "ramfs-ops",		1,	0,	OPT_ramfs_ops },
	{ "ramfs-size",		1,	0,	OPT_ramfs_size },
	{ "randlist",		1,	0,	OPT_randlist },
	{ "randlist-compact",	0,	0,	OPT_randlist_compact },
	{ "randlist-items", 	1,	0,	OPT_randlist_items },
	{ "randlist-ops",	1,	0,	OPT_randlist_ops },
	{ "randlist-size", 	1,	0,	OPT_randlist_size },
	{ "random",		1,	0,	OPT_random },
	{ "rawdev",		1,	0,	OPT_rawdev },
	{ "rawdev-method",	1,	0,	OPT_rawdev_method },
	{ "rawdev-ops",		1,	0,	OPT_rawdev_ops },
	{ "rawpkt",		1,	0,	OPT_rawpkt },
	{ "rawpkt-ops",		1,	0,	OPT_rawpkt_ops },
	{ "rawpkt-port",	1,	0,	OPT_rawpkt_port },
	{ "rawpkt-rxring",	1,	0,	OPT_rawpkt_rxring },
	{ "rawsock",		1,	0,	OPT_rawsock },
	{ "rawsock-ops",	1,	0,	OPT_rawsock_ops },
	{ "rawsock-port",	1,	0,	OPT_rawsock_port },
	{ "rawudp",		1,	0,	OPT_rawudp },
	{ "rawudp-if",		1,	0,	OPT_rawudp_if },
	{ "rawudp-ops",		1,	0,	OPT_rawudp_ops },
	{ "rawudp-port",	1,	0,	OPT_rawudp_port },
	{ "rdrand",		1,	0,	OPT_rdrand },
	{ "rdrand-ops",		1,	0,	OPT_rdrand_ops },
	{ "rdrand-seed",	0,	0,	OPT_rdrand_seed },
	{ "readahead",		1,	0,	OPT_readahead },
	{ "readahead-bytes",	1,	0,	OPT_readahead_bytes },
	{ "readahead-ops",	1,	0,	OPT_readahead_ops },
	{ "reboot",		1,	0,	OPT_reboot },
	{ "reboot-ops",		1,	0,	OPT_reboot_ops },
	{ "regs",		1,	0,	OPT_regs },
	{ "regs-ops",		1,	0,	OPT_regs_ops },
	{ "remap",		1,	0,	OPT_remap },
	{ "remap-ops",		1,	0,	OPT_remap_ops },
	{ "rename",		1,	0,	OPT_rename },
	{ "rename-ops",		1,	0,	OPT_rename_ops },
	{ "resched",		1,	0,	OPT_resched },
	{ "resched-ops",	1,	0,	OPT_resched_ops },
	{ "resources",		1,	0,	OPT_resources },
	{ "resources-ops",	1,	0,	OPT_resources_ops },
	{ "revio",		1,	0,	OPT_revio },
	{ "revio-bytes",	1,	0,	OPT_revio_bytes },
	{ "revio-ops",		1,	0,	OPT_revio_ops },
	{ "revio-opts",		1,	0,	OPT_revio_opts },
	{ "ring-pipe",		1,	0,	OPT_ring_pipe },
	{ "ring-pipe-num",	1,	0,	OPT_ring_pipe_num },
	{ "ring-pipe-ops",	1,	0,	OPT_ring_pipe_ops },
	{ "ring-pipe-size",	1,	0,	OPT_ring_pipe_size },
	{ "ring-pipe-splice",	0,	0,	OPT_ring_pipe_splice },
	{ "rlimit",		1,	0,	OPT_rlimit },
	{ "rlimit-ops",		1,	0,	OPT_rlimit_ops },
	{ "rmap",		1,	0,	OPT_rmap },
	{ "rmap-ops",		1,	0,	OPT_rmap_ops },
	{ "rotate",		1,	0,	OPT_rotate },
	{ "rotate-method",	1,	0,	OPT_rotate_method },
	{ "rotate-ops",		1,	0,	OPT_rotate_ops },
	{ "rseq",		1,	0,	OPT_rseq },
	{ "rseq-ops",		1,	0,	OPT_rseq_ops },
	{ "rtc",		1,	0,	OPT_rtc },
	{ "rtc-ops",		1,	0,	OPT_rtc_ops },
	{ "sched",		1,	0,	OPT_sched },
	{ "sched-deadline",	1,	0,	OPT_sched_deadline },
	{ "sched-period",	1,	0,	OPT_sched_period },
	{ "sched-prio",		1,	0,	OPT_sched_prio },
	{ "sched-reclaim",	0,	0,      OPT_sched_reclaim },
	{ "sched-runtime",	1,	0,	OPT_sched_runtime },
	{ "schedpolicy",	1,	0,	OPT_schedpolicy },
	{ "schedpolicy-ops",	1,	0,	OPT_schedpolicy_ops },
	{ "sctp",		1,	0,	OPT_sctp },
	{ "sctp-domain",	1,	0,	OPT_sctp_domain },
	{ "sctp-if",		1,	0,	OPT_sctp_if },
	{ "sctp-ops",		1,	0,	OPT_sctp_ops },
	{ "sctp-port",		1,	0,	OPT_sctp_port },
	{ "sctp-sched",		1,	0,	OPT_sctp_sched },
	{ "seal",		1,	0,	OPT_seal },
	{ "seal-ops",		1,	0,	OPT_seal_ops },
	{ "seccomp",		1,	0,	OPT_seccomp },
	{ "seccomp-ops",	1,	0,	OPT_seccomp_ops },
	{ "secretmem",		1,	0,	OPT_secretmem },
	{ "secretmem-ops",	1,	0,	OPT_secretmem_ops },
	{ "seed",		1,	0,	OPT_seed },
	{ "seek",		1,	0,	OPT_seek },
	{ "seek-ops",		1,	0,	OPT_seek_ops },
	{ "seek-punch",		0,	0,	OPT_seek_punch  },
	{ "seek-size",		1,	0,	OPT_seek_size },
	{ "sem",		1,	0,	OPT_sem },
	{ "sem-ops",		1,	0,	OPT_sem_ops },
	{ "sem-procs",		1,	0,	OPT_sem_procs },
	{ "sem-sysv",		1,	0,	OPT_sem_sysv },
	{ "sem-sysv-ops",	1,	0,	OPT_sem_sysv_ops },
	{ "sem-sysv-procs",	1,	0,	OPT_sem_sysv_procs },
	{ "sendfile",		1,	0,	OPT_sendfile },
	{ "sendfile-ops",	1,	0,	OPT_sendfile_ops },
	{ "sendfile-size",	1,	0,	OPT_sendfile_size },
	{ "sequential",		1,	0,	OPT_sequential },
	{ "session",		1,	0,	OPT_session },
	{ "session-ops",	1,	0,	OPT_session_ops },
	{ "set",		1,	0,	OPT_set },
	{ "set-ops",		1,	0,	OPT_set_ops },
	{ "shellsort",		1,	0,	OPT_shellsort },
	{ "shellsort-ops",	1,	0,	OPT_shellsort_ops },
	{ "shellsort-size",	1,	0,	OPT_shellsort_size },
	{ "shm",		1,	0,	OPT_shm },
	{ "shm-bytes",		1,	0,	OPT_shm_bytes },
	{ "shm-objs",		1,	0,	OPT_shm_objects },
	{ "shm-ops",		1,	0,	OPT_shm_ops },
	{ "shm-sysv",		1,	0,	OPT_shm_sysv },
	{ "shm-sysv-bytes",	1,	0,	OPT_shm_sysv_bytes },
	{ "shm-sysv-ops",	1,	0,	OPT_shm_sysv_ops },
	{ "shm-sysv-segs",	1,	0,	OPT_shm_sysv_segments },
	{ "sigabrt",		1,	0,	OPT_sigabrt },
	{ "sigabrt-ops",	1,	0,	OPT_sigabrt_ops },
	{ "sigchld",		1,	0,	OPT_sigchld },
	{ "sigchld-ops",	1,	0,	OPT_sigchld_ops },
	{ "sigfd",		1,	0,	OPT_sigfd },
	{ "sigfd-ops",		1,	0,	OPT_sigfd_ops },
	{ "sigio",		1,	0,	OPT_sigio },
	{ "sigio-ops",		1,	0,	OPT_sigio_ops },
	{ "sigfpe",		1,	0,	OPT_sigfpe },
	{ "sigfpe-ops",		1,	0,	OPT_sigfpe_ops },
	{ "signal",		1,	0,	OPT_signal },
	{ "signal-ops",		1,	0,	OPT_signal_ops },
	{ "signest",		1,	0,	OPT_signest },
	{ "signest-ops",	1,	0,	OPT_signest_ops },
	{ "sigpending",		1,	0,	OPT_sigpending},
	{ "sigpending-ops",	1,	0,	OPT_sigpending_ops },
	{ "sigpipe",		1,	0,	OPT_sigpipe },
	{ "sigpipe-ops",	1,	0,	OPT_sigpipe_ops },
	{ "sigq",		1,	0,	OPT_sigq },
	{ "sigq-ops",		1,	0,	OPT_sigq_ops },
	{ "sigrt",		1,	0,	OPT_sigrt },
	{ "sigrt-ops",		1,	0,	OPT_sigrt_ops },
	{ "sigsegv",		1,	0,	OPT_sigsegv },
	{ "sigsegv-ops",	1,	0,	OPT_sigsegv_ops },
	{ "sigsuspend",		1,	0,	OPT_sigsuspend },
	{ "sigsuspend-ops",	1,	0,	OPT_sigsuspend_ops },
	{ "sigtrap",		1,	0,	OPT_sigtrap },
	{ "sigtrap-ops",	1,	0,	OPT_sigtrap_ops},
	{ "skiplist",		1,	0,	OPT_skiplist },
	{ "skiplist-ops",	1,	0,	OPT_skiplist_ops },
	{ "skiplist-size",	1,	0,	OPT_skiplist_size },
	{ "skip-silent",	0,	0,	OPT_skip_silent },
	{ "sleep",		1,	0,	OPT_sleep },
	{ "sleep-max",		1,	0,	OPT_sleep_max },
	{ "sleep-ops",		1,	0,	OPT_sleep_ops },
	{ "smart",		0,	0,	OPT_smart },
	{ "smi",		1,	0,	OPT_smi },
	{ "smi-ops",		1,	0,	OPT_smi_ops },
	{ "sn",			0,	0,	OPT_sn },
	{ "sock",		1,	0,	OPT_sock },
	{ "sock-domain",	1,	0,	OPT_sock_domain },
	{ "sock-if",		1,	0,	OPT_sock_if },
	{ "sock-nodelay",	0,	0,	OPT_sock_nodelay },
	{ "sock-ops",		1,	0,	OPT_sock_ops },
	{ "sock-opts",		1,	0,	OPT_sock_opts },
	{ "sock-port",		1,	0,	OPT_sock_port },
	{ "sock-protocol",	1,	0,	OPT_sock_protocol },
	{ "sock-type",		1,	0,	OPT_sock_type },
	{ "sock-zerocopy", 	0,	0,	OPT_sock_zerocopy },
	{ "sockabuse",		1,	0,	OPT_sockabuse },
	{ "sockabuse-ops",	1,	0,	OPT_sockabuse_ops },
	{ "sockabuse-port",	1,	0,	OPT_sockabuse_port },
	{ "sockdiag",		1,	0,	OPT_sockdiag },
	{ "sockdiag-ops",	1,	0,	OPT_sockdiag_ops },
	{ "sockfd",		1,	0,	OPT_sockfd },
	{ "sockfd-ops",		1,	0,	OPT_sockfd_ops },
	{ "sockfd-port",	1,	0,	OPT_sockfd_port },
	{ "sockmany",		1,	0,	OPT_sockmany },
	{ "sockmany-if",	1,	0,	OPT_sockmany_if },
	{ "sockmany-ops",	1,	0,	OPT_sockmany_ops },
	{ "sockmany-port",	1,	0,	OPT_sockmany_port },
	{ "sockpair",		1,	0,	OPT_sockpair },
	{ "sockpair-ops",	1,	0,	OPT_sockpair_ops },
	{ "softlockup",		1,	0,	OPT_softlockup },
	{ "softlockup-ops",	1,	0,	OPT_softlockup_ops },
	{ "sparsematrix",	1,	0,	OPT_sparsematrix},
	{ "sparsematrix-items",	1,	0,	OPT_sparsematrix_items },
	{ "sparsematrix-method",1,	0,	OPT_sparsematrix_method },
	{ "sparsematrix-ops",	1,	0,	OPT_sparsematrix_ops },
	{ "sparsematrix-size",	1,	0,	OPT_sparsematrix_size },
	{ "spawn",		1,	0,	OPT_spawn },
	{ "spawn-ops",		1,	0,	OPT_spawn_ops },
	{ "splice",		1,	0,	OPT_splice },
	{ "splice-bytes",	1,	0,	OPT_splice_bytes },
	{ "splice-ops",		1,	0,	OPT_splice_ops },
	{ "stack",		1,	0,	OPT_stack},
	{ "stack-fill",		0,	0,	OPT_stack_fill },
	{ "stack-mlock",	0,	0,	OPT_stack_mlock },
	{ "stack-ops",		1,	0,	OPT_stack_ops },
	{ "stack-pageout",	0,	0,	OPT_stack_pageout },
	{ "stack-unmap",	0,	0,	OPT_stack_unmap },
	{ "stackmmap",		1,	0,	OPT_stackmmap },
	{ "stackmmap-ops",	1,	0,	OPT_stackmmap_ops },
	{ "stderr",		0,	0,	OPT_stderr },
	{ "stdout",		0,	0,	OPT_stdout },
	{ "str",		1,	0,	OPT_str },
	{ "str-method",		1,	0,	OPT_str_method },
	{ "str-ops",		1,	0,	OPT_str_ops },
	{ "stressors",		0,	0,	OPT_stressors },
	{ "stream",		1,	0,	OPT_stream },
	{ "stream-index",	1,	0,	OPT_stream_index },
	{ "stream-l3-size",	1,	0,	OPT_stream_l3_size },
	{ "stream-madvise",	1,	0,	OPT_stream_madvise },
	{ "stream-ops",		1,	0,	OPT_stream_ops },
	{ "swap",		1,	0,	OPT_swap },
	{ "swap-ops",		1,	0,	OPT_swap_ops },
	{ "switch",		1,	0,	OPT_switch },
	{ "switch-freq",	1,	0,	OPT_switch_freq },
	{ "switch-method",	1,	0,	OPT_switch_method },
	{ "switch-ops",		1,	0,	OPT_switch_ops },
	{ "symlink",		1,	0,	OPT_symlink },
	{ "symlink-ops",	1,	0,	OPT_symlink_ops },
	{ "symlink-sync",	0,	0,	OPT_symlink_sync },
	{ "sync-file",		1,	0,	OPT_sync_file },
	{ "sync-file-bytes", 	1,	0,	OPT_sync_file_bytes },
	{ "sync-file-ops", 	1,	0,	OPT_sync_file_ops },
	{ "syncload",		1,	0,	OPT_syncload },
	{ "syncload-msbusy",	1,	0,	OPT_syncload_msbusy },
	{ "syncload-mssleep",	1,	0,	OPT_syncload_mssleep },
	{ "syncload-ops",	1,	0,	OPT_syncload_ops },
	{ "sysbadaddr",		1,	0,	OPT_sysbadaddr },
	{ "sysbadaddr-ops",	1,	0,	OPT_sysbadaddr_ops },
	{ "syscall",		1,	0,	OPT_syscall },
	{ "syscall-method",	1,	0,	OPT_syscall_method },
	{ "syscall-ops",	1,	0,	OPT_syscall_ops },
	{ "sysfs",		1,	0,	OPT_sysfs },
	{ "sysfs-ops",		1,	0,	OPT_sysfs_ops },
	{ "sysinfo",		1,	0,	OPT_sysinfo },
	{ "sysinfo-ops",	1,	0,	OPT_sysinfo_ops },
	{ "sysinval",		1,	0,	OPT_sysinval },
	{ "sysinval-ops",	1,	0,	OPT_sysinval_ops },
#if defined(HAVE_SYSLOG_H)
	{ "syslog",		0,	0,	OPT_syslog },
#endif
	{ "taskset",		1,	0,	OPT_taskset },
	{ "tee",		1,	0,	OPT_tee },
	{ "tee-ops",		1,	0,	OPT_tee_ops },
	{ "temp-path",		1,	0,	OPT_temp_path },
	{ "timeout",		1,	0,	OPT_timeout },
	{ "timer",		1,	0,	OPT_timer },
	{ "timer-freq",		1,	0,	OPT_timer_freq },
	{ "timer-ops",		1,	0,	OPT_timer_ops },
	{ "timer-rand", 	0,	0,	OPT_timer_rand },
	{ "timerfd",		1,	0,	OPT_timerfd },
	{ "timerfd-fds",	1,	0,	OPT_timerfd_fds },
	{ "timerfd-freq",	1,	0,	OPT_timerfd_freq },
	{ "timerfd-ops",	1,	0,	OPT_timerfd_ops },
	{ "timerfd-rand",	0,	0,	OPT_timerfd_rand },
	{ "timer-slack"	,	1,	0,	OPT_timer_slack },
	{ "tlb-shootdown",	1,	0,	OPT_tlb_shootdown },
	{ "tlb-shootdown-ops",	1,	0,	OPT_tlb_shootdown_ops },
	{ "tmpfs",		1,	0,	OPT_tmpfs },
	{ "tmpfs-mmap-async",	0,	0,	OPT_tmpfs_mmap_async },
	{ "tmpfs-mmap-file",	0,	0,	OPT_tmpfs_mmap_file },
	{ "tmpfs-ops",		1,	0,	OPT_tmpfs_ops },
	{ "touch",		1,	0,	OPT_touch },
	{ "touch-method",	1,	0,	OPT_touch_method },
	{ "touch-ops",		1,	0,	OPT_touch_ops },
	{ "touch-opts",		1,	0,	OPT_touch_opts },
	{ "tree",		1,	0,	OPT_tree },
	{ "tree-method",	1,	0,	OPT_tree_method },
	{ "tree-ops",		1,	0,	OPT_tree_ops },
	{ "tree-size",		1,	0,	OPT_tree_size },
	{ "tsc",		1,	0,	OPT_tsc },
	{ "tsc-lfence",		0,	0,	OPT_tsc_lfence },
	{ "tsc-ops",		1,	0,	OPT_tsc_ops },
	{ "tsearch",		1,	0,	OPT_tsearch },
	{ "tsearch-ops",	1,	0,	OPT_tsearch_ops },
	{ "tsearch-size",	1,	0,	OPT_tsearch_size },
	{ "thermalstat",	1,	0,	OPT_thermalstat },
	{ "thrash",		0,	0,	OPT_thrash },
	{ "times",		0,	0,	OPT_times },
	{ "timestamp",		0,	0,	OPT_timestamp },
	{ "tz",			0,	0,	OPT_thermal_zones },
	{ "tun",		1,	0,	OPT_tun},
	{ "tun-tap",		0,	0,	OPT_tun_tap },
	{ "tun-ops",		1,	0,	OPT_tun_ops },
	{ "udp",		1,	0,	OPT_udp },
	{ "udp-domain",		1,	0,	OPT_udp_domain },
	{ "udp-gro",		0,	0,	OPT_udp_gro },
	{ "udp-if",		1,	0,	OPT_udp_if },
	{ "udp-lite",		0,	0,	OPT_udp_lite },
	{ "udp-ops",		1,	0,	OPT_udp_ops },
	{ "udp-port",		1,	0,	OPT_udp_port },
	{ "udp-flood",		1,	0,	OPT_udp_flood },
	{ "udp-flood-domain",	1,	0,	OPT_udp_flood_domain },
	{ "udp-flood-if",	1,	0,	OPT_udp_flood_if },
	{ "udp-flood-ops",	1,	0,	OPT_udp_flood_ops },
	{ "umount",		1,	0,	OPT_umount },
	{ "umount-ops",		1,	0,	OPT_umount_ops },
	{ "unshare",		1,	0,	OPT_unshare },
	{ "unshare-ops",	1,	0,	OPT_unshare_ops },
	{ "uprobe",		1,	0,	OPT_uprobe },
	{ "uprobe-ops",		1,	0,	OPT_uprobe_ops },
	{ "urandom",		1,	0,	OPT_urandom },
	{ "urandom-ops",	1,	0,	OPT_urandom_ops },
	{ "userfaultfd",	1,	0,	OPT_userfaultfd },
	{ "userfaultfd-bytes",	1,	0,	OPT_userfaultfd_bytes },
	{ "userfaultfd-ops",	1,	0,	OPT_userfaultfd_ops },
	{ "usersyscall",	1,	0,	OPT_usersyscall },
	{ "usersyscall-ops",	1,	0,	OPT_usersyscall_ops },
	{ "utime",		1,	0,	OPT_utime },
	{ "utime-fsync",	0,	0,	OPT_utime_fsync },
	{ "utime-ops",		1,	0,	OPT_utime_ops },
	{ "vdso",		1,	0,	OPT_vdso },
	{ "vdso-func",		1,	0,	OPT_vdso_func },
	{ "vdso-ops",		1,	0,	OPT_vdso_ops },
	{ "vecfp",		1,	0,	OPT_vecfp },
	{ "vecfp-method",	1,	0,	OPT_vecfp_method },
	{ "vecfp-ops",		1,	0,	OPT_vecfp_ops },
	{ "vecmath",		1,	0,	OPT_vecmath },
	{ "vecmath-ops",	1,	0,	OPT_vecmath_ops },
	{ "vecshuf",		1,	0,	OPT_vecshuf },
	{ "vecshuf-method",	1,	0,	OPT_vecshuf_method },
	{ "vecshuf-ops",	1,	0,	OPT_vecshuf_ops },
	{ "vecwide",		1,	0,	OPT_vecwide},
	{ "vecwide-ops",	1,	0,	OPT_vecwide_ops },
	{ "verbose",		0,	0,	OPT_verbose },
	{ "verify",		0,	0,	OPT_verify },
	{ "verifiable",		0,	0,	OPT_verifiable },
	{ "verity",		1,	0,	OPT_verity },
	{ "verity-ops",		1,	0,	OPT_verity_ops },
	{ "version",		0,	0,	OPT_version },
	{ "vfork",		1,	0,	OPT_vfork },
	{ "vfork-max",		1,	0,	OPT_vfork_max },
	{ "vfork-ops",		1,	0,	OPT_vfork_ops },
	{ "vforkmany",		1,	0,	OPT_vforkmany },
	{ "vforkmany-ops", 	1,	0,	OPT_vforkmany_ops },
	{ "vforkmany-vm", 	0,	0,	OPT_vforkmany_vm },
	{ "vm",			1,	0,	OPT_vm },
	{ "vm-bytes",		1,	0,	OPT_vm_bytes },
	{ "vm-hang",		1,	0,	OPT_vm_hang },
	{ "vm-keep",		0,	0,	OPT_vm_keep },
#if defined(MAP_LOCKED)
	{ "vm-locked",		0,	0,	OPT_vm_mmap_locked },
#endif
	{ "vm-madvise",		1,	0,	OPT_vm_madvise },
	{ "vm-method",		1,	0,	OPT_vm_method },
	{ "vm-ops",		1,	0,	OPT_vm_ops },
#if defined(MAP_POPULATE)
	{ "vm-populate",	0,	0,	OPT_vm_mmap_populate },
#endif
	{ "vm-addr",		1,	0,	OPT_vm_addr },
	{ "vm-addr-method",	1,	0,	OPT_vm_addr_method },
	{ "vm-addr-ops",	1,	0,	OPT_vm_addr_ops },
	{ "vm-rw",		1,	0,	OPT_vm_rw },
	{ "vm-rw-bytes",	1,	0,	OPT_vm_rw_bytes },
	{ "vm-rw-ops",		1,	0,	OPT_vm_rw_ops },
	{ "vm-segv",		1,	0,	OPT_vm_segv },
	{ "vm-segv-ops",	1,	0,	OPT_vm_segv_ops },
	{ "vm-splice",		1,	0,	OPT_vm_splice },
	{ "vm-splice-bytes",	1,	0,	OPT_vm_splice_bytes },
	{ "vm-splice-ops",	1,	0,	OPT_vm_splice_ops },
	{ "vmstat",		1,	0,	OPT_vmstat },
	{ "wait",		1,	0,	OPT_wait },
	{ "wait-ops",		1,	0,	OPT_wait_ops },
	{ "waitcpu",		1,	0,	OPT_waitcpu },
	{ "waitcpu-ops",	1,	0,	OPT_waitcpu_ops },
	{ "watchdog",		1,	0,	OPT_watchdog },
	{ "watchdog-ops",	1,	0,	OPT_watchdog_ops },
	{ "wcs",		1,	0,	OPT_wcs},
	{ "wcs-method",		1,	0,	OPT_wcs_method },
	{ "wcs-ops",		1,	0,	OPT_wcs_ops },
	{ "x86cpuid",		1,	0,	OPT_x86cpuid },
	{ "x86cpuid-ops",	1,	0,	OPT_x86cpuid_ops },
	{ "x86syscall",		1,	0,	OPT_x86syscall },
	{ "x86syscall-func",	1,	0,	OPT_x86syscall_func },
	{ "x86syscall-ops",	1,	0,	OPT_x86syscall_ops },
	{ "xattr",		1,	0,	OPT_xattr },
	{ "xattr-ops",		1,	0,	OPT_xattr_ops },
	{ "yaml",		1,	0,	OPT_yaml },
	{ "yield",		1,	0,	OPT_yield },
	{ "yield-ops",		1,	0,	OPT_yield_ops },
	{ "zero",		1,	0,	OPT_zero },
	{ "zero-ops",		1,	0,	OPT_zero_ops },
	{ "zlib",		1,	0,	OPT_zlib },
	{ "zlib-level",		1,	0,	OPT_zlib_level },
	{ "zlib-method",	1,	0,	OPT_zlib_method },
	{ "zlib-mem-level",	1,	0,	OPT_zlib_mem_level },
	{ "zlib-ops",		1,	0,	OPT_zlib_ops },
	{ "zlib-strategy",	1,	0,	OPT_zlib_strategy, },
	{ "zlib-stream-bytes",	1,	0,	OPT_zlib_stream_bytes, },
	{ "zlib-window-bits",	1,	0,	OPT_zlib_window_bits },
	{ "zombie",		1,	0,	OPT_zombie },
	{ "zombie-max",		1,	0,	OPT_zombie_max },
	{ "zombie-ops",		1,	0,	OPT_zombie_ops },
	{ NULL,			0,	0,	0 }
};

/*
 *  Generic help options
 */
static const stress_help_t help_generic[] = {
	{ NULL,		"abort",		"abort all stressors if any stressor fails" },
	{ NULL,		"aggressive",		"enable all aggressive options" },
	{ "a N",	"all N",		"start N workers of each stress test" },
	{ "b N",	"backoff N",		"wait of N microseconds before work starts" },
	{ NULL,		"class name",		"specify a class of stressors, use with --sequential" },
	{ "n",		"dry-run",		"do not run" },
	{ NULL,		"ftrace",		"enable kernel function call tracing" },
	{ "h",		"help",			"show help" },
	{ NULL,		"ignite-cpu",		"alter kernel controls to make CPU run hot" },
	{ NULL,		"ionice-class C",	"specify ionice class (idle, besteffort, realtime)" },
	{ NULL,		"ionice-level L",	"specify ionice level (0 max, 7 min)" },
	{ "j",		"job jobfile",		"run the named jobfile" },
	{ "k",		"keep-name",		"keep stress worker names to be 'stress-ng'" },
	{ NULL,		"keep-files",		"do not remove files or directories" },
	{ NULL,		"klog-check",		"check kernel message log for errors" },
	{ NULL,		"log-brief",		"less verbose log messages" },
	{ NULL,		"log-file filename",	"log messages to a log file" },
	{ NULL,		"maximize",		"enable maximum stress options" },
	{ NULL,		"max-fd",		"set maximum file descriptor limit" },
	{ NULL,		"mbind",		"set NUMA memory binding to specific nodes" },
	{ "M",		"metrics",		"print pseudo metrics of activity" },
	{ NULL,		"metrics-brief",	"enable metrics and only show non-zero results" },
	{ NULL,		"minimize",		"enable minimal stress options" },
	{ NULL,		"no-madvise",		"don't use random madvise options for each mmap" },
	{ NULL,		"no-rand-seed",		"seed random numbers with the same constant" },
	{ NULL,		"oomable",		"Do not respawn a stressor if it gets OOM'd" },
	{ NULL,		"oom-avoid",		"Try to avoid stressors from being OOM'" },
	{ NULL,		"page-in",		"touch allocated pages that are not in core" },
	{ NULL,		"parallel N",		"synonym for 'all N'" },
	{ NULL,		"pathological",		"enable stressors that are known to hang a machine" },
#if defined(STRESS_PERF_STATS) &&	\
    defined(HAVE_LINUX_PERF_EVENT_H)
	{ NULL,		"perf",			"display perf statistics" },
#endif
	{ "q",		"quiet",		"quiet output" },
	{ "r",		"random N",		"start N random workers" },
	{ NULL,		"sched type",		"set scheduler type" },
	{ NULL,		"sched-prio N",		"set scheduler priority level N" },
	{ NULL,		"sched-period N",	"set period for SCHED_DEADLINE to N nanosecs (Linux only)" },
	{ NULL,		"sched-runtime N",	"set runtime for SCHED_DEADLINE to N nanosecs (Linux only)" },
	{ NULL,		"sched-deadline N",	"set deadline for SCHED_DEADLINE to N nanosecs (Linux only)" },
	{ NULL,		"sched-reclaim",        "set reclaim cpu bandwidth for deadline scheduler (Linux only)" },
	{ NULL,		"seed N",		"set the random number generator seed with a 64 bit value" },
	{ NULL,		"sequential N",		"run all stressors one by one, invoking N of them" },
	{ NULL,		"skip-silent",		"silently skip unimplemented stressors" },
	{ NULL,		"stressors",		"show available stress tests" },
	{ NULL,		"smart",		"show changes in S.M.A.R.T. data" },
	{ NULL,		"sn",			"use scientific notation for metrics" },
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
	{ NULL,		"verifiable",		"show stressors that enable verification via --verify" },
	{ "V",		"version",		"show version" },
	{ "Y",		"yaml file",		"output results to YAML formatted file" },
	{ "x",		"exclude",		"list of stressors to exclude (not run)" },
	{ NULL,		NULL,			NULL }
};

/*
 *  stress_hash_checksum()
 *	generate a hash of the checksum data
 */
static inline void stress_hash_checksum(stress_checksum_t *checksum)
{
	checksum->hash = stress_hash_jenkin((uint8_t *)&checksum->data,
				sizeof(checksum->data));
}

/*
 *  stressor_name_find()
 *  	Find index into stressors by name
 */
static inline size_t stressor_name_find(const char *name)
{
	size_t i;

	for (i = 0; stressors[i].name; i++) {
		if (!stress_strcmp_munged(name, stressors[i].name))
			break;
	}
	return i;	/* End of array is a special "NULL" entry */
}

/*
 *  stress_remove_stressor()
 *	remove stressor from stressor list
 */
static void stress_remove_stressor(stress_stressor_t *ss)
{
	if (stressors_head == ss) {
		stressors_head = ss->next;
		if (ss->next)
			ss->next->prev = ss->prev;
	} else {
		if (ss->prev)
			ss->prev->next = ss->next;
	}

	if (stressors_tail == ss) {
		stressors_tail = ss->prev;
		if (ss->prev)
			ss->prev->next = ss->next;
	} else {
		if (ss->next)
			ss->next->prev = ss->prev;
	}
	free(ss);
}

/*
 *  stress_get_class_id()
 *	find the class id of a given class name
 */
static uint32_t stress_get_class_id(char *const str)
{
	size_t i;

	for (i = 0; i < SIZEOF_ARRAY(classes); i++) {
		if (!strcmp(classes[i].name, str))
			return classes[i].class;
	}
	return 0;
}

/*
 *  stress_get_class()
 *	parse for allowed class types, return bit mask of types, 0 if error
 */
static int stress_get_class(char *const class_str, uint32_t *class)
{
	char *str, *token;
	int ret = 0;

	*class = 0;
	for (str = class_str; (token = strtok(str, ",")) != NULL; str = NULL) {
		uint32_t cl = stress_get_class_id(token);

		if (!cl) {
			size_t i;
			const size_t len = strlen(token);

			if ((len > 1) && (token[len - 1] == '?')) {
				token[len - 1] = '\0';

				cl = stress_get_class_id(token);
				if (cl) {
					size_t j;

					(void)printf("class '%s' stressors:",
						token);
					for (j = 0; stressors[j].name; j++) {
						if (stressors[j].info->class & cl) {
							char munged[64];

							(void)stress_munge_underscore(munged, stressors[j].name, sizeof(munged));
							(void)printf(" %s", munged);
						}
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

	if (!stress_get_setting("exclude", &opt_exclude))
		return 0;

	for (str = opt_exclude; (token = strtok(str, ",")) != NULL; str = NULL) {
		unsigned int id;
		stress_stressor_t *ss = stressors_head;
		const size_t i = stressor_name_find(token);

		if (!stressors[i].name) {
			(void)fprintf(stderr, "Unknown stressor: '%s', "
				"invalid exclude option\n", token);
			return -1;
		}
		id = stressors[i].id;

		while (ss) {
			stress_stressor_t *next = ss->next;

			if (ss->stressor->id == id)
				stress_remove_stressor(ss);
			ss = next;
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
	if (g_shared)
		g_shared->caught_sigint = true;
	keep_stressing_set_flag(false);
	wait_flag = false;

	/* Send alarm to all stressors */
	stress_kill_stressors(SIGALRM, true);
}

/*
 *  stress_sigalrm_handler()
 *	handle signal in parent process, don't block on waits
 */
static void MLOCKED_TEXT stress_sigalrm_handler(int signum)
{
	if (g_shared)
		g_shared->caught_sigint = true;
	if (getpid() == main_pid) {
		/* Parent */
		wait_flag = false;
		stress_kill_stressors(SIGALRM, false);
	} else {
		/* Child */
		stress_handle_stop_stressing(signum);
	}
}

#if defined(SA_SIGINFO)
static void MLOCKED_TEXT stress_sigalrm_action_handler(
	int signum,
	siginfo_t *info,
	void *ucontext)
{
	(void)ucontext;

	if (g_shared && 			/* shared mem initialized */
	    !g_shared->caught_sigint &&		/* and SIGINT not already handled */
	    info && 				/* and info is valid */
	    (info->si_code == SI_USER) &&	/* and not from kernel SIGALRM */
	    (!g_sigalrm_info.triggered)) {	/* and not already handled */
		g_sigalrm_info.code = info->si_code;
		g_sigalrm_info.pid = info->si_pid;
		g_sigalrm_info.uid = info->si_uid;
		(void)gettimeofday(&g_sigalrm_info.when, NULL);
		g_sigalrm_info.triggered = true;
	}
	stress_sigalrm_handler(signum);
}
#endif

#if defined(SIGUSR2)
/*
 *  stress_stats_handler()
 *	dump current system stats
 */
static void MLOCKED_TEXT stress_stats_handler(int signum)
{
	static char buffer[80];
	char *ptr = buffer;
	double min1, min5, min15;
	size_t shmall, freemem, totalmem, freeswap, totalswap;

	(void)signum;

	*ptr = '\0';

	if (stress_get_load_avg(&min1, &min5, &min15) == 0) {
		int ret;

		ret = snprintf(ptr, sizeof(buffer),
			"Load Avg: %.2f %.2f %.2f, ",
			min1, min5, min15);
		if (ret > 0)
			ptr += ret;
	}
	stress_get_memlimits(&shmall, &freemem, &totalmem, &freeswap, &totalswap);

	(void)snprintf(ptr, (size_t)(buffer - ptr),
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
#if defined(SA_SIGINFO)
	struct sigaction sa;
#endif
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
#if defined(SA_SIGINFO)
	(void)shim_memset(&sa, 0, sizeof(sa));
	sa.sa_sigaction = stress_sigalrm_action_handler;
	sa.sa_flags = SA_SIGINFO;
	if (sigaction(SIGALRM, &sa, NULL) < 0) {
		pr_fail("%s: sigaction SIGALRM: errno=%d (%s)\n",
                        stress, errno, strerror(errno));
	}
#else
	if (stress_sighandler(stress, SIGALRM, stress_sigalrm_handler, NULL) < 0)
		return -1;
#endif
	return 0;
}

/*
 *  stress_version()
 *	print program version info
 */
static void stress_version(void)
{
	(void)printf("%s, version " VERSION " (%s, %s)%s\n",
		g_app_name, stress_get_compiler(), stress_get_uname_info(),
		stress_is_dev_tty(STDOUT_FILENO) ? "" : " \U0001F4BB\U0001F525");
}

/*
 *  stress_usage_help()
 *	show generic help information
 */
static void stress_usage_help(const stress_help_t help_info[])
{
	size_t i;
	const int cols = stress_tty_width();

	for (i = 0; help_info[i].description; i++) {
		char opt_s[10] = "";
		int wd = 0;
		bool first = true;
		const char *ptr, *space = NULL;
		const char *start = help_info[i].description;

		if (help_info[i].opt_s)
			(void)snprintf(opt_s, sizeof(opt_s), "-%s,",
					help_info[i].opt_s);
		(void)printf("%-6s--%-20s", opt_s,
			help_info[i].opt_l);

		for (ptr = start; *ptr; ptr++) {
			if (*ptr == ' ')
				space = ptr;
			wd++;
			if (wd >= cols - 28) {
				const size_t n = (size_t)(space - start);

				if (!first)
					(void)printf("%-28s", "");
				first = false;
				(void)printf("%*.*s\n", (int)n, (int)n, start);
				start = space + 1;
				wd = 0;
			}
		}
		if (start != ptr) {
			const int n = (int)(ptr - start);
			if (!first)
				(void)printf("%-28s", "");
			(void)printf("%*.*s\n", n, n, start);
		}
	}
}

/*
 *  stress_verfiable_mode()
 *	show the stressors that are verified by their verify mode
 */
static void stress_verifiable_mode(stress_verify_t mode)
{
	size_t i;
	bool space = false;

	for (i = 0; stressors[i].name; i++)
		if (stressors[i].info->verify == mode) {
			char munged[64];

			(void)stress_munge_underscore(munged, stressors[i].name, sizeof(munged));
			(void)printf("%s%s", space ? " " : "", munged);
			space = true;
		}
	(void)putchar('\n');
}

/*
 *  stress_verfiable()
 *	show the stressors that have --verify ability
 */
static void stress_verifiable(void)
{
	(void)printf("Verification always enabled:\n");
	stress_verifiable_mode(VERIFY_ALWAYS);
	(void)printf("\nVerification enabled by --verify option:\n");
	stress_verifiable_mode(VERIFY_OPTIONAL);
	(void)printf("\nVerification not implemented:\n");
	stress_verifiable_mode(VERIFY_NONE);
}

/*
 *  stress_usage_help_stressors()
 *	show per stressor help information
 */
static void stress_usage_help_stressors(void)
{
	size_t i;

	for (i = 0; stressors[i].id != STRESS_MAX; i++) {
		if (stressors[i].info->help)
			stress_usage_help(stressors[i].info->help);
	}
}

/*
 *  stress_show_stressor_names()
 *	show stressor names
 */
static inline void stress_show_stressor_names(void)
{
	size_t i;

	for (i = 0; stressors[i].name; i++) {
		char munged[64];

		(void)stress_munge_underscore(munged, stressors[i].name, sizeof(munged));
		(void)printf("%s%s", i ? " " : "", munged);
	}
	(void)putchar('\n');
}

/*
 *  stress_usage()
 *	print some help
 */
static void NORETURN stress_usage(void)
{
	stress_version();
	(void)printf("\nUsage: %s [OPTION [ARG]]\n", g_app_name);
	(void)printf("\nGeneral control options:\n");
	stress_usage_help(help_generic);
	(void)printf("\nStressor specific options:\n");
	stress_usage_help_stressors();
	(void)printf("\nExample: %s --cpu 8 --io 4 --vm 2 --vm-bytes 128M "
		"--fork 4 --timeout 10s\n\n"
		"Note: Sizes can be suffixed with B,K,M,G and times with "
		"s,m,h,d,y\n", g_app_name);
	stress_settings_free();
	stress_temp_path_free();
	exit(EXIT_SUCCESS);
}

/*
 *  stress_opt_name()
 *	find name associated with an option value
 */
static const char *stress_opt_name(const int opt_val)
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
 *  stress_stressor_finished()
 *	mark a stressor process as complete
 */
static inline void stress_stressor_finished(pid_t *pid)
{
	*pid = 0;
}

/*
 *  stress_kill_stressors()
 * 	kill stressor tasks using signal sig
 */
static void stress_kill_stressors(const int sig, const bool force_sigkill)
{
	static int count = 0;
	int signum = sig;
	stress_stressor_t *ss;

	if (force_sigkill) {
		/* multiple calls will always fallback to SIGKILL */
		count++;
		if (count > 5) {
			pr_dbg("killing processes with SIGKILL\n");
			signum = SIGKILL;
		}
	}

	for (ss = stressors_head; ss; ss = ss->next) {
		int32_t i;

		for (i = 0; i < ss->started_instances; i++) {
			stress_stats_t *const stats = ss->stats[i];
			const pid_t pid = stats->pid;

			if (pid && !stats->signalled) {
				(void)kill(pid, signum);
				stats->signalled = true;
			}
		}
	}
}

/*
 *  stress_exit_status_to_string()
 *	map stress-ng exit status returns into text
 */
static const char *stress_exit_status_to_string(const int status)
{
	typedef struct {
		const int status;
		const char *description;
	} stress_exit_status_map_t;

	static const stress_exit_status_map_t stress_exit_status_map[] = {
		{ EXIT_SUCCESS,			"success" },
		{ EXIT_FAILURE,			"stress-ng core failure " },
		{ EXIT_NOT_SUCCESS,		"stressor failed" },
		{ EXIT_NO_RESOURCE,		"no resource(s)" },
		{ EXIT_NOT_IMPLEMENTED,		"not implemented" },
		{ EXIT_SIGNALED,		"killed by signal" },
		{ EXIT_BY_SYS_EXIT,		"stressor terminated using _exit()" },
		{ EXIT_METRICS_UNTRUSTWORTHY,	"metrics may be untrustworthy" },
	};
	size_t i;

	for (i = 0; i < SIZEOF_ARRAY(stress_exit_status_map); i++) {
		if (status == stress_exit_status_map[i].status)
			return stress_exit_status_map[i].description;
	}
	return "unknown";
}

/*
 *  Filter out dot files . and ..
 */
static int stress_dot_filter(const struct dirent *d)
{
	if (d->d_name[0] == '.') {
		if (d->d_name[1] == '\0')
			return 0;
		if ((d->d_name[1] == '.') && (d->d_name[2] == '\0'))
			return 0;
	}
	return 1;
}

static void stress_unset_inode_flags(const char *filename, const int flag)
{
#if defined(FS_IOC_SETFLAGS)
	int fd;
        const long int new_flag = 0;

	fd = open(filename, O_RDWR | flag);
	if (fd < 0)
		return;

        VOID_RET(int, ioctl(fd, FS_IOC_SETFLAGS, &new_flag));
	(void)close(fd);
#else
	(void)filename;
	(void)flag;
#endif
}

/*
 *  stress_clean_dir_files()
 *  	recursively delete files in directories
 */
static void stress_clean_dir_files(
	const char *temp_path,
	const size_t temp_path_len,
	char *path,
	const size_t path_posn)
{
	struct stat statbuf;
	char *ptr = path + path_posn;
	char *end = path + PATH_MAX;
	int n;
	struct dirent **names = NULL;

	if (stat(path, &statbuf) < 0) {
		pr_dbg("stress-ng: failed to stat %s, errno=%d (%s)\n", path, errno, strerror(errno));
		return;
	}

	/* We don't follow symlinks */
	if (S_ISLNK(statbuf.st_mode))
		return;

	/* We don't remove paths with .. in */
	if (strstr(path, ".."))
		return;

	/* We don't remove paths that our out of the scope */
	if (strncmp(path, temp_path, temp_path_len))
		return;

	n = scandir(path, &names, stress_dot_filter, alphasort);
	if (n < 0) {
		(void)shim_rmdir(path);
		return;
	}

	while (n--) {
		size_t name_len = strlen(names[n]->d_name) + 1;
#if !defined(DT_DIR) ||	\
    !defined(DT_LNK) ||	\
    !defined(DT_REG)
		int ret;
#endif

		/* No more space */
		if (ptr + name_len > end) {
			free(names[n]);
			continue;
		}

		(void)snprintf(ptr, (size_t)(end - ptr), "/%s", names[n]->d_name);
		name_len = strlen(ptr);

#if defined(DT_DIR) &&	\
    defined(DT_LNK) &&	\
    defined(DT_REG)
		/* Modern fast d_type method */
		switch (names[n]->d_type) {
		case DT_DIR:
			free(names[n]);
#if defined(O_DIRECTORY)
			stress_unset_inode_flags(temp_path, O_DIRECTORY);
#endif
			stress_clean_dir_files(temp_path, temp_path_len, path, path_posn + name_len);
			(void)shim_rmdir(path);
			break;
		case DT_LNK:
		case DT_REG:
			free(names[n]);
			stress_unset_inode_flags(temp_path, 0);
			(void)shim_unlink(path);
			break;
		default:
			free(names[n]);
			break;
		}
#else
		/* Slower stat method */
		free(names[n]);
		ret = stat(path, &statbuf);
		if (ret < 0)
			continue;

		if ((statbuf.st_mode & S_IFMT) == S_IFDIR) {
#if defined(O_DIRECTORY)
			stress_unset_inode_flags(temp_path, O_DIRECTORY);
#endif
			stress_clean_dir_files(temp_path, temp_path_len, path, path_posn + name_len);
			(void)shim_rmdir(path);
		} else if (((statbuf.st_mode & S_IFMT) == S_IFLNK) ||
			   ((statbuf.st_mode & S_IFMT) == S_IFREG)) {
			stress_unset_inode_flags(temp_path, 0);
			(void)unlink(path);
		}
#endif
	}
	*ptr = '\0';
	free(names);
	(void)shim_rmdir(path);
}

/*
 *  stress_clean_dir()
 *	perform tidy up of any residual temp files; this
 *	happens if a stressor was terminated before it could
 *	tidy itself up, e.g. OOM'd or KILL'd
 */
static void stress_clean_dir(
	const char *name,
	const pid_t pid,
	const uint32_t instance)
{
	char path[PATH_MAX];
	const char *temp_path = stress_get_temp_path();
	const size_t temp_path_len = strlen(temp_path);

	(void)stress_temp_dir(path, sizeof(path), name, pid, instance);
	if (access(path, F_OK) == 0) {
		pr_dbg("%s: removing temporary files in %s\n", name, path);
		stress_clean_dir_files(temp_path, temp_path_len, path, strlen(path));
	}
}

#if defined(HAVE_SCHED_GETAFFINITY) &&	\
    NEED_GLIBC(2,3,0)
/*
 *  stress_wait_aggressive()
 *	while waiting for stressors to complete add some aggressive
 *	CPU affinity changing to exercise the scheduler placement
 */
static void stress_wait_aggressive(stress_stressor_t *stressors_list)
{
	stress_stressor_t *ss;
	cpu_set_t proc_mask;
	const int32_t ticks_per_sec = stress_get_ticks_per_second() * 5;
	const useconds_t usec_sleep =
		ticks_per_sec ? 1000000 / (useconds_t)ticks_per_sec : 1000000 / 250;

	while (wait_flag) {
		const int32_t cpus = stress_get_processors_configured();
		bool procs_alive = false;

		/*
		 *  If we can't get the mask, then don't do
		 *  any affinity twiddling
		 */
		if (sched_getaffinity(0, sizeof(proc_mask), &proc_mask) < 0)
			return;
		if (!CPU_COUNT(&proc_mask))	/* Highly unlikely */
			return;

		(void)shim_usleep(usec_sleep);

		for (ss = stressors_list; ss; ss = ss->next) {
			int32_t j;

			for (j = 0; j < ss->started_instances; j++) {
				const stress_stats_t *const stats = ss->stats[j];
				const pid_t pid = stats->pid;

				if (pid) {
					cpu_set_t mask;
					int32_t cpu_num;
					int status, ret;

					ret = waitpid(pid, &status, WNOHANG);
					if ((ret < 0) && (errno == ESRCH))
						continue;
					procs_alive = true;

					do {
						cpu_num = (int32_t)stress_mwc32modn(cpus);
					} while (!(CPU_ISSET(cpu_num, &proc_mask)));

					CPU_ZERO(&mask);
					CPU_SET(cpu_num, &mask);
					if (sched_setaffinity(pid, sizeof(mask), &mask) < 0)
						return;
				}
			}
		}
		if (!procs_alive)
			break;
	}
}
#endif

/*
 *   stress_wait_pid()
 *	wait for a stressor by their given pid
 */
static void stress_wait_pid(
	stress_stressor_t *ss,
	const pid_t pid,
	const char *stressor_name,
	stress_stats_t *stats,
	bool *success,
	bool *resource_success,
	bool *metrics_success)
{
	int status, ret;
	bool do_abort = false;

redo:
	ret = shim_waitpid(pid, &status, 0);
	if (ret > 0) {
		int wexit_status = WEXITSTATUS(status);

		if (WIFSIGNALED(status)) {
#if defined(WTERMSIG)
			const int wterm_signal = WTERMSIG(status);

			if (wterm_signal != SIGALRM) {
#if NEED_GLIBC(2,1,0)
				const char *signame = strsignal(wterm_signal);

				pr_dbg("process [%d] (%s) terminated on signal: %d (%s)\n",
					ret, stressor_name, wterm_signal, signame);
#else
				pr_dbg("process [%d] (%s) terminated on signal: %d\n",
					ret, stressor_name, wterm_signal);
#endif
			}
#else
			pr_dbg("process [%d] (%s) terminated on signal\n",
				ret, stressor_name);
#endif
			/*
			 *  If the stressor got killed by OOM or SIGKILL
			 *  then somebody outside of our control nuked it
			 *  so don't necessarily flag that up as a direct
			 *  failure.
			 */
			if (stress_process_oomed(ret)) {
				pr_dbg("process [%d] (%s) was killed by the OOM killer\n",
					ret, stressor_name);
			} else if (wterm_signal == SIGKILL) {
				pr_dbg("process [%d] (%s) was possibly killed by the OOM killer\n",
					ret, stressor_name);
			} else if (wterm_signal != SIGALRM) {
				*success = false;
			}
		}
		switch (wexit_status) {
		case EXIT_SUCCESS:
			ss->status[STRESS_STRESSOR_STATUS_PASSED]++;
			break;
		case EXIT_NO_RESOURCE:
			ss->status[STRESS_STRESSOR_STATUS_SKIPPED]++;
			pr_warn_skip("process [%d] (%s) aborted early, out of system resources\n",
				ret, stressor_name);
			*resource_success = false;
			do_abort = true;
			break;
		case EXIT_NOT_IMPLEMENTED:
			ss->status[STRESS_STRESSOR_STATUS_SKIPPED]++;
			do_abort = true;
			break;
			case EXIT_SIGNALED:
			do_abort = true;
#if defined(STRESS_REPORT_EXIT_SIGNALED)
			pr_dbg("process [%d] (%s) aborted via a termination signal\n",
				ret, stressor_name);
#endif
			break;
		case EXIT_BY_SYS_EXIT:
			ss->status[STRESS_STRESSOR_STATUS_FAILED]++;
			pr_dbg("process [%d] (%s) aborted via exit() which was not expected\n",
				ret, stressor_name);
			do_abort = true;
			break;
		case EXIT_METRICS_UNTRUSTWORTHY:
			ss->status[STRESS_STRESSOR_STATUS_FAILED]++;
			*metrics_success = false;
			break;
		case EXIT_FAILURE:
			ss->status[STRESS_STRESSOR_STATUS_FAILED]++;
			/*
			 *  Stressors should really return EXIT_NOT_SUCCESS
			 *  as EXIT_FAILURE should indicate a core stress-ng
			 *  problem.
			 */
			wexit_status = EXIT_NOT_SUCCESS;
		CASE_FALLTHROUGH;
		default:
			pr_err("process [%d] (%s) terminated with an error, exit status=%d (%s)\n",
				ret, stressor_name, wexit_status,
				stress_exit_status_to_string(wexit_status));
			*success = false;
			do_abort = true;
			break;
		}
		if ((g_opt_flags & OPT_FLAGS_ABORT) && do_abort) {
			keep_stressing_set_flag(false);
			wait_flag = false;
			stress_kill_stressors(SIGALRM, true);
		}

		stress_stressor_finished(&stats->pid);
		pr_dbg("process [%d] terminated\n", ret);
	} else if (ret == -1) {
		/* Somebody interrupted the wait */
		if (errno == EINTR)
			goto redo;
		/* This child did not exist, mark it done anyhow */
		if (errno == ECHILD)
			stress_stressor_finished(&stats->pid);
	}
}

/*
 *  stress_wait_stressors()
 * 	wait for stressor child processes
 */
static void stress_wait_stressors(
	stress_stressor_t *stressors_list,
	bool *success,
	bool *resource_success,
	bool *metrics_success)
{
	stress_stressor_t *ss;

#if defined(HAVE_SCHED_GETAFFINITY) &&	\
    NEED_GLIBC(2,3,0)
	/*
	 *  On systems that support changing CPU affinity
	 *  we keep on moving processes between processors
	 *  to impact on memory locality (e.g. NUMA) to
	 *  try to thrash the system when in aggressive mode
	 */
	if (g_opt_flags & OPT_FLAGS_AGGRESSIVE)
		stress_wait_aggressive(stressors_list);
#endif
	for (ss = stressors_list; ss; ss = ss->next) {
		int32_t j;

		for (j = 0; j < ss->started_instances; j++) {
			stress_stats_t *const stats = ss->stats[j];
			const pid_t pid = stats->pid;

			if (pid) {
				char munged[64];

				(void)stress_munge_underscore(munged, ss->stressor->name, sizeof(munged));
				stress_wait_pid(ss, pid, munged, stats, success, resource_success, metrics_success);
				stress_clean_dir(munged, pid, (uint32_t)j);
			}
		}
	}
	if (g_opt_flags & OPT_FLAGS_IGNITE_CPU)
		stress_ignite_cpu_stop();
}

/*
 *  stress_handle_terminate()
 *	catch terminating signals
 */
static void MLOCKED_TEXT stress_handle_terminate(int signum)
{
	static char buf[128];
	const int fd = fileno(stderr);
	terminate_signum = signum;
	keep_stressing_set_flag(false);

	switch (signum) {
	case SIGILL:
	case SIGSEGV:
	case SIGFPE:
	case SIGBUS:
		/*
		 *  Critical failure, report and die ASAP
		 */
		(void)snprintf(buf, sizeof(buf), "%s: info:  [%d] stressor terminated with unexpected signal %s\n",
			g_app_name, (int)getpid(), stress_strsignal(signum));
		VOID_RET(ssize_t, write(fd, buf, strlen(buf)));
		stress_kill_stressors(SIGALRM, true);
		_exit(EXIT_SIGNALED);
	default:
		/*
		 *  Kill stressors
		 */
		stress_kill_stressors(SIGALRM, true);
		break;
	}
}

/*
 *  stress_get_nth_stressor()
 *	return nth stressor from list
 */
static stress_stressor_t *stress_get_nth_stressor(const uint32_t n)
{
	stress_stressor_t *ss = stressors_head;
	uint32_t i;

	for (i = 0; ss && (i < n); i++)
		ss = ss->next;

	return ss;
}

/*
 *  stress_get_num_stressors()
 *	return number of stressors in stressor list
 */
static uint32_t stress_get_num_stressors(void)
{
	uint32_t n = 0;
	stress_stressor_t *ss;

	for (ss = stressors_head; ss; ss = ss->next)
		n++;

	return n;
}

/*
 *  stress_stressors_free()
 *	free stressor info from stressor list
 */
static void stress_stressors_free(void)
{
	stress_stressor_t *ss = stressors_head;

	while (ss) {
		stress_stressor_t *next = ss->next;

		free(ss->stats);
		free(ss);

		ss = next;
	}

	stressors_head = NULL;
	stressors_tail = NULL;
}

/*
 *  stress_get_total_num_instances()
 *	deterimine number of runnable stressors from list
 */
static int32_t stress_get_total_num_instances(stress_stressor_t *stressors_list)
{
	int32_t total_num_instances = 0;
	stress_stressor_t *ss;

	for (ss = stressors_list; ss; ss = ss->next)
		total_num_instances += ss->num_instances;

	return total_num_instances;
}

/*
 *  stress_child_atexit(void)
 *	handle unexpected exit() call in child stressor
 */
static void NORETURN stress_child_atexit(void)
{
	_exit(EXIT_BY_SYS_EXIT);
}

/*
 *  stress_metrics_set_const_check()
 *	set metrics with given description a value. If const_description is
 *	true then the description is a literal string and does not need
 *	to be dup'd from the shared memory heap, otherwise it's a stack
 *	based string and needs to be dup'd so it does not go out of scope.
 *
 *	Note that stress_shared_heap_dup_const will dup a string using
 *	special reserved shared heap that all stressors can access. The
 *	returned string must not be written to. It may even be a cached
 *	copy of another dup by another stressor process (to save memory).
 */
void stress_metrics_set_const_check(
	const stress_args_t *args,
	const size_t idx,
	char *description,
	const bool const_description,
	const double value)
{
	stress_metrics_data_t *metrics;

	if (idx >= STRESS_MISC_METRICS_MAX)
		return;
	if (!args)
		return;
	metrics = args->metrics;
	if (!metrics)
		return;

	metrics[idx].description = const_description ?
		description :
		stress_shared_heap_dup_const(description);
	if (metrics[idx].description)
		metrics[idx].value = value;
}

#if defined(HAVE_GETRUSAGE)
/*
 *  stress_getrusage()
 *	accumulate rusgage stats
 */
static void stress_getrusage(const int who, stress_stats_t *stats)
{
	struct rusage usage;

	if (shim_getrusage(who, &usage) == 0) {
		stats->rusage_utime += (double)usage.ru_utime.tv_sec + ((double)usage.ru_utime.tv_usec) / STRESS_DBL_MICROSECOND;
		stats->rusage_stime += (double)usage.ru_stime.tv_sec + ((double)usage.ru_stime.tv_usec) / STRESS_DBL_MICROSECOND;
#if defined(HAVE_RUSAGE_RU_MAXRSS)
		if (stats->rusage_maxrss < usage.ru_maxrss)
			stats->rusage_maxrss = usage.ru_maxrss;
#else
		stats->rusage_maxrss = 0;	/* Not available */
#endif
	}
}
#endif

/*
 *  stress_run()
 *	kick off and run stressors
 */
static void MLOCKED_TEXT stress_run(
	stress_stressor_t *stressors_list,
	double *duration,
	bool *success,
	bool *resource_success,
	bool *metrics_success,
	stress_checksum_t **checksum)
{
	double time_start, time_finish;
	int32_t started_instances = 0;
	const size_t page_size = stress_get_page_size();
	int64_t backoff = DEFAULT_BACKOFF;
	int32_t ionice_class = UNDEFINED;
	int32_t ionice_level = UNDEFINED;

	wait_flag = true;
	time_start = stress_time_now();
	pr_dbg("starting stressors\n");

	(void)stress_get_setting("backoff", &backoff);
	(void)stress_get_setting("ionice-class", &ionice_class);
	(void)stress_get_setting("ionice-level", &ionice_level);

	/*
	 *  Work through the list of stressors to run
	 */
	for (g_stressor_current = stressors_list; g_stressor_current; g_stressor_current = g_stressor_current->next) {
		int32_t j;

		/*
		 *  Each stressor has 1 or more instances to run
		 */
		for (j = 0; j < g_stressor_current->num_instances; j++, (*checksum)++) {
			int rc = EXIT_SUCCESS;
			size_t i;
			pid_t pid, child_pid;
			char name[64];
			stress_stats_t *const stats = g_stressor_current->stats[j];
			double run_duration, fork_time_start;
			bool ok;

			if (g_opt_timeout && (stress_time_now() - time_start > (double)g_opt_timeout))
				goto abort;

			stats->ci.counter_ready = true;
			stats->ci.counter = 0;
			stats->checksum = *checksum;
			for (i = 0; i < SIZEOF_ARRAY(stats->metrics); i++) {
				stats->metrics[i].value = -1.0;
				stats->metrics[i].description = NULL;
			}
again:
			if (!keep_stressing_flag())
				break;
			fork_time_start = stress_time_now();
			pid = fork();
			switch (pid) {
			case -1:
				if (errno == EAGAIN) {
					(void)shim_usleep(100000);
					goto again;
				}
				pr_err("Cannot fork: errno=%d (%s)\n",
					errno, strerror(errno));
				stress_kill_stressors(SIGALRM, false);
				goto wait_for_stressors;
			case 0:
				/* Child */
				child_pid = getpid();

				(void)stress_munge_underscore(name, g_stressor_current->stressor->name, sizeof(name));
				stress_set_proc_state(name, STRESS_STATE_START);

				(void)sched_settings_apply(true);
				(void)atexit(stress_child_atexit);
				if (stress_set_handler(name, true) < 0) {
					rc = EXIT_FAILURE;
					goto child_exit;
				}
				stress_parent_died_alarm();
				stress_process_dumpable(false);
				stress_set_timer_slack();

				if (g_opt_timeout)
					(void)alarm((unsigned int)g_opt_timeout);

				stress_set_proc_state(name, STRESS_STATE_INIT);
				stress_mwc_reseed();
				stress_set_oom_adjustment(name, false);
				stress_set_max_limits();
				stress_set_iopriority(ionice_class, ionice_level);
				(void)umask(0077);

				pr_dbg("%s: started [%d] (instance %" PRIu32 ")\n",
					name, (int)child_pid, j);

				stats->start = stats->finish = stress_time_now();
				stress_interrupts_start(stats->interrupts);
#if defined(STRESS_PERF_STATS) &&	\
    defined(HAVE_LINUX_PERF_EVENT_H)
				if (g_opt_flags & OPT_FLAGS_PERF_STATS)
					(void)stress_perf_open(&stats->sp);
#endif
				(void)shim_usleep((useconds_t)(backoff * started_instances));
#if defined(STRESS_PERF_STATS) &&	\
    defined(HAVE_LINUX_PERF_EVENT_H)
				if (g_opt_flags & OPT_FLAGS_PERF_STATS)
					(void)stress_perf_enable(&stats->sp);
#endif
				if (keep_stressing_flag() && !(g_opt_flags & OPT_FLAGS_DRY_RUN)) {
					const stress_args_t args = {
						.ci = &stats->ci,
						.name = name,
						.max_ops = g_stressor_current->bogo_ops,
						.instance = (uint32_t)j,
						.num_instances = (uint32_t)g_stressor_current->num_instances,
						.pid = child_pid,
						.page_size = page_size,
						.mapped = &g_shared->mapped,
						.metrics = stats->metrics,
						.info = g_stressor_current->stressor->info
					};

					(void)shim_memset(*checksum, 0, sizeof(**checksum));
					rc = g_stressor_current->stressor->info->stressor(&args);
					stress_interrupts_stop(stats->interrupts);
					stress_interrupts_check_failure(name, stats->interrupts, j, &rc);
					pr_fail_check(&rc);

#if defined(SA_SIGINFO) &&	\
    defined(SI_USER)
					/*
					 *  Sanity check if process was killed by
					 *  an external SIGALRM source
					 */
					if (g_sigalrm_info.triggered && (g_sigalrm_info.code == SI_USER)) {
						time_t t = g_sigalrm_info.when.tv_sec;
						const struct tm *tm = localtime(&t);

						if (tm) {
							pr_inf("%s: terminated by SIGALRM externally at %2.2d:%2.2d:%2.2d.%2.2ld by user %d\n",
								name,
								tm->tm_hour, tm->tm_min, tm->tm_sec,
								(long)g_sigalrm_info.when.tv_usec / 10000,
								g_sigalrm_info.uid);
						} else {
							pr_inf("%s: terminated by SIGALRM externally by user %d\n",
								name, g_sigalrm_info.uid);
						}
					}
#endif

					ok = (rc == EXIT_SUCCESS);
					stats->ci.run_ok = ok;
					(*checksum)->data.ci.run_ok = ok;
					/* Ensure reserved padding is zero to not confuse checksum */
					(void)shim_memset((*checksum)->data.reserved, 0, sizeof((*checksum)->data.reserved));

					/*
					 *  We're done, cancel SIGALRM
					 */
					(void)alarm(0);

					stress_set_proc_state(name, STRESS_STATE_STOP);
					/*
					 *  Bogo ops counter should be OK for reading,
					 *  if not then flag up that the counter may
					 *  be untrustyworthy
					 */
					if ((!stats->ci.counter_ready) && (!stats->ci.force_killed)) {
						pr_warn("%s: WARNING: bogo-ops counter in non-ready state, "
							"metrics are untrustworthy (process may have been "
							"terminated prematurely)\n",
							name);
						rc = EXIT_METRICS_UNTRUSTWORTHY;
					}
					(*checksum)->data.ci.counter = args.ci->counter;
					stress_hash_checksum(*checksum);
				}
#if defined(STRESS_PERF_STATS) &&	\
    defined(HAVE_LINUX_PERF_EVENT_H)
				if (g_opt_flags & OPT_FLAGS_PERF_STATS) {
					(void)stress_perf_disable(&stats->sp);
					(void)stress_perf_close(&stats->sp);
				}
#endif
#if defined(STRESS_THERMAL_ZONES)
				if (g_opt_flags & OPT_FLAGS_THERMAL_ZONES)
					(void)stress_tz_get_temperatures(&g_shared->tz_info, &stats->tz);
#endif
				stats->finish = stress_time_now();
#if defined(HAVE_GETRUSAGE)
				stats->rusage_utime = 0.0;
				stats->rusage_stime = 0.0;
				stress_getrusage(RUSAGE_SELF, stats);
				stress_getrusage(RUSAGE_CHILDREN, stats);
#else
				(void)shim_memset(&stats->tms, 0, sizeof(stats->tms));
				if (times(&stats->tms) == (clock_t)-1) {
					pr_dbg("times failed: errno=%d (%s)\n",
						errno, strerror(errno));
				}
#endif
				pr_dbg("%s: exited [%d] (instance %" PRIu32 ")\n",
					name, (int)child_pid, j);

				/* Allow for some slops of ~0.5 secs */
				run_duration = (stats->finish - fork_time_start) + 0.5;

				/*
				 * Apparently succeeded but terminated early?
				 * Could be a bug, so report a warning
				 */
				if (stats->ci.run_ok &&
				    (g_shared && !g_shared->caught_sigint) &&
				    (run_duration < (double)g_opt_timeout) &&
				    (!(g_stressor_current->bogo_ops && stats->ci.counter >= g_stressor_current->bogo_ops))) {

					pr_warn("%s: WARNING: finished prematurely after just %.2fs%s\n",
						name, run_duration, stress_duration_to_str(run_duration));
				}

child_exit:
				stress_stressors_free();
				stress_cache_free();
				stress_settings_free();
				stress_temp_path_free();
				(void)stress_ftrace_free();

				if ((rc != 0) && (g_opt_flags & OPT_FLAGS_ABORT)) {
					keep_stressing_set_flag(false);
					wait_flag = false;
					(void)kill(getppid(), SIGALRM);
				}
				stress_set_proc_state(name, STRESS_STATE_EXIT);
				if (terminate_signum)
					rc = EXIT_SIGNALED;
				pr_lock_exited(child_pid);
				_exit(rc);
			default:
				if (pid > -1) {
					stats->pid = pid;
					stats->signalled = false;
					g_stressor_current->started_instances++;
					started_instances++;
					stress_ftrace_add_pid(pid);
				}

				/* Forced early abort during startup? */
				if (!keep_stressing_flag()) {
					pr_dbg("abort signal during startup, cleaning up\n");
					stress_kill_stressors(SIGALRM, true);
					goto wait_for_stressors;
				}
				break;
			}
		}
	}
	(void)stress_set_handler("stress-ng", false);
	if (g_opt_timeout)
		(void)alarm((unsigned int)g_opt_timeout);

abort:
	pr_dbg("%d stressor%s started\n", started_instances,
		 started_instances == 1 ? "" : "s");

wait_for_stressors:
	if (g_opt_flags & OPT_FLAGS_IGNITE_CPU)
		stress_ignite_cpu_start();

	stress_wait_stressors(stressors_list, success, resource_success, metrics_success);
	time_finish = stress_time_now();

	*duration += time_finish - time_start;
}

/*
 *  stress_show_stressors()
 *	show names of stressors that are going to be run
 */
static int stress_show_stressors(void)
{
	char *newstr, *str = NULL;
	ssize_t len = 0;
	char buffer[64];
	bool previous = false;
	stress_stressor_t *ss;

	for (ss = stressors_head; ss; ss = ss->next) {
		const int32_t n = ss->num_instances;

		if (n) {
			char munged[64];
			ssize_t buffer_len;

			(void)stress_munge_underscore(munged, ss->stressor->name, sizeof(munged));
			buffer_len = snprintf(buffer, sizeof(buffer),
					"%s %" PRId32 " %s", previous ? "," : "", n, munged);
			previous = true;
			if (buffer_len >= 0) {
				newstr = realloc(str, (size_t)(len + buffer_len + 1));
				if (!newstr) {
					pr_err("Cannot allocate temporary buffer\n");
					free(str);
					return -1;
				}
				str = newstr;
				(void)shim_strlcpy(str + len, buffer, (size_t)(buffer_len + 1));
			}
			len += buffer_len;
		}
	}
	pr_inf("dispatching hogs:%s\n", str ? str : "");
	free(str);

	return 0;
}

/*
 *  stress_exit_status_type()
 *	report exit status of all instances of a given status type
 */
static void stress_exit_status_type(const char *name, const size_t type)
{
	stress_stressor_t *ss;

	char *str;
	size_t str_len = 1;
	uint32_t n = 0;

	str = malloc(1);
	if (!str)
		return;
	*str = '\0';

	for (ss = stressors_head; ss; ss = ss->next) {
		if (ss->status[type] > 0) {
			char buf[80], munged[64];
			char *new_str;
			size_t buf_len;

			(void)stress_munge_underscore(munged, ss->stressor->name, sizeof(munged));
			(void)snprintf(buf, sizeof(buf), " %s (%" PRIu32")", munged, ss->status[type]);
			buf_len = strlen(buf);
			new_str = realloc(str, str_len + buf_len);
			if (!new_str) {
				free(str);
				return;
			}
			str = new_str;
			str_len += buf_len;
			shim_strlcat(str, buf, str_len);
			n += ss->status[type];
		}
	}
	if (n) {
		pr_inf("%s: %" PRIu32 ":%s\n", name, n, str);
	} else  {
		pr_inf("%s: 0\n", name);
	}
	free(str);
}

/*
 *  stress_exit_status_summary()
 *	provide summary of exit status of all instances
 */
static void stress_exit_status_summary(void)
{
	stress_exit_status_type("passed", STRESS_STRESSOR_STATUS_PASSED);
	stress_exit_status_type("failed", STRESS_STRESSOR_STATUS_FAILED);
	stress_exit_status_type("skipped", STRESS_STRESSOR_STATUS_SKIPPED);
}

/*
 *  stress_metrics_check()
 *	as per ELISA request, sanity check bogo ops and run flag
 *	to see if corruption occurred and print failure messages
 *	and set *success to false if hash and data is dubious.
 */
static void stress_metrics_check(bool *success)
{
	stress_stressor_t *ss;
	bool ok = true;
	uint64_t counter_check = 0;
	double min_run_time = DBL_MAX;

	for (ss = stressors_head; ss; ss = ss->next) {
		int32_t j;

		for (j = 0; j < ss->started_instances; j++) {
			const stress_stats_t *const stats = ss->stats[j];
			const stress_checksum_t *checksum = stats->checksum;
			stress_checksum_t stats_checksum;
			const double duration = stats->finish - stats->start;

			counter_check |= stats->ci.counter;
			if (duration < min_run_time)
				min_run_time = duration;

			if (checksum == NULL) {
				pr_fail("%s instance %d unexpected null checksum data\n",
					ss->stressor->name, j);
				ok = false;
				continue;
			}

			(void)shim_memset(&stats_checksum, 0, sizeof(stats_checksum));
			stats_checksum.data.ci.counter = stats->ci.counter;
			stats_checksum.data.ci.run_ok = stats->ci.run_ok;
			stress_hash_checksum(&stats_checksum);

			if (stats->ci.counter != checksum->data.ci.counter) {
				pr_fail("%s instance %d corrupted bogo-ops counter, %" PRIu64 " vs %" PRIu64 "\n",
					ss->stressor->name, j,
					stats->ci.counter, checksum->data.ci.counter);
				ok = false;
			}
			if (stats->ci.run_ok != checksum->data.ci.run_ok) {
				pr_fail("%s instance %d corrupted run flag, %d vs %d\n",
					ss->stressor->name, j,
					stats->ci.run_ok, checksum->data.ci.run_ok);
				ok = false;
			}
			if (stats_checksum.hash != checksum->hash) {
				pr_fail("%s instance %d hash error in bogo-ops counter and run flag, %" PRIu32 " vs %" PRIu32 "\n",
					ss->stressor->name, j,
					stats_checksum.hash, checksum->hash);
				ok = false;
			}
		}
	}

	/*
	 *  Bogo ops counter should be not zero for the majority of
	 *  stressors after 30 seconds of run time
	 */
	if (!counter_check && (min_run_time > 30.0))
		pr_warn("metrics-check: all bogo-op counters are zero, data may be incorrect\n");

	if (ok) {
		pr_dbg("metrics-check: all stressor metrics validated and sane\n");
	} else {
		pr_fail("metrics-check: stressor metrics corrupted, data is compromised\n");
		*success = false;
	}
}

static char *stess_description_yamlify(const char *description)
{
	static char yamlified[40];
	char *dst, *end = yamlified + sizeof(yamlified);
	const char *src;

	for (dst = yamlified, src = description; *src; src++) {
		register int ch = (int)*src;

		if (isalpha(ch)) {
			*(dst++) = (char)tolower(ch);
		} else if (isdigit(ch)) {
			*(dst++) = (char)ch;
		} else if (ch == ' ') {
			*(dst++) = '-';
		}
		if (dst >= end - 1)
			break;
	}
	*dst = '\0';

	return yamlified;
}

/*
 *  stress_metrics_dump()
 *	output metrics
 */
static void stress_metrics_dump(
	FILE *yaml,
	const int32_t ticks_per_sec)
{
	stress_stressor_t *ss;
	bool misc_metrics = false;

	pr_lock();
	if (g_opt_flags & OPT_FLAGS_METRICS_BRIEF) {
		pr_metrics("%-13s %9.9s %9.9s %9.9s %9.9s %12s %14s\n",
			   "stressor", "bogo ops", "real time", "usr time",
			   "sys time", "bogo ops/s", "bogo ops/s");
		pr_metrics("%-13s %9.9s %9.9s %9.9s %9.9s %12s %14s\n",
			   "", "", "(secs) ", "(secs) ", "(secs) ", "(real time)",
			   "(usr+sys time)");
	} else {
		pr_metrics("%-13s %9.9s %9.9s %9.9s %9.9s %12s %14s %12.12s %13.13s\n",
			   "stressor", "bogo ops", "real time", "usr time",
			   "sys time", "bogo ops/s", "bogo ops/s", "CPU used per",
			   "RSS Max");
		pr_metrics("%-13s %9.9s %9.9s %9.9s %9.9s %12s %14s %12.12s %13.13s\n",
			   "", "", "(secs) ", "(secs) ", "(secs) ", "(real time)",
			   "(usr+sys time)","instance (%)", "(KB)");
	}
	pr_yaml(yaml, "metrics:\n");

	for (ss = stressors_head; ss; ss = ss->next) {
		uint64_t c_total = 0;
		double   r_total = 0.0, u_total = 0.0, s_total = 0.0;
		long int maxrss = 0;
		int32_t  j;
		size_t i;
		char munged[64];
		double u_time, s_time, t_time, bogo_rate_r_time, bogo_rate, cpu_usage;
		bool run_ok = false;

		if (!ss->stats)
			continue;

		(void)stress_munge_underscore(munged, ss->stressor->name, sizeof(munged));

		for (j = 0; j < ss->started_instances; j++) {
			const stress_stats_t *const stats = ss->stats[j];

			run_ok  |= stats->ci.run_ok;
			c_total += stats->ci.counter;
#if defined(HAVE_GETRUSAGE)
			u_total += stats->rusage_utime;
			s_total += stats->rusage_stime;
#if defined(HAVE_RUSAGE_RU_MAXRSS)
			if (maxrss < stats->rusage_maxrss)
				maxrss = stats->rusage_maxrss;
#endif
#else
			u_total += (double)(stats->tms.tms_utime + stats->tms.tms_cutime);
			s_total += (double)(stats->tms.tms_stime + stats->tms.tms_cstime);
#endif
			r_total += stats->finish - stats->start;
		}
		/* Real time in terms of average wall clock time of all procs */
		r_total = ss->started_instances ?
			r_total / (double)ss->started_instances : 0.0;

		if ((g_opt_flags & OPT_FLAGS_METRICS_BRIEF) &&
		    (c_total == 0) && (!run_ok))
			continue;

#if defined(HAVE_GETRUSAGE)
		(void)ticks_per_sec;

		u_time = u_total;
		s_time = s_total;
#else
		u_time = (ticks_per_sec > 0) ? (double)u_total / (double)ticks_per_sec : 0.0;
		s_time = (ticks_per_sec > 0) ? (double)s_total / (double)ticks_per_sec : 0.0;
#endif
		t_time = u_time + s_time;

		/* Total usr + sys time of all procs */
		bogo_rate_r_time = (r_total > 0.0) ? (double)c_total / r_total : 0.0;
		{
			double us_total = u_time + s_time;

			bogo_rate = (us_total > 0.0) ? (double)c_total / us_total : 0.0;
		}

		cpu_usage = (r_total > 0) ? 100.0 * t_time / r_total : 0.0;
		cpu_usage = ss->started_instances ? cpu_usage / ss->started_instances : 0.0;

		if (g_opt_flags & OPT_FLAGS_METRICS_BRIEF) {
			if (g_opt_flags & OPT_FLAGS_SN) {
				pr_metrics("%-13s %9" PRIu64 " %9.3e %9.3e %9.3e %12.5e %14.5e\n",
					munged,		/* stress test name */
					c_total,	/* op count */
					r_total,	/* average real (wall) clock time */
					u_time, 	/* actual user time */
					s_time,		/* actual system time */
					bogo_rate_r_time, /* bogo ops on wall clock time */
					bogo_rate);	/* bogo ops per second */
			} else {
				pr_metrics("%-13s %9" PRIu64 " %9.2f %9.2f %9.2f %12.2f %14.2f\n",
					munged,		/* stress test name */
					c_total,	/* op count */
					r_total,	/* average real (wall) clock time */
					u_time, 	/* actual user time */
					s_time,		/* actual system time */
					bogo_rate_r_time, /* bogo ops on wall clock time */
					bogo_rate);	/* bogo ops per second */
			}
		} else {
			/* extended metrics */
			if (g_opt_flags & OPT_FLAGS_SN) {
				pr_metrics("%-13s %9" PRIu64 " %9.3e %9.3e %9.3e %12.5e %14.5e %15.4e %13ld\n",
					munged,		/* stress test name */
					c_total,	/* op count */
					r_total,	/* average real (wall) clock time */
					u_time, 	/* actual user time */
					s_time,		/* actual system time */
					bogo_rate_r_time, /* bogo ops on wall clock time */
					bogo_rate,	/* bogo ops per second */
					cpu_usage,	/* % cpu usage */
					maxrss);	/* maximum RSS in KB */
			} else {
				pr_metrics("%-13s %9" PRIu64 " %9.2f %9.2f %9.2f %12.2f %14.2f %12.2f %13ld\n",
					munged,		/* stress test name */
					c_total,	/* op count */
					r_total,	/* average real (wall) clock time */
					u_time, 	/* actual user time */
					s_time,		/* actual system time */
					bogo_rate_r_time, /* bogo ops on wall clock time */
					bogo_rate,	/* bogo ops per second */
					cpu_usage,	/* % cpu usage */
					maxrss);	/* maximum RSS in KB */
			}
		}

		if (g_opt_flags & OPT_FLAGS_SN) {
			pr_yaml(yaml, "    - stressor: %s\n", munged);
			pr_yaml(yaml, "      bogo-ops: %" PRIu64 "\n", c_total);
			pr_yaml(yaml, "      bogo-ops-per-second-usr-sys-time: %e\n", bogo_rate);
			pr_yaml(yaml, "      bogo-ops-per-second-real-time: %e\n", bogo_rate_r_time);
			pr_yaml(yaml, "      wall-clock-time: %e\n", r_total);
			pr_yaml(yaml, "      user-time: %e\n", u_time);
			pr_yaml(yaml, "      system-time: %e\n", s_time);
			pr_yaml(yaml, "      cpu-usage-per-instance: %e\n", cpu_usage);
			pr_yaml(yaml, "      max-rss: %ld\n", maxrss);
		} else {
			pr_yaml(yaml, "    - stressor: %s\n", munged);
			pr_yaml(yaml, "      bogo-ops: %" PRIu64 "\n", c_total);
			pr_yaml(yaml, "      bogo-ops-per-second-usr-sys-time: %f\n", bogo_rate);
			pr_yaml(yaml, "      bogo-ops-per-second-real-time: %f\n", bogo_rate_r_time);
			pr_yaml(yaml, "      wall-clock-time: %f\n", r_total);
			pr_yaml(yaml, "      user-time: %f\n", u_time);
			pr_yaml(yaml, "      system-time: %f\n", s_time);
			pr_yaml(yaml, "      cpu-usage-per-instance: %f\n", cpu_usage);
			pr_yaml(yaml, "      max-rss: %ld\n", maxrss);
		}

		for (i = 0; i < SIZEOF_ARRAY(ss->stats[0]->metrics); i++) {
			const char *description = ss->stats[0]->metrics[i].description;

			if (description) {
				double metric, total = 0.0;

				misc_metrics = true;
				for (j = 0; j < ss->started_instances; j++) {
					const stress_stats_t *const stats = ss->stats[j];

					total += stats->metrics[i].value;
				}
				metric = ss->started_instances ? total / ss->started_instances : 0.0;
				if (g_opt_flags & OPT_FLAGS_SN) {
					pr_yaml(yaml, "      %s: %e\n", stess_description_yamlify(description), metric);
				} else {
					pr_yaml(yaml, "      %s: %f\n", stess_description_yamlify(description), metric);
				}
			}
		}

		pr_yaml(yaml, "\n");
	}

	if (misc_metrics) {
		pr_metrics("miscellaneous metrics:\n");
		for (ss = stressors_head; ss; ss = ss->next) {
			size_t i;
			int32_t j;
			char munged[64];

			if (!ss->stats)
				continue;

			(void)stress_munge_underscore(munged, ss->stressor->name, sizeof(munged));

			for (i = 0; i < SIZEOF_ARRAY(ss->stats[0]->metrics); i++) {
				const char *description = ss->stats[0]->metrics[i].description;

				if (description) {
					int64_t exponent = 0;
					double geomean, mantissa = 1.0;
					double n = 0.0;

					for (j = 0; j < ss->started_instances; j++) {
						int e;
						const stress_stats_t *const stats = ss->stats[j];

						if (stats->metrics[i].value > 0.0) {
							const double f = frexp(stats->metrics[i].value, &e);

							mantissa *= f;
							exponent += e;
							n += 1.0;
						}
					}
					if (n > 0.0) {
						const double inverse_n = 1.0 / (double)n;

						geomean = pow(mantissa, inverse_n) * pow(2.0, (double)exponent * inverse_n);
					} else {
						geomean = 0.0;
					}
					if (g_opt_flags & OPT_FLAGS_SN) {
						pr_metrics("%-13s %13.2e %s (geometric mean of %" PRIu32 " instances)\n",
							   munged, geomean, description, ss->started_instances);
					} else {
						pr_metrics("%-13s %13.2f %s (geometric mean of %" PRIu32 " instances)\n",
							   munged, geomean, description, ss->started_instances);
					}
				}
			}
		}
	}
	pr_unlock();
}

/*
 *  stress_times_dump()
 *	output the run times
 */
static void stress_times_dump(
	FILE *yaml,
	const int32_t ticks_per_sec,
	const double duration)
{
	struct tms buf;
	double total_cpu_time = stress_get_processors_configured() * duration;
	double u_time, s_time, t_time, u_pc, s_pc, t_pc;
	double min1, min5, min15;
	int rc;

	if (!(g_opt_flags & OPT_FLAGS_TIMES))
		return;

	if (times(&buf) == (clock_t)-1) {
		pr_err("cannot get run time information: errno=%d (%s)\n",
			errno, strerror(errno));
		return;
	}
	rc = stress_get_load_avg(&min1, &min5, &min15);

	u_time = (double)buf.tms_cutime / (double)ticks_per_sec;
	s_time = (double)buf.tms_cstime / (double)ticks_per_sec;
	t_time = ((double)buf.tms_cutime + (double)buf.tms_cstime) / (double)ticks_per_sec;
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
 *  stress_log_args()
 *	dump to syslog argv[]
 */
static void stress_log_args(int argc, char **argv)
{
	size_t i, len, buflen, *arglen;
	char *buf;
	const char *user = shim_getlogin();
	const uid_t uid = getuid();

	arglen = calloc((size_t)argc, sizeof(*arglen));
	if (!arglen)
		return;

	for (buflen = 0, i = 0; i < (size_t)argc; i++) {
		arglen[i] = strlen(argv[i]);
		buflen += arglen[i] + 1;
	}

	buf = calloc(buflen, sizeof(*buf));
	if (!buf) {
		free(arglen);
		return;
	}

	for (len = 0, i = 0; i < (size_t)argc; i++) {
		if (i) {
			(void)shim_strlcat(buf + len, " ", buflen - len);
			len++;
		}
		(void)shim_strlcat(buf + len, argv[i], buflen - len);
		len += arglen[i];
	}
	if (user) {
		shim_syslog(LOG_INFO, "invoked with '%s' by user %d '%s'\n", buf, uid, user);
		pr_dbg("invoked with '%s' by user %d '%s'\n", buf, uid, user);
	} else {
		shim_syslog(LOG_INFO, "invoked with '%s' by user %d\n", buf, uid);
		pr_dbg("invoked with '%s' by user %d\n", buf, uid);
	}
	free(buf);
	free(arglen);
}

/*
 *  stress_log_system_mem_info()
 *	dump system memory info
 */
void stress_log_system_mem_info(void)
{
#if defined(HAVE_SYS_SYSINFO_H) && \
    defined(HAVE_SYSINFO) && \
    defined(HAVE_SYSLOG_H)
	struct sysinfo info;

	(void)shim_memset(&info, 0, sizeof(info));
	if (sysinfo(&info) == 0) {
		shim_syslog(LOG_INFO, "memory (MB): total %.2f, "
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
 *  stress_log_system_info()
 *	dump system info
 */
static void stress_log_system_info(void)
{
#if defined(HAVE_UNAME) && 		\
    defined(HAVE_SYS_UTSNAME_H) &&	\
    defined(HAVE_SYSLOG_H)
	struct utsname buf;

	if (uname(&buf) >= 0) {
		shim_syslog(LOG_INFO, "system: '%s' %s %s %s %s\n",
			buf.nodename,
			buf.sysname,
			buf.release,
			buf.version,
			buf.machine);
	}
#endif
}

static void *stress_map_page(int prot, char *prot_str, size_t page_size)
{
	void *ptr;

	ptr = mmap(NULL, page_size, prot,
		MAP_PRIVATE | MAP_ANON, -1, 0);
	if (ptr == MAP_FAILED) {
		pr_err("cannot mmap %s shared page, errno=%d (%s)\n",
			prot_str, errno, strerror(errno));
	}
	return ptr;
}

/*
 *  stress_shared_map()
 *	mmap shared region, with an extra page at the end
 *	that is marked read-only to stop accidental smashing
 *	from a run-away stack expansion
 */
static inline void stress_shared_map(const int32_t num_procs)
{
	const size_t page_size = stress_get_page_size();
	size_t len = sizeof(stress_shared_t) +
		     (sizeof(stress_stats_t) * (size_t)num_procs);
	size_t sz = (len + (page_size << 1)) & ~(page_size - 1);
#if defined(HAVE_MPROTECT)
	void *last_page;
#endif

	g_shared = (stress_shared_t *)mmap(NULL, sz, PROT_READ | PROT_WRITE,
		MAP_SHARED | MAP_ANON, -1, 0);
	if (g_shared == MAP_FAILED) {
		pr_err("cannot mmap to shared memory region, errno=%d (%s)\n",
			errno, strerror(errno));
		stress_stressors_free();
		exit(EXIT_FAILURE);
	}

	/* Paraniod */
	(void)shim_memset(g_shared, 0, sz);
	g_shared->length = sz;

	/*
	 * libc on some systems warn that vfork is deprecated,
	 * we know this, force warnings off where possible
	 */
STRESS_PRAGMA_PUSH
STRESS_PRAGMA_WARN_OFF
	g_shared->vfork = vfork;
STRESS_PRAGMA_POP

#if defined(HAVE_MPROTECT)
	last_page = ((uint8_t *)g_shared) + sz - page_size;

	/* Make last page trigger a segfault if it is accessed */
	(void)mprotect(last_page, page_size, PROT_NONE);
#elif defined(HAVE_MREMAP) &&	\
      defined(MAP_FIXED)
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

	/*
	 *  copy of checksums and run data in a different shared
	 *  memory segment so that we can sanity check these for
	 *  any form of corruption
	 */
	len = sizeof(stress_checksum_t) * (size_t)num_procs;
	sz = (len + page_size) & ~(page_size - 1);
	g_shared->checksums = (stress_checksum_t *)mmap(NULL, sz,
		PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANON, -1, 0);
	if (g_shared->checksums == MAP_FAILED) {
		pr_err("cannot mmap checksums, errno=%d (%s)\n",
			errno, strerror(errno));
		goto err_unmap_shared;
	}
	(void)shim_memset(g_shared->checksums, 0, sz);
	g_shared->checksums_length = sz;

	/*
	 *  mmap some pages for testing invalid arguments in
	 *  various stressors, get the allocations done early
	 *  to avoid later mmap failures on stressor child
	 *  processes
	 */
	g_shared->mapped.page_none = stress_map_page(PROT_NONE, "PROT_NONE", page_size);
	if (g_shared->mapped.page_none == MAP_FAILED)
		goto err_unmap_checksums;
	g_shared->mapped.page_ro = stress_map_page(PROT_READ, "PROT_READ", page_size);
	if (g_shared->mapped.page_ro == MAP_FAILED)
		goto err_unmap_page_none;
	g_shared->mapped.page_wo = stress_map_page(PROT_READ, "PROT_WRITE", page_size);
	if (g_shared->mapped.page_wo == MAP_FAILED)
		goto err_unmap_page_ro;
	return;

err_unmap_page_ro:
	(void)munmap((void *)g_shared->mapped.page_ro, page_size);
err_unmap_page_none:
	(void)munmap((void *)g_shared->mapped.page_none, page_size);
err_unmap_checksums:
	(void)munmap((void *)g_shared->checksums, g_shared->checksums_length);
err_unmap_shared:
	(void)munmap((void *)g_shared, g_shared->length);
	stress_stressors_free();
	exit(EXIT_FAILURE);

}

/*
 *  stress_shared_unmap()
 *	unmap shared region
 */
void stress_shared_unmap(void)
{
	const size_t page_size = stress_get_page_size();

	(void)munmap((void *)g_shared->mapped.page_wo, page_size);
	(void)munmap((void *)g_shared->mapped.page_ro, page_size);
	(void)munmap((void *)g_shared->mapped.page_none, page_size);
	(void)munmap((void *)g_shared->checksums, g_shared->checksums_length);
	(void)munmap((void *)g_shared, g_shared->length);
}

/*
 *  stress_exclude_unsupported()
 *	tag stressor proc count to be excluded
 */
static inline void stress_exclude_unsupported(bool *unsupported)
{
	size_t i;

	for (i = 0; i < SIZEOF_ARRAY(stressors); i++) {
		if (stressors[i].info && stressors[i].info->supported) {
			stress_stressor_t *ss = stressors_head;
			unsigned int id = stressors[i].id;

			while (ss) {
				stress_stressor_t *next = ss->next;

				if ((ss->stressor->id == id) &&
				    ss->num_instances &&
				    (stressors[i].info->supported(stressors[i].name) < 0)) {
					stress_remove_stressor(ss);
					*unsupported = true;
				}
				ss = next;
			}
		}
	}
}

/*
 *  stress_set_proc_limits()
 *	set maximum number of processes for specific stressors
 */
static void stress_set_proc_limits(void)
{
#if defined(RLIMIT_NPROC)
	stress_stressor_t *ss;
	struct rlimit limit;

	if (getrlimit(RLIMIT_NPROC, &limit) < 0)
		return;

	for (ss = stressors_head; ss; ss = ss->next) {
		size_t i;

		for (i = 0; i < SIZEOF_ARRAY(stressors); i++) {
			if (stressors[i].info &&
			    stressors[i].info->set_limit &&
			    (stressors[i].id == ss->stressor->id) &&
			    ss->num_instances) {
				const uint64_t max = (uint64_t)limit.rlim_cur / (uint64_t)ss->num_instances;

				stressors[i].info->set_limit(max);
			}
		}
	}
#endif
}

/*
 *  stress_find_proc_info()
 *	find proc info that is associated with a specific
 *	stressor.  If it does not exist, create a new one
 *	and return that. Terminate if out of memory.
 */
static stress_stressor_t *stress_find_proc_info(const stress_t *stressor)
{
	stress_stressor_t *ss;

#if 0
	/* Scan backwards in time to find last matching stressor */
	for (ss = stressors_tail; ss; ss = ss->prev) {
		if (ss->stressor == stressor)
			return ss;
	}
#endif

	ss = calloc(1, sizeof(*ss));
	if (!ss) {
		(void)fprintf(stderr, "Cannot allocate stressor state info\n");
		exit(EXIT_FAILURE);
	}

	ss->stressor = stressor;

	/* Add to end of procs list */
	if (stressors_tail)
		stressors_tail->next = ss;
	else
		stressors_head = ss;
	ss->prev = stressors_tail;
	stressors_tail = ss;

	return ss;
}

/*
 *  stress_stressors_init()
 *	initialize any stressors that will be used
 */
static void stress_stressors_init(void)
{
	stress_stressor_t *ss;

	for (ss = stressors_head; ss; ss = ss->next) {
		size_t i;

		for (i = 0; i < SIZEOF_ARRAY(stressors); i++) {
			if (stressors[i].info &&
			    stressors[i].info->init &&
			    stressors[i].id == ss->stressor->id)
				stressors[i].info->init();
		}
	}
}

/*
 *  stress_stressors_deinit()
 *	de-initialize any stressors that will be used
 */
static void stress_stressors_deinit(void)
{
	stress_stressor_t *ss;

	for (ss = stressors_head; ss; ss = ss->next) {
		size_t i;

		for (i = 0; i < SIZEOF_ARRAY(stressors); i++) {
			if (stressors[i].info &&
			    stressors[i].info->deinit &&
			    stressors[i].id == ss->stressor->id)
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
 *  stress_exclude_pathological()
 *	Disable pathological stressors if user has not explicitly
 *	request them to be used. Let's play safe.
 */
static inline void stress_exclude_pathological(void)
{
	if (!(g_opt_flags & OPT_FLAGS_PATHOLOGICAL)) {
		stress_stressor_t *ss = stressors_head;

		while (ss) {
			stress_stressor_t *next = ss->next;

			if (ss->stressor->info->class & CLASS_PATHOLOGICAL) {
				if (ss->num_instances > 0) {
					char munged[64];

					(void)stress_munge_underscore(munged, ss->stressor->name, sizeof(munged));
					pr_inf("disabled '%s' as it "
						"may hang or reboot the machine "
						"(enable it with the "
						"--pathological option)\n", munged);
				}
				stress_remove_stressor(ss);
			}
			ss = next;
		}
	}
}

/*
 *  stress_setup_stats_buffers()
 *	setup the stats data from the shared memory
 */
static inline void stress_setup_stats_buffers(void)
{
	stress_stressor_t *ss;
	stress_stats_t *stats = g_shared->stats;

	for (ss = stressors_head; ss; ss = ss->next) {
		int32_t j;

		for (j = 0; j < ss->num_instances; j++, stats++)
			ss->stats[j] = stats;
	}
}

/*
 *  stress_set_random_stressors()
 *	select stressors at random
 */
static inline void stress_set_random_stressors(void)
{
	int32_t opt_random = 0;

	(void)stress_get_setting("random", &opt_random);

	if (g_opt_flags & OPT_FLAGS_RANDOM) {
		int32_t n = opt_random;
		const uint32_t n_procs = stress_get_num_stressors();

		if (g_opt_flags & OPT_FLAGS_SET) {
			(void)fprintf(stderr, "Cannot specify random "
				"option with other stress processes "
				"selected\n");
			exit(EXIT_FAILURE);
		}

		if (!n_procs) {
			(void)fprintf(stderr,
				"No stressors are available, unable to continue\n");
			exit(EXIT_FAILURE);
		}

		/* create n randomly chosen stressors */
		while (n > 0) {
			const uint32_t i = stress_mwc32modn(n_procs);
			stress_stressor_t *ss = stress_get_nth_stressor(i);

			if (!ss)
				continue;

			ss->num_instances++;
			n--;
		}
	}
}

/*
 *  stress_enable_all_stressors()
 *	enable all the stressors
 */
static void stress_enable_all_stressors(const int32_t instances)
{
	size_t i;

	/* Don't enable all if some stressors are set */
	if (g_opt_flags & OPT_FLAGS_SET)
		return;

	for (i = 0; i < STRESS_MAX; i++) {
		stress_stressor_t *ss = stress_find_proc_info(&stressors[i]);

		if (!ss) {
			(void)fprintf(stderr, "Cannot allocate stressor state info\n");
			exit(EXIT_FAILURE);
		}
		ss->num_instances = instances;
	}
}

/*
 *  stress_enable_classes()
 *	enable stressors based on class
 */
static void stress_enable_classes(const uint32_t class)
{
	size_t i;

	if (!class)
		return;

	/* This indicates some stressors are set */
	g_opt_flags |= OPT_FLAGS_SET;

	for (i = 0; stressors[i].id != STRESS_MAX; i++) {
		if (stressors[i].info->class & class) {
			stress_stressor_t *ss = stress_find_proc_info(&stressors[i]);

			if (g_opt_flags & OPT_FLAGS_SEQUENTIAL)
				ss->num_instances = g_opt_sequential;
			if (g_opt_flags & OPT_FLAGS_ALL)
				ss->num_instances = g_opt_parallel;
		}
	}
}


/*
 *  stress_parse_opts
 *	parse argv[] and set stress-ng options accordingly
 */
int stress_parse_opts(int argc, char **argv, const bool jobmode)
{
	optind = 0;

	for (;;) {
		int64_t i64;
		int32_t i32;
		uint32_t u32;
		uint64_t u64, max_fds;
		int16_t i16;
		int c, option_index, ret;
		size_t i;

		opterr = (!jobmode) ? opterr : 0;
next_opt:
		if ((c = getopt_long(argc, argv, "?khMVvqnt:b:c:i:j:m:d:f:s:l:p:P:C:S:a:y:F:D:T:u:o:r:B:R:Y:x:",
			long_options, &option_index)) == -1) {
			break;
		}

		for (i = 0; stressors[i].id != STRESS_MAX; i++) {
			if (stressors[i].short_getopt == c) {
				const char *name = stress_opt_name(c);
				stress_stressor_t *ss = stress_find_proc_info(&stressors[i]);
				g_stressor_current = ss;

				g_opt_flags |= OPT_FLAGS_SET;
				ss->num_instances = stress_get_int32(optarg);
				stress_get_processors(&ss->num_instances);
				stress_check_max_stressors(name, ss->num_instances);

				goto next_opt;
			}
			if (stressors[i].op == (stress_op_t)c) {
				uint64_t bogo_ops;

				bogo_ops = stress_get_uint64(optarg);
				stress_check_range(stress_opt_name(c), bogo_ops,
					MIN_OPS, MAX_OPS);
				/* We don't need to set this, but it may be useful */
				stress_set_setting(stress_opt_name(c), TYPE_ID_UINT64, &bogo_ops);
				if (g_stressor_current)
					g_stressor_current->bogo_ops = bogo_ops;
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
			g_opt_parallel = stress_get_int32(optarg);
			stress_get_processors(&g_opt_parallel);
			stress_check_max_stressors("all", g_opt_parallel);
			break;
		case OPT_backoff:
			i64 = (int64_t)stress_get_uint64(optarg);
			stress_set_setting_global("backoff", TYPE_ID_INT64, &i64);
			break;
		case OPT_cache_level:
			/*
			 * Note: Overly high values will be caught in the
			 * caching code.
			 */
			ret = atoi(optarg);
			if ((ret <= 0) || (ret > 3))
				ret = DEFAULT_CACHE_LEVEL;
			i16 = (int16_t)ret;
			stress_set_setting("cache-level", TYPE_ID_INT16, &i16);
			break;
		case OPT_cache_ways:
			u32 = stress_get_uint32(optarg);
			stress_set_setting("cache-ways", TYPE_ID_UINT32, &u32);
			break;
		case OPT_class:
			ret = stress_get_class(optarg, &u32);
			if (ret < 0)
				return EXIT_FAILURE;
			else if (ret > 0)
				exit(EXIT_SUCCESS);
			else {
				stress_set_setting("class", TYPE_ID_UINT32, &u32);
				stress_enable_classes(u32);
			}
			break;
		case OPT_exclude:
			stress_set_setting_global("exclude", TYPE_ID_STR, (void *)optarg);
			break;
		case OPT_help:
			stress_usage();
			break;
		case OPT_ionice_class:
			i32 = stress_get_opt_ionice_class(optarg);
			stress_set_setting("ionice-class", TYPE_ID_INT32, &i32);
			break;
		case OPT_ionice_level:
			i32 = stress_get_int32(optarg);
			stress_set_setting("ionice-level", TYPE_ID_INT32, &i32);
			break;
		case OPT_job:
			stress_set_setting_global("job", TYPE_ID_STR, (void *)optarg);
			break;
		case OPT_log_file:
			stress_set_setting_global("log-file", TYPE_ID_STR, (void *)optarg);
			break;
		case OPT_max_fd:
			max_fds = (uint64_t)stress_get_file_limit();
			u64 = stress_get_uint64_percent(optarg, 1, max_fds,
				"Cannot determine maximum file descriptor limit");
			stress_check_range(optarg, u64, 8, max_fds);
			stress_set_setting_global("max-fd", TYPE_ID_UINT64, &u64);
			break;
		case OPT_mbind:
			if (stress_set_mbind(optarg) < 0)
				exit(EXIT_FAILURE);
			break;
		case OPT_no_madvise:
			g_opt_flags &= ~OPT_FLAGS_MMAP_MADVISE;
			break;
		case OPT_oom_avoid_bytes:
			{
				size_t shmall, freemem, totalmem, freeswap, totalswap, bytes;

				bytes = (size_t)stress_get_uint64_byte_memory(optarg, 1);
				stress_get_memlimits(&shmall, &freemem, &totalmem, &freeswap, &totalswap);
				if ((freemem > 0) && (bytes > freemem / 2)) {
					char buf[32];

					bytes = freemem / 2;
					pr_inf("option --oom-avoid-bytes too large, limiting to "
						"50%% (%s) of free memory\n",
						stress_uint64_to_str(buf, sizeof(buf), (uint64_t)bytes));
				}
				stress_set_setting("oom-avoid-bytes", TYPE_ID_SIZE_T, &bytes);
				g_opt_flags |= OPT_FLAGS_OOM_AVOID;
			}
			break;
		case OPT_query:
			if (!jobmode) {
				(void)printf("Try '%s --help' for more information.\n", g_app_name);
			}
			return EXIT_FAILURE;
		case OPT_quiet:
			g_opt_flags &= ~(PR_ALL);
			break;
		case OPT_random:
			g_opt_flags |= OPT_FLAGS_RANDOM;
			i32 = stress_get_int32(optarg);
			stress_get_processors(&i32);
			stress_check_max_stressors("random", i32);
			stress_set_setting("random", TYPE_ID_INT32, &i32);
			break;
		case OPT_sched:
			i32 = stress_get_opt_sched(optarg);
			stress_set_setting_global("sched", TYPE_ID_INT32, &i32);
			break;
		case OPT_sched_prio:
			i32 = stress_get_int32(optarg);
			stress_set_setting_global("sched-prio", TYPE_ID_INT32, &i32);
			break;
		case OPT_sched_period:
			u64 = stress_get_uint64(optarg);
			stress_set_setting_global("sched-period", TYPE_ID_UINT64, &u64);
			break;
		case OPT_sched_runtime:
			u64 = stress_get_uint64(optarg);
			stress_set_setting_global("sched-runtime", TYPE_ID_UINT64, &u64);
			break;
		case OPT_sched_deadline:
			u64 = stress_get_uint64(optarg);
			stress_set_setting_global("sched-deadline", TYPE_ID_UINT64, &u64);
			break;
		case OPT_sched_reclaim:
			g_opt_flags |= OPT_FLAGS_DEADLINE_GRUB;
			break;
		case OPT_seed:
			u64 = stress_get_uint64(optarg);
			g_opt_flags |= OPT_FLAGS_SEED;
			stress_set_setting_global("seed", TYPE_ID_UINT64, &u64);
			break;
		case OPT_sequential:
			g_opt_flags |= OPT_FLAGS_SEQUENTIAL;
			g_opt_sequential = stress_get_int32(optarg);
			stress_get_processors(&g_opt_sequential);
			stress_check_range("sequential", (uint64_t)g_opt_sequential,
				MIN_SEQUENTIAL, MAX_SEQUENTIAL);
			break;
		case OPT_stressors:
			stress_show_stressor_names();
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
			g_opt_timeout = stress_get_uint64_time(optarg);
			break;
		case OPT_timer_slack:
			(void)stress_set_timer_slack_ns(optarg);
			break;
		case OPT_version:
			stress_version();
			exit(EXIT_SUCCESS);
		case OPT_verifiable:
			stress_verifiable();
			exit(EXIT_SUCCESS);
		case OPT_vmstat:
			if (stress_set_vmstat(optarg) < 0)
				exit(EXIT_FAILURE);
			break;
		case OPT_thermalstat:
			if (stress_set_thermalstat(optarg) < 0)
				exit(EXIT_FAILURE);
			break;
		case OPT_iostat:
			if (stress_set_iostat(optarg) < 0)
				exit(EXIT_FAILURE);
			break;
		case OPT_yaml:
			stress_set_setting_global("yaml", TYPE_ID_STR, (void *)optarg);
			break;
		default:
			if (!jobmode)
				(void)printf("Unknown option (%d)\n",c);
			return EXIT_FAILURE;
		}
	}

	if (optind < argc) {
		bool unicode = false;

		(void)printf("Error: unrecognised option:");
		while (optind < argc) {
			(void)printf(" %s", argv[optind]);
			if (((argv[optind][0] & 0xff) == 0xe2) &&
			    ((argv[optind][1] & 0xff) == 0x88)) {
				unicode = true;
			}
			optind++;
		}
		(void)printf("\n");
		if (unicode)
			(void)printf("note: a Unicode minus sign was used instead of an ASCII '-' for an option\n");
		return EXIT_FAILURE;
	}
	return EXIT_SUCCESS;
}

/*
 *  stress_alloc_proc_resources()
 *	allocate array of pids based on n pids required
 */
static void stress_alloc_proc_resources(
	stress_stats_t ***stats,
	const int32_t n)
{
	*stats = calloc((size_t)n, sizeof(stress_stats_t *));
	if (!*stats) {
		pr_err("cannot allocate stats list\n");
		stress_stressors_free();
		exit(EXIT_FAILURE);
	}
}

/*
 *  stress_set_default_timeout()
 *	set timeout to a default value if not already set
 */
static void stress_set_default_timeout(const uint64_t timeout)
{
	char *action;

	if (g_opt_timeout == TIMEOUT_NOT_SET) {
		g_opt_timeout = timeout;
		action = "defaulting";
	} else {
		action = "setting";
	}

	pr_inf("%s to a %" PRIu64 " second%s run per stressor\n",
		action, g_opt_timeout,
		stress_duration_to_str((double)g_opt_timeout));
}

/*
 *  stress_setup_sequential()
 *	setup for sequential --seq mode stressors
 */
static void stress_setup_sequential(const uint32_t class)
{
	stress_stressor_t *ss;

	stress_set_default_timeout(60);

	for (ss = stressors_head; ss; ss = ss->next) {
		if (ss->stressor->info->class & class)
			ss->num_instances = g_opt_sequential;
		stress_alloc_proc_resources(&ss->stats, ss->num_instances);
	}
}

/*
 *  stress_setup_parallel()
 *	setup for parallel mode stressors
 */
static void stress_setup_parallel(const uint32_t class)
{
	stress_stressor_t *ss;

	stress_set_default_timeout(DEFAULT_TIMEOUT);

	for (ss = stressors_head; ss; ss = ss->next) {
		if (ss->stressor->info->class & class)
			ss->num_instances = g_opt_parallel;
		/*
		 * Share bogo ops between processes equally, rounding up
		 * if nonzero bogo_ops
		 */
		ss->bogo_ops = ss->num_instances ?
			(ss->bogo_ops + (ss->num_instances - 1)) / ss->num_instances : 0;
		if (ss->num_instances)
			stress_alloc_proc_resources(&ss->stats, ss->num_instances);
	}
}

/*
 *  stress_run_sequential()
 *	run stressors sequentially
 */
static inline void stress_run_sequential(
	double *duration,
	bool *success,
	bool *resource_success,
	bool *metrics_success)
{
	stress_stressor_t *ss;
	stress_checksum_t *checksum = g_shared->checksums;

	/*
	 *  Step through each stressor one by one
	 */
	for (ss = stressors_head; ss && keep_stressing_flag(); ss = ss->next) {
		stress_stressor_t *next = ss->next;

		ss->next = NULL;
		stress_run(ss, duration, success, resource_success,
			metrics_success, &checksum);
		ss->next = next;
	}
}

/*
 *  stress_run_parallel()
 *	run stressors in parallel
 */
static inline void stress_run_parallel(
	double *duration,
	bool *success,
	bool *resource_success,
	bool *metrics_success)
{
	stress_checksum_t *checksum = g_shared->checksums;

	/*
	 *  Run all stressors in parallel
	 */
	stress_run(stressors_head, duration, success, resource_success,
			metrics_success, &checksum);
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

	stress_mlock_region(&__start_mlocked_text, &__stop_mlocked_text);
#endif
}

/*
 *  stress_yaml_open()
 *	open YAML results file
 */
static FILE *stress_yaml_open(const char *yaml_filename)
{
	FILE *yaml = NULL;

	if (yaml_filename) {
		yaml = fopen(yaml_filename, "w");
		if (!yaml)
			pr_err("Cannot output YAML data to %s\n", yaml_filename);

		pr_yaml(yaml, "---\n");
		pr_yaml_runinfo(yaml);
	}
	return yaml;
}

/*
 *  stress_yaml_open()
 *	close YAML results file
 */
static void stress_yaml_close(FILE *yaml)
{
	if (yaml) {
		pr_yaml(yaml, "...\n");
		(void)fclose(yaml);
	}
}

int main(int argc, char **argv, char **envp)
{
	double duration = 0.0;			/* stressor run time in secs */
	bool success = true;
	bool resource_success = true;
	bool metrics_success = true;
	FILE *yaml;				/* YAML output file */
	char *yaml_filename = NULL;		/* YAML file name */
	char *log_filename;			/* log filename */
	char *job_filename = NULL;		/* job filename */
	int32_t ticks_per_sec;			/* clock ticks per second (jiffies) */
	int32_t ionice_class = UNDEFINED;	/* ionice class */
	int32_t ionice_level = UNDEFINED;	/* ionice level */
	size_t i;
	uint32_t class = 0;
	const uint32_t cpus_online = (uint32_t)stress_get_processors_online();
	const uint32_t cpus_configured = (uint32_t)stress_get_processors_configured();
	int ret;
	bool unsupported = false;		/* true if stressors are unsupported */

	main_pid = getpid();

	/* Enable stress-ng stack smashing message */
	stress_set_stack_smash_check_flag(true);

	if (stress_set_temp_path(".") < 0)
		exit(EXIT_FAILURE);
	stress_set_proc_name_init(argc, argv, envp);

	if (setjmp(g_error_env) == 1) {
		ret = EXIT_FAILURE;
		goto exit_temp_path_free;
	}

	yaml = NULL;

	/* --exec stressor uses this to exec itself and then exit early */
	if ((argc == 2) && !strcmp(argv[1], "--exec-exit")) {
		ret = EXIT_SUCCESS;
		goto exit_temp_path_free;
	}

	stressors_head = NULL;
	stressors_tail = NULL;
	stress_mwc_reseed();

	(void)stress_get_page_size();
	stressor_set_defaults();

	if (stress_get_processors_configured() < 0) {
		pr_err("sysconf failed, number of cpus configured "
			"unknown: errno=%d: (%s)\n",
			errno, strerror(errno));
		ret = EXIT_FAILURE;
		goto exit_settings_free;
	}
	ticks_per_sec = stress_get_ticks_per_second();
	if (ticks_per_sec < 0) {
		pr_err("sysconf failed, clock ticks per second "
			"unknown: errno=%d (%s)\n",
			errno, strerror(errno));
		ret = EXIT_FAILURE;
		goto exit_settings_free;
	}

	ret = stress_parse_opts(argc, argv, false);
	if (ret != EXIT_SUCCESS)
		goto exit_settings_free;

	if ((g_opt_flags & (OPT_FLAGS_STDERR | OPT_FLAGS_STDOUT)) ==
	    (OPT_FLAGS_STDERR | OPT_FLAGS_STDOUT)) {
		(void)fprintf(stderr, "stderr and stdout cannot "
			"be used together\n");
		ret = EXIT_FAILURE;
		goto exit_stressors_free;
	}

	if (stress_check_temp_path() < 0) {
		ret = EXIT_FAILURE;
		goto exit_stressors_free;
	}

	/*
	 *  Load in job file options
	 */
	(void)stress_get_setting("job", &job_filename);
	if (stress_parse_jobfile(argc, argv, job_filename) < 0) {
		ret = EXIT_FAILURE;
		goto exit_stressors_free;
	}

	/*
	 *  Sanity check minimize/maximize options
	 */
	if ((g_opt_flags & OPT_FLAGS_MINMAX_MASK) == OPT_FLAGS_MINMAX_MASK) {
		(void)fprintf(stderr, "maximize and minimize cannot "
			"be used together\n");
		ret = EXIT_FAILURE;
		goto exit_stressors_free;
	}

	/*
	 *  Sanity check seq/all settings
	 */
	if ((g_opt_flags & (OPT_FLAGS_SEQUENTIAL | OPT_FLAGS_ALL)) ==
	    (OPT_FLAGS_SEQUENTIAL | OPT_FLAGS_ALL)) {
		(void)fprintf(stderr, "cannot invoke --sequential and --all "
			"options together\n");
		ret = EXIT_FAILURE;
		goto exit_stressors_free;
	}
	(void)stress_get_setting("class", &class);

	if (class &&
	    !(g_opt_flags & (OPT_FLAGS_SEQUENTIAL | OPT_FLAGS_ALL))) {
		(void)fprintf(stderr, "class option is only used with "
			"--sequential or --all options\n");
		ret = EXIT_FAILURE;
		goto exit_stressors_free;
	}

	/*
	 *  Sanity check mutually exclusive random seed flags
	 */
	if ((g_opt_flags & (OPT_FLAGS_NO_RAND_SEED | OPT_FLAGS_SEED)) ==
	    (OPT_FLAGS_NO_RAND_SEED | OPT_FLAGS_SEED)) {
		(void)fprintf(stderr, "cannot invoke mutually exclusive "
			"--seed and --no-rand-seed options together\n");
		ret = EXIT_FAILURE;
		goto exit_stressors_free;
	}

	/*
	 *  Setup logging
	 */
	if (stress_get_setting("log-file", &log_filename))
		pr_openlog(log_filename);
	shim_openlog("stress-ng", 0, LOG_USER);
	stress_log_args(argc, argv);
	stress_log_system_info();
	stress_log_system_mem_info();

	pr_runinfo();
	pr_dbg("%" PRId32 " processor%s online, %" PRId32
		" processor%s configured\n",
		cpus_online, cpus_online == 1 ? "" : "s",
		cpus_configured, cpus_configured == 1 ? "" : "s");

	/*
	 *  For random mode the stressors must be available
	 */
	if (g_opt_flags & OPT_FLAGS_RANDOM)
		stress_enable_all_stressors(0);
	/*
	 *  These two options enable all the stressors
	 */
	if (g_opt_flags & OPT_FLAGS_SEQUENTIAL)
		stress_enable_all_stressors(g_opt_sequential);
	if (g_opt_flags & OPT_FLAGS_ALL)
		stress_enable_all_stressors(g_opt_parallel);

	/*
	 *  Discard stressors that we can't run
	 */
	stress_exclude_unsupported(&unsupported);
	stress_exclude_pathological();
	/*
	 *  Throw away excluded stressors
	 */
	if (stress_exclude() < 0) {
		ret = EXIT_FAILURE;
		goto exit_logging_close;
	}

	/*
	 *  Setup random stressors if requested
	 */
	stress_set_random_stressors();

	(void)stress_ftrace_start();
#if defined(STRESS_PERF_STATS) &&	\
    defined(HAVE_LINUX_PERF_EVENT_H)
	if (g_opt_flags & OPT_FLAGS_PERF_STATS)
		stress_perf_init();
#endif

	/*
	 *  Setup running environment
	 */
	stress_process_dumpable(false);
	stress_cwd_readwriteable();
	stress_set_oom_adjustment("main", false);

	/*
	 *  Get various user defined settings
	 */
	if (sched_settings_apply(false) < 0) {
		ret = EXIT_FAILURE;
		goto exit_logging_close;
	}
	(void)stress_get_setting("ionice-class", &ionice_class);
	(void)stress_get_setting("ionice-level", &ionice_level);
	stress_set_iopriority(ionice_class, ionice_level);
	(void)stress_get_setting("yaml", &yaml_filename);

	stress_mlock_executable();

	/*
	 *  Enable signal handers
	 */
	for (i = 0; i < SIZEOF_ARRAY(terminate_signals); i++) {
		if (stress_sighandler("stress-ng", terminate_signals[i], stress_handle_terminate, NULL) < 0) {
			ret = EXIT_FAILURE;
			goto exit_logging_close;
		}
	}
	/*
	 *  Ignore other signals
	 */
	for (i = 0; i < SIZEOF_ARRAY(ignore_signals); i++) {
		VOID_RET(int, stress_sighandler("stress-ng", ignore_signals[i], SIG_IGN, NULL));
	}

	/*
	 *  Setup stressor proc info
	 */
	if (g_opt_flags & OPT_FLAGS_SEQUENTIAL) {
		stress_setup_sequential(class);
	} else {
		stress_setup_parallel(class);
	}
	/*
	 *  Seq/parallel modes may have added in
	 *  excluded stressors, so exclude check again
	 */
	stress_exclude_unsupported(&unsupported);
	stress_exclude_pathological();

	stress_set_proc_limits();

	if (!stressors_head) {
		pr_err("No stress workers invoked%s\n",
			unsupported ? " (one or more were unsupported)" : "");
		/*
		 *  If some stressors were given but marked as
		 *  unsupported then this is not an error.
		 */
		ret = unsupported ? EXIT_SUCCESS : EXIT_FAILURE;
		goto exit_logging_close;
	}

	/*
	 *  Show the stressors we're going to run
	 */
	if (stress_show_stressors() < 0) {
		ret = EXIT_FAILURE;
		goto exit_logging_close;
	}

	/*
	 *  Allocate shared memory segment for shared data
	 *  across all the child stressors
	 */
	stress_shared_map(stress_get_total_num_instances(stressors_head));

	/*
	 *  And now shared memory is created, initialize pr_* lock mechanism
	 */
	pr_lock_init();
	if (!stress_shared_heap_init()) {
		pr_err("failed to create shared heap \n");
		goto exit_shared_unmap;
	}

	/*
	 *  Initialize global locks
	 */
#if defined(STRESS_PERF_STATS) &&	\
    defined(HAVE_LINUX_PERF_EVENT_H)
	g_shared->perf.lock = stress_lock_create();
	if (!g_shared->perf.lock) {
		pr_err("failed to create perf lock\n");
		goto exit_shared_unmap;
	}
#endif
	g_shared->warn_once.lock = stress_lock_create();
	if (!g_shared->warn_once.lock) {
		pr_err("failed to create warn_once lock\n");
		goto exit_destroy_perf_lock;
	}
	g_shared->net_port_map.lock = stress_lock_create();
	if (!g_shared->warn_once.lock) {
		pr_err("failed to create net_port_map lock\n");
		goto exit_destroy_warn_once_lock;
	}

	/*
	 *  Assign procs with shared stats memory
	 */
	stress_setup_stats_buffers();

	/*
	 *  Allocate shared cache memory
	 */
	g_shared->mem_cache_level = DEFAULT_CACHE_LEVEL;
	(void)stress_get_setting("cache-level", &g_shared->mem_cache_level);
	g_shared->mem_cache_ways = 0;
	(void)stress_get_setting("cache-ways", &g_shared->mem_cache_ways);
	if (stress_cache_alloc("cache allocate") < 0) {
		ret = EXIT_FAILURE;
		goto exit_shared_unmap;
	}

#if defined(STRESS_THERMAL_ZONES)
	/*
	 *  Setup thermal zone data
	 */
	if (g_opt_flags & OPT_FLAGS_TZ_INFO)
		stress_tz_init(&g_shared->tz_info);
#endif

	stress_clear_warn_once();
	stress_stressors_init();

	/* Start thrasher process if required */
	if (g_opt_flags & OPT_FLAGS_THRASH)
		stress_thrash_start();

	stress_vmstat_start();
	stress_smart_start();
	stress_klog_start();

	if (g_opt_flags & OPT_FLAGS_SEQUENTIAL) {
		stress_run_sequential(&duration,
			&success, &resource_success, &metrics_success);
	} else {
		stress_run_parallel(&duration,
			&success, &resource_success, &metrics_success);
	}

	/* Stop thasher process */
	if (g_opt_flags & OPT_FLAGS_THRASH)
		stress_thrash_stop();

	yaml = stress_yaml_open(yaml_filename);

	/*
	 *  Dump metrics
	 */
	if (g_opt_flags & OPT_FLAGS_METRICS)
		stress_metrics_dump(yaml, ticks_per_sec);


	stress_metrics_check(&success);
	stress_interrupts_dump(yaml, stressors_head);

#if defined(STRESS_PERF_STATS) &&	\
    defined(HAVE_LINUX_PERF_EVENT_H)
	/*
	 *  Dump perf statistics
	 */
	if (g_opt_flags & OPT_FLAGS_PERF_STATS)
		stress_perf_stat_dump(yaml, stressors_head, duration);
#endif

#if defined(STRESS_THERMAL_ZONES)
	/*
	 *  Dump thermal zone measurements
	 */
	if (g_opt_flags & OPT_FLAGS_THERMAL_ZONES)
		stress_tz_dump(yaml, stressors_head);
	if (g_opt_flags & OPT_FLAGS_TZ_INFO)
		stress_tz_free(&g_shared->tz_info);
#endif
	/*
	 *  Dump run times
	 */
	stress_times_dump(yaml, ticks_per_sec, duration);

	stress_exit_status_summary();

	stress_klog_stop(&success);
	stress_smart_stop();
	stress_vmstat_stop();
	stress_ftrace_stop();
	stress_ftrace_free();

	pr_inf("%s run completed in %.2fs%s\n",
		success ? "successful" : "unsuccessful",
		duration, stress_duration_to_str(duration));

	/*
	 *  Tidy up
	 */
	(void)stress_lock_destroy(g_shared->net_port_map.lock);
	(void)stress_lock_destroy(g_shared->warn_once.lock);
#if defined(STRESS_PERF_STATS) && 	\
    defined(HAVE_LINUX_PERF_EVENT_H)
	(void)stress_lock_destroy(g_shared->perf.lock);
#endif

	stress_shared_heap_deinit();
	stress_stressors_deinit();
	stress_stressors_free();
	stress_cache_free();
	stress_shared_unmap();
	stress_settings_free();
	stress_temp_path_free();

	/*
	 *  Close logs
	 */
	shim_closelog();
	pr_closelog();
	stress_yaml_close(yaml);

	/*
	 *  Done!
	 */
	if (!success)
		exit(EXIT_NOT_SUCCESS);
	if (!resource_success)
		exit(EXIT_NO_RESOURCE);
	if (!metrics_success)
		exit(EXIT_METRICS_UNTRUSTWORTHY);
	exit(EXIT_SUCCESS);

exit_destroy_warn_once_lock:
	(void)stress_lock_destroy(g_shared->warn_once.lock);

exit_destroy_perf_lock:
#if defined(STRESS_PERF_STATS) && 	\
    defined(HAVE_LINUX_PERF_EVENT_H)
	(void)stress_lock_destroy(g_shared->perf.lock);
#endif

exit_shared_unmap:
	stress_shared_unmap();

exit_logging_close:
	shim_closelog();
	pr_closelog();

exit_stressors_free:
	stress_stressors_free();

exit_settings_free:
	stress_settings_free();

exit_temp_path_free:
	stress_temp_path_free();
	exit(ret);
}
