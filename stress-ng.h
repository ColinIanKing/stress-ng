/*
 * Copyright (C) 2013-2021 Canonical, Ltd.
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
#ifndef STRESS_NG_H
#define STRESS_NG_H

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
#if defined(__sun) &&	\
    !defined(__sun__)
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

#include "config.h"

#ifndef SCHED_FLAG_RESET_ON_FORK
#define SCHED_FLAG_RESET_ON_FORK	(0x01)
#endif

/*
 * SCHED_DEADLINE
 */
#ifndef SCHED_FLAG_RECLAIM
#define SCHED_FLAG_RECLAIM  		(0x02)
#endif

#ifndef SCHED_FLAG_DL_OVERRUN
#define SCHED_FLAG_DL_OVERRUN   	(0x04)
#endif

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

#if defined(HAVE_JUDY_H)
#include <Judy.h>
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

#if defined(HAVE_LIBKMOD_H)
#include <libkmod.h>
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

#if defined(HAVE_SOUND_ASOUND_H)
#include <sound/asound.h>
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

#if defined(HAVE_USTAT_H)
#if defined(__sun__)
/* ustat and long file support on sun does not build */
#undef HAVE_USTAT_H
#else
#include <ustat.h>
#endif
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

#if defined(HAVE_BSD_UNISTD_H)
#include <bsd/unistd.h>
#endif

#if defined(HAVE_BSD_WCHAR)
#include <bsd/wchar.h>
#endif

#if defined(HAVE_ASM_PRCTL_H)
#include <asm/prctl.h>
#endif

#if defined(HAVE_ASM_MTRR_H)
#include <asm/mtrr.h>
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

#if defined(HAVE_SYS_FSUID_H)
#include <sys/fsuid.h>
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

#if defined(HAVE_SYS_LOADAVG_H)
#include <sys/loadavg.h>
#endif

#if defined(HAVE_SYS_MOUNT_H)
#include <sys/mount.h>
#endif

#if defined(HAVE_SYS_MSG_H)
#include <sys/msg.h>
#endif

#if defined(HAVE_SYS_PARAM_H)
#include <sys/param.h>
#endif

#if defined(HAVE_SYS_PERSONALITY_H)
#include <sys/personality.h>
#endif

#if defined(HAVE_SYS_PRCTL_H)
#include <sys/prctl.h>
#endif

#if defined(HAVE_SYS_PLATFORM_PPC_H)
#include <sys/platform/ppc.h>
#endif

#if defined(HAVE_PTRACE)
#include <sys/ptrace.h>
#endif

#if defined(HAVE_SYS_QUEUE_H)
#include <sys/queue.h>
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

#if defined(HAVE_SYS_SYSCTL_H) &&	\
    !defined(__linux__)
#include <sys/sysctl.h>
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
#if defined(__GNUC__) &&	\
    !defined(__GLIBC__)
/* Suppress kernel sysinfo to avoid collision with musl */
#define _LINUX_SYSINFO_H
#endif
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

#if defined(HAVE_SYS_VMMETER_H)
#include <sys/vmmeter.h>
#endif

/*
 *  SCSI related headers
 */
#if defined(HAVE_SCSI_SCSI_H)
#include <scsi/scsi.h>
#endif

#if defined(HAVE_SCSI_SCSI_IOCTL_H)
#include <scsi/scsi_ioctl.h>
#endif

#if defined(HAVE_SCSI_SG_H)
#include <scsi/sg.h>
#endif

/*
 *  Linux specific includes
 */
#if defined(HAVE_LINUX_ANDROID_BINDER_H)
#include <linux/android/binder.h>
#endif

#if defined(HAVE_LINUX_ANDROID_BINDERFS_H)
#include <linux/android/binderfs.h>
#endif

#if defined(HAVE_LINUX_AUDIT_H)
#include <linux/audit.h>
#endif

#if defined(HAVE_LINUX_CDROM_H)
#include <linux/cdrom.h>
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

#if defined(HAVE_LINUX_FD_H)
#include <linux/fd.h>
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

#if defined(HAVE_LINUX_IF_PACKET_H)
#include <linux/if_packet.h>
#endif

#if defined(HAVE_LINUX_IF_TUN_H)
#include <linux/if_tun.h>
#endif

#if defined(HAVE_LINUX_IO_URING_H)
#include <linux/io_uring.h>
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

#if defined(HAVE_LINUX_FSVERITY_H)
#include <linux/fsverity.h>
#endif

#if defined(HAVE_LINUX_FUTEX_H)
#include <linux/futex.h>
#endif

#if defined(HAVE_LINUX_HPET_H)
#include <linux/hpet.h>
#endif

#if defined(HAVE_LINUX_KD_H)
#include <linux/kd.h>
#endif

#if defined(HAVE_LINUX_LANDLOCK_H)
#include <linux/landlock.h>
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

#if defined(HAVE_LINUX_MEMPOLICY_H)
#include <linux/mempolicy.h>
#endif

#if defined(HAVE_LINUX_NETLINK_H)
#include <linux/netlink.h>
#endif

#if defined(HAVE_LINUX_OPENAT2_H)
#include <linux/openat2.h>
#endif

#if defined(HAVE_LINUX_PCI_H)
#include <linux/pci.h>
#endif

#if defined(HAVE_LINUX_PERF_EVENT_H)
#include <linux/perf_event.h>
#endif

#if defined(HAVE_LINUX_PTP_CLOCK_H)
#include <linux/ptp_clock.h>
#endif

#if defined(HAVE_LINUX_POSIX_TYPES_H)
#include <linux/posix_types.h>
#endif

#if defined(HAVE_LINUX_PPDEV_H)
#include <linux/ppdev.h>
#endif

#if defined(HAVE_LINUX_RANDOM_H)
#include <linux/random.h>
#endif

#if defined(HAVE_LINUX_RSEQ_H)
#include <linux/rseq.h>
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

#if defined(HAVE_LINUX_SERIAL_H)
#include <linux/serial.h>
#endif

#if defined(HAVE_LINUX_SOCK_DIAG_H)
#include <linux/sock_diag.h>
#endif

#if defined(HAVE_LINUX_SOCKET_H)
#include <linux/socket.h>
#endif

#if defined(HAVE_LINUX_SOCKIOS_H)
#include <linux/sockios.h>
#endif

#if defined(HAVE_LINUX_SYSCTL_H)
#include <linux/sysctl.h>
#endif

#if defined(HAVE_LINUX_TASKSTATS_H)
#include <linux/taskstats.h>
#endif

#if defined(HAVE_LINUX_UDP_H)
#include <linux/udp.h>
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
#if defined(HAVE_SYS_XATTR_H) &&	\
    defined(HAVE_ATTR_XATTR_H)
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

#if defined(__NR_get_thread_area)
#define HAVE_GET_THREAD_AREA
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

#if defined(__NR_mmap2)
#define HAVE_MMAP2
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

#if defined(__NR_set_thread_area)
#define HAVE_SET_THREAD_AREA
#endif

#if defined(__NR_syslog)
#define HAVE_SYSLOG
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

#define STRESS_BIT_U(shift)	(1U << shift)
#define STRESS_BIT_UL(shift)	(1UL << shift)
#define STRESS_BIT_ULL(shift)	(1ULL << shift)

/*
 *  cacheflush(2) cache options
 */
#ifdef ICACHE
#define SHIM_ICACHE	(ICACHE)
#else
#define SHIM_ICACHE	(1 << 0)
#endif

#ifdef DCACHE
#define SHIM_DCACHE	(DCACHE)
#else
#define SHIM_DCACHE	(1 << 1)
#endif

#if !defined(EXIT_SUCCESS)
#define EXIT_SUCESSS			(0)
#endif
#if !defined(EXIT_FAILURE)
#define EXIT_FAILURE			(1)
#endif
#define EXIT_NOT_SUCCESS		(2)
#define EXIT_NO_RESOURCE		(3)
#define EXIT_NOT_IMPLEMENTED		(4)
#define EXIT_SIGNALED			(5)
#define EXIT_BY_SYS_EXIT		(6)
#define EXIT_METRICS_UNTRUSTWORTHY	(7)

/*
 *  Stressor run states
 */
#define STRESS_STATE_START		(0)
#define STRESS_STATE_INIT		(1)
#define STRESS_STATE_RUN		(2)
#define STRESS_STATE_DEINIT		(3)
#define STRESS_STATE_STOP		(4)
#define STRESS_STATE_EXIT		(5)
#define STRESS_STATE_WAIT		(6)

/*
 *  Timing units
 */
#define STRESS_NANOSECOND		(1000000000L)
#define STRESS_MICROSECOND		(1000000L)
#define STRESS_MILLISECOND		(1000L)

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
#if defined(MAP_ANON) &&	\
    !defined(MAP_ANONYMOUS)
#define MAP_ANONYMOUS MAP_ANON
#endif

/* GNU HURD and other systems that don't define PATH_MAX */
#ifndef PATH_MAX
#define PATH_MAX 		(4096)
#endif

/*
 * making local static fixes globbering warnings
 */
#define NOCLOBBER	static

#if (_BSD_SOURCE || _SVID_SOURCE || !defined(__gnu_hurd__))
#define STRESS_PAGE_IN
#endif

#define STRESS_TRY_OPEN_OK	  (0)		/* File can be opened */
#define STRESS_TRY_OPEN_FORK_FAIL (1)		/* Try failed, e.g. can't fork */
#define STRESS_TRY_OPEN_WAIT_FAIL (2)		/* Wait on child open failed */
#define STRESS_TRY_OPEN_EXIT_FAIL (3)		/* Can't get _exit() status */
#define STRESS_TRY_OPEN_FAIL	  (4)		/* Can't open file */
#define STRESS_TRY_AGAIN	  (5)		/* Device busy, try again */

#define STRESS_FD_MAX		(65536)		/* Max fds if we can't figure it out */
#define STRESS_PROCS_MAX	(8192)		/* Max number of processes per stressor */

#define DCCP_BUF		(1024)		/* DCCP I/O buffer size */
#define SOCKET_BUF		(8192)		/* Socket I/O buffer size */
#define UDP_BUF			(1024)		/* UDP I/O buffer size */
#define SOCKET_PAIR_BUF		(64)		/* Socket pair I/O buffer size */

#define ABORT_FAILURES		(5)		/* Number of failures before we abort */

/* debug output bitmasks */
#define PR_ERROR		 STRESS_BIT_ULL(0)	/* Print errors */
#define PR_INFO			 STRESS_BIT_ULL(1)	/* Print info */
#define PR_DEBUG		 STRESS_BIT_ULL(2) 	/* Print debug */
#define PR_FAIL			 STRESS_BIT_ULL(3) 	/* Print test failure message */
#define PR_ALL			 (PR_ERROR | PR_INFO | PR_DEBUG | PR_FAIL)

/* Option bit masks */
#define OPT_FLAGS_DRY_RUN	 STRESS_BIT_ULL(4)	/* Don't actually run */
#define OPT_FLAGS_METRICS	 STRESS_BIT_ULL(5)	/* Dump metrics at end */
#define OPT_FLAGS_RANDOM	 STRESS_BIT_ULL(6)	/* Randomize */
#define OPT_FLAGS_SET		 STRESS_BIT_ULL(7)	/* Set if user specifies stress procs */
#define OPT_FLAGS_KEEP_NAME	 STRESS_BIT_ULL(8)	/* Keep stress names to stress-ng */
#define OPT_FLAGS_METRICS_BRIEF	 STRESS_BIT_ULL(9)	/* dump brief metrics */
#define OPT_FLAGS_VERIFY	 STRESS_BIT_ULL(10)	/* verify mode */
#define OPT_FLAGS_MMAP_MADVISE	 STRESS_BIT_ULL(11)	/* enable random madvise settings */
#define OPT_FLAGS_MMAP_MINCORE	 STRESS_BIT_ULL(12)	/* mincore force pages into mem */
#define OPT_FLAGS_TIMES		 STRESS_BIT_ULL(13)	/* user/system time summary */
#define OPT_FLAGS_HDD_SYNC	 STRESS_BIT_ULL(14)	/* HDD O_SYNC */
#define OPT_FLAGS_HDD_DSYNC	 STRESS_BIT_ULL(15)	/* HDD O_DYNC */
#define OPT_FLAGS_HDD_DIRECT	 STRESS_BIT_ULL(16)	/* HDD O_DIRECT */
#define OPT_FLAGS_HDD_NOATIME	 STRESS_BIT_ULL(17)	/* HDD O_NOATIME */
#define OPT_FLAGS_MINIMIZE	 STRESS_BIT_ULL(18)	/* Minimize */
#define OPT_FLAGS_MAXIMIZE	 STRESS_BIT_ULL(19)	/* Maximize */
#define OPT_FLAGS_SYSLOG	 STRESS_BIT_ULL(20)	/* log test progress to syslog */
#define OPT_FLAGS_AGGRESSIVE	 STRESS_BIT_ULL(21)	/* aggressive mode enabled */
#define OPT_FLAGS_ALL		 STRESS_BIT_ULL(22)	/* --all mode */
#define OPT_FLAGS_SEQUENTIAL	 STRESS_BIT_ULL(23)	/* --sequential mode */
#define OPT_FLAGS_PERF_STATS	 STRESS_BIT_ULL(24)	/* --perf stats mode */
#define OPT_FLAGS_LOG_BRIEF	 STRESS_BIT_ULL(25)	/* --log-brief */
#define OPT_FLAGS_THERMAL_ZONES  STRESS_BIT_ULL(26)	/* --tz thermal zones */
#define OPT_FLAGS_SOCKET_NODELAY STRESS_BIT_ULL(27)	/* --sock-nodelay */
#define OPT_FLAGS_IGNITE_CPU	 STRESS_BIT_ULL(28)	/* --cpu-ignite */
#define OPT_FLAGS_PATHOLOGICAL	 STRESS_BIT_ULL(29)	/* --pathological */
#define OPT_FLAGS_NO_RAND_SEED	 STRESS_BIT_ULL(30)	/* --no-rand-seed */
#define OPT_FLAGS_THRASH	 STRESS_BIT_ULL(31)	/* --thrash */
#define OPT_FLAGS_OOMABLE	 STRESS_BIT_ULL(32)	/* --oomable */
#define OPT_FLAGS_ABORT		 STRESS_BIT_ULL(33)	/* --abort */
#define OPT_FLAGS_CPU_ONLINE_ALL STRESS_BIT_ULL(34)	/* --cpu-online-all */
#define OPT_FLAGS_TIMESTAMP	 STRESS_BIT_ULL(35)	/* --timestamp */
#define OPT_FLAGS_DEADLINE_GRUB  STRESS_BIT_ULL(36)	/* --sched-reclaim */
#define OPT_FLAGS_FTRACE	 STRESS_BIT_ULL(37)	/* --ftrace */
#define OPT_FLAGS_SEED		 STRESS_BIT_ULL(38)	/* --seed */
#define OPT_FLAGS_SKIP_SILENT	 STRESS_BIT_ULL(39)	/* --skip-silent */
#define OPT_FLAGS_SMART		 STRESS_BIT_ULL(40)	/* --smart */
#define OPT_FLAGS_NO_OOM_ADJUST	 STRESS_BIT_ULL(41)	/* --no-oom-adjust */

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

/* Stressor classes */
#define CLASS_CPU		STRESS_BIT_UL(0)	/* CPU only */
#define CLASS_MEMORY		STRESS_BIT_UL(1)	/* Memory thrashers */
#define CLASS_CPU_CACHE		STRESS_BIT_UL(2)	/* CPU cache */
#define CLASS_IO		STRESS_BIT_UL(3)	/* I/O read/writes etc */
#define CLASS_NETWORK		STRESS_BIT_UL(4)	/* Network, sockets, etc */
#define CLASS_SCHEDULER		STRESS_BIT_UL(5)	/* Scheduling */
#define CLASS_VM		STRESS_BIT_UL(6)	/* VM stress, big memory, swapping */
#define CLASS_INTERRUPT		STRESS_BIT_UL(7)	/* interrupt floods */
#define CLASS_OS		STRESS_BIT_UL(8)	/* generic OS tests */
#define CLASS_PIPE_IO		STRESS_BIT_UL(9)	/* pipe I/O */
#define CLASS_FILESYSTEM	STRESS_BIT_UL(10)	/* file system */
#define CLASS_DEV		STRESS_BIT_UL(11)	/* device (null, zero, etc) */
#define CLASS_SECURITY		STRESS_BIT_UL(12)	/* security APIs */
#define CLASS_PATHOLOGICAL	STRESS_BIT_UL(13)	/* can hang a machine */


/* Help information for options */
typedef struct {
	const char *opt_s;		/* short option */
	const char *opt_l;		/* long option */
	const char *description;	/* description */
} stress_help_t;

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
} stress_type_id_t;

typedef struct stress_stressor_info *stress_pstressor_info_t;

/*
 *  Per ELISA request, we have a duplicated counter
 *  and run_ok flag in a different shared memory region
 *  so we can sanity check these just in case the stats
 *  have got corrupted.
 */
typedef struct {
	struct {
		uint64_t counter;	/* Copy of stats counter */
		bool     run_ok;	/* Copy of run_ok */
	} data;
	uint32_t	hash;		/* Hash of data */
} stress_checksum_t;

/* settings for storing opt arg parsed data */
typedef struct stress_setting {
	struct stress_setting *next;	/* next setting in list */
	stress_pstressor_info_t	proc;
	char *name;			/* name of setting */
	stress_type_id_t type_id;	/* setting type */
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
		char 		*str;
		bool		boolean;
		uintptr_t	uintptr;/* for func pointers */
	} u;
} stress_setting_t;

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
	volatile void 		*void_ptr_val;
} stress_put_val_t;

/* Network domains flags */
#define DOMAIN_INET		(0x00000001)	/* AF_INET */
#define DOMAIN_INET6		(0x00000002)	/* AF_INET6 */
#define DOMAIN_UNIX		(0x00000004)	/* AF_UNIX */

#define DOMAIN_INET_ALL		(DOMAIN_INET | DOMAIN_INET6)
#define DOMAIN_ALL		(DOMAIN_INET | DOMAIN_INET6 | DOMAIN_UNIX)

/* Large prime to stride around large VM regions */
#define PRIME_64		(0x8f0000000017116dULL)

typedef uint32_t stress_class_t;

typedef struct {
	void *page_none;		/* mmap'd PROT_NONE page */
	void *page_ro;			/* mmap'd PROT_RO page */
	void *page_wo;			/* mmap'd PROT_WO page */
} stress_mapped_t;

#define STRESS_MISC_STATS_MAX	(10)

typedef struct {
	char description[32];
	double value;
} stress_misc_stats_t;

/* stressor args */
typedef struct {
	uint64_t *counter;		/* stressor counter */
	bool *counter_ready;		/* counter can be read */
	const char *name;		/* stressor name */
	uint64_t max_ops;		/* max number of bogo ops */
	const uint32_t instance;	/* stressor instance # */
	const uint32_t num_instances;	/* number of instances */
	pid_t pid;			/* stressor pid */
	pid_t ppid;			/* stressor ppid */
	size_t page_size;		/* page size */
	stress_mapped_t *mapped;	/* mmap'd pages, addr of g_shared mapped */
	stress_misc_stats_t *misc_stats;/* misc per stressor stats */
} stress_args_t;

typedef struct {
	const int opt;			/* optarg option*/
	int (*opt_set_func)(const char *opt); /* function to set it */
} stress_opt_set_func_t;

/* stressor information */
typedef struct {
	int (*stressor)(const stress_args_t *args);	/* stressor function */
	int (*supported)(const char *name);	/* return 0 = supported, -1, not */
	void (*init)(void);		/* stressor init, NULL = ignore */
	void (*deinit)(void);		/* stressor de-init, NULL = ignore */
	void (*set_default)(void);	/* default set-up */
	void (*set_limit)(uint64_t max);/* set limits */
	const stress_class_t class;	/* stressor class */
	const stress_opt_set_func_t *opt_set_funcs;	/* option functions */
	const stress_help_t *help;	/* stressor help options */
} stressor_info_t;

/* pthread wrapped stress_args_t */
typedef struct {
	const stress_args_t *args;	/* Stress test args */
	void *data;			/* Per thread private data */
	int pthread_ret;		/* Per thread return value */
} stress_pthread_args_t;

/* string hash linked list type */
typedef struct stress_hash {
	struct stress_hash *next; 	/* next hash item */
} stress_hash_t;

/* string hash table */
typedef struct {
	stress_hash_t	**table;	/* hash table */
	size_t		n;		/* number of hash items in table */
} stress_hash_table_t;

/* vmstat information */
typedef struct {			/* vmstat column */
	uint64_t	procs_running;	/* r */
	uint64_t	procs_blocked;	/* b */
	uint64_t	swap_total;	/* swpd info, total */
	uint64_t	swap_free;	/* swpd info, free */
	uint64_t	swap_used;	/* swpd used = total - free */
	uint64_t	memory_free;	/* free */
	uint64_t	memory_buff;	/* buff */
	uint64_t	memory_cache;	/* cache */
	uint64_t	swap_in;	/* si */
	uint64_t	swap_out;	/* so */
	uint64_t	block_in;	/* bi */
	uint64_t	block_out;	/* bo */
	uint64_t	interrupt;	/* in */
	uint64_t	context_switch;	/* cs */
	uint64_t	user_time;	/* us */
	uint64_t	system_time;	/* sy */
	uint64_t	idle_time;	/* id */
	uint64_t	wait_time;	/* wa */
	uint64_t	stolen_time;	/* st */
} stress_vmstat_t;

/* iostat information, from /sys/block/$dev/stat */
typedef struct {
	uint64_t	read_io;	/* number of read I/Os processed */
	uint64_t	read_merges;	/* number of read I/Os merged with in-queue I/O */
	uint64_t	read_sectors;	/* number of sectors read */
	uint64_t	read_ticks;	/* total wait time for read requests */
	uint64_t	write_io;	/* number of write I/Os processed */
	uint64_t	write_merges;	/* number of write I/Os merged with in-queue I/O */
	uint64_t	write_sectors;	/* number of sectors written */
	uint64_t	write_ticks;	/* total wait time for write requests */
	uint64_t	in_flight;	/* number of I/Os currently in flight */
	uint64_t	io_ticks;	/* total time this block device has been active */
	uint64_t	time_in_queue;	/* total wait time for all requests */
	uint64_t	discard_io;	/* number of discard I/Os processed */
	uint64_t	discard_merges;	/* number of discard I/Os merged with in-queue I/O */
	uint64_t	discard_sectors;/* number of sectors discarded */
	uint64_t	discard_ticks;	/* total wait time for discard requests */
} stress_iostat_t;

/* gcc 4.7 and later support vector ops */
#if defined(__GNUC__) &&	\
    NEED_GNUC(4, 7, 0)
#define STRESS_VECTOR	1
#endif

/* gcc 7.0 and later support __attribute__((fallthrough)); */
#if defined(HAVE_ATTRIBUTE_FALLTHROUGH)
#define CASE_FALLTHROUGH __attribute__((fallthrough)) /* Fallthrough */
#else
#define CASE_FALLTHROUGH /* Fallthrough */
#endif

/* no return hint */
#if (defined(__GNUC__) && NEED_GNUC(2, 5, 0)) || 	\
    (defined(__clang__) && NEED_CLANG(3, 0, 0))
#define NORETURN 	__attribute__ ((noreturn))
#else
#define NORETURN
#endif

/* weak attribute */
#if (defined(__GNUC__) && NEED_GNUC(4, 0, 0)) || 	\
    (defined(__clang__) && NEED_CLANG(3, 4, 0))
#define WEAK		__attribute__ ((weak))
#define HAVE_WEAK_ATTRIBUTE
#else
#define WEAK
#endif

/* force inlining hint */
#if (defined(__GNUC__) && NEED_GNUC(3, 4, 0) 					\
     && ((!defined(__s390__) && !defined(__s390x__)) || NEED_GNUC(6, 0, 1))) ||	\
    (defined(__clang__) && NEED_CLANG(3, 0, 0))
#define ALWAYS_INLINE	__attribute__ ((always_inline))
#else
#define ALWAYS_INLINE
#endif

/* force no inlining hint */
#if (defined(__GNUC__) && NEED_GNUC(3, 4, 0)) ||	\
    (defined(__clang__) && NEED_CLANG(3, 0, 0))
#define NOINLINE	__attribute__ ((noinline))
#else
#define NOINLINE
#endif

/* -O3 attribute support */
#if defined(__GNUC__) &&	\
    !defined(__clang__) &&	\
    NEED_GNUC(4, 6, 0)
#define OPTIMIZE3 	__attribute__((optimize("-O3")))
#else
#define OPTIMIZE3
#endif

/* -O1 attribute support */
#if defined(__GNUC__) &&	\
    !defined(__clang__) &&	\
    NEED_GNUC(4, 6, 0)
#define OPTIMIZE1 	__attribute__((optimize("-O1")))
#else
#define OPTIMIZE1
#endif

/* -O0 attribute support */
#if defined(__GNUC__) &&	\
    !defined(__clang__) &&	\
    NEED_GNUC(4, 6, 0)
#define OPTIMIZE0 	__attribute__((optimize("-O0")))
#else
#define OPTIMIZE0
#endif

/* warn unused attribute */
#if (defined(__GNUC__) && NEED_GNUC(4, 2, 0)) ||	\
    (defined(__clang__) && NEED_CLANG(3, 0, 0))
#define WARN_UNUSED	__attribute__((warn_unused_result))
#else
#define WARN_UNUSED
#endif

#if ((defined(__GNUC__) && NEED_GNUC(3, 3, 0)) ||	\
     (defined(__clang__) && NEED_CLANG(3, 0, 0))) &&	\
    !defined(__PCC__)
#define ALIGNED(a)	__attribute__((aligned(a)))
#else
#define ALIGNED(a)
#endif

/* Force alignment to nearest 128 bytes */
#if ((defined(__GNUC__) && NEED_GNUC(3, 3, 0)) ||	\
     (defined(__clang__) && NEED_CLANG(3, 0, 0))) &&	\
    defined(HAVE_ALIGNED_128)
#define ALIGN128	ALIGNED(128)
#else
#define ALIGN128
#endif

/* Force alignment to nearest 64 bytes */
#if ((defined(__GNUC__) && NEED_GNUC(3, 3, 0)) ||	\
     (defined(__clang__) && NEED_CLANG(3, 0, 0))) &&	\
    defined(HAVE_ALIGNED_64)
#define ALIGN64		ALIGNED(64)
#else
#define ALIGN64
#endif

#if (defined(__GNUC__) && NEED_GNUC(4, 6, 0)) ||	\
    (defined(__clang__) && NEED_CLANG(3, 0, 0))
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
#if (defined(__GNUC__) && NEED_GNUC(4, 6, 0)) ||	\
    (defined(__clang__) && NEED_CLANG(3, 3, 0))
#define HOT		__attribute__ ((hot))
#else
#define HOT
#endif

/* GCC mlocked data and data section attribute */
#if ((defined(__GNUC__) && NEED_GNUC(4, 6, 0) ||	\
     (defined(__clang__) && NEED_CLANG(3, 0, 0)))) &&	\
    !defined(__sun__) &&				\
    !defined(__APPLE__) &&				\
    !defined(BUILD_STATIC)
#define MLOCKED_TEXT	__attribute__((__section__("mlocked_text")))
#define MLOCKED_SECTION 1
#else
#define MLOCKED_TEXT
#endif

/* print format attribute */
#if ((defined(__GNUC__) && NEED_GNUC(3, 2, 0)) ||	\
     (defined(__clang__) && NEED_CLANG(3, 0, 0)))
#define FORMAT(func, a, b) __attribute__((format(func, a, b)))
#else
#define FORMAT(func, a, b)
#endif

/* restrict keyword */
#if defined(HAVE___RESTRICT)
#define RESTRICT __restrict
#else
#define RESTRICT
#endif

/* optimisation on branching */
#if defined(HAVE_BUILTIN_EXPECT)
#define LIKELY(x)	__builtin_expect((x),1)
#define UNLIKELY(x)	__builtin_expect((x),0)
#else
#define LIKELY(x)	(x)
#define UNLIKELY(x)	(x)
#endif

#if !defined(HAVE_BUILTIN_PREFETCH) || defined(__PCC__)
/* a fake prefetch var-args no-op */
static inline void shim_builtin_prefetch(const void *addr, ...)
{
	va_list ap;

	va_start(ap, addr);
	va_end(ap);
}
#else
#define shim_builtin_prefetch		__builtin_prefetch
#endif

#if defined(HAVE_BUILTIN_MEMMOVE)
#define shim_builtin_memmove		__builtin_memmove
#else
#define shim_builtin_memmove		memmove
#endif

/* do nothing */
#if defined(HAVE_ASM_NOP)
#define FORCE_DO_NOTHING() __asm__ __volatile__("nop;")
#elif defined(HAVE_ASM_NOTHING)
#define FORCE_DO_NOTHING() __asm__ __volatile__("")
#else
#define FORCE_DO_NOTHING() while (0)
#endif

#if defined(__clang__) &&	\
    NEED_CLANG(4, 0, 0) &&	\
    defined(HAVE_PRAGMA)
#define STRESS_PRAGMA_PUSH	_Pragma("GCC diagnostic push")
#define STRESS_PRAGMA_POP	_Pragma("GCC diagnostic pop")
#define STRESS_PRAGMA_WARN_OFF	_Pragma("GCC diagnostic ignored \"-Weverything\"")
#elif defined(__GNUC__) &&	\
      defined(HAVE_PRAGMA) &&	\
      NEED_GNUC(4, 4, 0)
#define STRESS_PRAGMA_PUSH	_Pragma("GCC diagnostic push")
#define STRESS_PRAGMA_POP	_Pragma("GCC diagnostic pop")
#define STRESS_PRAGMA_WARN_OFF	_Pragma("GCC diagnostic ignored \"-Wall\"") \
				_Pragma("GCC diagnostic ignored \"-Wextra\"") \
				_Pragma("GCC diagnostic ignored \"-Wdeprecated-declarations\"") \
				_Pragma("GCC diagnostic ignored \"-Wcast-qual\"") \
				_Pragma("GCC diagnostic ignored \"-Wnonnull\"")
#else
#define STRESS_PRAGMA_PUSH
#define STRESS_PRAGMA_POP
#define STRESS_PRAGMA_WARN_OFF
#endif

/* Logging helpers */
extern int pr_msg(FILE *fp, const uint64_t flag,
	const char *const fmt, va_list va) FORMAT(printf, 3, 0);
extern int pr_yaml(FILE *fp, const char *const fmt, ...) FORMAT(printf, 2, 3);
extern void pr_yaml_runinfo(FILE *fp);
extern void pr_runinfo(void);
extern void pr_openlog(const char *filename);
extern void pr_closelog(void);
extern void pr_fail_check(int *rc);

extern void pr_dbg(const char *fmt, ...)  FORMAT(printf, 1, 2);
extern void pr_dbg_skip(const char *fmt, ...)  FORMAT(printf, 1, 2);
extern void pr_inf(const char *fmt, ...)  FORMAT(printf, 1, 2);
extern void pr_inf_skip(const char *fmt, ...)  FORMAT(printf, 1, 2);
extern void pr_err(const char *fmt, ...)  FORMAT(printf, 1, 2);
extern void pr_err_skip(const char *fmt, ...)  FORMAT(printf, 1, 2);
extern void pr_fail(const char *fmt, ...) FORMAT(printf, 1, 2);
extern void pr_tidy(const char *fmt, ...) FORMAT(printf, 1, 2);

extern void pr_lock(bool *locked);
extern void pr_unlock(bool *locked);
extern void pr_inf_lock(bool *locked, const char *fmt, ...)  FORMAT(printf, 2, 3);
extern void pr_dbg_lock(bool *locked, const char *fmt, ...)  FORMAT(printf, 2, 3);

/* volatile debug print macro */
#ifdef DEBUG
#define pr_dbg_v(fmt, ...) \
	do { if (DEBUG) pr_dbg(fmt, __VA_ARGS__); } while (0)
#else
#define pr_dbg_v(fmt, ...)
#endif

#if defined(HAVE_SYSLOG_H)
#define shim_syslog(priority, format, ...)	\
		syslog(priority, format, __VA_ARGS__)
#define shim_openlog(ident, option, facility) \
		openlog(ident, option, facility)
#define shim_closelog()		closelog()
#else
#define shim_syslog(priority, format, ...)
#define shim_openlog(ident, option, facility)
#define shim_closelog()
#endif

/* Memory size constants */
#define KB			(1ULL << 10)
#define	MB			(1ULL << 20)
#define GB			(1ULL << 30)
#define TB			(1ULL << 40)
#define PB			(1ULL << 50)
#define EB			(1ULL << 60)

#define ONE_BILLIONTH		(1.0E-9)
#define ONE_MILLIONTH		(1.0E-6)
#define ONE_THOUSANDTH		(1.0E-3)

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
#define MAX_MEM_LIMIT		(MAX_32)
#else
#define MAX_MEM_LIMIT		(MAX_48)
#endif

#define MAX_FILE_LIMIT		((1ULL << ((sizeof(off_t) * 8) - 1)) - 1)
/*
 * --maximize files must not be so big that we fill up
 * a disk, so make them either the MAX_FILE_FILE_LIMIT for
 * systems with small off_t or 4GB for large off_t systems
 */
#define MAXIMIZED_FILE_SIZE	((sizeof(off_t) < 8) ? MAX_FILE_LIMIT : MAX_32)

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
#define MAX_COPY_FILE_BYTES	(MAX_FILE_LIMIT)
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
#define MAX_HDD_BYTES		(MAX_FILE_LIMIT)
#define DEFAULT_HDD_BYTES	(1 * GB)

#define MIN_HDD_WRITE_SIZE	(1)
#define MAX_HDD_WRITE_SIZE	(4 * MB)
#define DEFAULT_HDD_WRITE_SIZE	(64 * 1024)

#define MIN_FALLOCATE_BYTES	(1 * MB)
#define MAX_FALLOCATE_BYTES	(MAX_FILE_LIMIT)
#define DEFAULT_FALLOCATE_BYTES	(1 * GB)

#define MIN_FIEMAP_SIZE		(1 * MB)
#define MAX_FIEMAP_SIZE		(MAX_FILE_LIMIT)
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
#define MAX_IOMIX_BYTES		(MAX_FILE_LIMIT)
#define DEFAULT_IOMIX_BYTES	(1 * GB)

#define MIN_JUDY_SIZE		(1 * KB)
#define MAX_JUDY_SIZE		(4 * MB)
#define DEFAULT_JUDY_SIZE	(256 * KB)

#define MIN_VFORKS		(1)
#define MAX_VFORKS		(16000)
#define DEFAULT_VFORKS		(1)

#define MIN_HSEARCH_SIZE	(1 * KB)
#define MAX_HSEARCH_SIZE	(4 * MB)
#define DEFAULT_HSEARCH_SIZE	(8 * KB)

#define MIN_LEASE_BREAKERS	(1)
#define MAX_LEASE_BREAKERS	(64)
#define DEFAULT_LEASE_BREAKERS	(1)

#define MIN_LIST_SIZE		(64)
#define MAX_LIST_SIZE		(1000000)
#define DEFAULT_LIST_SIZE	(5000)

#define MIN_LSEARCH_SIZE	(1 * KB)
#define MAX_LSEARCH_SIZE	(1 * MB)
#define DEFAULT_LSEARCH_SIZE	(8 * KB)

#define MIN_MALLOC_BYTES	(1 * KB)
#define MAX_MALLOC_BYTES	(MAX_MEM_LIMIT)
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
#define MAX_MEMFD_BYTES		(MAX_MEM_LIMIT)
#define DEFAULT_MEMFD_BYTES	(256 * MB)

#define MIN_MEMFD_FDS		(8)
#define MAX_MEMFD_FDS		(4096)
#define DEFAULT_MEMFD_FDS	(256)

#define MIN_MERGESORT_SIZE	(1 * KB)
#define MAX_MERGESORT_SIZE	(4 * MB)
#define DEFAULT_MERGESORT_SIZE	(256 * KB)

#define MIN_MMAP_BYTES		(4 * KB)
#define MAX_MMAP_BYTES		(MAX_MEM_LIMIT)
#define DEFAULT_MMAP_BYTES	(256 * MB)

#define MIN_MEMRATE_BYTES	(4 * KB)
#define MAX_MEMRATE_BYTES	(MAX_MEM_LIMIT)
#define DEFAULT_MEMRATE_BYTES	(256 * MB)

#define DEFAULT_MREMAP_BYTES	(256 * MB)
#define MIN_MREMAP_BYTES	(4 * KB)
#define MAX_MREMAP_BYTES	(MAX_MEM_LIMIT)
#define DEFAULT_MREMAP_BYTES	(256 * MB)

#define MIN_MSYNC_BYTES		(1 * MB)	/* MUST NOT BE page size or less! */
#define MAX_MSYNC_BYTES		(MAX_FILE_LIMIT)
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
#define MAX_READAHEAD_BYTES	(MAX_FILE_LIMIT)
#define DEFAULT_READAHEAD_BYTES	(64 * MB)

#define MIN_REVIO_BYTES		(1 * MB)
#define MAX_REVIO_BYTES		(MAX_FILE_LIMIT)
#define DEFAULT_REVIO_BYTES	(1 * GB)

#define MIN_SCTP_PORT		(1024)
#define MAX_SCTP_PORT		(65535)
#define DEFAULT_SCTP_PORT	(9000)

#define MIN_SENDFILE_SIZE	(1 * KB)
#define MAX_SENDFILE_SIZE	(1 * GB)
#define DEFAULT_SENDFILE_SIZE	(4 * MB)

#define MIN_SEEK_SIZE		(1 * MB)
#define MAX_SEEK_SIZE		(MAX_FILE_LIMIT)
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

#define MIN_SKIPLIST_SIZE	(1 * KB)
#define MAX_SKIPLIST_SIZE	(4 * MB)
#define DEFAULT_SKIPLIST_SIZE	(64 * KB)

#define MIN_SLEEP		(1)
#define MAX_SLEEP		(30000)
#define DEFAULT_SLEEP		(1024)

#define MIN_SOCKET_PORT		(1024)
#define MAX_SOCKET_PORT		(65535)
#define DEFAULT_SOCKET_PORT	(5000)

#define DEFAULT_SOCKET_MANY_PORT (11000)
#define DEFAULT_SOCKABUSE_PORT	(12000)

#define MIN_RAWUDP_PORT		(1024)
#define MAX_RAWUDP_PORT		(65535)
#define DEFAULT_RAWUDP_PORT	(13000)

#define MIN_RAWPKT_PORT		(1024)
#define MAX_RAWPKT_PORT		(65535)
#define DEFAULT_RAWPKT_PORT	(14000)

#define MIN_SOCKET_FD_PORT	(1024)
#define MAX_SOCKET_FD_PORT	(65535)
#define DEFAULT_SOCKET_FD_PORT	(8000)

#define MIN_SPLICE_BYTES	(1 * KB)
#define MAX_SPLICE_BYTES	(64 * MB)
#define DEFAULT_SPLICE_BYTES	(64 * KB)

#define MIN_STREAM_L3_SIZE	(4 * KB)
#define MAX_STREAM_L3_SIZE	(MAX_MEM_LIMIT)
#define DEFAULT_STREAM_L3_SIZE	(4 * MB)

#define MIN_SYNC_FILE_BYTES	(1 * MB)
#define MAX_SYNC_FILE_BYTES	(MAX_FILE_LIMIT)
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
#define MAX_USERFAULTFD_BYTES	(MAX_MEM_LIMIT)
#define DEFAULT_USERFAULTFD_BYTES (16 * MB)

#define MIN_VM_BYTES		(4 * KB)
#define MAX_VM_BYTES		(MAX_MEM_LIMIT)
#define DEFAULT_VM_BYTES	(256 * MB)

#define MIN_VM_HANG		(0)
#define MAX_VM_HANG		(3600)
#define DEFAULT_VM_HANG		(~0ULL)

#define MIN_VM_RW_BYTES		(4 * KB)
#define MAX_VM_RW_BYTES		(MAX_MEM_LIMIT)
#define DEFAULT_VM_RW_BYTES	(16 * MB)

#define MIN_VM_SPLICE_BYTES	(4 * KB)
#define MAX_VM_SPLICE_BYTES	(64 * MB)
#define DEFAULT_VM_SPLICE_BYTES	(64 * KB)

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

/* MWC random number initial seed */
#define STRESS_MWC_SEED_Z	(362436069UL)
#define STRESS_MWC_SEED_W	(521288629UL)
#define STRESS_MWC_SEED()	stress_mwc_seed(STRESS_MWC_SEED_W, STRESS_MWC_SEED_Z)

#define SIZEOF_ARRAY(a)		(sizeof(a) / sizeof(a[0]))

/* Arch specific, x86 */
#if defined(__x86_64__) || defined(__x86_64) || \
    defined(__i386__)   || defined(__i386)
#define STRESS_ARCH_X86		(1)
#endif

/* Arch specific, ARM */
#if defined(__ARM_ARCH_6__)   || defined(__ARM_ARCH_6J__)  || \
    defined(__ARM_ARCH_6K__)  || defined(__ARM_ARCH_6Z__)  || \
    defined(__ARM_ARCH_6ZK__) || defined(__ARM_ARCH_6T2__) || \
    defined(__ARM_ARCH_6M__)  || defined(__ARM_ARCH_7__)   || \
    defined(__ARM_ARCH_7A__)  || defined(__ARM_ARCH_7R__)  || \
    defined(__ARM_ARCH_7M__)  || defined(__ARM_ARCH_7EM__) || \
    defined(__ARM_ARCH_8A__)  || defined(__aarch64__)
#define STRESS_ARCH_ARM		(1)
#endif

/* Arch specific RISC-V */
#if defined(__riscv) || \
    defined(__riscv__)
#define STRESS_ARCH_RISC_V	(1)
#endif

/* Arch specific, IBM S390 */
#if defined(__s390__)
#define STRESS_ARCH_S390	(1)
#endif

/* Arch specific PPC64 */
#if defined(__PPC64__)
#define STRESS_ARCH_PPC64	(1)
#endif

/* Arch specific M68K */
#if defined(__m68k__) ||	\
    defined(__mc68000__) ||	\
    defined(__mc68010__) ||	\
    defined(__mc68020__)
#define STRESS_ARCH_M68K	(1)
#endif

/* Arch specific SPARC */
#if defined(__sparc) ||		\
    defined(__sparc__) ||	\
    defined(__sparc_v9__)
#define STRESS_ARCH_SPARC
#endif

/* GCC5.0+ target_clones attribute, x86 */
#if defined(STRESS_ARCH_X86) &&	\
    defined(HAVE_TARGET_CLONES)

#if defined(HAVE_TARGET_CLONES_MMX)
#define TARGET_CLONE_MMX	"mmx",
#define TARGET_CLONE_USE
#else
#define TARGET_CLONE_MMX
#endif

#if defined(HAVE_TARGET_CLONES_AVX)
#define TARGET_CLONE_AVX	"avx",
#define TARGET_CLONE_USE
#else
#define TARGET_CLONE_AVX
#endif

#if defined(HAVE_TARGET_CLONES_AVX2)
#define TARGET_CLONE_AVX2	"avx2",
#define TARGET_CLONE_USE
#else
#define TARGET_CLONE_AVX2
#endif

#if defined(HAVE_TARGET_CLONES_SSE)
#define TARGET_CLONE_SSE	"sse",
#define TARGET_CLONE_USE
#else
#define TARGET_CLONE_SSE
#endif

#if defined(HAVE_TARGET_CLONES_SSE2)
#define TARGET_CLONE_SSE2	"sse2",
#define TARGET_CLONE_USE
#else
#define TARGET_CLONE_SSE2
#endif

#if defined(HAVE_TARGET_CLONES_SSE3)
#define TARGET_CLONE_SSE3	"sse3",
#define TARGET_CLONE_USE
#else
#define TARGET_CLONE_SSE3
#endif

#if defined(HAVE_TARGET_CLONES_SSSE3)
#define TARGET_CLONE_SSSE3	"ssse3",
#define TARGET_CLONE_USE
#else
#define TARGET_CLONE_SSSE3
#endif

#if defined(HAVE_TARGET_CLONES_SSE4_1)
#define TARGET_CLONE_SSE4_1	"sse4.1",
#define TARGET_CLONE_USE
#else
#define TARGET_CLONE_SSE4_1
#endif

#if defined(HAVE_TARGET_CLONES_SSE4_2)
#define TARGET_CLONE_SSE4_2	"sse4.2",
#define TARGET_CLONE_USE
#else
#define TARGET_CLONE_SSE4_2
#endif

#if defined(HAVE_TARGET_CLONES_SKYLAKE_AVX512)
#define TARGET_CLONE_SKYLAKE_AVX512	"arch=skylake-avx512",
#define TARGET_CLONE_USE
#else
#define TARGET_CLONE_SKYLAKE_AVX512
#endif

#if defined(HAVE_TARGET_CLONES_ALDERLAKE)
#define TARGET_CLONE_ALDERLAKE	"arch=alderlake",
#define TARGET_CLONE_USE
#else
#define TARGET_CLONE_ALDERLAKE
#endif

#define TARGET_CLONES_ALL			\
	TARGET_CLONE_AVX TARGET_CLONE_AVX2 	\
	TARGET_CLONE_MMX TARGET_CLONE_SSE	\
	TARGET_CLONE_SSE2 TARGET_CLONE_SSE3	\
	TARGET_CLONE_SSSE3 TARGET_CLONE_SSE4_1	\
	TARGET_CLONE_SSE4_2			\
	TARGET_CLONE_SKYLAKE_AVX512		\
	TARGET_CLONE_ALDERLAKE			\
	"default"

#if defined(TARGET_CLONE_USE)
#define TARGET_CLONES	__attribute__((target_clones(TARGET_CLONES_ALL)))
#endif
#endif

/* GCC5.0+ target_clones attribute, ppc64 */

#if defined(STRESS_ARCH_PPC64) &&	\
    defined(HAVE_TARGET_CLONES) && 	\
    defined(HAVE_TARGET_CLONES_POWER9)
#define TARGET_CLONES	__attribute__((target_clones("cpu=power9,default")))
#endif

#if !defined(TARGET_CLONES)
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

static inline void ALWAYS_INLINE shim_mb(void)
{
	asm volatile ("" ::: "memory");
}

/* increment the stessor bogo ops counter */
static inline void ALWAYS_INLINE inc_counter(const stress_args_t *args)
{
	*args->counter_ready = false;
	shim_mb();
	(*(args->counter))++;
	shim_mb();
	*args->counter_ready = true;
	shim_mb();
}

static inline uint64_t ALWAYS_INLINE get_counter(const stress_args_t *args)
{
	return *args->counter;
}

static inline void ALWAYS_INLINE set_counter(const stress_args_t *args, const uint64_t val)
{
	*args->counter_ready = false;
	shim_mb();
	*args->counter = val;
	shim_mb();
	*args->counter_ready = true;
	shim_mb();
}

static inline void ALWAYS_INLINE add_counter(const stress_args_t *args, const uint64_t inc)
{
	*args->counter_ready = false;
	shim_mb();
	*args->counter += inc;
	shim_mb();
	*args->counter_ready = true;
	shim_mb();
}

/* pthread porting shims, spinlock or fallback to mutex */
#if defined(HAVE_LIB_PTHREAD)
#if defined(HAVE_LIB_PTHREAD_SPINLOCK) &&	\
    !defined(__DragonFly__) &&			\
    !defined(__OpenBSD__)
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

/* Capabilities shim */

#define SHIM_CAP_IS_ROOT		(-1)

/* POSIX-draft defined capabilities */
#if defined(CAP_CHOWN)
#define SHIM_CAP_CHOWN			CAP_CHOWN
#else
#define SHIM_CAP_CHOWN			SHIM_CAP_IS_ROOT
#endif

#if defined(CAP_DAC_OVERRIDE)
#define SHIM_CAP_DAC_OVERRIDE		CAP_DAC_OVERRIDE
#else
#define SHIM_CAP_DAC_OVERRIDE		SHIM_CAP_IS_ROOT
#endif

#if defined(CAP_DAC_READ_SEARCH)
#define SHIM_CAP_DAC_READ_SEARCH	CAP_DAC_READ_SEARCH
#else
#define SHIM_CAP_DAC_READ_SEARCH	SHIM_CAP_IS_ROOT
#endif

#if defined(CAP_FOWNER)
#define SHIM_CAP_FOWNER			CAP_FOWNER
#else
#define SHIM_CAP_FOWNER			SHIM_CAP_IS_ROOT
#endif

#if defined(CAP_FSETID)
#define SHIM_CAP_FSETID			CAP_FSETID
#else
#define SHIM_CAP_FSETID			SHIM_CAP_IS_ROOT
#endif

#if defined(CAP_KILL)
#define SHIM_CAP_KILL			CAP_KILL
#else
#define SHIM_CAP_KILL			SHIM_CAP_IS_ROOT
#endif

#if defined(CAP_SETGID)
#define SHIM_CAP_SETGID			CAP_SETGID
#else
#define SHIM_CAP_SETGID			SHIM_CAP_IS_ROOT
#endif

#if defined(CAP_SETUID)
#define SHIM_CAP_SETUID			CAP_SETUID
#else
#define SHIM_CAP_SETUID			SHIM_CAP_IS_ROOT
#endif

/* Linux specific capabilities */
#if defined(CAP_SETPCAP)
#define SHIM_CAP_SETPCAP		CAP_SETPCAP
#else
#define SHIM_CAP_SETPCAP		SHIM_CAP_IS_ROOT
#endif

#if defined(CAP_LINUX_IMMUTABLE)
#define SHIM_CAP_LINUX_IMMUTABLE	CAP_LINUX_IMMUTABLE
#else
#define SHIM_CAP_LINUX_IMMUTABLE	SHIM_CAP_IS_ROOT
#endif

#if defined(CAP_NET_BIND_SERVICE)
#define SHIM_CAP_NET_BIND_SERVICE	CAP_NET_BIND_SERVICE
#else
#define SHIM_CAP_NET_BIND_SERVICE	SHIM_CAP_IS_ROOT
#endif

#if defined(CAP_NET_BROADCAST)
#define SHIM_CAP_NET_BROADCAST		CAP_NET_BROADCAST
#else
#define SHIM_CAP_NET_BROADCAST		SHIM_CAP_IS_ROOT
#endif

#if defined(CAP_NET_ADMIN)
#define SHIM_CAP_NET_ADMIN		CAP_NET_ADMIN
#else
#define SHIM_CAP_NET_ADMIN		SHIM_CAP_IS_ROOT
#endif

#if defined(CAP_NET_RAW)
#define SHIM_CAP_NET_RAW		CAP_NET_RAW
#else
#define SHIM_CAP_NET_RAW		SHIM_CAP_IS_ROOT
#endif

#if defined(CAP_IPC_LOCK)
#define SHIM_CAP_IPC_LOCK		CAP_IPC_LOCK
#else
#define SHIM_CAP_IPC_LOCK		SHIM_CAP_IS_ROOT
#endif

#if defined(CAP_IPC_OWNER)
#define SHIM_CAP_IPC_OWNER		CAP_IPC_OWNER
#else
#define SHIM_CAP_IPC_OWNER		SHIM_CAP_IS_ROOT
#endif

#if defined(CAP_SYS_MODULE)
#define SHIM_CAP_SYS_MODULE		CAP_SYS_MODULE
#else
#define SHIM_CAP_SYS_MODULE		SHIM_CAP_IS_ROOT
#endif

#if defined(CAP_SYS_RAWIO)
#define SHIM_CAP_SYS_RAWIO		CAP_SYS_RAWIO
#else
#define SHIM_CAP_SYS_RAWIO		SHIM_CAP_IS_ROOT
#endif

#if defined(CAP_SYS_CHROOT)
#define SHIM_CAP_SYS_CHROOT		CAP_SYS_CHROOT
#else
#define SHIM_CAP_SYS_CHROOT		SHIM_CAP_IS_ROOT
#endif

#if defined(CAP_SYS_PTRACE)
#define SHIM_CAP_SYS_PTRACE		CAP_SYS_PTRACE
#else
#define SHIM_CAP_SYS_PTRACE		SHIM_CAP_IS_ROOT
#endif

#if defined(CAP_SYS_PACCT)
#define SHIM_CAP_SYS_PACCT		CAP_SYS_PACCT
#else
#define SHIM_CAP_SYS_PACCT		SHIM_CAP_IS_ROOT
#endif

#if defined(CAP_SYS_ADMIN)
#define SHIM_CAP_SYS_ADMIN		CAP_SYS_ADMIN
#else
#define SHIM_CAP_SYS_ADMIN		SHIM_CAP_IS_ROOT
#endif

#if defined(CAP_SYS_BOOT)
#define SHIM_CAP_SYS_BOOT		CAP_SYS_BOOT
#else
#define SHIM_CAP_SYS_BOOT		SHIM_CAP_IS_ROOT
#endif

#if defined(CAP_SYS_NICE)
#define SHIM_CAP_SYS_NICE		CAP_SYS_NICE
#else
#define SHIM_CAP_SYS_NICE		SHIM_CAP_IS_ROOT
#endif

#if defined(CAP_SYS_RESOURCE)
#define SHIM_CAP_SYS_RESOURCE		CAP_SYS_RESOURCE
#else
#define SHIM_CAP_SYS_RESOURCE		SHIM_CAP_IS_ROOT
#endif

#if defined(CAP_SYS_TIME)
#define SHIM_CAP_SYS_TIME		CAP_SYS_TIME
#else
#define SHIM_CAP_SYS_TIME		SHIM_CAP_IS_ROOT
#endif

#if defined(CAP_SYS_TTY_CONFIG)
#define SHIM_CAP_SYS_TTY_CONFIG		CAP_SYS_TTY_CONFIG
#else
#define SHIM_CAP_SYS_TTY_CONFIG		SHIM_CAP_IS_ROOT
#endif

#if defined(CAP_MKNOD)
#define SHIM_CAP_MKNOD			CAP_MKNOD
#else
#define SHIM_CAP_MKNOD			SHIM_CAP_IS_ROOT
#endif

#if defined(CAP_LEASE)
#define SHIM_CAP_LEASE			CAP_LEASE
#else
#define SHIM_CAP_LEASE			SHIM_CAP_IS_ROOT
#endif

#if defined(CAP_AUDIT_WRITE)
#define SHIM_CAP_AUDIT_WRITE		CAP_AUDIT_WRITE
#else
#define SHIM_CAP_AUDIT_WRITE		SHIM_CAP_IS_ROOT
#endif

#if defined(CAP_AUDIT_CONTROL)
#define SHIM_CAP_AUDIT_CONTROL		CAP_AUDIT_CONTROL
#else
#define SHIM_CAP_AUDIT_CONTROL		SHIM_CAP_IS_ROOT
#endif

#if defined(CAP_SETFCAP)
#define SHIM_CAP_SETFCAP		CAP_SETFCAP
#else
#define SHIM_CAP_SETFCAP		SHIM_CAP_IS_ROOT
#endif

#if defined(CAP_MAC_OVERRIDE)
#define SHIM_CAP_MAC_OVERRIDE		CAP_MAC_OVERRIDE
#else
#define SHIM_CAP_MAC_OVERRIDE		SHIM_CAP_IS_ROOT
#endif

#if defined(CAP_MAC_ADMIN)
#define SHIM_CAP_MAC_ADMIN		CAP_MAC_ADMIN
#else
#define SHIM_CAP_MAC_ADMIN		SHIM_CAP_IS_ROOT
#endif

#if defined(CAP_SYSLOG)
#define SHIM_CAP_SYSLOG			CAP_SYSLOG
#else
#define SHIM_CAP_SYSLOG			SHIM_CAP_IS_ROOT
#endif

#if defined(CAP_WAKE_ALARM)
#define SHIM_CAP_WAKE_ALARM		CAP_WAKE_ALARM
#else
#define SHIM_CAP_WAKE_ALARM		SHIM_CAP_IS_ROOT
#endif

#if defined(CAP_BLOCK_SUSPEND)
#define SHIM_CAP_BLOCK_SUSPEND		CAP_BLOCK_SUSPEND
#else
#define SHIM_CAP_BLOCK_SUSPEND		SHIM_CAP_IS_ROOT
#endif

#if defined(CAP_AUDIT_READ)
#define SHIM_CAP_AUDIT_READ		CAP_AUDIT_READ
#else
#define SHIM_CAP_AUDIT_READ		SHIM_CAP_IS_ROOT
#endif

#if defined(CAP_PERFMON)
#define SHIM_CAP_PERFMON		CAP_PERFMON
#else
#define SHIM_CAP_PERFMON		SHIM_CAP_IS_ROOT
#endif

#if defined(CAP_BPF)
#define SHIM_CAP_BPF			CAP_BPF
#else
#define SHIM_CAP_BPF			SHIM_CAP_IS_ROOT
#endif


/* stress process prototype */
typedef int (*stress_func_t)(const stress_args_t *args);

/* Fast random number generator state */
typedef struct {
	uint32_t w;
	uint32_t z;
} stress_mwc_t;

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
} stress_perf_stat_t;

/* per stressor perf info */
typedef struct {
	stress_perf_stat_t	perf_stat[STRESS_PERF_MAX]; /* perf counters */
	int			perf_opened;	/* count of opened counters */
} stress_perf_t;
#endif

/* linux thermal zones */
#define	STRESS_THERMAL_ZONES	 (1)
#define STRESS_THERMAL_ZONES_MAX (31)	/* best if prime */

#if defined(STRESS_THERMAL_ZONES)
/* per stressor thermal zone info */
typedef struct stress_tz_info {
	char	*path;			/* thermal zone path */
	char 	*type;			/* thermal zone type */
	uint32_t type_instance;		/* thermal zone instance # */
	size_t	index;			/* thermal zone # index */
	struct stress_tz_info *next;	/* next thermal zone in list */
} stress_tz_info_t;

typedef struct {
	uint64_t temperature;		/* temperature in Celsius * 1000 */
} stress_tz_stat_t;

typedef struct {
	stress_tz_stat_t tz_stat[STRESS_THERMAL_ZONES_MAX];
} stress_tz_t;
#endif

/* Per stressor statistics and accounting info */
typedef struct {
	uint64_t counter;		/* number of bogo ops */
	bool counter_ready;		/* counter can be read */
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
	stress_checksum_t *checksum;	/* pointer to checksum data */
	stress_misc_stats_t misc_stats[STRESS_MISC_STATS_MAX];
} stress_stats_t;

#define	STRESS_WARN_HASH_MAX		(128)

/* The stress-ng global shared memory segment */
typedef struct {
	size_t length;					/* Size of segment */
	uint8_t	*mem_cache;				/* Shared memory cache */
	uint64_t mem_cache_size;			/* Bytes */
	uint16_t mem_cache_level;			/* 1=L1, 2=L2, 3=L3 */
	uint16_t padding1;				/* alignment padding */
	uint32_t mem_cache_ways;			/* cache ways size */
	uint64_t zero;					/* zero'd data */
	stress_mapped_t mapped;				/* mmap'd pages to help testing */
	struct {
		uint32_t hash[STRESS_WARN_HASH_MAX];	/* hash patterns */
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
	stress_tz_info_t *tz_info;			/* List of valid thermal zones */
#endif
#if defined(HAVE_ATOMIC)
	uint32_t softlockup_count;			/* Atomic counter of softlock children */
#endif
	uint8_t  str_shared[STR_SHARED_SIZE];		/* str copying buffer */
	stress_checksum_t *checksums;			/* per stressor counter checksum */
	size_t	checksums_length;			/* size of checksums mapping */
	stress_stats_t stats[0];			/* Shared statistics */
} stress_shared_t;

/* Stress test classes */
typedef struct {
	stress_class_t class;		/* Class type bit mask */
	const char *name;		/* Name of class */
} stress_class_info_t;

#define STRESSORS(MACRO)	\
	MACRO(access) 		\
	MACRO(af_alg) 		\
	MACRO(affinity) 	\
	MACRO(aio) 		\
	MACRO(aiol) 		\
	MACRO(apparmor) 	\
	MACRO(alarm)		\
	MACRO(atomic)		\
	MACRO(bad_altstack) 	\
	MACRO(bad_ioctl) 	\
	MACRO(bigheap)		\
	MACRO(bind_mount)	\
	MACRO(binderfs)		\
	MACRO(branch)		\
	MACRO(brk)		\
	MACRO(bsearch)		\
	MACRO(cache)		\
	MACRO(cap)		\
	MACRO(chattr)		\
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
	MACRO(dirmany)		\
	MACRO(dnotify)		\
	MACRO(dup)		\
	MACRO(dynlib)		\
	MACRO(efivar)		\
	MACRO(enosys)		\
	MACRO(env)		\
	MACRO(epoll)		\
	MACRO(eventfd) 		\
	MACRO(exec)		\
	MACRO(exit_group)	\
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
	MACRO(fpunch)		\
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
	MACRO(io_uring)		\
	MACRO(ipsec_mb)		\
	MACRO(itimer)		\
	MACRO(judy)		\
	MACRO(kcmp)		\
	MACRO(key)		\
	MACRO(kill)		\
	MACRO(klog)		\
	MACRO(l1cache)		\
	MACRO(landlock)		\
	MACRO(lease)		\
	MACRO(link)		\
	MACRO(list)		\
	MACRO(loadavg)		\
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
	MACRO(memhotplug)	\
	MACRO(memrate)		\
	MACRO(memthrash)	\
	MACRO(mergesort)	\
	MACRO(mincore)		\
	MACRO(misaligned)	\
	MACRO(mknod)		\
	MACRO(mlock)		\
	MACRO(mlockmany)	\
	MACRO(mmap)		\
	MACRO(mmapaddr)		\
	MACRO(mmapfixed)	\
	MACRO(mmapfork)		\
	MACRO(mmaphuge)		\
	MACRO(mmapmany)		\
	MACRO(mq)		\
	MACRO(mremap)		\
	MACRO(msg)		\
	MACRO(msync)		\
	MACRO(munmap)		\
	MACRO(nanosleep)	\
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
	MACRO(pci)		\
	MACRO(personality)	\
	MACRO(physpage)		\
	MACRO(pidfd)		\
	MACRO(ping_sock)	\
	MACRO(pipe)		\
	MACRO(pipeherd)		\
	MACRO(pkey)		\
	MACRO(poll)		\
	MACRO(prctl)		\
	MACRO(prefetch)		\
	MACRO(procfs)		\
	MACRO(pthread)		\
	MACRO(ptrace)		\
	MACRO(pty)		\
	MACRO(qsort)		\
	MACRO(quota)		\
	MACRO(radixsort)	\
	MACRO(ramfs)		\
	MACRO(rawdev)		\
	MACRO(rawpkt)		\
	MACRO(rawsock)		\
	MACRO(rawudp)		\
	MACRO(rdrand)		\
	MACRO(readahead)	\
	MACRO(reboot)		\
	MACRO(remap)		\
	MACRO(rename)		\
	MACRO(resched)		\
	MACRO(resources)	\
	MACRO(revio)		\
	MACRO(rlimit)		\
	MACRO(rmap)		\
	MACRO(rseq)		\
	MACRO(rtc)		\
	MACRO(schedpolicy)	\
	MACRO(sctp)		\
	MACRO(seal)		\
	MACRO(seccomp)		\
	MACRO(secretmem)	\
	MACRO(seek)		\
	MACRO(sem)		\
	MACRO(sem_sysv)		\
	MACRO(sendfile)		\
	MACRO(session)		\
	MACRO(set)		\
	MACRO(shellsort)	\
	MACRO(shm)		\
	MACRO(shm_sysv)		\
	MACRO(sigabrt)		\
	MACRO(sigchld)		\
	MACRO(sigfd)		\
	MACRO(sigfpe)		\
	MACRO(sigio)		\
	MACRO(signal)		\
	MACRO(signest)		\
	MACRO(sigpending)	\
	MACRO(sigpipe)		\
	MACRO(sigq)		\
	MACRO(sigrt)		\
	MACRO(sigsegv)		\
	MACRO(sigsuspend)	\
	MACRO(sigtrap)		\
	MACRO(skiplist)		\
	MACRO(sleep)		\
	MACRO(smi)		\
	MACRO(sock)		\
	MACRO(sockabuse)	\
	MACRO(sockdiag)		\
	MACRO(sockfd)		\
	MACRO(sockpair)		\
	MACRO(sockmany)		\
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
	MACRO(sysinval)		\
	MACRO(sysfs)		\
	MACRO(tee)		\
	MACRO(timer)		\
	MACRO(timerfd)		\
	MACRO(tlb_shootdown)	\
	MACRO(tmpfs)		\
	MACRO(tree)		\
	MACRO(tsc)		\
	MACRO(tsearch)		\
	MACRO(tun)		\
	MACRO(udp)		\
	MACRO(udp_flood)	\
	MACRO(unshare)		\
	MACRO(uprobe)		\
	MACRO(urandom)		\
	MACRO(userfaultfd)	\
	MACRO(utime)		\
	MACRO(vdso)		\
	MACRO(vecmath)		\
	MACRO(vecwide)		\
	MACRO(verity)		\
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
	MACRO(x86syscall)	\
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

/*
 *  Declaration of stress_*_info object
 */
#define STRESSOR_DECL(name)     \
	extern stressor_info_t stress_ ## name ## _info;

STRESSORS(STRESSOR_DECL)

/* Stress tests */
typedef enum {
	STRESS_START = -1,
	STRESSORS(STRESSOR_ENUM)
	/* STRESS_MAX must be last one */
	STRESS_MAX
} stress_id_t;

/* Command line long options */
typedef enum {
	OPT_undefined = 0,
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
	OPT_affinity_delay,
	OPT_affinity_ops,
	OPT_affinity_pin,
	OPT_affinity_rand,
	OPT_affinity_sleep,

	OPT_af_alg,
	OPT_af_alg_ops,
	OPT_af_alg_dump,

	OPT_aggressive,

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

	OPT_bad_altstack,
	OPT_bad_altstack_ops,

	OPT_bad_ioctl,
	OPT_bad_ioctl_ops,

	OPT_branch,
	OPT_branch_ops,

	OPT_brk,
	OPT_brk_ops,
	OPT_brk_mlock,
	OPT_brk_notouch,

	OPT_bsearch,
	OPT_bsearch_ops,
	OPT_bsearch_size,

	OPT_bigheap_ops,
	OPT_bigheap_growth,

	OPT_bind_mount,
	OPT_bind_mount_ops,

	OPT_binderfs,
	OPT_binderfs_ops,

	OPT_class,
	OPT_cache_ops,
	OPT_cache_prefetch,
	OPT_cache_flush,
	OPT_cache_fence,
	OPT_cache_sfence,
	OPT_cache_level,
	OPT_cache_ways,
	OPT_cache_no_affinity,

	OPT_cap,
	OPT_cap_ops,

	OPT_chattr,
	OPT_chattr_ops,

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
	OPT_dev_file,

	OPT_dev_shm,
	OPT_dev_shm_ops,

	OPT_dir,
	OPT_dir_ops,
	OPT_dir_dirs,

	OPT_dirdeep,
	OPT_dirdeep_ops,
	OPT_dirdeep_dirs,
	OPT_dirdeep_inodes,

	OPT_dirmany,
	OPT_dirmany_ops,

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

	OPT_env,
	OPT_env_ops,

	OPT_epoll,
	OPT_epoll_ops,
	OPT_epoll_port,
	OPT_epoll_domain,

	OPT_eventfd,
	OPT_eventfd_ops,
	OPT_eventfd_nonblock,

	OPT_exec,
	OPT_exec_ops,
	OPT_exec_max,

	OPT_exit_group,
	OPT_exit_group_ops,

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
	OPT_fork_vm,

	OPT_fp_error,
	OPT_fp_error_ops,

	OPT_fpunch,
	OPT_fpunch_ops,

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

	OPT_iostat,

	OPT_io_ops,

	OPT_io_uring,
	OPT_io_uring_ops,

	OPT_ipsec_mb,
	OPT_ipsec_mb_ops,
	OPT_ipsec_mb_feature,

	OPT_itimer,
	OPT_itimer_ops,
	OPT_itimer_freq,
	OPT_itimer_rand,

	OPT_judy,
	OPT_judy_ops,
	OPT_judy_size,

	OPT_kcmp,
	OPT_kcmp_ops,

	OPT_key,
	OPT_key_ops,

	OPT_kill,
	OPT_kill_ops,

	OPT_klog,
	OPT_klog_ops,

	OPT_l1cache,
	OPT_l1cache_ops,
	OPT_l1cache_line_size,
	OPT_l1cache_size,
	OPT_l1cache_sets,
	OPT_l1cache_ways,

	OPT_landlock,
	OPT_landlock_ops,

	OPT_lease,
	OPT_lease_ops,
	OPT_lease_breakers,

	OPT_link,
	OPT_link_ops,

	OPT_list,
	OPT_list_ops,
	OPT_list_method,
	OPT_list_size,

	OPT_loadavg,
	OPT_loadavg_ops,

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
	OPT_malloc_pthreads,
	OPT_malloc_threshold,
	OPT_malloc_touch,

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

	OPT_memhotplug,
	OPT_memhotplug_ops,

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
	OPT_mmap_ops,
	OPT_mmap_bytes,
	OPT_mmap_file,
	OPT_mmap_async,
	OPT_mmap_mprotect,
	OPT_mmap_osync,
	OPT_mmap_odirect,
	OPT_mmap_mmap2,

	OPT_mmapaddr,
	OPT_mmapaddr_ops,

	OPT_mmapfixed,
	OPT_mmapfixed_ops,

	OPT_mmapfork,
	OPT_mmapfork_ops,

	OPT_mmaphuge,
	OPT_mmaphuge_ops,
	OPT_mmaphuge_mmaps,

	OPT_mmapmany,
	OPT_mmapmany_ops,

	OPT_mq,
	OPT_mq_ops,
	OPT_mq_size,

	OPT_mremap,
	OPT_mremap_ops,
	OPT_mremap_bytes,
	OPT_mremap_mlock,

	OPT_msg,
	OPT_msg_ops,
	OPT_msg_types,

	OPT_msync,
	OPT_msync_bytes,
	OPT_msync_ops,

	OPT_munmap,
	OPT_munmap_ops,

	OPT_nanosleep,
	OPT_nanosleep_ops,

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

	OPT_numa,
	OPT_numa_ops,

	OPT_oomable,

	OPT_oom_pipe,
	OPT_oom_pipe_ops,

	OPT_opcode,
	OPT_opcode_ops,
	OPT_opcode_method,

	OPT_open_ops,
	OPT_open_fd,

	OPT_page_in,
	OPT_pathological,

	OPT_pci,
	OPT_pci_ops,

	OPT_perf_stats,

	OPT_personality,
	OPT_personality_ops,

	OPT_physpage,
	OPT_physpage_ops,

	OPT_pidfd,
	OPT_pidfd_ops,

	OPT_ping_sock,
	OPT_ping_sock_ops,

	OPT_pipe_ops,
	OPT_pipe_size,
	OPT_pipe_data_size,

	OPT_pipeherd,
	OPT_pipeherd_ops,
	OPT_pipeherd_yield,

	OPT_pkey,
	OPT_pkey_ops,

	OPT_poll_ops,
	OPT_poll_fds,

	OPT_prefetch,
	OPT_prefetch_ops,
	OPT_prefetch_l3_size,

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

	OPT_ramfs,
	OPT_ramfs_ops,
	OPT_ramfs_size,

	OPT_rawdev,
	OPT_rawdev_method,
	OPT_rawdev_ops,

	OPT_rawpkt,
	OPT_rawpkt_ops,
	OPT_rawpkt_port,

	OPT_rawsock,
	OPT_rawsock_ops,

	OPT_rawudp,
	OPT_rawudp_ops,
	OPT_rawudp_port,

	OPT_rdrand,
	OPT_rdrand_ops,
	OPT_rdrand_seed,

	OPT_readahead,
	OPT_readahead_ops,
	OPT_readahead_bytes,

	OPT_reboot,
	OPT_reboot_ops,

	OPT_remap,
	OPT_remap_ops,

	OPT_rename_ops,

	OPT_resched,
	OPT_resched_ops,

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

	OPT_rseq,
	OPT_rseq_ops,

	OPT_rtc,
	OPT_rtc_ops,

	OPT_sched,
	OPT_sched_prio,

	OPT_schedpolicy,
	OPT_schedpolicy_ops,

	OPT_sched_period,
	OPT_sched_runtime,
	OPT_sched_deadline,
	OPT_sched_reclaim,

	OPT_sctp,
	OPT_sctp_ops,
	OPT_sctp_domain,
	OPT_sctp_port,

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

	OPT_sem_sysv,
	OPT_sem_sysv_ops,
	OPT_sem_sysv_procs,

	OPT_session,
	OPT_session_ops,

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

	OPT_sigabrt,
	OPT_sigabrt_ops,

	OPT_sigchld,
	OPT_sigchld_ops,

	OPT_sigfd,
	OPT_sigfd_ops,

	OPT_sigfpe,
	OPT_sigfpe_ops,

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

	OPT_sock_ops,
	OPT_sock_domain,
	OPT_sock_nodelay,
	OPT_sock_opts,
	OPT_sock_port,
	OPT_sock_protocol,
	OPT_sock_type,
	OPT_sock_zerocopy,

	OPT_sockabuse,
	OPT_sockabuse_ops,

	OPT_sockdiag,
	OPT_sockdiag_ops,

	OPT_sockfd,
	OPT_sockfd_ops,
	OPT_sockfd_port,

	OPT_sockmany,
	OPT_sockmany_ops,

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
	OPT_stack_mlock,

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

	OPT_sysinval,
	OPT_sysinval_ops,

	OPT_sysfs,
	OPT_sysfs_ops,

	OPT_syslog,

	OPT_tee,
	OPT_tee_ops,

	OPT_taskset,

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

	OPT_tun,
	OPT_tun_ops,
	OPT_tun_tap,

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

	OPT_uprobe,
	OPT_uprobe_ops,

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

	OPT_vecwide,
	OPT_vecwide_ops,

	OPT_verify,

	OPT_verity,
	OPT_verity_ops,

	OPT_vfork,
	OPT_vfork_ops,
	OPT_vfork_max,
	OPT_vfork_vm,

	OPT_vforkmany,
	OPT_vforkmany_ops,
	OPT_vforkmany_vm,

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

	OPT_vmstat,

	OPT_wait,
	OPT_wait_ops,

	OPT_watchdog,
	OPT_watchdog_ops,

	OPT_wcs,
	OPT_wcs_ops,
	OPT_wcs_method,

	OPT_x86syscall,
	OPT_x86syscall_ops,
	OPT_x86syscall_func,

	OPT_xattr,
	OPT_xattr_ops,

	OPT_yield_ops,

	OPT_zero,
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

/* stress test metadata */
typedef struct {
	const stressor_info_t *info;	/* stress test info */
	const stress_id_t id;		/* stress test ID */
	const short int short_getopt;	/* getopt short option */
	const stress_op_t op;		/* ops option */
	const char *name;		/* name of stress test */
} stress_t;

/* Per stressor information */
typedef struct stress_stressor_info {
	struct stress_stressor_info *next;	/* next proc info struct in list */
	struct stress_stressor_info *prev;	/* prev proc info struct in list */
	const stress_t *stressor;	/* stressor */
	pid_t	*pids;			/* stressor process id */
	stress_stats_t **stats;		/* stressor stats info */
	int32_t started_instances;	/* count of started instances */
	int32_t num_instances;		/* number of instances per stressor */
	uint64_t bogo_ops;		/* number of bogo ops */
} stress_stressor_t;

/* Pointer to current running stressor proc info */
extern stress_stressor_t *g_stressor_current;

/* Scale lookup mapping, suffix -> scale by */
typedef struct {
	const char	ch;		/* Scaling suffix */
	const uint64_t	scale;		/* Amount to scale by */
} stress_scale_t;

/* Cache types */
typedef enum stress_cache_type {
	CACHE_TYPE_UNKNOWN = 0,		/* Unknown type */
	CACHE_TYPE_DATA,		/* D$ */
	CACHE_TYPE_INSTRUCTION,		/* I$ */
	CACHE_TYPE_UNIFIED,		/* D$ + I$ */
} stress_cache_type_t;

/* CPU cache information */
typedef struct stress_cpu_cache {
	uint64_t           size;      	/* cache size in bytes */
	uint32_t           line_size;	/* cache line size in bytes */
	uint32_t           ways;	/* cache ways */
	stress_cache_type_t type;	/* cache type */
	uint16_t           level;	/* cache level, L1, L2 etc */
} stress_cpu_cache_t;

typedef struct stress_cpu {
	stress_cpu_cache_t *caches;	/* CPU cache data */
	uint32_t       num;		/* CPU # number */
	uint32_t       cache_count;	/* CPU cache #  */
	bool           online;		/* CPU online when true */
} stress_cpu_t;

typedef struct stress_cpus {
	stress_cpu_t *cpus;		/* CPU data */
	uint32_t   count;		/* CPU count */
} stress_cpus_t;

/* Various global option settings and flags */
extern const char *g_app_name;		/* Name of application */
extern stress_shared_t *g_shared;	/* shared memory */
extern uint64_t	g_opt_timeout;		/* timeout in seconds */
extern uint64_t	g_opt_flags;		/* option flags */
extern int32_t g_opt_sequential;	/* Number of sequential stressors */
extern int32_t g_opt_parallel;		/* Number of parallel stressors */
extern volatile bool g_keep_stressing_flag; /* false to exit stressor */
extern volatile bool g_caught_sigint;	/* true if stopped by SIGINT */
extern pid_t g_pgrp;			/* proceess group leader */
extern jmp_buf g_error_env;		/* parsing error env */
extern stress_put_val_t g_put_val;	/* sync data to somewhere */

static inline bool ALWAYS_INLINE OPTIMIZE3 keep_stressing_flag(void)
{
	return g_keep_stressing_flag;
}

static inline void OPTIMIZE3 keep_stressing_set_flag(const bool setting)
{
	g_keep_stressing_flag = setting;
}

/*
 *  keep_stressing()
 *      returns true if we can keep on running a stressor
 */
static inline bool OPTIMIZE3 keep_stressing(const stress_args_t *args)
{
	return (LIKELY(g_keep_stressing_flag) &&
		LIKELY(!args->max_ops || (get_counter(args) < args->max_ops)));
}

/*
 *  stressor option value handling
 */
extern int stress_set_setting(const char *name, const stress_type_id_t type_id,
	const void *value);
extern int stress_set_setting_global(const char *name, const stress_type_id_t type_id,
	const void *value);
extern bool stress_get_setting(const char *name, void *value);
extern void stress_settings_free(void);

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
static inline void ALWAYS_INLINE stress_uint8_put(const uint8_t a)
{
	g_put_val.uint8_val = a;
}

/*
 *  uint16_put()
 *	stash a uint16_t value
 */
static inline void ALWAYS_INLINE stress_uint16_put(const uint16_t a)
{
	g_put_val.uint16_val = a;
}

/*
 *  stress_uint32_put()
 *	stash a uint32_t value
 */
static inline void ALWAYS_INLINE stress_uint32_put(const uint32_t a)
{
	g_put_val.uint32_val = a;
}

/*
 *  stress_uint64_put()
 *	stash a uint64_t value
 */
static inline void ALWAYS_INLINE stress_uint64_put(const uint64_t a)
{
	g_put_val.uint64_val = a;
}

#if defined(HAVE_INT128_T)
/*
 *  stress_uint128_put()
 *	stash a uint128_t value
 */
static inline void ALWAYS_INLINE stress_uint128_put(const __uint128_t a)
{
	g_put_val.uint128_val = a;
}
#endif

/*
 *  stress_float_put()
 *	stash a float value
 */
static inline void ALWAYS_INLINE stress_float_put(const float a)
{
	g_put_val.float_val = a;
}

/*
 *  stress_double_put()
 *	stash a double value
 */
static inline void ALWAYS_INLINE stress_double_put(const double a)
{
	g_put_val.double_val = a;
}

/*
 *  stress_long_double_put()
 *	stash a double value
 */
static inline void ALWAYS_INLINE stress_long_double_put(const long double a)
{
	g_put_val.long_double_val = a;
}

/*
 *  stress_void_ptr_put()
 *	stash a void * pointer value
 */
static inline void ALWAYS_INLINE stress_void_ptr_put(volatile void * const a)
{
	g_put_val.void_ptr_val = a;
}

/* Filenames and directories */
extern int stress_temp_filename(char *path, const size_t len,
	const char *name, const pid_t pid, const uint32_t instance,
	const uint64_t magic);
extern int stress_temp_filename_args(const stress_args_t *args, char *path,
	const size_t len, const uint64_t magic);
extern int stress_temp_dir(char *path, const size_t len,
	const char *name, const pid_t pid, const uint32_t instance);
extern int stress_temp_dir_args(const stress_args_t *args, char *path,
	const size_t len);
extern WARN_UNUSED int stress_temp_dir_mk(const char *name, const pid_t pid,
	const uint32_t instance);
extern WARN_UNUSED int stress_temp_dir_mk_args(const stress_args_t *args);
extern int stress_temp_dir_rm(const char *name, const pid_t pid,
	const uint32_t instance);
extern int stress_temp_dir_rm_args(const stress_args_t *args);
extern void stress_cwd_readwriteable(void);

extern const char *stress_signal_name(const int signum);
extern const char *stress_strsignal(const int signum);

#if defined(STRESS_ARCH_X86)
/*
 *  shim_clflush()
 *	flush a cache line
 */
static inline void ALWAYS_INLINE shim_clflush(volatile void *ptr)
{
	asm volatile("clflush %0" : "+m" (*(volatile char *)ptr));
}
#else
#define shim_clflush(ptr)	do { } while (0) /* No-op */
#endif

/*
 *  shim_mfence()
 *	serializing memory fence
 */
static inline void ALWAYS_INLINE shim_mfence(void)
{
#if NEED_GNUC(4, 2, 0) &&	\
    !defined(__PCC__)
	__sync_synchronize();
#else
#if defined(STRESS_ARCH_X86)
	asm volatile("mfence" : : : "memory");
#else
	/* Other arches not yet implemented for older GCC flavours */
#endif
#endif
}

/* Fast random numbers */
extern uint32_t stress_mwc32(void);
extern uint64_t stress_mwc64(void);
extern uint16_t stress_mwc16(void);
extern uint8_t stress_mwc8(void);
extern uint8_t stress_mwc1(void);
extern void stress_mwc_seed(const uint32_t w, const uint32_t z);
extern void stress_mwc_reseed(void);

/* Time handling */
extern WARN_UNUSED double stress_timeval_to_double(const struct timeval *tv);
extern WARN_UNUSED double stress_time_now(void);
extern const char *stress_duration_to_str(const double duration);

/* Perf statistics */
#if defined(STRESS_PERF_STATS)
extern int stress_perf_open(stress_perf_t *sp);
extern int stress_perf_enable(stress_perf_t *sp);
extern int stress_perf_disable(stress_perf_t *sp);
extern int stress_perf_close(stress_perf_t *sp);
extern bool stress_perf_stat_succeeded(const stress_perf_t *sp);
extern void stress_perf_stat_dump(FILE *yaml, stress_stressor_t *procs_head,
	const double duration);
extern void stress_perf_init(void);
#endif

/* CPU helpers */
extern WARN_UNUSED bool stress_cpu_is_x86(void);

typedef int stress_oomable_child_func_t(const stress_args_t *args, void *context);

#define	STRESS_OOMABLE_NORMAL	(0x00000000)		/* Normal oomability */
#define STRESS_OOMABLE_DROP_CAP	(0x00000001)		/* Drop capabilities */
#define STRESS_OOMABLE_QUIET	(0x00000002)		/* Don't report activity */

/* Misc helpers */
extern size_t stress_mk_filename(char *fullname, const size_t fullname_len,
	const char *pathname, const char *filename);
extern void stress_set_oom_adjustment(const char *name, const bool killable);
extern WARN_UNUSED bool stress_process_oomed(const pid_t pid);
extern WARN_UNUSED int stress_oomable_child(const stress_args_t *args,
	void *context, stress_oomable_child_func_t func, const int flag);
extern WARN_UNUSED int stress_set_sched(const pid_t pid, const int sched,
	const int sched_priority, const bool quiet);
extern WARN_UNUSED int stress_set_deadline_sched(const pid_t, const uint64_t period,
	const uint64_t runtime, const uint64_t deadline, const bool quiet);
extern int sched_settings_apply(const bool quiet);
extern const char *stress_get_sched_name(const int sched);
extern void stress_set_iopriority(const int32_t class, const int32_t level);
extern void stress_set_proc_name_init(int argc, char *argv[], char *envp[]);
extern void stress_set_proc_name(const char *name);
extern void stress_set_proc_state(const char *name, const int state);
extern WARN_UNUSED int stress_get_unused_uid(uid_t *uid);
extern void NORETURN MLOCKED_TEXT stress_sig_handler_exit(int signum);

/* Memory locking */
extern int stress_mlock_region(const void *addr_start, const void *addr_end);

/* Argument parsing and range checking */
extern WARN_UNUSED uint64_t stress_get_uint64(const char *const str);
extern WARN_UNUSED uint64_t stress_get_uint64_scale(const char *const str,
	const stress_scale_t scales[], const char *const msg);
extern WARN_UNUSED uint64_t stress_get_uint64_percent(const char *const str,
	const uint32_t instances, const uint64_t max, const char *const errmsg);
extern WARN_UNUSED uint64_t stress_get_uint64_byte(const char *const str);
extern WARN_UNUSED uint64_t stress_get_uint64_byte_memory(
	const char *const str, const uint32_t instances);
extern WARN_UNUSED uint64_t stress_get_uint64_byte_filesystem(
	const char *const str, const uint32_t instances);
extern WARN_UNUSED uint64_t stress_get_uint64_time(const char *const str);
extern void stress_check_value(const char *const msg, const int val);
extern void stress_check_range(const char *const opt, const uint64_t val,
	const uint64_t lo, const uint64_t hi);
extern void stress_check_range_bytes(const char *const opt,
	const uint64_t val, const uint64_t lo, const uint64_t hi);
extern WARN_UNUSED int stress_set_cpu_affinity(const char *arg);
extern WARN_UNUSED uint32_t stress_get_uint32(const char *const str);
extern WARN_UNUSED int32_t  stress_get_int32(const char *const str);
extern WARN_UNUSED int32_t  stress_get_opt_sched(const char *const str);
extern WARN_UNUSED int32_t  stress_get_opt_ionice_class(const char *const str);

/* Misc helper funcs */
extern WARN_UNUSED size_t stress_sig_stack_size(void);
extern WARN_UNUSED size_t stress_min_sig_stack_size(void);
extern WARN_UNUSED size_t stress_min_pthread_stack_size(void);

#define STRESS_SIGSTKSZ		(stress_sig_stack_size())
#define STRESS_MINSIGSTKSZ	(stress_min_sig_stack_size())

extern void stress_shared_unmap(void);
extern void stress_log_system_mem_info(void);
extern WARN_UNUSED char *stress_munge_underscore(const char *str);
extern size_t stress_get_pagesize(void);
extern WARN_UNUSED int32_t stress_get_processors_online(void);
extern WARN_UNUSED int32_t stress_get_processors_configured(void);
extern WARN_UNUSED int32_t stress_get_ticks_per_second(void);
extern WARN_UNUSED ssize_t stress_get_stack_direction(void);
extern WARN_UNUSED void *stress_get_stack_top(void *start, size_t size);
extern void stress_get_memlimits(size_t *shmall, size_t *freemem,
	size_t *totalmem, size_t *freeswap);
extern WARN_UNUSED int stress_get_load_avg(double *min1, double *min5,
	double *min15);
extern void stress_set_max_limits(void);
extern void stress_parent_died_alarm(void);
extern int stress_process_dumpable(const bool dumpable);
extern int stress_set_timer_slack_ns(const char *opt);
extern void stress_set_timer_slack(void);
extern WARN_UNUSED int stress_set_temp_path(const char *path);
extern WARN_UNUSED const char *stress_get_temp_path(void);
extern void stress_temp_path_free(void);
extern void stress_strnrnd(char *str, const size_t len);
extern void stress_get_cache_size(uint64_t *l2, uint64_t *l3);
extern WARN_UNUSED unsigned int stress_get_cpu(void);
extern WARN_UNUSED const char *stress_get_compiler(void);
extern WARN_UNUSED const char *stress_get_uname_info(void);
extern WARN_UNUSED int stress_cache_alloc(const char *name);
extern void stress_cache_free(void);
extern void stress_ignite_cpu_start(void);
extern void stress_ignite_cpu_stop(void);
extern ssize_t system_write(const char *path, const char *buf, const size_t buf_len);
extern WARN_UNUSED int stress_set_nonblock(const int fd);
extern WARN_UNUSED ssize_t system_read(const char *path, char *buf,
	const size_t buf_len);
extern WARN_UNUSED bool stress_is_prime64(const uint64_t n);
extern WARN_UNUSED uint64_t stress_get_prime64(const uint64_t n);
extern WARN_UNUSED size_t stress_get_file_limit(void);
extern WARN_UNUSED size_t stress_get_max_file_limit(void);
extern WARN_UNUSED int stress_get_bad_fd(void);
extern void stress_vmstat_start(void);
extern void stress_vmstat_stop(void);
extern WARN_UNUSED int stress_sigaltstack(void *stack, const size_t size);
extern WARN_UNUSED int stress_sighandler(const char *name, const int signum,
	void (*handler)(int), struct sigaction *orig_action);
extern int stress_sighandler_default(const int signum);
extern void stress_handle_stop_stressing(int dummy);
extern WARN_UNUSED int stress_sig_stop_stressing(const char *name,
	const int sig);
extern int stress_sigrestore(const char *name, const int signum,
	struct sigaction *orig_action);
extern WARN_UNUSED int stress_not_implemented(const stress_args_t *args);
extern WARN_UNUSED size_t stress_probe_max_pipe_size(void);
extern WARN_UNUSED void *stress_align_address(const void *addr,
	const size_t alignment);
extern void stress_mmap_set(uint8_t *buf, const size_t sz,
	const size_t page_size);
extern WARN_UNUSED int stress_mmap_check(uint8_t *buf, const size_t sz,
	const size_t page_size);
extern WARN_UNUSED uint64_t stress_get_phys_mem_size(void);
extern WARN_UNUSED uint64_t stress_get_filesystem_size(void);
extern WARN_UNUSED ssize_t stress_read_buffer(int, void*, ssize_t, bool);
extern WARN_UNUSED ssize_t stress_write_buffer(int, void*, ssize_t, bool);
extern WARN_UNUSED uint64_t stress_get_filesystem_available_inodes(void);
extern WARN_UNUSED int stress_kernel_release(const int major,
	const int minor, const int patchlevel);
extern WARN_UNUSED int stress_get_kernel_release(void);
extern char *stress_uint64_to_str(char *str, size_t len, const uint64_t val);
extern WARN_UNUSED int stress_drop_capabilities(const char *name);
extern WARN_UNUSED bool stress_is_dot_filename(const char *name);
extern WARN_UNUSED char *stress_const_optdup(const char *opt);
extern size_t stress_text_addr(char **start, char **end);
extern WARN_UNUSED bool stress_check_capability(const int capability);
extern WARN_UNUSED bool stress_sigalrm_pending(void);
extern WARN_UNUSED bool stress_is_dev_tty(const int fd);
extern WARN_UNUSED stress_hash_table_t *stress_hash_create(const size_t n);
extern stress_hash_t *stress_hash_add(stress_hash_table_t *hash_table,
	const char *str);
extern WARN_UNUSED stress_hash_t *stress_hash_get(
	stress_hash_table_t *hash_table, const char *str);
extern void stress_hash_delete(stress_hash_table_t *hash_table);
extern WARN_UNUSED int stress_try_open(const stress_args_t *args,
	const char *path, const int flags, const unsigned long timeout_ns);
extern WARN_UNUSED int stress_open_timeout(const char *name,
        const char *path, const int flags, const unsigned long timeout_ns);
extern WARN_UNUSED uint32_t stress_hash_jenkin(const uint8_t *data,
	const size_t len);
extern WARN_UNUSED uint32_t stress_hash_pjw(const char *str);
extern WARN_UNUSED uint32_t stress_hash_djb2a(const char *str);
extern WARN_UNUSED uint32_t stress_hash_fnv1a(const char *str);
extern WARN_UNUSED uint32_t stress_hash_sdbm(const char *str);
extern WARN_UNUSED uint32_t stress_hash_nhash(const char *str);
extern WARN_UNUSED uint32_t stress_hash_murmur3_32(const uint8_t* key, size_t len, uint32_t seed);
extern void stress_dirent_list_free(struct dirent **dlist, const int n);
extern WARN_UNUSED int stress_dirent_list_prune(struct dirent **dlist, const int n);
extern WARN_UNUSED uint16_t stress_ipv4_checksum(uint16_t *ptr, const size_t n);
extern int stress_read_fdinfo(const pid_t pid, const int fd);
extern WARN_UNUSED pid_t stress_get_unused_pid_racy(const bool fork_test);
extern WARN_UNUSED size_t stress_hostname_length(void);
extern WARN_UNUSED int32_t stress_set_vmstat(const char *const str);
extern WARN_UNUSED int32_t stress_set_thermalstat(const char *const str);
extern WARN_UNUSED int32_t stress_set_iostat(const char *const str);
extern void stress_misc_stats_set(stress_misc_stats_t *misc_stats,
	const int idx, const char *description, const double value);
extern WARN_UNUSED int stress_tty_width(void);
extern WARN_UNUSED size_t stress_get_extents(const int fd);
extern WARN_UNUSED bool stress_redo_fork(const int err);
extern int stress_killpid(const pid_t pid);

extern void stress_smart_start(void);
extern void stress_smart_stop(void);

extern int stress_ftrace_start(void);
extern void stress_ftrace_stop(void);
extern void stress_ftrace_free(void);
extern void stress_ftrace_add_pid(const pid_t pid);

extern int stress_module_load(const char *name, const char *alias,
	const char *options, bool *already_loaded);
extern int stress_module_unload(const char *name, const char *alias,
	const bool already_loaded);

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
	return EXIT_FAILURE;	/* cppcheck-suppress ConfigurationNotChecked */
}

/*
 *  Stack aligning for clone() system calls
 *	align to nearest 16 bytes for aarch64 et al,
 *	assumes we have enough slop to do this
 */
static inline WARN_UNUSED ALWAYS_INLINE void *stress_align_stack(void *stack_top)
{
	return (void *)((uintptr_t)stack_top & ~(uintptr_t)0xf);
}

/*
 *  stress_warn_once hashes the current filename and line where
 *  the macro is used and returns true if it's never been called
 *  there before across all threads and child processes
 */
extern WARN_UNUSED bool stress_warn_once_hash(const char *filename, const int line);
#define stress_warn_once()	stress_warn_once_hash(__FILE__, __LINE__)

/* Jobfile parsing */
extern WARN_UNUSED int stress_parse_jobfile(int argc, char **argv,
	const char *jobfile);
extern WARN_UNUSED int stress_parse_opts(int argc, char **argv,
	const bool jobmode);

/* Memory tweaking */
extern int stress_madvise_random(void *addr, const size_t length);
extern void stress_madvise_pid_all_pages(const pid_t pid, const int advise);
extern int stress_mincore_touch_pages(void *buf, const size_t buf_len);
extern int stress_mincore_touch_pages_interruptible(void *buf,
	const size_t buf_len);

/* Mounts */
extern void stress_mount_free(char *mnts[], const int n);
extern WARN_UNUSED int stress_mount_get(char *mnts[], const int max);

/* Thermal Zones */
#if defined(STRESS_THERMAL_ZONES)
extern int stress_tz_init(stress_tz_info_t **tz_info_list);
extern void stress_tz_free(stress_tz_info_t **tz_info_list);
extern int stress_tz_get_temperatures(stress_tz_info_t **tz_info_list,
	stress_tz_t *tz);
extern void stress_tz_dump(FILE *yaml, stress_stressor_t *procs_head);
#endif

/* Network helpers */

#define NET_ADDR_ANY		(0)
#define NET_ADDR_LOOPBACK	(1)

extern void stress_set_net_port(const char *optname, const char *opt,
	const int min_port, const int max_port, int *port);
extern WARN_UNUSED int stress_set_net_domain(const int domain_mask,
	const char *name, const char *domain_name, int *domain);
extern void stress_set_sockaddr(const char *name, const uint32_t instance,
	const pid_t ppid, const int domain, const int port,
	struct sockaddr **sockaddr, socklen_t *len, const int net_addr);
extern void stress_set_sockaddr_port(const int domain, const int port,
	struct sockaddr *sockaddr);

/* CPU caches */
extern stress_cpus_t *stress_get_all_cpu_cache_details(void);
extern uint16_t stress_get_max_cache_level(const stress_cpus_t *cpus);
extern stress_cpu_cache_t *stress_get_cpu_cache(const stress_cpus_t *cpus,
	const uint16_t cache_level);
extern void stress_free_cpu_caches(stress_cpus_t *cpus);

/* CPU thrashing start/stop helpers */
extern int  stress_thrash_start(void);
extern void stress_thrash_stop(void);

/* Used to set options for specific stressors */
extern void stress_adjust_pthread_max(const uint64_t max);
extern void stress_adjust_sleep_max(const uint64_t max);

/* Enable/disable stack smashing error message */
extern void stress_set_stack_smash_check_flag(const bool flag);

/* loff_t and off64_t porting shims */
#if defined(HAVE_LOFF_T)
typedef	loff_t		shim_loff_t;
#elif defined(HAVE_OFF_T)
typedef	off_t		shim_loff_t;
#else
typedef long		shim_loff_t;
#endif

#if defined(HAVE_OFF64_T)
typedef off64_t		shim_off64_t;
#else
typedef uint64_t	shim_off64_t;
#endif

/* clone3 clone args */
struct shim_clone_args {
	uint64_t flags;			/* Flags bit mask */
	uint64_t pidfd;			/* (pid_t *) PID fd */
	uint64_t child_tid;		/* (pid_t *) child TID */
	uint64_t parent_tid;		/* (pid_t *) parent TID */
	uint64_t exit_signal;		/* exit signal */
	uint64_t stack;			/* lowest address of stack */
	uint64_t stack_size;		/* size of stack */
	uint64_t tls;			/* tls address */
};

struct shim_getcpu_cache {
        unsigned long blob[128 / sizeof(long)];
};

/* futex2 waitv shim */
struct shim_futex_waitv {
	uint64_t val;
	uint64_t uaddr;
	uint32_t flags;
	uint32_t __reserved;
};

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
	uint32_t sched_util_min;	/* utilization hint, min */
	uint32_t sched_util_max;	/* utilization hint, max */
};

#if defined(HAVE_TERMIOS_H)

#define HAVE_SHIM_TERMIOS2
/* shim_speed_t */
typedef unsigned int shim_speed_t;

/* shim termios2 */
struct shim_termios2 {
	tcflag_t c_iflag;		/* input mode flags */
	tcflag_t c_oflag;		/* output mode flags */
	tcflag_t c_cflag;		/* control mode flags */
	tcflag_t c_lflag;		/* local mode flags */
	cc_t c_line;			/* line discipline */
	cc_t c_cc[NCCS];		/* control characters */
	shim_speed_t c_ispeed;		/* input speed */
	shim_speed_t c_ospeed;		/* output speed */
};
#endif

/* shim'd STATX flags */
#define SHIM_STATX_TYPE			(0x00000001U)
#define SHIM_STATX_MODE			(0x00000002U)
#define SHIM_STATX_NLINK		(0x00000004U)
#define SHIM_STATX_UID			(0x00000008U)
#define SHIM_STATX_GID			(0x00000010U)
#define SHIM_STATX_ATIME		(0x00000020U)
#define SHIM_STATX_MTIME		(0x00000040U)
#define SHIM_STATX_CTIME		(0x00000080U)
#define SHIM_STATX_INO			(0x00000100U)
#define SHIM_STATX_SIZE			(0x00000200U)
#define SHIM_STATX_BLOCKS		(0x00000400U)
#define SHIM_STATX_BASIC_STATS		(0x000007ffU)
#define SHIM_STATX_BTIME		(0x00000800U)
#define SHIM_STATX_ALL			(0x00000fffU)

struct shim_statx_timestamp {
	int64_t	tv_sec;
	int32_t	tv_nsec;
	int32_t	__reserved;
};

/* shim'd statx */
struct shim_statx {
	uint32_t stx_mask;
	uint32_t stx_blksize;
	uint64_t stx_attributes;
	uint32_t stx_nlink;
	uint32_t stx_uid;
	uint32_t stx_gid;
	uint16_t stx_mode;
	uint16_t __spare0[1];
	uint64_t stx_ino;
	uint64_t stx_size;
	uint64_t stx_blocks;
	uint64_t __spare1[1];
	struct shim_statx_timestamp stx_atime;
	struct shim_statx_timestamp stx_btime;
	struct shim_statx_timestamp stx_ctime;
	struct shim_statx_timestamp stx_mtime;
	uint32_t stx_rdev_major;
	uint32_t stx_rdev_minor;
	uint32_t stx_dev_major;
	uint32_t stx_dev_minor;
	uint64_t __spare2[14];
};

/* old ustat struct */
struct shim_ustat {
#if defined(HAVE_DADDR_T)
	daddr_t	f_tfree;
#else
	long	f_tfree;
#endif
	ino_t	f_tinode;
	char	f_fname[6];
	char	f_fpack[6];
};

/* waitid/pidfd shims */
#if !defined(P_PIDFD)
#define P_PIDFD		(3)
#endif

#if defined(HAVE_SYS_TIMEX_H)
#define shim_timex	timex
#else
struct shim_timex {
	char data[128];
};
#endif

/* linux/pid.h */
enum shim_pid_type {
	SHIM_PIDTYPE_PID,
	SHIM_PIDTYPE_TGID,
	SHIM_PIDTYPE_PGID,
	SHIM_PIDTYPE_SID,
	SHIM_PIDTYPE_MAX,
};

/*
 *  shim_unconstify_ptr()
 *      some older system calls require non-const void *
 *      or caddr_t args, so we need to unconstify them
 */
#if defined(__sun__)
static inline void *shim_unconstify_ptr(const void *ptr)
{
	void *unconst_ptr = (void *)ptr;

	return unconst_ptr;
}
#else
#define shim_unconstify_ptr(ptr)        (ptr)
#endif

extern int shim_arch_prctl(int code, unsigned long addr);
extern int shim_brk(void *addr);
extern int shim_cacheflush(char *addr, int nbytes, int cache);
extern void shim_flush_icache(void *begin, void *end);
extern int shim_clock_adjtime(clockid_t clk_id, struct shim_timex *tx);
extern int shim_clock_getres(clockid_t clk_id, struct timespec *res);
extern int shim_clock_gettime(clockid_t clk_id, struct timespec *tp);
extern int shim_clock_settime(clockid_t clk_id, struct timespec *tp);
extern int sys_clone3(struct shim_clone_args *cl_args, size_t size);
extern int shim_close_range(unsigned int fd, unsigned int max_fd, unsigned int flags);
extern ssize_t shim_copy_file_range(int fd_in, shim_loff_t *off_in,
	int fd_out, shim_loff_t *off_out, size_t len, unsigned int flags);
extern int shim_dup3(int oldfd, int newfd, int flags);
extern int shim_execveat(int dir_fd, const char *pathname, char *const argv[],
	char *const envp[], int flags);
extern void shim_exit_group(int status);
extern int shim_fallocate(int fd, int mode, off_t offset, off_t len);
extern int shim_fdatasync(int fd);
extern ssize_t shim_fgetxattr(int fd, const char *name, void *value, size_t size);
extern ssize_t shim_flistxattr(int fd, char *list, size_t size);
extern int shim_fsconfig(int fd, unsigned int cmd, const char *key,
	const void *value, int aux);
extern int shim_fsetxattr(int fd, const char *name, const void *value,
	size_t size, int flags);
extern int shim_fsmount(int fd, unsigned int flags, unsigned int ms_flags);
extern int shim_fsopen(const char *fsname, unsigned int flags);
extern int shim_fsync(int fd);
extern int shim_futex_wait(const void *futex, const int val,
	const struct timespec *timeout);
extern int shim_futex_wake(const void *futex, const int n);
extern long shim_getcpu(unsigned *cpu, unsigned *node, void *tcache);
extern int shim_getdents(unsigned int fd, struct shim_linux_dirent *dirp,
	unsigned int count);
extern int shim_getdents64(unsigned int fd, struct shim_linux_dirent64 *dirp,
	unsigned int count);
extern char *shim_getlogin(void);
extern int shim_get_mempolicy(int *mode, unsigned long *nodemask,
	unsigned long maxnode, void *addr, unsigned long flags);
extern int shim_getrandom(void *buff, size_t buflen, unsigned int flags);
extern int shim_getrusage(int who, struct rusage *usage);
extern int shim_gettid(void);
extern ssize_t shim_getxattr(const char *path, const char *name,
	void *value, size_t size);
extern int shim_ioprio_set(int which, int who, int ioprio);
extern int shim_ioprio_get(int which, int who);
extern long shim_kcmp(pid_t pid1, pid_t pid2, int type, unsigned long idx1,
	unsigned long idx2);
extern int shim_klogctl(int type, char *bufp, int len);
extern ssize_t shim_lgetxattr(const char *path, const char *name, void *value,
	size_t size);
extern ssize_t shim_llistxattr(const char *path, char *list, size_t size);
extern int shim_lsetxattr(const char *path, const char *name,
	const void *value, size_t size, int flags);
extern ssize_t shim_listxattr(const char *path, char *list, size_t size);
extern int shim_lookup_dcookie(uint64_t cookie, char *buffer, size_t len);
extern int shim_lremovexattr(const char *path, const char *name);
extern int shim_madvise(void *addr, size_t length, int advice);
extern long shim_mbind(void *addr, unsigned long len,
	int mode, const unsigned long *nodemask,
	unsigned long maxnode, unsigned flags);
extern int shim_membarrier(int cmd, int flags, int cpu_id);
extern int shim_memfd_create(const char *name, unsigned int flags);
extern int shim_memfd_secret(unsigned long flags);
extern long shim_migrate_pages(int pid, unsigned long maxnode,
	const unsigned long *old_nodes, const unsigned long *new_nodes);
extern int shim_mincore(void *addr, size_t length, unsigned char *vec);
extern int shim_mlock(const void *addr, size_t len);
extern int shim_mlock2(const void *addr, size_t len, int flags);
extern int shim_mlockall(int flags);
extern int shim_move_mount(int from_dfd, const char *from_pathname,
	int to_dfd, const char *to_pathname, unsigned int flags);
extern long shim_move_pages(int pid, unsigned long count,
	void **pages, const int *nodes, int *status, int flags);
extern int shim_msync(void *addr, size_t length, int flags);
extern int shim_munlock(const void *addr, size_t len);
extern int shim_munlockall(void);
extern int shim_modify_ldt(int func, void *ptr, unsigned long bytecount);
extern int shim_nanosleep_uint64(uint64_t usec);
extern int shim_nice(int inc);
extern time_t shim_time(time_t *tloc);
extern int shim_gettimeofday(struct timeval *tv, struct timezone *tz);
extern int shim_pidfd_getfd(int pidfd, int targetfd, unsigned int flags);
extern int shim_pidfd_open(pid_t pid, unsigned int flags);
extern int shim_pidfd_send_signal(int pidfd, int sig, siginfo_t *info,
	unsigned int flags);
extern int shim_pkey_alloc(unsigned long flags, unsigned long access_rights);
extern int shim_pkey_free(int pkey);
extern int shim_pkey_mprotect(void *addr, size_t len, int prot, int pkey);
extern int shim_pkey_get(int pkey);
extern int shim_pkey_set(int pkey, unsigned int rights);
extern ssize_t shim_process_madvise(int pidfd, const struct iovec *iovec,
	unsigned long vlen, int advice, unsigned int flags);
extern int shim_process_mrelease(int pidfd, unsigned int flags);
extern int shim_quotactl_fd(unsigned int fd, unsigned int cmd, int id, void *addr);
extern ssize_t shim_readlink(const char *pathname, char *buf, size_t bufsiz);
extern int shim_reboot(int magic, int magic2, int cmd, void *arg);
extern int shim_removexattr(const char *path, const char *name);
extern void *shim_sbrk(intptr_t increment);
extern int shim_sched_getattr(pid_t pid, struct shim_sched_attr *attr,
	unsigned int size, unsigned int flags);
extern int shim_sched_setattr(pid_t pid, struct shim_sched_attr *attr,
	unsigned int flags);
extern long shim_sgetmask(void);
extern long shim_ssetmask(long newmask);
extern int shim_stime(const time_t *t);
extern int shim_sched_yield(void);
extern int shim_set_mempolicy(int mode, unsigned long *nodemask,
	unsigned long maxnode);
extern int shim_seccomp(unsigned int operation, unsigned int flags, void *args);
extern ssize_t shim_statx(int dfd, const char *filename, unsigned int flags,
	unsigned int mask, struct shim_statx *buffer);
extern int shim_setxattr(const char *path, const char *name, const void *value,
	size_t size, int flags);
extern size_t shim_strlcat(char *dst, const char *src, size_t len);
extern size_t shim_strlcpy(char *dst, const char *src, size_t len);
extern int shim_sync_file_range(int fd, shim_off64_t offset,
	shim_off64_t nbytes, unsigned int flags);
extern int shim_sysfs(int option, ...);
extern int shim_tgkill(int tgid, int tid, int sig);
extern int shim_tkill(int tid, int sig);
extern int shim_fremovexattr(int fd, const char *name);
extern int shim_unshare(int flags);
extern int shim_userfaultfd(int flags);
extern int shim_usleep(uint64_t usec);
extern int shim_usleep_interruptible(uint64_t usec);
extern int shim_ustat(dev_t dev, struct shim_ustat *ubuf);
extern int shim_vhangup(void);
extern pid_t shim_waitpid(pid_t pid, int *wstatus, int options);
extern pid_t shim_wait(int *wstatus);
extern pid_t shim_wait3(int *wstatus, int options, struct rusage *rusage);
extern pid_t shim_wait4(pid_t pid, int *wstatus, int options, struct rusage *rusage);
extern int shim_futex_waitv(struct shim_futex_waitv *waiters, unsigned int nr_futexes,
	unsigned int flags, struct timespec *timeout, clockid_t clockid);
#endif
