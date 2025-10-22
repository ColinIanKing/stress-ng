/*
 * Copyright (C) 2022-2025 Colin Ian King.
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
#include "core-arch.h"
#include "core-cpu-cache.h"
#include "core-builtin.h"
#include "core-io-priority.h"

#include <math.h>
#include <sched.h>
#include <time.h>
#if defined(HAVE_SYS_FILE_H)
#include <sys/file.h>
#endif
#include <sys/times.h>

#define SYSCALL_METHOD_ALL	(0)
#define SYSCALL_METHOD_FAST10	(1)
#define SYSCALL_METHOD_FAST25	(2)
#define SYSCALL_METHOD_FAST50	(3)
#define SYSCALL_METHOD_FAST75	(4)
#define SYSCALL_METHOD_FAST90	(5)
#define SYSCALL_METHOD_GEOMEAN1 (11)
#define SYSCALL_METHOD_GEOMEAN2 (12)
#define SYSCALL_METHOD_GEOMEAN3 (13)

#define NUMA_LONG_BITS	(sizeof(unsigned long int) * 8)

/* 1 day in nanoseconds */
#define SYSCALL_DAY_NS		(8.64E13)

#if defined(HAVE_LINUX_AUDIT_H)
#include <linux/audit.h>
#endif

#if defined(HAVE_LINUX_FILTER_H)
#include <linux/filter.h>
#endif

#if defined(HAVE_LINUX_FUTEX_H)
#include <linux/futex.h>
#endif

#if defined(HAVE_LINUX_IO_URING_H)
#include <linux/io_uring.h>
#endif

#if defined(HAVE_LINUX_MEMBARRIER_H)
#include <linux/membarrier.h>
#endif

#if defined(HAVE_LINUX_MEMPOLICY_H)
#include <linux/mempolicy.h>
#endif

#if defined(HAVE_LINUX_RSEQ_H)
#include <linux/rseq.h>
#endif

#if defined(HAVE_LINUX_SECCOMP_H)
#include <linux/seccomp.h>
#endif

#if defined(HAVE_LINUX_SYSCTL_H)
#include <linux/sysctl.h>
#endif

#if defined(HAVE_LINUX_USERFAULTFD_H)
#include <linux/userfaultfd.h>
#endif

#if defined(HAVE_MODIFY_LDT)
#include <asm/ldt.h>
#endif

#if defined(HAVE_KEYUTILS_H)
#include <keyutils.h>
#endif

#if defined(HAVE_LIBAIO_H)
#include <libaio.h>
#endif

#if defined(HAVE_MQUEUE_H)
#include <mqueue.h>
#endif

#include <netinet/in.h>

#if defined(HAVE_POLL_H)
#include <poll.h>
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

#if defined(HAVE_SYS_INOTIFY_H)
#include <sys/inotify.h>
#endif

#if defined(HAVE_SYS_IO_H)
#include <sys/io.h>
#endif

#if defined(HAVE_SYS_UN_H)
#include <sys/un.h>
#endif

#if defined(HAVE_SYS_IPC_H)
#include <sys/ipc.h>
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

#if defined(HAVE_SYS_QUOTA_H)
#include <sys/quota.h>
#endif

#if defined(HAVE_SEM_SYSV)
#include <sys/sem.h>
#endif

#if defined(HAVE_SYS_SENDFILE_H)
#include <sys/sendfile.h>
#endif

#if defined(HAVE_SYS_SELECT_H)
#include <sys/select.h>
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

#if defined(HAVE_SYS_TIMERFD_H)
#include <sys/timerfd.h>
#endif

#if defined(HAVE_SYS_TIMEX_H)
#include <sys/timex.h>
#endif

#if defined(HAVE_SYS_UIO_H)
#include <sys/uio.h>
#endif

#if defined(HAVE_SYS_UTSNAME_H)
#include <sys/utsname.h>
#endif

#if defined(HAVE_SYS_XATTR_H)
#include <sys/xattr.h>
#undef HAVE_ATTR_XATTR_H
#elif defined(HAVE_ATTR_XATTR_H)
#include <attr/xattr.h>
#endif

#if defined(HAVE_UTIME_H)
#include <utime.h>
#endif

#if defined(__NR_ioprio_get) &&	\
    defined(HAVE_SYSCALL)
#define HAVE_IOPRIO_GET
#endif

#if defined(__NR_ioprio_set) && \
    defined(HAVE_SYSCALL)
#define HAVE_IOPRIO_SET
#endif

typedef struct {
	const char *opt;	/* method option string */
	const int method;	/* SYSCALL_METHOD_* value */
} syscall_method_t;

typedef int (*syscall_func_t)(void);

typedef struct {
	const syscall_func_t syscall;	/* syscall test function */
	const char *name;		/* name of system call */
} syscall_t;

/* mq message type */
typedef struct {
        uint64_t        value;
} syscall_mq_msg_t;

/*
 *  shared system call timings, error returns etc to allow a system
 *  call information to be shared back to a parent from a child
 */
typedef struct {
	uint64_t t1;			/* start time */
	uint64_t t2;			/* end time */
	uint64_t sig_t;			/* signal time */
	int syscall_errno;		/* errno from syscall */
	int syscall_ret;		/* return value from syscall */
	volatile bool t_set;		/* true of sig_t set */
} syscall_shared_info_t;

/*
 *  system call statistics for each exercised system call
 */
typedef struct {
	uint64_t count;			/* # times called */
	double total_duration;		/* syscall duration in ns */
	double average_duration;	/* average syscall duration */
	uint64_t min_duration;		/* syscall min duration in ns */
	uint64_t max_duration;		/* syscall max duration in ns */
	uint64_t max_test_duration;	/* maximum test duration */
	int syscall_errno;		/* syscall errno */
	bool ignore;			/* true if too slow */
	bool succeed;			/* syscall returned OK */
} syscall_stats_t;

#if (defined(HAVE_CLOCK_ADJTIME) &&	\
     defined(HAVE_SYS_TIMEX_H) &&	\
     defined(HAVE_TIMEX)) ||		\
     defined(HAVE_CLOCK_GETRES) ||	\
     defined(HAVE_CLOCK_GETTIME) ||	\
     defined(HAVE_CLOCK_SETTIME)
/*
 *  various clock types
 */
static const int clocks[] = {
#if defined(CLOCK_REALTIME)
	CLOCK_REALTIME,
#endif
#if defined(CLOCK_REALTIME_COARSE)
	CLOCK_REALTIME_COARSE,
#endif
#if defined(CLOCK_MONOTONIC)
	CLOCK_MONOTONIC,
#endif
#if defined(CLOCK_MONOTONIC_RAW)
	CLOCK_MONOTONIC_RAW,
#endif
#if defined(CLOCK_MONOTONIC_ACTIVE)
	CLOCK_MONOTONIC_ACTIVE,
#endif
#if defined(CLOCK_BOOTTIME)
	CLOCK_BOOTTIME,
#endif
#if defined(CLOCK_PROCESS_CPUTIME_ID)
	CLOCK_PROCESS_CPUTIME_ID,
#endif
#if defined(CLOCK_THREAD_CPUTIME_ID)
	CLOCK_THREAD_CPUTIME_ID,
#endif
#if defined(CLOCK_MONOTONIC_ACTIVE)
	CLOCK_MONOTONIC_ACTIVE,
#endif
#if defined(CLOCK_TAI)
	CLOCK_TAI,
#endif
#if defined(CLOCK_AUX)
	CLOCK_AUX,
#endif
};
#endif

/*
 *  various access() system call access mode flags
 */
static const int access_modes[] = {
	F_OK,
	R_OK,
	W_OK,
	X_OK,
	R_OK | W_OK,
	R_OK | X_OK,
	R_OK | W_OK | X_OK,
	W_OK | X_OK
};

/*
 *  various chmod() system call mode flags
 */
static const int chmod_modes[] = {
	S_ISUID,
	S_ISGID,
	S_ISVTX,
	S_IRUSR,
	S_IWUSR,
	S_IXUSR,
	S_IRGRP,
	S_IWGRP,
	S_IXGRP,
	S_IROTH,
	S_IWOTH,
	S_IXOTH,
};

#if defined(HAVE_GETITIMER)
/*
 *  various itimer types
 */
static const shim_itimer_which_t itimers[] = {
#if defined(ITIMER_REAL)
        ITIMER_REAL,
#endif
#if defined(ITIMER_VIRTUAL)
        ITIMER_VIRTUAL,
#endif
#if defined(ITIMER_PROF)
        ITIMER_PROF,
#endif
};
#endif

/*
 *  various ulimit/rlimit limit types
 */
static const shim_rlimit_resource_t limits[] = {
#if defined(RLIMIT_AS)
	RLIMIT_AS,
#endif
#if defined(RLIMIT_CORE)
	RLIMIT_CORE,
#endif
#if defined(RLIMIT_CPU)
	RLIMIT_CPU,
#endif
#if defined(RLIMIT_DATA)
	RLIMIT_DATA,
#endif
#if defined(RLIMIT_FSIZE)
	RLIMIT_FSIZE,
#endif
#if defined(RLIMIT_LOCKS)
	RLIMIT_LOCKS,
#endif
#if defined(RLIMIT_MEMLOCK)
	RLIMIT_MEMLOCK,
#endif
#if defined(RLIMIT_MSGQUEUE)
	RLIMIT_MSGQUEUE,
#endif
#if defined(RLIMIT_NICE)
	RLIMIT_NICE,
#endif
#if defined(RLIMIT_NPROC)
	RLIMIT_NPROC,
#endif
#if defined(RLIMIT_RSS)
	RLIMIT_RSS,
#endif
#if defined(RLIMIT_RTTIME)
	RLIMIT_RTTIME,
#endif
#if defined(RLIMIT_SIGPENDING)
	RLIMIT_SIGPENDING,
#endif
#if defined(RLIMIT_STACK)
	RLIMIT_STACK,
#endif
};

/*
 *  various scheduler policies
 */
#if (defined(_POSIX_PRIORITY_SCHEDULING) || defined(__linux__)) &&	\
     !defined(__OpenBSD__) &&						\
     !defined(__minix__) &&						\
     !defined(__APPLE__) &&						\
     !defined(__HAIKU__) &&						\
     !defined(__serenity__)
static const int sched_policies[] = {
#if defined(SCHED_BATCH)
	SCHED_BATCH,
#endif
#if defined(SCHED_DEADLINE)
	SCHED_DEADLINE,
#endif
#if defined(SCHED_EXT)
	SCHED_EXT,
#endif
#if defined(SCHED_FIFO)
	SCHED_FIFO,
#endif
#if defined(SCHED_IDLE)
	SCHED_IDLE,
#endif
#if defined(SCHED_OTHER)
	SCHED_OTHER,
#endif
#if defined(SCHED_RR)
	SCHED_RR,
#endif
};
#endif

#define SYSCALL(x)	{ x, & # x[8] }

static char syscall_filename[PATH_MAX];		/* filename of test file */
static char syscall_tmp_filename[PATH_MAX];	/* filename of temporary test file */
static char syscall_symlink_filename[PATH_MAX];	/* symlink file name */
static int  syscall_fd = -1;			/* file descriptor to syscall_filename */
static int  syscall_dir_fd = -1;		/* file descriptor to test directory */
static void *syscall_2_pages;			/* two mmap'd pages */
static void *syscall_brk_addr;			/* current brk address */
static gid_t syscall_gid;			/* current gid */
static uid_t syscall_uid;			/* current uid */
static pid_t syscall_pid;			/* current pid */
static pid_t syscall_sid;			/* current sid */
static uint64_t t1, t2;				/* t1 = start time, t2 = end time of test */
static size_t syscall_page_size;		/* size of 1 page in bytes */
static size_t syscall_2_pages_size;		/* size of 2 pages in bytes */
static char syscall_cwd[PATH_MAX];		/* current working directory */
static void *syscall_mmap_page = MAP_FAILED;	/* mmap() family test page */
static int syscall_errno;			/* errno from syscall */
static mode_t syscall_umask_mask;		/* current umask */
static syscall_shared_info_t *syscall_shared_info = NULL;
static char *syscall_exec_prog;			/* stress-ng exec path */

#if (defined(HAVE_SYS_XATTR_H) || defined(HAVE_ATTR_XATTR_H)) && \
    (defined(HAVE_FGETXATTR) ||		\
     defined(HAVE_FSETXATTR) ||		\
     defined(HAVE_FREMOVEXATTR) ||	\
     defined(HAVE_GETXATTR) ||		\
     defined(HAVE_REMOVEXATTR))
static const char *syscall_xattr_name = "user.val";	/* xattr name */
#endif
#if (defined(HAVE_SYS_XATTR_H) || defined(HAVE_ATTR_XATTR_H)) && \
    (defined(HAVE_LGETXATTR) ||		\
     defined(HAVE_LSETXATTR) ||		\
     defined(HAVE_LREMOVEXATTR))
static const char *syscall_lxattr_name = "trusted.val";	/* lxattr name */
#endif

/*
 *  syscall_shellsort_size_t()
 *	shellsort of size_t sized array
 *	this is used instead of qsort since some libc implementations
 *	do not provide qsort.
 */
static void NOINLINE syscall_shellsort_size_t(
	size_t *base,
	size_t nmemb,
	int (*cmp)(const void *, const void *))
{
	register size_t interval;

	for (interval = nmemb / 2; interval > 0; interval /= 2) {
		register size_t i;

		for (i = interval; i < nmemb; i++) {
			register size_t j;
			size_t tmp = base[i];

			for (j = i; (j >= interval) && cmp(&base[j - interval], &tmp); j -= interval) {
				base[j] = base[j - interval];
			}
			base[j] = tmp;
		}
	}
}

/*
 *  syscall_shared_error()
 *	indicate an error in shared data back to parent by
 *	setting invalid times and syscall return value.
 */
static void syscall_shared_error(const int ret)
{
	syscall_shared_info->t1 = ~0ULL;
	syscall_shared_info->t2 = ~0ULL;
	syscall_shared_info->syscall_ret = ret;
}

/*
 *  syscall_time_now()
 *	get nanosecond time delta since the first call
 *	this allows a couple of hundredd years of run
 *	time before overflow occurs.
 */
static uint64_t syscall_time_now(void)
{
#if defined(HAVE_CLOCK_GETTIME)
	static struct timespec base_ts = { 0, 0 };
	struct timespec ts;
	int64_t sec, ns;

	syscall_errno = errno;
	if (UNLIKELY(clock_gettime(CLOCK_MONOTONIC, &ts) < 0))
		return 0;
	if (base_ts.tv_sec == 0)
		base_ts = ts;	/* first call, save the baseline time */

	/* now return time delta since baseline time */
	ns = ts.tv_nsec - base_ts.tv_nsec;
	sec = (ts.tv_sec - base_ts.tv_sec) * 1000000000;
	return (uint64_t)sec + ns;
#else
	static struct timeval base_tv = { 0, 0 };
	struct timeval tv;
	int64_t sec, ns;

        if (gettimeofday(&tv, NULL) < 0)
		return 0;
	if (base_tv.tv_sec == 0)
		base_tv = tv;
	ns = (tv.tv_usec - base_tv.tv_usec) * 1000;
	sec = (tv.tv_sec - base_tv.tv_sec) * 1000000000;
	return (uint64_t)sec + ns;
#endif
}

static const stress_help_t help[] = {
	{ NULL,	"syscall N",		"start N workers that exercise a wide range of system calls" },
	{ NULL,	"syscall-method M",	"select method of selecting system calls to exercise" },
	{ NULL,	"syscall-ops N",	"stop after N syscall bogo operations" },
	{ NULL,	"syscall-top N",	"display fastest top N system calls" },
	{ NULL,	NULL,			NULL }
};

static const syscall_method_t syscall_methods[] = {
	{ "all",	SYSCALL_METHOD_ALL },
	{ "fast10",	SYSCALL_METHOD_FAST10 },
	{ "fast25",	SYSCALL_METHOD_FAST25 },
	{ "fast50",	SYSCALL_METHOD_FAST50 },
	{ "fast75",	SYSCALL_METHOD_FAST75 },
	{ "fast90",	SYSCALL_METHOD_FAST90 },
	{ "geomean1",	SYSCALL_METHOD_GEOMEAN1 },
	{ "geomean2",	SYSCALL_METHOD_GEOMEAN2 },
	{ "geomean3",	SYSCALL_METHOD_GEOMEAN3 },
};

#if defined(HAVE_SYS_UN_H) &&	\
    defined(AF_UNIX)

#define SOCK_MEASURE_ACCEPT		(0)
#define SOCK_MEASURE_BIND		(1)
#define SOCK_MEASURE_CONNECT		(2)
#define SOCK_MEASURE_LISTEN		(3)
#define SOCK_MEASURE_RECV		(4)
#define SOCK_MEASURE_RECVFROM		(5)
#define SOCK_MEASURE_RECVMMSG		(6)
#define SOCK_MEASURE_RECVMSG		(7)
#define SOCK_MEASURE_SEND		(8)
#define SOCK_MEASURE_SENDTO		(9)
#define SOCK_MEASURE_SENDMMSG		(10)
#define SOCK_MEASURE_SENDMSG		(11)
#define SOCK_MEASURE_GETPEERNAME	(12)
#define SOCK_MEASURE_SHUTDOWN		(13)
#define SOCK_MEASURE_ACCEPT4		(14)

/*
 *  syscall_socket_measure()
 *	generic socket operation benchmarking
 */
static int syscall_socket_measure(const int measure)
{
	pid_t pid;
	char buffer[64];
	struct sockaddr_un addr;
#if defined(HAVE_RECVMMSG) ||	\
    defined(HAVE_RECVMSG) ||	\
    defined(HAVE_SENDMMSG) ||	\
    defined(HAVE_SENDMSG)
	struct iovec vec[1];
#endif
#if defined(HAVE_RECVMSG) || 	\
    defined(HAVE_SENDMSG)
	struct msghdr msg;
#endif
#if defined(HAVE_RECVMMSG) || 	\
    defined(HAVE_SENDMMSG)
	struct mmsghdr msgvec[1];
#endif

	(void)shim_memset(buffer, 0, sizeof(buffer));
	(void)shim_memset(&addr, 0, sizeof(addr));
	addr.sun_family = AF_UNIX;
	(void)snprintf(addr.sun_path, sizeof(addr.sun_path), "/tmp/stress-ng-client-%" PRIdMAX, (intmax_t)getpid());

	syscall_shared_error(-1);

	pid = fork();
	if (pid < 0) {
		return -1;
	} else if (pid == 0) {
		int sfd, ret;
		ssize_t sret;

		sfd = socket(AF_UNIX, SOCK_STREAM, 0);
		if (sfd < 0)
			_exit(EXIT_FAILURE);

		if (measure == SOCK_MEASURE_CONNECT) {
			syscall_shared_info->t1 = syscall_time_now();
			ret = connect(sfd, (struct sockaddr *)&addr, sizeof(addr));
			syscall_shared_info->t2 = syscall_time_now();
			if (ret < 0)
				syscall_shared_error(ret);
		} else {
			ret = connect(sfd, (struct sockaddr *)&addr, sizeof(addr));
			if (ret < 0)
				goto close_sfd_child;
		}
		if (measure == SOCK_MEASURE_GETPEERNAME) {
			struct sockaddr peeraddr;
			socklen_t len = sizeof(peeraddr);

			syscall_shared_info->t1 = syscall_time_now();
			ret = getpeername(sfd, &peeraddr, &len);
			syscall_shared_info->t2 = syscall_time_now();
			if (ret < 0)
				syscall_shared_error(ret);
		}
		(void)shim_strscpy(buffer, "senddata", sizeof(buffer));
		switch (measure) {
		case SOCK_MEASURE_SEND:
			syscall_shared_info->t1 = syscall_time_now();
			sret = send(sfd, buffer, strlen(buffer), 0);
			syscall_shared_info->t2 = syscall_time_now();
			if (sret < 0)
				syscall_shared_error((int)sret);
			break;
		case SOCK_MEASURE_SENDTO:
			syscall_shared_info->t1 = syscall_time_now();
			sret = sendto(sfd, buffer, strlen(buffer), 0, NULL, 0);
			syscall_shared_info->t2 = syscall_time_now();
			if (sret < 0)
				syscall_shared_error((int)sret);
			break;
#if defined(HAVE_SENDMMSG)
		case SOCK_MEASURE_SENDMMSG:
			(void)shim_memset(msgvec, 0, sizeof(msgvec));
			vec[0].iov_base = buffer;
			vec[0].iov_len = sizeof(buffer);
			msgvec[0].msg_hdr.msg_iov = vec;
			msgvec[0].msg_hdr.msg_iovlen = 1;
			syscall_shared_info->t1 = syscall_time_now();
			sret = sendmmsg(sfd, msgvec, 1, 0);
			syscall_shared_info->t2 = syscall_time_now();
			if (sret < 0)
				syscall_shared_error((int)sret);
			break;
#endif
#if defined(HAVE_SENDMSG)
		case SOCK_MEASURE_SENDMSG:
			vec[0].iov_base = buffer;
			vec[0].iov_len = sizeof(buffer);
			(void)shim_memset(&msg, 0, sizeof(msg));
			msg.msg_iov = vec;
			msg.msg_iovlen = 1;
			syscall_shared_info->t1 = syscall_time_now();
			sret = sendmsg(sfd, &msg, 0);
			syscall_shared_info->t2 = syscall_time_now();
			if (sret < 0)
				syscall_shared_error((int)sret);
			break;
#endif
		default:
			VOID_RET(ssize_t, send(sfd, buffer, strlen(buffer), 0));
			break;
		}
close_sfd_child:
		VOID_RET(int, shutdown(sfd, SHUT_RDWR));
		(void)close(sfd);
		_exit(0);
	} else {
		int sfd, fd, ret, status;
		ssize_t sret;

		sfd = socket(AF_UNIX, SOCK_STREAM, 0);
		if (sfd < 0)
			goto reap_child;
		if (measure == SOCK_MEASURE_BIND) {
			syscall_shared_info->t1 = syscall_time_now();
			ret = bind(sfd, (struct sockaddr *)&addr, sizeof(addr));
			syscall_shared_info->t2 = syscall_time_now();
			if (ret < 0) {
				syscall_shared_error(ret);
				goto close_sfd;
			}
		} else {
			ret = bind(sfd, (struct sockaddr *)&addr, sizeof(addr));
			if (ret < 0)
				goto close_sfd;
		}

		if (measure == SOCK_MEASURE_LISTEN) {
			syscall_shared_info->t1 = syscall_time_now();
			ret = listen(sfd, 1);
			syscall_shared_info->t2 = syscall_time_now();
			if (ret < 0) {
				syscall_shared_error(ret);
				goto close_sfd;
			}
		} else {
			ret = listen(sfd, 1);
			if (ret < 0)
				goto close_sfd;
		}

		switch (measure) {
		case SOCK_MEASURE_ACCEPT:
			syscall_shared_info->t1 = syscall_time_now();
			fd = accept(sfd, NULL, NULL);
			syscall_shared_info->t2 = syscall_time_now();
			if (fd < 0) {
				syscall_shared_error(fd);
				goto close_sfd;
			}
			break;
#if defined(HAVE_ACCEPT4)
		case SOCK_MEASURE_ACCEPT4:
			syscall_shared_info->t1 = syscall_time_now();
			fd = accept4(sfd, NULL, NULL, 0);
			syscall_shared_info->t2 = syscall_time_now();
			if (fd < 0) {
				syscall_shared_error(fd);
				goto close_sfd;
			}
			break;
#endif
		default:
			fd = accept(sfd, NULL, NULL);
			if (fd < 0)
				goto close_sfd;
		}

		switch (measure) {
		case SOCK_MEASURE_RECV:
			syscall_shared_info->t1 = syscall_time_now();
			sret = recv(fd, buffer, sizeof(buffer), 0);
			syscall_shared_info->t2 = syscall_time_now();
			if (sret < 0)
				syscall_shared_error((int)sret);
			break;
		case SOCK_MEASURE_RECVFROM:
			syscall_shared_info->t1 = syscall_time_now();
			sret = recvfrom(fd, buffer, sizeof(buffer), 0, NULL, NULL);
			syscall_shared_info->t2 = syscall_time_now();
			if (sret < 0)
				syscall_shared_error((int)sret);
			break;
#if defined(HAVE_RECVMSG)
		case SOCK_MEASURE_RECVMSG:
			vec[0].iov_base = buffer;
			vec[0].iov_len = sizeof(buffer);
			(void)shim_memset(&msg, 0, sizeof(msg));
			msg.msg_iov = vec;
			msg.msg_iovlen = 1;
			syscall_shared_info->t1 = syscall_time_now();
			sret = recvmsg(fd, &msg, 0);
			syscall_shared_info->t2 = syscall_time_now();
			if (sret < 0)
				syscall_shared_error((int)sret);
			break;
#endif
#if defined(HAVE_RECVMMSG)
		case SOCK_MEASURE_RECVMMSG:
			(void)shim_memset(msgvec, 0, sizeof(msgvec));
			vec[0].iov_base = buffer;
			vec[0].iov_len = sizeof(buffer);
			msgvec[0].msg_hdr.msg_iov = vec;
			msgvec[0].msg_hdr.msg_iovlen = 1;
			syscall_shared_info->t1 = syscall_time_now();
			sret = recvmmsg(fd, msgvec, 1, 0, NULL);
			syscall_shared_info->t2 = syscall_time_now();
			if (sret < 0)
				syscall_shared_error((int)sret);
			break;
#endif
		default:
			VOID_RET(ssize_t, recv(fd, buffer, sizeof(buffer), 0));
			break;
		}
		(void)close(fd);
close_sfd:
		if (measure == SOCK_MEASURE_SHUTDOWN) {
			syscall_shared_info->t1 = syscall_time_now();
			ret = shutdown(sfd, SHUT_RDWR);
			syscall_shared_info->t2 = syscall_time_now();
			if (ret < 0)
				syscall_shared_error(ret);
		} else {
			VOID_RET(int, shutdown(sfd, SHUT_RDWR));
		}
		(void)close(sfd);
reap_child:
		VOID_RET(pid_t, waitpid(pid, &status, 0));
	}
	t1 = syscall_shared_info->t1;
	t2 = syscall_shared_info->t2;
	VOID_RET(int, shim_unlink(addr.sun_path));
	return 0;
}
#endif

static void syscall_sigignore_handler(int num)
{
	(void)num;
}

static void syscall_sigusr1_handler(int num)
{
	(void)num;

	syscall_shared_info->sig_t = syscall_time_now();
	syscall_shared_info->t_set = true;
}

#if defined(HAVE_SYS_UN_H) &&	\
    defined(AF_UNIX)
#define HAVE_SYSCALL_ACCEPT
static int syscall_accept(void)
{
	return syscall_socket_measure(SOCK_MEASURE_ACCEPT);
}
#endif

#if defined(HAVE_SYS_UN_H) &&	\
    defined(AF_UNIX) &&		\
    defined(HAVE_ACCEPT4)
#define HAVE_SYSCALL_ACCEPT4
static int syscall_accept4(void)
{
	return syscall_socket_measure(SOCK_MEASURE_ACCEPT4);
}
#endif

#define HAVE_SYSCALL_ACCESS
static int syscall_access(void)
{
	static size_t i = 0;
	int ret;

	i++;
	if (i >= SIZEOF_ARRAY(access_modes))
		i = 0;
	t1 = syscall_time_now();
	ret = access(syscall_filename, access_modes[i]);
	t2 = syscall_time_now();
	return ret;
}

#if defined(HAVE_KEYUTILS_H) &&		\
    defined(HAVE_SYSCALL) &&		\
    defined(__NR_add_key) &&		\
    defined(__NR_keyctl) &&		\
    defined(KEYCTL_INVALIDATE)
#define HAVE_SYSCALL_ADD_KEY
static int syscall_add_key(void)
{
	key_serial_t key;
	char ALIGN64 description[64];
	static char payload[] = "example payload";

	(void)snprintf(description, sizeof(description),
		"stress-ng-syscall-key-%" PRIdMAX, (intmax_t)syscall_pid);

	t1 = syscall_time_now();
	key = (key_serial_t)syscall(__NR_add_key, "user",
                description, payload, sizeof(payload), KEY_SPEC_PROCESS_KEYRING);
	t2 = syscall_time_now();
	if (key < 0)
		return -1;
	(void)syscall(__NR_keyctl, KEYCTL_INVALIDATE, key);
	return (int)key;
}
#endif

#define HAVE_SYSCALL_ALARM
static int syscall_alarm(void)
{
	pid_t pid;
	int ret = -1;

	pid = fork();
	if (pid < 0)
		return -1;
	if (pid == 0) {
		syscall_shared_info->t1 = syscall_time_now();
		ret = alarm(1);
		syscall_shared_info->t2 = syscall_time_now();
		syscall_shared_info->syscall_ret = ret;
		_exit(0);
	} else {
		int status;

		VOID_RET(pid_t, waitpid(pid, &status, 0));
		t1 = syscall_shared_info->t1;
		t2 = syscall_shared_info->t2;
		ret = syscall_shared_info->syscall_ret;
	}
	return ret;
}

#if defined(HAVE_SYS_UN_H) &&	\
    defined(AF_UNIX)
#define HAVE_SYSCALL_BIND
static int syscall_bind(void)
{
	return syscall_socket_measure(SOCK_MEASURE_BIND);
}
#endif

#define HAVE_SYSCALL_BRK
static int syscall_brk(void)
{
	if (syscall_brk_addr != (void *)-1) {
		t1 = syscall_time_now();
		(void)shim_brk(syscall_brk_addr);
		t2 = syscall_time_now();
		return 0;
	}
	return -1;
}

#if defined(HAVE_ASM_CACHECTL_H) &&	\
    defined(HAVE_CACHEFLUSH) &&		\
    defined(STRESS_ARCH_MIPS)
#define HAVE_SYSCALL_CACHEFLUSH
static int syscall_cacheflush(void)
{
	int ret;

	t1 = syscall_time_now();
	ret = shim_cacheflush(syscall_2_pages, syscall_2_pages_size, SHIM_DCACHE);
	t2 = syscall_time_now();
	return ret;
}
#endif

#if defined(HAVE_SYS_CAPABILITY_H) &&		\
    defined(_LINUX_CAPABILITY_U32S_3) &&	\
    defined(_LINUX_CAPABILITY_VERSION_3)
#define HAVE_SYSCALL_CAPGET
static int syscall_capget(void)
{
	struct __user_cap_header_struct uch;
	struct __user_cap_data_struct ucd[_LINUX_CAPABILITY_U32S_3];
	int ret;

	(void)shim_memset(&uch, 0, sizeof uch);
	(void)shim_memset(ucd, 0, sizeof ucd);

        uch.version = _LINUX_CAPABILITY_VERSION_3;
        uch.pid = syscall_pid;

	t1 = syscall_time_now();
        ret = capget(&uch, ucd);
	t2 = syscall_time_now();
	return ret;
}
#endif

#if defined(HAVE_SYS_CAPABILITY_H) &&		\
    defined(_LINUX_CAPABILITY_U32S_3) &&	\
    defined(_LINUX_CAPABILITY_VERSION_3)
#define HAVE_SYSCALL_CAPSET
static int syscall_capset(void)
{
	struct __user_cap_header_struct uch;
	struct __user_cap_data_struct ucd[_LINUX_CAPABILITY_U32S_3];
	int ret;

	(void)shim_memset(&uch, 0, sizeof uch);
	(void)shim_memset(ucd, 0, sizeof ucd);

        uch.version = _LINUX_CAPABILITY_VERSION_3;
        uch.pid = syscall_pid;

        ret = capget(&uch, ucd);
	if (ret < 0)
		return -1;
	t1 = syscall_time_now();
        ret = capset(&uch, ucd);
	t2 = syscall_time_now();
	return ret;
}
#endif

#define HAVE_SYSCALL_CHDIR
static int syscall_chdir(void)
{
	int ret;

	t1 = syscall_time_now();
	ret = chdir("/");
	t2 = syscall_time_now();
	VOID_RET(int, chdir(syscall_cwd));
	return ret;
}

#define HAVE_SYSCALL_CHMOD
static int syscall_chmod(void)
{
	static size_t i = 0;
	int ret;

	i++;
	if (i >= SIZEOF_ARRAY(chmod_modes))
		i = 0;
	t1 = syscall_time_now();
	ret = chmod(syscall_filename, chmod_modes[i]);
	t2 = syscall_time_now();
	VOID_RET(int, chmod(syscall_filename, S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP));
	return ret;
}

#define HAVE_SYSCALL_CHOWN
static int syscall_chown(void)
{
	int ret;

	t1 = syscall_time_now();
	ret = chown(syscall_filename, syscall_uid, syscall_gid);
	t2 = syscall_time_now();
	return ret;
}

#if defined(HAVE_CHROOT)
#define HAVE_SYSCALL_CHROOT
static int syscall_chroot(void)
{
	pid_t pid;

	syscall_shared_error(0);

	pid = fork();
	if (pid < 0)
		return -1;
	else if (pid == 0) {
		const char *path = stress_get_temp_path();
		int ret;

		syscall_shared_info->t1 = syscall_time_now();
		ret = chroot(path);
		syscall_shared_info->t2 = syscall_time_now();

		syscall_shared_info->syscall_errno = syscall_errno;
		syscall_shared_info->syscall_ret = ret;
		if (ret < 0)
			syscall_shared_error(ret);
		VOID_RET(int, chdir("/"));
		_exit(0);
	} else {
		int status;

		VOID_RET(pid_t, waitpid(pid, &status, 0));
		t1 = syscall_shared_info->t1;
		t2 = syscall_shared_info->t2;
		syscall_errno = syscall_shared_info->syscall_errno;
	}
	return syscall_shared_info->syscall_ret;
}
#endif

#if defined(HAVE_CLOCK_ADJTIME) &&	\
    defined(HAVE_SYS_TIMEX_H) &&	\
    defined(HAVE_TIMEX)
#define HAVE_SYSCALL_CLOCK_ADJTIME
static int syscall_clock_adjtime(void)
{

	shim_timex_t t;
	static size_t i = 0;
	int ret;
	const int clock_id = clocks[i];

	(void)shim_memset(&t, 0, sizeof(t));
	i++;
	if (i >= SIZEOF_ARRAY(clocks))
		i = 0;
	t1 = syscall_time_now();
	ret = shim_clock_adjtime(clock_id, &t);
	t2 = syscall_time_now();
	return ret;
}
#endif

#if defined(HAVE_CLOCK_GETRES)
#define HAVE_SYSCALL_CLOCK_GETRES
static int syscall_clock_getres(void)
{
	struct timespec t;
	static size_t i = 0;
	int ret;
	const int clock_id = clocks[i];

	i++;
	if (i >= SIZEOF_ARRAY(clocks))
		i = 0;
	t1 = syscall_time_now();
	ret = shim_clock_getres(clock_id, &t);
	t2 = syscall_time_now();
	return ret;
}
#endif

#if defined(HAVE_CLOCK_GETTIME)
#define HAVE_SYSCALL_CLOCK_GETTIME
static int syscall_clock_gettime(void)
{
	struct timespec t;
	static size_t i = 0;
	int ret;
	const int clock_id = clocks[i];

	i++;
	if (i >= SIZEOF_ARRAY(clocks))
		i = 0;
	t1 = syscall_time_now();
	ret = shim_clock_gettime(clock_id, &t);
	t2 = syscall_time_now();
	return ret;
}
#endif

#if defined(HAVE_CLOCK_NANOSLEEP)
#define HAVE_SYSCALL_CLOCK_NANOSLEEP
static int syscall_clock_nanosleep(void)
{
	static const int nanosleep_clocks[] = {
#if defined(CLOCK_REALTIME)
		CLOCK_REALTIME,
#endif
#if defined(CLOCK_TAI)
		CLOCK_TAI,
#endif
#if defined(CLOCK_MONOTONIC)
		CLOCK_MONOTONIC,
#endif
#if defined(CLOCK_BOOTTIME)
		CLOCK_BOOTTIME,
#endif
	};

	struct timespec t, remain;
	static size_t i = 0;
	int ret;
	const int nanosleep_clock = nanosleep_clocks[i];

	t.tv_sec = 0;
	t.tv_nsec = 1;

	i++;
	if (i >= SIZEOF_ARRAY(nanosleep_clocks))
		i = 0;
	t1 = syscall_time_now();
	ret = clock_nanosleep(nanosleep_clock, 0, &t, &remain);
	t2 = syscall_time_now();
	return ret;
}
#endif

#if defined(HAVE_CLOCK_SETTIME)
#define HAVE_SYSCALL_CLOCK_SETTIME
static int syscall_clock_settime(void)
{
	struct timespec t;
	static size_t i = 0;
	int ret;
	const int clock_id = clocks[i];

	i++;
	if (i >= SIZEOF_ARRAY(clocks))
		i = 0;
	ret = shim_clock_gettime(clock_id, &t);
	if (ret < 0)
		return -1;
	t1 = syscall_time_now();
	ret = shim_clock_settime(clock_id, &t);
	t2 = syscall_time_now();
	return ret;
}
#endif

#if defined(HAVE_CLONE) &&	\
    defined(__linux__)
static int syscall_clone_func(void *arg)
{
	(void)arg;

	syscall_shared_info->t2 = syscall_time_now();
	syscall_shared_info->t_set = true;

	return 0;
}
#endif

#if defined(HAVE_CLONE) &&	\
    defined(__linux__)
#define HAVE_SYSCALL_CLONE
static int syscall_clone(void)
{
	pid_t pid;
	pid_t parent_tid = -1;
	pid_t child_tid = -1;
	int status;
	char stack[8192];
	char *stack_top = (char *)stress_get_stack_top((char *)stack, sizeof(stack));

	syscall_shared_info->t1 = ~0ULL;
	syscall_shared_info->t2 = ~0ULL;
	syscall_shared_info->t_set = false;

	t1 = syscall_time_now();

	pid = clone(syscall_clone_func, stress_align_stack(stack_top),
		    CLONE_CHILD_CLEARTID | CLONE_CHILD_SETTID | SIGCHLD, NULL,
		    &parent_tid, NULL, &child_tid);
	if (pid < 0)
		return -1;
	VOID_RET(pid_t, waitpid(pid, &status, 0));
	t2 = syscall_shared_info->t2;
	return pid;
}
#endif

/* If we have clone support then we can call shim_clone3 */
#if defined(HAVE_CLONE) &&	\
    defined(__linux__) &&	\
    defined(CLONE_FS)
#define HAVE_SYSCALL_CLONE3
static int syscall_clone3(void)
{
	pid_t pid;
	pid_t parent_tid = -1;
	pid_t child_tid = -1;
	int status, pidfd = -1;
	struct shim_clone_args cl_args;

	syscall_shared_info->t1 = ~0ULL;
	syscall_shared_info->t2 = ~0ULL;
	syscall_shared_info->t_set = false;

	(void)shim_memset(&cl_args, 0, sizeof(cl_args));
	cl_args.flags = 0;
	cl_args.pidfd = (uint64_t)(uintptr_t)&pidfd;
	cl_args.child_tid = (uint64_t)(uintptr_t)&child_tid;
	cl_args.parent_tid = (uint64_t)(uintptr_t)&parent_tid;
	cl_args.exit_signal = SIGCHLD;
	cl_args.stack = (uint64_t)(uintptr_t)NULL;
	cl_args.stack_size = 0;
	cl_args.tls = (uint64_t)(uintptr_t)NULL;

	t1 = syscall_time_now();
	pid = shim_clone3(&cl_args, sizeof(cl_args));
	if (pid < 0) {
		return -1;
	} else if (pid == 0) {
		syscall_shared_info->t2 = syscall_time_now();
		syscall_shared_info->t_set = true;
		_exit(0);
	}
	VOID_RET(pid_t, waitpid(pid, &status, 0));
	t2 = syscall_shared_info->t2;
	return pid;
}
#endif

#define HAVE_SYSCALL_CLOSE
static int syscall_close(void)
{
	int ret, fd = dup(syscall_fd);

	if (fd < 0)
		return -1;
	t1 = syscall_time_now();
	ret = close(fd);
	t2 = syscall_time_now();
	return ret;
}

#if defined(HAVE_SYS_UN_H) &&	\
    defined(AF_UNIX)
#define HAVE_SYSCALL_CONNECT
static int syscall_connect(void)
{
	return syscall_socket_measure(SOCK_MEASURE_CONNECT);
}
#endif

#if defined(HAVE_COPY_FILE_RANGE)
#define HAVE_SYSCALL_COPY_FILE_RANGE
static int syscall_copy_file_range(void)
{
	int ret;
	shim_off64_t off_in = 0;
	shim_off64_t off_out = 8192;

	t1 = syscall_time_now();
	ret = shim_copy_file_range(syscall_fd, &off_in, syscall_fd, &off_out, 4096, 0);
	t2 = syscall_time_now();
	return ret;
}
#endif

#define HAVE_SYSCALL_CREAT
static int syscall_creat(void)
{
	int fd;

	t1 = syscall_time_now();
	fd = creat(syscall_tmp_filename, S_IRUSR | S_IWUSR);
	t2 = syscall_time_now();

	if (fd < 0) {
		VOID_RET(int, shim_unlink(syscall_tmp_filename));
		return -1;
	}
	VOID_RET(int, close(fd));
	VOID_RET(int, shim_unlink(syscall_tmp_filename));
	return fd;
}

#define HAVE_SYSCALL_DUP
static int syscall_dup(void)
{
	int fd;

	t1 = syscall_time_now();
	fd = dup(syscall_fd);
	t2 = syscall_time_now();
	if (fd >= 0)
		VOID_RET(int, close(fd));
	return fd;
}

#define HAVE_SYSCALL_DUP2
static int syscall_dup2(void)
{
	int fd, newfd = stress_mwc8() + 32;

	t1 = syscall_time_now();
	fd = dup2(syscall_fd, newfd);
	t2 = syscall_time_now();
	if (fd >= 0)
		VOID_RET(int, close(fd));
	return fd;
}

#if defined(HAVE_DUP3)
#define HAVE_SYSCALL_DUP3
static int syscall_dup3(void)
{
	int fd, newfd = stress_mwc8() + 32;

	t1 = syscall_time_now();
#if defined(O_CLOEXEC)
	fd = shim_dup3(syscall_fd, newfd, O_CLOEXEC);
#else
	fd = shim_dup3(syscall_fd, newfd, 0);
#endif
	t2 = syscall_time_now();
	if (fd >= 0)
		VOID_RET(int, close(fd));
	return fd;
}
#endif

#if defined(HAVE_SYS_EPOLL_H) &&	\
    defined(HAVE_EPOLL_CREATE)
#define HAVE_SYSCALL_EPOLL_CREATE
static int syscall_epoll_create(void)
{
	int fd;

	t1 = syscall_time_now();
	fd = epoll_create(1);
	t2 = syscall_time_now();

	if (fd >= 0)
		(void)close(fd);
	return fd;
}
#endif

#if defined(HAVE_SYS_EPOLL_H) &&	\
    defined(HAVE_EPOLL_CREATE1)
#define HAVE_SYSCALL_EPOLL_CREATE1
static int syscall_epoll_create1(void)
{
	int fd;

	t1 = syscall_time_now();
	fd = epoll_create1(0);
	t2 = syscall_time_now();
	if (fd >= 0)
		(void)close(fd);
	return fd;
}
#endif

#if defined(HAVE_SYS_EPOLL_H) &&	\
    defined(EPOLL_CTL_ADD) &&		\
    defined(EPOLLIN) &&			\
    defined(HAVE_EPOLL_CREATE)
#define HAVE_SYSCALL_EPOLL_CTL
static int syscall_epoll_ctl(void)
{
	int fd, fds[2], ret;
	struct epoll_event event;

	fd = epoll_create(1);
	if (fd < 0)
		return -1;
	if (pipe(fds) < 0) {
		(void)close(fd);
		return -1;
	}

	(void)shim_memset(&event, 0, sizeof(event));
	event.data.fd = fds[1];
	event.events = EPOLLIN;
	t1 = syscall_time_now();
	ret = epoll_ctl(fd, EPOLL_CTL_ADD, fds[1], &event);
	t2 = syscall_time_now();
	(void)close(fds[0]);
	(void)close(fds[1]);

	return ret;
}
#endif

#if defined(HAVE_SYS_EPOLL_H) &&	\
    defined(EPOLL_CTL_ADD) &&		\
    defined(EPOLLIN) &&			\
    defined(HAVE_EPOLL_CREATE)
#define HAVE_SYSCALL_EPOLL_PWAIT
static int syscall_epoll_pwait(void)
{
	int fd, fds[2], ret;
	struct epoll_event event;
	sigset_t sigmask;

	fd = epoll_create(1);
	if (fd < 0)
		return -1;
	if (pipe(fds) < 0) {
		(void)close(fd);
		return -1;
	}

	(void)shim_memset(&event, 0, sizeof(event));
	event.data.fd = fds[1];
	event.events = EPOLLIN;
	ret = epoll_ctl(fd, EPOLL_CTL_ADD, fds[1], &event);
	if (ret < 0)
		goto close_fds;

	(void)sigemptyset(&sigmask);
	(void)sigaddset(&sigmask, SIGALRM);
	t1 = syscall_time_now();
	ret = epoll_pwait(fd, &event, 1, 0, &sigmask);
	t2 = syscall_time_now();
close_fds:
	(void)close(fds[0]);
	(void)close(fds[1]);
	(void)close(fd);

	return ret;
}
#endif

#if defined(HAVE_SYS_EPOLL_H) &&	\
    defined(EPOLL_CTL_ADD) &&		\
    defined(EPOLLIN) &&			\
    defined(HAVE_EPOLL_CREATE)
#define HAVE_SYSCALL_EPOLL_WAIT
static int syscall_epoll_wait(void)
{
	int fd, fds[2], ret;
	struct epoll_event event;
	sigset_t sigmask;

	fd = epoll_create(1);
	if (fd < 0)
		return -1;
	if (pipe(fds) < 0) {
		(void)close(fd);
		return -1;
	}

	(void)shim_memset(&event, 0, sizeof(event));
	event.data.fd = fds[1];
	event.events = EPOLLIN;
	ret = epoll_ctl(fd, EPOLL_CTL_ADD, fds[1], &event);
	if (ret < 0)
		goto close_fds;

	(void)sigemptyset(&sigmask);
	(void)sigaddset(&sigmask, SIGALRM);
	t1 = syscall_time_now();
	ret = epoll_wait(fd, &event, 1, 0);
	t2 = syscall_time_now();
close_fds:
	(void)close(fds[0]);
	(void)close(fds[1]);
	(void)close(fd);

	return ret;
}
#endif

#if defined(HAVE_SYS_EVENTFD_H) &&	\
    defined(HAVE_EVENTFD) &&		\
    NEED_GLIBC(2,8,0)
#define HAVE_SYSCALL_EVENTFD
static int syscall_eventfd(void)
{
	int fd;

	t1 = syscall_time_now();
	fd = eventfd(0, 0);
	t2 = syscall_time_now();
	if (fd >= 0)
		(void)close(fd);
	return fd;
}
#endif

static void syscall_execve_silence_stdio(void)
{
	int fd_in, fd_out;

	fd_in = open("/dev/zero", O_RDONLY);
	if (fd_in < 0) {
		syscall_shared_error(fd_in);
		_exit(0);
	}
	fd_out = open("/dev/null", O_WRONLY);
	if (fd_out < 0) {
		syscall_shared_error(fd_out);
		_exit(0);
	}

	(void)dup2(fd_out, STDOUT_FILENO);
	(void)dup2(fd_out, STDERR_FILENO);
	(void)dup2(fd_in, STDIN_FILENO);
	(void)close(fd_out);
	(void)close(fd_in);
}

#define HAVE_SYSCALL_EXECVE
static int syscall_execve(void)
{
	pid_t pid;

	syscall_shared_error(0);

	if (!syscall_exec_prog)
		return -1;

	pid = fork();
	if (pid < 0)
		return -1;
	else if (pid == 0) {
		int ret;
		char *argv[3];
		char *env[1];

		argv[0] = syscall_exec_prog;
		argv[1] = "--exec-exit";
		argv[2] = NULL;
		env[0] = NULL;

		syscall_execve_silence_stdio();

		syscall_shared_info->t1 = syscall_time_now();
		ret = execve(syscall_exec_prog, argv, env);
		if (ret < 0)
			syscall_shared_error(ret);
		_exit(0);
	} else {
		int status;

		VOID_RET(pid_t, waitpid(pid, &status, 0));
		t1 = syscall_shared_info->t1;
		t2 = syscall_time_now();
	}
	return syscall_shared_info->syscall_ret;
}

#if defined(HAVE_EXECVEAT)
#define HAVE_SYSCALL_EXECVEAT
static int syscall_execveat(void)
{
	pid_t pid;

	syscall_shared_error(0);

	if (!syscall_exec_prog)
		return -1;

	pid = fork();
	if (pid < 0)
		return -1;
	else if (pid == 0) {
		int ret;
#if defined(O_PATH) &&	\
    defined(AT_EMPTY_PATH)
		int fd;
#endif
		char *argv[3];
		char *env[1];

#if defined(O_PATH) &&	\
    defined(AT_EMPTY_PATH)
		fd = open(syscall_exec_prog, O_PATH);
#endif
		argv[0] = syscall_exec_prog;
		argv[1] = "--exec-exit";
		argv[2] = NULL;
		env[0] = NULL;

		syscall_execve_silence_stdio();

#if defined(O_PATH) &&	\
    defined(AT_EMPTY_PATH)
		if (fd < 0) {
			syscall_shared_info->t1 = syscall_time_now();
			ret = shim_execveat(0, syscall_exec_prog, argv, env, 0);
		} else {
			syscall_shared_info->t1 = syscall_time_now();
			ret = shim_execveat(fd, "", argv, env, AT_EMPTY_PATH);
		}
#else
		syscall_shared_info->t1 = syscall_time_now();
		ret = shim_execveat(0, syscall_exec_prog, argv, env, 0);
#endif
		if (ret < 0)
			syscall_shared_error(ret);
		if (fd >= 0)
			(void)close(fd);
		_exit(0);
	} else {
		int status;

		VOID_RET(pid_t, waitpid(pid, &status, 0));
		t1 = syscall_shared_info->t1;
		t2 = syscall_time_now();
	}
	return syscall_shared_info->syscall_ret;
}
#endif

#define HAVE_SYSCALL_EXIT
static int syscall_exit(void)
{
	pid_t pid;

	syscall_shared_error(0);

	pid = fork();
	if (pid < 0) {
		return -1;
	} else if (pid == 0) {
		syscall_shared_info->t1 = syscall_time_now();
		_exit(0);
	} else {
		int status;

		VOID_RET(pid_t, waitpid(pid, &status, 0));
		t2 = syscall_time_now();
		t1 = syscall_shared_info->t1;
	}
	return 0;
}

#if defined(HAVE_FACCESSAT)
#define HAVE_SYSCALL_FACCESSAT
static int syscall_faccessat(void)
{
	static size_t i = 0;
	int ret;
	const int access_mode = access_modes[i];

	i++;
	if (i >= SIZEOF_ARRAY(access_modes))
		i = 0;
	t1 = syscall_time_now();
	ret = faccessat(syscall_dir_fd, syscall_filename, access_mode, 0);
	t2 = syscall_time_now();
	return ret;
}
#endif

#if defined(HAVE_FALLOCATE)
#define HAVE_SYSCALL_FALLOCATE
static int syscall_fallocate(void)
{
	const off_t size = (off_t)stress_mwc32() & 0xffff;
	int ret;

	t1 = syscall_time_now();
	ret = shim_fallocate(syscall_fd, 0, (off_t)0, size);
	t2 = syscall_time_now();
	return ret;
}
#endif

#if defined(HAVE_SYS_FANOTIFY_H) &&	\
    defined(HAVE_FANOTIFY)
#define HAVE_SYSCALL_FANOTIFY_INIT
static int syscall_fanotify_init(void)
{
	int fd;

	t1 = syscall_time_now();
	fd = fanotify_init(0, 0);
	t2 = syscall_time_now();
	if (fd < 0)
		return -1;
	(void)close(fd);

	return fd;
}
#endif

#if defined(HAVE_SYS_FANOTIFY_H) &&	\
    defined(HAVE_FANOTIFY)
#define HAVE_SYSCALL_FANOTIFY_MARK
static int syscall_fanotify_mark(void)
{
	int fd, ret;

	fd = fanotify_init(0, 0);
	if (fd < 0)
		return -1;
	t1 = syscall_time_now();
	ret = fanotify_mark(fd, FAN_MARK_ADD | FAN_MARK_MOUNT,
			FAN_ACCESS, AT_FDCWD, "/");
	t2 = syscall_time_now();
	(void)close(fd);

	return ret;
}
#endif

#if defined(HAVE_OPENAT)
#define HAVE_SYSCALL_FCHDIR
static int syscall_fchdir(void)
{
	int ret, fd;

	fd = openat(AT_FDCWD, ".", O_RDONLY | O_DIRECTORY);
	if (fd < 0)
		return -1;
	t1 = syscall_time_now();
	ret = fchdir(fd);
	t2 = syscall_time_now();
	VOID_RET(int, chdir(syscall_cwd));
	VOID_RET(int, close(fd));
	return ret;
}
#endif

#define HAVE_SYSCALL_FCHMOD
static int syscall_fchmod(void)
{
	static size_t i = 0;
	int ret;
	const int chmod_mode = chmod_modes[i];

	i++;
	if (i >= SIZEOF_ARRAY(chmod_modes))
		i = 0;
	t1 = syscall_time_now();
	ret = fchmod(syscall_fd, chmod_mode);
	t2 = syscall_time_now();
	VOID_RET(int, fchmod(syscall_fd, S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP));
	return ret;
}

#if defined(HAVE_FCHMODAT)
#define HAVE_SYSCALL_FCHMODAT
static int syscall_fchmodat(void)
{
	static size_t i = 0;
	int ret;
	const int chmod_mode = chmod_modes[i];

	i++;
	if (i >= SIZEOF_ARRAY(chmod_modes))
		i = 0;
	t1 = syscall_time_now();
	ret = fchmodat(syscall_dir_fd, syscall_filename, chmod_mode, 0);
	t2 = syscall_time_now();
	VOID_RET(int, fchmodat(syscall_dir_fd, syscall_filename, S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP, 0));
	return ret;
}
#endif

#if defined(HAVE_FCHMODAT2)
#define HAVE_SYSCALL_FCHMODAT2
static int syscall_fchmodat2(void)
{
	static size_t i = 0;
	int ret;
	const int chmod_mode = chmod_modes[i];

	i++;
	if (i >= SIZEOF_ARRAY(chmod_modes))
		i = 0;
	t1 = syscall_time_now();
	ret = fchmodat2(syscall_dir_fd, syscall_filename, chmod_mode, 0);
	t2 = syscall_time_now();
	VOID_RET(int, fchmodat2(syscall_dir_fd, syscall_filename, S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP, 0));
	return ret;
}
#endif

#define HAVE_SYSCALL_FCHOWN
static int syscall_fchown(void)
{
	int ret;

	t1 = syscall_time_now();
	ret = fchown(syscall_fd, syscall_uid, syscall_gid);
	t2 = syscall_time_now();
	return ret;
}

#if defined(HAVE_FCHOWNAT)
#define HAVE_SYSCALL_FCHOWNAT
static int syscall_fchownat(void)
{
	int ret;

	t1 = syscall_time_now();
	ret = fchownat(syscall_dir_fd, syscall_filename, syscall_uid, syscall_gid, 0);
	t2 = syscall_time_now();
	return ret;
}
#endif

#if defined(F_GETFL)
#define HAVE_SYSCALL_FCNTL
static int syscall_fcntl(void)
{
	int ret;

	t1 = syscall_time_now();
	ret = fcntl(syscall_fd, F_GETFL);
	t2 = syscall_time_now();
	return ret;
}
#endif

#if defined(HAVE_FDATASYNC)
#define HAVE_SYSCALL_FDATASYNC
static int syscall_fdatasync(void)
{
	int ret;

	t1 = syscall_time_now();
	ret = fdatasync(syscall_fd);
	t2 = syscall_time_now();
	return ret;
}
#endif

#if (defined(HAVE_SYS_XATTR_H) || defined(HAVE_ATTR_XATTR_H)) && \
     defined(HAVE_FGETXATTR) &&		\
     defined(HAVE_FSETXATTR) &&		\
     defined(HAVE_FREMOVEXATTR)
#define HAVE_SYSCALL_FGETXATTR
static int syscall_fgetxattr(void)
{
	int ret;
	char buf[64];

	VOID_RET(int, shim_fsetxattr(syscall_fd, syscall_xattr_name, "123", 3, 0));
	t1 = syscall_time_now();
	ret = shim_fgetxattr(syscall_fd, syscall_xattr_name, buf, sizeof(buf));
	t2 = syscall_time_now();
	VOID_RET(int, shim_fremovexattr(syscall_fd, syscall_xattr_name));
	return ret;
}
#endif

#if defined(HAVE_SYS_SYSINFO_H) &&      \
    defined(HAVE_SYSINFO) &&            \
    defined(HAVE_SYS_STATFS_H)
#define HAVE_SYSCALL_FSTATFS
static int syscall_fstatfs(void)
{
	struct statfs statfsbuf;
	int ret;

	t1 = syscall_time_now();
	ret = fstatfs(syscall_dir_fd, &statfsbuf);
	t2 = syscall_time_now();
	return ret;
}
#endif

#if defined(HAVE_FSYNC)
#define HAVE_SYSCALL_FSYNC
static int syscall_fsync(void)
{
	int ret;

	t1 = syscall_time_now();
	ret = fsync(syscall_fd);
	t2 = syscall_time_now();
	return ret;
}
#endif

#if (defined(HAVE_SYS_XATTR_H) || defined(HAVE_ATTR_XATTR_H)) && \
     defined(HAVE_FLISTXATTR) &&	\
     defined(HAVE_FSETXATTR) &&		\
     defined(HAVE_FREMOVEXATTR)
#define HAVE_SYSCALL_FLISTXATTR
static int syscall_flistxattr(void)
{
	ssize_t ret;

	VOID_RET(int, shim_fsetxattr(syscall_fd, syscall_xattr_name, "123", 3, 0));
	t1 = syscall_time_now();
	ret = shim_flistxattr(syscall_fd, NULL, 0);
	t2 = syscall_time_now();
	VOID_RET(int, shim_fremovexattr(syscall_fd, syscall_xattr_name));
	return (int)ret;
}
#endif

#if defined(HAVE_FLOCK) &&      \
    defined(LOCK_EX) &&         \
    defined(LOCK_UN)
#define HAVE_SYSCALL_FLOCK
static int syscall_flock(void)
{
	int ret;

	t1 = syscall_time_now();
	ret = flock(syscall_fd, LOCK_EX);
	t2 = syscall_time_now();

	VOID_RET(int, flock(syscall_fd, LOCK_UN));
	return ret;
}
#endif

#define HAVE_SYSCALL_FORK
static int syscall_fork(void)
{
	pid_t pid;

	t1 = syscall_time_now();
	pid = fork();
	if (pid < 0) {
		return -1;
	} else if (pid == 0) {
		_exit(0);
	} else {
		int status;

		t2 = syscall_time_now();
		VOID_RET(pid_t, waitpid(pid, &status, 0));
	}
	return 0;
}

#if (defined(HAVE_SYS_XATTR_H) || defined(HAVE_ATTR_XATTR_H)) && \
     defined(HAVE_FSETXATTR) &&		\
     defined(HAVE_FREMOVEXATTR)
#define HAVE_SYSCALL_FREMOVEXATTR
static int syscall_fremovexattr(void)
{
	int ret;

	VOID_RET(int, shim_fsetxattr(syscall_fd, syscall_xattr_name, "123", 3, 0));
	t1 = syscall_time_now();
	ret = shim_fremovexattr(syscall_fd, syscall_xattr_name);
	t2 = syscall_time_now();
	return ret;
}
#endif

#if (defined(HAVE_SYS_XATTR_H) || defined(HAVE_ATTR_XATTR_H)) && \
     defined(HAVE_FSETXATTR) &&		\
     defined(HAVE_FREMOVEXATTR)
#define HAVE_SYSCALL_FSETXATTR
static int syscall_fsetxattr(void)
{
	int ret;

	t1 = syscall_time_now();
	ret = shim_fsetxattr(syscall_fd, syscall_xattr_name, "123", 3, 0);
	t2 = syscall_time_now();
	VOID_RET(int, shim_fremovexattr(syscall_fd, syscall_xattr_name));
	return ret;
}
#endif

#define HAVE_SYSCALL_FSTAT
static int syscall_fstat(void)
{
	struct stat statbuf;
	int ret;

	t1 = syscall_time_now();
	ret = shim_fstat(syscall_fd, &statbuf);
	t2 = syscall_time_now();
	return ret;
}

#if defined(HAVE_FSTATAT)
#define HAVE_SYSCALL_FSTATAT
static int syscall_fstatat(void)
{
	struct stat statbuf;
	int ret;

	t1 = syscall_time_now();
	ret = fstatat(syscall_dir_fd, syscall_filename, &statbuf, 0);
	t2 = syscall_time_now();
	return ret;
}
#endif

#define HAVE_SYSCALL_FTRUNCATE
static int syscall_ftruncate(void)
{
	const off_t size = (off_t)65536;
	int ret;

	t1 = syscall_time_now();
	ret = ftruncate(syscall_fd, size);
	t2 = syscall_time_now();
	return ret;
}

#if defined(HAVE_FUTIMES)
#define HAVE_SYSCALL_FUTIMES
static int syscall_futimes(void)
{
	struct timeval tvs[2];
	int ret;

	VOID_RET(int, gettimeofday(&tvs[0], NULL));
	tvs[1] = tvs[0];

	t1 = syscall_time_now();
	ret = futimes(syscall_fd, tvs);
	t2 = syscall_time_now();
	return ret;
}
#endif

#if defined(HAVE_FUTIMESAT_DEPRECATED)
#define HAVE_SYSCALL_FUTIMESAT
static int syscall_futimesat(void)
{
	struct timeval tvs[2];
	int ret;

	VOID_RET(int, gettimeofday(&tvs[0], NULL));
	tvs[1] = tvs[0];

	t1 = syscall_time_now();
	ret = futimesat(syscall_dir_fd, syscall_filename, tvs);
	t2 = syscall_time_now();
	return ret;
}
#endif

#if defined(HAVE_GETCPU)
#define HAVE_SYSCALL_GETCPU
static int syscall_getcpu(void)
{
	unsigned cpu, node;
	int ret;

	t1 = syscall_time_now();
	ret = shim_getcpu(&cpu, &node, NULL);
	t2 = syscall_time_now();
	return ret;
}
#endif

#define HAVE_SYSCALL_GETCWD
static int syscall_getcwd(void)
{
	char path[PATH_MAX];
	char *ptr;

	t1 = syscall_time_now();
	ptr =  getcwd(path, sizeof(path));
	t2 = syscall_time_now();
	return (ptr == NULL) ? -1 : 0;
}

#if defined(__linux__) &&	\
    defined(HAVE_SYSCALL) &&	\
    defined(__NR_getdents) &&	\
    defined(O_DIRECTORY)
#define HAVE_SYSCALL_GETDENTS
static int syscall_getdents(void)
{
	int fd, ret;
	struct shim_linux_dirent *buf;
	const size_t ndents = 32;

	buf = (struct shim_linux_dirent *)calloc(ndents, sizeof(*buf));
	if (!buf)
		return -1;

	fd = open("/", O_RDONLY | O_DIRECTORY);
	if (fd < 0) {
		free(buf);
		return -1;
	}

	t1 = syscall_time_now();
	ret = shim_getdents(fd, buf, ndents * sizeof(*buf));
	t2 = syscall_time_now();
	(void)close(fd);
	free(buf);
	return ret;
}
#endif

#define HAVE_SYSCALL_GETEGID
static int syscall_getegid(void)
{
	t1 = syscall_time_now();
	VOID_RET(gid_t, getegid());
	t2 = syscall_time_now();
	return 0;
}

#define HAVE_SYSCALL_GETEUID
static int syscall_geteuid(void)
{
	t1 = syscall_time_now();
	VOID_RET(uid_t, geteuid());
	t2 = syscall_time_now();
	return 0;
}

#define HAVE_SYSCALL_GETGID
static int syscall_getgid(void)
{
	t1 = syscall_time_now();
	VOID_RET(gid_t, getgid());
	t2 = syscall_time_now();
	return 0;
}

#if defined(HAVE_GETPGRP)
#define HAVE_SYSCALL_GETPGRP
static int syscall_getpgrp(void)
{
	pid_t ret;

	t1 = syscall_time_now();
	ret = getpgrp();
	t2 = syscall_time_now();
	return (int)ret;
}
#endif

#define HAVE_SYSCALL_GETPID
static int syscall_getpid(void)
{
	t1 = syscall_time_now();
	VOID_RET(pid_t, getpid());
	t2 = syscall_time_now();
	return 0;
}

#if defined(HAVE_GETGROUPS)
#define HAVE_SYSCALL_GETGROUPS
static int syscall_getgroups(void)
{
	gid_t groups[1024];
	int ret;

	t1 = syscall_time_now();
	ret = getgroups(SIZEOF_ARRAY(groups), groups);
	t2 = syscall_time_now();
	return ret;
}
#endif

#if defined(HAVE_GETITIMER)
#define HAVE_SYSCALL_GETITIMER
static int syscall_getitimer(void)
{
	static size_t i = 0;

	struct itimerval val;
	int ret;
	const shim_itimer_which_t itimer = itimers[i];

	i++;
	if (i >= SIZEOF_ARRAY(itimers))
		i = 0;

	t1 = syscall_time_now();
	ret = getitimer(itimer, &val);
	t2 = syscall_time_now();
	return ret;
}
#endif

#if defined(HAVE_LINUX_MEMPOLICY_H) &&	\
    defined(HAVE_SYSCALL) &&		\
    defined(__NR_get_mempolicy) &&	\
    defined(MPOL_F_ADDR)
#define HAVE_SYSCALL_GET_MEMPOLICY
static int syscall_get_mempolicy(void)
{
	unsigned long int node_mask[NUMA_LONG_BITS];
	unsigned long int max_nodes = 1;
	int ret, mode;
	void *buf;

	buf = mmap(NULL, syscall_page_size, PROT_READ | PROT_WRITE,
		MAP_ANONYMOUS | MAP_SHARED, -1, 0);
	if (buf == MAP_FAILED)
		return -1;

	t1 = syscall_time_now();
	ret = shim_get_mempolicy(&mode, node_mask, max_nodes, buf, MPOL_F_ADDR);
	t2 = syscall_time_now();
	(void)munmap(buf, syscall_page_size);
	return ret;
}
#endif

#if defined(HAVE_SYS_UN_H) &&	\
    defined(AF_UNIX)
#define HAVE_SYSCALL_GETPEERNAME
static int syscall_getpeername(void)
{
	return syscall_socket_measure(SOCK_MEASURE_GETPEERNAME);
}
#endif

#if defined(HAVE_GETPGID)
#define HAVE_SYSCALL_GETPGID
static int syscall_getpgid(void)
{
	pid_t ret;
	t1 = syscall_time_now();
	ret = getpgid(syscall_pid);
	t2 = syscall_time_now();
	return (int)ret;
}
#endif

#define HAVE_SYSCALL_GETPPID
static int syscall_getppid(void)
{
	t1 = syscall_time_now();
	VOID_RET(pid_t, getppid());
	t2 = syscall_time_now();
	return 0;
}

#if defined(HAVE_GETPRIORITY)
#define HAVE_SYSCALL_GETPRIORITY
static int syscall_getpriority(void)
{
	int ret;

	t1 = syscall_time_now();
	ret = getpriority(PRIO_PROCESS, syscall_pid);
	t2 = syscall_time_now();
	return ret;
}
#endif

#define HAVE_SYSCALL_GETRANDOM
static int syscall_getrandom(void)
{
#if defined(__OpenBSD__) ||     \
    defined(__APPLE__)
#define RANDOM_BUFFER_SIZE	(256)
#else
#define RANDOM_BUFFER_SIZE	(8192)
#endif
	int ret;
	char buffer[RANDOM_BUFFER_SIZE];

	t1 = syscall_time_now();
	ret = shim_getrandom(buffer, sizeof(buffer), 0);
	t2 = syscall_time_now();
	return ret;
}

#if defined(HAVE_GETRESGID)
#define HAVE_SYSCALL_GETRESGID
static int syscall_getresgid(void)
{
	uid_t ruid, euid, sgid;
	int ret;

	t1 = syscall_time_now();
	ret = getresgid(&ruid, &euid, &sgid);
	t2 = syscall_time_now();
	return ret;
}
#endif

#if defined(HAVE_GETRESUID)
#define HAVE_SYSCALL_GETRESUID
static int syscall_getresuid(void)
{
	uid_t ruid, euid, suid;
	int ret;

	t1 = syscall_time_now();
	ret = getresuid(&ruid, &euid, &suid);
	t2 = syscall_time_now();
	return ret;
}
#endif

#define HAVE_SYSCALL_GETRLIMIT
static int syscall_getrlimit(void)
{
	static size_t i = 0;

	struct rlimit rlim;
	int ret;
	const shim_rlimit_resource_t limit = limits[i];

	i++;
	if (i >= SIZEOF_ARRAY(limits))
		i = 0;
	t1 = syscall_time_now();
	ret = getrlimit(limit, &rlim);
	t2 = syscall_time_now();
	return ret;
}

#if defined(__NR_get_robust_list) &&	\
    defined(HAVE_LINUX_FUTEX_H) &&	\
    defined(HAVE_SYSCALL)
#define HAVE_SYSCALL_GET_ROBUST_LIST
static int syscall_get_robust_list(void)
{
	struct robust_list_head *head_ptr;
	size_t len_ptr;
	int ret;

	t1 = syscall_time_now();
	ret = (int)syscall(__NR_get_robust_list, syscall_pid, &head_ptr, &len_ptr);
	t2 = syscall_time_now();
	return ret;
}
#endif

#if defined(HAVE_GETRUSAGE) && \
    defined(RUSAGE_SELF)
#define HAVE_SYSCALL_GETRUSAGE
static int syscall_getrusage(void)
{
	struct rusage usage;
	int ret;

	t1 = syscall_time_now();
	ret = shim_getrusage(RUSAGE_SELF, &usage);
	t2 = syscall_time_now();
	return ret;
}
#endif

#if defined(HAVE_GETSID)
#define HAVE_SYSCALL_GETSID
static int syscall_getsid(void)
{
	int ret;

	t1 = syscall_time_now();
	ret = getsid(syscall_pid);
	t2 = syscall_time_now();
	return ret;
}
#endif

#if defined(HAVE_SYS_UN_H) &&	\
    defined(AF_UNIX)
#define HAVE_SYSCALL_GETSOCKNAME
static int syscall_getsockname(void)
{
	int sfd, ret;
	struct sockaddr addr;
	socklen_t len = sizeof(addr);

	sfd = socket(AF_UNIX, SOCK_STREAM, 0);
	if (sfd < 0)
		return -1;
	t1 = syscall_time_now();
	ret = getsockname(sfd, &addr, &len);
	t2 = syscall_time_now();
	(void)close(sfd);
	return ret;
}
#endif

#if defined(HAVE_SYS_UN_H) &&	\
    defined(AF_UNIX) &&		\
    defined(SOL_SOCKET)
#define HAVE_SYSCALL_GETSOCKOPT
static int syscall_getsockopt(void)
{
	int sfd, rcvbuf, ret;
	socklen_t len = sizeof(rcvbuf);

	sfd = socket(AF_UNIX, SOCK_STREAM, 0);
	if (sfd < 0)
		return -1;
	t1 = syscall_time_now();
	ret = getsockopt(sfd, SOL_SOCKET, SO_RCVBUF, &rcvbuf, &len);
	t2 = syscall_time_now();
	(void)close(sfd);
	return ret;
}
#endif

#if defined(HAVE_ASM_LDT_H) &&		\
    defined(STRESS_ARCH_X86) &&		\
    defined(HAVE_USER_DESC) &&		\
    defined(HAVE_SYSCALL) &&		\
    defined(__NR_get_thread_area)
#define HAVE_SYSCALL_GET_THREAD_AREA
static int syscall_get_thread_area(void)
{
	struct user_desc u_info;
	int ret;

	t1 = syscall_time_now();
	ret = (int)syscall(__NR_get_thread_area, &u_info);
	t2 = syscall_time_now();
	return ret;
}
#endif

#define HAVE_SYSCALL_GETTID
static int syscall_gettid(void)
{
	t1 = syscall_time_now();
	VOID_RET(pid_t, shim_gettid());
	t2 = syscall_time_now();
	return 0;
}

#define HAVE_SYSCALL_GETTIMEOFDAY
static int syscall_gettimeofday(void)
{
	struct timeval tv;
	int ret;

	t1 = syscall_time_now();
	ret = gettimeofday(&tv, NULL);
	t2 = syscall_time_now();
	return ret;
}

#define HAVE_SYSCALL_GETUID
static int syscall_getuid(void)
{
	t1 = syscall_time_now();
	VOID_RET(uid_t, getuid());
	t2 = syscall_time_now();
	return 0;
}

#if (defined(HAVE_SYS_XATTR_H) || defined(HAVE_ATTR_XATTR_H)) && \
     defined(HAVE_GETXATTR) &&		\
     defined(HAVE_SETXATTR) &&		\
     defined(HAVE_REMOVEXATTR)
#define HAVE_SYSCALL_GETXATTR
static int syscall_getxattr(void)
{
	char buf[64];
	int ret;

	VOID_RET(int, shim_setxattr(syscall_filename, syscall_xattr_name, "123", 3, 0));
	t1 = syscall_time_now();
	ret = shim_getxattr(syscall_filename, syscall_xattr_name, buf, sizeof(buf));
	t2 = syscall_time_now();
	VOID_RET(int, shim_removexattr(syscall_filename, syscall_xattr_name));
	return ret;
}
#endif

#if defined(HAVE_SYS_INOTIFY_H) &&	\
    defined(HAVE_INOTIFY) &&		\
    defined(IN_ACCESS) &&		\
    defined(IN_MODIFY) &&		\
    defined(IN_OPEN)
#define HAVE_SYSCALL_INOTIFY_ADD_WATCH
static int syscall_inotify_add_watch(void)
{
	int fd, wd;
	const int flags = IN_ACCESS | IN_MODIFY | IN_OPEN;

	fd = inotify_init();
	if (fd < 0)
		return -1;

	t1 = syscall_time_now();
	wd = inotify_add_watch(fd, syscall_filename, flags);
	t2 = syscall_time_now();
	if (wd >= 0)
		VOID_RET(int, inotify_rm_watch(fd, wd));
	(void)close(fd);
	return wd;
}
#endif

#if defined(HAVE_SYS_INOTIFY_H) &&	\
    defined(HAVE_INOTIFY)
#define HAVE_SYSCALL_INOTIFY_INIT
static int syscall_inotify_init(void)
{
	int fd;

	t1 = syscall_time_now();
	fd = inotify_init();
	t2 = syscall_time_now();
	if (fd >= 0)
		(void)close(fd);
	return fd;
}
#endif

#if defined(HAVE_SYS_INOTIFY_H) &&	\
    defined(HAVE_INOTIFY1) &&		\
    defined(IN_NONBLOCK)
#define HAVE_SYSCALL_INOTIFY_INIT1
static int syscall_inotify_init1(void)
{
	int fd;

	t1 = syscall_time_now();
	fd = inotify_init1(IN_NONBLOCK);
	t2 = syscall_time_now();
	if (fd >= 0)
		(void)close(fd);
	return fd;
}
#endif

#if defined(HAVE_SYS_INOTIFY_H) &&	\
    defined(HAVE_INOTIFY) &&		\
    defined(IN_ACCESS) &&		\
    defined(IN_MODIFY) &&		\
    defined(IN_OPEN)
#define HAVE_SYSCALL_INOTIFY_RM_WATCH
static int syscall_inotify_rm_watch(void)
{
	int fd, wd, ret;
	const int flags = IN_ACCESS | IN_MODIFY | IN_OPEN;

	fd = inotify_init();
	if (fd < 0)
		return -1;
	wd = inotify_add_watch(fd, syscall_filename, flags);
	if (wd < 0) {
		(void)close(fd);
		return -1;
	}

	t1 = syscall_time_now();
	ret = inotify_rm_watch(fd, wd);
	t2 = syscall_time_now();
	(void)close(fd);
	return ret;
}
#endif

#if defined(HAVE_LIB_AIO) &&		\
    defined(HAVE_LIBAIO_H) &&		\
    defined(HAVE_SYSCALL) &&		\
    defined(__NR_io_cancel) &&		\
    defined(__NR_io_submit) &&		\
    defined(__NR_io_setup) &&		\
    defined(__NR_io_destroy)
#define HAVE_SYSCALL_IO_CANCEL
static int syscall_io_cancel(void)
{
	int ret;
	io_context_t ctx = 0;
	struct iocb cb[1];
	struct iocb *cbs[1];
	struct io_event event;
	uint32_t buffer[128];

	ret = syscall(__NR_io_setup, 1, &ctx);
	if (ret < 0)
		return -1;

	stress_uint8rnd4((uint8_t *)buffer, sizeof(buffer));
	(void)shim_memset(&cb, 0, sizeof(cb));
	cb[0].aio_fildes = syscall_fd;
	cb[0].aio_lio_opcode = IO_CMD_PWRITE;
	cb[0].u.c.buf = buffer;
	cb[0].u.c.offset = 0;
	cb[0].u.c.nbytes = sizeof(buffer);
	cbs[0] = &cb[0];
	cb[0].key = 0xff;

	ret = syscall(__NR_io_submit, ctx, 1, cbs);
	if (ret < 0) {
		VOID_RET(int, syscall(__NR_io_destroy, ctx));
		return -1;
	}

	t1 = syscall_time_now();
	VOID_RET(int, syscall(__NR_io_cancel, ctx, &cb[0], &event));
	t2 = syscall_time_now();

	VOID_RET(int, syscall(__NR_io_destroy, ctx));
	return 0;
}
#endif

#if defined(HAVE_LIB_AIO) &&		\
    defined(HAVE_LIBAIO_H) &&		\
    defined(HAVE_SYSCALL) &&		\
    defined(__NR_io_destroy) &&		\
    defined(__NR_io_setup)
#define HAVE_SYSCALL_IO_DESTROY
static int syscall_io_destroy(void)
{
	int ret;
	io_context_t ctx = 0;

	ret = syscall(__NR_io_setup, 1, &ctx);
	if (ret < 0)
		return -1;
	t1 = syscall_time_now();
	ret = syscall(__NR_io_destroy, ctx);
	t2 = syscall_time_now();
	return ret;
}
#endif

#if defined(HAVE_LIB_AIO) &&		\
    defined(HAVE_LIBAIO_H) &&		\
    defined(HAVE_SYSCALL) &&		\
    defined(__NR_io_getevents) &&	\
    defined(__NR_io_destroy) &&		\
    defined(__NR_io_setup)
#define HAVE_SYSCALL_IO_GETEVENTS
static int syscall_io_getevents(void)
{
	int ret;
	io_context_t ctx = 0;
	struct io_event events;
	struct timespec timeout;

	ret = syscall(__NR_io_setup, 1, &ctx);
	if (ret < 0)
		return -1;
	timeout.tv_sec = 0;
	timeout.tv_nsec = 1;
	t1 = syscall_time_now();
	ret = syscall(__NR_io_getevents, ctx, 1, 1, &events, &timeout);
	t2 = syscall_time_now();
	VOID_RET(int, syscall(__NR_io_destroy, ctx));
	return ret;
}
#endif

#if defined(HAVE_LIB_AIO) &&		\
    defined(HAVE_LIBAIO_H) &&		\
    defined(HAVE_SYSCALL) &&		\
    defined(__NR_io_pgetevents) &&	\
    defined(__NR_io_destroy) &&		\
    defined(__NR_io_setup)
#define HAVE_SYSCALL_IO_PGETEVENTS
static int syscall_io_pgetevents(void)
{
	int ret;
	io_context_t ctx = 0;
	struct io_event events;
	struct timespec timeout;

	ret = syscall(__NR_io_setup, 1, &ctx);
	if (ret < 0)
		return -1;

	timeout.tv_sec = 0;
	timeout.tv_nsec = 1;

	t1 = syscall_time_now();
	ret = syscall(__NR_io_pgetevents, ctx, 1, 1, &events, &timeout, NULL);
	t2 = syscall_time_now();
	VOID_RET(int, syscall(__NR_io_destroy, ctx));
	return ret;
}
#endif

#if defined(HAVE_IOPRIO_GET)
#define HAVE_SYSCALL_IOPRIO_GET
static int syscall_ioprio_get(void)
{
	int ret;

	t1 = syscall_time_now();
	ret = shim_ioprio_get(IOPRIO_WHO_PROCESS, syscall_pid);
	t2 = syscall_time_now();
	return ret;
}
#endif

#if defined(HAVE_IOPRIO_SET)
#define HAVE_SYSCALL_IOPRIO_SET
static int syscall_ioprio_set(void)
{
	int ret;

	ret = shim_ioprio_get(IOPRIO_WHO_PROCESS, syscall_pid);
	if (ret < 0)
		return -1;

	t1 = syscall_time_now();
	ret = shim_ioprio_set(IOPRIO_WHO_PROCESS, syscall_pid, ret);
	t2 = syscall_time_now();
	return ret;
}
#endif

#if defined(HAVE_LIB_AIO) &&		\
    defined(HAVE_LIBAIO_H) &&		\
    defined(HAVE_SYSCALL) &&		\
    defined(__NR_io_setup) &&		\
    defined(__NR_io_destroy)
#define HAVE_SYSCALL_IO_SETUP
static int syscall_io_setup(void)
{
	int ret;
	io_context_t ctx = 0;

	t1 = syscall_time_now();
	ret = syscall(__NR_io_setup, 1, &ctx);
	t2 = syscall_time_now();
	if (ret >= 0)
		VOID_RET(int, syscall(__NR_io_destroy, ctx));
	return ret;
}
#endif

#if defined(HAVE_LIB_AIO) &&		\
    defined(HAVE_LIBAIO_H) &&		\
    defined(HAVE_SYSCALL) &&		\
    defined(__NR_io_submit) &&		\
    defined(__NR_io_setup) &&		\
    defined(__NR_io_getevents) &&	\
    defined(__NR_io_destroy)
#define HAVE_SYSCALL_IO_SUBMIT
static int syscall_io_submit(void)
{
	int ret, i;
	io_context_t ctx = 0;
	struct iocb cb[1];
	struct iocb *cbs[1];
	uint32_t buffer[128];

	ret = syscall(__NR_io_setup, 1, &ctx);
	if (ret < 0)
		return -1;

	stress_uint8rnd4((uint8_t *)buffer, sizeof(buffer));
	(void)shim_memset(&cb, 0, sizeof(cb));
	cb[0].aio_fildes = syscall_fd;
	cb[0].aio_lio_opcode = IO_CMD_PWRITE;
	cb[0].u.c.buf = buffer;
	cb[0].u.c.offset = 0;
	cb[0].u.c.nbytes = sizeof(buffer);
	cbs[0] = &cb[0];

	t1 = syscall_time_now();
	ret = syscall(__NR_io_submit, ctx, 1, cbs);
	t2 = syscall_time_now();

	if (ret < 0) {
		VOID_RET(int, syscall(__NR_io_destroy, ctx));
		return -1;
	}

	for (i = 0; i < 1000; i++) {
		struct timespec timeout;
		struct io_event events;

		timeout.tv_sec = 0;
		timeout.tv_nsec = 10000;
		ret = syscall(__NR_io_getevents, ctx, 1, 1, &events, &timeout);
		if (ret != 0)
			break;
		if (!stress_continue_flag())
			break;
	}
	VOID_RET(int, syscall(__NR_io_destroy, ctx));
	return ret;
}
#endif

#if defined(HAVE_LINUX_IO_URING_H) &&	\
    defined(HAVE_SYSCALL) &&		\
    defined(__NR_io_uring_setup)
#define HAVE_SYSCALL_IO_URING_SETUP
static int syscall_io_uring_setup(void)
{
	int fd;
	struct io_uring_params p[16];

	(void)shim_memset(&p, 0, sizeof(p));
	t1 = syscall_time_now();
	fd = syscall(__NR_io_uring_setup, (unsigned long int)SIZEOF_ARRAY(p), &p);
	t2 = syscall_time_now();
	if (fd >= 0)
		(void)close(fd);
	return fd;
}
#endif

#if defined(STRESS_ARCH_X86) &&	\
    defined(HAVE_SYS_IO_H) &&	\
    defined(HAVE_IOPORT)
#define HAVE_SYSCALL_IOPERM
static int syscall_ioperm(void)
{
	int ret;

	t1 = syscall_time_now();
	ret = ioperm(0x80, 1, 1);
	t2 = syscall_time_now();
	if (ret == 0)
		VOID_RET(int, ioperm(0x80, 1, 0));
	return ret;
}
#endif

#if defined(STRESS_ARCH_X86) &&	\
    defined(HAVE_SYS_IO_H) &&	\
    defined(HAVE_IOPL)
#define HAVE_SYSCALL_IOPL
static int syscall_iopl(void)
{
	int ret;

	t1 = syscall_time_now();
	ret = iopl(0);
	t2 = syscall_time_now();
	return ret;
}
#endif

#if defined(HAVE_FIONREAD)
#define HAVE_SYSCALL_IOCTL
static int syscall_ioctl(void)
{
	int nread, ret;

	t1 = syscall_time_now();
	ret = ioctl(0, FIONREAD, &nread);
	t2 = syscall_time_now();
	return ret;
}
#endif

#if defined(__NR_kcmp) &&	\
    defined(HAVE_SYSCALL)
#define HAVE_SYSCALL_KCMP
static int syscall_kcmp(void)
{
	int ret;
	pid_t ppid = getppid();

#if !defined(KCMP_FS)
#define KCMP_FS		(3)
#endif
	t1 = syscall_time_now();
	ret = shim_kcmp(syscall_pid, ppid, KCMP_FS, 0, 0);
	t2 = syscall_time_now();
	return ret;

}
#endif

#if defined(HAVE_KEYUTILS_H) &&	\
    defined(HAVE_SYSCALL) &&	\
    defined(__NR_keyctl) &&	\
    defined(__NR_add_key) &&	\
    defined(KEYCTL_INVALIDATE)
#define HAVE_SYSCALL_KEYCTL
static int syscall_keyctl(void)
{
	key_serial_t key;
	char ALIGN64 description[64];
	static char payload[] = "example payload";
	int ret;

	(void)snprintf(description, sizeof(description),
		"stress-ng-syscall-key-%" PRIdMAX, (intmax_t)syscall_pid);

	key = (key_serial_t)syscall(__NR_add_key, "user",
                description, payload, sizeof(payload), KEY_SPEC_PROCESS_KEYRING);
	if (key < 0)
		return -1;
	t1 = syscall_time_now();
	ret = syscall(__NR_keyctl, KEYCTL_INVALIDATE, key);
	t2 = syscall_time_now();
	return ret;
}
#endif

#define HAVE_SYSCALL_KILL
static int syscall_kill(void)
{
	int ret;
#if defined(SIGKILL)
	pid_t pid;

	pid = fork();
	if (pid < 0)
		return -1;
	if (pid == 0) {
		(void)shim_pause();
		_exit(0);
	} else {
		int status;

		t1 = syscall_time_now();
		ret = kill(pid, SIGKILL);
		t2 = syscall_time_now();

		VOID_RET(pid_t, waitpid(pid, &status, 0));
	}
#else
	t1 = syscall_time_now();
	ret = kill(syscall_pid, 0);
	t2 = syscall_time_now();
#endif
	return ret;
}

#define HAVE_SYSCALL_LCHOWN
static int syscall_lchown(void)
{
	int ret;

	t1 = syscall_time_now();
	ret = chown(syscall_filename, syscall_uid, syscall_gid);
	t2 = syscall_time_now();
	return ret;
}

#if (defined(HAVE_SYS_XATTR_H) || defined(HAVE_ATTR_XATTR_H)) && \
     defined(HAVE_LGETXATTR) &&		\
     defined(HAVE_LSETXATTR) &&		\
     defined(HAVE_LREMOVEXATTR)
#define HAVE_SYSCALL_LGETXATTR
static int syscall_lgetxattr(void)
{
	if (*syscall_symlink_filename) {
		char buf[64];
		int ret;

		if (shim_lsetxattr(syscall_symlink_filename, syscall_lxattr_name, "123", 3, 0) < 0)
			return -1;
		t1 = syscall_time_now();
		ret = shim_lgetxattr(syscall_symlink_filename, syscall_lxattr_name, buf, sizeof(buf));
		t2 = syscall_time_now();
		VOID_RET(int, shim_lremovexattr(syscall_symlink_filename, syscall_lxattr_name));
		return ret;
	}
	return -1;
}
#endif

#define HAVE_SYSCALL_LINK
static int syscall_link(void)
{
	int ret;

	t1 = syscall_time_now();
	ret = link(syscall_filename, syscall_tmp_filename);
	t2 = syscall_time_now();
	VOID_RET(int, shim_unlink(syscall_tmp_filename));
	return ret;
}

#if defined(HAVE_LINKAT)
#define HAVE_SYSCALL_LINKAT
static int syscall_linkat(void)
{
	int ret;

	t1 = syscall_time_now();
	ret = linkat(syscall_dir_fd, syscall_filename,
		     syscall_dir_fd, syscall_tmp_filename, 0);
	t2 = syscall_time_now();
	VOID_RET(int, shim_unlink(syscall_tmp_filename));
	return ret;
}
#endif

#if defined(HAVE_SYS_UN_H) &&	\
    defined(AF_UNIX)
#define HAVE_SYSCALL_LISTEN
static int syscall_listen(void)
{
	return syscall_socket_measure(SOCK_MEASURE_LISTEN);
}
#endif

#if (defined(HAVE_SYS_XATTR_H) || defined(HAVE_ATTR_XATTR_H)) && \
     defined(HAVE_LISTXATTR) &&	\
     defined(HAVE_SETXATTR) &&		\
     defined(HAVE_REMOVEXATTR)
#define HAVE_SYSCALL_LISTXATTR
static int syscall_listxattr(void)
{
	ssize_t ret;

	VOID_RET(int, shim_setxattr(syscall_filename, syscall_xattr_name, "123", 3, 0));
	t1 = syscall_time_now();
	ret = shim_listxattr(syscall_filename, NULL, 0);
	t2 = syscall_time_now();
	VOID_RET(int, shim_removexattr(syscall_filename, syscall_xattr_name));
	return (int)ret;
}
#endif

#if (defined(HAVE_SYS_XATTR_H) || defined(HAVE_ATTR_XATTR_H)) && \
     defined(HAVE_LLISTXATTR) &&	\
     defined(HAVE_SETXATTR) &&		\
     defined(HAVE_REMOVEXATTR)
#define HAVE_SYSCALL_LLISTXATTR
static int syscall_llistxattr(void)
{
	ssize_t ret;

	VOID_RET(int, shim_setxattr(syscall_filename, syscall_xattr_name, "123", 3, 0));
	t1 = syscall_time_now();
	ret = shim_llistxattr(syscall_filename, NULL, 0);
	t2 = syscall_time_now();
	VOID_RET(int, shim_removexattr(syscall_filename, syscall_xattr_name));
	return (int)ret;
}
#endif

#if defined(HAVE_LOOKUP_DCOOKIE) &&	\
    defined(HAVE_SYSCALL) &&		\
    defined(__NR_lookup_dcookie)
#define HAVE_SYSCALL_LOOKUP_DCOOKIE
static int syscall_lookup_dcookie(void)
{
	char buf[PATH_MAX];
	int ret;

	t1 = syscall_time_now();
	ret = syscall(__NR_lookup_dcookie, stress_mwc64(), buf, sizeof(buf));
	t2 = syscall_time_now();
	return ret;
}
#endif

#if (defined(HAVE_SYS_XATTR_H) || defined(HAVE_ATTR_XATTR_H)) && \
     defined(HAVE_LREMOVEXATTR) &&	\
     defined(HAVE_LSETXATTR)
#define HAVE_SYSCALL_LREMOVEXATTR
static int syscall_lremovexattr(void)
{
	if (*syscall_symlink_filename) {
		int ret;

		VOID_RET(int, shim_lsetxattr(syscall_symlink_filename, syscall_lxattr_name, "123", 3, 0));
		t1 = syscall_time_now();
		ret = shim_lremovexattr(syscall_symlink_filename, syscall_lxattr_name);
		t2 = syscall_time_now();
		return ret;
	}
	return -1;
}
#endif

#define HAVE_SYSCALL_LSEEK
static int syscall_lseek(void)
{
	static int i = 0;
	off_t offset;
	int whence;

	i++;
	if (i >= 3)
		i = 0;
	switch (i) {
	case 0:
		offset = (stress_mwc8() & 0x7) * 512;
		whence = SEEK_SET;
		break;
	case 1:
		offset = 16;
		whence = SEEK_CUR;
		break;
	case 2:
	default:
		offset = 0;
		whence = SEEK_END;
		break;
	}

	t1 = syscall_time_now();
	offset = lseek(syscall_fd, offset, whence);
	t2 = syscall_time_now();
	return (int)offset;
}

#if (defined(HAVE_SYS_XATTR_H) || defined(HAVE_ATTR_XATTR_H)) && \
     defined(HAVE_LSETXATTR) &&		\
     defined(HAVE_LREMOVEXATTR)
#define HAVE_SYSCALL_LSETXATTR
static int syscall_lsetxattr(void)
{
	if (*syscall_symlink_filename) {
		int ret;

		t1 = syscall_time_now();
		ret = shim_lsetxattr(syscall_symlink_filename, syscall_lxattr_name, "123", 3, 0);
		t2 = syscall_time_now();
		VOID_RET(int, shim_lremovexattr(syscall_symlink_filename, syscall_lxattr_name));
		return ret;
	}
	return -1;
}
#endif

#define HAVE_SYSCALL_LSTAT
static int syscall_lstat(void)
{
	int ret = -1;

	if (*syscall_symlink_filename) {
		struct stat statbuf;

		t1 = syscall_time_now();
		ret = shim_lstat(syscall_symlink_filename, &statbuf);
		t2 = syscall_time_now();
	}
	return ret;
}

#if defined(HAVE_LSM_GET_SELF_ATTR) && 	\
    defined(LSM_ATTR_CURRENT)
#define HAVE_SYSCALL_LSM_GET_SELF_ATTR
static int syscall_lsm_get_self_attr(void)
{
	int ret;
	char buf[4096];
	struct lsm_ctx *ctx = (struct lsm_ctx *)buf;
	size_t size = sizeof(buf);

	t1 = syscall_time_now();
	ret = lsm_get_self_attr(LSM_ATTR_CURRENT, ctx, &size, 0);
	t2 = syscall_time_now();

	return ret;
}
#endif

#if defined(HAVE_LSM_LIST_MODULES)
#define HAVE_SYSCALL_LSM_LIST_MODULES
static int syscall_lsm_list_modules(void)
{
	int ret;
	char buf[4096];
	uint64_t *ids = (uint64_t *)buf;
	size_t size = sizeof(buf);

	t1 = syscall_time_now();
	ret = lsm_list_modules(ids, &size, 0);
	t2 = syscall_time_now();

	return ret;
}
#endif

#if defined(HAVE_MADVISE)
#define HAVE_SYSCALL_MADVISE
static int syscall_madvise(void)
{
	int ret;

	t1 = syscall_time_now();
	ret = madvise(syscall_2_pages, syscall_2_pages_size, MADV_RANDOM);
	t2 = syscall_time_now();
	VOID_RET(int, madvise(syscall_2_pages, syscall_2_pages_size, MADV_NORMAL));
	return ret;
}
#endif

#if defined(HAVE_SYSCALL) &&		\
    defined(__NR_map_shadow_stack)
#define HAVE_SYSCALL_MAP_SHADOW_STACK
#if !defined(SHADOW_STACK_SET_TOKEN)
#define SHADOW_STACK_SET_TOKEN	STRESS_BIT_ULL(0)
#endif
static int syscall_map_shadow_stack(void)
{
	void *stack, *addr;
	const size_t stack_size = 0x20000;

	addr = NULL;
	stack = (void *)syscall(__NR_map_shadow_stack, addr, stack_size, SHADOW_STACK_SET_TOKEN);
	if (stack == MAP_FAILED)
		return -1;
	(void)munmap(stack, stack_size);

	addr = mmap(NULL, stack_size, PROT_READ | PROT_WRITE,
			MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
	if (addr != MAP_FAILED) {
		(void)munmap(addr, stack_size);
		stack = (void *)syscall(__NR_map_shadow_stack, addr, stack_size, SHADOW_STACK_SET_TOKEN);
		if (stack == MAP_FAILED)
			return -1;
		(void)munmap(stack, stack_size);
	}
	return 0;
}

#endif

#if defined(HAVE_LINUX_MEMPOLICY_H) &&	\
    defined(HAVE_SYSCALL) &&		\
    defined(__NR_mbind) &&		\
    defined(MPOL_BIND) &&		\
    defined(MPOL_DEFAULT)
#define HAVE_SYSCALL_MBIND
static int syscall_mbind(void)
{
	unsigned long int node_mask[NUMA_LONG_BITS];
	long int ret;
	void *buf;

	buf = mmap(NULL, syscall_page_size, PROT_READ | PROT_WRITE,
		MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
	if (buf == MAP_FAILED)
		return -1;

	(void)shim_memset(node_mask, 0, sizeof(node_mask));
	STRESS_SETBIT(node_mask, 0);
	t1 = syscall_time_now();
	ret = shim_mbind(buf, syscall_2_pages_size, MPOL_BIND, node_mask, sizeof(node_mask) * 8, MPOL_DEFAULT);
	t2 = syscall_time_now();
	(void)munmap(buf, syscall_page_size);
	return (int)ret;
}
#endif

#if defined(HAVE_LINUX_MEMBARRIER_H) &&	\
    defined(HAVE_SYSCALL) &&		\
    defined(__NR_membarrier)
#define HAVE_SYSCALL_MEMBARRIER
static int syscall_membarrier(void)
{
	int ret;

	t1 = syscall_time_now();
	ret = shim_membarrier(MEMBARRIER_CMD_QUERY, 0, 0);
	t2 = syscall_time_now();
	return ret;
}
#endif

#if defined(HAVE_MEMFD_CREATE)
#define HAVE_SYSCALL_MEMFD_CREATE
static int syscall_memfd_create(void)
{
	int fd;

	t1 = syscall_time_now();
	fd = memfd_create(syscall_tmp_filename, 0);
	t2 = syscall_time_now();
	if (fd < 0)
		return -1;
	(void)close(fd);

	return fd;
}
#endif

#if defined(HAVE_LINUX_MEMPOLICY_H) &&	\
    defined(HAVE_SYSCALL) &&		\
    defined(__NR_migrate_pages)
#define HAVE_SYSCALL_MIGRATE_PAGES
static int syscall_migrate_pages(void)
{
	long int ret;
	unsigned long int old_node_mask[NUMA_LONG_BITS];
	unsigned long int new_node_mask[NUMA_LONG_BITS];

	(void)shim_memset(old_node_mask, 0, sizeof(old_node_mask));
	STRESS_SETBIT(old_node_mask, 0);
	(void)shim_memset(new_node_mask, 0, sizeof(new_node_mask));
	STRESS_SETBIT(new_node_mask, 0);

	t1 = syscall_time_now();
	ret = shim_migrate_pages(syscall_pid, sizeof(old_node_mask) * 8, old_node_mask, new_node_mask);
	t2 = syscall_time_now();
	return (int)ret;
}
#endif

#if defined(HAVE_MINCORE)
#define HAVE_SYSCALL_MINCORE
static int syscall_mincore(void)
{
	unsigned char vec[2];
	int ret;

	t1 = syscall_time_now();
	ret = shim_mincore((void *)syscall_2_pages, syscall_2_pages_size, vec);
	t2 = syscall_time_now();
	return ret;
}
#endif

#define HAVE_SYSCALL_MKDIR
static int syscall_mkdir(void)
{
	int ret;

	t1 = syscall_time_now();
	ret = mkdir(syscall_tmp_filename, S_IRUSR | S_IWUSR);
	t2 = syscall_time_now();
	VOID_RET(int, shim_rmdir(syscall_tmp_filename));
	return ret;
}

#if defined(HAVE_MKDIRAT)
#define HAVE_SYSCALL_MKDIRAT
static int syscall_mkdirat(void)
{
	int ret;

	t1 = syscall_time_now();
	ret = mkdirat(syscall_dir_fd, syscall_tmp_filename, S_IRUSR | S_IWUSR);
	t2 = syscall_time_now();
	VOID_RET(int, shim_rmdir(syscall_tmp_filename));
	return ret;
}
#endif

#if defined(HAVE_MKNOD)
#define HAVE_SYSCALL_MKNOD
static int syscall_mknod(void)
{
	static size_t i = 0;
	int ret;
	dev_t dev;

	static const int modes[] = {
#if defined(S_IFIFO)
		S_IFIFO,
#endif
#if defined(S_IFREG)
		S_IFREG
#endif
	};

	i++;
	if (i >= SIZEOF_ARRAY(modes))
		i = 0;
	(void)shim_memset((void *)&dev, 0, sizeof(dev));
	t1 = syscall_time_now();
	ret = mknod(syscall_tmp_filename, modes[i], dev);
	t2 = syscall_time_now();
	(void)shim_unlink(syscall_tmp_filename);
	return ret;
}
#endif

#if defined(HAVE_MKNODAT) &&	\
    !defined(__APPLE__)
#define HAVE_SYSCALL_MKNODAT
static int syscall_mknodat(void)
{
	static size_t i = 0;
	int ret;
	dev_t dev;

	static const int modes[] = {
#if defined(S_IFIFO)
		S_IFIFO,
#endif
#if defined(S_IFREG)
		S_IFREG
#endif
	};

	i++;
	if (i >= SIZEOF_ARRAY(modes))
		i = 0;
	(void)shim_memset((void *)&dev, 0, sizeof(dev));
	t1 = syscall_time_now();
	ret = mknodat(syscall_dir_fd, syscall_tmp_filename, modes[i], dev);
	t2 = syscall_time_now();
	(void)shim_unlink(syscall_tmp_filename);
	return ret;
}
#endif

#if defined(HAVE_MSYNC)
#define HAVE_SYSCALL_MSYNC
static int syscall_msync(void)
{
	char *ptr;
	size_t j;
	static size_t i = 0;
	int ret;

	static const int flags[] = {
		MS_ASYNC,
		MS_SYNC,
		MS_INVALIDATE,
	};

	i++;
	if (i >= SIZEOF_ARRAY(flags))
		i = 0;

	ptr = mmap(NULL, syscall_page_size, PROT_READ | PROT_WRITE,
			MAP_SHARED, syscall_fd, 0);
	if (ptr == MAP_FAILED)
		return -1;
	for (j = 0; j < syscall_page_size; j++)
		*(ptr + j) = ~ *(ptr + j);

	t1 = syscall_time_now();
	ret = msync((void *)ptr, syscall_page_size, flags[i]);
	t2 = syscall_time_now();

	(void)munmap((void *)ptr, syscall_page_size);
	return ret;
}
#endif

#if defined(HAVE_MLOCK) &&	\
    defined(HAVE_MUNLOCK)
#define HAVE_SYSCALL_MLOCK
static int syscall_mlock(void)
{
	int ret;

	t1 = syscall_time_now();
	ret = shim_mlock(syscall_2_pages, syscall_2_pages_size);
	t2 = syscall_time_now();
	VOID_RET(int, shim_munlock(syscall_2_pages, syscall_2_pages_size));
	return ret;
}
#endif

#if defined(HAVE_MLOCK2) &&	\
    defined(HAVE_MUNLOCK)
#define HAVE_SYSCALL_MLOCK2
static int syscall_mlock2(void)
{
#ifndef MLOCK_ONFAULT
#define MLOCK_ONFAULT 1
#endif
	int ret;

	t1 = syscall_time_now();
	ret = shim_mlock2(syscall_2_pages, syscall_2_pages_size, MLOCK_ONFAULT);
	t2 = syscall_time_now();
	VOID_RET(int, shim_munlock(syscall_2_pages, syscall_2_pages_size));
	return ret;
}
#endif

#if defined(HAVE_MLOCKALL) &&	\
    defined(HAVE_MUNLOCKALL) &&	\
    defined(MCL_FUTURE)
#define HAVE_SYSCALL_MLOCKALL
static int syscall_mlockall(void)
{
	int ret;

	t1 = syscall_time_now();
	ret = shim_mlockall(MCL_FUTURE);
	t2 = syscall_time_now();
	VOID_RET(int, shim_munlockall());
	return ret;
}
#endif

#define HAVE_SYSCALL_MMAP
static int syscall_mmap(void)
{
	static int i = 0;

	void *ptr = (void *)MAP_FAILED;

	i++;
	if (i >= 12)
		i = 0;
	t1 = syscall_time_now();
	switch (i) {
	case 0:
		ptr = mmap(NULL, syscall_page_size, PROT_READ | PROT_WRITE,
				MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
		break;
	case 1:
		ptr = mmap(NULL, syscall_page_size, PROT_READ,
				MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
		break;
	case 2:
		ptr = mmap(NULL, syscall_page_size, PROT_WRITE,
				MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
		break;
	case 3:
		ptr = mmap(NULL, syscall_page_size, PROT_READ | PROT_WRITE,
				MAP_ANONYMOUS | MAP_SHARED, -1, 0);
		break;
	case 4:
		ptr = mmap(NULL, syscall_page_size, PROT_READ,
				MAP_ANONYMOUS | MAP_SHARED, -1, 0);
		break;
	case 5:
		ptr = mmap(NULL, syscall_page_size, PROT_WRITE,
				MAP_ANONYMOUS | MAP_SHARED, -1, 0);
		break;
	case 6:
		ptr = mmap(NULL, syscall_page_size, PROT_READ | PROT_WRITE,
				MAP_PRIVATE, syscall_fd, 0);
		break;
	case 7:
		ptr = mmap(NULL, syscall_page_size, PROT_READ,
				MAP_PRIVATE, syscall_fd, 0);
		break;
	case 8:
		ptr = mmap(NULL, syscall_page_size, PROT_WRITE,
				MAP_PRIVATE, syscall_fd, 0);
		break;
	case 9:
		ptr = mmap(NULL, syscall_page_size, PROT_READ | PROT_WRITE,
				MAP_SHARED, syscall_fd, 0);
		break;
	case 10:
		ptr = mmap(NULL, syscall_page_size, PROT_READ,
				MAP_SHARED, syscall_fd, 0);
		break;
	case 11:
		ptr = mmap(NULL, syscall_page_size, PROT_WRITE,
				MAP_SHARED, syscall_fd, 0);
		break;
	}
	t2 = syscall_time_now();
	if (ptr == MAP_FAILED)
		return -1;
	if (syscall_mmap_page == MAP_FAILED) {
		syscall_mmap_page = ptr;
	} else {
		(void)munmap(ptr, syscall_page_size);
	}
	return 0;
}

#if defined(HAVE_LINUX_MEMPOLICY_H) &&	\
    defined(HAVE_SYSCALL) &&		\
    defined(__NR_move_pages) &&		\
    defined(MPOL_MF_MOVE)
#define HAVE_SYSCALL_MOVE_PAGES
static int syscall_move_pages(void)
{
	long int ret;
	void *buf;
	void *pages[1];
	int status[1];
	int dest_nodes[1];

	buf = mmap(NULL, syscall_page_size, PROT_READ | PROT_WRITE,
		MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
	if (buf == MAP_FAILED)
		return -1;

	(void)shim_memset(buf, 0xff, syscall_page_size);
	pages[0] = buf;
	dest_nodes[0] = 0;
	status[0] = 0;

	t1 = syscall_time_now();
	ret = shim_move_pages(syscall_pid, 1, pages,
                                dest_nodes, status, MPOL_MF_MOVE);
	t2 = syscall_time_now();
	(void)munmap(buf, syscall_page_size);
	return (int)ret;
}
#endif

#if defined(HAVE_MPROTECT) &&	\
    defined(PROT_READ) && 	\
    defined(PROT_WRITE)
#define HAVE_SYSCALL_MPROTECT
static int syscall_mprotect(void)
{
	static size_t i = 0;
	int ret;

	static const int prot[] = {
#if defined(PROT_NONE)
		PROT_NONE,
#endif
		PROT_READ,
		PROT_WRITE,
#if defined(PROT_EXEC)
		PROT_EXEC,
#endif
#if defined(PROT_SEM)
		PROT_SEM,
#endif
#if defined(PROT_SAO)
		PROT_SAO,
#endif
	};

	i++;
	if (i >= SIZEOF_ARRAY(prot))
		i = 0;
	t1 = syscall_time_now();
	ret = mprotect(syscall_2_pages, syscall_2_pages_size, prot[i]);
	t2 = syscall_time_now();
	VOID_RET(int, mprotect(syscall_2_pages, syscall_2_pages_size, PROT_READ | PROT_WRITE));
	return ret;
}
#endif

#if defined(HAVE_MQUEUE_H) &&	\
    defined(HAVE_LIB_RT) && 	\
    defined(HAVE_MQ_POSIX)
#define HAVE_SYSCALL_MQ_CLOSE
static int syscall_mq_close(void)
{
	char mq_name[64];
	struct mq_attr attr;
	mqd_t mq;
	int ret;

	(void)snprintf(mq_name, sizeof(mq_name), "/stress-syscall-%" PRIdMAX "-%" PRIu32,
                (intmax_t)syscall_pid, stress_mwc32());

	attr.mq_flags = 0;
	attr.mq_maxmsg = 10;
	attr.mq_msgsize = sizeof(syscall_mq_msg_t);
	attr.mq_curmsgs = 0;

	mq = mq_open(mq_name, O_CREAT | O_RDWR, S_IRUSR | S_IWUSR, &attr);
	if (mq < 0) {
		(void)mq_unlink(mq_name);
		return -1;
	}

	t1 = syscall_time_now();
	ret = mq_close(mq);
	t2 = syscall_time_now();
	(void)mq_unlink(mq_name);

	return ret;
}
#endif

#if defined(HAVE_MQUEUE_H) &&	\
    defined(HAVE_LIB_RT) && 	\
    defined(HAVE_MQ_POSIX)
#define HAVE_SYSCALL_MQ_GETATTR
static int syscall_mq_getattr(void)
{
	char mq_name[64];
	struct mq_attr attr;
	mqd_t mq;
	int ret;

	(void)snprintf(mq_name, sizeof(mq_name), "/stress-syscall-%" PRIdMAX "-%" PRIu32,
                (intmax_t)syscall_pid, stress_mwc32());

	attr.mq_flags = 0;
	attr.mq_maxmsg = 10;
	attr.mq_msgsize = sizeof(syscall_mq_msg_t);
	attr.mq_curmsgs = 0;

	mq = mq_open(mq_name, O_CREAT | O_RDWR, S_IRUSR | S_IWUSR, &attr);
	if (mq < 0) {
		(void)mq_unlink(mq_name);
		return -1;
	}

	t1 = syscall_time_now();
	ret = mq_getattr(mq, &attr);
	t2 = syscall_time_now();
	(void)mq_close(mq);
	(void)mq_unlink(mq_name);
	return ret;
}
#endif

#if defined(HAVE_MQUEUE_H) &&	\
    defined(HAVE_LIB_RT) && 	\
    defined(HAVE_MQ_POSIX)
#define HAVE_SYSCALL_MQ_NOTIFY
static int syscall_mq_notify(void)
{
	char mq_name[64];
	struct mq_attr attr;
	mqd_t mq;
	int ret = -1;

	(void)snprintf(mq_name, sizeof(mq_name), "/stress-syscall-%" PRIdMAX "-%" PRIu32,
                (intmax_t)syscall_pid, stress_mwc32());

	attr.mq_flags = 0;
	attr.mq_maxmsg = 10;
	attr.mq_msgsize = sizeof(syscall_mq_msg_t);
	attr.mq_curmsgs = 0;

	mq = mq_open(mq_name, O_CREAT | O_RDWR, S_IRUSR | S_IWUSR, &attr);
	if (mq >= 0) {
		struct sigevent sev;

		(void)shim_memset(&sev, 0, sizeof(sev));
		sev.sigev_notify = SIGEV_SIGNAL;
		sev.sigev_signo = SIGUSR1;

		t1 = syscall_time_now();
		ret = mq_notify(mq, &sev);
		t2 = syscall_time_now();
		(void)mq_close(mq);
	}
	(void)mq_unlink(mq_name);
	return ret;
}
#endif

#if defined(HAVE_MQUEUE_H) &&	\
    defined(HAVE_LIB_RT) && 	\
    defined(HAVE_MQ_POSIX)
#define HAVE_SYSCALL_MQ_OPEN
static int syscall_mq_open(void)
{
	char mq_name[64];
	struct mq_attr attr;
	mqd_t mq;

	(void)snprintf(mq_name, sizeof(mq_name), "/stress-syscall-%" PRIdMAX "-%" PRIu32,
                (intmax_t)syscall_pid, stress_mwc32());

	attr.mq_flags = 0;
	attr.mq_maxmsg = 10;
	attr.mq_msgsize = sizeof(syscall_mq_msg_t);
	attr.mq_curmsgs = 0;

	t1 = syscall_time_now();
	mq = mq_open(mq_name, O_CREAT | O_RDWR, S_IRUSR | S_IWUSR, &attr);
	t2 = syscall_time_now();
	if (mq >= 0)
		(void)mq_close(mq);

	(void)mq_unlink(mq_name);
	return mq;
}
#endif

#if defined(HAVE_MQUEUE_H) &&	\
    defined(HAVE_LIB_RT) && 	\
    defined(HAVE_MQ_POSIX)
#define HAVE_SYSCALL_MQ_SETATTR
static int syscall_mq_setattr(void)
{
	char mq_name[64];
	struct mq_attr attr, old_attr;
	mqd_t mq;
	int ret;

	(void)snprintf(mq_name, sizeof(mq_name), "/stress-syscall-%" PRIdMAX "-%" PRIu32,
                (intmax_t)syscall_pid, stress_mwc32());

	attr.mq_flags = 0;
	attr.mq_maxmsg = 10;
	attr.mq_msgsize = sizeof(syscall_mq_msg_t);
	attr.mq_curmsgs = 0;

	mq = mq_open(mq_name, O_CREAT | O_RDWR, S_IRUSR | S_IWUSR, &attr);
	if (mq < 0) {
		(void)mq_unlink(mq_name);
		return -1;
	}
	ret = mq_getattr(mq, &attr);
	if (ret == 0) {
		t1 = syscall_time_now();
		ret = mq_setattr(mq, &attr, &old_attr);
		t2 = syscall_time_now();
	}
	(void)mq_close(mq);
	(void)mq_unlink(mq_name);
	return ret;
}
#endif

#if defined(HAVE_MQUEUE_H) &&	\
    defined(HAVE_LIB_RT) && 	\
    defined(HAVE_MQ_POSIX)
#define HAVE_SYSCALL_MQ_TIMEDRECEIVE
static int syscall_mq_timedreceive(void)
{
	char mq_name[64];
	struct mq_attr attr;
	struct timespec ts;
	mqd_t mq;
	int ret;
	syscall_mq_msg_t msg;

	(void)snprintf(mq_name, sizeof(mq_name), "/stress-syscall-%" PRIdMAX "-%" PRIu32,
                (intmax_t)syscall_pid, stress_mwc32());

	attr.mq_flags = 0;
	attr.mq_maxmsg = 10;
	attr.mq_msgsize = sizeof(syscall_mq_msg_t);
	attr.mq_curmsgs = 0;

	msg.value = stress_mwc64();
	mq = mq_open(mq_name, O_CREAT | O_RDWR, S_IRUSR | S_IWUSR, &attr);
	if (mq < 0) {
		(void)mq_unlink(mq_name);
		return -1;
	}

	ts.tv_sec = 0;
	ts.tv_nsec = 0;
	ret = mq_timedsend(mq, (const char *)&msg, sizeof(msg), 0, &ts);

	if (ret >= 0) {
		ssize_t sret;

		ts.tv_sec = 0;
		ts.tv_nsec = 0;
		t1 = syscall_time_now();
		sret = mq_timedreceive(mq, (char *)&msg, sizeof(msg), 0, &ts);
		t2 = syscall_time_now();
		ret = (int)sret;
	}
	(void)mq_close(mq);
	(void)mq_unlink(mq_name);
	return ret;
}
#endif

#if defined(HAVE_MQUEUE_H) &&	\
    defined(HAVE_LIB_RT) && 	\
    defined(HAVE_MQ_POSIX)
#define HAVE_SYSCALL_MQ_TIMEDSEND
static int syscall_mq_timedsend(void)
{
	char mq_name[64];
	struct mq_attr attr;
	struct timespec ts;
	mqd_t mq;
	int ret;
	syscall_mq_msg_t msg;

	(void)snprintf(mq_name, sizeof(mq_name), "/stress-syscall-%" PRIdMAX "-%" PRIu32,
                (intmax_t)syscall_pid, stress_mwc32());

	attr.mq_flags = 0;
	attr.mq_maxmsg = 10;
	attr.mq_msgsize = sizeof(syscall_mq_msg_t);
	attr.mq_curmsgs = 0;

	msg.value = stress_mwc64();
	mq = mq_open(mq_name, O_CREAT | O_RDWR, S_IRUSR | S_IWUSR, &attr);
	if (mq < 0) {
		(void)mq_unlink(mq_name);
		return -1;
	}

	ts.tv_sec = 0;
	ts.tv_nsec = 0;
	t1 = syscall_time_now();
	ret = mq_timedsend(mq, (const char *)&msg, sizeof(msg), 0, &ts);
	t2 = syscall_time_now();
	(void)mq_close(mq);
	(void)mq_unlink(mq_name);
	return ret;
}
#endif

#if defined(HAVE_MQUEUE_H) &&	\
    defined(HAVE_LIB_RT) && 	\
    defined(HAVE_MQ_POSIX)
#define HAVE_SYSCALL_MQ_UNLINK
static int syscall_mq_unlink(void)
{
	char mq_name[64];
	struct mq_attr attr;
	mqd_t mq;
	int ret;

	(void)snprintf(mq_name, sizeof(mq_name), "/stress-syscall-%" PRIdMAX "-%" PRIu32,
                (intmax_t)syscall_pid, stress_mwc32());

	attr.mq_flags = 0;
	attr.mq_maxmsg = 10;
	attr.mq_msgsize = sizeof(syscall_mq_msg_t);
	attr.mq_curmsgs = 0;

	mq = mq_open(mq_name, O_CREAT | O_RDWR, S_IRUSR | S_IWUSR, &attr);
	if (mq >= 0)
		(void)mq_close(mq);
	t1 = syscall_time_now();
	ret = mq_unlink(mq_name);
	t2 = syscall_time_now();
	return ret;
}
#endif

#if defined(HAVE_MREMAP)
#define HAVE_SYSCALL_MREMAP
static int syscall_mremap(void)
{
	void *new_addr, *old_addr;
	const size_t old_size = syscall_page_size;
	const size_t new_size = old_size << 1;

	old_addr = mmap(NULL, syscall_page_size, PROT_READ | PROT_WRITE,
			MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if (old_addr == MAP_FAILED)
		return -1;

	t1 = syscall_time_now();
	new_addr = mremap(old_addr, old_size, new_size, MREMAP_MAYMOVE, NULL);
	t2 = syscall_time_now();
	if (new_addr == MAP_FAILED) {
		(void)munmap(old_addr, old_size);
		return -1;
	}
	(void)munmap(new_addr, new_size);
	return 0;
}
#endif

#if defined(HAVE_SYS_IPC_H) &&  \
    defined(HAVE_SYS_MSG_H) &&  \
    defined(HAVE_MQ_SYSV)
#define HAVE_SYSCALL_MSGCTL
static int syscall_msgctl(void)
{
	int msgq_id, ret;

	msgq_id = msgget(IPC_PRIVATE, S_IRUSR | S_IWUSR | IPC_CREAT | IPC_EXCL);
	if (msgq_id < 0)
		return -1;
	t1 = syscall_time_now();
	ret = msgctl(msgq_id, IPC_RMID, NULL);
	t2 = syscall_time_now();
	return ret;
}
#endif


#if defined(HAVE_SYS_IPC_H) &&  \
    defined(HAVE_SYS_MSG_H) &&  \
    defined(HAVE_MQ_SYSV)
#define HAVE_SYSCALL_MSGGET
static int syscall_msgget(void)
{
	int msgq_id;

	t1 = syscall_time_now();
	msgq_id = msgget(IPC_PRIVATE, S_IRUSR | S_IWUSR | IPC_CREAT | IPC_EXCL);
	t2 = syscall_time_now();
	if (msgq_id < 0)
		return -1;
	VOID_RET(int, msgctl(msgq_id, IPC_RMID, NULL));
	return msgq_id;
}
#endif

#if defined(HAVE_SYS_IPC_H) &&  \
    defined(HAVE_SYS_MSG_H) &&  \
    defined(HAVE_MQ_SYSV)
#define HAVE_SYSCALL_MSGRCV
static int syscall_msgrcv(void)
{
	const uint32_t value = stress_mwc32();
	int msgq_id, ret;

	struct syscall_msgbuf {
		long int mtype;
		uint32_t value;
	} msg_snd, msg_rcv;

	msgq_id = msgget(IPC_PRIVATE, S_IRUSR | S_IWUSR | IPC_CREAT | IPC_EXCL);
	if (msgq_id < 0)
		return -1;

	msg_snd.mtype = 1;
	msg_snd.value = value;

	if (msgsnd(msgq_id, &msg_snd, sizeof(msg_snd.value), 0) < 0) {
		VOID_RET(int, msgctl(msgq_id, IPC_RMID, NULL));
		return -1;
	}
	t1 = syscall_time_now();
	ret = msgrcv(msgq_id, &msg_rcv, sizeof(msg_rcv.value), 1, 0);
	t2 = syscall_time_now();
	VOID_RET(int, msgctl(msgq_id, IPC_RMID, NULL));
	if (msg_rcv.value != value)
		return -1;
	return ret;
}
#endif

#if defined(HAVE_SYS_IPC_H) &&  \
    defined(HAVE_SYS_MSG_H) &&  \
    defined(HAVE_MQ_SYSV)
#define HAVE_SYSCALL_MSGSND
static int syscall_msgsnd(void)
{
	const uint32_t value = stress_mwc32();
	int msgq_id, ret;

	struct syscall_msgbuf {
		long int mtype;
		uint32_t value;
	} msg_snd;

	msgq_id = msgget(IPC_PRIVATE, S_IRUSR | S_IWUSR | IPC_CREAT | IPC_EXCL);
	if (msgq_id < 0)
		return -1;

	msg_snd.mtype = 1;
	msg_snd.value = value;

	t1 = syscall_time_now();
	ret = msgsnd(msgq_id, &msg_snd, sizeof(msg_snd.value), 0);
	t2 = syscall_time_now();
	VOID_RET(int, msgctl(msgq_id, IPC_RMID, NULL));
	return ret;
}
#endif

#if defined(HAVE_MLOCK) &&	\
    defined(HAVE_MUNLOCK)
#define HAVE_SYSCALL_MUNLOCK
static int syscall_munlock(void)
{
	int ret;

	ret = shim_mlock(syscall_2_pages, syscall_2_pages_size);
	if (ret < 0)
		return -1;
	t1 = syscall_time_now();
	ret = shim_munlock(syscall_2_pages, syscall_2_pages_size);
	t2 = syscall_time_now();
	return ret;

}
#endif

#if defined(HAVE_MLOCKALL) &&	\
    defined(HAVE_MUNLOCKALL) &&	\
    defined(MCL_FUTURE)
#define HAVE_SYSCALL_MUNLOCKALL
static int syscall_munlockall(void)
{
	int ret;

	ret = shim_mlockall(MCL_FUTURE);
	if (ret < 0)
		return -1;
	t1 = syscall_time_now();
	ret = shim_munlockall();
	t2 = syscall_time_now();
	return ret;
}
#endif

#define HAVE_SYSCALL_MUNMAP
static int syscall_munmap(void)
{
	int ret;

	if (syscall_mmap_page == MAP_FAILED)
		syscall_mmap_page = mmap(NULL, syscall_page_size, PROT_READ | PROT_WRITE,
					MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
	if (syscall_mmap_page == MAP_FAILED) {
		return -1;
	}
	t1 = syscall_time_now();
	ret = munmap(syscall_mmap_page, syscall_page_size);
	t2 = syscall_time_now();
	syscall_mmap_page = MAP_FAILED;
	return ret;
}

#if defined(HAVE_NAME_TO_HANDLE_AT)
#define HAVE_SYSCALL_NAME_TO_HANDLE_AT
static int syscall_name_to_handle_at(void)
{
	int ret, mount_id;
	struct file_handle *fhp, *tmp;

	fhp = (struct file_handle *)malloc(sizeof(*fhp));
	if (!fhp)
		return -1;

	fhp->handle_bytes = 0;
	ret = name_to_handle_at(AT_FDCWD, syscall_filename, fhp, &mount_id, 0);
	if ((ret < 0) && (errno != EOVERFLOW)) {
		free(fhp);
		return -1;
	}
	tmp = realloc(fhp, sizeof(*tmp) + fhp->handle_bytes);
	if (!tmp) {
		free(fhp);
		return -1;
	}
	fhp = tmp;
	t1 = syscall_time_now();
	ret = name_to_handle_at(AT_FDCWD, syscall_filename, fhp, &mount_id, 0);
	t2 = syscall_time_now();
	free(fhp);
	return ret;
}
#endif

#if defined(HAVE_NANOSLEEP)
#define HAVE_SYSCALL_NANOSLEEP
static int syscall_nanosleep(void)
{
	struct timespec req, rem;
	int ret;

	(void)shim_memset((void *)&rem, 0, sizeof(rem));
	req.tv_sec = 0;
	req.tv_nsec = 1;

	t1 = syscall_time_now();
	ret = nanosleep(&req, &rem);
	t2 = syscall_time_now();
	return ret;
}
#endif

#if defined(HAVE_NICE)
#define HAVE_SYSCALL_NICE
static int syscall_nice(void)
{
	t1 = syscall_time_now();
	errno = 0;
	VOID_RET(int, nice(0));
	t2 = syscall_time_now();

	return errno ? -errno : 0;
}
#endif

#define HAVE_SYSCALL_OPEN
static int syscall_open(void)
{
	int fd;

	t1 = syscall_time_now();
	fd = open(syscall_tmp_filename, O_RDWR | O_CREAT, S_IRUSR | S_IWUSR);
	t2 = syscall_time_now();
	if (fd < 0) {
		VOID_RET(int, shim_unlink(syscall_tmp_filename));
		return -1;
	}
	VOID_RET(int, close(fd));
	VOID_RET(int, shim_unlink(syscall_tmp_filename));
	return 0;
}

#if defined(HAVE_OPENAT)
#define HAVE_SYSCALL_OPENAT
static int syscall_openat(void)
{
	int fd;

	t1 = syscall_time_now();
	fd = openat(syscall_dir_fd, syscall_tmp_filename, O_RDWR | O_CREAT, S_IRUSR | S_IWUSR);
	t2 = syscall_time_now();
	if (fd < 0) {
		VOID_RET(int, shim_unlink(syscall_tmp_filename));
		return -1;
	}
	VOID_RET(int, close(fd));
	VOID_RET(int, shim_unlink(syscall_tmp_filename));
	return 0;
}
#endif

#if defined(HAVE_OPEN_BY_HANDLE_AT) &&	\
    defined(HAVE_OPEN_BY_HANDLE_AT)

#define XSTR(s) STR(s)
#define STR(s) #s

#define HAVE_SYSCALL_OPEN_BY_HANDLE_AT
static int syscall_open_by_handle_at(void)
{
	int ret, mount_id, mount_fd, fd;
	struct file_handle *fhp, *tmp;
	FILE *fp;
	char buffer[5000];
	char path[PATH_MAX + 1];

	fhp = (struct file_handle *)malloc(sizeof(*fhp));
	if (!fhp)
		return -1;

	fhp->handle_bytes = 0;
	ret = name_to_handle_at(AT_FDCWD, syscall_filename, fhp, &mount_id, 0);
	if ((ret < 0) && (errno != EOVERFLOW))
		goto err_free_fhp;
	tmp = realloc(fhp, sizeof(*tmp) + fhp->handle_bytes);
	if (!tmp)
		goto err_free_fhp;
	fhp = tmp;
	ret = name_to_handle_at(AT_FDCWD, syscall_filename, fhp, &mount_id, 0);
	if (ret < 0)
		goto err_free_fhp;

	fp = fopen("/proc/self/mountinfo", "r");
	if (!fp)
		goto err_free_fhp;

	(void)shim_memset(path, 0, sizeof(path));
	while (fgets(buffer, sizeof(buffer), fp) != NULL) {
		ssize_t n;
		int id;

		(void)shim_memset(path, 0, sizeof(path));
		n = sscanf(buffer, "%12d %*d %*s %*s %" XSTR(PATH_MAX) "s", &id, path);
		if ((n == 2) && (id == mount_id))
			break;
		*path = '\0';
	}
	(void)fclose(fp);

	if (!*path)
		goto err_free_fhp;

	mount_fd = open(path, O_RDONLY);
	if (mount_fd < 0)
		goto err_free_fhp;
	t1 = syscall_time_now();
	fd = open_by_handle_at(mount_fd, fhp, O_RDONLY);
	t2 = syscall_time_now();
	if (fd >= 0)
		(void)close(fd);
	(void)close(mount_fd);
	free(fhp);

	return fd;

err_free_fhp:
	free(fhp);
	return -1;
}
#undef XSTR
#undef STR

#endif

#define HAVE_SYSCALL_PAUSE
static int syscall_pause(void)
{
	pid_t pid;

	syscall_shared_error(0);

	pid = fork();
	if (pid < 0) {
		return -1;
	} else if (pid == 0) {
		syscall_shared_info->t1 = syscall_time_now();
		(void)shim_pause();
		syscall_shared_info->t2 = syscall_time_now();
		_exit(0);
	} else {
		for (;;) {
			pid_t ret;
			int status;

			VOID_RET(int, kill(pid, SIGUSR1));

			ret = waitpid(pid, &status, WNOHANG);
			if (ret == pid)
				break;
			(void)shim_sched_yield();
		}
	}
	t1 = syscall_shared_info->t1;
	t2 = syscall_shared_info->t2;
	return 0;
}

#if defined(HAVE_SYS_PERSONALITY_H) &&	\
    defined(HAVE_PERSONALITY)
#define HAVE_SYSCALL_PERSONALITY
static int syscall_personality(void)
{
	int ret;

	t1 = syscall_shared_info->t1;
	ret = personality(0xffffffffUL);	/* Get */
	t2 = syscall_shared_info->t1;
	return ret;
}
#endif

#if defined(HAVE_PIDFD_OPEN)
#define HAVE_SYSCALL_PIDFD_OPEN
static int syscall_pidfd_open(void)
{
	int pidfd;

	t1 = syscall_shared_info->t1;
	pidfd = shim_pidfd_open(syscall_pid, 0);
	t2 = syscall_shared_info->t1;
	if (pidfd >= 0)
		(void)close(pidfd);
	return pidfd;
}
#endif

#if defined(HAVE_PIDFD_SEND_SIGNAL)
#define HAVE_SYSCALL_PIDFD_SEND_SIGNAL
static int syscall_pidfd_send_signal(void)
{
	int pidfd, ret;

	pidfd = shim_pidfd_open(syscall_pid, 0);
	if (pidfd < 0)
		return -1;
	t1 = syscall_shared_info->t1;
	ret = shim_pidfd_send_signal(pidfd, 0, NULL, 0);
	t2 = syscall_shared_info->t1;
	(void)close(pidfd);
	return ret;
}
#endif

#define HAVE_SYSCALL_PIPE
static int syscall_pipe(void)
{
	int fds[2], ret;

	t1 = syscall_time_now();
	ret = pipe(fds);
	t2 = syscall_time_now();
	if (ret < 0)
		return -1;
	(void)close(fds[0]);
	(void)close(fds[1]);
	return 0;
}

#if defined(HAVE_PIPE2) &&	\
    defined(O_DIRECT)
#define HAVE_SYSCALL_PIPE2
static int syscall_pipe2(void)
{
	int fds[2], ret;

	t1 = syscall_time_now();
	ret = pipe2(fds, O_DIRECT);
	t2 = syscall_time_now();
	if (ret < 0)
		return -1;
	(void)close(fds[0]);
	(void)close(fds[1]);
	return 0;
}
#endif

#if defined(HAVE_PKEY_ALLOC) &&	\
    defined(HAVE_PKEY_FREE)
#define HAVE_SYSCALL_PKEY_ALLOC
static int syscall_pkey_alloc(void)
{
	int pkey;

	t1 = syscall_time_now();
	pkey = shim_pkey_alloc(0, 0);
	t2 = syscall_time_now();
	if (pkey >= 0)
		(void)shim_pkey_free(pkey);
	return pkey;
}
#endif

#if defined(HAVE_PKEY_FREE) &&	\
    defined(HAVE_PKEY_ALLOC)
#define HAVE_SYSCALL_PKEY_FREE
static int syscall_pkey_free(void)
{
	int pkey;

	pkey = shim_pkey_alloc(0, 0);
	if (pkey < 0)
		return -1;

	t1 = syscall_time_now();
	(void)shim_pkey_free(pkey);
	t2 = syscall_time_now();
	return 0;
}
#endif

#if defined(HAVE_PKEY_GET) &&	\
    defined(HAVE_PKEY_FREE) &&	\
    defined(HAVE_PKEY_ALLOC)
#define HAVE_SYSCALL_PKEY_GET
static int syscall_pkey_get(void)
{
	int pkey, rights;

	pkey = shim_pkey_alloc(0, 0);
	if (pkey < 0)
		return -1;

	t1 = syscall_time_now();
	rights = shim_pkey_get(pkey);
	t2 = syscall_time_now();

	(void)shim_pkey_free(pkey);
	return rights;
}
#endif

#if defined(HAVE_PKEY_MPROTECT)
#define HAVE_SYSCALL_PKEY_MPROTECT
static int syscall_pkey_mprotect(void)
{
	int ret;

	t1 = syscall_time_now();
	ret = shim_pkey_mprotect(syscall_2_pages,
		syscall_2_pages_size, PROT_READ | PROT_WRITE, -1);
	t2 = syscall_time_now();
	return ret;
}
#endif


#if defined(HAVE_PKEY_GET) &&	\
    defined(HAVE_PKEY_SET) &&	\
    defined(HAVE_PKEY_FREE) &&	\
    defined(HAVE_PKEY_ALLOC)
#define HAVE_SYSCALL_PKEY_SET
static int syscall_pkey_set(void)
{
	int pkey, rights, ret;

	pkey = shim_pkey_alloc(0, 0);
	if (pkey < 0)
		return -1;

	rights = shim_pkey_get(pkey);
	if (rights < 0) {
		(void)shim_pkey_free(pkey);
		return -1;
	}
	t1 = syscall_time_now();
	ret = shim_pkey_set(pkey, (unsigned int)rights);
	t2 = syscall_time_now();

	(void)shim_pkey_free(pkey);
	return ret;
}
#endif

#if defined(HAVE_POLL_H) &&	\
    defined(HAVE_POLL)
#define HAVE_SYSCALL_POLL
static int syscall_poll(void)
{
	struct pollfd fds[4];
	int ret;

	fds[0].fd = fileno(stdin);
	fds[0].events = POLLIN;
	fds[0].revents = 0;

	fds[1].fd = fileno(stdout);
	fds[1].events = POLLOUT;
	fds[1].revents = 0;

	fds[2].fd = fileno(stderr);
	fds[2].events = POLLOUT;
	fds[2].revents = 0;

	fds[3].fd = syscall_fd;
	fds[3].events = POLLIN | POLLOUT;
	fds[3].revents = 0;

	t1 = syscall_time_now();
	ret = poll(fds, SIZEOF_ARRAY(fds), 0);
	t2 = syscall_time_now();
	return ret;
}
#endif

#if defined(HAVE_POLL_H) &&	\
    defined(HAVE_PPOLL)
#define HAVE_SYSCALL_PPOLL
static int syscall_ppoll(void)
{
	struct pollfd fds[4];
	struct timespec ts;
	sigset_t sigmask;
	int ret;

	fds[0].fd = fileno(stdin);
	fds[0].events = POLLIN;
	fds[0].revents = 0;

	fds[1].fd = fileno(stdout);
	fds[1].events = POLLOUT;
	fds[1].revents = 0;

	fds[2].fd = fileno(stderr);
	fds[2].events = POLLOUT;
	fds[2].revents = 0;

	fds[3].fd = syscall_fd;
	fds[3].events = POLLIN | POLLOUT;
	fds[3].revents = 0;

	ts.tv_sec = 0;
	ts.tv_nsec = 1;

	VOID_RET(int, sigemptyset(&sigmask));

	t1 = syscall_time_now();
	ret = shim_ppoll(fds, SIZEOF_ARRAY(fds), &ts, &sigmask);
	t2 = syscall_time_now();
	return ret;
}
#endif

#if defined(HAVE_SYS_PRCTL_H) &&	\
    defined(HAVE_PRCTL)
#define HAVE_SYSCALL_PRCTL
static int syscall_prctl(void)
{
	int ret = -1;
	static size_t i = 0;

	static const int cmds[] = {
#if defined(PR_GET_CHILD_SUBREAPER)
		PR_GET_CHILD_SUBREAPER,
#endif
#if defined(PR_GET_DUMPABLE)
		PR_GET_DUMPABLE,
#endif
#if defined(PR_GET_KEEPCAPS)
		PR_GET_KEEPCAPS,
#endif
#if defined(PR_GET_NAME)
		PR_GET_NAME,
#endif
#if defined(PR_GET_NO_NEW_PRIVS)
		PR_GET_NO_NEW_PRIVS,
#endif
#if defined(PR_GET_PDEATHSIG)
		PR_GET_PDEATHSIG,
#endif
#if defined(PR_GET_THP_DISABLE)
		PR_GET_THP_DISABLE,
#endif
#if defined(PR_GET_TIMERSLACK)
		PR_GET_TIMERSLACK,
#endif
	};

	i++;
	if (i >= SIZEOF_ARRAY(cmds))
		i = 0;

	switch (cmds[i]) {
#if defined(PR_GET_CHILD_SUBREAPER)
	case PR_GET_CHILD_SUBREAPER:
		{
			int reaper = 0;

			t1 = syscall_time_now();
			ret = prctl(PR_GET_CHILD_SUBREAPER, &reaper, 0, 0, 0);
			t2 = syscall_time_now();
		}
		break;
#endif
#if defined(PR_GET_DUMPABLE)
	case PR_GET_DUMPABLE:
		t1 = syscall_time_now();
		ret = prctl(PR_GET_DUMPABLE, 0, 0, 0, 0);
		t2 = syscall_time_now();
		break;
#endif
#if defined(PR_GET_KEEPCAPS)
	case PR_GET_KEEPCAPS:
		{
			int flag = 0;

			t1 = syscall_time_now();
			ret = prctl(PR_GET_KEEPCAPS, &flag, 0, 0, 0);
			t2 = syscall_time_now();
		}
		break;
#endif
#if defined(PR_GET_NAME)
	case PR_GET_NAME:
		{
			char name[17];

			(void)shim_memset(name, 0, sizeof name);
			t1 = syscall_time_now();
			ret = prctl(PR_GET_NAME, name, 0, 0, 0);
			t2 = syscall_time_now();
		}
		break;
#endif
#if defined(PR_GET_NO_NEW_PRIVS)
	case PR_GET_NO_NEW_PRIVS:
		t1 = syscall_time_now();
		ret = prctl(PR_GET_NO_NEW_PRIVS, 0, 0, 0, 0);
		t2 = syscall_time_now();
		break;
#endif
#if defined(PR_GET_PDEATHSIG)
	case PR_GET_PDEATHSIG:
		{
			int sig = 0;

			t1 = syscall_time_now();
			ret = prctl(PR_GET_PDEATHSIG, &sig, 0, 0, 0);
			t2 = syscall_time_now();
		}
		break;
#endif
#if defined(PR_GET_THP_DISABLE)
	case PR_GET_THP_DISABLE:
		t1 = syscall_time_now();
		ret = prctl(PR_GET_THP_DISABLE, 0, 0, 0, 0);
		t2 = syscall_time_now();
		break;
#endif
#if defined(PR_GET_TIMERSLACK)
	case PR_GET_TIMERSLACK:
		t1 = syscall_time_now();
		ret = prctl(PR_GET_TIMERSLACK, 0, 0, 0, 0);
		t2 = syscall_time_now();
		break;
#endif
	default:
		break;
	}

	return ret;
}
#endif

#define HAVE_SYSCALL_PREAD
static int syscall_pread(void)
{
	char buffer[512];
	ssize_t ret;
	off_t offset = (stress_mwc8() & 0x7) * 512;

	t1 = syscall_time_now();
	ret = pread(syscall_fd, buffer, sizeof(buffer), offset);
	t2 = syscall_time_now();
	return (int)ret;
}

#if defined(HAVE_SYS_UIO_H) &&	\
    defined(HAVE_PREADV)
#define HAVE_SYSCALL_PREADV
static int syscall_preadv(void)
{
	char buffer[1024];
	ssize_t ret;
	off_t offset = (stress_mwc8() & 0x7) * 512;
	struct iovec iov[2];

	iov[0].iov_base = &buffer[0];
	iov[0].iov_len = 512;

	iov[1].iov_base = &buffer[512];
	iov[1].iov_len = 512;

	t1 = syscall_time_now();
	ret = preadv(syscall_fd, iov, SIZEOF_ARRAY(iov), offset);
	t2 = syscall_time_now();
	return (int)ret;
}
#endif

#if defined(HAVE_SYS_UIO_H) &&	\
    defined(HAVE_PREADV2)
#define HAVE_SYSCALL_PREADV2
static int syscall_preadv2(void)
{
	char buffer[1024];
	ssize_t ret;
	off_t offset = (stress_mwc8() & 0x7) * 512;
	struct iovec iov[2];

	iov[0].iov_base = &buffer[0];
	iov[0].iov_len = 512;

	iov[1].iov_base = &buffer[512];
	iov[1].iov_len = 512;

	t1 = syscall_time_now();
	ret = preadv2(syscall_fd, iov, SIZEOF_ARRAY(iov), offset, 0);
	t2 = syscall_time_now();
	return (int)ret;
}
#endif

#if defined(HAVE_PRLIMIT)
#define HAVE_SYSCALL_PRLIMIT
static int syscall_prlimit(void)
{
	static size_t i = 0;

	struct rlimit old_rlim, new_rlim;
	int ret;
	const shim_rlimit_resource_t limit = limits[i];

	i++;
	if (i >= SIZEOF_ARRAY(limits))
		i = 0;
	ret = prlimit(syscall_pid, limit, NULL, &old_rlim);
	if (ret < 0)
		return -1;

	new_rlim = old_rlim;
	new_rlim.rlim_cur = new_rlim.rlim_max;
	t1 = syscall_time_now();
	ret = prlimit(syscall_pid, limit, &new_rlim, NULL);
	t2 = syscall_time_now();
	VOID_RET(int, prlimit(syscall_pid, limit, &old_rlim, NULL));
	return ret;
}
#endif

#if defined(HAVE_SYS_UIO_H) &&	\
    defined(HAVE_PROCESS_VM_READV)
#define HAVE_SYSCALL_PROCESS_VM_READV
static int syscall_process_vm_readv(void)
{
	struct iovec local[1], remote[1];
	void *buf, *local_buf, *remote_buf;
	int ret;

	buf = mmap(NULL, syscall_page_size * 2, PROT_READ | PROT_WRITE,
		MAP_ANONYMOUS | MAP_SHARED, -1, 0);
	if (buf == MAP_FAILED)
		return -1;
	local_buf = buf;
	remote_buf = (void *)(((uintptr_t)buf) + syscall_page_size);

	(void)shim_memset(remote_buf, 0x5a, syscall_page_size);

	local[0].iov_base = local_buf;
	local[0].iov_len = syscall_page_size;
	remote[0].iov_base = remote_buf;
	remote[0].iov_len = syscall_page_size;

	t1 = syscall_time_now();
	ret = process_vm_readv(syscall_pid, local, 1, remote, 1, 0);
	t2 = syscall_time_now();
	(void)munmap(buf, syscall_page_size * 2);
	return ret;
}
#endif

#if defined(HAVE_SYS_UIO_H) &&	\
    defined(HAVE_PROCESS_VM_WRITEV)
#define HAVE_SYSCALL_PROCESS_VM_WRITEV
static int syscall_process_vm_writev(void)
{
	struct iovec local[1], remote[1];
	void *buf, *local_buf, *remote_buf;
	int ret;

	buf = mmap(NULL, syscall_page_size * 2, PROT_READ | PROT_WRITE,
		MAP_ANONYMOUS | MAP_SHARED, -1, 0);
	if (buf == MAP_FAILED)
		return -1;
	local_buf = buf;
	remote_buf = (void *)(((uintptr_t)buf) + syscall_page_size);

	(void)shim_memset(local_buf, 0xa5, syscall_page_size);

	local[0].iov_base = local_buf;
	local[0].iov_len = syscall_page_size;
	remote[0].iov_base = remote_buf;
	remote[0].iov_len = syscall_page_size;

	t1 = syscall_time_now();
	ret = process_vm_writev(syscall_pid, local, 1, remote, 1, 0);
	t2 = syscall_time_now();
	(void)munmap(buf, syscall_page_size * 2);
	return ret;
}
#endif

#if defined(HAVE_PSELECT)
#define HAVE_SYSCALL_PSELECT
static int syscall_pselect(void)
{
	fd_set rd_set, wr_set;
	int fds[4], nfds = -1, ret;
	size_t i;
	struct timespec ts;
	sigset_t sigmask;

	fds[0] = fileno(stdin);
	fds[1] = fileno(stdout);
	fds[2] = fileno(stderr);
	fds[3] = syscall_fd;

	for (i = 0; i < SIZEOF_ARRAY(fds); i++)
		if (nfds < fds[i])
			nfds = fds[i];

	FD_ZERO(&rd_set);
	FD_SET(fds[0], &rd_set);
	FD_SET(fds[3], &rd_set);

	FD_ZERO(&wr_set);
	FD_SET(fds[1], &wr_set);
	FD_SET(fds[2], &wr_set);

	ts.tv_sec = 0;
	ts.tv_nsec = 0;

	VOID_RET(int, sigemptyset(&sigmask));

	t1 = syscall_time_now();
	ret = pselect(nfds + 1, &rd_set, &wr_set, NULL, &ts, &sigmask);
	t2 = syscall_time_now();
	return ret;
}
#endif

#define HAVE_SYSCALL_PWRITE
static int syscall_pwrite(void)
{
	char buffer[512];
	ssize_t ret;
	off_t offset = (off_t)((stress_mwc8() & 0x7) * 512);

	stress_uint8rnd4((uint8_t *)buffer, sizeof(buffer));
	t1 = syscall_time_now();
	ret = pwrite(syscall_fd, buffer, sizeof(buffer), offset);
	t2 = syscall_time_now();
	return (int)ret;
}

#if defined(HAVE_SYS_UIO_H) &&	\
    defined(HAVE_PWRITEV)
#define HAVE_SYSCALL_PWRITEV
static int syscall_pwritev(void)
{
	char buffer[1024];
	ssize_t ret;
	off_t offset = (stress_mwc8() & 0x7) * 512;
	struct iovec iov[2];

	iov[0].iov_base = &buffer[512];
	iov[0].iov_len = 512;

	iov[1].iov_base = &buffer[0];
	iov[1].iov_len = 512;

	stress_uint8rnd4((uint8_t *)buffer, sizeof(buffer));

	t1 = syscall_time_now();
	ret = pwritev(syscall_fd, iov, SIZEOF_ARRAY(iov), offset);
	t2 = syscall_time_now();
	return (int)ret;
}
#endif

#if defined(HAVE_SYS_QUOTA_H) &&	\
    defined(Q_SYNC) &&			\
    defined(__linux__)
#define HAVE_SYSCALL_QUOTACTL
static int syscall_quotactl(void)
{
	int ret;
	char buf[4096];

	t1 = syscall_time_now();
	ret = quotactl(QCMD(Q_SYNC, USRQUOTA), "", 0, (caddr_t)buf);
	t2 = syscall_time_now();
	return ret;
}
#endif

#if defined(HAVE_SYS_QUOTA_H) &&	\
    defined(Q_SYNC) &&			\
    defined(O_DIRECTORY)
#define HAVE_SYSCALL_QUOTACTL_FD
static int syscall_quotactl_fd(void)
{
	int fd, ret;

	fd = open("/", O_DIRECTORY | O_RDONLY);
	if (fd < 0)
		return -1;

	t1 = syscall_time_now();
	ret = shim_quotactl_fd(fd, QCMD(Q_SYNC, USRQUOTA), 0, NULL);
	t2 = syscall_time_now();
	(void)close(fd);
	return ret;
}
#endif

#if defined(HAVE_SYS_UIO_H) &&	\
    defined(HAVE_PWRITEV2)
#define HAVE_SYSCALL_PWRITEV2
static int syscall_pwritev2(void)
{
	char buffer[1024];
	ssize_t ret;
	off_t offset = (stress_mwc8() & 0x7) * 512;
	struct iovec iov[2];

	iov[0].iov_base = &buffer[512];
	iov[0].iov_len = 512;

	iov[1].iov_base = &buffer[0];
	iov[1].iov_len = 512;

	stress_uint8rnd4((uint8_t *)buffer, sizeof(buffer));

	t1 = syscall_time_now();
#if defined(RWF_SYNC)
	ret = pwritev2(syscall_fd, iov, SIZEOF_ARRAY(iov), offset, RWF_SYNC);
#else
	ret = pwritev2(syscall_fd, iov, SIZEOF_ARRAY(iov), offset, 0);
#endif
	t2 = syscall_time_now();
	return (int)ret;
}
#endif

#define HAVE_SYSCALL_READ
static int syscall_read(void)
{
	char buffer[512];
	ssize_t ret;
	off_t offset = (off_t)((stress_mwc8() & 0x7) * 512);

	VOID_RET(off_t, lseek(syscall_fd, offset, SEEK_SET));
	t1 = syscall_time_now();
	ret = read(syscall_fd, buffer, sizeof(buffer));
	t2 = syscall_time_now();
	return (int)ret;
}

#if defined(HAVE_SYS_UIO_H)
#define HAVE_SYSCALL_READV
static int syscall_readv(void)
{
	char buffer[1024];
	ssize_t ret;
	off_t offset = (stress_mwc8() & 0x7) * 512;
	struct iovec iov[2];

	iov[0].iov_base = &buffer[512];
	iov[0].iov_len = 512;

	iov[1].iov_base = &buffer[0];
	iov[1].iov_len = 512;

	VOID_RET(off_t, lseek(syscall_fd, offset, SEEK_SET));
	t1 = syscall_time_now();
	ret = readv(syscall_fd, iov, SIZEOF_ARRAY(iov));
	t2 = syscall_time_now();
	return (int)ret;
}
#endif

#if defined(HAVE_SYS_UN_H) &&	\
    defined(AF_UNIX)
#define HAVE_SYSCALL_RECV
static int syscall_recv(void)
{
	return syscall_socket_measure(SOCK_MEASURE_RECV);
}
#endif

#if defined(HAVE_SYS_UN_H) &&	\
    defined(AF_UNIX)
#define HAVE_SYSCALL_RECVFROM
static int syscall_recvfrom(void)
{
	return syscall_socket_measure(SOCK_MEASURE_RECVFROM);
}
#endif

#if defined(HAVE_SYS_UN_H) &&	\
    defined(AF_UNIX) &&		\
    defined(HAVE_SENDMMSG)
#define HAVE_SYSCALL_RECVMMSG
static int syscall_recvmmsg(void)
{
	return syscall_socket_measure(SOCK_MEASURE_RECVMMSG);
}
#endif

#if defined(HAVE_SYS_UN_H) &&	\
    defined(AF_UNIX)
#define HAVE_SYSCALL_RECVMSG
static int syscall_recvmsg(void)
{
	return syscall_socket_measure(SOCK_MEASURE_RECVMSG);
}
#endif

#if defined(__linux__) &&	\
    NEED_GLIBC(2,3,0)
#define HAVE_SYSCALL_READAHEAD
static int syscall_readahead(void)
{
	off64_t offset = (off64_t)((stress_mwc8() & 0x7) * 512);
	ssize_t ret;

	t1 = syscall_time_now();
	ret = readahead(syscall_fd, offset, 4096);
	t2 = syscall_time_now();
	return ret;
}
#endif

#define HAVE_SYSCALL_READLINK
static int syscall_readlink(void)
{
	if (*syscall_symlink_filename) {
		char path[PATH_MAX];
		int ret;

		t1 = syscall_time_now();
		ret = readlink(syscall_symlink_filename, path, sizeof(path));
		t2 = syscall_time_now();
		return ret;
	}
	return -1;
}

#if defined(HAVE_READLINKAT)
#define HAVE_SYSCALL_READLINKAT
static int syscall_readlinkat(void)
{
	if (*syscall_symlink_filename) {
		char path[PATH_MAX];
		int ret;

		t1 = syscall_time_now();
		ret = readlinkat(syscall_dir_fd, syscall_symlink_filename, path, sizeof(path));
		t2 = syscall_time_now();
		return ret;
	}
	return -1;
}
#endif

#if defined(HAVE_RFORK) &&	\
    defined(RFFDG) &&		\
    defined(RFPROC)
#define HAVE_SYSCALL_RFORK
static int syscall_rfork(void)
{
	pid_t pid;

	t1 = syscall_time_now();
	pid = rfork(RFFDG | RFPROC);
	if (pid < 0) {
		return -1;
	} else if (pid == 0) {
		_exit(0);
	} else {
		int status;

		t2 = syscall_time_now();
		VOID_RET(pid_t, waitpid(pid, &status, 0));
	}
	return 0;
}
#endif

#if defined(HAVE_REMAP_FILE_PAGES) &&	\
    !defined(STRESS_ARCH_SPARC)
#define HAVE_SYSCALL_REMAP_FILE_PAGES
static int syscall_remap_file_pages(void)
{
	void *ptr;
	int ret;

	ptr = mmap(NULL, syscall_page_size, PROT_READ,
		   MAP_SHARED, syscall_fd, 0);
	if (ptr == MAP_FAILED)
		return -1;
	t1 = syscall_time_now();
	ret = remap_file_pages(ptr, syscall_page_size, 0, 1, 0);
	t2 = syscall_time_now();
	(void)munmap(ptr, syscall_page_size);
	return ret;
}
#endif


#if (defined(HAVE_SYS_XATTR_H) || defined(HAVE_ATTR_XATTR_H)) && \
     defined(HAVE_REMOVEXATTR) &&	\
     defined(HAVE_SETXATTR)
#define HAVE_SYSCALL_REMOVEXATTR
static int syscall_removexattr(void)
{
	int ret;

	VOID_RET(int, shim_setxattr(syscall_filename, syscall_xattr_name, "123", 3, 0));
	t1 = syscall_time_now();
	ret = shim_removexattr(syscall_filename, syscall_xattr_name);
	t2 = syscall_time_now();
	return ret;
}
#endif

#define HAVE_SYSCALL_RENAME
static int syscall_rename(void)
{
	int ret;

	VOID_RET(int, shim_unlink(syscall_tmp_filename));
	t1 = syscall_time_now();
	ret = rename(syscall_filename, syscall_tmp_filename);
	t2 = syscall_time_now();
	VOID_RET(int, rename(syscall_tmp_filename, syscall_filename));
	return ret;
}

#if defined(HAVE_RENAMEAT)
#define HAVE_SYSCALL_RENAMEAT
static int syscall_renameat(void)
{
	int ret;

	VOID_RET(int, shim_unlink(syscall_tmp_filename));
	t1 = syscall_time_now();
	ret = renameat(syscall_dir_fd, syscall_filename,
		       syscall_dir_fd, syscall_tmp_filename);
	t2 = syscall_time_now();
	VOID_RET(int, renameat(syscall_dir_fd, syscall_tmp_filename,
			       syscall_dir_fd, syscall_filename));
	return ret;
}
#endif

#if defined(HAVE_RENAMEAT2)
#define HAVE_SYSCALL_RENAMEAT2
static int syscall_renameat2(void)
{
	int ret;

	VOID_RET(int, shim_unlink(syscall_tmp_filename));
	t1 = syscall_time_now();
	ret = renameat2(syscall_dir_fd, syscall_filename,
		       syscall_dir_fd, syscall_tmp_filename, 0);
	t2 = syscall_time_now();
	VOID_RET(int, renameat2(syscall_dir_fd, syscall_tmp_filename,
			       syscall_dir_fd, syscall_filename, 0));
	return ret;
}
#endif

#if defined(HAVE_KEYUTILS_H) &&		\
    defined(HAVE_SYSCALL) &&		\
    defined(__NR_request_key) &&	\
    defined(__NR_keyctl) &&		\
    defined(__NR_add_key) &&		\
    defined(KEYCTL_INVALIDATE)
#define HAVE_SYSCALL_REQUEST_KEY
static int syscall_request_key(void)
{
	key_serial_t key;
	char ALIGN64 description[64];
	static char payload[] = "example payload";

	(void)snprintf(description, sizeof(description),
		"stress-ng-syscall-key-%" PRIdMAX, (intmax_t)syscall_pid);

	key = (key_serial_t)syscall(__NR_add_key, "user",
                description, payload, sizeof(payload), KEY_SPEC_PROCESS_KEYRING);
	if (key < 0)
		return -1;
	t1 = syscall_time_now();
	key = (key_serial_t)syscall(__NR_request_key, "user", description, NULL, KEY_SPEC_PROCESS_KEYRING);
	t2 = syscall_time_now();
	if (key < 0)
		return -1;
	(void)syscall(__NR_keyctl, KEYCTL_INVALIDATE, key);
	return (int)key;
}
#endif

#if defined(HAVE_SYSCALL) &&		\
    defined(__NR_restart_syscall)
#define HAVE_SYSCALL_RESTART_SYSCALL
static int syscall_restart_syscall(void)
{
	int ret;

	t1 = syscall_time_now();
	ret = syscall(__NR_restart_syscall);
	t2 = syscall_time_now();
	return ret;
}
#endif

#if defined(HAVE_SYSCALL) &&		\
    defined(__NR_riscv_flush_icache)
#define HAVE_SYSCALL_RISCV_FLUSH_ICACHE
static int syscall_riscv_flush_icache(void)
{
	int ret;
	char *start, *end;

	(void)stress_exec_text_addr(&start, &end);
	t1 = syscall_time_now();
        ret = (int)syscall(__NR_riscv_flush_icache, (uintptr_t)start, (uintptr_t)end, 1UL);
	t2 = syscall_time_now();
	return ret;
}
#endif

#if defined(HAVE_SYSCALL) &&		\
    defined(__NR_riscv_hwprobe)
#define HAVE_SYSCALL_RISCV_HWPROBE
static int syscall_riscv_hwprobe(void)
{
	/* should be from asm/hwprobe.h */
	struct shim_riscv_hwprobe {
		int64_t key;
		uint64_t value;
	};

	int ret;
	long int i;
	struct shim_riscv_hwprobe pairs[8];
	unsigned long int cpus;

	for (i = 0; i < 8; i++)
                pairs[i].key = i;

	t1 = syscall_time_now();
        ret = (int)syscall(__NR_riscv_hwprobe, pairs, 8, 1, &cpus, 0);
	t2 = syscall_time_now();
	return ret;
}
#endif

#define HAVE_SYSCALL_RMDIR
static int syscall_rmdir(void)
{
	int ret;

	if (mkdir(syscall_tmp_filename, S_IRUSR | S_IWUSR) < 0)
		return -1;
	t1 = syscall_time_now();
	ret = shim_rmdir(syscall_tmp_filename);
	t2 = syscall_time_now();
	return ret;
}

#if defined(HAVE_LINUX_RSEQ_H) &&	\
    defined(__NR_rseq) &&		\
    defined(HAVE_SYSCALL)
#define HAVE_SYSCALL_RSEQ
static int syscall_rseq(void)
{
	static struct rseq rseq;
	uint32_t signature = stress_mwc32();
	int ret;

	t1 = syscall_time_now();
	ret = (int)syscall(__NR_rseq, &rseq, sizeof(rseq), 0, signature);
	t2 = syscall_time_now();
	if (ret < 0)
		return -1;
	VOID_RET(int, (int)syscall(__NR_rseq, &rseq, sizeof(rseq), RSEQ_FLAG_UNREGISTER, signature));
	return 0;
}
#endif

#if defined(HAVE_SCHED_GETAFFINITY)
#define HAVE_SYSCALL_SCHED_GETAFFINITY
static int syscall_sched_getaffinity(void)
{
	int ret;
	cpu_set_t mask;

	CPU_ZERO(&mask);
	t1 = syscall_time_now();
	ret = sched_getaffinity(0, sizeof(mask), &mask);
	t2 = syscall_time_now();
	return ret;
}
#endif

#if defined(__NR_sched_getattr) &&	\
    defined(HAVE_SYSCALL)
#define HAVE_SYSCALL_SCHED_GETATTR
static int syscall_sched_getattr(void)
{
	int ret;
	struct shim_sched_attr attr;

	(void)shim_memset(&attr, 0, sizeof(attr));
	attr.size = sizeof(attr);
	t1 = syscall_time_now();
	ret = shim_sched_getattr(syscall_pid, &attr, sizeof(attr), 0);
	t2 = syscall_time_now();
	return ret;
}
#endif

#if (defined(_POSIX_PRIORITY_SCHEDULING) || defined(__linux__)) &&	\
     !defined(__OpenBSD__) &&						\
     !defined(__minix__) &&						\
     !defined(__APPLE__) &&						\
     !defined(__HAIKU__) &&						\
     !defined(__serenity__)
#define HAVE_SYSCALL_SCHED_GETPARAM
static int syscall_sched_getparam(void)
{
	int ret;
	struct sched_param param;

	(void)shim_memset(&param, 0, sizeof(param));
	t1 = syscall_time_now();
	ret = sched_getparam(syscall_pid, &param);
	t2 = syscall_time_now();
	return ret;
}
#endif

#if (defined(_POSIX_PRIORITY_SCHEDULING) || defined(__linux__)) &&	\
     !defined(__OpenBSD__) &&						\
     !defined(__minix__) &&						\
     !defined(__APPLE__) &&						\
     !defined(__HAIKU__) &&						\
     !defined(__serenity__)
#define HAVE_SYSCALL_SCHED_GET_PRIORITY_MAX
static int syscall_sched_get_priority_max(void)
{
	static size_t i = 0;
	int ret;
	const int sched_policy = sched_policies[i];

	i++;
	if (i >= SIZEOF_ARRAY(sched_policies))
		i = 0;
	t1 = syscall_time_now();
	ret = sched_get_priority_max(sched_policy);
	t2 = syscall_time_now();
	return ret;
}
#endif

#if (defined(_POSIX_PRIORITY_SCHEDULING) || defined(__linux__)) &&	\
     !defined(__OpenBSD__) &&						\
     !defined(__minix__) &&						\
     !defined(__APPLE__) &&						\
     !defined(__HAIKU__) &&						\
     !defined(__serenity__)
#define HAVE_SYSCALL_SCHED_GET_PRIORITY_MIN
static int syscall_sched_get_priority_min(void)
{
	static size_t i = 0;
	int ret;
	const int sched_policy = sched_policies[i];

	i++;
	if (i >= SIZEOF_ARRAY(sched_policies))
		i = 0;
	t1 = syscall_time_now();
	ret = sched_get_priority_min(sched_policy);
	t2 = syscall_time_now();
	return ret;
}
#endif

#if (defined(_POSIX_PRIORITY_SCHEDULING) || defined(__linux__)) &&	\
     !defined(__OpenBSD__) &&						\
     !defined(__minix__) &&						\
     !defined(__APPLE__) &&						\
     !defined(__HAIKU__) &&						\
     !defined(__serenity__)
#define HAVE_SYSCALL_SCHED_GETSCHEDULER
static int syscall_sched_getscheduler(void)
{
	int ret;

	t1 = syscall_time_now();
	ret = sched_getscheduler(syscall_pid);
	t2 = syscall_time_now();
	return ret;
}
#endif

#if (defined(_POSIX_PRIORITY_SCHEDULING) || defined(__linux__)) &&	\
     defined(HAVE_SCHED_RR_GET_INTERVAL) &&				\
     !defined(__OpenBSD__) &&						\
     !defined(__minix__) &&						\
     !defined(__APPLE__) &&						\
     !defined(__HAIKU__) &&						\
     !defined(__serenity__)
#define HAVE_SYSCALL_SCHED_RR_GET_INTERVAL
static int syscall_sched_rr_get_interval(void)
{
	struct timespec t;
	int ret;

	t1 = syscall_time_now();
	ret = sched_rr_get_interval(syscall_pid, &t);
	t2 = syscall_time_now();
	return ret;
}
#endif

#if defined(HAVE_SCHED_GETAFFINITY) &&	\
    defined(HAVE_SCHED_SETAFFINITY)
#define HAVE_SYSCALL_SCHED_SETAFFINITY
static int syscall_sched_setaffinity(void)
{
	int ret;
	cpu_set_t mask;

	CPU_ZERO(&mask);
	ret = sched_getaffinity(0, sizeof(mask), &mask);
	if (ret < 0)
		return -1;
	t1 = syscall_time_now();
	ret = sched_setaffinity(0, sizeof(mask), &mask);
	t2 = syscall_time_now();
	return ret;
}
#endif

#if defined(__NR_sched_setattr) &&	\
    defined(HAVE_SYSCALL)
#define HAVE_SYSCALL_SCHED_SETATTR
static int syscall_sched_setattr(void)
{
	int ret;
	struct shim_sched_attr attr;

	(void)shim_memset(&attr, 0, sizeof(attr));
	attr.size = sizeof(attr);
	ret = shim_sched_getattr(syscall_pid, &attr, sizeof(attr), 0);
	if (ret < 0)
		return -1;
	t1 = syscall_time_now();
	ret = shim_sched_setattr(syscall_pid, &attr, 0);
	t2 = syscall_time_now();
	return ret;
}
#endif

#if (defined(_POSIX_PRIORITY_SCHEDULING) || defined(__linux__)) &&	\
     !defined(__OpenBSD__) &&						\
     !defined(__minix__) &&						\
     !defined(__APPLE__) &&						\
     !defined(__HAIKU__) &&						\
     !defined(__serenity__)
#define HAVE_SYSCALL_SCHED_SETPARAM
static int syscall_sched_setparam(void)
{
	int ret;
	struct sched_param param;

	(void)shim_memset(&param, 0, sizeof(param));
	ret = sched_getparam(syscall_pid, &param);
	if (ret < 0)
		return -1;
	t1 = syscall_time_now();
	ret = sched_setparam(syscall_pid, &param);
	t2 = syscall_time_now();
	return ret;
}
#endif

#if (defined(_POSIX_PRIORITY_SCHEDULING) || defined(__linux__)) &&	\
     !defined(__OpenBSD__) &&						\
     !defined(__minix__) &&						\
     !defined(__APPLE__) &&						\
     !defined(__HAIKU__) &&						\
     !defined(__serenity__)
#define HAVE_SYSCALL_SCHED_SETSCHEDULER
static int syscall_sched_setscheduler(void)
{
	int policy, ret;
	struct sched_param param;

	(void)shim_memset(&param, 0, sizeof(param));
	policy = sched_getscheduler(syscall_pid);
	if (policy < 0)
		return -1;
	ret = sched_getparam(syscall_pid, &param);
	if (ret < 0)
		return -1;
	t1 = syscall_time_now();
	ret = sched_setscheduler(syscall_pid, policy, &param);
	t2 = syscall_time_now();
	return ret;
}
#endif


#define HAVE_SYSCALL_SCHED_YIELD
static int syscall_sched_yield(void)
{
	int ret;

	t1 = syscall_time_now();
	ret = shim_sched_yield();
	t2 = syscall_time_now();
	return ret;
}

#if defined(HAVE_LINUX_SECCOMP_H) &&	\
    defined(HAVE_LINUX_AUDIT_H) &&	\
    defined(HAVE_LINUX_FILTER_H) &&	\
    defined(SECCOMP_SET_MODE_FILTER) &&	\
    defined(SECCOMP_RET_ALLOW) &&	\
    defined(BPF_RET) &&			\
    defined(BPF_K)
#define HAVE_SYSCALL_SECCOMP
static int syscall_seccomp(void)
{
	static struct sock_filter filter_allow_all[] = {
		BPF_STMT(BPF_RET + BPF_K, SECCOMP_RET_ALLOW)
	};

	static struct sock_fprog prog_allow_all = {
		.len = (unsigned short int)SIZEOF_ARRAY(filter_allow_all),
		.filter = filter_allow_all
	};
	int ret;

	t1 = syscall_time_now();
	ret = shim_seccomp(SECCOMP_SET_MODE_FILTER, 0, &prog_allow_all);
	t2 = syscall_time_now();

	return ret;
}
#endif


#if defined(HAVE_SELECT) &&	\
    defined(HAVE_SYS_SELECT_H)
#define HAVE_SYSCALL_SELECT
static int syscall_select(void)
{
	fd_set rd_set, wr_set;
	int fds[4], nfds = -1, ret;
	size_t i;
	struct timeval tv;

	fds[0] = fileno(stdin);
	fds[1] = fileno(stdout);
	fds[2] = fileno(stderr);
	fds[3] = syscall_fd;

	for (i = 0; i < SIZEOF_ARRAY(fds); i++)
		if (nfds < fds[i])
			nfds = fds[i];

	FD_ZERO(&rd_set);
	FD_SET(fds[0], &rd_set);
	FD_SET(fds[3], &rd_set);

	FD_ZERO(&wr_set);
	FD_SET(fds[1], &wr_set);
	FD_SET(fds[2], &wr_set);

	tv.tv_sec = 0;
	tv.tv_usec = 0;

	t1 = syscall_time_now();
	ret = select(nfds + 1, &rd_set, &wr_set, NULL, &tv);
	t2 = syscall_time_now();
	return ret;
}
#endif

#if defined(HAVE_SEM_SYSV) &&	\
    defined(HAVE_SYS_IPC_H)
static int syscall_new_sem_sysv(key_t *key)
{
	int i;
	static key_t saved_key = 0;

	if (saved_key == 0)
		*key = (key_t)stress_mwc16();
	else
		*key = saved_key;
	for (i = 0; i < 65536; i++) {
		int ret;

		ret = semget(*key, 1, IPC_CREAT | IPC_EXCL | S_IRUSR | S_IWUSR);
		if (ret != -1) {
			saved_key = *key;
			return ret;
		}
		if ((errno == ENOENT) ||
		    (errno == ENOMEM) ||
		    (errno == ENOSPC))
			return -1;
		(*key)++;
	}
	return -1;
}
#endif

#if defined(HAVE_SEM_SYSV) &&	\
    defined(HAVE_SYS_IPC_H)
#define HAVE_SYSCALL_SEMCTL
static int syscall_semctl(void)
{
	int sem_id, ret;
	key_t key;

	sem_id = syscall_new_sem_sysv(&key);
	if (sem_id == -1)
		return -1;

	t1 = syscall_time_now();
	ret = semctl(sem_id, 0, IPC_RMID);
	t2 = syscall_time_now();
	return ret;
}
#endif

#if defined(HAVE_SEM_SYSV) &&	\
    defined(HAVE_SYS_IPC_H)
#define HAVE_SYSCALL_SEMGET
static int syscall_semget(void)
{
	int sem_id, ret;
	key_t key;

	sem_id = syscall_new_sem_sysv(&key);
	if (sem_id == -1)
		return -1;

	if (semctl(sem_id, 0, IPC_RMID) < 0)
		return -1;

	t1 = syscall_time_now();
	ret = semget(key, 1, IPC_CREAT | IPC_EXCL | S_IRUSR | S_IWUSR);
	t2 = syscall_time_now();
	VOID_RET(int, semctl(ret, 0, IPC_RMID));
	return ret;
}
#endif

#if defined(HAVE_SEM_SYSV) &&	\
    defined(HAVE_SYS_IPC_H)
#define HAVE_SYSCALL_SEMOP
static int syscall_semop(void)
{
	int sem_id, ret;
	key_t key;
	struct sembuf sop;

	sem_id = syscall_new_sem_sysv(&key);
	if (sem_id == -1)
		return -1;

	sop.sem_num = 0;
	sop.sem_op = 0;
	sop.sem_flg = 0;

	t1 = syscall_time_now();
	ret = semop(sem_id, &sop, 1);
	t2 = syscall_time_now();
	VOID_RET(int, semctl(sem_id, 0, IPC_RMID));
	return ret;
}
#endif

#if defined(HAVE_SEM_SYSV) &&	\
    defined(HAVE_SYS_IPC_H) &&	\
    defined(HAVE_SEMTIMEDOP)
#define HAVE_SYSCALL_SEMTIMEDOP
static int syscall_semtimedop(void)
{
	int sem_id, ret;
	key_t key;
	struct sembuf sop;
	struct timespec ts;

	sem_id = syscall_new_sem_sysv(&key);
	if (sem_id == -1)
		return -1;

	sop.sem_num = 0;
	sop.sem_op = 0;
	sop.sem_flg = 0;

	ts.tv_sec = 0;
	ts.tv_nsec = 1;

	t1 = syscall_time_now();
	ret = semtimedop(sem_id, &sop, 1, &ts);
	t2 = syscall_time_now();
	VOID_RET(int, semctl(sem_id, 0, IPC_RMID));
	return ret;
}
#endif

#if defined(HAVE_SYS_UN_H) &&	\
    defined(AF_UNIX)
#define HAVE_SYSCALL_SEND
static int syscall_send(void)
{
	return syscall_socket_measure(SOCK_MEASURE_SEND);
}
#endif

#if defined(HAVE_SYS_UN_H) &&	\
    defined(AF_UNIX) &&		\
    defined(HAVE_SENDMMSG)
#define HAVE_SYSCALL_SENDMMSG
static int syscall_sendmmsg(void)
{
	return syscall_socket_measure(SOCK_MEASURE_SENDMMSG);
}
#endif

#if defined(HAVE_SYS_UN_H) &&	\
    defined(HAVE_SENDMSG) &&	\
    defined(AF_UNIX)
#define HAVE_SYSCALL_SENDMSG
static int syscall_sendmsg(void)
{
	return syscall_socket_measure(SOCK_MEASURE_SENDMSG);
}
#endif

#if defined(HAVE_SYS_UN_H) &&	\
    defined(AF_UNIX)
#define HAVE_SYSCALL_SENDTO
static int syscall_sendto(void)
{
	return syscall_socket_measure(SOCK_MEASURE_SENDTO);
}
#endif

#if defined(HAVE_SYS_SENDFILE_H) &&	\
    defined(HAVE_SENDFILE) &&		\
    NEED_GLIBC(2,1,0)
#define HAVE_SYSCALL_SENDFILE
static int syscall_sendfile(void)
{
	int ret, fd;
	off_t offset = 0;

	fd = creat(syscall_tmp_filename, S_IRUSR | S_IWUSR);
	if (fd < 0)
		return -1;

	t1 = syscall_time_now();
	ret = sendfile(fd, syscall_fd, &offset, syscall_page_size * 32);
	t2 = syscall_time_now();

	VOID_RET(int, close(fd));
	VOID_RET(int, shim_unlink(syscall_tmp_filename));
	return ret;
}
#endif

#define HAVE_SYSCALL_SETGID
static int syscall_setgid(void)
{
	int ret;

	t1 = syscall_time_now();
	ret = setgid(syscall_gid);
	t2 = syscall_time_now();
	return ret;
}

#if defined(HAVE_SETITIMER) &&	\
    defined(HAVE_GETITIMER)
#define HAVE_SYSCALL_SETITIMER
static int syscall_setitimer(void)
{
	static size_t i = 0;

	struct itimerval val, oldval;
	int ret;
	const shim_itimer_which_t itimer = itimers[i];

	i++;
	if (i >= SIZEOF_ARRAY(itimers))
		i = 0;

	ret = getitimer(itimer, &val);
	if (ret < 0)
		return -1;
	t1 = syscall_time_now();
	ret = setitimer(itimer, &val, &oldval);
	t2 = syscall_time_now();
	return ret;
}
#endif

#if defined(HAVE_LINUX_MEMPOLICY_H) &&	\
    defined(HAVE_SYSCALL) &&		\
    defined(__NR_get_mempolicy) &&	\
    defined(__NR_get_mempolicy) &&	\
    defined(MPOL_F_ADDR)
#define HAVE_SYSCALL_SET_MEMPOLICY
static int syscall_set_mempolicy(void)
{
	unsigned long int node_mask[NUMA_LONG_BITS];
	unsigned long int max_nodes = 1;
	int ret, mode;
	void *buf;

	buf = mmap(NULL, syscall_page_size, PROT_READ | PROT_WRITE,
		MAP_ANONYMOUS | MAP_SHARED, -1, 0);
	if (buf == MAP_FAILED)
		return -1;

	ret = shim_get_mempolicy(&mode, node_mask, max_nodes, buf, MPOL_F_ADDR);
	if (ret < 0)
		goto unmap;
	t1 = syscall_time_now();
	ret = shim_set_mempolicy(mode, node_mask, max_nodes);
	t2 = syscall_time_now();

unmap:
	(void)munmap(buf, syscall_page_size);

	return ret;
}
#endif

#if defined(HAVE_GETPGID) &&	\
    defined(HAVE_SETPGID)
#define HAVE_SYSCALL_SETPGID
static int syscall_setpgid(void)
{
	int ret;
	pid_t pgid;

	pgid = getpgid(syscall_pid);
	if (pgid < 0)
		return -1;

	t1 = syscall_time_now();
	ret = setpgid(syscall_pid, pgid);
	t2 = syscall_time_now();
	return ret;
}
#endif

#if defined(HAVE_GETPRIORITY) &&	\
    defined(HAVE_SETPRIORITY)
#define HAVE_SYSCALL_SETPRIORITY
static int syscall_setpriority(void)
{
	int prio, ret;

	prio = getpriority(PRIO_PROCESS, syscall_pid);
	if (prio < 0)
		return -1;
	t1 = syscall_time_now();
	ret = setpriority(PRIO_PROCESS, syscall_pid, prio);
	t2 = syscall_time_now();
	return ret;
}
#endif


#if defined(HAVE_SETREGID)
#define HAVE_SYSCALL_SETREGID
static int syscall_setregid(void)
{
	int ret;
	gid_t rgid, egid;

	rgid = getgid();
	egid = getegid();

	t1 = syscall_time_now();
	ret = setregid(rgid, egid);
	t2 = syscall_time_now();
	return ret;
}
#endif

#if defined(HAVE_SETRESGID) &&	\
    defined(HAVE_GETRESGID)
#define HAVE_SYSCALL_SETRESGID
static int syscall_setresgid(void)
{
	int ret;
	gid_t rgid, egid, sgid;

	if (getresgid(&rgid, &egid, &sgid) < 0)
		return -1;

	t1 = syscall_time_now();
	ret = setresgid(rgid, egid, sgid);
	t2 = syscall_time_now();
	return ret;
}
#endif

#if defined(HAVE_SETRESUID) &&	\
    defined(HAVE_GETRESUID)
#define HAVE_SYSCALL_SETRESUID
static int syscall_setresuid(void)
{
	uid_t ruid, euid, suid;
	int ret;

	if (getresuid(&ruid, &euid, &suid) < 0)
		return -1;

	t1 = syscall_time_now();
	ret = setresuid(ruid, euid, suid);
	t2 = syscall_time_now();
	return ret;
}
#endif

#if defined(HAVE_GETRESUID)
#define HAVE_SYSCALL_SETREUID
static int syscall_setreuid(void)
{
	uid_t ruid, euid, suid;
	int ret;

	if (getresuid(&ruid, &euid, &suid) < 0)
		return -1;

	t1 = syscall_time_now();
	ret = setreuid(ruid, euid);
	t2 = syscall_time_now();
	return ret;
}
#endif

#define HAVE_SYSCALL_SETRLIMIT
static int syscall_setrlimit(void)
{
	static size_t i = 0;

	struct rlimit old_rlim, new_rlim;
	int ret;
	const shim_rlimit_resource_t limit = limits[i];

	i++;
	if (i >= SIZEOF_ARRAY(limits))
		i = 0;
	ret = getrlimit(limit, &old_rlim);
	if (ret < 0)
		return -1;

	(void)shim_memcpy(&new_rlim, &old_rlim, sizeof(new_rlim));
	new_rlim.rlim_cur = new_rlim.rlim_max;
	t1 = syscall_time_now();
	ret = setrlimit(limit, &new_rlim);
	t2 = syscall_time_now();
	VOID_RET(int, setrlimit(limit, &old_rlim));
	return ret;
}

#if defined(__NR_set_robust_list) &&	\
    defined(__NR_get_robust_list) &&	\
    defined(HAVE_LINUX_FUTEX_H) &&	\
    defined(HAVE_SYSCALL)
#define HAVE_SYSCALL_SET_ROBUST_LIST
static int syscall_set_robust_list(void)
{
	struct robust_list_head *head;
	size_t len;
	int ret;

	ret = (int)syscall(__NR_get_robust_list, syscall_pid, &head, &len);
	if (ret < 0)
		return -1;
	t1 = syscall_time_now();
	ret = (int)syscall(__NR_set_robust_list, head, len);
	t2 = syscall_time_now();

	return ret;
}
#endif

#if defined(HAVE_SETSID) &&	\
    defined(HAVE_GETSID)
#define HAVE_SYSCALL_SETSID
static int syscall_setsid(void)
{
	int ret;
	pid_t sid;

	sid = getsid(syscall_pid);
	if (sid < 0)
		return -1;
	t1 = syscall_time_now();
	ret = setsid(sid);
	t2 = syscall_time_now();
	return ret;
}
#endif

#if defined(HAVE_SYS_UN_H) &&	\
    defined(AF_UNIX) &&		\
    defined(SOL_SOCKET)
#define HAVE_SYSCALL_SETSOCKOPT
static int syscall_setsockopt(void)
{
	int sfd, rcvbuf = 2048, ret;
	socklen_t len = sizeof(rcvbuf);

	sfd = socket(AF_UNIX, SOCK_STREAM, 0);
	if (sfd < 0)
		return -1;
	t1 = syscall_time_now();
	ret = setsockopt(sfd, SOL_SOCKET, SO_RCVBUF, &rcvbuf, len);
	t2 = syscall_time_now();
	(void)close(sfd);
	return ret;
}
#endif

#define HAVE_SYSCALL_SETUID
static int syscall_setuid(void)
{
	int ret;

	t1 = syscall_time_now();
	ret = setuid(syscall_uid);
	t2 = syscall_time_now();
	return ret;
}

#if (defined(HAVE_SYS_XATTR_H) || defined(HAVE_ATTR_XATTR_H)) && \
     defined(HAVE_SETXATTR) && \
     defined(HAVE_REMOVEXATTR)
#define HAVE_SYSCALL_SETXATTR
static int syscall_setxattr(void)
{
	int ret;
	t1 = syscall_time_now();
	ret = shim_setxattr(syscall_filename, syscall_xattr_name, "123", 3, 0);
	t2 = syscall_time_now();
	VOID_RET(int, shim_removexattr(syscall_filename, syscall_xattr_name));
	return ret;
}
#endif

#if defined(HAVE_SYS_IPC_H) &&	\
    defined(HAVE_SYS_SHM_H)
static int syscall_new_shm_sysv(key_t *key)
{
	int i;
	static key_t saved_key = 0;

	if (saved_key == 0)
		*key = (key_t)stress_mwc16();
	else
		*key = saved_key;
	for (i = 0; i < 65536; i++) {
		int ret;

		ret = shmget(*key, 1 * MB, IPC_CREAT | S_IRUSR | S_IWUSR);
		if (ret != -1) {
			saved_key = *key;
			return ret;
		}
		if ((errno == ENFILE) ||
		    (errno == ENOMEM) ||
		    (errno == ENOENT) ||
		    (errno == EACCES) ||
		    (errno == EPERM)  ||
		    (errno == ENOSPC))
			return -1;
		(*key)++;
	}
	return -1;
}
#endif

#if defined(HAVE_SYS_IPC_H) &&	\
    defined(HAVE_SYS_SHM_H)
#define HAVE_SYSCALL_SHMAT
static int syscall_shmat(void)
{
	key_t key;
	void *addr;
	int shm_id;

	shm_id = syscall_new_shm_sysv(&key);
	if (shm_id < 0)
		return -1;

	t1 = syscall_time_now();
	addr = shmat(shm_id, NULL, SHM_RDONLY);
	t2 = syscall_time_now();

	if (addr != (void *)-1)
		VOID_RET(int, shmdt(addr));
	VOID_RET(int, shmctl(shm_id, IPC_RMID, NULL));

	if (addr == (void *)-1)
		return -1;
	return 0;
}
#endif

#if defined(HAVE_SYS_IPC_H) &&	\
    defined(HAVE_SYS_SHM_H)
#define HAVE_SYSCALL_SHMCTL
static int syscall_shmctl(void)
{
	key_t key;
	int shm_id, ret;

	shm_id = syscall_new_shm_sysv(&key);
	if (shm_id < 0)
		return -1;

	t1 = syscall_time_now();
	ret = shmctl(shm_id, IPC_RMID, NULL);
	t2 = syscall_time_now();

	return ret;
}
#endif

#if defined(HAVE_SYS_IPC_H) &&	\
    defined(HAVE_SYS_SHM_H)
#define HAVE_SYSCALL_SHMDT
static int syscall_shmdt(void)
{
	key_t key;
	void *addr;
	int shm_id, ret = -1;

	shm_id = syscall_new_shm_sysv(&key);
	if (shm_id < 0)
		return -1;

	addr = shmat(shm_id, NULL, SHM_RDONLY);
	if (addr != (void *)-1) {
		t1 = syscall_time_now();
		ret = shmdt(addr);
		t2 = syscall_time_now();
	}
	VOID_RET(int, shmctl(shm_id, IPC_RMID, NULL));

	return ret;
}
#endif

#if defined(HAVE_SYS_IPC_H) &&	\
    defined(HAVE_SYS_SHM_H)
#define HAVE_SYSCALL_SHMGET
static int syscall_shmget(void)
{
	key_t key;
	int shm_id;

	shm_id = syscall_new_shm_sysv(&key);
	if (shm_id < 0)
		return -1;

	VOID_RET(int, shmctl(shm_id, IPC_RMID, NULL));

	t1 = syscall_time_now();
	shm_id = shmget(key, syscall_page_size, IPC_CREAT | IPC_EXCL | S_IRUSR | S_IWUSR);
	t2 = syscall_time_now();
	if (shm_id >= 0)
		VOID_RET(int, shmctl(shm_id, IPC_RMID, NULL));

	return shm_id;
}
#endif

#if defined(HAVE_SYS_UN_H) &&	\
    defined(AF_UNIX)
#define HAVE_SYSCALL_SHUTDOWN
static int syscall_shutdown(void)
{
	return syscall_socket_measure(SOCK_MEASURE_SHUTDOWN);
}
#endif

#define HAVE_SYSCALL_SIGACTION
static int syscall_sigaction(void)
{
	struct sigaction act, old_act;
	int ret;

	(void)shim_memset(&act, 0, sizeof(act));

	act.sa_handler = stress_mwc1() ? SIG_DFL : SIG_IGN;
	act.sa_sigaction = 0;
	(void)sigemptyset(&act.sa_mask);
	act.sa_flags = 0;

	t1 = syscall_time_now();
	ret = sigaction(SIGUSR2, &act, &old_act);
	t2 = syscall_time_now();
	return ret;
}

#if defined(HAVE_SIGALTSTACK)
#define HAVE_SYSCALL_SIGALTSTACK
static int syscall_sigaltstack(void)
{
	stack_t new_ss, old_ss;
	uint64_t stack[1024];
	int ret;

	(void)shim_memset(stack, 0, sizeof(stack));

	new_ss.ss_sp = (void *)stack;
	new_ss.ss_size = sizeof(stack);
	new_ss.ss_flags = 0;

	t1 = syscall_time_now();
	ret = sigaltstack(&new_ss, &old_ss);
	t2 = syscall_time_now();

	if (ret == 0)
		VOID_RET(int, sigaltstack(&old_ss, NULL));
	return ret;
}
#endif

#if defined(SIGCHLD)
#define HAVE_SYSCALL_SIGNAL
static int syscall_signal(void)
{
	typedef void (*shim_sighandler_t)(int);

	shim_sighandler_t prev_handler;

	t1 = syscall_time_now();
	prev_handler = signal(SIGCHLD, syscall_sigignore_handler);
	t2 = syscall_time_now();
	if (prev_handler == SIG_ERR)
		return -1;
	VOID_RET(shim_sighandler_t, signal(SIGCHLD, prev_handler));
	return 0;
}
#endif

#if defined(HAVE_SYS_SIGNALFD_H) &&	\
    defined(HAVE_SIGNALFD) &&		\
    defined(SIGCHLD) &&			\
    NEED_GLIBC(2,8,0)
#define HAVE_SYSCALL_SIGNALFD
static int syscall_signalfd(void)
{
	sigset_t mask;
	int fd;

	(void)sigemptyset(&mask);
	(void)sigaddset(&mask, SIGCHLD);
	t1 = syscall_time_now();
	fd = signalfd(-1, &mask, 0);
	t2 = syscall_time_now();
	if (fd >= 0)
		(void)close(fd);
	return fd;
}
#endif

#define HAVE_SYSCALL_SIGPENDING
static int syscall_sigpending(void)
{
	sigset_t set;
	int ret;

	VOID_RET(int, sigemptyset(&set));
	t1 = syscall_time_now();
	ret = sigpending(&set);
	t2 = syscall_time_now();
	return ret;
}

#define HAVE_SYSCALL_SIGPROCMASK
static int syscall_sigprocmask(void)
{
	sigset_t new_set, old_set;
	int ret;

	VOID_RET(int, sigemptyset(&new_set));
	VOID_RET(int, sigaddset(&new_set, SIGUSR2));

	t1 = syscall_time_now();
	ret = sigprocmask(SIG_BLOCK, &new_set, &old_set);
	t2 = syscall_time_now();
	VOID_RET(int, sigprocmask(SIG_SETMASK, &old_set, NULL));
	return ret;
}

#define HAVE_SYSCALL_SIGRETURN
static int syscall_sigreturn(void)
{
	syscall_shared_info->sig_t = ~0ULL;
	syscall_shared_info->t_set = false;

	t1 = syscall_time_now();
	while (!syscall_shared_info->t_set) {
		(void)kill(syscall_pid, SIGUSR1);
		(void)shim_sched_yield();
		if (syscall_time_now() - t1 > 1000000) {
			return -1;
		}
	}
	t2 = syscall_shared_info->sig_t;
	return 0;
}

#define HAVE_SYSCALL_SIGSUSPEND
static int syscall_sigsuspend(void)
{
	pid_t pid;
	sigset_t new_mask, old_mask;
	int ret;

	syscall_shared_error(-1);
	(void)sigemptyset(&new_mask);
	ret = sigprocmask(SIG_BLOCK, &new_mask, &old_mask);
	if (ret < 0)
		return -1;

	pid = fork();
	if (pid < 0) {
		goto restore_mask;
	} else if (pid == 0) {
		syscall_shared_info->t1 = syscall_time_now();
		syscall_shared_info->syscall_ret = sigsuspend(&new_mask);
		syscall_shared_info->t2 = syscall_time_now();
		_exit(0);
	} else {
		int status;

		do {
			pid_t wret;

			VOID_RET(int, kill(pid, SIGUSR1));

			wret = waitpid(pid, &status, WNOHANG);
			if (wret == pid)
				break;
			(void)shim_sched_yield();
		} while (stress_continue_flag());

		VOID_RET(int, kill(pid, SIGKILL));
		VOID_RET(pid_t, waitpid(pid, &status, WNOHANG));
	}
	t1 = syscall_shared_info->t1;
	t2 = syscall_shared_info->t2;
	ret = syscall_shared_info->syscall_ret;

restore_mask:
	sigprocmask(SIG_BLOCK, &old_mask, NULL);

	return ret;
}

#if defined(HAVE_SYS_UN_H) &&	\
    defined(AF_UNIX) &&		\
    defined(SOCK_NONBLOCK)
#define HAVE_SYSCALL_SOCKET
static int syscall_socket(void)
{
	int sfd;

	t1 = syscall_time_now();
	sfd = socket(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK, 0);
	t2 = syscall_time_now();
	if (sfd >= 0)
		(void)close(sfd);

	return sfd;
}
#endif

#if defined(HAVE_SYS_UN_H) &&	\
    defined(AF_UNIX) &&		\
    defined(SOCK_NONBLOCK)
#define HAVE_SYSCALL_SOCKETPAIR
static int syscall_socketpair(void)
{
	int sfds[2], ret;

	t1 = syscall_time_now();
	ret = socketpair(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK, 0, sfds);
	t2 = syscall_time_now();
	if (ret >= 0) {
		(void)close(sfds[0]);
		(void)close(sfds[1]);
	}

	return ret;
}
#endif

#if defined(HAVE_SPLICE) && 	\
    defined(SPLICE_F_NONBLOCK)
#define HAVE_SYSCALL_SPLICE
static int syscall_splice(void)
{
	int ret = -1;
	int fd1[2], fd2[2];
	char buf[4];
	ssize_t sret;

	if (pipe(fd1) < 0)
		return -1;
	if (pipe(fd2) < 0)
		goto pipe_close_fd1;

	sret = write(fd1[1], "test", 4);
	if (sret < 0)
		goto pipe_close_fd2;

	t1 = syscall_time_now();
	ret = splice(fd1[0], NULL, fd2[1], NULL, 4, SPLICE_F_NONBLOCK);
	t2 = syscall_time_now();
	sret = read(fd2[0], buf, 4);
	if (sret < 0)
		ret = -1;
pipe_close_fd2:
	(void)close(fd2[0]);
	(void)close(fd2[1]);
pipe_close_fd1:
	(void)close(fd1[0]);
	(void)close(fd1[1]);

	return ret;
}
#endif

#define HAVE_SYSCALL_STAT
static int syscall_stat(void)
{
	struct stat statbuf;
	int ret;

	t1 = syscall_time_now();
	ret = shim_stat(syscall_filename, &statbuf);
	t2 = syscall_time_now();
	return ret;
}

#if defined(HAVE_SYS_SYSINFO_H) &&      \
    defined(HAVE_SYSINFO) &&            \
    defined(HAVE_SYS_STATFS_H) &&	\
    defined(HAVE_STATFS)
#define HAVE_SYSCALL_STATFS
static int syscall_statfs(void)
{
	struct statfs statfsbuf;
	int ret;

	t1 = syscall_time_now();
	ret = statfs("/", &statfsbuf);
	t2 = syscall_time_now();
	return ret;
}
#endif

/* FIXME */
#if defined(AT_EMPTY_PATH) &&   \
    defined(AT_SYMLINK_NOFOLLOW)
#define HAVE_SYSCALL_STATX
static int syscall_statx(void)
{
	shim_statx_t bufx;
	int ret;
	char path[PATH_MAX];

	if (!realpath(syscall_filename, path))
		return -1;

	t1 = syscall_time_now();
	ret = shim_statx(AT_EMPTY_PATH, path, AT_SYMLINK_NOFOLLOW, SHIM_STATX_ALL, &bufx);
	t2 = syscall_time_now();
	return ret;
}
#endif

#define HAVE_SYSCALL_SYMLINK
static int syscall_symlink(void)
{
	int ret;

	t1 = syscall_time_now();
	ret = symlink(syscall_filename, syscall_tmp_filename);
	t2 = syscall_time_now();
	VOID_RET(int, shim_unlink(syscall_tmp_filename));
	return ret;
}

#if defined(HAVE_SYMLINKAT)
#define HAVE_SYSCALL_SYMLINKAT
static int syscall_symlinkat(void)
{
	int ret;

	t1 = syscall_time_now();
	ret = symlinkat(syscall_filename, syscall_dir_fd, syscall_tmp_filename);
	t2 = syscall_time_now();
	VOID_RET(int, shim_unlink(syscall_tmp_filename));
	return ret;
}
#endif

#if defined(HAVE_SYNC)
#define HAVE_SYSCALL_SYNC
static int syscall_sync(void)
{
	t1 = syscall_time_now();
	sync();
	t2 = syscall_time_now();
	return 0;
}
#endif

#if defined(HAVE_SYNC_FILE_RANGE)
#define HAVE_SYSCALL_SYNC_FILE_RANGE
static int syscall_sync_file_range(void)
{
	static size_t i = 0;
	int ret;
	static const int flags[] = {
#if defined(SYNC_FILE_RANGE_WAIT_BEFORE)
		SYNC_FILE_RANGE_WAIT_BEFORE,
#endif
#if defined(SYNC_FILE_RANGE_WRITE)
		SYNC_FILE_RANGE_WRITE,
#endif
#if defined(SYNC_FILE_RANGE_WAIT_AFTER)
		SYNC_FILE_RANGE_WAIT_AFTER,
#endif
#if defined(SYNC_FILE_RANGE_WAIT_BEFORE) && \
    defined(SYNC_FILE_RANGE_WRITE)
		SYNC_FILE_RANGE_WAIT_BEFORE | SYNC_FILE_RANGE_WRITE,
#endif
#if defined(SYNC_FILE_RANGE_WAIT_BEFORE) && \
    defined(SYNC_FILE_RANGE_WAIT_AFTER)
		SYNC_FILE_RANGE_WAIT_BEFORE | SYNC_FILE_RANGE_WAIT_AFTER,
#endif
#if defined(SYNC_FILE_RANGE_WRITE) &&	\
    defined(SYNC_FILE_RANGE_WAIT_AFTER)
		SYNC_FILE_RANGE_WRITE | SYNC_FILE_RANGE_WAIT_AFTER,
#endif
#if defined(SYNC_FILE_RANGE_WAIT_BEFORE) && \
    defined(SYNC_FILE_RANGE_WRITE) &&	\
    defined(SYNC_FILE_RANGE_WAIT_AFTER)
		SYNC_FILE_RANGE_WAIT_BEFORE | SYNC_FILE_RANGE_WRITE | SYNC_FILE_RANGE_WAIT_AFTER,
#endif
	};
	const int flag = flags[i];

	i++;
	if (i >= SIZEOF_ARRAY(flags))
		i = 0;
	t1 = syscall_time_now();
	ret = shim_sync_file_range(syscall_fd, 0, 4096, flag);
	t2 = syscall_time_now();
	return ret;
}
#endif

#if defined(HAVE_SYNCFS) &&	\
    defined(O_NONBLOCK) &&	\
    defined(O_DIRECTORY) &&	\
    defined(HAVE_OPENAT)
#define HAVE_SYSCALL_SYNCFS
static int syscall_syncfs(void)
{
	int fd, ret;

	fd = openat(AT_FDCWD, ".", O_RDONLY | O_NONBLOCK | O_DIRECTORY);
	if (fd < 0)
		return -1;
	t1 = syscall_time_now();
	ret = syncfs(fd);
	t2 = syscall_time_now();
	(void)close(fd);
	return ret;
}
#endif

#if defined(HAVE_SYS_SYSINFO_H) &&	\
    defined(HAVE_SYSINFO)
#define HAVE_SYSCALL_SYSINFO
static int syscall_sysinfo(void)
{
	struct sysinfo info;
	int ret;

	t1 = syscall_time_now();
	ret = sysinfo(&info);
	t2 = syscall_time_now();
	return ret;
}
#endif

#if defined(__linux__) &&	\
    defined(HAVE_SYSCALL) &&	\
    defined(__NR_syslog)
#define HAVE_SYSCALL_SYSLOG
static int syscall_syslog(void)
{
#define SYSLOG_ACTION_READ	(2)
	int ret;

	char buffer[1024];

	t1 = syscall_time_now();
	ret = syscall(__NR_syslog, SYSLOG_ACTION_READ, buffer, sizeof(buffer));
	t2 = syscall_time_now();

	return ret;

#undef SYSLOG_ACTION_READ
}
#endif

#if defined(HAVE_TEE) && 	\
    defined(SPLICE_F_NONBLOCK)
#define HAVE_SYSCALL_TEE
static int syscall_tee(void)
{
	int ret;
	int fd1[2], fd2[2];
	char buf[4];
	ssize_t sret;

	if (pipe(fd1) < 0)
		return -1;
	if (pipe(fd2) < 0) {
		ret = -1;
		goto close_fd1;
	}

	sret = write(fd1[1], "test", 4);
	if (sret < 0) {
		ret = -1;
		goto close_fd2;
	}
	t1 = syscall_time_now();
	ret = tee(fd1[0], fd2[1], 1, SPLICE_F_NONBLOCK);
	t2 = syscall_time_now();
	sret = read(fd2[0], buf, 4);
	if (sret < 0)
		ret = -1;
close_fd2:
	(void)close(fd2[0]);
	(void)close(fd2[1]);
close_fd1:
	(void)close(fd1[0]);
	(void)close(fd1[1]);
	return ret;
}
#endif

#define HAVE_SYSCALL_TIME
static int syscall_time(void)
{
	time_t t;
	time_t ret;

	t1 = syscall_time_now();
	ret = time(&t);
	t2 = syscall_time_now();
	return (ret == ((time_t) -1)) ? -1 : 0;
}

#if defined(HAVE_LIB_RT) &&             \
    defined(HAVE_TIMER_CREATE) &&       \
    defined(HAVE_TIMER_DELETE)
#define HAVE_SYSCALL_TIMER_CREATE
static int syscall_timer_create(void)
{
	struct sigevent sev;
	timer_t timerid;
	int ret;

	(void)shim_memset(&sev, 0, sizeof(sev));
	sev.sigev_notify = SIGEV_SIGNAL;
	sev.sigev_signo = SIGRTMIN;
	sev.sigev_value.sival_ptr = &timerid;

	t1 = syscall_time_now();
	ret = timer_create(CLOCK_REALTIME, &sev, &timerid);
	t2 = syscall_time_now();

	VOID_RET(int, timer_delete(timerid));

	return ret;
}
#endif

#if defined(HAVE_LIB_RT) &&             \
    defined(HAVE_TIMER_CREATE) &&       \
    defined(HAVE_TIMER_DELETE)
#define HAVE_SYSCALL_TIMER_DELETE
static int syscall_timer_delete(void)
{
	struct sigevent sev;
	timer_t timerid;
	int ret;

	(void)shim_memset(&sev, 0, sizeof(sev));
	sev.sigev_notify = SIGEV_SIGNAL;
	sev.sigev_signo = SIGRTMIN;
	sev.sigev_value.sival_ptr = &timerid;

	ret = timer_create(CLOCK_REALTIME, &sev, &timerid);
	if (ret < 0)
		return -1;

	t1 = syscall_time_now();
	ret = timer_delete(timerid);
	t2 = syscall_time_now();

	return ret;
}
#endif

#if defined(HAVE_SYS_TIMERFD_H)	&&	\
    defined(HAVE_TIMERFD_CREATE)
#define HAVE_SYSCALL_TIMERFD_CREATE
static int syscall_timerfd_create(void)
{
	int fd;

	t1 = syscall_time_now();
	fd = timerfd_create(CLOCK_REALTIME, 0);
	t2 = syscall_time_now();

	if (fd < 0)
		return -1;
	(void)close(fd);

	return fd;
}
#endif

#if defined(HAVE_SYS_TIMERFD_H)	&&	\
    defined(HAVE_TIMERFD_GETTIME) &&	\
    defined(HAVE_TIMERFD_CREATE)
#define HAVE_SYSCALL_TIMERFD_GETTIME
static int syscall_timerfd_gettime(void)
{
	int fd, ret;
	struct itimerspec value;

	fd = timerfd_create(CLOCK_REALTIME, 0);
	if (fd < 0)
		return -1;

	t1 = syscall_time_now();
	ret = timerfd_gettime(fd, &value);
	t2 = syscall_time_now();

	(void)close(fd);

	return ret;
}
#endif

#if defined(HAVE_SYS_TIMERFD_H)	&&	\
    defined(HAVE_TIMERFD_SETTIME) &&	\
    defined(HAVE_TIMERFD_GETTIME) &&	\
    defined(HAVE_TIMERFD_CREATE)
#define HAVE_SYSCALL_TIMERFD_SETTIME
static int syscall_timerfd_settime(void)
{
	int fd, ret;
	struct itimerspec value;

	fd = timerfd_create(CLOCK_REALTIME, 0);
	if (fd < 0)
		return -1;

	ret = timerfd_gettime(fd, &value);
	if (ret < 0)
		return -1;
	t1 = syscall_time_now();
	ret = timerfd_settime(fd, 0, &value, NULL);
	t2 = syscall_time_now();

	(void)close(fd);

	return ret;
}
#endif

#if defined(HAVE_LIB_RT) &&             \
    defined(HAVE_TIMER_CREATE) &&       \
    defined(HAVE_TIMER_DELETE) &&	\
    defined(HAVE_TIMER_GETTIME)
#define HAVE_SYSCALL_TIMER_GETTIME
static int syscall_timer_gettime(void)
{
	struct sigevent sev;
	struct itimerspec value;
	timer_t timerid;
	int ret;

	(void)shim_memset(&sev, 0, sizeof(sev));
	sev.sigev_notify = SIGEV_SIGNAL;
	sev.sigev_signo = SIGRTMIN;
	sev.sigev_value.sival_ptr = &timerid;

	ret = timer_create(CLOCK_REALTIME, &sev, &timerid);
	if (ret < 0)
		return -1;

	t1 = syscall_time_now();
	ret = timer_gettime(timerid, &value);
	t2 = syscall_time_now();

	VOID_RET(int, timer_delete(timerid));

	return ret;
}
#endif

#if defined(HAVE_LIB_RT) &&             \
    defined(HAVE_TIMER_CREATE) &&       \
    defined(HAVE_TIMER_DELETE) &&	\
    defined(HAVE_TIMER_GETOVERRUN)
#define HAVE_SYSCALL_TIMER_GETOVERRUN
static int syscall_timer_getoverrun(void)
{
	struct sigevent sev;
	timer_t timerid;
	int ret;

	(void)shim_memset(&sev, 0, sizeof(sev));
	sev.sigev_notify = SIGEV_SIGNAL;
	sev.sigev_signo = SIGRTMIN;
	sev.sigev_value.sival_ptr = &timerid;

	ret = timer_create(CLOCK_REALTIME, &sev, &timerid);
	if (ret < 0)
		return -1;

	t1 = syscall_time_now();
	ret = timer_getoverrun(timerid);
	t2 = syscall_time_now();

	VOID_RET(int, timer_delete(timerid));

	return ret;
}
#endif

#if defined(HAVE_LIB_RT) &&             \
    defined(HAVE_TIMER_CREATE) &&       \
    defined(HAVE_TIMER_DELETE) &&	\
    defined(HAVE_TIMER_SETTIME) &&	\
    defined(HAVE_TIMER_GETTIME)
#define HAVE_SYSCALL_TIMER_SETTIME
static int syscall_timer_settime(void)
{
	struct sigevent sev;
	struct itimerspec new_value, old_value;
	timer_t timerid;
	int ret;

	(void)shim_memset(&sev, 0, sizeof(sev));
	sev.sigev_notify = SIGEV_SIGNAL;
	sev.sigev_signo = SIGRTMIN;
	sev.sigev_value.sival_ptr = &timerid;

	ret = timer_create(CLOCK_REALTIME, &sev, &timerid);
	if (ret < 0)
		return -1;

	ret = timer_gettime(timerid, &new_value);
	if (ret < 0) {
		VOID_RET(int, timer_delete(timerid));
		return -1;
	}

	t1 = syscall_time_now();
	ret = timer_settime(timerid, 0, &new_value, &old_value);
	t2 = syscall_time_now();

	if (ret == 0)
		VOID_RET(int, timer_settime(timerid, 0, &old_value, NULL));
	VOID_RET(int, timer_delete(timerid));

	return ret;
}
#endif

#define HAVE_SYSCALL_TIMES
static int syscall_times(void)
{
	struct tms buf;
	clock_t ret;

	t1 = syscall_time_now();
	ret = times(&buf);
	t2 = syscall_time_now();
	return (int)ret;
}

#define HAVE_SYSCALL_TRUNCATE
static int syscall_truncate(void)
{
	const off_t size = (off_t)65536;
	int ret;

	t1 = syscall_time_now();
	ret = truncate(syscall_filename, size);
	t2 = syscall_time_now();
	return ret;
}

#define HAVE_SYSCALL_UMASK
static int syscall_umask(void)
{
	mode_t mask = (mode_t)stress_mwc32() & 0777;

	t1 = syscall_time_now();
	VOID_RET(mode_t, umask(mask));
	t2 = syscall_time_now();

	/* And restore */
	VOID_RET(int, umask(syscall_umask_mask));
	return 0;
}

#if defined(HAVE_UNAME) &&	\
    defined(HAVE_SYS_UTSNAME_H)
#define HAVE_SYSCALL_UNAME
static int syscall_uname(void)
{
	struct utsname utsbuf;
	int ret;

	t1 = syscall_time_now();
	ret = uname(&utsbuf);
	t2 = syscall_time_now();
	return ret;
}
#endif

#define HAVE_SYSCALL_UNLINK
static int syscall_unlink(void)
{
	int fd, ret;

	fd = creat(syscall_tmp_filename, S_IRUSR | S_IWUSR);
	if (fd < 0)
		return -1;

	VOID_RET(int, close(fd));

	t1 = syscall_time_now();
	ret = shim_unlink(syscall_tmp_filename);
	t2 = syscall_time_now();
	return ret;
}

#if defined(HAVE_UNLINKAT)
#define HAVE_SYSCALL_UNLINKAT
static int syscall_unlinkat(void)
{
	int fd, ret;

	fd = creat(syscall_tmp_filename, S_IRUSR | S_IWUSR);
	if (fd < 0)
		return -1;

	VOID_RET(int, close(fd));

	t1 = syscall_time_now();
	ret = shim_unlinkat(syscall_dir_fd, syscall_tmp_filename, 0);
	t2 = syscall_time_now();
	return ret;
}
#endif

#if defined(HAVE_UNSHARE) &&	\
    defined(HAVE_CLONE) &&	\
    defined(__linux__)

static const int unshare_flags[] = {
#if defined(CLONE_FS)
	CLONE_FS,
#endif
#if defined(CLONE_FILES)
	CLONE_FILES,
#endif
#if defined(CLONE_NEWCGROUP)
	CLONE_NEWCGROUP,
#endif
#if defined(CLONE_NEWIPC)
	CLONE_NEWIPC,
#endif
#if defined(CLONE_NEWNET)
	/* CLONE_NEWNET, can be super slow */
#endif
#if defined(CLONE_NEWNS)
	CLONE_NEWNS,
#endif
#if defined(CLONE_NEWPID)
	CLONE_NEWPID,
#endif
#if defined(CLONE_NEWUSER)
	CLONE_NEWUSER,
#endif
#if defined(CLONE_NEWUTS)
	CLONE_NEWUTS,
#endif
#if defined(CLONE_SYSVSEM)
	CLONE_SYSVSEM,
#endif
#if defined(CLONE_THREAD)
	CLONE_THREAD,
#endif
#if defined(CLONE_SIGHAND)
	CLONE_SIGHAND,
#endif
#if defined(CLONE_VM)
	CLONE_VM,
#endif
};

static int syscall_unshare_func(void *arg)
{
	const int *unshare_flag = (int *)arg;
	int ret;

	(void)arg;

	syscall_shared_info->t1 = syscall_time_now();
	ret = unshare(*unshare_flag);
	syscall_shared_info->t2 = syscall_time_now();
	if (ret < 0) {
		syscall_shared_error(ret);
		return -1;
	}
	syscall_shared_info->t_set = true;
	return 0;
}

#define HAVE_SYSCALL_UNSHARE
static int syscall_unshare(void)
{
	pid_t pid;
	pid_t parent_tid = -1;
	pid_t child_tid = -1;
	int status;
	char stack[8192];
	char *stack_top = (char *)stress_get_stack_top((char *)stack, sizeof(stack));
	static size_t i = 0;
	int unshare_flag = unshare_flags[i];

	i++;
	if (i >= SIZEOF_ARRAY(unshare_flags))
		i = 0;

	syscall_shared_info->t1 = ~0ULL;
	syscall_shared_info->t2 = ~0ULL;
	syscall_shared_info->t_set = false;

	pid = clone(syscall_unshare_func, stress_align_stack(stack_top),
		    CLONE_CHILD_CLEARTID | CLONE_CHILD_SETTID | SIGCHLD, &unshare_flag,
		    &parent_tid, NULL, &child_tid);
	if (pid < 0)
		return -1;
	VOID_RET(pid_t, waitpid(pid, &status, 0));
	t1 = syscall_shared_info->t1;
	t2 = syscall_shared_info->t2;

	return pid;
}
#endif

#if defined(HAVE_LINUX_USERFAULTFD_H) &&	\
    defined(HAVE_SYSCALL) &&			\
    defined(__NR_userfaultfd)
#define HAVE_SYSCALL_USERFAULTFD
static int syscall_userfaultfd(void)
{
	int fd;

	t1 = syscall_time_now();
	fd = shim_userfaultfd(0);
	t2 = syscall_time_now();

	if (fd >= 0)
		(void)close(fd);
	return fd;
}
#endif

#if defined(HAVE_UTIME_H) &&	\
    defined(HAVE_UTIME) &&      \
    defined(HAVE_UTIMBUF)
#define HAVE_SYSCALL_UTIME
static int syscall_utime(void)
{
	struct utimbuf utbuf;
	struct timeval tv;
	int ret;

	VOID_RET(int, gettimeofday(&tv, NULL));
	utbuf.actime = (time_t)tv.tv_sec;
	utbuf.modtime = utbuf.actime;

	t1 = syscall_time_now();
	ret = utime(syscall_filename, &utbuf);
	t2 = syscall_time_now();
	return ret;
}
#endif

#if defined(HAVE_UTIME_H) &&	\
    defined(HAVE_UTIMENSAT) &&	\
    defined(UTIME_NOW)
#define HAVE_SYSCALL_UTIMENSAT
static int syscall_utimensat(void)
{
	struct timespec ts[2];
	int ret;

	ts[0].tv_sec = UTIME_NOW;
	ts[0].tv_nsec = UTIME_NOW;

	ts[1].tv_sec = UTIME_NOW;
	ts[1].tv_nsec = UTIME_NOW;

	t1 = syscall_time_now();
	ret = utimensat(syscall_dir_fd, syscall_filename, ts, 0);
	t2 = syscall_time_now();
	return ret;
}
#endif

#if defined(HAVE_UTIMES)
#define HAVE_SYSCALL_UTIMES
static int syscall_utimes(void)
{
	struct timeval tvs[2];
	int ret;

	VOID_RET(int, gettimeofday(&tvs[0], NULL));
	tvs[1] = tvs[0];

	t1 = syscall_time_now();
	ret = utimes(syscall_filename, tvs);
	t2 = syscall_time_now();
	return ret;
}
#endif

#define HAVE_SYSCALL_VFORK
static int syscall_vfork(void)
{
	pid_t pid;

	t1 = syscall_time_now();
	pid = shim_vfork();
	if (pid < 0) {
		return -1;
	} else if (pid == 0) {
		_exit(0);
	} else {
		int status;

		t2 = syscall_time_now();
		VOID_RET(pid_t, waitpid(pid, &status, 0));
	}
	return 0;
}

#if defined(HAVE_VMSPLICE) &&   \
    defined(SPLICE_F_MOVE)
#define HAVE_SYSCALL_VMSPLICE
static int syscall_vmsplice(void)
{
	int fds[2];
	struct iovec iov;
	void *buf;
	ssize_t ret;

	buf = mmap(NULL, syscall_page_size, PROT_READ | PROT_WRITE,
			MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
	if (buf == MAP_FAILED)
		return -1;

	(void)shim_memset(buf, 0xa5, syscall_page_size);
	if (pipe(fds) < 0) {
		(void)munmap(buf, syscall_page_size);
		return -1;
	}
	iov.iov_base = buf;
	iov.iov_len = syscall_page_size;

	t1 = syscall_time_now();
	ret = vmsplice(fds[1], &iov, 1, 0);
	t2 = syscall_time_now();
	(void)close(fds[0]);
	(void)close(fds[1]);
	(void)munmap(buf, syscall_page_size);

	return (int)ret;
}
#endif

#if defined(HAVE_WAITID)
#define HAVE_SYSCALL_WAITID
static int syscall_waitid(void)
{
	pid_t pid;

	syscall_shared_error(0);
	pid = fork();
	if (pid < 0) {
		return -1;
	} else if (pid == 0) {
		syscall_shared_info->t1 = syscall_time_now();
		_exit(0);
	} else {
		for (;;) {
			int ret;
			siginfo_t info;

			ret = waitid(P_PID, pid, &info, WEXITED);
			if ((ret == 0) && (info.si_pid == pid))
				break;
			(void)shim_sched_yield();
		}
		t2 = syscall_time_now();
		t1 = syscall_shared_info->t1;
	}
	return 0;
}
#endif

#define HAVE_SYSCALL_WAIT
static int syscall_wait(void)
{
	pid_t pid;

	syscall_shared_error(0);
	pid = fork();
	if (pid < 0) {
		return -1;
	} else if (pid == 0) {
		syscall_shared_info->t1 = syscall_time_now();
		_exit(0);
	} else {
		for (;;) {
			int ret, status;

			ret = wait(&status);
			if (ret == pid)
				break;
			(void)shim_sched_yield();
		}
		t2 = syscall_time_now();
		t1 = syscall_shared_info->t1;
	}
	return 0;
}

#if defined(HAVE_WAIT3)
#define HAVE_SYSCALL_WAIT3
static int syscall_wait3(void)
{
	pid_t pid;

	syscall_shared_error(0);
	pid = fork();
	if (pid < 0) {
		return -1;
	} else if (pid == 0) {
		syscall_shared_info->t1 = syscall_time_now();
		_exit(0);
	} else {
		for (;;) {
			int ret, status;
			struct rusage usage;

			ret = wait3(&status, 0, &usage);
			if (ret == pid)
				break;
			(void)shim_sched_yield();
		}
		t2 = syscall_time_now();
		t1 = syscall_shared_info->t1;
	}
	return 0;
}
#endif

#if defined(HAVE_WAIT4)
#define HAVE_SYSCALL_WAIT4
static int syscall_wait4(void)
{
	pid_t pid;

	syscall_shared_error(0);
	pid = fork();
	if (pid < 0) {
		return -1;
	} else if (pid == 0) {
		syscall_shared_info->t1 = syscall_time_now();
		_exit(0);
	} else {
		for (;;) {
			int ret, status;
			struct rusage usage;

			ret = wait4(pid, &status, 0, &usage);
			if (ret == pid)
				break;
			(void)shim_sched_yield();
		}
		t2 = syscall_time_now();
		t1 = syscall_shared_info->t1;
	}
	return 0;
}
#endif

#define HAVE_SYSCALL_WAITPID
static int syscall_waitpid(void)
{
	pid_t pid;

	syscall_shared_error(0);
	pid = fork();
	if (pid < 0) {
		return -1;
	} else if (pid == 0) {
		syscall_shared_info->t1 = syscall_time_now();
		_exit(0);
	} else {
		int status;

		for (;;) {
			pid_t ret;

			ret = waitpid(pid, &status, 0);
			if (ret == pid)
				break;
			(void)shim_sched_yield();
		}
		t2 = syscall_time_now();
		t1 = syscall_shared_info->t1;
	}
	return 0;
}

#define HAVE_SYSCALL_WRITE
static int syscall_write(void)
{
	char buffer[512];
	ssize_t ret;
	off_t offset = (stress_mwc8() & 0x7) * 512;

	stress_uint8rnd4((uint8_t *)buffer, sizeof(buffer));
	VOID_RET(off_t, lseek(syscall_fd, offset, SEEK_SET));
	t1 = syscall_time_now();
	ret = write(syscall_fd, buffer, sizeof(buffer));
	t2 = syscall_time_now();
	return (int)ret;
}

#if defined(HAVE_SYS_UIO_H)
#define HAVE_SYSCALL_WRITEV
static int syscall_writev(void)
{
	char buffer[1024];
	ssize_t ret;
	off_t offset = (stress_mwc8() & 0x7) * 512;
	struct iovec iov[2];

	iov[0].iov_base = &buffer[512];
	iov[0].iov_len = 512;

	iov[1].iov_base = &buffer[0];
	iov[1].iov_len = 512;

	stress_uint8rnd4((uint8_t *)buffer, sizeof(buffer));

	VOID_RET(off_t, lseek(syscall_fd, offset, SEEK_SET));
	t1 = syscall_time_now();
	ret = writev(syscall_fd, iov, SIZEOF_ARRAY(iov));
	t2 = syscall_time_now();
	return (int)ret;
}
#endif

static const syscall_t syscalls[] = {
#if defined(HAVE_SYSCALL_ACCEPT)
	SYSCALL(syscall_accept),
#endif
#if defined(HAVE_SYSCALL_ACCEPT4)
	SYSCALL(syscall_accept4),
#endif
#if defined(HAVE_SYSCALL_ACCESS)
	SYSCALL(syscall_access),
#endif
	/* syscall_acct, ignore, don't want to interfere with process accounting */
#if defined(HAVE_SYSCALL_ADD_KEY)
	SYSCALL(syscall_add_key),
#endif
	/* syscall_adjtimex, ignore, don't want to adjust system time */
#if defined(HAVE_SYSCALL_ALARM)
	SYSCALL(syscall_alarm),
#endif
	/* syscall_arch_prctl, ignored */
#if defined(HAVE_SYSCALL_BIND)
	SYSCALL(syscall_bind),
#endif
	/* syscall_bpf, ignore for now */
#if defined(HAVE_SYSCALL_BRK)
	SYSCALL(syscall_brk),
#endif
#if defined(HAVE_SYSCALL_CACHEFLUSH)
	SYSCALL(syscall_cacheflush),
#endif
#if defined(HAVE_SYSCALL_CAPGET)
	SYSCALL(syscall_capget),
#endif
#if defined(HAVE_SYSCALL_CAPSET)
	SYSCALL(syscall_capset),
#endif
#if defined(HAVE_SYSCALL_CHDIR)
	SYSCALL(syscall_chdir),
#endif
#if defined(HAVE_SYSCALL_CHMOD)
	SYSCALL(syscall_chmod),
#endif
#if defined(HAVE_SYSCALL_CHOWN)
	SYSCALL(syscall_chown),
#endif
#if defined(HAVE_SYSCALL_CHROOT)
	SYSCALL(syscall_chroot),
#endif
#if defined(HAVE_SYSCALL_CLOCK_ADJTIME)
	SYSCALL(syscall_clock_adjtime),
#endif
#if defined(HAVE_SYSCALL_CLOCK_GETRES)
	SYSCALL(syscall_clock_getres),
#endif
#if defined(HAVE_SYSCALL_CLOCK_GETTIME)
	SYSCALL(syscall_clock_gettime),
#endif
#if defined(HAVE_SYSCALL_CLOCK_NANOSLEEP)
	SYSCALL(syscall_clock_nanosleep),
#endif
#if defined(HAVE_SYSCALL_CLOCK_SETTIME)
	SYSCALL(syscall_clock_settime),
#endif
#if defined(HAVE_SYSCALL_CLONE)
	SYSCALL(syscall_clone),
#endif
#if defined(HAVE_SYSCALL_CLONE3)
	SYSCALL(syscall_clone3),
#endif
#if defined(HAVE_SYSCALL_CLOSE)
	SYSCALL(syscall_close),
#endif
#if defined(HAVE_SYSCALL_CONNECT)
	SYSCALL(syscall_connect),
#endif
#if defined(HAVE_SYSCALL_COPY_FILE_RANGE)
	SYSCALL(syscall_copy_file_range),
#endif
#if defined(HAVE_SYSCALL_CREAT)
	SYSCALL(syscall_creat),
#endif
	/* syscall_create_module, ignore */
	/* syscall_delete_module. ignore */
#if defined(HAVE_SYSCALL_DUP2)
	SYSCALL(syscall_dup),
#endif
#if defined(HAVE_SYSCALL_DUP2)
	SYSCALL(syscall_dup2),
#endif
#if defined(HAVE_SYSCALL_DUP3)
	SYSCALL(syscall_dup3),
#endif
#if defined(HAVE_SYSCALL_EPOLL_CREATE)
	SYSCALL(syscall_epoll_create),
#endif
#if defined(HAVE_SYSCALL_EPOLL_CREATE1)
	SYSCALL(syscall_epoll_create1),
#endif
#if defined(HAVE_SYSCALL_EPOLL_CTL)
	SYSCALL(syscall_epoll_ctl),
#endif
#if defined(HAVE_SYSCALL_EPOLL_PWAIT)
	SYSCALL(syscall_epoll_pwait),
#endif
#if defined(HAVE_SYSCALL_EPOLL_WAIT)
	SYSCALL(syscall_epoll_wait),
#endif
#if defined(HAVE_SYSCALL_EVENTFD)
	SYSCALL(syscall_eventfd),
#endif
#if defined(HAVE_SYSCALL_EVENTFD2)
	SYSCALL(syscall_eventfd2),	/* not yet implemented */
#endif
#if defined(HAVE_SYSCALL_EXECVE)
	SYSCALL(syscall_execve),
#endif
#if defined(HAVE_SYSCALL_EXECVEAT)
	SYSCALL(syscall_execveat),
#endif
#if defined(HAVE_SYSCALL_EXIT)
	SYSCALL(syscall_exit),
#endif
	/* syscall_exit_group */
#if defined(HAVE_SYSCALL_FACCESSAT)
	SYSCALL(syscall_faccessat),
#endif
#if defined(HAVE_SYSCALL_FALLOCATE)
	SYSCALL(syscall_fallocate),
#endif
#if defined(HAVE_SYSCALL_FANOTIFY_INIT)
	SYSCALL(syscall_fanotify_init),
#endif
#if defined(HAVE_SYSCALL_FANOTIFY_MARK)
	SYSCALL(syscall_fanotify_mark),
#endif
#if defined(HAVE_SYSCALL_FCHDIR)
	SYSCALL(syscall_fchdir),
#endif
#if defined(HAVE_SYSCALL_FCHMOD)
	SYSCALL(syscall_fchmod),
#endif
#if defined(HAVE_SYSCALL_FCHMODAT)
	SYSCALL(syscall_fchmodat),
#endif
#if defined(HAVE_SYSCALL_FCHMODAT2)
	SYSCALL(syscall_fchmodat2),
#endif
#if defined(HAVE_SYSCALL_FCHOWN)
	SYSCALL(syscall_fchown),
#endif
#if defined(HAVE_SYSCALL_FCHOWNAT)
	SYSCALL(syscall_fchownat),
#endif
#if defined(HAVE_SYSCALL_FCNTL)
	SYSCALL(syscall_fcntl),
#endif
#if defined(HAVE_SYSCALL_FDATASYNC)
	SYSCALL(syscall_fdatasync),
#endif
#if defined(HAVE_SYSCALL_FGETXATTR)
	SYSCALL(syscall_fgetxattr),
#endif
	/* syscall_finit_module, */
#if defined(HAVE_SYSCALL_FLISTXATTR)
	SYSCALL(syscall_flistxattr),
#endif
#if defined(HAVE_SYSCALL_FLOCK)
	SYSCALL(syscall_flock),
#endif
#if defined(HAVE_SYSCALL_FORK)
	SYSCALL(syscall_fork),
#endif
#if defined(HAVE_SYSCALL_FREMOVEXATTR)
	SYSCALL(syscall_fremovexattr),
#endif
	/* syscall_fsconfig, ignored */
#if defined(HAVE_SYSCALL_FSETXATTR)
	SYSCALL(syscall_fsetxattr),
#endif
	/* syscall_fsmount, ignored */
	/* syscall_fsopen, ignored */
	/* syscall_fspick, ignored */
#if defined(HAVE_SYSCALL_FSTAT)
	SYSCALL(syscall_fstat),
#endif
#if defined(HAVE_SYSCALL_FSTATAT)
	SYSCALL(syscall_fstatat),
#endif
#if defined(HAVE_SYSCALL_FSTATFS)
	SYSCALL(syscall_fstatfs),
#endif
#if defined(HAVE_SYSCALL_FSYNC)
	SYSCALL(syscall_fsync),
#endif
#if defined(HAVE_SYSCALL_FTRUNCATE)
	SYSCALL(syscall_ftruncate),
#endif
	/* syscall_futex, */
#if defined(HAVE_SYSCALL_FUTIMES)
	SYSCALL(syscall_futimes),
#endif
#if defined(HAVE_SYSCALL_FUTIMESAT)
	SYSCALL(syscall_futimesat),
#endif
#if defined(HAVE_SYSCALL_GETCPU)
	SYSCALL(syscall_getcpu),
#endif
#if defined(HAVE_SYSCALL_GETCWD)
	SYSCALL(syscall_getcwd),
#endif
#if defined(HAVE_SYSCALL_GETDENTS)
	SYSCALL(syscall_getdents),
#endif
#if defined(HAVE_SYSCALL_GETEGID)
	SYSCALL(syscall_getegid),
#endif
#if defined(HAVE_SYSCALL_GETEUID)
	SYSCALL(syscall_geteuid),
#endif
#if defined(HAVE_SYSCALL_GETGID)
	SYSCALL(syscall_getgid),
#endif
#if defined(HAVE_SYSCALL_GETGROUPS)
	SYSCALL(syscall_getgroups),
#endif
#if defined(HAVE_SYSCALL_GETITIMER)
	SYSCALL(syscall_getitimer),
#endif
#if defined(HAVE_SYSCALL_GET_MEMPOLICY)
	SYSCALL(syscall_get_mempolicy),
#endif
#if defined(HAVE_SYSCALL_GETPEERNAME)
	SYSCALL(syscall_getpeername),
#endif
#if defined(HAVE_SYSCALL_GETPGID)
	SYSCALL(syscall_getpgid),
#endif
#if defined(HAVE_SYSCALL_GETPGRP)
	SYSCALL(syscall_getpgrp),
#endif
#if defined(HAVE_SYSCALL_GETPID)
	SYSCALL(syscall_getpid),
#endif
#if defined(HAVE_SYSCALL_GETPPID)
	SYSCALL(syscall_getppid),
#endif
#if defined(HAVE_SYSCALL_GETPRIORITY)
	SYSCALL(syscall_getpriority),
#endif
#if defined(HAVE_SYSCALL_GETRANDOM)
	SYSCALL(syscall_getrandom),
#endif
#if defined(HAVE_SYSCALL_GETRESGID)
	SYSCALL(syscall_getresgid),
#endif
#if defined(HAVE_SYSCALL_GETRESUID)
	SYSCALL(syscall_getresuid),
#endif
#if defined(HAVE_SYSCALL_GETRLIMIT)
	SYSCALL(syscall_getrlimit),
#endif
#if defined(HAVE_SYSCALL_GET_ROBUST_LIST)
	SYSCALL(syscall_get_robust_list),
#endif
#if defined(HAVE_SYSCALL_GETRUSAGE)
	SYSCALL(syscall_getrusage),
#endif
#if defined(HAVE_SYSCALL_GETSID)
	SYSCALL(syscall_getsid),
#endif
#if defined(HAVE_SYSCALL_GETSOCKNAME)
	SYSCALL(syscall_getsockname),
#endif
#if defined(HAVE_SYSCALL_GETSOCKOPT)
	SYSCALL(syscall_getsockopt),
#endif
#if defined(HAVE_SYSCALL_GET_THREAD_AREA)
	SYSCALL(syscall_get_thread_area),
#endif
#if defined(HAVE_SYSCALL_GETTID)
	SYSCALL(syscall_gettid),
#endif
#if defined(HAVE_SYSCALL_GETTIMEOFDAY)
	SYSCALL(syscall_gettimeofday),
#endif
#if defined(HAVE_SYSCALL_GETUID)
	SYSCALL(syscall_getuid),
#endif
#if defined(HAVE_SYSCALL_GETXATTR)
	SYSCALL(syscall_getxattr),
#endif
	/* syscall_init_module, */
#if defined(HAVE_SYSCALL_INOTIFY_ADD_WATCH)
	SYSCALL(syscall_inotify_add_watch),
#endif
#if defined(HAVE_SYSCALL_INOTIFY_INIT)
	SYSCALL(syscall_inotify_init),
#endif
#if defined(HAVE_SYSCALL_INOTIFY_INIT1)
	SYSCALL(syscall_inotify_init1),
#endif
#if defined(HAVE_SYSCALL_INOTIFY_RM_WATCH)
	SYSCALL(syscall_inotify_rm_watch),
#endif
#if defined(HAVE_SYSCALL_IO_CANCEL)
	SYSCALL(syscall_io_cancel),
#endif
#if defined(HAVE_SYSCALL_IO_DESTROY)
	SYSCALL(syscall_io_destroy),
#endif
#if defined(HAVE_SYSCALL_IO_GETEVENTS)
	SYSCALL(syscall_io_getevents),
#endif
#if defined(HAVE_SYSCALL_IO_PGETEVENTS)
	SYSCALL(syscall_io_pgetevents),
#endif
#if defined(HAVE_SYSCALL_IOPRIO_GET)
	SYSCALL(syscall_ioprio_get),
#endif
#if defined(HAVE_SYSCALL_IOPRIO_SET)
	SYSCALL(syscall_ioprio_set),
#endif
#if defined(HAVE_SYSCALL_IO_SETUP)
	SYSCALL(syscall_io_setup),
#endif
#if defined(HAVE_SYSCALL_IO_SUBMIT)
	SYSCALL(syscall_io_submit),
#endif
	/* syscall_io_uring_enter, */
	/* syscall_io_uring_register, */
#if defined(HAVE_SYSCALL_IO_URING_SETUP)
	SYSCALL(syscall_io_uring_setup),
#endif
#if defined(HAVE_SYSCALL_IOPERM)
	SYSCALL(syscall_ioperm),
#endif
#if defined(HAVE_SYSCALL_IOPL)
	SYSCALL(syscall_iopl),
#endif
#if defined(HAVE_SYSCALL_IOCTL)
	SYSCALL(syscall_ioctl),
#endif
	/* syscall_ipc, ignored */
#if defined(HAVE_SYSCALL_KCMP)
	SYSCALL(syscall_kcmp),
#endif
	/* syscall_kexec_file_load, ignored */
	/* syscall_kexec_load, ignored */
#if defined(HAVE_SYSCALL_KEYCTL)
	SYSCALL(syscall_keyctl),
#endif
#if defined(HAVE_SYSCALL_KILL)
	SYSCALL(syscall_kill),
#endif
#if defined(HAVE_SYSCALL_LCHOWN)
	SYSCALL(syscall_lchown),
#endif
#if defined(HAVE_SYSCALL_LGETXATTR)
	SYSCALL(syscall_lgetxattr),
#endif
#if defined(HAVE_SYSCALL_LINK)
	SYSCALL(syscall_link),
#endif
#if defined(HAVE_SYSCALL_LINKAT)
	SYSCALL(syscall_linkat),
#endif
#if defined(HAVE_SYSCALL_LISTEN)
	SYSCALL(syscall_listen),
#endif
#if defined(HAVE_SYSCALL_LISTXATTR)
	SYSCALL(syscall_listxattr),
#endif
#if defined(HAVE_SYSCALL_LLISTXATTR)
	SYSCALL(syscall_llistxattr),
#endif
#if defined(HAVE_SYSCALL_LOOKUP_DCOOKIE)
	SYSCALL(syscall_lookup_dcookie),
#endif
#if defined(HAVE_SYSCALL_LREMOVEXATTR)
	SYSCALL(syscall_lremovexattr),
#endif
#if defined(HAVE_SYSCALL_LSEEK)
	SYSCALL(syscall_lseek),
#endif
#if defined(HAVE_SYSCALL_LSETXATTR)
	SYSCALL(syscall_lsetxattr),
#endif
#if defined(HAVE_SYSCALL_LSTAT)
	SYSCALL(syscall_lstat),
#endif
#if defined(HAVE_SYSCALL_LSM_GET_SELF_ATTR)
	SYSCALL(syscall_lsm_get_self_attr),
#endif
#if defined(HAVE_SYSCALL_LSM_LIST_MODULES)
	SYSCALL(syscall_lsm_list_modules),
#endif
#if defined(HAVE_SYSCALL_MADVISE)
	SYSCALL(syscall_madvise),
#endif
#if defined(HAVE_SYSCALL_MAP_SHADOW_STACK)
	SYSCALL(syscall_map_shadow_stack),
#endif
#if defined(HAVE_SYSCALL_MBIND)
	SYSCALL(syscall_mbind),
#endif
	/* syscall_memory_ordering, SPARC only */
#if defined(HAVE_SYSCALL_MEMBARRIER)
	SYSCALL(syscall_membarrier),
#endif
#if defined(HAVE_SYSCALL_MEMFD_CREATE)
	SYSCALL(syscall_memfd_create),
#endif
#if defined(HAVE_SYSCALL_MIGRATE_PAGES)
	SYSCALL(syscall_migrate_pages),
#endif
#if defined(HAVE_SYSCALL_MINCORE)
	SYSCALL(syscall_mincore),
#endif
#if defined(HAVE_SYSCALL_MKDIR)
	SYSCALL(syscall_mkdir),
#endif
#if defined(HAVE_SYSCALL_MKDIRAT)
	SYSCALL(syscall_mkdirat),
#endif
#if defined(HAVE_SYSCALL_MKNOD)
	SYSCALL(syscall_mknod),
#endif
#if defined(HAVE_SYSCALL_MKNODAT)
	SYSCALL(syscall_mknodat),
#endif
#if defined(HAVE_SYSCALL_MLOCK)
	SYSCALL(syscall_mlock),
#endif
#if defined(HAVE_SYSCALL_MLOCK2)
	SYSCALL(syscall_mlock2),
#endif
#if defined(HAVE_SYSCALL_MLOCKALL)
	SYSCALL(syscall_mlockall),
#endif
#if defined(HAVE_SYSCALL_MMAP)
	SYSCALL(syscall_mmap),
#endif
	/* syscall_modify_ldt, too risky */
	/* syscall_mount, ignored */
	/* syscall_move_mount, ignored */
#if defined(HAVE_SYSCALL_MOVE_PAGES)
	SYSCALL(syscall_move_pages),
#endif
#if defined(HAVE_SYSCALL_MPROTECT)
	SYSCALL(syscall_mprotect),
#endif
#if defined(HAVE_SYSCALL_MQ_CLOSE)
	SYSCALL(syscall_mq_close),
#endif
#if defined(HAVE_SYSCALL_MQ_GETATTR)
	SYSCALL(syscall_mq_getattr),
#endif
#if defined(HAVE_SYSCALL_MQ_NOTIFY)
	SYSCALL(syscall_mq_notify),
#endif
#if defined(HAVE_SYSCALL_MQ_OPEN)
	SYSCALL(syscall_mq_open),
#endif
#if defined(HAVE_SYSCALL_MQ_SETATTR)
	SYSCALL(syscall_mq_setattr),
#endif
#if defined(HAVE_SYSCALL_MQ_TIMEDRECEIVE)
	SYSCALL(syscall_mq_timedreceive),
#endif
#if defined(HAVE_SYSCALL_MQ_TIMEDSEND)
	SYSCALL(syscall_mq_timedsend),
#endif
#if defined(HAVE_SYSCALL_MQ_UNLINK)
	SYSCALL(syscall_mq_unlink),
#endif
#if defined(HAVE_SYSCALL_MREMAP)
	SYSCALL(syscall_mremap),
#endif
#if defined(HAVE_SYSCALL_MSGCTL)
	SYSCALL(syscall_msgctl),
#endif
#if defined(HAVE_SYSCALL_MSGGET)
	SYSCALL(syscall_msgget),
#endif
#if defined(HAVE_SYSCALL_MSGRCV)
	SYSCALL(syscall_msgrcv),
#endif
#if defined(HAVE_SYSCALL_MSGSND)
	SYSCALL(syscall_msgsnd),
#endif
#if defined(HAVE_SYSCALL_MSYNC)
	SYSCALL(syscall_msync),
#endif
#if defined(HAVE_SYSCALL_MUNLOCK)
	SYSCALL(syscall_munlock),
#endif
#if defined(HAVE_SYSCALL_MUNLOCKALL)
	SYSCALL(syscall_munlockall),
#endif
#if defined(HAVE_SYSCALL_MUNMAP)
	SYSCALL(syscall_munmap),
#endif
#if defined(HAVE_SYSCALL_NAME_TO_HANDLE_AT)
	SYSCALL(syscall_name_to_handle_at),
#endif
#if defined(HAVE_SYSCALL_NANOSLEEP)
	SYSCALL(syscall_nanosleep),
#endif
	/* syscall_nfsservctl, ignored */
#if defined(HAVE_SYSCALL_NICE)
	SYSCALL(syscall_nice),
#endif
#if defined(HAVE_SYSCALL_OPEN)
	SYSCALL(syscall_open),
#endif
#if defined(HAVE_SYSCALL_OPENAT)
	SYSCALL(syscall_openat),
#endif
#if defined(HAVE_OPEN_BY_HANDLE_AT)
	SYSCALL(syscall_open_by_handle_at),
#endif
	/* syscall_open_tree, ignored */
#if defined(HAVE_SYSCALL_PAUSE)
	SYSCALL(syscall_pause),
#endif
	/* syscall_perf_event_open, may clash with --perf, ignore */
#if defined(HAVE_SYSCALL_PERSONALITY)
	SYSCALL(syscall_personality),
#endif
#if defined(HAVE_SYSCALL_PIDFD_OPEN)
	SYSCALL(syscall_pidfd_open),
#endif
#if defined(HAVE_SYSCALL_PIDFD_SEND_SIGNAL)
	SYSCALL(syscall_pidfd_send_signal),
#endif
#if defined(HAVE_SYSCALL_PIPE)
	SYSCALL(syscall_pipe),
#endif
#if defined(HAVE_SYSCALL_PIPE2)
	SYSCALL(syscall_pipe2),
#endif
	/* syscall_pivot_root, ignored for now */
#if defined(HAVE_SYSCALL_PKEY_ALLOC)
	SYSCALL(syscall_pkey_alloc),
#endif
#if defined(HAVE_SYSCALL_PKEY_FREE)
	SYSCALL(syscall_pkey_free),
#endif
#if defined(HAVE_SYSCALL_PKEY_GET)
	SYSCALL(syscall_pkey_get),
#endif
#if defined(HAVE_SYSCALL_PKEY_MPROTECT)
	SYSCALL(syscall_pkey_mprotect),
#endif
#if defined(HAVE_SYSCALL_PKEY_SET)
	SYSCALL(syscall_pkey_set),
#endif
#if defined(HAVE_SYSCALL_POLL)
	SYSCALL(syscall_poll),
#endif
#if defined(HAVE_SYSCALL_PPOLL)
	SYSCALL(syscall_ppoll),
#endif
#if defined(HAVE_SYSCALL_PRCTL)
	SYSCALL(syscall_prctl),
#endif
#if defined(HAVE_SYSCALL_PREAD)
	SYSCALL(syscall_pread),
#endif
#if defined(HAVE_SYSCALL_PREADV)
	SYSCALL(syscall_preadv),
#endif
#if defined(HAVE_SYSCALL_PREADV2)
	SYSCALL(syscall_preadv2),
#endif
#if defined(HAVE_SYSCALL_PRLIMIT)
	SYSCALL(syscall_prlimit),
#endif
#if defined(HAVE_SYSCALL_PROCESS_VM_READV)
	SYSCALL(syscall_process_vm_readv),
#endif
#if defined(HAVE_SYSCALL_PROCESS_VM_WRITEV)
	SYSCALL(syscall_process_vm_writev),
#endif
#if defined(HAVE_SYSCALL_PSELECT)
	SYSCALL(syscall_pselect),
#endif
	/* syscall_ptrace, */
#if defined(HAVE_SYSCALL_PWRITE)
	SYSCALL(syscall_pwrite),
#endif
#if defined(HAVE_SYSCALL_PWRITEV)
	SYSCALL(syscall_pwritev),
#endif
#if defined(HAVE_SYSCALL_PWRITEV2)
	SYSCALL(syscall_pwritev2),
#endif
#if defined(HAVE_SYSCALL_QUOTACTL)
	SYSCALL(syscall_quotactl),
#endif
#if defined(HAVE_SYSCALL_QUOTACTL_FD)
	SYSCALL(syscall_quotactl_fd),
#endif
#if defined(HAVE_SYSCALL_READ)
	SYSCALL(syscall_read),
#endif
#if defined(HAVE_SYSCALL_READAHEAD)
	SYSCALL(syscall_readahead),
#endif
	/* syscall_readdir, ancient, ignore */
#if defined(HAVE_SYSCALL_READLINK)
	SYSCALL(syscall_readlink),
#endif
#if defined(HAVE_SYSCALL_READLINKAT)
	SYSCALL(syscall_readlinkat),
#endif
#if defined(HAVE_SYSCALL_READV)
	SYSCALL(syscall_readv),
#endif
	/* syscall_reboot, ignore */
#if defined(HAVE_SYSCALL_RECV)
	SYSCALL(syscall_recv),
#endif
#if defined(HAVE_SYSCALL_RECVFROM)
	SYSCALL(syscall_recvfrom),
#endif
#if defined(HAVE_SYSCALL_RECVMMSG)
	SYSCALL(syscall_recvmmsg),
#endif
#if defined(HAVE_SYSCALL_RECVMSG)
	SYSCALL(syscall_recvmsg),
#endif
#if defined(HAVE_SYSCALL_RFORK)
	SYSCALL(syscall_rfork),
#endif
#if defined(HAVE_SYSCALL_REMAP_FILE_PAGES)
	SYSCALL(syscall_remap_file_pages),
#endif
#if defined(HAVE_SYSCALL_REMOVEXATTR)
	SYSCALL(syscall_removexattr),
#endif
#if defined(HAVE_SYSCALL_RENAME)
	SYSCALL(syscall_rename),
#endif
#if defined(HAVE_SYSCALL_RENAMEAT)
	SYSCALL(syscall_renameat),
#endif
#if defined(HAVE_SYSCALL_RENAMEAT2)
	SYSCALL(syscall_renameat2),
#endif
#if defined(HAVE_SYSCALL_REQUEST_KEY)
	SYSCALL(syscall_request_key),
#endif
#if defined(HAVE_SYSCALL_RESTART_SYSCALL)
	SYSCALL(syscall_restart_syscall),
#endif
#if defined(HAVE_SYSCALL_RISCV_FLUSH_ICACHE)
	SYSCALL(syscall_riscv_flush_icache),
#endif
#if defined(HAVE_SYSCALL_RISCV_HWPROBE)
	SYSCALL(syscall_riscv_hwprobe),
#endif
#if defined(HAVE_SYSCALL_RMDIR)
	SYSCALL(syscall_rmdir),
#endif
#if defined(HAVE_SYSCALL_RSEQ)
	SYSCALL(syscall_rseq),
#endif
#if defined(HAVE_SYSCALL_SCHED_GETAFFINITY)
	SYSCALL(syscall_sched_getaffinity),
#endif
#if defined(HAVE_SYSCALL_SCHED_GETATTR)
	SYSCALL(syscall_sched_getattr),
#endif
#if defined(HAVE_SYSCALL_SCHED_GETPARAM)
	SYSCALL(syscall_sched_getparam),
#endif
#if defined(HAVE_SYSCALL_SCHED_GET_PRIORITY_MAX)
	SYSCALL(syscall_sched_get_priority_max),
#endif
#if defined(HAVE_SYSCALL_SCHED_GET_PRIORITY_MIN)
	SYSCALL(syscall_sched_get_priority_min),
#endif
#if defined(HAVE_SYSCALL_SCHED_GETSCHEDULER)
	SYSCALL(syscall_sched_getscheduler),
#endif
#if defined(HAVE_SYSCALL_SCHED_RR_GET_INTERVAL)
	SYSCALL(syscall_sched_rr_get_interval),
#endif
#if defined(HAVE_SYSCALL_SCHED_SETAFFINITY)
	SYSCALL(syscall_sched_setaffinity),
#endif
#if defined(HAVE_SYSCALL_SCHED_SETATTR)
	SYSCALL(syscall_sched_setattr),
#endif
#if defined(HAVE_SYSCALL_SCHED_SETPARAM)
	SYSCALL(syscall_sched_setparam),
#endif
#if defined(HAVE_SYSCALL_SCHED_SETSCHEDULER)
	SYSCALL(syscall_sched_setscheduler),
#endif
#if defined(HAVE_SYSCALL_SCHED_YIELD)
	SYSCALL(syscall_sched_yield),
#endif
#if defined(HAVE_SYSCALL_SECCOMP)
	SYSCALL(syscall_seccomp),
#endif
#if defined(HAVE_SYSCALL_SELECT)
	SYSCALL(syscall_select),
#endif
#if defined(HAVE_SYSCALL_SEMCTL)
	SYSCALL(syscall_semctl),
#endif
#if defined(HAVE_SYSCALL_SEMGET)
	SYSCALL(syscall_semget),
#endif
#if defined(HAVE_SYSCALL_SEMOP)
	SYSCALL(syscall_semop),
#endif
#if defined(HAVE_SYSCALL_SEMTIMEDOP)
	SYSCALL(syscall_semtimedop),
#endif
#if defined(HAVE_SYSCALL_SENDFILE)
	SYSCALL(syscall_sendfile),
#endif
#if defined(HAVE_SYSCALL_SEND)
	SYSCALL(syscall_send),
#endif
#if defined(HAVE_SYSCALL_SENDMMSG)
	SYSCALL(syscall_sendmmsg),
#endif
#if defined(HAVE_SYSCALL_SENDMSG)
	SYSCALL(syscall_sendmsg),
#endif
#if defined(HAVE_SYSCALL_SENDTO)
	SYSCALL(syscall_sendto),
#endif
	/* syscall_setdomainname, */
	/* syscall_setfsgid, */
	/* syscall_setfsuid, */
#if defined(HAVE_SYSCALL_SETGID)
	SYSCALL(syscall_setgid),
#endif
	/* syscall_setgroups, */
	/* syscall_sethostname, */
#if defined(HAVE_SYSCALL_SETITIMER)
	SYSCALL(syscall_setitimer),
#endif
#if defined(HAVE_SYSCALL_SET_MEMPOLICY)
	SYSCALL(syscall_set_mempolicy),
#endif
#if defined(HAVE_SYSCALL_SETPGID)
	SYSCALL(syscall_setpgid),
#endif
#if defined(HAVE_SYSCALL_SETPRIORITY)
	SYSCALL(syscall_setpriority),
#endif
#if defined(HAVE_SYSCALL_SETREGID)
	SYSCALL(syscall_setregid),
#endif
#if defined(HAVE_SYSCALL_SETRESGID)
	SYSCALL(syscall_setresgid),
#endif
#if defined(HAVE_SYSCALL_SETRESUID)
	SYSCALL(syscall_setresuid),
#endif
#if defined(HAVE_SYSCALL_SETREUID)
	SYSCALL(syscall_setreuid),
#endif
#if defined(HAVE_SYSCALL_SETRLIMIT)
	SYSCALL(syscall_setrlimit),
#endif
#if defined(HAVE_SYSCALL_SET_ROBUST_LIST)
	SYSCALL(syscall_set_robust_list),
#endif
#if defined(HAVE_SYSCALL_SETSID)
	SYSCALL(syscall_setsid),
#endif
#if defined(HAVE_SYSCALL_SETSOCKOPT)
	SYSCALL(syscall_setsockopt),
#endif
	/* syscall_set_thread_area, ignore */
	/* syscall_set_tid_address, ignore */
	/* syscall_settimeofday, ignore, don't modify time of day */
#if defined(HAVE_SYSCALL_SETUID)
	SYSCALL(syscall_setuid),
#endif
#if defined(HAVE_SYSCALL_SETXATTR)
	SYSCALL(syscall_setxattr),
#endif
#if defined(HAVE_SYSCALL_SHMAT)
	SYSCALL(syscall_shmat),
#endif
#if defined(HAVE_SYSCALL_SHMCTL)
	SYSCALL(syscall_shmctl),
#endif
#if defined(HAVE_SYSCALL_SHMDT)
	SYSCALL(syscall_shmdt),
#endif
#if defined(HAVE_SYSCALL_SHMGET)
	SYSCALL(syscall_shmget),
#endif
#if defined(HAVE_SYSCALL_SHUTDOWN)
	SYSCALL(syscall_shutdown),
#endif
#if defined(HAVE_SYSCALL_SIGACTION)
	SYSCALL(syscall_sigaction),
#endif
#if defined(HAVE_SYSCALL_SIGALTSTACK)
	SYSCALL(syscall_sigaltstack),
#endif
#if defined(HAVE_SYSCALL_SIGNAL)
	SYSCALL(syscall_signal),
#endif
#if defined(HAVE_SYSCALL_SIGNALFD)
	SYSCALL(syscall_signalfd),
#endif
#if defined(HAVE_SYSCALL_SIGPENDING)
	SYSCALL(syscall_sigpending),
#endif
#if defined(HAVE_SYSCALL_SIGPROCMASK)
	SYSCALL(syscall_sigprocmask),
#endif
#if defined(HAVE_SYSCALL_SIGRETURN)
	SYSCALL(syscall_sigreturn),
#endif
#if defined(HAVE_SYSCALL_SIGSUSPEND)
	SYSCALL(syscall_sigsuspend),
#endif
#if defined(HAVE_SYSCALL_SOCKET)
	SYSCALL(syscall_socket),
#endif
#if defined(HAVE_SYSCALL_SOCKETPAIR)
	SYSCALL(syscall_socketpair),
#endif
#if defined(HAVE_SYSCALL_SPLICE)
	SYSCALL(syscall_splice),
#endif
#if defined(HAVE_SYSCALL_STAT)
	SYSCALL(syscall_stat),
#endif
#if defined(HAVE_SYSCALL_STATFS)
	SYSCALL(syscall_statfs),
#endif
#if defined(HAVE_SYSCALL_STATX)
	SYSCALL(syscall_statx),
#endif
	/* syscall_swapoff, */
	/* syscall_swapon, */
#if defined(HAVE_SYSCALL_SYMLINK)
	SYSCALL(syscall_symlink),
#endif
#if defined(HAVE_SYSCALL_SYMLINKAT)
	SYSCALL(syscall_symlinkat),
#endif
#if defined(HAVE_SYSCALL_SYNC)
	SYSCALL(syscall_sync),
#endif
#if defined(HAVE_SYSCALL_SYNC_FILE_RANGE)
	SYSCALL(syscall_sync_file_range),
#endif
#if defined(HAVE_SYSCALL_SYNCFS)
	SYSCALL(syscall_syncfs),
#endif
	/* SYSCALL(syscall_sysctl), deprecated */
	/* SYSCALL(syscall_sysfs), obsolete */
#if defined(HAVE_SYSCALL_SYSINFO)
	SYSCALL(syscall_sysinfo),
#endif
#if defined(HAVE_SYSCALL_SYSLOG)
	SYSCALL(syscall_syslog),
#endif
#if defined(HAVE_SYSCALL_TEE)
	SYSCALL(syscall_tee),
#endif
	/* syscall_tgkill, */
#if defined(HAVE_SYSCALL_TIME)
	SYSCALL(syscall_time),
#endif
#if defined(HAVE_SYSCALL_TIMER_CREATE)
	SYSCALL(syscall_timer_create),
#endif
#if defined(HAVE_SYSCALL_TIMER_DELETE)
	SYSCALL(syscall_timer_delete),
#endif
#if defined(HAVE_SYSCALL_TIMERFD_CREATE)
	SYSCALL(syscall_timerfd_create),
#endif
#if defined(HAVE_SYSCALL_TIMERFD_GETTIME)
	SYSCALL(syscall_timerfd_gettime),
#endif
#if defined(HAVE_SYSCALL_TIMERFD_SETTIME)
	SYSCALL(syscall_timerfd_settime),
#endif
#if defined(HAVE_SYSCALL_TIMER_GETOVERRUN)
	SYSCALL(syscall_timer_getoverrun),
#endif
#if defined(HAVE_SYSCALL_TIMER_GETTIME)
	SYSCALL(syscall_timer_gettime),
#endif
#if defined(HAVE_SYSCALL_TIMER_SETTIME)
	SYSCALL(syscall_timer_settime),
#endif
#if defined(HAVE_SYSCALL_TIMES)
	SYSCALL(syscall_times),
#endif
	/* syscall_tkill, obsolete */
#if defined(HAVE_SYSCALL_TRUNCATE)
	SYSCALL(syscall_truncate),
#endif
#if defined(HAVE_SYSCALL_UMASK)
	SYSCALL(syscall_umask),
#endif
	/* syscall_umount, ignored */
	/* syscall_umount2, ignored */
#if defined(HAVE_SYSCALL_UNAME)
	SYSCALL(syscall_uname),
#endif
#if defined(HAVE_SYSCALL_UNLINK)
	SYSCALL(syscall_unlink),
#endif
#if defined(HAVE_SYSCALL_UNLINKAT)
	SYSCALL(syscall_unlinkat),
#endif
#if defined(HAVE_SYSCALL_UNSHARE)
	SYSCALL(syscall_unshare),
#endif
#if defined(HAVE_SYSCALL_USERFAULTFD)
	SYSCALL(syscall_userfaultfd),
#endif
#if defined(HAVE_SYSCALL_UTIME)
	SYSCALL(syscall_utime),
#endif
#if defined(HAVE_SYSCALL_UTIMENSAT)
	SYSCALL(syscall_utimensat),
#endif
#if defined(HAVE_SYSCALL_UTIMES)
	SYSCALL(syscall_utimes),
#endif
#if defined(HAVE_SYSCALL_VFORK)
	SYSCALL(syscall_vfork),
#endif
	/* syscall_vhangup, */
	/* syscall_vm86, x86 32 bit only, ignore */
#if defined(HAVE_SYSCALL_VMSPLICE)
	SYSCALL(syscall_vmsplice),
#endif
#if defined(HAVE_SYSCALL_WAIT)
	SYSCALL(syscall_wait),
#endif
#if defined(HAVE_SYSCALL_WAIT3)
	SYSCALL(syscall_wait3),
#endif
#if defined(HAVE_SYSCALL_WAIT4)
	SYSCALL(syscall_wait4),
#endif
#if defined(HAVE_SYSCALL_WAITID)
	SYSCALL(syscall_waitid),
#endif
#if defined(HAVE_SYSCALL_WAITPID)
	SYSCALL(syscall_waitpid),
#endif
#if defined(HAVE_SYSCALL_WRITE)
	SYSCALL(syscall_write),
#endif
#if defined(HAVE_SYSCALL_WRITEV)
	SYSCALL(syscall_writev),
#endif
};

#define STRESS_SYSCALLS_MAX	(SIZEOF_ARRAY(syscalls))

static syscall_stats_t syscall_stats[STRESS_SYSCALLS_MAX];	/* stats */
static size_t stress_syscall_index[STRESS_SYSCALLS_MAX];	/* shuffle index */

/*
 *  stress_syscall_reset_index()
 *	reset shuffle index
 */
static void stress_syscall_reset_index(void)
{
	register size_t i;

	for (i = 0; i < STRESS_SYSCALLS_MAX; i++)
		stress_syscall_index[i] = i;
}

/*
 *  stress_syscall_reset_ignore()
 *	reset ignore flag
 */
static void stress_syscall_reset_ignore(void)
{
	register size_t i;

	for (i = 0; i < STRESS_SYSCALLS_MAX; i++)
		syscall_stats[i].ignore = false;
}

/*
 *  stress_syscall_shuffle_calls()
 *	shuffle the shuffle index to mix up the order
 *	of system calls
 */
static void stress_syscall_shuffle_calls(void)
{
	register size_t i;

	for (i = 0; i < STRESS_SYSCALLS_MAX; i++) {
		register size_t j, tmp;

		j = stress_mwc16modn(STRESS_SYSCALLS_MAX);
		tmp = stress_syscall_index[i];
		stress_syscall_index[i] = stress_syscall_index[j];
		stress_syscall_index[j] = tmp;
	}
}

/*
 *  stress_syscall_rank_calls_by_geomean()
 *	ignore system calls slower than the geomean of the timings
 *	scaled by scale.
 */
static void stress_syscall_rank_calls_by_geomean(const double scale)
{
	register size_t i;
	size_t n;
	double geomean;
	double mant = 1.0;	/* geomean mantissa */
	int64_t expon = 0;	/* goemean exponent */

	stress_syscall_reset_index();

	for (n = 0, i = 0; i < STRESS_SYSCALLS_MAX; i++) {
		const syscall_stats_t *ss = &syscall_stats[i];
		const uint64_t d = ss->max_test_duration;

		if (ss->succeed && (d > 0)) {
			int e;
			const double f = frexp((double)d, &e);

			mant *= f;
			expon += e;
			n++;
		}
	}
	if (n) {
		const double inverse_n = 1.0 / (double)n;

		geomean = pow(mant, inverse_n) *
			  pow(2.0, (double)expon * inverse_n);
		geomean *= scale;
	} else {
		geomean = SYSCALL_DAY_NS;
	}

	for (n = 0, i = 0; i < STRESS_SYSCALLS_MAX; i++) {
		syscall_stats_t *ss = &syscall_stats[i];
		bool ignore = false;

		if (ss->succeed) {
			if (ss->max_test_duration > geomean)
				ignore = true;
		} else {
			ss->ignore = true;
		}
		n += ignore ? 0 : 1;
	}
}

static inline int cmp_syscall_time(const void *p1, const void *p2)
{
	register const size_t i1 = *(const size_t *)p1;
	register const size_t i2 = *(const size_t *)p2;

	return syscall_stats[i1].average_duration > syscall_stats[i2].average_duration;
}

/*
 *  stress_syscall_report_syscall_top10()
 *	report the top 10 fastest system calls sorted by average syscall
 *	duration in nanoseconds.
 */
static void stress_syscall_report_syscall_top10(stress_args_t *args)
{
	size_t i, n, sort_index[STRESS_SYSCALLS_MAX], syscall_top = 10;

	(void)stress_get_setting("syscall-top", &syscall_top);

	for (n = 0, i = 0; (i < STRESS_SYSCALLS_MAX); i++) {
		if (syscall_stats[i].succeed)
			n++;
	}
	if ((syscall_top == 0) || (syscall_top > n))
		syscall_top = n;

	for (i = 0; i < STRESS_SYSCALLS_MAX; i++) {
		syscall_stats_t *ss = &syscall_stats[i];

		sort_index[i] = i;
		if (ss->succeed) {
			ss->average_duration = (ss->count > 0) ?
				ss->total_duration / (double)ss->count : SYSCALL_DAY_NS;
		} else {
			ss->average_duration = SYSCALL_DAY_NS;
		}
	}
	syscall_shellsort_size_t(sort_index, STRESS_SYSCALLS_MAX, cmp_syscall_time);

	pr_block_begin();
	pr_inf("%s: Top %zu fastest system calls (timings in nanosecs):\n",
		args->name, syscall_top);
	pr_inf("%s: %25s %10s %10s %10s\n", args->name,
		"System Call", "Avg (ns)", "Min (ns)", "Max (ns)");
	for (i = 0; i < syscall_top; i++) {
		const size_t j = sort_index[i];
		syscall_stats_t *ss = &syscall_stats[j];

		if (ss->succeed) {
			pr_inf("%s: %25s %10.1f %10" PRIu64 " %10" PRIu64 "\n",
				args->name,
				syscalls[j].name,
				ss->total_duration / (double)ss->count,
				ss->min_duration,
				ss->max_duration);
		}
	}
	pr_block_end();
}

static int cmp_test_duration(const void *p1, const void *p2)
{
	const size_t i1 = *(const size_t *)p1;
	const size_t i2 = *(const size_t *)p2;

	const uint64_t v1 = syscall_stats[i1].max_test_duration;
	const uint64_t v2 = syscall_stats[i2].max_test_duration;

	return v1 > v2;
}

/*
 *  stress_syscall_rank_calls_by_sort()
 *	rank system calls in order of maximum test run duration
 *	(and NOT by system call run time). We want to get the fastest
 *	run times so we can max out the system call rate.
 */
static void stress_syscall_rank_calls_by_sort(const int percent)
{
	size_t i, n, max, sort_index[STRESS_SYSCALLS_MAX];

	stress_syscall_reset_index();

	for (i = 0; i < STRESS_SYSCALLS_MAX; i++) {
		sort_index[i] = i;
	}

	syscall_shellsort_size_t(sort_index, STRESS_SYSCALLS_MAX, cmp_test_duration);
	max = (double)STRESS_SYSCALLS_MAX * ((double)percent / 100.0);
	for (n = 0, i = 0; (n < max) && (i < STRESS_SYSCALLS_MAX); i++) {
		syscall_stats_t *ss = &syscall_stats[sort_index[i]];

		if (ss->succeed) {
			ss->ignore = false;
			n++;
		} else {
			ss->ignore = true;
		}
	}
	for (; i < STRESS_SYSCALLS_MAX; i++) {
		syscall_stats_t *ss = &syscall_stats[sort_index[i]];

		ss->ignore = true;
	}
}

/*
 *  stress_syscall_rank_calls()
 *	rank system calls using the specified ranking method. We
 * 	aim to get a set of system calls to run that will be the
 *	fastest (or slowest) in test run time rather than in system
 *	call time to try and maximize (or minimize) the system call
 *	throughput rate.
 */
static void stress_syscall_rank_calls(const int syscall_method)
{
	size_t i;
	switch (syscall_method) {
	default:
	case SYSCALL_METHOD_ALL:
		for (i = 0; i < STRESS_SYSCALLS_MAX; i++)
			syscall_stats[i].ignore = false;
		break;
	case SYSCALL_METHOD_FAST10:
		stress_syscall_rank_calls_by_sort(10);
		break;
	case SYSCALL_METHOD_FAST25:
		stress_syscall_rank_calls_by_sort(25);
		break;
	case SYSCALL_METHOD_FAST50:
		stress_syscall_rank_calls_by_sort(50);
		break;
	case SYSCALL_METHOD_FAST75:
		stress_syscall_rank_calls_by_sort(75);
		break;
	case SYSCALL_METHOD_FAST90:
		stress_syscall_rank_calls_by_sort(90);
		break;
	case SYSCALL_METHOD_GEOMEAN1:
		stress_syscall_rank_calls_by_geomean(1.0);
		break;
	case SYSCALL_METHOD_GEOMEAN2:
		stress_syscall_rank_calls_by_geomean(2.0);
		break;
	case SYSCALL_METHOD_GEOMEAN3:
		stress_syscall_rank_calls_by_geomean(3.0);
		break;
	}
	for (i = 0; i < STRESS_SYSCALLS_MAX; i++)
		syscall_stats[i].max_test_duration = 0;
}

/*
 *  stress_syscall_benchmark_calls()
 *	benchmark the system calls that are not marked to be ignored
 */
static void stress_syscall_benchmark_calls(stress_args_t *args)
{
	size_t i;

	for (i = 0; i < STRESS_SYSCALLS_MAX; i++) {
		int ret;
		uint64_t test_t1, test_t2;
		uint64_t d;
		size_t j = stress_syscall_index[i];
		syscall_stats_t *ss = &syscall_stats[j];

		if (ss->ignore)
			continue;	/* Skip ignored tests */

		t1 = ~0ULL;
		t2 = ~0ULL;
		errno = 0;

		/* Do the system call test */
		test_t1 = syscall_time_now();
		ret = syscalls[j].syscall();
		ss->syscall_errno = syscall_errno;
		test_t2 = syscall_time_now();

		/* Test duration */
		d = test_t2 - test_t1;
		if (ss->max_test_duration < d)
			ss->max_test_duration = d;

		if ((ret < 0) && (syscall_errno == EINTR)) {
			ret = 0;
			ss->succeed = true;
		}

		/* System call duration */
		d = t2 - t1;
		if ((d > 0) && (ret >= 0) && (t1 != ~0ULL) && (t2 != ~0ULL)) {
			if (ss->min_duration > d)
				ss->min_duration = d;
			if (ss->max_duration < d)
				ss->max_duration = d;
			ss->total_duration += (double)d;
			ss->succeed = true;
			ss->count++;
		}
		stress_bogo_inc(args);
	}
}

/*
 *  stress_syscall
 *	stress system calls
 */
static int stress_syscall(stress_args_t *args)
{
	int ret, rc = EXIT_NO_RESOURCE;
	size_t exercised = 0;
	size_t i;
	int syscall_method = SYSCALL_METHOD_FAST75;
	char exec_path[PATH_MAX];
	const uint32_t rnd_filenum = stress_mwc32();

	(void)stress_get_setting("syscall-method", &syscall_method);

	if (stress_instance_zero(args)) {
		for (i = 0; i < SIZEOF_ARRAY(syscall_methods); i++) {
			if (syscall_method == syscall_methods[i].method) {
				pr_inf("%s: using method '%s'\n", args->name, syscall_methods[i].opt);
				break;
			}
		}
	}

	syscall_pid = getpid();
	syscall_uid = getuid();
	syscall_gid = getgid();
	syscall_sid = getsid(syscall_pid);
	syscall_umask_mask = umask(0);
	syscall_exec_prog = stress_get_proc_self_exe(exec_path, sizeof(exec_path));

	if (stress_sighandler(args->name, SIGUSR1, syscall_sigusr1_handler, NULL) < 0)
		return EXIT_NO_RESOURCE;
#if defined(SIGXFSZ)
	if (stress_sighandler(args->name, SIGXFSZ, syscall_sigignore_handler, NULL) < 0)
		return EXIT_NO_RESOURCE;
#endif

	syscall_page_size = args->page_size;
	syscall_2_pages_size = args->page_size * 2;
	if (!getcwd(syscall_cwd, sizeof(syscall_cwd))) {
		pr_inf_skip("%s: failed to get current working directory, skipping stressor\n", args->name);
		return EXIT_NO_RESOURCE;
	}

#if defined(O_DIRECTORY)
	syscall_dir_fd = open(stress_get_temp_path(), O_DIRECTORY | O_RDONLY);
#else
	syscall_dir_fd = -1;
#endif

	ret = stress_temp_dir_mk_args(args);
	if (ret < 0) {
		rc = stress_exit_status((int)-ret);
		goto err_close_dir_fd;
	}

	(void)stress_temp_filename_args(args,
		syscall_filename, sizeof(syscall_filename), rnd_filenum);
	(void)stress_temp_filename_args(args,
		syscall_tmp_filename, sizeof(syscall_tmp_filename), rnd_filenum + 1);
	(void)stress_temp_filename_args(args,
		syscall_symlink_filename, sizeof(syscall_symlink_filename), rnd_filenum + 2);

	syscall_2_pages = mmap(NULL, args->page_size * 2, PROT_READ | PROT_WRITE,
				MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
	if (syscall_2_pages == MAP_FAILED) {
		pr_inf_skip("%s: failed to mmap %zu bytes%s, errno=%d (%s), "
			"skipping stressor\n", args->name, args->page_size * 2,
			stress_get_memfree_str(), errno, strerror(errno));
		goto err_rmdir;
	}
	stress_uint8rnd4(syscall_2_pages, syscall_2_pages_size);

	syscall_shared_info = (syscall_shared_info_t *)mmap(NULL,
				sizeof(*syscall_shared_info),
				PROT_READ | PROT_WRITE,
				MAP_ANONYMOUS | MAP_SHARED, -1, 0);
	if (syscall_shared_info == MAP_FAILED) {
		pr_inf_skip("%s: failed to mmap %zu bytes%s, errno=%d (%s), "
			"skipping stressor\n", args->name, sizeof(*syscall_shared_info),
			stress_get_memfree_str(), errno, strerror(errno));
		goto err_unmap_syscall_page;
	}

	syscall_fd = open(syscall_filename, O_RDWR | O_CREAT, S_IRUSR | S_IWUSR);
	if (syscall_fd < 0) {
		pr_inf_skip("%s: cannot create file %s, errno=%d (%s), skipping stressor\n",
			args->name, syscall_filename, errno, strerror(errno));
		goto err_unmap_syscall_shared_info;
	}
	VOID_RET(ssize_t, write(syscall_fd, syscall_2_pages, syscall_2_pages_size));

	if (symlink(syscall_filename, syscall_symlink_filename) < 0)
		*syscall_symlink_filename = '\0';

	for (i = 0; i < STRESS_SYSCALLS_MAX; i++) {
		syscall_stats_t *ss = &syscall_stats[i];

		ss->total_duration = 0.0;
		ss->count = 0ULL;
		ss->min_duration = ~0ULL;
		ss->max_duration = 0ULL;
		ss->max_test_duration = 0ULL;
		ss->succeed = false;
		ss->ignore = false;
	}

	syscall_brk_addr = shim_sbrk(0);

	stress_set_proc_state(args->name, STRESS_STATE_SYNC_WAIT);
	stress_sync_start_wait(args);
	stress_set_proc_state(args->name, STRESS_STATE_RUN);

	/*
	 *  First benchmark all system calls, find the ones
	 *  that can be run without error, and rank them by
	 *  the syscall-method option in test run time order
	 */
	stress_syscall_reset_ignore();
	stress_syscall_reset_index();
	stress_syscall_benchmark_calls(args);
	stress_syscall_rank_calls(syscall_method);

	/*
	 *  Now benchmark and shuffle the desired system calls
	 *  We need to check for time out because timers may share the
 	 *  alarm() timer so we can't rely on this on some systems.
	 */
	do {
		stress_syscall_benchmark_calls(args);
		stress_syscall_shuffle_calls();
	} while (stress_continue(args) && (stress_time_now() < args->time_end));

	for (i = 0; i < STRESS_SYSCALLS_MAX; i++) {
		const syscall_stats_t *ss = &syscall_stats[i];

		if (ss->ignore)
			continue;
		if (ss->total_duration > 0.0)
			exercised++;
	}

	if (stress_instance_zero(args)) {
		pr_inf("%s: %zd system call tests, %zd (%.1f%%) fastest non-failing tests fully exercised\n",
			args->name, STRESS_SYSCALLS_MAX, exercised,
			(double)exercised * 100.0 / (double)STRESS_SYSCALLS_MAX);
		stress_syscall_report_syscall_top10(args);
	}

	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);
	rc = EXIT_SUCCESS;

	if (syscall_mmap_page != MAP_FAILED)
		(void)munmap(syscall_mmap_page, syscall_page_size);
	(void)close(syscall_fd);
	if (*syscall_symlink_filename)
		(void)shim_unlink(syscall_symlink_filename);
	(void)shim_unlink(syscall_tmp_filename);
	(void)shim_unlink(syscall_filename);

err_unmap_syscall_shared_info:
	(void)munmap((void *)syscall_shared_info, sizeof(*syscall_shared_info));
err_unmap_syscall_page:
	(void)munmap(syscall_2_pages, syscall_2_pages_size);
err_rmdir:
	(void)stress_temp_dir_rm_args(args);
err_close_dir_fd:
#if defined(O_DIRECTORY)
	if (syscall_dir_fd >= 0)
		(void)close(syscall_dir_fd);
#endif
	return rc;
}

static const char *stress_syscall_method(const size_t i)
{
	return (i < SIZEOF_ARRAY(syscall_methods)) ? syscall_methods[i].opt : NULL;
}

static const stress_opt_t opts[] = {
	{ OPT_syscall_method, "syscall-method", TYPE_ID_SIZE_T_METHOD, 0, 0, stress_syscall_method },
	{ OPT_syscall_top,    "syscall-top",    TYPE_ID_SIZE_T, 0, 1000, NULL },
	END_OPT,
};

const stressor_info_t stress_syscall_info = {
	.stressor = stress_syscall,
	.classifier = CLASS_OS,
	.opts = opts,
	.help = help
};
