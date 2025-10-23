/*
 * Copyright (C) 2024-2025 Colin Ian King
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
#ifndef CORE_OPTS_H
#define CORE_OPTS_H

#include <unistd.h>
#include <getopt.h>

/* pr_* bit masks, first bits of global option flags */
#define OPT_FLAGS_PR_ERROR	 STRESS_BIT_ULL(0)	/* Print errors */
#define OPT_FLAGS_PR_INFO	 STRESS_BIT_ULL(1)	/* Print info */
#define OPT_FLAGS_PR_DEBUG	 STRESS_BIT_ULL(2) 	/* Print debug */
#define OPT_FLAGS_PR_FAIL	 STRESS_BIT_ULL(3) 	/* Print test failure message */
#define OPT_FLAGS_PR_WARN	 STRESS_BIT_ULL(4)	/* Print warning */
#define OPT_FLAGS_PR_METRICS	 STRESS_BIT_ULL(5)	/* Print metrics */
#define OPT_FLAGS_PR_ALL	 (OPT_FLAGS_PR_ERROR | OPT_FLAGS_PR_INFO | \
				  OPT_FLAGS_PR_DEBUG | OPT_FLAGS_PR_FAIL | \
				  OPT_FLAGS_PR_WARN  | OPT_FLAGS_PR_METRICS)

/* Option bit masks, stats from the next PR_ option onwards */
#define OPT_FLAGS_METRICS	 STRESS_BIT_ULL(6)	/* --metrics, Dump metrics at end */
#define OPT_FLAGS_RANDOM	 STRESS_BIT_ULL(7)	/* --random, Randomize */
#define OPT_FLAGS_SET		 STRESS_BIT_ULL(8)	/* Set if user specifies stress procs */
#define OPT_FLAGS_KEEP_NAME	 STRESS_BIT_ULL(9)	/* --keep-name, Keep stress names to stress-ng */
#define OPT_FLAGS_METRICS_BRIEF	 STRESS_BIT_ULL(10)	/* --metrics-brief, dump brief metrics */
#define OPT_FLAGS_VERIFY	 STRESS_BIT_ULL(11)	/* --verify, verify mode */
#define OPT_FLAGS_MMAP_MADVISE	 STRESS_BIT_ULL(12)	/* --no-madvise, disable random madvise settings */
#define OPT_FLAGS_MMAP_MINCORE	 STRESS_BIT_ULL(13)	/* --page-in, mincore force pages into mem */
#define OPT_FLAGS_TIMES		 STRESS_BIT_ULL(14)	/* --times, user/system time summary */
#define OPT_FLAGS_MINIMIZE	 STRESS_BIT_ULL(15)	/* --minimize, Minimize */
#define OPT_FLAGS_MAXIMIZE	 STRESS_BIT_ULL(16)	/* --maximize Maximize */
#define OPT_FLAGS_SYSLOG	 STRESS_BIT_ULL(17)	/* --syslog, log test progress to syslog */
#define OPT_FLAGS_AGGRESSIVE	 STRESS_BIT_ULL(18)	/* --aggressive, aggressive mode enabled */
#define OPT_FLAGS_ALL		 STRESS_BIT_ULL(19)	/* --all mode */
#define OPT_FLAGS_SEQUENTIAL	 STRESS_BIT_ULL(20)	/* --sequential mode */
#define OPT_FLAGS_PERF_STATS	 STRESS_BIT_ULL(21)	/* --perf stats mode */
#define OPT_FLAGS_LOG_BRIEF	 STRESS_BIT_ULL(22)	/* --log-brief */
#define OPT_FLAGS_THERMAL_ZONES  STRESS_BIT_ULL(23)	/* --tz thermal zones */
#define OPT_FLAGS_SOCKET_NODELAY STRESS_BIT_ULL(24)	/* --sock-nodelay */
#define OPT_FLAGS_IGNITE_CPU	 STRESS_BIT_ULL(25)	/* --cpu-ignite */
#define OPT_FLAGS_PATHOLOGICAL	 STRESS_BIT_ULL(26)	/* --pathological */
#define OPT_FLAGS_NO_RAND_SEED	 STRESS_BIT_ULL(27)	/* --no-rand-seed */
#define OPT_FLAGS_THRASH	 STRESS_BIT_ULL(28)	/* --thrash */
#define OPT_FLAGS_OOMABLE	 STRESS_BIT_ULL(29)	/* --oomable */
#define OPT_FLAGS_ABORT		 STRESS_BIT_ULL(30)	/* --abort */
#define OPT_FLAGS_TIMESTAMP	 STRESS_BIT_ULL(31)	/* --timestamp */
#define OPT_FLAGS_DEADLINE_GRUB  STRESS_BIT_ULL(32)	/* --sched-reclaim */
#define OPT_FLAGS_FTRACE	 STRESS_BIT_ULL(33)	/* --ftrace */
#define OPT_FLAGS_SEED		 STRESS_BIT_ULL(34)	/* --seed */
#define OPT_FLAGS_SKIP_SILENT	 STRESS_BIT_ULL(35)	/* --skip-silent */
#define OPT_FLAGS_SMART		 STRESS_BIT_ULL(36)	/* --smart */
#define OPT_FLAGS_NO_OOM_ADJUST	 STRESS_BIT_ULL(37)	/* --no-oom-adjust */
#define OPT_FLAGS_KEEP_FILES	 STRESS_BIT_ULL(38)	/* --keep-files */
#define OPT_FLAGS_STDERR	 STRESS_BIT_ULL(39)	/* --stderr */
#define OPT_FLAGS_STDOUT	 STRESS_BIT_ULL(40)	/* --stdout */
#define OPT_FLAGS_KLOG_CHECK	 STRESS_BIT_ULL(41)	/* --klog-check */
#define OPT_FLAGS_DRY_RUN	 STRESS_BIT_ULL(42)	/* --dry-run, don't actually run */
#define OPT_FLAGS_OOM_AVOID	 STRESS_BIT_ULL(43)	/* --oom-avoid */
#define OPT_FLAGS_TZ_INFO	 STRESS_BIT_ULL(44)	/* --tz, enable thermal zone info */
#define OPT_FLAGS_LOG_LOCKLESS	 STRESS_BIT_ULL(45)	/* --log-lockless */
#define OPT_FLAGS_SN		 STRESS_BIT_ULL(46)	/* --sn scientific notation */
#define OPT_FLAGS_CHANGE_CPU	 STRESS_BIT_ULL(47)	/* --change-cpu */
#define OPT_FLAGS_KSM		 STRESS_BIT_ULL(48)	/* --ksm */
#define OPT_FLAGS_SETTINGS	 STRESS_BIT_ULL(49)	/* --settings */
#define OPT_FLAGS_WITH		 STRESS_BIT_ULL(50)	/* --with list */
#define OPT_FLAGS_PERMUTE	 STRESS_BIT_ULL(51)	/* --permute N */
#define OPT_FLAGS_INTERRUPTS	 STRESS_BIT_ULL(52)	/* --interrupts */
#define OPT_FLAGS_PROGRESS	 STRESS_BIT_ULL(53)	/* --progress */
#define OPT_FLAGS_SYNC_START	 STRESS_BIT_ULL(54)	/* --sync-start */
#define OPT_FLAGS_RAPL		 STRESS_BIT_ULL(55)	/* --rapl */
#define OPT_FLAGS_RAPL_REQUIRED  STRESS_BIT_ULL(56)	/* set if RAPL is required */
#define OPT_FLAGS_C_STATES	 STRESS_BIT_ULL(57)	/* --c-states */
#define OPT_FLAGS_STRESSOR_TIME	 STRESS_BIT_ULL(58)	/* --stressor-time */
#define OPT_FLAGS_TASKSET_RANDOM STRESS_BIT_ULL(59)	/* --taskset-random */
#define OPT_FLAGS_BUILDINFO	 STRESS_BIT_ULL(60)	/* --buildinfo */
#define OPT_FLAGS_AUTOGROUP	 STRESS_BIT_ULL(61)	/* --autogroup */
#define OPT_FLAGS_RANDPROCNAME	 STRESS_BIT_ULL(62)	/* --randprocname */
#define OPT_FLAGS_OOM_NO_CHILD	 STRESS_BIT_ULL(63)	/* --oom-no-child */

#define OPT_FLAGS_MINMAX_MASK		\
	(OPT_FLAGS_MINIMIZE | OPT_FLAGS_MAXIMIZE)

/* Aggressive mode flags */
#define OPT_FLAGS_AGGRESSIVE_MASK 	\
	(OPT_FLAGS_MMAP_MADVISE |	\
	 OPT_FLAGS_MMAP_MINCORE |	\
	 OPT_FLAGS_AGGRESSIVE |		\
	 OPT_FLAGS_IGNITE_CPU)

extern const struct option stress_long_options[];

/* Command line long options */
typedef enum {
	OPT_undefined = 0,
	/* Short options */
	OPT_query = '?',
	OPT_aggressive = 'A',
	OPT_all = 'a',
	OPT_backoff = 'b',
	OPT_bigheap = 'B',
	OPT_cpu = 'c',
	OPT_cache = 'C',
	OPT_hdd = 'd',
	OPT_dentry = 'D',
	OPT_fork = 'f',
	OPT_fallocate = 'F',
	OPT_help = 'h',
	OPT_io = 'i',
	OPT_iostat = 'I',
	OPT_job = 'j',
	OPT_keep_name = 'k',
	OPT_klog_check = 'K',
	OPT_cpu_load = 'l',
	OPT_log_file = 'L',
	OPT_vm = 'm',
	OPT_metrics = 'M',
	OPT_dry_run = 'n',
	OPT_open = 'o',
	OPT_oomable = 'O',
	OPT_pipe = 'p',
	OPT_poll = 'P',
	OPT_quiet = 'q',
	OPT_random = 'r',
	OPT_rename = 'R',
	OPT_switch = 's',
	OPT_sock = 'S',
	OPT_timeout = 't',
	OPT_timer = 'T',
	OPT_urandom = 'u',
	OPT_verbose = 'v',
	OPT_version = 'V',
	OPT_with = 'w',
	OPT_exclude = 'x',
	OPT_yield = 'y',
	OPT_yaml = 'Y',

	/* Long options only */

	OPT_long_ops_start = 0x7f,

	OPT_abort,

	OPT_access,
	OPT_access_ops,

	OPT_acl,
	OPT_acl_rand,
	OPT_acl_ops,

	OPT_affinity,
	OPT_affinity_delay,
	OPT_affinity_ops,
	OPT_affinity_pin,
	OPT_affinity_rand,
	OPT_affinity_sleep,

	OPT_af_alg,
	OPT_af_alg_ops,
	OPT_af_alg_dump,

	OPT_aio,
	OPT_aio_ops,
	OPT_aio_requests,

	OPT_aiol,
	OPT_aiol_ops,
	OPT_aiol_requests,

	OPT_alarm,
	OPT_alarm_ops,

	OPT_apparmor,
	OPT_apparmor_ops,

	OPT_atomic,
	OPT_atomic_ops,

	OPT_autogroup,

	OPT_bad_altstack,
	OPT_bad_altstack_ops,

	OPT_bad_ioctl,
	OPT_bad_ioctl_method,
	OPT_bad_ioctl_ops,

	OPT_besselmath,
	OPT_besselmath_method,
	OPT_besselmath_ops,

	OPT_bigheap_bytes,
	OPT_bigheap_growth,
	OPT_bigheap_mlock,
	OPT_bigheap_ops,

	OPT_bind_mount,
	OPT_bind_mount_ops,

	OPT_binderfs,
	OPT_binderfs_ops,

	OPT_bitonicsort,
	OPT_bitonicsort_ops,
	OPT_bitonicsort_size,

	OPT_bitops,
	OPT_bitops_method,
	OPT_bitops_ops,

	OPT_branch,
	OPT_branch_ops,

	OPT_brk,
	OPT_brk_bytes,
	OPT_brk_mlock,
	OPT_brk_notouch,
	OPT_brk_ops,

	OPT_bsearch,
	OPT_bsearch_method,
	OPT_bsearch_ops,
	OPT_bsearch_size,

	OPT_bubblesort,
	OPT_bubblesort_method,
	OPT_bubblesort_ops,
	OPT_bubblesort_size,

	OPT_buildinfo,

	OPT_c_states,

	OPT_class,

	OPT_cache_ops,
	OPT_cache_size,
	OPT_cache_clflushopt,
	OPT_cache_cldemote,
	OPT_cache_clwb,
	OPT_cache_enable_all,
	OPT_cache_flush,
	OPT_cache_fence,
	OPT_cache_level,
	OPT_cache_sfence,
	OPT_cache_no_affinity,
	OPT_cache_permute,
	OPT_cache_prefetch,
	OPT_cache_prefetchw,
	OPT_cache_ways,

	OPT_cachehammer,
	OPT_cachehammer_numa,
	OPT_cachehammer_ops,

	OPT_cacheline,
	OPT_cacheline_ops,
	OPT_cacheline_affinity,
	OPT_cacheline_method,

	OPT_cap,
	OPT_cap_ops,

	OPT_cgroup,
	OPT_cgroup_ops,

	OPT_chattr,
	OPT_chattr_ops,

	OPT_change_cpu,

	OPT_chdir,
	OPT_chdir_dirs,
	OPT_chdir_ops,

	OPT_chmod,
	OPT_chmod_ops,

	OPT_chown,
	OPT_chown_ops,

	OPT_chroot,
	OPT_chroot_ops,

	OPT_chyperbolic,
	OPT_chyperbolic_method,
	OPT_chyperbolic_ops,

	OPT_clock,
	OPT_clock_ops,

	OPT_clone,
	OPT_clone_ops,
	OPT_clone_max,

	OPT_close,
	OPT_close_ops,

	OPT_context,
	OPT_context_ops,

	OPT_config,

	OPT_copy_file,
	OPT_copy_file_ops,
	OPT_copy_file_bytes,

	OPT_cpu_ops,
	OPT_cpu_method,
	OPT_cpu_load_slice,
	OPT_cpu_old_metrics,

	OPT_cpu_online,
	OPT_cpu_online_affinity,
	OPT_cpu_online_all,
	OPT_cpu_online_ops,

	OPT_cpu_sched,
	OPT_cpu_sched_ops,

	OPT_crypt,
	OPT_crypt_method,
	OPT_crypt_ops,

	OPT_ctrig,
	OPT_ctrig_method,
	OPT_ctrig_ops,

	OPT_cyclic,
	OPT_cyclic_ops,
	OPT_cyclic_dist,
	OPT_cyclic_method,
	OPT_cyclic_policy,
	OPT_cyclic_prio,
	OPT_cyclic_samples,
	OPT_cyclic_sleep,

	OPT_daemon,
	OPT_daemon_ops,
	OPT_daemon_wait,

	OPT_dccp,
	OPT_dccp_domain,
	OPT_dccp_if,
	OPT_dccp_msgs,
	OPT_dccp_ops,
	OPT_dccp_opts,
	OPT_dccp_port,

	OPT_dekker,
	OPT_dekker_ops,

	OPT_dentry_ops,
	OPT_dentries,
	OPT_dentry_order,

	OPT_dev,
	OPT_dev_ops,
	OPT_dev_file,

	OPT_dev_shm,
	OPT_dev_shm_ops,

	OPT_dfp,
	OPT_dfp_method,
	OPT_dfp_ops,

	OPT_dir,
	OPT_dir_ops,
	OPT_dir_dirs,

	OPT_dirdeep,
	OPT_dirdeep_ops,
	OPT_dirdeep_bytes,
	OPT_dirdeep_dirs,
	OPT_dirdeep_files,
	OPT_dirdeep_inodes,

	OPT_dirmany,
	OPT_dirmany_ops,
	OPT_dirmany_bytes,

	OPT_dnotify,
	OPT_dnotify_ops,

	OPT_dup,
	OPT_dup_ops,

	OPT_dynlib,
	OPT_dynlib_ops,

	OPT_easy_opcode,
	OPT_easy_opcode_ops,

	OPT_eigen,
	OPT_eigen_ops,
	OPT_eigen_method,
	OPT_eigen_size,

	OPT_efivar,
	OPT_efivar_ops,

	OPT_enosys,
	OPT_enosys_ops,

	OPT_env,
	OPT_env_ops,

	OPT_epoll,
	OPT_epoll_ops,
	OPT_epoll_port,
	OPT_epoll_domain,
	OPT_epoll_sockets,

	OPT_eventfd,
	OPT_eventfd_ops,
	OPT_eventfd_nonblock,

	OPT_exec,
	OPT_exec_ops,
	OPT_exec_max,
	OPT_exec_method,
	OPT_exec_fork_method,
	OPT_exec_no_pthread,

	OPT_exit_group,
	OPT_exit_group_ops,

	OPT_expmath,
	OPT_expmath_method,
	OPT_expmath_ops,

	OPT_factor,
	OPT_factor_digits,
	OPT_factor_ops,

	OPT_fallocate_ops,
	OPT_fallocate_bytes,

	OPT_fanotify,
	OPT_fanotify_ops,

	OPT_far_branch,
	OPT_far_branch_flush,
	OPT_far_branch_ops,
	OPT_far_branch_pageout,
	OPT_far_branch_pages,

	OPT_fault,
	OPT_fault_ops,

	OPT_fcntl,
	OPT_fcntl_ops,

	OPT_fd_abuse,
	OPT_fd_abuse_ops,

	OPT_fd_fork,
	OPT_fd_fork_fds,
	OPT_fd_fork_file,
	OPT_fd_fork_ops,

	OPT_fd_race,
	OPT_fd_race_dev,
	OPT_fd_race_ops,
	OPT_fd_race_proc,

	OPT_fibsearch,
	OPT_fibsearch_ops,
	OPT_fibsearch_size,

	OPT_fiemap,
	OPT_fiemap_ops,
	OPT_fiemap_bytes,

	OPT_fifo,
	OPT_fifo_data_size,
	OPT_fifo_ops,
	OPT_fifo_readers,

	OPT_file_ioctl,
	OPT_file_ioctl_ops,

	OPT_filename,
	OPT_filename_ops,
	OPT_filename_opts,

	OPT_filerace,
	OPT_filerace_ops,

	OPT_flipflop,
	OPT_flipflop_bits,
	OPT_flipflop_ops,
	OPT_flipflop_taskset1,
	OPT_flipflop_taskset2,

	OPT_flock,
	OPT_flock_ops,

	OPT_flushcache,
	OPT_flushcache_ops,

	OPT_fma,
	OPT_fma_ops,
	OPT_fma_libc,

	OPT_fork_max,
	OPT_fork_ops,
	OPT_fork_pageout,
	OPT_fork_unmap,
	OPT_fork_vm,

	OPT_forkheavy,
	OPT_forkheavy_allocs,
	OPT_forkheavy_mlock,
	OPT_forkheavy_ops,
	OPT_forkheavy_procs,

	OPT_fp,
	OPT_fp_method,
	OPT_fp_ops,

	OPT_fp_error,
	OPT_fp_error_ops,

	OPT_fpunch,
	OPT_fpunch_bytes,
	OPT_fpunch_ops,

	OPT_fractal,
	OPT_fractal_iterations,
	OPT_fractal_method,
	OPT_fractal_ops,
	OPT_fractal_xsize,
	OPT_fractal_ysize,

	OPT_fsize,
	OPT_fsize_ops,

	OPT_fstat,
	OPT_fstat_ops,
	OPT_fstat_dir,

	OPT_ftrace,

	OPT_full,
	OPT_full_ops,

	OPT_funccall,
	OPT_funccall_ops,
	OPT_funccall_method,

	OPT_funcret,
	OPT_funcret_ops,
	OPT_funcret_method,

	OPT_futex,
	OPT_futex_ops,

	OPT_get,
	OPT_get_ops,
	OPT_get_slow_sync,

	OPT_getrandom,
	OPT_getrandom_ops,

	OPT_getdent,
	OPT_getdent_ops,

	OPT_goto,
	OPT_goto_ops,
	OPT_goto_direction,

	OPT_gpu,
	OPT_gpu_ops,
	OPT_gpu_devnode,
	OPT_gpu_frag,
	OPT_gpu_upload,
	OPT_gpu_size,
	OPT_gpu_xsize,
	OPT_gpu_ysize,

	OPT_handle,
	OPT_handle_ops,

	OPT_hash,
	OPT_hash_ops,
	OPT_hash_method,

	OPT_hdd_bytes,
	OPT_hdd_write_size,
	OPT_hdd_ops,
	OPT_hdd_opts,

	OPT_heapsort,
	OPT_heapsort_method,
	OPT_heapsort_ops,
	OPT_heapsort_size,

	OPT_hrtimers,
	OPT_hrtimers_ops,
	OPT_hrtimers_adjust,

	OPT_hsearch,
	OPT_hsearch_method,
	OPT_hsearch_ops,
	OPT_hsearch_size,

	OPT_hyperbolic,
	OPT_hyperbolic_method,
	OPT_hyperbolic_ops,

	OPT_icache,
	OPT_icache_ops,

	OPT_icmp_flood,
	OPT_icmp_flood_ops,
	OPT_icmp_flood_max_size,

	OPT_idle_page,
	OPT_idle_page_ops,

	OPT_ignite_cpu,

	OPT_interrupts,

	OPT_inode_flags,
	OPT_inode_flags_ops,

	OPT_inotify,
	OPT_inotify_ops,

	OPT_insertionsort,
	OPT_insertionsort_ops,
	OPT_insertionsort_size,

	OPT_intmath,
	OPT_intmath_fast,
	OPT_intmath_method,
	OPT_intmath_ops,

	OPT_iomix,
	OPT_iomix_bytes,
	OPT_iomix_ops,

	OPT_ioport,
	OPT_ioport_ops,
	OPT_ioport_opts,
	OPT_ioport_port,

	OPT_ionice_class,
	OPT_ionice_level,

	OPT_ioprio,
	OPT_ioprio_ops,

	OPT_io_ops,

	OPT_io_uring,
	OPT_io_uring_entries,
	OPT_io_uring_ops,
	OPT_io_uring_rand,

	OPT_ipsec_mb,
	OPT_ipsec_mb_ops,
	OPT_ipsec_mb_feature,
	OPT_ipsec_mb_jobs,
	OPT_ipsec_mb_method,

	OPT_itimer,
	OPT_itimer_ops,
	OPT_itimer_freq,
	OPT_itimer_rand,

	OPT_jpeg,
	OPT_jpeg_ops,
	OPT_jpeg_height,
	OPT_jpeg_image,
	OPT_jpeg_width,
	OPT_jpeg_quality,

	OPT_judy,
	OPT_judy_ops,
	OPT_judy_size,

	OPT_kcmp,
	OPT_kcmp_ops,

	OPT_keep_files,

	OPT_key,
	OPT_key_ops,

	OPT_kill,
	OPT_kill_ops,

	OPT_klog,
	OPT_klog_ops,

	OPT_ksm,

	OPT_kvm,
	OPT_kvm_ops,

	OPT_l1cache,
	OPT_l1cache_line_size,
	OPT_l1cache_method,
	OPT_l1cache_mlock,
	OPT_l1cache_ops,
	OPT_l1cache_sets,
	OPT_l1cache_size,
	OPT_l1cache_ways,

	OPT_landlock,
	OPT_landlock_ops,

	OPT_lease,
	OPT_lease_ops,
	OPT_lease_breakers,

	OPT_led,
	OPT_led_ops,

	OPT_limit_as,
	OPT_limit_data,
	OPT_limit_stack,

	OPT_link,
	OPT_link_ops,
	OPT_link_sync,

	OPT_list,
	OPT_list_ops,
	OPT_list_method,
	OPT_list_size,

	OPT_llc_affinity,
	OPT_llc_affinity_clflush,
	OPT_llc_affinity_mlock,
	OPT_llc_affinity_numa,
	OPT_llc_affinity_ops,
	OPT_llc_affinity_size,

	OPT_loadavg,
	OPT_loadavg_ops,
	OPT_loadavg_max,

	OPT_lockbus,
	OPT_lockbus_ops,
	OPT_lockbus_nosplit,

	OPT_locka,
	OPT_locka_ops,

	OPT_lockf,
	OPT_lockf_ops,
	OPT_lockf_nonblock,

	OPT_lockmix,
	OPT_lockmix_ops,

	OPT_lockofd,
	OPT_lockofd_ops,

	OPT_log_brief,
	OPT_log_lockless,

	OPT_logmath,
	OPT_logmath_method,
	OPT_logmath_ops,

	OPT_longjmp,
	OPT_longjmp_ops,

	OPT_loop,
	OPT_loop_ops,

	OPT_lsearch,
	OPT_lsearch_method,
	OPT_lsearch_ops,
	OPT_lsearch_size,

	OPT_lsm,
	OPT_lsm_ops,

	OPT_madvise,
	OPT_madvise_ops,
	OPT_madvise_hwpoison,

	OPT_mbind,

	OPT_malloc,
	OPT_malloc_ops,
	OPT_malloc_bytes,
	OPT_malloc_max,
	OPT_malloc_mlock,
	OPT_malloc_pthreads,
	OPT_malloc_threshold,
	OPT_malloc_touch,
	OPT_malloc_trim,
	OPT_malloc_zerofree,

	OPT_matrix,
	OPT_matrix_ops,
	OPT_matrix_size,
	OPT_matrix_method,
	OPT_matrix_yx,

	OPT_matrix_3d,
	OPT_matrix_3d_ops,
	OPT_matrix_3d_size,
	OPT_matrix_3d_method,
	OPT_matrix_3d_zyx,

	OPT_maximize,
	OPT_max_fd,

	OPT_mcontend,
	OPT_mcontend_numa,
	OPT_mcontend_ops,

	OPT_membarrier,
	OPT_membarrier_ops,

	OPT_memcpy,
	OPT_memcpy_ops,
	OPT_memcpy_method,

	OPT_memfd,
	OPT_memfd_bytes,
	OPT_memfd_fds,
	OPT_memfd_madvise,
	OPT_memfd_mlock,
	OPT_memfd_numa,
	OPT_memfd_ops,
	OPT_memfd_zap_pte,

	OPT_memhotplug,
	OPT_memhotplug_ops,
	OPT_memhotplug_mmap,

	OPT_memrate,
	OPT_memrate_bytes,
	OPT_memrate_flush,
	OPT_memrate_method,
	OPT_memrate_ops,
	OPT_memrate_rd_mbs,
	OPT_memrate_wr_mbs,

	OPT_memthrash,
	OPT_memthrash_ops,
	OPT_memthrash_method,

	OPT_mergesort,
	OPT_mergesort_method,
	OPT_mergesort_ops,
	OPT_mergesort_size,

	OPT_metamix,
	OPT_metamix_ops,
	OPT_metamix_bytes,

	OPT_metrics_brief,

	OPT_mincore,
	OPT_mincore_ops,
	OPT_mincore_rand,

	OPT_min_nanosleep,
	OPT_min_nanosleep_ops,
	OPT_min_nanosleep_max,
	OPT_min_nanosleep_sched,

	OPT_misaligned,
	OPT_misaligned_ops,
	OPT_misaligned_method,

	OPT_mknod,
	OPT_mknod_ops,

	OPT_minimize,

	OPT_mlock,
	OPT_mlock_ops,

	OPT_mlockmany,
	OPT_mlockmany_ops,
	OPT_mlockmany_procs,

	OPT_mmap,
	OPT_mmap_async,
	OPT_mmap_bytes,
	OPT_mmap_file,
	OPT_mmap_madvise,
	OPT_mmap_mergeable,
	OPT_mmap_mlock,
	OPT_mmap_mmap2,
	OPT_mmap_mprotect,
	OPT_mmap_numa,
	OPT_mmap_odirect,
	OPT_mmap_ops,
	OPT_mmap_osync,
	OPT_mmap_slow_munmap,
	OPT_mmap_stressful,
	OPT_mmap_write_check,

	OPT_mmapaddr,
	OPT_mmapaddr_mlock,
	OPT_mmapaddr_ops,

	OPT_mmapcow,
	OPT_mmapcow_fork,
	OPT_mmapcow_free,
	OPT_mmapcow_mlock,
	OPT_mmapcow_numa,
	OPT_mmapcow_ops,

	OPT_mmapfiles,
	OPT_mmapfiles_numa,
	OPT_mmapfiles_ops,
	OPT_mmapfiles_populate,
	OPT_mmapfiles_shared,

	OPT_mmapfixed,
	OPT_mmapfixed_mlock,
	OPT_mmapfixed_numa,
	OPT_mmapfixed_ops,

	OPT_mmapfork,
	OPT_mmapfork_ops,
	OPT_mmapfork_bytes,

	OPT_mmaphuge,
	OPT_mmaphuge_file,
	OPT_mmaphuge_mlock,
	OPT_mmaphuge_mmaps,
	OPT_mmaphuge_numa,
	OPT_mmaphuge_ops,

	OPT_mmapmany,
	OPT_mmapmany_mlock,
	OPT_mmapmany_numa,
	OPT_mmapmany_ops,

	OPT_mmaprandom,
	OPT_mmaprandom_ops,
	OPT_mmaprandom_mappings,
	OPT_mmaprandom_maxpages,
	OPT_mmaprandom_numa,

	OPT_mmaptorture,
	OPT_mmaptorture_bytes,
	OPT_mmaptorture_msync,
	OPT_mmaptorture_ops,

	OPT_module,
	OPT_module_name,
	OPT_module_no_modver,
	OPT_module_no_vermag,
	OPT_module_no_unload,
	OPT_module_ops,

	OPT_monte_carlo,
	OPT_monte_carlo_method,
	OPT_monte_carlo_ops,
	OPT_monte_carlo_rand,
	OPT_monte_carlo_samples,

	OPT_mprotect,
	OPT_mprotect_ops,

	OPT_mpfr,
	OPT_mpfr_ops,
	OPT_mpfr_precision,

	OPT_mq,
	OPT_mq_ops,
	OPT_mq_size,

	OPT_mremap,
	OPT_mremap_ops,
	OPT_mremap_bytes,
	OPT_mremap_mlock,
	OPT_mremap_numa,

	OPT_mseal,
	OPT_mseal_ops,

	OPT_msg,
	OPT_msg_bytes,
	OPT_msg_ops,
	OPT_msg_types,

	OPT_msync,
	OPT_msync_bytes,
	OPT_msync_ops,

	OPT_msyncmany,
	OPT_msyncmany_ops,

	OPT_mtx,
	OPT_mtx_ops,
	OPT_mtx_procs,

	OPT_munmap,
	OPT_munmap_ops,

	OPT_mutex,
	OPT_mutex_ops,
	OPT_mutex_affinity,
	OPT_mutex_procs,

	OPT_nanosleep,
	OPT_nanosleep_method,
	OPT_nanosleep_ops,
	OPT_nanosleep_threads,

	OPT_netdev,
	OPT_netdev_ops,

	OPT_netlink_proc,
	OPT_netlink_proc_ops,

	OPT_netlink_task,
	OPT_netlink_task_ops,

	OPT_nice,
	OPT_nice_ops,

	OPT_no_madvise,
	OPT_no_oom_adjust,
	OPT_no_rand_seed,

	OPT_nop,
	OPT_nop_ops,
	OPT_nop_instr,

	OPT_null,
	OPT_null_ops,
	OPT_null_write,

	OPT_numa,
	OPT_numa_bytes,
	OPT_numa_ops,
	OPT_numa_shuffle_addr,
	OPT_numa_shuffle_node,

	OPT_oom_no_child,
	OPT_oom_avoid,
	OPT_oom_avoid_bytes,

	OPT_oom_pipe,
	OPT_oom_pipe_ops,

	OPT_opcode,
	OPT_opcode_ops,
	OPT_opcode_method,

	OPT_open_ops,
	OPT_open_fd,
	OPT_open_max,

	OPT_page_in,

	OPT_pathological,

	OPT_pause,

	OPT_pagemove,
	OPT_pagemove_bytes,
	OPT_pagemove_mlock,
	OPT_pagemove_numa,
	OPT_pagemove_ops,

	OPT_pageswap,
	OPT_pageswap_ops,

	OPT_pci,
	OPT_pci_dev,
	OPT_pci_ops,
	OPT_pci_ops_rate,

	OPT_perf_stats,

	OPT_permute,

	OPT_personality,
	OPT_personality_ops,

	OPT_peterson,
	OPT_peterson_ops,

	OPT_physpage,
	OPT_physpage_ops,
	OPT_physpage_mtrr,

	OPT_physmmap,
	OPT_physmmap_ops,
	OPT_physmmap_read,

	OPT_pidfd,
	OPT_pidfd_ops,

	OPT_ping_sock,
	OPT_ping_sock_ops,

	OPT_pipe_data_size,
	OPT_pipe_ops,
	OPT_pipe_size,
	OPT_pipe_vmsplice,

	OPT_pipeherd,
	OPT_pipeherd_ops,
	OPT_pipeherd_yield,

	OPT_pkey,
	OPT_pkey_ops,

	OPT_plugin,
	OPT_plugin_ops,
	OPT_plugin_method,
	OPT_plugin_so,

	OPT_poll_fds,
	OPT_poll_ops,
	OPT_poll_random_us,

	OPT_powmath,
	OPT_powmath_method,
	OPT_powmath_ops,

	OPT_prefetch,
	OPT_prefetch_l3_size,
	OPT_prefetch_method,
	OPT_prefetch_ops,

	OPT_prctl,
	OPT_prctl_ops,

	OPT_prime,
	OPT_prime_method,
	OPT_prime_ops,
	OPT_prime_progress,
	OPT_prime_start,

	OPT_prio_inv,
	OPT_prio_inv_ops,
	OPT_prio_inv_policy,
	OPT_prio_inv_type,

	OPT_priv_instr,
	OPT_priv_instr_ops,

	OPT_procfs,
	OPT_procfs_ops,

	OPT_progress,

	OPT_pseek,
	OPT_pseek_ops,
	OPT_pseek_rand,
	OPT_pseek_io_size,

	OPT_pthread,
	OPT_pthread_ops,
	OPT_pthread_max,

	OPT_ptrace,
	OPT_ptrace_ops,

	OPT_ptr_chase,
	OPT_ptr_chase_ops,
	OPT_ptr_chase_pages,

	OPT_pty,
	OPT_pty_ops,
	OPT_pty_max,

	OPT_qsort,
	OPT_qsort_ops,
	OPT_qsort_size,
	OPT_qsort_method,

	OPT_quota,
	OPT_quota_ops,

	OPT_race_sched,
	OPT_race_sched_ops,
	OPT_race_sched_method,

	OPT_radixsort,
	OPT_radixsort_method,
	OPT_radixsort_ops,
	OPT_radixsort_size,

	OPT_randlist,
	OPT_randlist_ops,
	OPT_randlist_compact,
	OPT_randlist_items,
	OPT_randlist_size,

	OPT_randprocname,

	OPT_rapl,
	OPT_raplstat,

	OPT_ramfs,
	OPT_ramfs_ops,
	OPT_ramfs_fill,
	OPT_ramfs_size,

	OPT_rawdev,
	OPT_rawdev_method,
	OPT_rawdev_ops,

	OPT_rawpkt,
	OPT_rawpkt_ops,
	OPT_rawpkt_port,
	OPT_rawpkt_rxring,

	OPT_rawsock,
	OPT_rawsock_ops,
	OPT_rawsock_port,

	OPT_rawudp,
	OPT_rawudp_ops,
	OPT_rawudp_if,
	OPT_rawudp_port,

	OPT_rdrand,
	OPT_rdrand_ops,
	OPT_rdrand_seed,

	OPT_readahead,
	OPT_readahead_ops,
	OPT_readahead_bytes,

	OPT_reboot,
	OPT_reboot_ops,

	OPT_regex,
	OPT_regex_ops,

	OPT_regs,
	OPT_regs_ops,

	OPT_remap,
	OPT_remap_mlock,
	OPT_remap_ops,
	OPT_remap_pages,

	OPT_rename_ops,

	OPT_resched,
	OPT_resched_ops,

	OPT_resctrl,

	OPT_resources,
	OPT_resources_mlock,
	OPT_resources_ops,

	OPT_revio,
	OPT_revio_ops,
	OPT_revio_opts,
	OPT_revio_bytes,

	OPT_ring_pipe,
	OPT_ring_pipe_num,
	OPT_ring_pipe_ops,
	OPT_ring_pipe_size,
	OPT_ring_pipe_splice,

	OPT_rlimit,
	OPT_rlimit_ops,

	OPT_rmap,
	OPT_rmap_ops,

	OPT_rotate,
	OPT_rotate_method,
	OPT_rotate_ops,

	OPT_rseq,
	OPT_rseq_ops,

	OPT_rtc,
	OPT_rtc_ops,

	OPT_sched,
	OPT_sched_prio,

	OPT_schedmix,
	OPT_schedmix_ops,
	OPT_schedmix_procs,

	OPT_schedpolicy,
	OPT_schedpolicy_ops,
	OPT_schedpolicy_rand,

	OPT_sched_period,
	OPT_sched_runtime,
	OPT_sched_deadline,
	OPT_sched_reclaim,

	OPT_sctp,
	OPT_sctp_ops,
	OPT_sctp_domain,
	OPT_sctp_if,
	OPT_sctp_port,
	OPT_sctp_sched,

	OPT_seal,
	OPT_seal_ops,

	OPT_seccomp,
	OPT_seccomp_ops,

	OPT_secretmem,
	OPT_secretmem_ops,

	OPT_seed,

	OPT_seek,
	OPT_seek_ops,
	OPT_seek_punch,
	OPT_seek_size,

	OPT_sendfile,
	OPT_sendfile_ops,
	OPT_sendfile_size,

	OPT_sem,
	OPT_sem_ops,
	OPT_sem_procs,
	OPT_sem_shared,

	OPT_sem_sysv,
	OPT_sem_sysv_ops,
	OPT_sem_sysv_procs,
	OPT_sem_sysv_setall,

	OPT_session,
	OPT_session_ops,

	OPT_set,
	OPT_set_ops,

	OPT_settings,

	OPT_shellsort,
	OPT_shellsort_ops,
	OPT_shellsort_size,

	OPT_shm,
	OPT_shm_bytes,
	OPT_shm_mlock,
	OPT_shm_ops,
	OPT_shm_objs,

	OPT_shm_sysv,
	OPT_shm_sysv_bytes,
	OPT_shm_sysv_mlock,
	OPT_shm_sysv_ops,
	OPT_shm_sysv_segs,

	OPT_sequential,

	OPT_sigabrt,
	OPT_sigabrt_ops,

	OPT_sigbus,
	OPT_sigbus_ops,

	OPT_sigchld,
	OPT_sigchld_ops,

	OPT_sigfd,
	OPT_sigfd_ops,

	OPT_sigfpe,
	OPT_sigfpe_ops,

	OPT_sighup,
	OPT_sighup_ops,

	OPT_sigill,
	OPT_sigill_ops,

	OPT_sigio,
	OPT_sigio_ops,

	OPT_signal,
	OPT_signal_ops,

	OPT_signest,
	OPT_signest_ops,

	OPT_sigpending,
	OPT_sigpending_ops,

	OPT_sigpipe,
	OPT_sigpipe_ops,

	OPT_sigq,
	OPT_sigq_ops,

	OPT_sigrt,
	OPT_sigrt_ops,

	OPT_sigsegv,
	OPT_sigsegv_ops,

	OPT_sigsuspend,
	OPT_sigsuspend_ops,

	OPT_sigtrap,
	OPT_sigtrap_ops,

	OPT_sigurg,
	OPT_sigurg_ops,

	OPT_sigvtalrm,
	OPT_sigvtalrm_ops,

	OPT_sigxcpu,
	OPT_sigxcpu_ops,

	OPT_sigxfsz,
	OPT_sigxfsz_ops,

	OPT_skiplist,
	OPT_skiplist_ops,
	OPT_skiplist_size,

	OPT_skip_silent,

	OPT_sleep,
	OPT_sleep_ops,
	OPT_sleep_max,

	OPT_smart,

	OPT_smi,
	OPT_smi_ops,

	OPT_sn,

	OPT_sock_ops,
	OPT_sock_domain,
	OPT_sock_if,
	OPT_sock_msgs,
	OPT_sock_nodelay,
	OPT_sock_opts,
	OPT_sock_port,
	OPT_sock_protocol,
	OPT_sock_type,
	OPT_sock_zerocopy,

	OPT_sockabuse,
	OPT_sockabuse_ops,
	OPT_sockabuse_port,

	OPT_sockdiag,
	OPT_sockdiag_ops,

	OPT_sockfd,
	OPT_sockfd_ops,
	OPT_sockfd_port,
	OPT_sockfd_reuse,

	OPT_sockmany,
	OPT_sockmany_if,
	OPT_sockmany_ops,
	OPT_sockmany_port,

	OPT_sockpair,
	OPT_sockpair_ops,

	OPT_softlockup,
	OPT_softlockup_ops,

	OPT_swap,
	OPT_swap_ops,
	OPT_swap_self,

	OPT_switch_ops,
	OPT_switch_freq,
	OPT_switch_method,

	OPT_spawn,
	OPT_spawn_ops,

	OPT_sparsematrix,
	OPT_sparsematrix_ops,
	OPT_sparsematrix_items,
	OPT_sparsematrix_method,
	OPT_sparsematrix_size,

	OPT_spinmem,
	OPT_spinmem_affinity,
	OPT_spinmem_method,
	OPT_spinmem_numa,
	OPT_spinmem_ops,
	OPT_spinmem_yield,

	OPT_splice,
	OPT_splice_ops,
	OPT_splice_bytes,

	OPT_stack,
	OPT_stack_ops,
	OPT_stack_fill,
	OPT_stack_mlock,
	OPT_stack_pageout,
	OPT_stack_unmap,

	OPT_stackmmap,
	OPT_stackmmap_ops,

	OPT_statmount,
	OPT_statmount_ops,

	OPT_status,

	OPT_stderr,
	OPT_stdout,

	OPT_str,
	OPT_str_ops,
	OPT_str_method,

	OPT_stream,
	OPT_stream_index,
	OPT_stream_l3_size,
	OPT_stream_madvise,
	OPT_stream_mlock,
	OPT_stream_ops,
	OPT_stream_prefetch,

	OPT_stressor_time,

	OPT_stressors,

	OPT_symlink,
	OPT_symlink_ops,
	OPT_symlink_sync,

	OPT_sync_file,
	OPT_sync_file_ops,
	OPT_sync_file_bytes,

	OPT_sync_start,

	OPT_syncload,
	OPT_syncload_ops,
	OPT_syncload_msbusy,
	OPT_syncload_mssleep,

	OPT_sysbadaddr,
	OPT_sysbadaddr_ops,

	OPT_syscall,
	OPT_syscall_method,
	OPT_syscall_ops,
	OPT_syscall_top,

	OPT_sysinfo,
	OPT_sysinfo_ops,

	OPT_sysinval,
	OPT_sysinval_ops,

	OPT_sysfs,
	OPT_sysfs_ops,

	OPT_syslog,

	OPT_tee,
	OPT_tee_ops,

	OPT_taskset,

	OPT_taskset_random,

	OPT_temp_path,

	OPT_thermalstat,
	OPT_thermal_zones,

	OPT_thrash,

	OPT_timer_slack,

	OPT_timer_ops,
	OPT_timer_freq,
	OPT_timer_rand,

	OPT_timerfd,
	OPT_timerfd_ops,
	OPT_timerfd_fds,
	OPT_timerfd_freq,
	OPT_timerfd_rand,

	OPT_timermix,
	OPT_timermix_ops,

	OPT_times,

	OPT_timestamp,

	OPT_time_warp,
	OPT_time_warp_ops,

	OPT_tlb_shootdown,
	OPT_tlb_shootdown_ops,

	OPT_tmpfs,
	OPT_tmpfs_ops,
	OPT_tmpfs_mmap_async,
	OPT_tmpfs_mmap_file,

	OPT_touch,
	OPT_touch_ops,
	OPT_touch_opts,
	OPT_touch_method,

	OPT_tree,
	OPT_tree_ops,
	OPT_tree_method,
	OPT_tree_size,

	OPT_trig,
	OPT_trig_method,
	OPT_trig_ops,

	OPT_tsc,
	OPT_tsc_ops,
	OPT_tsc_lfence,
	OPT_tsc_rdtscp,

	OPT_tsearch,
	OPT_tsearch_ops,
	OPT_tsearch_size,

	OPT_tun,
	OPT_tun_ops,
	OPT_tun_tap,

	OPT_udp,
	OPT_udp_ops,
	OPT_udp_port,
	OPT_udp_domain,
	OPT_udp_lite,
	OPT_udp_gro,
	OPT_udp_if,

	OPT_udp_flood,
	OPT_udp_flood_ops,
	OPT_udp_flood_domain,
	OPT_udp_flood_if,

	OPT_umask,
	OPT_umask_ops,

	OPT_umount,
	OPT_umount_ops,

	OPT_unlink,
	OPT_unlink_ops,

	OPT_unshare,
	OPT_unshare_ops,

	OPT_uprobe,
	OPT_uprobe_ops,

	OPT_urandom_ops,

	OPT_userfaultfd,
	OPT_userfaultfd_ops,
	OPT_userfaultfd_bytes,

	OPT_usersyscall,
	OPT_usersyscall_ops,

	OPT_utime,
	OPT_utime_ops,
	OPT_utime_fsync,

	OPT_vdso,
	OPT_vdso_ops,
	OPT_vdso_func,

	OPT_veccmp,
	OPT_veccmp_ops,

	OPT_vecfp,
	OPT_vecfp_ops,
	OPT_vecfp_method,

	OPT_vecmath,
	OPT_vecmath_ops,

	OPT_vecshuf,
	OPT_vecshuf_ops,
	OPT_vecshuf_method,

	OPT_vecwide,
	OPT_vecwide_ops,

	OPT_verify,
	OPT_verifiable,

	OPT_verity,
	OPT_verity_ops,

	OPT_vfork,
	OPT_vfork_ops,
	OPT_vfork_max,

	OPT_vforkmany,
	OPT_vforkmany_ops,
	OPT_vforkmany_vm,
	OPT_vforkmany_vm_bytes,

	OPT_vm_bytes,
	OPT_vm_flush,
	OPT_vm_hang,
	OPT_vm_keep,
	OPT_vm_populate,
	OPT_vm_locked,
	OPT_vm_ops,
	OPT_vm_madvise,
	OPT_vm_method,
	OPT_vm_numa,

	OPT_vm_addr,
	OPT_vm_addr_method,
	OPT_vm_addr_mlock,
	OPT_vm_addr_numa,
	OPT_vm_addr_ops,

	OPT_vm_rw,
	OPT_vm_rw_ops,
	OPT_vm_rw_bytes,

	OPT_vm_segv,
	OPT_vm_segv_ops,

	OPT_vm_splice,
	OPT_vm_splice_ops,
	OPT_vm_splice_bytes,

	OPT_vma,
	OPT_vma_ops,

	OPT_vmstat,
	OPT_vmstat_units,

	OPT_vnni,
	OPT_vnni_intrinsic,
	OPT_vnni_method,
	OPT_vnni_ops,

	OPT_wait,
	OPT_wait_ops,

	OPT_waitcpu,
	OPT_waitcpu_ops,

	OPT_watchdog,
	OPT_watchdog_ops,

	OPT_wcs,
	OPT_wcs_ops,
	OPT_wcs_method,

	OPT_workload,
	OPT_workload_dist,
	OPT_workload_load,
	OPT_workload_method,
	OPT_workload_ops,
	OPT_workload_quanta_us,
	OPT_workload_sched,
	OPT_workload_slice_us,
	OPT_workload_threads,

	OPT_x86cpuid,
	OPT_x86cpuid_ops,

	OPT_x86syscall,
	OPT_x86syscall_ops,
	OPT_x86syscall_func,

	OPT_xattr,
	OPT_xattr_ops,

	OPT_yield_ops,
	OPT_yield_procs,
	OPT_yield_sched,

	OPT_zero,
	OPT_zero_read,
	OPT_zero_ops,

	OPT_zlib,
	OPT_zlib_ops,
	OPT_zlib_level,
	OPT_zlib_mem_level,
	OPT_zlib_method,
	OPT_zlib_window_bits,
	OPT_zlib_stream_bytes,
	OPT_zlib_strategy,

	OPT_zombie,
	OPT_zombie_ops,
	OPT_zombie_max,
} stress_op_t;

#endif
