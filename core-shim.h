/*
 * Copyright (C) 2024      Colin Ian King
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
#ifndef CORE_SHIM_H
#define CORE_SHIM_H

#include "core-attribute.h"

#if defined(HAVE_SYS_UIO_H)
#include <sys/uio.h>
#endif

#if defined(HAVE_POLL_H)
#include <poll.h>
#endif

#include <dirent.h>
#include <sched.h>
#include <sys/resource.h>

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

#if defined(HAVE_RLIMIT_RESOURCE_T)
typedef __rlimit_resource_t shim_rlimit_resource_t;
#else
typedef int shim_rlimit_resource_t;
#endif

#if defined(HAVE_PRIORITY_WHICH_T)
typedef __priority_which_t shim_priority_which_t;
#else
typedef int shim_priority_which_t;
#endif

#if defined(HAVE_ITIMER_WHICH_T)
typedef __itimer_which_t shim_itimer_which_t;
#else
typedef int shim_itimer_which_t;
#endif

/* loff_t and off64_t porting shims */
#if defined(HAVE_LOFF_T)
typedef	loff_t		shim_loff_t;
#elif defined(HAVE_OFF_T)
typedef	off_t		shim_loff_t;
#else
typedef long int	shim_loff_t;
#endif

#if defined(HAVE_OFF64_T)
typedef off64_t		shim_off64_t;
#else
typedef uint64_t	shim_off64_t;
#endif

/* Should be defined by POSIX, but add shim */
#if defined(DT_BLK)
#define SHIM_DT_BLK	(DT_BLK)
#else
#define SHIM_DT_BLK	(6)
#endif

#if defined(DT_CHR)
#define SHIM_DT_CHR	(DT_CHR)
#else
#define SHIM_DT_CHR	(2)
#endif

#if defined(DT_DIR)
#define SHIM_DT_DIR	(DT_DIR)
#else
#define SHIM_DT_DIR	(4)
#endif

#if defined(DT_FIFO)
#define SHIM_DT_FIFO	(DT_FIFO)
#else
#define SHIM_DT_FIFO	(1)
#endif

#if defined(DT_LNK)
#define SHIM_DT_LNK	(DT_LNK)
#else
#define SHIM_DT_LNK	(10)
#endif

#if defined(DT_REG)
#define SHIM_DT_REG	(DT_REG)
#else
#define SHIM_DT_REG	(8)
#endif

#if defined(DT_SOCK)
#define SHIM_DT_SOCK	(DT_SOCK)
#else
#define SHIM_DT_SOCK	(12)
#endif

#if defined(DT_UNKNOWN)
#define SHIM_DT_UNKNOWN	(DT_UNKNOWN)
#else
#define SHIM_DT_UNKNOWN	(0)
#endif

/* clone3 clone args */
struct shim_clone_args {
	uint64_t flags;		/* Flags bit mask */
	uint64_t pidfd;		/* (pid_t *) PID fd */
	uint64_t child_tid;	/* (pid_t *) child TID */
	uint64_t parent_tid;	/* (pid_t *) parent TID */
	uint64_t exit_signal;	/* exit signal */
	uint64_t stack;		/* lowest address of stack */
	uint64_t stack_size;	/* size of stack */
	uint64_t tls;		/* tls address */
};

struct shim_getcpu_cache {
        unsigned long int blob[128 / sizeof(long)];
};

/* futex2 waitv shim */
struct shim_futex_waitv {
	uint64_t val;
	uint64_t uaddr;
	uint32_t flags;
	uint32_t reserved;
};

/*
 *  shim'd abstracted system or library calls
 *  that have a layer of OS abstraction
 */
struct shim_linux_dirent {
	unsigned long int d_ino;	/* Inode number */
	unsigned long int d_off;	/* Offset to next linux_dirent */
	unsigned short int d_reclen;	/* Length of this linux_dirent */
	char		d_name[];	/* Filename (null-terminated) */
};

#if defined(HAVE_INO64_T)
typedef ino64_t	shim_ino64_t;
#else
typedef int64_t shim_ino64_t;
#endif

/* dirent64 porting shim */
struct shim_linux_dirent64 {
	shim_ino64_t	d_ino;		/* 64-bit inode number */
	shim_off64_t	d_off;		/* 64-bit offset to next structure */
	unsigned short int d_reclen;	/* Size of this dirent */
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

/* shim'd STATX flags */
#if defined(STATX_TYPE)
#define SHIM_STATX_TYPE			(STATX_TYPE)
#else
#define SHIM_STATX_TYPE			(0x00000001U)
#endif
#if defined(STATX_MODE)
#define SHIM_STATX_MODE			(STATX_MODE)
#else
#define SHIM_STATX_MODE			(0x00000002U)
#endif
#if defined(STATX_NLINK)
#define SHIM_STATX_NLINK		(STATX_NLINK)
#else
#define SHIM_STATX_NLINK		(0x00000004U)
#endif
#if defined(STATX_UID)
#define SHIM_STATX_UID			(STATX_UID)
#else
#define SHIM_STATX_UID			(0x00000008U)
#endif
#if defined(STATX_GID)
#define SHIM_STATX_GID			(STATX_GID)
#else
#define SHIM_STATX_GID			(0x00000010U)
#endif
#if defined(STATX_ATIME)
#define SHIM_STATX_ATIME		(STATX_ATIME)
#else
#define SHIM_STATX_ATIME		(0x00000020U)
#endif
#if defined(STATX_MTIME)
#define SHIM_STATX_MTIME		(STATX_MTIME)
#else
#define SHIM_STATX_MTIME		(0x00000040U)
#endif
#if defined(STATX_CTIME)
#define SHIM_STATX_CTIME		(STATX_CTIME)
#else
#define SHIM_STATX_CTIME		(0x00000080U)
#endif
#if defined(STATX_INO)
#define SHIM_STATX_INO			(STATX_INO)
#else
#define SHIM_STATX_INO			(0x00000100U)
#endif
#if defined(STATX_SIZE)
#define SHIM_STATX_SIZE			(STATX_SIZE)
#else
#define SHIM_STATX_SIZE			(0x00000200U)
#endif
#if defined(STATX_BLOCKS)
#define SHIM_STATX_BLOCKS		(STATX_BLOCKS)
#else
#define SHIM_STATX_BLOCKS		(0x00000400U)
#endif
#if defined(STATX_BASIC_STATS)
#define SHIM_STATX_BASIC_STATS		(STATX_BASIC_STATS)
#else
#define SHIM_STATX_BASIC_STATS		(0x000007ffU)
#endif
#if defined(STATX_BTIME)
#define SHIM_STATX_BTIME		(STATX_BTIME)
#else
#define SHIM_STATX_BTIME		(0x00000800U)
#endif
#if defined(STATX_ALL)
#define SHIM_STATX_ALL			(STATX_ALL)
#else
#define SHIM_STATX_ALL			(0x00000fffU)
#endif

#if defined(HAVE_STATX)
typedef struct statx shim_statx_t;
#else
struct shim_statx_timestamp {
        int64_t	 tv_sec;
        uint32_t tv_nsec;
        int32_t  __reserved;
};

typedef struct shim_statx {
        uint32_t   stx_mask;       /* What results were written [uncond] */
        uint32_t   stx_blksize;    /* Preferred general I/O size [uncond] */
        uint64_t   stx_attributes; /* Flags conveying information about the file [uncond] */
        uint32_t   stx_nlink;      /* Number of hard links */
        uint32_t   stx_uid;        /* User ID of owner */
        uint32_t   stx_gid;        /* Group ID of owner */
        uint16_t   stx_mode;       /* File mode */
        uint16_t   __spare0[1];
        uint64_t   stx_ino;        /* Inode number */
        uint64_t   stx_size;       /* File size */
        uint64_t   stx_blocks;     /* Number of 512-byte blocks allocated */
        uint64_t   stx_attributes_mask; /* Mask to show what's supported in stx_attributes */
        struct shim_statx_timestamp  stx_atime;      /* Last access time */
        struct shim_statx_timestamp  stx_btime;      /* File creation time */
        struct shim_statx_timestamp  stx_ctime;      /* Last attribute change time */
        struct shim_statx_timestamp  stx_mtime;      /* Last data modification time */
        uint32_t   stx_rdev_major; /* Device ID of special file [if bdev/cdev] */
        uint32_t   stx_rdev_minor;
        uint32_t   stx_dev_major;  /* ID of device containing file [uncond] */
        uint32_t   stx_dev_minor;
        uint64_t   stx_mnt_id;
        uint32_t   stx_dio_mem_align;      /* Memory buffer alignment for direct I/O */
        uint32_t   stx_dio_offset_align;   /* File offset alignment for direct I/O */
        uint64_t   stx_subvol;     /* Subvolume identifier */
        uint64_t   __spare3[11];   /* Spare space for future expansion */
} shim_statx_t;
#endif

/* old ustat struct */
struct shim_ustat {
#if defined(HAVE_DADDR_T)
	daddr_t	f_tfree;
#else
	long int f_tfree;
#endif
	ino_t	f_tinode;
	char	f_fname[6];
	char	f_fpack[6];
};

/* waitid/pidfd shims */
#if !defined(P_PIDFD)
#define P_PIDFD		(3)
#endif

/* shim'd timex struct */
#if defined(HAVE_SYS_TIMEX_H) &&	\
    defined(HAVE_TIMEX)
typedef struct timex	shim_timex_t;
#else
typedef struct shim_timex {
	int modes;
	/* other fields we don't care about */
	uint8_t padding[256 - sizeof(int)];
} shim_timex_t;
#endif

#if defined(HAVE_PPOLL)
typedef nfds_t shim_nfds_t;

typedef struct pollfd shim_pollfd_t;
#else
typedef unsigned int shim_nfds_t;

typedef struct shim_pollfd {
	int fd;
	short int events;
	short int revents;
} shim_pollfd_t;
#endif

typedef struct shim_xattr_args {
        uint64_t value ALIGN8;
        uint32_t size;
        uint32_t flags;
} shim_xattr_args;

/*
 *  shim_unconstify_ptr()
 *      some older system calls require non-const void *
 *      or caddr_t args, so we need to unconstify them
 */
static inline void *shim_unconstify_ptr(const void *ptr)
{
	union stress_unconstify {
		const void *cptr;
		void *ptr;
	} su;

	su.cptr = ptr;
	return su.ptr;
}

extern int shim_sched_yield(void);
extern int shim_cacheflush(char *addr, int nbytes, int cache);
extern ssize_t shim_copy_file_range(int fd_in, shim_off64_t *off_in, int fd_out,
	shim_off64_t *off_out, size_t len, unsigned int flags);
extern int shim_posix_fallocate(int fd, off_t offset, off_t len);
extern int shim_fallocate(int fd, int mode, off_t offset, off_t len);
extern int shim_gettid(void);
extern long int shim_getcpu(unsigned int *cpu, unsigned int *node, void *tcache);
extern int shim_getdents(unsigned int fd, struct shim_linux_dirent *dirp,
	unsigned int count);
extern int shim_getdents64(unsigned int fd, struct shim_linux_dirent64 *dirp,
	unsigned int count);
extern int shim_getrandom(void *buff, size_t buflen, unsigned int flags);
extern void shim_flush_icache(void *begin, void *end);
extern long int shim_kcmp(pid_t pid1, pid_t pid2, int type, unsigned long int idx1,
	unsigned long int idx2);
extern int shim_klogctl(int type, char *bufp, int len);
extern int shim_membarrier(int cmd, int flags, int cpu_id);
extern int shim_memfd_create(const char *name, unsigned int flags);
extern int shim_get_mempolicy(int *mode, unsigned long int *nodemask,
	unsigned long int maxnode, void *addr, unsigned long int flags);
extern int shim_set_mempolicy(int mode, unsigned long int *nodemask,
	unsigned long int maxnode);
extern long int shim_mbind(void *addr, unsigned long int len, int mode,
	const unsigned long int *nodemask, unsigned long int maxnode, unsigned flags);
extern long int shim_migrate_pages(int pid, unsigned long int maxnode,
	const unsigned long int *old_nodes, const unsigned long int *new_nodes);
extern long int shim_move_pages(int pid, unsigned long int count, void **pages,
	const int *nodes, int *status, int flags);
extern int shim_userfaultfd(int flags);
extern int shim_seccomp(unsigned int operation, unsigned int flags, void *args);
extern int shim_unshare(int flags);
extern int shim_sched_getattr(pid_t pid, struct shim_sched_attr *attr,
	unsigned int size, unsigned int flags);
extern int shim_sched_setattr(pid_t pid, struct shim_sched_attr *attr,
	unsigned int flags);
extern int shim_mlock(const void *addr, size_t len);
extern int shim_munlock(const void *addr, size_t len);
extern int shim_mlock2(const void *addr, size_t len, int flags);
extern int shim_mlockall(int flags);
extern int shim_munlockall(void);
extern int shim_nanosleep_uint64(uint64_t nsec);
extern int shim_usleep(uint64_t usec);
extern int shim_usleep_interruptible(uint64_t usec);
extern char *shim_getlogin(void);
extern int shim_msync(void *addr, size_t length, int flags);
extern int shim_sysfs(int option, ...);
extern int shim_madvise(void *addr, size_t length, int advice);
extern int shim_mincore(void *addr, size_t length, unsigned char *vec);
extern int shim_statx(int dfd, const char *filename, int flags,
	unsigned int mask, shim_statx_t *buffer);
extern int shim_futex_wake(const void *futex, const int n);
extern int shim_futex_wait(const void *futex, const int val,
	const struct timespec *timeout);
extern int shim_dup3(int oldfd, int newfd, int flags);
extern int shim_sync_file_range(int fd, shim_off64_t offset,
	shim_off64_t nbytes, unsigned int flags);
extern int shim_ioprio_set(int which, int who, int ioprio);
extern int shim_ioprio_get(int which, int who);
extern int shim_brk(void *addr);
extern void *shim_sbrk(intptr_t increment);
extern ssize_t shim_strscpy(char *dst, const char *src, size_t len);
extern size_t shim_strlcat(char *dst, const char *src, size_t len);
extern int shim_fsync(int fd);
extern int shim_fdatasync(int fd);
extern int shim_pkey_alloc(unsigned int flags, unsigned int access_rights);
extern int shim_pkey_free(int pkey);
extern int shim_pkey_mprotect(void *addr, size_t len, int prot, int pkey);
extern int shim_pkey_get(int pkey);
extern int shim_pkey_set(int pkey, unsigned int rights);
extern int shim_execveat(int dir_fd, const char *pathname, char *const argv[],
	char *const envp[], int flags);
extern pid_t shim_waitpid(pid_t pid, int *wstatus, int options);
extern pid_t shim_wait(int *wstatus);
extern pid_t shim_wait3(int *wstatus, int options, struct rusage *rusage);
extern pid_t shim_wait4(pid_t pid, int *wstatus, int options,
	struct rusage *rusage);
extern void shim_exit_group(int status);
extern int shim_pidfd_send_signal(int pidfd, int sig, siginfo_t *info,
	unsigned int flags);
extern int shim_pidfd_open(pid_t pid, unsigned int flags);
extern int shim_pidfd_getfd(int pidfd, int targetfd, unsigned int flags);
extern int shim_fsopen(const char *fsname, unsigned int flags);
extern int shim_fsmount(int fd, unsigned int flags, unsigned int ms_flags);
extern int shim_fsconfig(int fd, unsigned int cmd, const char *key,
	const void *value, int aux);
extern int shim_move_mount(int from_dfd, const char *from_pathname, int to_dfd,
	const char *to_pathname, unsigned int flags);
extern int shim_clone3(struct shim_clone_args *cl_args, size_t size);
extern int shim_ustat(dev_t dev, struct shim_ustat *ubuf);
extern ssize_t shim_getxattr(const char *path, const char *name, void *value,
	size_t size);
extern ssize_t shim_getxattrat(int dfd, const char *path, unsigned int at_flags,
	const char *name, struct shim_xattr_args *args, size_t size);
extern ssize_t shim_listxattr(const char *path, char *list, size_t size);
extern ssize_t shim_listxattrat(int dfd, const char *path, unsigned int at_flags,
	char *list, size_t size);
extern ssize_t shim_flistxattr(int fd, char *list, size_t size);
extern int shim_setxattr(const char *path, const char *name, const void *value,
	size_t size, int flags);
extern int shim_setxattrat(int dfd, const char *path, unsigned int at_flags,
	const char *name, const struct shim_xattr_args *args, size_t size);
extern int shim_fsetxattr(int fd, const char *name, const void *value,
	size_t size, int flags);
extern int shim_lsetxattr(const char *path, const char *name,
	const void *value, size_t size, int flags);
extern ssize_t shim_lgetxattr(const char *path, const char *name, void *value,
	size_t size);
extern ssize_t shim_fgetxattr(int fd, const char *name, void *value,
	size_t size);
extern int shim_removexattr(const char *path, const char *name);
extern int shim_removexattrat(int dfd, const char *path, unsigned int at_flags,
	const char *name);
extern int shim_lremovexattr(const char *path, const char *name);
extern int shim_fremovexattr(int fd, const char *name);
extern ssize_t shim_llistxattr(const char *path, char *list, size_t size);
extern int shim_reboot(int magic, int magic2, int cmd, void *arg);
extern ssize_t shim_process_madvise(int pidfd, const struct iovec *iovec,
	unsigned int long vlen, int advice, unsigned int flags);
extern int shim_clock_getres(clockid_t clk_id, struct timespec *res);
extern int shim_clock_adjtime(clockid_t clk_id, shim_timex_t *buf);
extern int shim_clock_gettime(clockid_t clk_id, struct timespec *tp);
extern int shim_clock_settime(clockid_t clk_id, struct timespec *tp);
extern int shim_nice(int inc);
extern time_t shim_time(time_t *tloc);
extern int shim_gettimeofday(struct timeval *tv, struct timezone *tz);
extern int shim_close_range(unsigned int fd, unsigned int max_fd,
	unsigned int flags);
extern int shim_lookup_dcookie(uint64_t cookie, char *buffer, size_t len);
extern ssize_t shim_readlink(const char *pathname, char *buf, size_t bufsiz);
extern long int shim_sgetmask(void);
extern long int shim_ssetmask(long int newmask);
extern int shim_stime(const time_t *t);
extern int shim_vhangup(void);
extern int shim_arch_prctl(int code, unsigned long int addr);
extern int shim_tgkill(int tgid, int tid, int sig);
extern int shim_tkill(int tid, int sig);
extern int shim_memfd_secret(unsigned long int flags);
extern int shim_getrusage(int who, struct rusage *usage);
extern int shim_quotactl_fd(unsigned int fd, unsigned int cmd, int id,
	void *addr);
extern int shim_modify_ldt(int func, void *ptr, unsigned long int bytecount);
extern int shim_process_mrelease(int pidfd, unsigned int flags);
extern int shim_futex_waitv(struct shim_futex_waitv *waiters,
	unsigned int nr_futexes, unsigned int flags, struct timespec *timeout,
	clockid_t clockid);
extern int shim_force_unlink(const char *pathname);
extern int shim_unlink(const char *pathname);
extern int shim_unlinkat(int dirfd, const char *pathname, int flags);
extern int shim_force_rmdir(const char *pathname);
extern int shim_rmdir(const char *pathname);
extern int shim_getdomainname(char *name, size_t len);
extern int shim_setdomainname(const char *name, size_t len);
extern int shim_setgroups(int size, const gid_t *list);
extern int shim_finit_module(int fd, const char *uargs, int flags);
extern int shim_delete_module(const char *name, unsigned int flags);
extern int shim_raise(int sig);
extern int shim_kill(pid_t pid, int sig);
extern int shim_set_mempolicy_home_node(unsigned long int start, unsigned long int len,
        unsigned long int home_node, unsigned long int flags);
extern int shim_fchmodat(int dfd, const char *filename, mode_t mode,
	unsigned int flags);
extern int shim_fchmodat2(int dfd, const char *filename, mode_t mode,
	unsigned int flags);
extern int shim_fstat(int fd, struct stat *statbuf);
extern int shim_lstat(const char *pathname, struct stat *statbuf);
extern int shim_stat(const char *pathname, struct stat *statbuf);
extern unsigned char shim_dirent_type(const char *path, const struct dirent *d);
extern int shim_mseal(void *addr, size_t len, unsigned long int flags);
extern int shim_ppoll(shim_pollfd_t *fds, shim_nfds_t nfds,
	const struct timespec *tmo_p, const sigset_t *sigmask);

#endif
