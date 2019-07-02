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
#ifndef __STRESS_NG_H__
#define __STRESS_NG_H__

#define _GNU_SOURCE
#define _ATFILE_SOURCE
#ifndef _LARGEFILE_SOURCE
#define _LARGEFILE_SOURCE
#endif
#ifndef _LARGEFILE64_SOURCE
#define _LARGEFILE64_SOURCE
#endif

#if !defined(_FILE_OFFSET_BITS)
#define _FILE_OFFSET_BITS 	(64)
#endif

/* Some Solaris tool chains only define __sun */
#if defined(__sun) && !defined(__sun__)
#define __sun__
#endif

/*
 *  Standard includes
 */
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <inttypes.h>
#include <limits.h>
#include <math.h>
#include <pwd.h>
#include <sched.h>
#include <search.h>
#include <setjmp.h>
#include <signal.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

/*
 *  Networking includes that are part of
 *  Single UNIX Specification V2
 */
#include <arpa/inet.h>
#include <netinet/in.h>
#if defined(HAVE_NET_IF_H)
#include <net/if.h>
#endif
#if defined(HAVE_NETINET_IP_H)
#include <netinet/ip.h>
#endif
#if defined(HAVE_NETINET_IP_ICMP_H)
#include <netinet/ip_icmp.h>
#endif
#if defined(HAVE_NETINET_TCP_H)
#include <netinet/tcp.h>
#endif
#if defined(HAVE_NETINET_SCTP_H)
#include <netinet/sctp.h>
#endif

#if defined(HAVE_AIO_H)
#include <aio.h>
#endif

#if defined(HAVE_COMPLEX_H)
#include <complex.h>
#endif

#if defined(HAVE_CPUID_H)
#include <cpuid.h>
#endif

#if defined(HAVE_CRYPT_H)
#include <crypt.h>
#endif

#if defined(HAVE_FEATURES_H)
#include <features.h>
#endif

#if defined(HAVE_FENV_H)
#include <fenv.h>
#endif

#if defined(HAVE_FLOAT_H)
#include <float.h>
#endif

#if defined(HAVE_GRP_H)
#include <grp.h>
#endif

#if defined(HAVE_INTEL_IPSEC_MB_H)
#include <intel-ipsec-mb.h>
#endif

#if defined(HAVE_KEYUTILS_H)
#include <keyutils.h>
#endif

#if defined(HAVE_LIBAIO_H)
#include <libaio.h>
#endif

#if defined(HAVE_LIBGEN_H)
#include <libgen.h>
#endif

#if defined(HAVE_LINK_H)
#include <link.h>
#endif

#if defined(HAVE_LOCALE_H)
#include <locale.h>
#endif

#if defined(HAVE_MALLOC_H)
#include <malloc.h>
#endif

#if defined(HAVE_MNTENT_H)
#include <mntent.h>
#endif

#if defined(HAVE_MQUEUE_H)
#include <mqueue.h>
#endif

#if defined(HAVE_POLL_H)
#include <poll.h>
#endif

#if defined(HAVE_LIB_PTHREAD)
#include <pthread.h>
#endif

#if defined(HAVE_SEMAPHORE_H)
#include <semaphore.h>
#endif

#if defined(HAVE_SPAWN_H)
#include <spawn.h>
#endif

#if defined(HAVE_STRINGS_H)
#include <strings.h>
#endif

#if defined(HAVE_SYSLOG_H)
#include <syslog.h>
#endif

#if defined(HAVE_TERMIO_H)
#include <termio.h>
#endif

#if defined(HAVE_TERMIOS_H)
#include <termios.h>
#endif

#if defined(HAVE_UCONTEXT_H)
#include <ucontext.h>
#endif

#if defined(HAVE_USTAT)
#include <ustat.h>
#endif

#if defined(HAVE_UTIME_H)
#include <utime.h>
#endif

#if defined(HAVE_WCHAR)
#include <wchar.h>
#endif

#if defined(HAVE_SYS_TREE_H)
#include <sys/tree.h>
#endif

#if defined(HAVE_BSD_STDLIB_H)
#include <bsd/stdlib.h>
#endif

#if defined(HAVE_BSD_STRING_H)
#include <bsd/string.h>
#endif

#if defined(HAVE_BSD_SYS_TREE_H)
#include <bsd/sys/tree.h>
#endif

#if defined(HAVE_BSD_WCHAR)
#include <bsd/wchar.h>
#endif

#if defined(HAVE_MODIFY_LDT)
#include <asm/ldt.h>
#endif

/*
 *  Various sys include files
 */
#include <sys/ioctl.h>
#include <sys/file.h>
#include <sys/mman.h>
#include <sys/resource.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/times.h>
#include <sys/types.h>
#include <sys/wait.h>

#if defined(HAVE_SYS_APPARMOR_H)
#include <sys/apparmor.h>
#endif

#if defined(HAVE_SYS_AUXV_H)
#include <sys/auxv.h>
#endif

#if defined(HAVE_SYS_CAPABILITY_H)
#include <sys/capability.h>
#endif

#if defined(HAVE_SYS_EPOLL_H)
#include <sys/epoll.h>
#endif

#if defined(HAVE_SYS_EVENTFD_H)
#include <sys/eventfd.h>
#endif

#if defined(HAVE_SYS_FANOTIFY_H)
#include <sys/fanotify.h>
#endif

#if defined(HAVE_SYS_IO_H)
#include <sys/io.h>
#endif

#if defined(HAVE_SYS_IPC_H)
#include <sys/ipc.h>
#endif

#if defined(HAVE_SYS_INOTIFY_H)
#include <sys/inotify.h>
#endif

#if defined(HAVE_SYS_MEMFD_H)
#include <sys/memfd.h>
#endif

#if defined(HAVE_SYS_MOUNT_H)
#include <sys/mount.h>
#endif

#if defined(HAVE_SYS_PARAM_H)
#include <sys/param.h>
#endif

#if defined(HAVE_SYS_MSG_H)
#include <sys/msg.h>
#endif

#if defined(HAVE_SYS_PERSONALITY_H)
#include <sys/personality.h>
#endif

#if defined(HAVE_SYS_PRCTL_H)
#include <sys/prctl.h>
#endif

#if defined(HAVE_PTRACE)
#include <sys/ptrace.h>
#endif

#if defined(HAVE_SYS_QUOTA_H)
#include <sys/quota.h>
#endif

#if defined(__APPLE__)
#include <sys/random.h>
#endif

#if defined(HAVE_SYS_SELECT_H)
#include <sys/select.h>
#endif

#if defined(HAVE_SEM_SYSV)
#include <sys/sem.h>
#endif

#if defined(HAVE_SYS_SENDFILE_H)
#include <sys/sendfile.h>
#endif

#if defined(HAVE_SYS_SHM_H)
#include <sys/shm.h>
#endif

#if defined(HAVE_SYS_SIGNALFD_H)
#include <sys/signalfd.h>
#endif

#if defined(HAVE_SYS_STATFS_H)
#include <sys/statfs.h>
#endif

#if defined(HAVE_SYS_STATVFS_H)
#include <sys/statvfs.h>
#endif

#if defined(__sun__)
/* Disable for SunOs/Solaris because */
#undef HAVE_SYS_SWAP_H
#endif
#if defined(HAVE_SYS_SWAP_H)
#include <sys/swap.h>
#endif

#if defined(HAVE_SYSCALL_H)
#include <sys/syscall.h>
#endif

#if defined(HAVE_SYS_SYSINFO_H)
#include <sys/sysinfo.h>
#endif

#if defined(HAVE_SYS_SYSMACROS_H)
#include <sys/sysmacros.h>
#endif

#if defined(HAVE_SYS_TIMEX_H)
#include <sys/timex.h>
#endif

#if defined(HAVE_SYS_TIMERFD_H)
#include <sys/timerfd.h>
#endif

#if defined(HAVE_SYS_UCRED_H)
#include <sys/ucred.h>
#endif

#if defined(HAVE_SYS_UIO_H)
#include <sys/uio.h>
#endif

#if defined(HAVE_SYS_UN_H)
#include <sys/un.h>
#endif

#if defined(HAVE_SYS_UTSNAME_H)
#include <sys/utsname.h>
#endif

#if defined(HAVE_SYS_VFS_H)
#include <sys/vfs.h>
#endif


/*
 *  SCSI related headers
 */
#if defined(HAVE_SCSI_SCSI_H)
#include <scsi/scsi.h>
#endif

#if defined(HAVE_SCSI_SG_H)
#include <scsi/sg.h>
#endif

/*
 *  Linux specific includes
 */
#if defined(HAVE_LINUX_AUDIT_H)
#include <linux/audit.h>
#endif

#if defined(HAVE_LINUX_CN_PROC_H)
#include <linux/cn_proc.h>
#endif

#if defined(HAVE_LINUX_CONNECTOR_H)
#include <linux/connector.h>
#endif

#if defined(HAVE_LINUX_DM_IOCTL_H)
#include <linux/dm-ioctl.h>
#endif

#if defined(HAVE_LINUX_GENETLINK_H)
#include <linux/genetlink.h>
#endif

#if defined(HAVE_LINUX_HDREG_H)
#include <linux/hdreg.h>
#endif

#if defined(HAVE_LINUX_IF_ALG_H)
#include <linux/if_alg.h>
#endif

#if defined(HAVE_LINUX_FIEMAP_H)
#include <linux/fiemap.h>
#endif

#if defined(HAVE_LINUX_FILTER_H)
#include <linux/filter.h>
#endif

#if defined(HAVE_LINUX_FS_H)
#include <linux/fs.h>
#endif

#if defined(HAVE_LINUX_FUTEX_H)
#include <linux/futex.h>
#endif

#if defined(HAVE_LINUX_HPET_H)
#include <linux/hpet.h>
#endif

#if defined(HAVE_LINUX_LOOP_H)
#include <linux/loop.h>
#endif

#if defined(HAVE_LINUX_MEDIA_H)
#include <linux/media.h>
#endif

#if defined(HAVE_LINUX_MEMBARRIER_H)
#include <linux/membarrier.h>
#endif

#if defined(HAVE_LINUX_NETLINK_H)
#include <linux/netlink.h>
#endif

#if defined(HAVE_LINUX_PERF_EVENT_H)
#include <linux/perf_event.h>
#endif

#if defined(HAVE_LINUX_POSIX_TYPES_H)
#include <linux/posix_types.h>
#endif

#if defined(HAVE_LINUX_RANDOM_H)
#include <linux/random.h>
#endif

#if defined(HAVE_LINUX_RTC_H)
#include <linux/rtc.h>
#endif

#if defined(HAVE_LINUX_RTNETLINK_H)
#include <linux/rtnetlink.h>
#endif

#if defined(HAVE_LINUX_SECCOMP_H)
#include <linux/seccomp.h>
#endif

#if defined(HAVE_LINUX_SOCK_DIAG_H)
#include <linux/sock_diag.h>
#endif

#if defined(HAVE_LINUX_SOCKET_H)
#include <linux/socket.h>
#endif

#if defined(HAVE_LINUX_SYSCTL_H)
#include <linux/sysctl.h>
#endif

#if defined(HAVE_LINUX_TASKSTATS_H)
#include <linux/taskstats.h>
#endif

#if defined(HAVE_LINUX_UNIX_DIAG_H)
#include <linux/unix_diag.h>
#endif

#if defined(HAVE_LINUX_USERFAULTFD_H)
#include <linux/userfaultfd.h>
#endif

#if defined(HAVE_LINUX_VERSION_H)
#include <linux/version.h>
#endif

#if defined(HAVE_LINUX_VIDEODEV2_H)
#include <linux/videodev2.h>
#endif

#if defined(HAVE_LINUX_VT_H)
#include <linux/vt.h>
#endif

#if defined(HAVE_LINUX_WATCHDOG_H)
#include <linux/watchdog.h>
#endif

/*
 *  We want sys/xattr.h in preference
 *  to the older attr/xattr.h if both
 *  are available
 */
#if defined(HAVE_SYS_XATTR_H)
#include <sys/xattr.h>
#undef HAVE_ATTR_XATTR_H
#elif defined(HAVE_ATTR_XATTR_H)
#include <attr/xattr.h>
#endif
/*  Sanity check */
#if defined(HAVE_SYS_XATTR_H) && defined(HAVE_ATTR_XATTR_H)
#error cannot have both HAVE_SYS_XATTR_H and HAVE_ATTR_XATTR_H
#endif

#if defined(HAVE_LIB_DL)
#include <dlfcn.h>
#include <gnu/lib-names.h>
#endif

/*
 *  Various system calls not included in libc (yet)
 */
#if defined(__linux__)

#if defined(__NR_add_key)
#define HAVE_ADD_KEY
#endif

#if defined(__NR_getcpu)
#define HAVE_GETCPU
#endif

#if defined(__NR_getdents)
#define HAVE_GETDENTS
#endif

#if defined(__NR_getdents64)
#define HAVE_GETDENTS64
#endif

#if defined(__NR_gettid)
#define HAVE_GETTID
#endif

#if defined(__NR_get_robust_list)
#define HAVE_GET_ROBUST_LIST
#endif

#if defined(__NR_ioprio_get)
#define HAVE_IOPRIO_GET
#endif

#if defined(__NR_ioprio_set)
#define HAVE_IOPRIO_SET
#endif

#if defined(__NR_kcmp)
#define HAVE_KCMP
#endif

#if defined(__NR_keyctl)
#define HAVE_KEYCTL
#endif

#if defined(__NR_membarrier)
#define HAVE_MEMBARRIER
#endif

#if defined(__NR_pkey_get)
#define HAVE_PKEY_GET
#endif

#if defined(__NR_pkey_set)
#define HAVE_PKEY_SET
#endif

#if defined(__NR_request_key)
#define HAVE_REQUEST_KEY
#endif

#if defined(__NR_sched_getattr)
#define HAVE_SCHED_GETATTR
#endif

#if defined(__NR_sched_setattr)
#define HAVE_SCHED_SETATTR
#endif

#if defined(__NR_set_robust_list)
#define HAVE_SET_ROBUST_LIST
#endif

#if defined(__NR_syslog)
#define HAVE_SYSLOG
#endif

#if defined(__NR_tgkill)
#define HAVE_TGKILL
#endif

#if defined(__NR_userfaultfd)
#define HAVE_USERFAULTFD
#endif

#endif

#include "stress-version.h"

/*
 *  BeagleBoneBlack with 4.1.15 kernel does not
 *  define the following, these should be defined
 *  in linux/posix_types.h - define them if they
 *  don't exist.
 */
#if !defined(HAVE_KERNEL_LONG_T)
typedef long int __kernel_long_t;
#endif
#if !defined(HAVE_KERNEL_ULONG_T)
typedef unsigned long int __kernel_ulong_t;
#endif

/*
 *  cacheflush(2) cache options
 */
#ifndef ICACHE
#define ICACHE  (1 << 0)
#endif
#ifndef DCACHE
#define DCACHE  (1 << 1)
#endif

#define EXIT_NOT_SUCCESS	(2)
#define EXIT_NO_RESOURCE	(3)
#define EXIT_NOT_IMPLEMENTED	(4)
#define EXIT_SIGNALED		(5)
#define EXIT_BY_SYS_EXIT	(6)

/*
 * STRESS_ASSERT(test)
 *   throw compile time error if test not true
 */
#define STRESS_CONCAT(a, b) a ## b
#define STRESS_CONCAT_EXPAND(a, b) STRESS_CONCAT(a, b)
#define STRESS_ASSERT(expr) \
	enum { STRESS_CONCAT_EXPAND(STRESS_ASSERT_AT_LINE_, __LINE__) = \
		1 / !!(expr) };

#define STRESS_MINIMUM(a,b) (((a) < (b)) ? (a) : (b))
#define STRESS_MAXIMUM(a,b) (((a) > (b)) ? (a) : (b))

/* NetBSD does not define MAP_ANONYMOUS */
#if defined(MAP_ANON) && !defined(MAP_ANONYMOUS)
#define MAP_ANONYMOUS MAP_ANON
#endif

/* GNU HURD and other systems that don't define PATH_MAX */
#ifndef PATH_MAX
#define PATH_MAX 		(4096)
#endif

/*
 * making local static fixes globbering warnings on older gcc versions
 */
#if defined(__GNUC__) || defined(__clang__)
#define NOCLOBBER	static
#else
#define NOCLOBBER
#endif

#if (_BSD_SOURCE || _SVID_SOURCE || !defined(__gnu_hurd__))
#define STRESS_PAGE_IN
#endif

#define STRESS_FD_MAX		(65536)		/* Max fds if we can't figure it out */
#define STRESS_PROCS_MAX	(8192)		/* Max number of processes per stressor */

#define DCCP_BUF		(1024)		/* DCCP I/O buffer size */
#define SOCKET_BUF		(8192)		/* Socket I/O buffer size */
#define UDP_BUF			(1024)		/* UDP I/O buffer size */
#define SOCKET_PAIR_BUF		(64)		/* Socket pair I/O buffer size */

#define ABORT_FAILURES		(5)		/* Number of failures before we abort */

/* debug output bitmasks */
#define PR_ERROR		 0x00000000000001ULL 	/* Print errors */
#define PR_INFO			 0x00000000000002ULL 	/* Print info */
#define PR_DEBUG		 0x00000000000004ULL 	/* Print debug */
#define PR_FAIL			 0x00000000000008ULL 	/* Print test failure message */
#define PR_ALL			 (PR_ERROR | PR_INFO | PR_DEBUG | PR_FAIL)

/* Option bit masks */
#define OPT_FLAGS_DRY_RUN	 0x00000000000010ULL	/* Don't actually run */
#define OPT_FLAGS_METRICS	 0x00000000000020ULL	/* Dump metrics at end */
#define OPT_FLAGS_RANDOM	 0x00000000000040ULL	/* Randomize */
#define OPT_FLAGS_SET		 0x00000000000080ULL	/* Set if user specifies stress procs */
#define OPT_FLAGS_KEEP_NAME	 0x00000000000100ULL	/* Keep stress names to stress-ng */
#define OPT_FLAGS_METRICS_BRIEF	 0x00000000000200ULL	/* dump brief metrics */
#define OPT_FLAGS_VERIFY	 0x00000000000400ULL	/* verify mode */
#define OPT_FLAGS_MMAP_MADVISE	 0x00000000000800ULL	/* enable random madvise settings */
#define OPT_FLAGS_MMAP_MINCORE	 0x00000000001000ULL	/* mincore force pages into mem */
#define OPT_FLAGS_TIMES		 0x00000000002000ULL	/* user/system time summary */
#define OPT_FLAGS_HDD_SYNC	 0x00000000004000ULL	/* HDD O_SYNC */
#define OPT_FLAGS_HDD_DSYNC	 0x00000000008000ULL	/* HDD O_DYNC */
#define OPT_FLAGS_HDD_DIRECT	 0x00000000010000ULL	/* HDD O_DIRECT */
#define OPT_FLAGS_HDD_NOATIME	 0x00000000020000ULL	/* HDD O_NOATIME */
#define OPT_FLAGS_MINIMIZE	 0x00000000040000ULL	/* Minimize */
#define OPT_FLAGS_MAXIMIZE	 0x00000000080000ULL	/* Maximize */
#define OPT_FLAGS_SYSLOG	 0x00000000100000ULL	/* log test progress to syslog */
#define OPT_FLAGS_AGGRESSIVE	 0x00000000200000ULL	/* aggressive mode enabled */
#define OPT_FLAGS_ALL		 0x00000000400000ULL	/* --all mode */
#define OPT_FLAGS_SEQUENTIAL	 0x00000000800000ULL	/* --sequential mode */
#define OPT_FLAGS_PERF_STATS	 0x00000001000000ULL	/* --perf stats mode */
#define OPT_FLAGS_LOG_BRIEF	 0x00000002000000ULL	/* --log-brief */
#define OPT_FLAGS_THERMAL_ZONES  0x00000004000000ULL	/* --tz thermal zones */
#define OPT_FLAGS_TIMER_SLACK	 0x00000008000000ULL	/* --timer-slack */
#define OPT_FLAGS_SOCKET_NODELAY 0x00000010000000ULL	/* --sock-nodelay */
#define OPT_FLAGS_IGNITE_CPU	 0x00000020000000ULL	/* --cpu-ignite */
#define OPT_FLAGS_PATHOLOGICAL	 0x00000040000000ULL	/* --pathological */
#define OPT_FLAGS_NO_RAND_SEED	 0x00000080000000ULL	/* --no-rand-seed */
#define OPT_FLAGS_THRASH	 0x00000100000000ULL	/* --thrash */
#define OPT_FLAGS_OOMABLE	 0x00000200000000ULL	/* --oomable */
#define OPT_FLAGS_ABORT		 0x00000400000000ULL	/* --abort */
#define OPT_FLAGS_CPU_ONLINE_ALL 0x00000800000000ULL	/* --cpu-online-all */
#define OPT_FLAGS_TIMESTAMP	 0x00001000000000ULL	/* --timestamp */

#define OPT_FLAGS_MINMAX_MASK		\
	(OPT_FLAGS_MINIMIZE | OPT_FLAGS_MAXIMIZE)

/* Aggressive mode flags */
#define OPT_FLAGS_AGGRESSIVE_MASK 	\
	(OPT_FLAGS_MMAP_MADVISE |	\
	 OPT_FLAGS_MMAP_MINCORE |	\
	 OPT_FLAGS_HDD_SYNC |		\
	 OPT_FLAGS_HDD_DSYNC |		\
	 OPT_FLAGS_HDD_DIRECT |		\
	 OPT_FLAGS_AGGRESSIVE |		\
	 OPT_FLAGS_IGNITE_CPU)

#define WARN_ONCE_NO_CACHE	0x00000001	/* No /sys/../cpu0/cache */
#define WARN_ONCE_CACHE_DEFAULT	0x00000002	/* default cache size */
#define WARN_ONCE_CACHE_NONE	0x00000004	/* no cache info */
#define WARN_ONCE_CACHE_WAY	0x00000008	/* cache way too high */
#define WARN_ONCE_CACHE_SIZE	0x00000010	/* cache size info */
#define WARN_ONCE_CACHE_REDUCED	0x00000020	/* reduced cache */

/* Stressor classes */
#define CLASS_CPU		0x00000001	/* CPU only */
#define CLASS_MEMORY		0x00000002	/* Memory thrashers */
#define CLASS_CPU_CACHE		0x00000004	/* CPU cache */
#define CLASS_IO		0x00000008	/* I/O read/writes etc */
#define CLASS_NETWORK		0x00000010	/* Network, sockets, etc */
#define CLASS_SCHEDULER		0x00000020	/* Scheduling */
#define CLASS_VM		0x00000040	/* VM stress, big memory, swapping */
#define CLASS_INTERRUPT		0x00000080	/* interrupt floods */
#define CLASS_OS		0x00000100	/* generic OS tests */
#define CLASS_PIPE_IO		0x00000200	/* pipe I/O */
#define CLASS_FILESYSTEM	0x00000400	/* file system */
#define CLASS_DEV		0x00000800	/* device (null, zero, etc) */
#define CLASS_SECURITY		0x00001000	/* security APIs */
#define CLASS_PATHOLOGICAL	0x00002000	/* can hang a machine */

typedef struct proc_info *pproc_info_t;

/* Help information for options */
typedef struct {
	const char *opt_s;		/* short option */
	const char *opt_l;		/* long option */
	const char *description;	/* description */
} help_t;

/* native setting types */
typedef enum {
	TYPE_ID_UNDEFINED,
	TYPE_ID_UINT8,
	TYPE_ID_INT8,
	TYPE_ID_UINT16,
	TYPE_ID_INT16,
	TYPE_ID_UINT32,
	TYPE_ID_INT32,
	TYPE_ID_UINT64,
	TYPE_ID_INT64,
	TYPE_ID_SIZE_T,
	TYPE_ID_SSIZE_T,
	TYPE_ID_UINT,
	TYPE_ID_INT,
	TYPE_ID_ULONG,
	TYPE_ID_LONG,
	TYPE_ID_OFF_T,
	TYPE_ID_STR,
	TYPE_ID_BOOL,
	TYPE_ID_UINTPTR_T
} type_id_t;

/* settings for storing opt arg parsed data */
typedef struct setting {
	struct setting *next;		/* next setting in list */
	pproc_info_t	proc;
	char *name;			/* name of setting */
	type_id_t	type_id;	/* setting type */
	bool		global;		/* true if global */
	union {				/* setting value */
		uint8_t		uint8;
		int8_t		int8;
		uint16_t	uint16;
		int16_t		int16;
		uint32_t	uint32;
		int32_t		int32;
		uint64_t	uint64;
		int64_t		int64;
		size_t		size;
		ssize_t		ssize;
		unsigned int	uint;
		signed int	sint;
		unsigned long	ulong;
		signed long	slong;
		off_t		off;
		const char *	str;
		bool		boolean;
		uintptr_t	uintptr;/* for func pointers */
	} u;
} setting_t;

typedef union {
	volatile uint8_t	uint8_val;
	volatile uint16_t	uint16_val;
	volatile uint32_t	uint32_val;
	volatile uint64_t	uint64_val;
#if defined(HAVE_INT128_T)
	volatile __uint128_t	uint128_val;
#endif
	volatile float		float_val;
	volatile double		double_val;
	volatile long double	long_double_val;
} put_val_t;

/* Network domains flags */
#define DOMAIN_INET		0x00000001	/* AF_INET */
#define DOMAIN_INET6		0x00000002	/* AF_INET6 */
#define DOMAIN_UNIX		0x00000004	/* AF_UNIX */

#define DOMAIN_INET_ALL		(DOMAIN_INET | DOMAIN_INET6)
#define DOMAIN_ALL		(DOMAIN_INET | DOMAIN_INET6 | DOMAIN_UNIX)

/* Large prime to stride around large VM regions */
#define PRIME_64		(0x8f0000000017116dULL)

typedef uint32_t class_t;

/* stressor args */
typedef struct {
	uint64_t *const counter;	/* stressor counter */
	const char *name;		/* stressor name */
	const uint64_t max_ops;		/* max number of bogo ops */
	const uint32_t instance;	/* stressor instance # */
	const uint32_t num_instances;	/* number of instances */
	pid_t pid;			/* stressor pid */
	pid_t ppid;			/* stressor ppid */
	size_t page_size;		/* page size */
} args_t;

typedef struct {
	const int opt;			/* optarg option*/
	int (*opt_set_func)(const char *opt); /* function to set it */
} opt_set_func_t;

typedef struct {
	int (*stressor)(const args_t *args);
	int (*supported)(void);
	void (*init)(void);
	void (*deinit)(void);
	void (*set_default)(void);
	void (*set_limit)(uint64_t max);
	const class_t class;
	const opt_set_func_t *opt_set_funcs;
	const help_t *help;
} stressor_info_t;

/* pthread wrapped args_t */
typedef struct {
	const args_t *args;
	void *data;
} pthread_args_t;

/* gcc 4.7 and later support vector ops */
#if defined(__GNUC__) && NEED_GNUC(4,7,0)
#define STRESS_VECTOR	1
#endif

/* gcc 7.0 and later support __attribute__((fallthrough)); */
#if defined(__GNUC__) && NEED_GNUC(7,0,0)
#define CASE_FALLTHROUGH __attribute__((fallthrough)) /* Fallthrough */
#else
#define CASE_FALLTHROUGH /* Fallthrough */
#endif

/* no return hint */
#if defined(__GNUC__) && NEED_GNUC(2,5,0)
#define NORETURN 	__attribute__ ((noreturn))
#else
#define NORETURN
#endif

/* force inlining hint */
#if defined(__GNUC__) && NEED_GNUC(3,4,0)	/* or possibly earlier */ \
 && ((!defined(__s390__) && !defined(__s390x__)) || NEED_GNUC(6,0,1))
#define ALWAYS_INLINE	__attribute__ ((always_inline))
#else
#define ALWAYS_INLINE
#endif

/* force no inlining hint */
#if defined(__GNUC__) && NEED_GNUC(3,4,0)	/* or possibly earier */
#define NOINLINE	__attribute__ ((noinline))
#else
#define NOINLINE
#endif

/* -O3 attribute support */
#if defined(__GNUC__) && !defined(__clang__) && NEED_GNUC(4,6,0)
#define OPTIMIZE3 	__attribute__((optimize("-O3")))
#else
#define OPTIMIZE3
#endif

/* -O1 attribute support */
#if defined(__GNUC__) && !defined(__clang__) && NEED_GNUC(4,6,0)
#define OPTIMIZE1 	__attribute__((optimize("-O1")))
#else
#define OPTIMIZE1
#endif

/* -O0 attribute support */
#if defined(__GNUC__) && !defined(__clang__) && NEED_GNUC(4,6,0)
#define OPTIMIZE0 	__attribute__((optimize("-O0")))
#else
#define OPTIMIZE0
#endif

/* warn unused attribute */
#if defined(__GNUC__) && NEED_GNUC(4,2,0)
#define WARN_UNUSED	__attribute__((warn_unused_result))
#else
#define WARN_UNUSED
#endif

#if defined(__GNUC__) && NEED_GNUC(3,3,0)
#define ALIGNED(a)	__attribute__((aligned(a)))
#else
#define ALIGNED(a)
#endif

/* Force aligment to nearest 128 bytes */
#if defined(__GNUC__) && NEED_GNUC(3,3,0) && defined(HAVE_ALIGNED_128)
#define ALIGN128	ALIGNED(128)
#else
#define ALIGN128
#endif

/* Force aligment to nearest 64 bytes */
#if defined(__GNUC__) && NEED_GNUC(3,3,0) && defined(HAVE_ALIGNED_64)
#define ALIGN64		ALIGNED(64)
#else
#define ALIGN64
#endif

#if defined(__GNUC__) && NEED_GNUC(4,6,0)
#define SECTION(s)	__attribute__((__section__(# s)))
#else
#define SECTION(s)
#endif

/* Choose cacheline alignment */
#if defined(ALIGN128)
#define ALIGN_CACHELINE ALIGN128
#else
#define ALIGN_CACHELINE ALIGN64
#endif

/* GCC hot attribute */
#if defined(__GNUC__) && NEED_GNUC(4,6,0)
#define HOT		__attribute__ ((hot))
#else
#define HOT
#endif

/* GCC mlocked data and data section attribute */
#if defined(__GNUC__) && NEED_GNUC(4,6,0) && !defined(__sun__)
#define MLOCKED_DATA	__attribute__((__section__("mlocked_data")))
#define MLOCKED_TEXT	__attribute__((__section__("mlocked_text")))
#define MLOCKED_SECTION 1
#else
#define MLOCKED_DATA
#define MLOCKED_TEXT
#endif

/* print format attribute */
#if ((defined(__GNUC__) && NEED_GNUC(3,2,0)) ||	\
     (defined(__clang__) && NEED_CLANG(3, 7, 0)))
#define FORMAT(func, a, b) __attribute__((format(func, a, b)))
#else
#define FORMAT(func, a, b)
#endif

/* restrict keyword */
#if defined(__GNUC__) || defined(__clang__)
#define RESTRICT __restrict
#else
#define RESTRICT
#endif

/* optimisation on branching */
#if defined(__GNUC__)
#define LIKELY(x)	__builtin_expect((x),1)
#define UNLIKELY(x)	__builtin_expect((x),0)
#else
#define LIKELY(x)	(x)
#define UNLIKELY(x)	(x)
#endif

#if !defined(HAVE_BUILTIN_PREFETCH)
static inline void __builtin_prefetch(const void *addr, ...)
{
	va_list ap;

	va_start(ap, addr);
	va_end(ap);
}
#endif

/* waste some cycles */
#if defined(__GNUC__) || defined(__clang__)
#  if defined(HAVE_ASM_NOP)
#    define FORCE_DO_NOTHING() __asm__ __volatile__("nop;")
#  else
#    define FORCE_DO_NOTHING() __asm__ __volatile__("")
#  endif
#else
#  define FORCE_DO_NOTHING() while (0)
#endif

#if defined(__clang__) && NEED_CLANG(4, 0, 0)
#define PRAGMA_PUSH	_Pragma("GCC diagnostic push")
#define PRAGMA_POP	_Pragma("GCC diagnostic pop")
#define PRAGMA_WARN_OFF	_Pragma("GCC diagnostic ignored \"-Weverything\"")
#elif defined(__GNUC__) && NEED_GNUC(4, 4, 0)
#define PRAGMA_PUSH	_Pragma("GCC diagnostic push")
#define PRAGMA_POP	_Pragma("GCC diagnostic pop")
#define PRAGMA_WARN_OFF	_Pragma("GCC diagnostic ignored \"-Wall\"") \
			_Pragma("GCC diagnostic ignored \"-Wextra\"") \
			_Pragma("GCC diagnostic ignored \"-Wdeprecated-declarations\"") \
			_Pragma("GCC diagnostic ignored \"-Wcast-qual\"")
#else
#define PRAGMA_PUSH
#define PRAGMA_POP
#define PRAGMA_WARN_OFF
#endif

/* Logging helpers */
extern int pr_msg(FILE *fp, const uint64_t flag,
	const char *const fmt, va_list va) FORMAT(printf, 3, 0);
extern void pr_msg_fail(const uint64_t flag, const char *name, const char *what, const int err);
extern int pr_yaml(FILE *fp, const char *const fmt, ...) FORMAT(printf, 2, 3);
extern void pr_yaml_runinfo(FILE *fp);
extern void pr_openlog(const char *filename);
extern void pr_closelog(void);
extern void pr_fail_check(int *rc);

extern void pr_dbg(const char *fmt, ...)  FORMAT(printf, 1, 2);
extern void pr_inf(const char *fmt, ...)  FORMAT(printf, 1, 2);
extern void pr_err(const char *fmt, ...)  FORMAT(printf, 1, 2);
extern void pr_fail(const char *fmt, ...) FORMAT(printf, 1, 2);
extern void pr_tidy(const char *fmt, ...) FORMAT(printf, 1, 2);

extern void pr_lock(bool *locked);
extern void pr_unlock(bool *locked);
extern void pr_inf_lock(bool *locked, const char *fmt, ...)  FORMAT(printf, 2, 3);
extern void pr_dbg_lock(bool *locked, const char *fmt, ...)  FORMAT(printf, 2, 3);

extern void pr_fail_err__(const args_t *args, const char *msg);
extern void pr_fail_errno__(const args_t *args, const char *msg, int err);
extern void pr_fail_dbg__(const args_t *args, const char *msg);

#define pr_fail_err(msg)		pr_fail_err__(args, msg)
#define pr_fail_errno(msg, err)		pr_fail_errno__(args, msg, err)
#define pr_fail_dbg(msg)		pr_fail_dbg__(args, msg)

/* Memory size constants */
#define KB			(1ULL << 10)
#define	MB			(1ULL << 20)
#define GB			(1ULL << 30)
#define TB			(1ULL << 40)
#define PB			(1ULL << 50)
#define EB			(1ULL << 60)

#define PAGE_4K_SHIFT		(12)
#define PAGE_4K			(1 << PAGE_4K_SHIFT)

#define STACK_ALIGNMENT		(64)	/* thread stacks align to 64 bytes */

#define MIN_OPS			(1ULL)
#define MAX_OPS			(100000000ULL)
#define MAX_32			(0xffffffffUL)
#define MAX_48			(0xffffffffffffULL)
#define MAX_64			(0xffffffffffffffffULL)

/* Maximum memory limits, 256TB for 64 bit is good enough for 2017 */
#if UINTPTR_MAX == MAX_32
#define MAX_MEM_LIMIT		MAX_32
#else
#define MAX_MEM_LIMIT		MAX_48
#endif

/* Stressor defaults */
#define MIN_AIO_REQUESTS	(1)
#define MAX_AIO_REQUESTS	(4096)
#define DEFAULT_AIO_REQUESTS	(16)

#define MIN_AIO_LINUX_REQUESTS	(1)
#define MAX_AIO_LINUX_REQUESTS	(4096)
#define DEFAULT_AIO_LINUX_REQUESTS	(64)

#define MIN_BIGHEAP_GROWTH	(4 * KB)
#define MAX_BIGHEAP_GROWTH	(64 * MB)
#define DEFAULT_BIGHEAP_GROWTH	(64 * KB)

#define MIN_BSEARCH_SIZE	(1 * KB)
#define MAX_BSEARCH_SIZE	(4 * MB)
#define DEFAULT_BSEARCH_SIZE	(64 * KB)

#define MIN_CHDIR_DIRS		(64)
#define MAX_CHDIR_DIRS		(65536)
#define DEFAULT_CHDIR_DIRS	(8192)

#define MIN_CLONES		(1)
#define MAX_CLONES		(1000000)
#define DEFAULT_CLONES		(8192)

#define MIN_COPY_FILE_BYTES	(128 * MB)
#define MAX_COPY_FILE_BYTES	(256ULL * GB)
#define DEFAULT_COPY_FILE_BYTES	(256 * MB)
#define DEFAULT_COPY_FILE_SIZE  (2 * MB)

#define MIN_DCCP_PORT		(1024)
#define MAX_DCCP_PORT		(65535)
#define DEFAULT_DCCP_PORT	(10000)

#define MIN_DENTRIES		(1)
#define MAX_DENTRIES		(1000000)
#define DEFAULT_DENTRIES	(2048)

#define MIN_DIR_DIRS		(64)
#define MAX_DIR_DIRS		(65536)
#define DEFAULT_DIR_DIRS	(8192)

#define MIN_EPOLL_PORT		(1024)
#define MAX_EPOLL_PORT		(65535)
#define DEFAULT_EPOLL_PORT	(6000)

#define MIN_HDD_BYTES		(1 * MB)
#define MAX_HDD_BYTES		(256ULL * GB)
#define DEFAULT_HDD_BYTES	(1 * GB)

#define MIN_HDD_WRITE_SIZE	(1)
#define MAX_HDD_WRITE_SIZE	(4 * MB)
#define DEFAULT_HDD_WRITE_SIZE	(64 * 1024)

#define MIN_FALLOCATE_BYTES	(1 * MB)
#if UINTPTR_MAX == MAX_32
#define MAX_FALLOCATE_BYTES	(MAX_32)
#else
#define MAX_FALLOCATE_BYTES	(4 * GB)
#endif
#define DEFAULT_FALLOCATE_BYTES	(1 * GB)

#define MIN_FIEMAP_SIZE		(1 * MB)
#if UINTPTR_MAX == MAX_32
#define MAX_FIEMAP_SIZE		(0xffffe00)
#else
#define MAX_FIEMAP_SIZE		(256ULL * GB)
#endif
#define DEFAULT_FIEMAP_SIZE	(16 * MB)

#define MIN_FIFO_READERS	(1)
#define MAX_FIFO_READERS	(64)
#define DEFAULT_FIFO_READERS	(4)

#define MIN_ITIMER_FREQ		(1)
#define MAX_ITIMER_FREQ		(100000000)
#define DEFAULT_ITIMER_FREQ	(1000000)

#define MIN_MQ_SIZE		(1)
#define MAX_MQ_SIZE		(32)
#define DEFAULT_MQ_SIZE		(10)

#define MIN_SEMAPHORE_PROCS	(2)
#define MAX_SEMAPHORE_PROCS	(64)
#define DEFAULT_SEMAPHORE_PROCS	(2)

#define MIN_EXECS		(1)
#define MAX_EXECS		(16000)
#define DEFAULT_EXECS		(1)

#define MIN_FORKS		(1)
#define MAX_FORKS		(16000)
#define DEFAULT_FORKS		(1)

#define MIN_HEAPSORT_SIZE	(1 * KB)
#define MAX_HEAPSORT_SIZE	(4 * MB)
#define DEFAULT_HEAPSORT_SIZE	(256 * KB)

#define MIN_IOMIX_BYTES		(1 * MB)
#define MAX_IOMIX_BYTES		(256ULL * GB)
#define DEFAULT_IOMIX_BYTES	(1 * GB)

#define MIN_VFORKS		(1)
#define MAX_VFORKS		(16000)
#define DEFAULT_VFORKS		(1)

#define MIN_HSEARCH_SIZE	(1 * KB)
#define MAX_HSEARCH_SIZE	(4 * MB)
#define DEFAULT_HSEARCH_SIZE	(8 * KB)

#define MIN_LEASE_BREAKERS	(1)
#define MAX_LEASE_BREAKERS	(64)
#define DEFAULT_LEASE_BREAKERS	(1)

#define MIN_LSEARCH_SIZE	(1 * KB)
#define MAX_LSEARCH_SIZE	(1 * MB)
#define DEFAULT_LSEARCH_SIZE	(8 * KB)

#define MIN_MALLOC_BYTES	(1 * KB)
#if UINTPTR_MAX == MAX_32
#define MAX_MALLOC_BYTES	(MAX_32)
#else
#define MAX_MALLOC_BYTES	(4 * GB)
#endif
#define DEFAULT_MALLOC_BYTES	(64 * KB)

#define MIN_MALLOC_MAX		(32)
#define MAX_MALLOC_MAX		(256 * 1024)
#define DEFAULT_MALLOC_MAX	(64 * KB)

#define MIN_MALLOC_THRESHOLD	(1)
#define MAX_MALLOC_THRESHOLD	(256 * MB)
#define DEFAULT_MALLOC_THRESHOLD (128 * KB)

#define MIN_MATRIX_SIZE		(16)
#define MAX_MATRIX_SIZE		(8192)
#define DEFAULT_MATRIX_SIZE	(256)

#define MIN_MATRIX3D_SIZE	(16)
#define MAX_MATRIX3D_SIZE	(1024)
#define DEFAULT_MATRIX3D_SIZE	(64)

#define MIN_MEMFD_BYTES		(2 * MB)
#if UINTPTR_MAX == MAX_32
#define MAX_MEMFD_BYTES		(MAX_32)
#else
#define MAX_MEMFD_BYTES		(4 * GB)
#endif
#define DEFAULT_MEMFD_BYTES	(256 * MB)

#define MIN_MEMFD_FDS		(8)
#define MAX_MEMFD_FDS		(4096)
#define DEFAULT_MEMFD_FDS	(256)

#define MIN_MERGESORT_SIZE	(1 * KB)
#define MAX_MERGESORT_SIZE	(4 * MB)
#define DEFAULT_MERGESORT_SIZE	(256 * KB)

#define MIN_MMAP_BYTES		(4 * KB)
#if UINTPTR_MAX == MAX_32
#define MAX_MMAP_BYTES		(MAX_32)
#else
#define MAX_MMAP_BYTES		(4 * GB)
#endif
#define DEFAULT_MMAP_BYTES	(256 * MB)

#define MIN_MEMRATE_BYTES	(4 * KB)
#if UINTPTR_MAX == MAX_32
#define MAX_MEMRATE_BYTES	(MAX_32)
#else
#define MAX_MEMRATE_BYTES	(4 * GB)
#endif
#define DEFAULT_MEMRATE_BYTES	(256 * MB)

#define DEFAULT_MREMAP_BYTES	(256 * MB)
#define MIN_MREMAP_BYTES	(4 * KB)
#if UINTPTR_MAX == MAX_32
#define MAX_MREMAP_BYTES	(MAX_32)
#else
#define MAX_MREMAP_BYTES	(4 * GB)
#endif
#define DEFAULT_MREMAP_BYTES	(256 * MB)

#define MIN_MSYNC_BYTES		(1 * MB)	/* MUST NOT BE page size or less! */
#if UINTPTR_MAX == MAX_32
#define MAX_MSYNC_BYTES		(MAX_32)
#else
#define MAX_MSYNC_BYTES		(4 * GB)
#endif
#define DEFAULT_MSYNC_BYTES	(256 * MB)

#define MIN_PTHREAD		(1)
#define MAX_PTHREAD		(30000)
#define DEFAULT_PTHREAD		(1024)

#define MIN_PTYS		(8)
#define MAX_PTYS		(65536)
#define DEFAULT_PTYS		(65536)

#define MIN_QSORT_SIZE		(1 * KB)
#define MAX_QSORT_SIZE		(4 * MB)
#define DEFAULT_QSORT_SIZE	(256 * KB)

#define MIN_RADIXSORT_SIZE	(1 * KB)
#define MAX_RADIXSORT_SIZE	(4 * MB)
#define DEFAULT_RADIXSORT_SIZE	(256 * KB)

#define MIN_READAHEAD_BYTES	(1 * MB)
#define MAX_READAHEAD_BYTES	(256ULL * GB)
#define DEFAULT_READAHEAD_BYTES	(1 * GB)

#define MIN_REVIO_BYTES		(1 * MB)
#define MAX_REVIO_BYTES		(256ULL * GB)
#define DEFAULT_REVIO_BYTES	(1 * GB)

#define MIN_SCTP_PORT		(1024)
#define MAX_SCTP_PORT		(65535)
#define DEFAULT_SCTP_PORT	(9000)

#define MIN_SENDFILE_SIZE	(1 * KB)
#define MAX_SENDFILE_SIZE	(1 * GB)
#define DEFAULT_SENDFILE_SIZE	(4 * MB)

#define MIN_SEEK_SIZE		(1 * MB)
#if UINTPTR_MAX == MAX_32
#define MAX_SEEK_SIZE		(0xffffe00)
#else
#define MAX_SEEK_SIZE 		(256ULL * GB)
#endif
#define DEFAULT_SEEK_SIZE	(16 * MB)

#define MIN_SEQUENTIAL		(0)
#define MAX_SEQUENTIAL		(1000000)
#define DEFAULT_SEQUENTIAL	(0)	/* Disabled */
#define DEFAULT_PARALLEL	(0)	/* Disabled */

#define MIN_SHELLSORT_SIZE	(1 * KB)
#define MAX_SHELLSORT_SIZE	(4 * MB)
#define DEFAULT_SHELLSORT_SIZE	(256 * KB)

#define MIN_SHM_SYSV_BYTES	(1 * MB)
#define MAX_SHM_SYSV_BYTES	(256 * MB)
#define DEFAULT_SHM_SYSV_BYTES	(8 * MB)

#define MIN_SHM_SYSV_SEGMENTS	(1)
#define MAX_SHM_SYSV_SEGMENTS	(128)
#define DEFAULT_SHM_SYSV_SEGMENTS (8)

#define MIN_SHM_POSIX_BYTES	(1 * MB)
#define MAX_SHM_POSIX_BYTES	(1 * GB)
#define DEFAULT_SHM_POSIX_BYTES	(8 * MB)

#define MIN_SHM_POSIX_OBJECTS	(1)
#define MAX_SHM_POSIX_OBJECTS	(128)
#define DEFAULT_SHM_POSIX_OBJECTS (32)

#define MAX_SIGSUSPEND_PIDS	(4)

#define MIN_SLEEP		(1)
#define MAX_SLEEP		(30000)
#define DEFAULT_SLEEP		(1024)

#define MIN_SOCKET_PORT		(1024)
#define MAX_SOCKET_PORT		(65535)
#define DEFAULT_SOCKET_PORT	(5000)

#define MIN_SOCKET_FD_PORT	(1024)
#define MAX_SOCKET_FD_PORT	(65535)
#define DEFAULT_SOCKET_FD_PORT	(8000)

#define MIN_SPLICE_BYTES	(1*KB)
#define MAX_SPLICE_BYTES	(64*MB)
#define DEFAULT_SPLICE_BYTES	(64*KB)

#define MIN_STREAM_L3_SIZE	(4 * KB)
#if UINTPTR_MAX == MAX_32
#define MAX_STREAM_L3_SIZE	(MAX_32)
#else
#define MAX_STREAM_L3_SIZE	(4 * GB)
#endif
#define DEFAULT_STREAM_L3_SIZE	(4 * MB)

#define MIN_SYNC_FILE_BYTES	(1 * MB)
#if UINTPTR_MAX == MAX_32
#define MAX_SYNC_FILE_BYTES	(MAX_32)
#else
#define MAX_SYNC_FILE_BYTES	(4 * GB)
#endif
#define DEFAULT_SYNC_FILE_BYTES	(1 * GB)

#define MIN_TIMER_FREQ		(1)
#define MAX_TIMER_FREQ		(100000000)
#define DEFAULT_TIMER_FREQ	(1000000)

#define MIN_TIMERFD_FREQ	(1)
#define MAX_TIMERFD_FREQ	(100000000)
#define DEFAULT_TIMERFD_FREQ	(1000000)

#define MIN_TREE_SIZE		(1000)
#define MAX_TREE_SIZE		(25000000)
#define DEFAULT_TREE_SIZE	(250000)

#define MIN_TSEARCH_SIZE	(1 * KB)
#define MAX_TSEARCH_SIZE	(4 * MB)
#define DEFAULT_TSEARCH_SIZE	(64 * KB)

#define MIN_UDP_PORT		(1024)
#define MAX_UDP_PORT		(65535)
#define DEFAULT_UDP_PORT	(7000)

#define MIN_USERFAULTFD_BYTES	(4 * KB)
#if UINTPTR_MAX == MAX_32
#define MAX_USERFAULTFD_BYTES	(MAX_32)
#else
#define MAX_USERFAULTFD_BYTES	(4 * GB)
#endif
#define DEFAULT_USERFAULTFD_BYTES (16 * MB)

#define MIN_VM_BYTES		(4 * KB)
#if UINTPTR_MAX == MAX_32
#define MAX_VM_BYTES		(MAX_32)
#else
#define MAX_VM_BYTES		(4 * GB)
#endif
#define DEFAULT_VM_BYTES	(256 * MB)

#define MIN_VM_HANG		(0)
#define MAX_VM_HANG		(3600)
#define DEFAULT_VM_HANG		(~0ULL)

#define MIN_VM_RW_BYTES		(4 * KB)
#if UINTPTR_MAX == MAX_32
#define MAX_VM_RW_BYTES		(MAX_32)
#else
#define MAX_VM_RW_BYTES		(4 * GB)
#endif
#define DEFAULT_VM_RW_BYTES	(16 * MB)

#define MIN_VM_SPLICE_BYTES	(4*KB)
#define MAX_VM_SPLICE_BYTES	(64*MB)
#define DEFAULT_VM_SPLICE_BYTES	(64*KB)

#define MIN_ZOMBIES		(1)
#define MAX_ZOMBIES		(1000000)
#define DEFAULT_ZOMBIES		(8192)

#define TIMEOUT_NOT_SET		(~0ULL)
#define DEFAULT_TIMEOUT		(60 * 60 * 24)
#define DEFAULT_BACKOFF		(0)
#define DEFAULT_LINKS		(8192)
#define DEFAULT_DIRS		(8192)

#define STR_SHARED_SIZE		(65536 * 32)
#define MEM_CACHE_SIZE		(65536 * 32)
#define DEFAULT_CACHE_LEVEL     3
#define UNDEFINED		(-1)

#define PAGE_MAPPED		(0x01)
#define PAGE_MAPPED_FAIL	(0x02)

#if defined(__GNUC__) || defined(__clang__)
#define TYPEOF_CAST(a)	(typeof(a))
#else
#define	TYPEOF_CAST(a)
#endif

/* Generic bit setting on an array macros */
#define STRESS_NBITS(a)		(sizeof(a[0]) * 8)
#define STRESS_GETBIT(a, i)	(a[i / STRESS_NBITS(a)] & \
				 (TYPEOF_CAST(a[0])1 << (i & (STRESS_NBITS(a)-1))))
#define STRESS_CLRBIT(a, i)	(a[i / STRESS_NBITS(a)] &= \
				 ~(TYPEOF_CAST(a[0])1 << (i & (STRESS_NBITS(a)-1))))
#define STRESS_SETBIT(a, i)	(a[i / STRESS_NBITS(a)] |= \
				 (TYPEOF_CAST(a[0])1 << (i & (STRESS_NBITS(a)-1))))

#define SIEVE_SIZE 		(10000000)

/* MWC random number initial seed */
#define MWC_SEED_Z		(362436069UL)
#define MWC_SEED_W		(521288629UL)
#define MWC_SEED()		mwc_seed(MWC_SEED_W, MWC_SEED_Z)

#define SIZEOF_ARRAY(a)		(sizeof(a) / sizeof(a[0]))

/* Arch specific, x86 */
#if defined(__x86_64__) || defined(__x86_64) || \
    defined(__i386__)   || defined(__i386)
#define STRESS_X86	1
#endif

/* Arch specific, ARM */
#if defined(__ARM_ARCH_6__)   || defined(__ARM_ARCH_6J__)  || \
    defined(__ARM_ARCH_6K__)  || defined(__ARM_ARCH_6Z__)  || \
    defined(__ARM_ARCH_6ZK__) || defined(__ARM_ARCH_6T2__) || \
    defined(__ARM_ARCH_6M__)  || defined(__ARM_ARCH_7__)   || \
    defined(__ARM_ARCH_7A__)  || defined(__ARM_ARCH_7R__)  || \
    defined(__ARM_ARCH_7M__)  || defined(__ARM_ARCH_7EM__) || \
    defined(__ARM_ARCH_8A__)  || defined(__aarch64__)
#define STRESS_ARM      1
#endif

/* Arch specific, IBM S390 */
#if defined(__s390__)
#define STRESS_S390
#endif

/* Arch specific PPC64 */
#if defined(__PPC64__)
#define STRESS_PPC64
#endif

/* GCC5.0+ target_clones attribute */
#if defined(HAVE_TARGET_CLONES)
#define TARGET_CLONES	__attribute__((target_clones("mmx","sse","sse2","ssse3", "sse4.1", "sse4a", "avx", "avx2", "avx512f", "default")))
#else
#define TARGET_CLONES
#endif

/*
 *  See ioprio_set(2) and linux/ioprio.h, glibc has no definitions
 *  for these at present. Also refer to Documentation/block/ioprio.txt
 *  in the Linux kernel source.
 */
#if !defined(IOPRIO_CLASS_RT)
#define IOPRIO_CLASS_RT         (1)
#endif
#if !defined(IOPRIO_CLASS_BE)
#define IOPRIO_CLASS_BE         (2)
#endif
#if !defined(IOPRIO_CLASS_IDLE)
#define IOPRIO_CLASS_IDLE       (3)
#endif

#if !defined(IOPRIO_WHO_PROCESS)
#define IOPRIO_WHO_PROCESS      (1)
#endif
#if !defined(IOPRIO_WHO_PGRP)
#define IOPRIO_WHO_PGRP         (2)
#endif
#if !defined(IOPRIO_WHO_USER)
#define IOPRIO_WHO_USER         (3)
#endif

#if !defined(IOPRIO_PRIO_VALUE)
#define IOPRIO_PRIO_VALUE(class, data)  (((class) << 13) | data)
#endif

/* prctl(2) timer slack support */
#if defined(HAVE_SYS_PRCTL_H) && \
    defined(HAVE_PRCTL) && \
    defined(PR_SET_TIMERSLACK) && \
    defined(PR_GET_TIMERSLACK)
#define HAVE_PRCTL_TIMER_SLACK
#endif

/*
 *  checks to see if we should keep in running the stressors
 */
extern bool __keep_stressing(const args_t *args);

#define keep_stressing()	__keep_stressing(args)

/* increment the stessor bogo ops counter */
static inline void ALWAYS_INLINE inc_counter(const args_t *args)
{
	(*(args->counter))++;
}

static inline uint64_t ALWAYS_INLINE get_counter(const args_t *args)
{
	return *args->counter;
}

static inline void ALWAYS_INLINE set_counter(const args_t *args, const uint64_t val)
{
	*args->counter = val;
}

static inline void ALWAYS_INLINE add_counter(const args_t *args, const uint64_t inc)
{
	*args->counter += inc;
}

/* pthread porting shims, spinlock or fallback to mutex */
#if defined(HAVE_LIB_PTHREAD)
#if defined(HAVE_LIB_PTHREAD_SPINLOCK) && !defined(__DragonFly__) && !defined(__OpenBSD__)
typedef pthread_spinlock_t 	shim_pthread_spinlock_t;

#define SHIM_PTHREAD_PROCESS_SHARED		PTHREAD_PROCESS_SHARED
#define SHIM_PTHREAD_PROCESS_PRIVATE		PTHREAD_PROCESS_PRIVATE

#define shim_pthread_spin_lock(lock)		pthread_spin_lock(lock)
#define shim_pthread_spin_unlock(lock)		pthread_spin_unlock(lock)
#define shim_pthread_spin_init(lock, shared)	pthread_spin_init(lock, shared)
#define shim_pthread_spin_destroy(lock)		pthread_spin_destroy(lock)
#else
typedef pthread_mutex_t		shim_pthread_spinlock_t;

#define SHIM_PTHREAD_PROCESS_SHARED		NULL
#define SHIM_PTHREAD_PROCESS_PRIVATE		NULL

#define shim_pthread_spin_lock(lock)		pthread_mutex_lock(lock)
#define shim_pthread_spin_unlock(lock)		pthread_mutex_unlock(lock)
#define shim_pthread_spin_init(lock, shared)	pthread_mutex_init(lock, shared)
#define shim_pthread_spin_destroy(lock)		pthread_mutex_destroy(lock)
#endif
#endif

/* capabilities shim */
#if defined(CAP_MAC_ADMIN)
#define SHIM_CAP_MAC_ADMIN	CAP_MAC_ADMIN
#else
#define SHIM_CAP_MAC_ADMIN	(0)
#endif

#if defined(CAP_NET_ADMIN)
#define SHIM_CAP_NET_ADMIN	CAP_NET_ADMIN
#else
#define SHIM_CAP_NET_ADMIN	(0)
#endif

#if defined(CAP_NET_RAW)
#define SHIM_CAP_NET_RAW	CAP_NET_RAW
#else
#define SHIM_CAP_NET_RAW	(0)
#endif

#if defined(CAP_SYS_ADMIN)
#define SHIM_CAP_SYS_ADMIN	CAP_SYS_ADMIN
#else
#define SHIM_CAP_SYS_ADMIN	(0)
#endif

#if defined(CAP_SYS_NICE)
#define SHIM_CAP_SYS_NICE	CAP_SYS_NICE
#else
#define SHIM_CAP_SYS_NICE	(0)
#endif

#if defined(CAP_SYS_RESOURCE)
#define SHIM_CAP_SYS_RESOURCE	CAP_SYS_RESOURCE
#else
#define SHIM_CAP_SYS_RESOURCE	(0)
#endif

#if defined(CAP_SYS_TIME)
#define SHIM_CAP_SYS_TIME	CAP_SYS_TIME
#else
#define SHIM_CAP_SYS_TIME	(0)
#endif

/* stress process prototype */
typedef int (*stress_func_t)(const args_t *args);

/* Fast random number generator state */
typedef struct {
	uint32_t w;
	uint32_t z;
} mwc_t;

/* perf related constants */
#if defined(HAVE_LIB_PTHREAD) &&	\
    defined(HAVE_LINUX_PERF_EVENT_H) &&	\
    defined(__NR_perf_event_open)
#define STRESS_PERF_STATS	(1)
#define STRESS_PERF_INVALID	(~0ULL)
#define STRESS_PERF_MAX		(128)

/* per perf counter info */
typedef struct {
	uint64_t counter;		/* perf counter */
	int	 fd;			/* perf per counter fd */
} perf_stat_t;

/* per stressor perf info */
typedef struct {
	perf_stat_t	perf_stat[STRESS_PERF_MAX]; /* perf counters */
	int		perf_opened;	/* count of opened counters */
} stress_perf_t;
#endif

/* linux thermal zones */
#define	STRESS_THERMAL_ZONES	 (1)
#define STRESS_THERMAL_ZONES_MAX (31)	/* best if prime */

#if defined(STRESS_THERMAL_ZONES)
/* per stressor thermal zone info */
typedef struct tz_info {
	char	*path;			/* thermal zone path */
	char 	*type;			/* thermal zone type */
	size_t	index;			/* thermal zone # index */
	struct tz_info *next;		/* next thermal zone in list */
} tz_info_t;

typedef struct {
	uint64_t temperature;		/* temperature in Celsius * 1000 */
} tz_stat_t;

typedef struct {
	tz_stat_t tz_stat[STRESS_THERMAL_ZONES_MAX];
} stress_tz_t;
#endif

/* Per process statistics and accounting info */
typedef struct {
	uint64_t counter;		/* number of bogo ops */
	struct tms tms;			/* run time stats of process */
	double start;			/* wall clock start time */
	double finish;			/* wall clock stop time */
#if defined(STRESS_PERF_STATS)
	stress_perf_t sp;		/* perf counters */
#endif
#if defined(STRESS_THERMAL_ZONES)
	stress_tz_t tz;			/* thermal zones */
#endif
	bool run_ok;			/* true if stressor exited OK */
} proc_stats_t;

/* The stress-ng global shared memory segment */
typedef struct {
	size_t length;					/* Size of segment */
	uint8_t	*mem_cache;				/* Shared memory cache */
	uint64_t mem_cache_size;			/* Bytes */
	uint16_t mem_cache_level;			/* 1=L1, 2=L2, 3=L3 */
	uint16_t padding1;				/* alignment padding */
	uint32_t mem_cache_ways;			/* cache ways size */
	uint64_t zero;					/* zero'd data */
	struct {
		uint32_t	flags;			/* flag bits */
#if defined(HAVE_LIB_PTHREAD)
		shim_pthread_spinlock_t lock;		/* protection lock */
#endif
	} warn_once;
	uint32_t warn_once_flags;			/* Warn once flags */
	struct {
		uint64_t val64;
		uint32_t val32;
		uint16_t val16;
		uint8_t	 val8;
		uint8_t	 padding2;			/* more padding */
	} atomic;					/* Shared atomic temp vars */
	struct {
		uint32_t futex[STRESS_PROCS_MAX];	/* Shared futexes */
		uint64_t timeout[STRESS_PROCS_MAX];	/* Shared futex timeouts */
	} futex;
	struct {
		key_t key_id;				/* System V semaphore key id */
		int sem_id;				/* System V semaphore id */
		bool init;				/* System V semaphore initialized */
	} sem_sysv;
#if defined(STRESS_PERF_STATS)
	struct {
		bool no_perf;				/* true = Perf not available */
		shim_pthread_spinlock_t lock;		/* spinlock on no_perf updates */
	} perf;
#endif
	bool *af_alg_hash_skip;				/* Shared array of hash skip flags */
	bool *af_alg_cipher_skip;			/* Shared array of cipher skip flags */
#if defined(STRESS_THERMAL_ZONES)
	tz_info_t *tz_info;				/* List of valid thermal zones */
#endif
#if defined(HAVE_ATOMIC)
	uint32_t softlockup_count;			/* Atomic counter of softlock children */
#endif
	uint8_t  str_shared[STR_SHARED_SIZE];		/* str copying buffer */
	proc_stats_t stats[0];				/* Shared statistics */
} shared_t;

/* Stress test classes */
typedef struct {
	class_t class;			/* Class type bit mask */
	const char *name;		/* Name of class */
} class_info_t;

#define STRESSORS(MACRO)	\
	MACRO(access) 		\
	MACRO(af_alg) 		\
	MACRO(affinity) 	\
	MACRO(aio) 		\
	MACRO(aiol) 		\
	MACRO(apparmor) 	\
	MACRO(atomic)		\
	MACRO(bad_altstack) 	\
	MACRO(bigheap)		\
	MACRO(bind_mount)	\
	MACRO(branch)		\
	MACRO(brk)		\
	MACRO(bsearch)		\
	MACRO(cache)		\
	MACRO(cap)		\
	MACRO(chdir)		\
	MACRO(chmod)		\
	MACRO(chown)		\
	MACRO(chroot)		\
	MACRO(clock)		\
	MACRO(clone)		\
	MACRO(close)		\
	MACRO(context)		\
	MACRO(copy_file)	\
	MACRO(cpu)		\
	MACRO(cpu_online)	\
	MACRO(crypt)		\
	MACRO(cyclic)		\
	MACRO(daemon)		\
	MACRO(dccp)		\
	MACRO(dentry)		\
	MACRO(dev)		\
	MACRO(dev_shm)		\
	MACRO(dir)		\
	MACRO(dirdeep)		\
	MACRO(dnotify)		\
	MACRO(dup)		\
	MACRO(dynlib)		\
	MACRO(efivar)		\
	MACRO(enosys)		\
	MACRO(epoll)		\
	MACRO(eventfd) 		\
	MACRO(exec)		\
	MACRO(fallocate)	\
	MACRO(fanotify)		\
	MACRO(fault)		\
	MACRO(fcntl)		\
	MACRO(fiemap)		\
	MACRO(fifo)		\
	MACRO(file_ioctl)	\
	MACRO(filename)		\
	MACRO(flock)		\
	MACRO(fork)		\
	MACRO(fp_error)		\
	MACRO(fstat)		\
	MACRO(full)		\
	MACRO(funccall)		\
	MACRO(funcret)		\
	MACRO(futex)		\
	MACRO(get)		\
	MACRO(getdent)		\
	MACRO(getrandom)	\
	MACRO(handle)		\
	MACRO(hdd)		\
	MACRO(heapsort)		\
	MACRO(hrtimers)		\
	MACRO(hsearch)		\
	MACRO(icache)		\
	MACRO(icmp_flood)	\
	MACRO(idle_page)	\
	MACRO(inode_flags)	\
	MACRO(inotify)		\
	MACRO(io)		\
	MACRO(iomix)		\
	MACRO(ioport)		\
	MACRO(ioprio)		\
	MACRO(ipsec_mb)		\
	MACRO(itimer)		\
	MACRO(kcmp)		\
	MACRO(key)		\
	MACRO(kill)		\
	MACRO(klog)		\
	MACRO(lease)		\
	MACRO(link)		\
	MACRO(locka)		\
	MACRO(lockbus)		\
	MACRO(lockf)		\
	MACRO(lockofd)		\
	MACRO(longjmp)		\
	MACRO(loop)		\
	MACRO(lsearch)		\
	MACRO(madvise)		\
	MACRO(malloc)		\
	MACRO(matrix)		\
	MACRO(matrix_3d)	\
	MACRO(mcontend)		\
	MACRO(membarrier)	\
	MACRO(memcpy)		\
	MACRO(memfd)		\
	MACRO(memrate)		\
	MACRO(memthrash)	\
	MACRO(mergesort)	\
	MACRO(mincore)		\
	MACRO(mknod)		\
	MACRO(mlock)		\
	MACRO(mlockmany)	\
	MACRO(mmap)		\
	MACRO(mmapaddr)		\
	MACRO(mmapfixed)	\
	MACRO(mmapfork)		\
	MACRO(mmapmany)		\
	MACRO(mq)		\
	MACRO(mremap)		\
	MACRO(msg)		\
	MACRO(msync)		\
	MACRO(netdev)		\
	MACRO(netlink_proc)	\
	MACRO(netlink_task)	\
	MACRO(nice)		\
	MACRO(nop)		\
	MACRO(null)		\
	MACRO(numa)		\
	MACRO(oom_pipe)		\
	MACRO(opcode)		\
	MACRO(open)		\
	MACRO(personality)	\
	MACRO(physpage)		\
	MACRO(pidfd)		\
	MACRO(pipe)		\
	MACRO(pkey)		\
	MACRO(poll)		\
	MACRO(prctl)		\
	MACRO(procfs)		\
	MACRO(pthread)		\
	MACRO(ptrace)		\
	MACRO(pty)		\
	MACRO(qsort)		\
	MACRO(quota)		\
	MACRO(radixsort)	\
	MACRO(rawdev)		\
	MACRO(rawsock)		\
	MACRO(rdrand)		\
	MACRO(readahead)	\
	MACRO(remap)		\
	MACRO(rename)		\
	MACRO(resources)	\
	MACRO(revio)		\
	MACRO(rlimit)		\
	MACRO(rmap)		\
	MACRO(rtc)		\
	MACRO(schedpolicy)	\
	MACRO(sctp)		\
	MACRO(seal)		\
	MACRO(seccomp)		\
	MACRO(seek)		\
	MACRO(sem)		\
	MACRO(sem_sysv)		\
	MACRO(sendfile)		\
	MACRO(set)		\
	MACRO(shellsort)	\
	MACRO(shm)		\
	MACRO(shm_sysv)		\
	MACRO(sigfd)		\
	MACRO(sigfpe)		\
	MACRO(sigio)		\
	MACRO(sigpending)	\
	MACRO(sigpipe)		\
	MACRO(sigq)		\
	MACRO(sigrt)		\
	MACRO(sigsegv)		\
	MACRO(sigsuspend)	\
	MACRO(sleep)		\
	MACRO(sock)		\
	MACRO(sockdiag)		\
	MACRO(sockfd)		\
	MACRO(sockpair)		\
	MACRO(softlockup)	\
	MACRO(spawn)		\
	MACRO(splice)		\
	MACRO(stack)		\
	MACRO(stackmmap)	\
	MACRO(str)		\
	MACRO(stream)		\
	MACRO(swap)		\
	MACRO(switch)		\
	MACRO(symlink)		\
	MACRO(sync_file)	\
	MACRO(sysbadaddr)	\
	MACRO(sysinfo)		\
	MACRO(sysfs)		\
	MACRO(tee)		\
	MACRO(timer)		\
	MACRO(timerfd)		\
	MACRO(tlb_shootdown)	\
	MACRO(tmpfs)		\
	MACRO(tree)		\
	MACRO(tsc)		\
	MACRO(tsearch)		\
	MACRO(udp)		\
	MACRO(udp_flood)	\
	MACRO(unshare)		\
	MACRO(urandom)		\
	MACRO(userfaultfd)	\
	MACRO(utime)		\
	MACRO(vdso)		\
	MACRO(vecmath)		\
	MACRO(vfork)		\
	MACRO(vforkmany)	\
	MACRO(vm)		\
	MACRO(vm_addr)		\
	MACRO(vm_rw)		\
	MACRO(vm_segv)		\
	MACRO(vm_splice)	\
	MACRO(wait)		\
	MACRO(watchdog)		\
	MACRO(wcs)		\
	MACRO(xattr)		\
	MACRO(yield)		\
	MACRO(zero)		\
	MACRO(zlib)		\
	MACRO(zombie)

/*
 *  Declaration of stress_*_info object
 */
#define STRESSOR_ENUM(name)	\
	STRESS_ ## name,

/* Stress tests */
typedef enum {
	STRESS_START = -1,
	STRESSORS(STRESSOR_ENUM)
	/* STRESS_MAX must be last one */
	STRESS_MAX
} stress_id_t;

/* Command line long options */
typedef enum {
	/* Short options */
	OPT_query = '?',
	OPT_all = 'a',
	OPT_backoff = 'b',
	OPT_bigheap = 'B',
	OPT_cpu = 'c',
	OPT_cache = 'C',
	OPT_hdd = 'd',
	OPT_dentry = 'D',
	OPT_fork = 'f',
	OPT_fallocate = 'F',
	OPT_io = 'i',
	OPT_job = 'j',
	OPT_help = 'h',
	OPT_keep_name = 'k',
	OPT_cpu_load = 'l',
	OPT_vm = 'm',
	OPT_metrics = 'M',
	OPT_dry_run = 'n',
	OPT_rename = 'R',
	OPT_open = 'o',
	OPT_pipe = 'p',
	OPT_poll = 'P',
	OPT_quiet = 'q',
	OPT_random = 'r',
	OPT_switch = 's',
	OPT_sock = 'S',
	OPT_timeout = 't',
	OPT_timer = 'T',
	OPT_urandom = 'u',
	OPT_verbose = 'v',
	OPT_version = 'V',
	OPT_yield = 'y',
	OPT_yaml = 'Y',
	OPT_exclude = 'x',

	/* Long options only */

	OPT_long_ops_start = 0x7f,

	OPT_abort,

	OPT_access,
	OPT_access_ops,

	OPT_affinity,
	OPT_affinity_ops,
	OPT_affinity_rand,

	OPT_af_alg,
	OPT_af_alg_ops,

	OPT_aggressive,

	OPT_aio,
	OPT_aio_ops,
	OPT_aio_requests,

	OPT_aiol,
	OPT_aiol_ops,
	OPT_aiol_requests,

	OPT_apparmor,
	OPT_apparmor_ops,

	OPT_atomic,
	OPT_atomic_ops,

	OPT_bad_altstack,
	OPT_bad_altstack_ops,

	OPT_branch,
	OPT_branch_ops,

	OPT_brk,
	OPT_brk_ops,
	OPT_brk_notouch,

	OPT_bsearch,
	OPT_bsearch_ops,
	OPT_bsearch_size,

	OPT_bigheap_ops,
	OPT_bigheap_growth,

	OPT_bind_mount,
	OPT_bind_mount_ops,

	OPT_class,
	OPT_cache_ops,
	OPT_cache_prefetch,
	OPT_cache_flush,
	OPT_cache_fence,
	OPT_cache_level,
	OPT_cache_ways,
	OPT_cache_no_affinity,

	OPT_cap,
	OPT_cap_ops,

	OPT_chdir,
	OPT_chdir_dirs,
	OPT_chdir_ops,

	OPT_chmod,
	OPT_chmod_ops,

	OPT_chown,
	OPT_chown_ops,

	OPT_chroot,
	OPT_chroot_ops,

	OPT_clock,
	OPT_clock_ops,

	OPT_clone,
	OPT_clone_ops,
	OPT_clone_max,

	OPT_close,
	OPT_close_ops,

	OPT_context,
	OPT_context_ops,

	OPT_copy_file,
	OPT_copy_file_ops,
	OPT_copy_file_bytes,

	OPT_cpu_ops,
	OPT_cpu_method,
	OPT_cpu_load_slice,

	OPT_cpu_online,
	OPT_cpu_online_ops,
	OPT_cpu_online_all,

	OPT_crypt,
	OPT_crypt_ops,

	OPT_cyclic,
	OPT_cyclic_ops,
	OPT_cyclic_method,
	OPT_cyclic_policy,
	OPT_cyclic_prio,
	OPT_cyclic_sleep,
	OPT_cyclic_dist,

	OPT_daemon,
	OPT_daemon_ops,

	OPT_dccp,
	OPT_dccp_domain,
	OPT_dccp_ops,
	OPT_dccp_opts,
	OPT_dccp_port,

	OPT_dentry_ops,
	OPT_dentries,
	OPT_dentry_order,

	OPT_dev,
	OPT_dev_ops,

	OPT_dev_shm,
	OPT_dev_shm_ops,

	OPT_dir,
	OPT_dir_ops,
	OPT_dir_dirs,

	OPT_dirdeep,
	OPT_dirdeep_ops,
	OPT_dirdeep_dirs,
	OPT_dirdeep_inodes,

	OPT_dnotify,
	OPT_dnotify_ops,

	OPT_dup,
	OPT_dup_ops,

	OPT_dynlib,
	OPT_dynlib_ops,

	OPT_efivar,
	OPT_efivar_ops,

	OPT_enosys,
	OPT_enosys_ops,

	OPT_epoll,
	OPT_epoll_ops,
	OPT_epoll_port,
	OPT_epoll_domain,

	OPT_eventfd,
	OPT_eventfd_ops,

	OPT_exec,
	OPT_exec_ops,
	OPT_exec_max,

	OPT_fallocate_ops,
	OPT_fallocate_bytes,

	OPT_fanotify,
	OPT_fanotify_ops,

	OPT_fault,
	OPT_fault_ops,

	OPT_fcntl,
	OPT_fcntl_ops,

	OPT_fiemap,
	OPT_fiemap_ops,
	OPT_fiemap_bytes,

	OPT_fifo,
	OPT_fifo_ops,
	OPT_fifo_readers,

	OPT_file_ioctl,
	OPT_file_ioctl_ops,

	OPT_filename,
	OPT_filename_ops,
	OPT_filename_opts,

	OPT_flock,
	OPT_flock_ops,

	OPT_fork_ops,
	OPT_fork_max,

	OPT_fp_error,
	OPT_fp_error_ops,

	OPT_fstat,
	OPT_fstat_ops,
	OPT_fstat_dir,

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

	OPT_getrandom,
	OPT_getrandom_ops,

	OPT_getdent,
	OPT_getdent_ops,

	OPT_handle,
	OPT_handle_ops,

	OPT_hdd_bytes,
	OPT_hdd_write_size,
	OPT_hdd_ops,
	OPT_hdd_opts,

	OPT_heapsort,
	OPT_heapsort_ops,
	OPT_heapsort_integers,

	OPT_hrtimers,
	OPT_hrtimers_ops,

	OPT_hsearch,
	OPT_hsearch_ops,
	OPT_hsearch_size,

	OPT_icache,
	OPT_icache_ops,

	OPT_icmp_flood,
	OPT_icmp_flood_ops,

	OPT_idle_page,
	OPT_idle_page_ops,

	OPT_ignite_cpu,

	OPT_inode_flags,
	OPT_inode_flags_ops,

	OPT_inotify,
	OPT_inotify_ops,

	OPT_iomix,
	OPT_iomix_bytes,
	OPT_iomix_ops,

	OPT_ioport,
	OPT_ioport_ops,
	OPT_ioport_opts,

	OPT_ionice_class,
	OPT_ionice_level,

	OPT_ioprio,
	OPT_ioprio_ops,

	OPT_io_ops,

	OPT_ipsec_mb,
	OPT_ipsec_mb_ops,

	OPT_itimer,
	OPT_itimer_ops,
	OPT_itimer_freq,
	OPT_itimer_rand,

	OPT_kcmp,
	OPT_kcmp_ops,

	OPT_key,
	OPT_key_ops,

	OPT_kill,
	OPT_kill_ops,

	OPT_klog,
	OPT_klog_ops,

	OPT_lease,
	OPT_lease_ops,
	OPT_lease_breakers,

	OPT_link,
	OPT_link_ops,

	OPT_lockbus,
	OPT_lockbus_ops,

	OPT_locka,
	OPT_locka_ops,

	OPT_lockf,
	OPT_lockf_ops,
	OPT_lockf_nonblock,

	OPT_lockofd,
	OPT_lockofd_ops,

	OPT_log_brief,
	OPT_log_file,

	OPT_longjmp,
	OPT_longjmp_ops,

	OPT_loop,
	OPT_loop_ops,

	OPT_lsearch,
	OPT_lsearch_ops,
	OPT_lsearch_size,

	OPT_madvise,
	OPT_madvise_ops,

	OPT_malloc,
	OPT_malloc_ops,
	OPT_malloc_bytes,
	OPT_malloc_max,
	OPT_malloc_threshold,

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

	OPT_mcontend,
	OPT_mcontend_ops,

	OPT_membarrier,
	OPT_membarrier_ops,

	OPT_memcpy,
	OPT_memcpy_ops,
	OPT_memcpy_method,

	OPT_memfd,
	OPT_memfd_ops,
	OPT_memfd_bytes,
	OPT_memfd_fds,

	OPT_memrate,
	OPT_memrate_ops,
	OPT_memrate_rd_mbs,
	OPT_memrate_wr_mbs,
	OPT_memrate_bytes,

	OPT_memthrash,
	OPT_memthrash_ops,
	OPT_memthrash_method,

	OPT_mergesort,
	OPT_mergesort_ops,
	OPT_mergesort_integers,

	OPT_metrics_brief,

	OPT_mincore,
	OPT_mincore_ops,
	OPT_mincore_rand,

	OPT_mknod,
	OPT_mknod_ops,

	OPT_minimize,

	OPT_mlock,
	OPT_mlock_ops,

	OPT_mlockmany,
	OPT_mlockmany_ops,

	OPT_mmap,
	OPT_mmap_ops,
	OPT_mmap_bytes,
	OPT_mmap_file,
	OPT_mmap_async,
	OPT_mmap_mprotect,

	OPT_mmapaddr,
	OPT_mmapaddr_ops,

	OPT_mmapfixed,
	OPT_mmapfixed_ops,

	OPT_mmapfork,
	OPT_mmapfork_ops,

	OPT_mmapmany,
	OPT_mmapmany_ops,

	OPT_mq,
	OPT_mq_ops,
	OPT_mq_size,

	OPT_mremap,
	OPT_mremap_ops,
	OPT_mremap_bytes,

	OPT_msg,
	OPT_msg_ops,
	OPT_msg_types,

	OPT_msync,
	OPT_msync_bytes,
	OPT_msync_ops,

	OPT_netdev,
	OPT_netdev_ops,

	OPT_netlink_proc,
	OPT_netlink_proc_ops,

	OPT_netlink_task,
	OPT_netlink_task_ops,

	OPT_nice,
	OPT_nice_ops,

	OPT_no_madvise,
	OPT_no_rand_seed,

	OPT_nop,
	OPT_nop_ops,

	OPT_null,
	OPT_null_ops,

	OPT_numa,
	OPT_numa_ops,

	OPT_oomable,

	OPT_physpage,
	OPT_physpage_ops,

	OPT_oom_pipe,
	OPT_oom_pipe_ops,

	OPT_opcode,
	OPT_opcode_ops,
	OPT_opcode_method,

	OPT_open_ops,

	OPT_page_in,
	OPT_pathological,

	OPT_perf_stats,

	OPT_personality,
	OPT_personality_ops,

	OPT_pidfd,
	OPT_pidfd_ops,

	OPT_pipe_ops,
	OPT_pipe_size,
	OPT_pipe_data_size,

	OPT_pkey,
	OPT_pkey_ops,

	OPT_poll_ops,

	OPT_prctl,
	OPT_prctl_ops,

	OPT_procfs,
	OPT_procfs_ops,

	OPT_pthread,
	OPT_pthread_ops,
	OPT_pthread_max,

	OPT_ptrace,
	OPT_ptrace_ops,

	OPT_pty,
	OPT_pty_ops,
	OPT_pty_max,

	OPT_qsort,
	OPT_qsort_ops,
	OPT_qsort_integers,

	OPT_quota,
	OPT_quota_ops,

	OPT_radixsort,
	OPT_radixsort_ops,
	OPT_radixsort_size,

	OPT_rawdev,
	OPT_rawdev_method,
	OPT_rawdev_ops,

	OPT_rawsock,
	OPT_rawsock_ops,

	OPT_rdrand,
	OPT_rdrand_ops,

	OPT_readahead,
	OPT_readahead_ops,
	OPT_readahead_bytes,

	OPT_remap,
	OPT_remap_ops,

	OPT_rename_ops,

	OPT_resources,
	OPT_resources_ops,

	OPT_revio,
	OPT_revio_ops,
	OPT_revio_opts,
	OPT_revio_bytes,

	OPT_rlimit,
	OPT_rlimit_ops,

	OPT_rmap,
	OPT_rmap_ops,

	OPT_rtc,
	OPT_rtc_ops,

	OPT_sched,
	OPT_sched_prio,

	OPT_schedpolicy,
	OPT_schedpolicy_ops,

	OPT_sctp,
	OPT_sctp_ops,
	OPT_sctp_domain,
	OPT_sctp_port,

	OPT_seal,
	OPT_seal_ops,

	OPT_seccomp,
	OPT_seccomp_ops,

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

	OPT_sem_sysv,
	OPT_sem_sysv_ops,
	OPT_sem_sysv_procs,

	OPT_set,
	OPT_set_ops,

	OPT_shellsort,
	OPT_shellsort_ops,
	OPT_shellsort_integers,

	OPT_shm,
	OPT_shm_ops,
	OPT_shm_bytes,
	OPT_shm_objects,

	OPT_shm_sysv,
	OPT_shm_sysv_ops,
	OPT_shm_sysv_bytes,
	OPT_shm_sysv_segments,

	OPT_sequential,

	OPT_sigfd,
	OPT_sigfd_ops,

	OPT_sigfpe,
	OPT_sigfpe_ops,

	OPT_sigio,
	OPT_sigio_ops,

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

	OPT_sleep,
	OPT_sleep_ops,
	OPT_sleep_max,

	OPT_sock_ops,
	OPT_sock_domain,
	OPT_sock_nodelay,
	OPT_sock_opts,
	OPT_sock_port,
	OPT_sock_type,

	OPT_sockdiag,
	OPT_sockdiag_ops,

	OPT_sockfd,
	OPT_sockfd_ops,
	OPT_sockfd_port,

	OPT_sockpair,
	OPT_sockpair_ops,

	OPT_softlockup,
	OPT_softlockup_ops,

	OPT_swap,
	OPT_swap_ops,

	OPT_switch_ops,
	OPT_switch_freq,

	OPT_spawn,
	OPT_spawn_ops,

	OPT_splice,
	OPT_splice_ops,
	OPT_splice_bytes,

	OPT_stack,
	OPT_stack_ops,
	OPT_stack_fill,

	OPT_stackmmap,
	OPT_stackmmap_ops,

	OPT_str,
	OPT_str_ops,
	OPT_str_method,

	OPT_stream,
	OPT_stream_ops,
	OPT_stream_index,
	OPT_stream_l3_size,
	OPT_stream_madvise,

	OPT_stressors,

	OPT_symlink,
	OPT_symlink_ops,

	OPT_sync_file,
	OPT_sync_file_ops,
	OPT_sync_file_bytes,

	OPT_sysbadaddr,
	OPT_sysbadaddr_ops,

	OPT_sysinfo,
	OPT_sysinfo_ops,

	OPT_sysfs,
	OPT_sysfs_ops,

	OPT_syslog,

	OPT_tee,
	OPT_tee_ops,

	OPT_taskset,

	OPT_temp_path,

	OPT_thermal_zones,

	OPT_thrash,

	OPT_timer_slack,

	OPT_timer_ops,
	OPT_timer_freq,
	OPT_timer_rand,

	OPT_timerfd,
	OPT_timerfd_ops,
	OPT_timerfd_freq,
	OPT_timerfd_rand,

	OPT_times,

	OPT_timestamp,

	OPT_tlb_shootdown,
	OPT_tlb_shootdown_ops,

	OPT_tmpfs,
	OPT_tmpfs_ops,
	OPT_tmpfs_mmap_async,
	OPT_tmpfs_mmap_file,

	OPT_tree,
	OPT_tree_ops,
	OPT_tree_method,
	OPT_tree_size,

	OPT_tsc,
	OPT_tsc_ops,

	OPT_tsearch,
	OPT_tsearch_ops,
	OPT_tsearch_size,

	OPT_udp,
	OPT_udp_ops,
	OPT_udp_port,
	OPT_udp_domain,
	OPT_udp_lite,

	OPT_udp_flood,
	OPT_udp_flood_ops,
	OPT_udp_flood_domain,

	OPT_unshare,
	OPT_unshare_ops,

	OPT_urandom_ops,

	OPT_userfaultfd,
	OPT_userfaultfd_ops,
	OPT_userfaultfd_bytes,

	OPT_utime,
	OPT_utime_ops,
	OPT_utime_fsync,

	OPT_vdso,
	OPT_vdso_ops,
	OPT_vdso_func,

	OPT_vecmath,
	OPT_vecmath_ops,

	OPT_verify,

	OPT_vfork,
	OPT_vfork_ops,
	OPT_vfork_max,

	OPT_vforkmany,
	OPT_vforkmany_ops,

	OPT_vm_bytes,
	OPT_vm_hang,
	OPT_vm_keep,
	OPT_vm_mmap_populate,
	OPT_vm_mmap_locked,
	OPT_vm_ops,
	OPT_vm_madvise,
	OPT_vm_method,

	OPT_vm_addr,
	OPT_vm_addr_method,
	OPT_vm_addr_ops,

	OPT_vm_rw,
	OPT_vm_rw_ops,
	OPT_vm_rw_bytes,

	OPT_vm_segv,
	OPT_vm_segv_ops,

	OPT_vm_splice,
	OPT_vm_splice_ops,
	OPT_vm_splice_bytes,

	OPT_wait,
	OPT_wait_ops,

	OPT_watchdog,
	OPT_watchdog_ops,

	OPT_wcs,
	OPT_wcs_ops,
	OPT_wcs_method,

	OPT_xattr,
	OPT_xattr_ops,

	OPT_yield_ops,

	OPT_zero,
	OPT_zero_ops,

	OPT_zlib,
	OPT_zlib_ops,
	OPT_zlib_level,
	OPT_zlib_method,

	OPT_zombie,
	OPT_zombie_ops,
	OPT_zombie_max,
} stress_op_t;

/* stress test metadata */
typedef struct {
	const stressor_info_t *info;	/* stress test info */
	const stress_id_t id;		/* stress test ID */
	const short int short_getopt;	/* getopt short option */
	const stress_op_t op;		/* ops option */
	const char *name;		/* name of stress test */
} stress_t;

/* Per process information */
typedef struct proc_info {
	struct proc_info *next;		/* next proc info struct in list */
	struct proc_info *prev;		/* prev proc info struct in list */
	const stress_t *stressor;	/* stressor */
	pid_t	*pids;			/* process id */
	proc_stats_t **stats;		/* process proc stats info */
	int32_t started_procs;		/* count of started processes */
	int32_t num_procs;		/* number of process per stressor */
	uint64_t bogo_ops;		/* number of bogo ops */
} proc_info_t;

/* Pointer to current running stressor proc info */
extern proc_info_t *g_proc_current;

/* Scale lookup mapping, suffix -> scale by */
typedef struct {
	const char	ch;		/* Scaling suffix */
	const uint64_t	scale;		/* Amount to scale by */
} scale_t;

/* Cache types */
typedef enum cache_type {
	CACHE_TYPE_UNKNOWN = 0,		/* Unknown type */
	CACHE_TYPE_DATA,		/* D$ */
	CACHE_TYPE_INSTRUCTION,		/* I$ */
	CACHE_TYPE_UNIFIED,		/* D$ + I$ */
} cache_type_t;

/* CPU cache information */
typedef struct cpu_cache {
	uint64_t           size;      	/* cache size in bytes */
	uint32_t           line_size;	/* cache line size in bytes */
	uint32_t           ways;	/* cache ways */
	cache_type_t       type;	/* cache type */
	uint16_t           level;	/* cache level, L1, L2 etc */
} cpu_cache_t;

typedef struct cpu {
	cpu_cache_t   *caches;		/* CPU cache data */
	uint32_t       num;		/* CPU # number */
	uint32_t       cache_count;	/* CPU cache #  */
	bool           online;		/* CPU online when true */
} cpu_t;

typedef struct cpus {
	cpu_t     *cpus;		/* CPU data */
	uint32_t   count;		/* CPU count */
} cpus_t;

/* Various global option settings and flags */
extern const char *g_app_name;		/* Name of application */
extern shared_t *g_shared;		/* shared memory */
extern uint64_t	g_opt_timeout;		/* timeout in seconds */
extern uint64_t	g_opt_flags;		/* option flags */
extern int32_t g_opt_sequential;	/* Number of sequential stressors */
extern int32_t g_opt_parallel;		/* Number of parallel stressors */
extern volatile bool g_keep_stressing_flag; /* false to exit stressor */
extern volatile bool g_caught_sigint;	/* true if stopped by SIGINT */
extern pid_t g_pgrp;			/* proceess group leader */
extern jmp_buf g_error_env;		/* parsing error env */
extern put_val_t g_put_val;		/* sync data to somewhere */

/*
 *  stressor option value handling
 */
extern int set_setting(const char *name, const type_id_t type_id, const void *value);
extern int set_setting_global(const char *name, const type_id_t type_id, const void *value);
extern bool get_setting(const char *name, void *value);
extern void free_settings(void);

/*
 *  externs to force gcc to stash computed values and hence
 *  to stop the optimiser optimising code away to zero. The
 *  *_put funcs are essentially no-op functions.
 */
extern uint64_t stress_uint64_zero(void);

/*
 *  uint8_put()
 *	stash a uint8_t value
 */
static inline void ALWAYS_INLINE uint8_put(const uint8_t a)
{
	g_put_val.uint8_val = a;
}

/*
 *  uint16_put()
 *	stash a uint16_t value
 */
static inline void ALWAYS_INLINE uint16_put(const uint16_t a)
{
	g_put_val.uint16_val = a;
}

/*
 *  uint32_put()
 *	stash a uint32_t value
 */
static inline void ALWAYS_INLINE uint32_put(const uint32_t a)
{
	g_put_val.uint32_val = a;
}

/*
 *  uint64_put()
 *	stash a uint64_t value
 */
static inline void ALWAYS_INLINE uint64_put(const uint64_t a)
{
	g_put_val.uint64_val = a;
}

#if defined(HAVE_INT128_T)
/*
 *  uint128_put()
 *	stash a uint128_t value
 */
static inline void ALWAYS_INLINE uint128_put(const __uint128_t a)
{
	g_put_val.uint128_val = a;
}
#endif

/*
 *  float_put()
 *	stash a float value
 */
static inline void ALWAYS_INLINE float_put(const float a)
{
	g_put_val.float_val = a;
}

/*
 *  double_put()
 *	stash a double value
 */
static inline void ALWAYS_INLINE double_put(const double a)
{
	g_put_val.double_val = a;
}

/*
 *  long_double_put()
 *	stash a double value
 */
static inline void ALWAYS_INLINE long_double_put(const double a)
{
	g_put_val.long_double_val = a;
}


/* Filenames and directories */
extern int stress_temp_filename(char *path, const size_t len,
	const char *name, const pid_t pid, const uint32_t instance,
	const uint64_t magic);
extern int stress_temp_filename_args(const args_t *args, char *path,
	const size_t len, const uint64_t magic);
extern int stress_temp_dir(char *path, const size_t len,
	const char *name, const pid_t pid, const uint32_t instance);
extern int stress_temp_dir_args(const args_t *args, char *path, const size_t len);
extern WARN_UNUSED int stress_temp_dir_mk(const char *name, const pid_t pid,
	const uint32_t instance);
extern WARN_UNUSED int stress_temp_dir_mk_args(const args_t *args);
extern int stress_temp_dir_rm(const char *name, const pid_t pid,
	const uint32_t instance);
extern int stress_temp_dir_rm_args(const args_t *args);
extern void stress_cwd_readwriteable(void);

extern const char *stress_strsignal(const int signum);

#if defined(STRESS_X86)
/*
 *  clflush()
 *	flush a cache line
 */
static inline void ALWAYS_INLINE clflush(volatile void *ptr)
{
        asm volatile("clflush %0" : "+m" (*(volatile char *)ptr));
}
#else
#define clflush(ptr)	do { } while (0) /* No-op */
#endif

/*
 *  mfence()
 *	serializing memory fence
 */
static inline void ALWAYS_INLINE mfence(void)
{
#if NEED_GNUC(4, 2, 0) && !defined(__PCC__)
	__sync_synchronize();
#else
#if defined(STRESS_X86)
	asm volatile("mfence" : : : "memory");
#else
	/* Other arches not yet implemented for older GCC flavours */
#endif
#endif
}

/* Fast random numbers */
extern uint32_t mwc32(void);
extern uint64_t mwc64(void);
extern uint16_t mwc16(void);
extern uint8_t mwc8(void);
extern uint8_t mwc1(void);
extern void mwc_seed(const uint32_t w, const uint32_t z);
extern void mwc_reseed(void);

/* Time handling */

/*
 *  timeval_to_double()
 *      convert timeval to seconds as a double
 */
static inline WARN_UNUSED double timeval_to_double(const struct timeval *tv)
{
        return (double)tv->tv_sec + ((double)tv->tv_usec / 1000000.0);
}

extern double time_now(void);
extern const char *duration_to_str(const double duration);

/* Perf statistics */
#if defined(STRESS_PERF_STATS)
extern int perf_open(stress_perf_t *sp);
extern int perf_enable(stress_perf_t *sp);
extern int perf_disable(stress_perf_t *sp);
extern int perf_close(stress_perf_t *sp);
extern bool perf_stat_succeeded(const stress_perf_t *sp);
extern void perf_stat_dump(FILE *yaml, proc_info_t *procs_head, const double duration);
extern void perf_init(void);
#endif

/* CPU helpers */
extern WARN_UNUSED bool cpu_is_x86(void);

/* Misc settings helpers */
extern void set_oom_adjustment(const char *name, const bool killable);
extern WARN_UNUSED bool process_oomed(const pid_t pid);
extern WARN_UNUSED int stress_set_sched(const pid_t pid, const int32_t sched,
	const int sched_priority, const bool quiet);
extern const char *stress_get_sched_name(const int sched);
extern void set_iopriority(const int32_t class, const int32_t level);
extern void stress_set_proc_name(const char *name);

/* Memory locking */
extern int stress_mlock_region(const void *addr_start, const void *addr_end);

/* Argument parsing and range checking */
extern WARN_UNUSED uint64_t get_uint64(const char *const str);
extern WARN_UNUSED uint64_t get_uint64_scale(const char *const str,
	const scale_t scales[], const char *const msg);
extern WARN_UNUSED uint64_t get_uint64_percent(const char *const str,
	const uint32_t instances, const uint64_t max, const char *const errmsg);
extern WARN_UNUSED uint64_t get_uint64_byte(const char *const str);
extern WARN_UNUSED uint64_t get_uint64_byte_memory(const char *const str,
	const uint32_t instances);
extern WARN_UNUSED uint64_t get_uint64_byte_filesystem(const char *const str,
	const uint32_t instances);
extern WARN_UNUSED uint64_t get_uint64_time(const char *const str);
extern void check_value(const char *const msg, const int val);
extern void check_range(const char *const opt, const uint64_t val,
	const uint64_t lo, const uint64_t hi);
extern void check_range_bytes(const char *const opt, const uint64_t val,
	const uint64_t lo, const uint64_t hi);
extern WARN_UNUSED int stress_set_cpu_affinity(const char *arg);
extern WARN_UNUSED uint32_t get_uint32(const char *const str);
extern WARN_UNUSED int32_t  get_int32(const char *const str);
extern WARN_UNUSED int32_t  get_opt_sched(const char *const str);
extern WARN_UNUSED int32_t  get_opt_ionice_class(const char *const str);

/* Misc helper funcs */
extern void stress_unmap_shared(void);
extern void log_system_mem_info(void);
extern WARN_UNUSED char *stress_munge_underscore(const char *str);
extern size_t stress_get_pagesize(void);
extern WARN_UNUSED int32_t stress_get_processors_online(void);
extern WARN_UNUSED int32_t stress_get_processors_configured(void);
extern WARN_UNUSED int32_t stress_get_ticks_per_second(void);
extern WARN_UNUSED ssize_t stress_get_stack_direction(void);
extern void stress_get_memlimits(size_t *shmall, size_t *freemem, size_t *totalmem, size_t *freeswap);
extern WARN_UNUSED int stress_get_load_avg(double *min1, double *min5, double *min15);
extern void set_max_limits(void);
extern void stress_parent_died_alarm(void);
extern int stress_process_dumpable(const bool dumpable);
extern int stress_set_timer_slack_ns(const char *opt);
extern void stress_set_timer_slack(void);
extern WARN_UNUSED int stress_set_temp_path(const char *path);
extern void stress_strnrnd(char *str, const size_t len);
extern void stress_get_cache_size(uint64_t *l2, uint64_t *l3);
extern WARN_UNUSED unsigned int stress_get_cpu(void);
extern WARN_UNUSED const char *stress_get_compiler(void);
extern WARN_UNUSED const char *stress_get_uname_info(void);
extern WARN_UNUSED int stress_cache_alloc(const char *name);
extern void stress_cache_free(void);
extern void ignite_cpu_start(void);
extern void ignite_cpu_stop(void);
extern int system_write(const char *path, const char *buf, const size_t buf_len);
extern WARN_UNUSED int stress_set_nonblock(const int fd);
extern WARN_UNUSED int system_read(const char *path, char *buf, const size_t buf_len);
extern WARN_UNUSED uint64_t stress_get_prime64(const uint64_t n);
extern WARN_UNUSED size_t stress_get_file_limit(void);
extern WARN_UNUSED int stress_sigaltstack(const void *stack, const size_t size);
extern WARN_UNUSED int stress_sighandler(const char *name, const int signum, void (*handler)(int), struct sigaction *orig_action);
extern void stress_handle_stop_stressing(int dummy);
extern WARN_UNUSED int stress_sig_stop_stressing(const char *name, const int sig);
extern int stress_sigrestore(const char *name, const int signum, struct sigaction *orig_action);
extern WARN_UNUSED int stress_not_implemented(const args_t *args);
extern WARN_UNUSED size_t stress_probe_max_pipe_size(void);
extern WARN_UNUSED void *stress_align_address(const void *addr, const size_t alignment);
extern void mmap_set(uint8_t *buf, const size_t sz, const size_t page_size);
extern WARN_UNUSED int mmap_check(uint8_t *buf, const size_t sz, const size_t page_size);
extern WARN_UNUSED uint64_t stress_get_phys_mem_size(void);
extern WARN_UNUSED uint64_t stress_get_filesystem_size(void);
extern WARN_UNUSED uint64_t stress_get_filesystem_available_inodes(void);
extern char *stress_uint64_to_str(char *str, size_t len, const uint64_t val);
extern WARN_UNUSED int stress_drop_capabilities(const char *name);
extern WARN_UNUSED bool stress_is_dot_filename(const char *name);
extern WARN_UNUSED char *stress_const_optdup(const char *opt);
extern size_t stress_text_addr(char **start, char **end);
extern WARN_UNUSED bool stress_check_capability(const int capability);
extern WARN_UNUSED bool stress_sigalrm_pending(void);
extern void stress_sigalrm_block(void);

/*
 *  Indicate a stress test failed because of limited resources
 *  rather than a failure of the tests during execution.
 *  err is the errno of the failure.
 */
static inline WARN_UNUSED ALWAYS_INLINE int exit_status(const int err)
{
	switch (err) {
	case ENOMEM:
	case ENOSPC:
		return EXIT_NO_RESOURCE;
	case ENOSYS:
		return EXIT_NOT_IMPLEMENTED;
	}
	return EXIT_FAILURE;
}

/*
 *  Stack aligning for clone() system calls
 *	align to nearest 16 bytes for aarch64 et al,
 *	assumes we have enough slop to do this
 */
static inline WARN_UNUSED ALWAYS_INLINE void *align_stack(void *stack_top)
{
	return (void *)((uintptr_t)stack_top & ~(uintptr_t)0xf);
}

/*
 *  Check if flag is set, and set flag
 */
static inline WARN_UNUSED uint32_t warn_once(const uint32_t flag)
{
	uint32_t tmp;
#if defined(HAVE_LIB_PTHREAD)
	int ret;

	ret = shim_pthread_spin_lock(&g_shared->warn_once.lock);
#endif
	tmp = !(g_shared->warn_once.flags & flag);
	g_shared->warn_once.flags |= flag;
#if defined(HAVE_LIB_PTHREAD)
	if (!ret)
		shim_pthread_spin_unlock(&g_shared->warn_once.lock);
#endif
	return tmp;
}

/* Jobfile parsing */
extern WARN_UNUSED int parse_jobfile(int argc, char **argv, const char *jobfile);
extern WARN_UNUSED int parse_opts(int argc, char **argv, const bool jobmode);

/* Memory tweaking */
extern int madvise_random(void *addr, const size_t length);
extern int mincore_touch_pages(void *buf, const size_t buf_len);
extern int mincore_touch_pages_interruptible(void *buf, const size_t buf_len);

/* Mounts */
extern void mount_free(char *mnts[], const int n);
extern WARN_UNUSED int mount_get(char *mnts[], const int max);

/* Thermal Zones */
#if defined(STRESS_THERMAL_ZONES)
extern int tz_init(tz_info_t **tz_info_list);
extern void tz_free(tz_info_t **tz_info_list);
extern int tz_get_temperatures(tz_info_t **tz_info_list, stress_tz_t *tz);
extern void tz_dump(FILE *yaml, proc_info_t *procs_head);
#endif

/* Network helpers */

#define NET_ADDR_ANY		(0)
#define NET_ADDR_LOOPBACK	(1)

extern void stress_set_net_port(const char *optname, const char *opt,
	const int min_port, const int max_port, int *port);
extern WARN_UNUSED int stress_set_net_domain(const int domain_mask, const char *name, const char *domain_name, int *domain);
extern void stress_set_sockaddr(const char *name, const uint32_t instance,
	const pid_t ppid, const int domain, const int port,
	struct sockaddr **sockaddr, socklen_t *len, const int net_addr);
extern void stress_set_sockaddr_port(const int domain, const int port, struct sockaddr *sockaddr);

/* CPU caches */
extern cpus_t *get_all_cpu_cache_details(void);
extern uint16_t get_max_cache_level(const cpus_t *cpus);
extern cpu_cache_t *get_cpu_cache(const cpus_t *cpus, const uint16_t cache_level);
extern void free_cpu_caches(cpus_t *cpus);

/* CPU thrashing start/stop helpers */
extern int  thrash_start(void);
extern void thrash_stop(void);

/* Used to set options for specific stressors */
extern void stress_adjust_pthread_max(const uint64_t max);
extern void stress_adjust_sleep_max(const uint64_t max);

/* loff_t and off64_t porting shims */
#if defined(HAVE_LOFF_T)
typedef	loff_t		shim_loff_t;
#else
typedef uint64_t	shim_loff_t;	/* Assume 64 bit */
#endif

#if defined(HAVE_OFF64_T)
typedef off64_t		shim_off64_t;
#else
typedef uint64_t	shim_off64_t;
#endif

/*
 *  shim'd abstracted system or library calls
 *  that have a layer of OS abstraction
 */
struct shim_linux_dirent {
	unsigned long	d_ino;		/* Inode number */
	unsigned long	d_off;		/* Offset to next linux_dirent */
	unsigned short	d_reclen;	/* Length of this linux_dirent */
	char		d_name[];	/* Filename (null-terminated) */
};

/* dirent64 porting shim */
struct shim_linux_dirent64 {
#if defined(HAVE_INO64_T)
	ino64_t		d_ino;		/* 64-bit inode number */
#else
	int64_t		d_ino;		/* 64-bit inode number */
#endif
	shim_off64_t	d_off;		/* 64-bit offset to next structure */
	unsigned short	d_reclen;	/* Size of this dirent */
	unsigned char	d_type;		/* File type */
	char		d_name[];	/* Filename (null-terminated) */
};

/* sched_getattr attributes */
struct shim_sched_attr {
	uint32_t size;			/* size of struct */
	uint32_t sched_policy;		/* policy, SCHED_* */
	uint64_t sched_flags;		/* scheduling flags */
	int32_t  sched_nice;		/* nice value SCHED_OTHER, SCHED_BATCH */
	uint32_t sched_priority;	/* priority SCHED_FIFO, SCHED_RR */
	uint64_t sched_runtime;		/* runtime SCHED_DEADLINE, ns */
	uint64_t sched_deadline;	/* deadline time, ns */
	uint64_t sched_period;		/* period, ns */
};

/* shim'd STATX flags */
#define SHIM_STATX_TYPE              0x00000001U
#define SHIM_STATX_MODE              0x00000002U
#define SHIM_STATX_NLINK             0x00000004U
#define SHIM_STATX_UID               0x00000008U
#define SHIM_STATX_GID               0x00000010U
#define SHIM_STATX_ATIME             0x00000020U
#define SHIM_STATX_MTIME             0x00000040U
#define SHIM_STATX_CTIME             0x00000080U
#define SHIM_STATX_INO               0x00000100U
#define SHIM_STATX_SIZE              0x00000200U
#define SHIM_STATX_BLOCKS            0x00000400U
#define SHIM_STATX_BASIC_STATS       0x000007ffU
#define SHIM_STATX_BTIME             0x00000800U
#define SHIM_STATX_ALL               0x00000fffU

struct shim_statx_timestamp {
        int64_t		tv_sec;
        int32_t		tv_nsec;
        int32_t		__reserved;
};

/* shim'd statx */
struct shim_statx {
        uint32_t   stx_mask;
        uint32_t   stx_blksize;
        uint64_t   stx_attributes;
        uint32_t   stx_nlink;
        uint32_t   stx_uid;
        uint32_t   stx_gid;
        uint16_t   stx_mode;
        uint16_t   __spare0[1];
        uint64_t   stx_ino;
        uint64_t   stx_size;
        uint64_t   stx_blocks;
        uint64_t   __spare1[1];
        struct shim_statx_timestamp  stx_atime;
        struct shim_statx_timestamp  stx_btime;
        struct shim_statx_timestamp  stx_ctime;
        struct shim_statx_timestamp  stx_mtime;
        uint32_t   stx_rdev_major;
        uint32_t   stx_rdev_minor;
        uint32_t   stx_dev_major;
        uint32_t   stx_dev_minor;
        uint64_t   __spare2[14];
};

extern int shim_brk(void *addr);
extern int shim_cacheflush(char *addr, int nbytes, int cache) ;
extern void shim_clear_cache(char* begin, char *end);
extern ssize_t shim_copy_file_range(int fd_in, shim_loff_t *off_in,
        int fd_out, shim_loff_t *off_out, size_t len, unsigned int flags);
extern int shim_dup3(int oldfd, int newfd, int flags);
extern int shim_execveat(int dir_fd, const char *pathname, char *const argv[],
	char *const envp[], int flags);
extern int shim_fallocate(int fd, int mode, off_t offset, off_t len);
extern int shim_fdatasync(int fd);
extern long shim_getcpu(unsigned *cpu, unsigned *node, void *tcache);
extern int shim_getdents(unsigned int fd, struct shim_linux_dirent *dirp,
	unsigned int count);
extern int shim_getdents64(unsigned int fd, struct shim_linux_dirent64 *dirp,
	unsigned int count);
extern char *shim_getlogin(void);
extern int shim_get_mempolicy(int *mode, unsigned long *nodemask,
	unsigned long maxnode, unsigned long addr, unsigned long flags);
extern int shim_getrandom(void *buff, size_t buflen, unsigned int flags);
extern int shim_gettid(void);
extern ssize_t shim_getxattr(const char *path, const char *name,
	void *value, size_t size);
extern int shim_futex_wait(const void *futex, const int val,
	const struct timespec *timeout);
extern int shim_futex_wake(const void *futex, const int n);
extern int shim_fsync(int fd);
extern int shim_ioprio_set(int which, int who, int ioprio);
extern int shim_ioprio_get(int which, int who);
extern long shim_kcmp(pid_t pid1, pid_t pid2, int type, unsigned long idx1,
	unsigned long idx2);
extern int shim_madvise(void *addr, size_t length, int advice);
extern long shim_mbind(void *addr, unsigned long len,
	int mode, const unsigned long *nodemask,
	unsigned long maxnode, unsigned flags);
extern int shim_membarrier(int cmd, int flags);
extern int shim_memfd_create(const char *name, unsigned int flags);
extern long shim_migrate_pages(int pid, unsigned long maxnode,
	const unsigned long *old_nodes, const unsigned long *new_nodes);
extern int shim_mincore(void *addr, size_t length, unsigned char *vec);
extern int shim_mlock(const void *addr, size_t len);
extern int shim_mlock2(const void *addr, size_t len, int flags);
extern int shim_mlockall(int flags);
extern long shim_move_pages(int pid, unsigned long count,
	void **pages, const int *nodes, int *status, int flags);
extern int shim_msync(void *addr, size_t length, int flags);
extern int shim_munlock(const void *addr, size_t len);
extern int shim_munlockall(void);
extern int shim_nanosleep_uint64(uint64_t usec);
extern int shim_pidfd_send_signal(int pidfd, int sig, siginfo_t *info, unsigned int flags);
extern int shim_pkey_alloc(unsigned long flags, unsigned long access_rights);
extern int shim_pkey_free(int pkey);
extern int shim_pkey_mprotect(void *addr, size_t len, int prot, int pkey);
extern int shim_pkey_get(int pkey);
extern int shim_pkey_set(int pkey, unsigned int rights);
extern void *shim_sbrk(intptr_t increment);
extern int shim_sched_getattr(pid_t pid, struct shim_sched_attr *attr,
	unsigned int size, unsigned int flags);
extern int shim_sched_setattr(pid_t pid, struct shim_sched_attr *attr,
	unsigned int flags);
extern int shim_sched_yield(void);
extern int shim_set_mempolicy(int mode, unsigned long *nodemask,
	unsigned long maxnode);
extern int shim_seccomp(unsigned int operation, unsigned int flags, void *args);
extern ssize_t shim_statx(int dfd, const char *filename, unsigned int flags,
	unsigned int mask, struct shim_statx *buffer);
extern size_t shim_strlcat(char *dst, const char *src, size_t len);
extern size_t shim_strlcpy(char *dst, const char *src, size_t len);
extern int shim_sync_file_range(int fd, shim_off64_t offset,
	shim_off64_t nbytes, unsigned int flags);
extern int shim_sysfs(int option, ...);
extern int shim_syslog(int type, char *bufp, int len);
extern int shim_unshare(int flags);
extern int shim_userfaultfd(int flags);
extern int shim_usleep(uint64_t usec);
extern int shim_usleep_interruptible(uint64_t usec);
extern pid_t shim_waitpid(pid_t pid, int *wstatus, int options);

#endif
