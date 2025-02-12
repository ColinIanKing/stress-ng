/*
 * Copyright (C) 2014-2021 Canonical, Ltd.
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
#include "git-commit-id.h"
#include "core-bitops.h"
#include "core-builtin.h"
#include "core-attribute.h"
#include "core-capabilities.h"
#include "core-cpu-cache.h"
#include "core-hash.h"
#include "core-lock.h"
#include "core-numa.h"
#include "core-pthread.h"
#include "core-pragma.h"
#include "core-sort.h"
#include "core-target-clones.h"

#include <ctype.h>
#include <math.h>
#include <sched.h>
#include <stdarg.h>
#include <pwd.h>
#include <sys/ioctl.h>
#include <time.h>

#if defined(HAVE_EXECINFO_H)
#include <execinfo.h>
#endif

#if defined(HAVE_LINUX_FIEMAP_H)
#include <linux/fiemap.h>
#endif

#if defined(HAVE_SYS_AUXV_H)
#include <sys/auxv.h>
#endif

#if defined(HAVE_SYS_CAPABILITY_H)
#include <sys/capability.h>
#endif

#if defined(HAVE_SYS_LOADAVG_H)
#include <sys/loadavg.h>
#endif

#if defined(HAVE_MACH_MACH_H)
#include <mach/mach.h>
#endif

#if defined(HAVE_MACH_VM_STATISTICS_H)
#include <mach/vm_statistics.h>
#endif

#if (defined(__FreeBSD__) || 	\
     defined(__OpenBSD__)) &&	\
     defined(HAVE_SYS_MOUNT_H)
#include <sys/mount.h>
#endif

#if defined(__FreeBSD__) &&	\
    defined(HAVE_SYS_PARAM_H)
#include <sys/param.h>
#endif

#if defined(HAVE_SYS_PRCTL_H)
#include <sys/prctl.h>
#endif

#if defined(HAVE_SYS_PROCCTL_H)
#include <sys/procctl.h>
#endif

#if defined(HAVE_SYS_STATVFS_H)
#include <sys/statvfs.h>
#endif

#if defined(HAVE_SYS_SWAP_H) &&	\
    !defined(__sun__)
#include <sys/swap.h>
#endif

#if defined(HAVE_SYS_SYSCTL_H) &&	\
    !defined(__linux__)
#include <sys/sysctl.h>
#endif

#if defined(HAVE_SYS_UTSNAME_H)
#include <sys/utsname.h>
#endif

#if defined(HAVE_LINUX_FS_H)
#include <linux/fs.h>
#endif

#if defined(HAVE_SYS_VFS_H)
#include <sys/vfs.h>
#endif

#if defined(HAVE_LINUX_MAGIC_H)
#include <linux/magic.h>
#endif

#if defined(HAVE_UVM_UVM_EXTERN_H)
#include <uvm/uvm_extern.h>
#endif

/* prctl(2) timer slack support */
#if defined(HAVE_SYS_PRCTL_H) && \
    defined(HAVE_PRCTL) && \
    defined(PR_SET_TIMERSLACK) && \
    defined(PR_GET_TIMERSLACK)
#define HAVE_PRCTL_TIMER_SLACK
#endif

#if defined(NSIG)
#define STRESS_NSIG	NSIG
#elif defined(_NSIG)
#define STRESS_NSIG	_NSIG
#endif

#if defined(HAVE_COMPILER_TCC) || defined(HAVE_COMPILER_PCC)
int __dso_handle;
#endif

#define MEM_CACHE_SIZE			(2 * MB)
#define PAGE_4K_SHIFT			(12)
#define PAGE_4K				(1 << PAGE_4K_SHIFT)

#define BACKTRACE_BUF_SIZE		(64)

#define STRESS_ABS_MIN_STACK_SIZE	(64 * 1024)

const char ALIGN64 stress_ascii64[64] =
	"0123456789ABCDEFGHIJKLMNOPQRSTUV"
	"WXYZabcdefghijklmnopqrstuvwxyz@!";

const char ALIGN64 stress_ascii32[32] =
	"ABCDEFGHIJKLMNOPQRSTUVWXYZ_+@:#!";

static bool stress_stack_check_flag;

typedef struct {
	const unsigned long int	fs_magic;
	const char *		fs_name;
} stress_fs_name_t;

#if defined(HAVE_LINUX_MAGIC_H) &&	\
    defined(HAVE_SYS_STATFS_H)
static const stress_fs_name_t stress_fs_names[] = {
#if defined(ADFS_SUPER_MAGIC)
	{ ADFS_SUPER_MAGIC,	"adfs" },
#endif
#if defined(AFFS_SUPER_MAGIC)
	{ AFFS_SUPER_MAGIC,	"affs" },
#endif
#if defined(AFS_SUPER_MAGIC)
	{ AFS_SUPER_MAGIC,	"afs" },
#endif
#if defined(AUTOFS_SUPER_MAGIC)
	{ AUTOFS_SUPER_MAGIC,	"autofs" },
#endif
#if defined(CEPH_SUPER_MAGIC)
	{ CEPH_SUPER_MAGIC,	"ceph" },
#endif
#if defined(CODA_SUPER_MAGIC)
	{ CODA_SUPER_MAGIC,	"coda" },
#endif
#if defined(CRAMFS_MAGIC)
	{ CRAMFS_MAGIC,		"cramfs" },
#endif
#if defined(CRAMFS_MAGIC_WEND)
	{ CRAMFS_MAGIC,		"cramfs" },
#endif
#if defined(DEBUGFS_MAGIC)
	{ DEBUGFS_MAGIC,	"debugfs" },
#endif
#if defined(SECURITYFS_MAGIC)
	{ SECURITYFS_MAGIC,	"securityfs" },
#endif
#if defined(SELINUX_MAGIC)
	{ SELINUX_MAGIC,	"selinux" },
#endif
#if defined(SMACK_MAGIC)
	{ SMACK_MAGIC,		"smack" },
#endif
#if defined(RAMFS_MAGIC)
	{ RAMFS_MAGIC,		"ramfs" },
#endif
#if defined(TMPFS_MAGIC)
	{ TMPFS_MAGIC,		"tmpfs" },
#endif
#if defined(HUGETLBFS_MAGIC)
	{ HUGETLBFS_MAGIC,	"hugetlbfs" },
#endif
#if defined(SQUASHFS_MAGIC)
	{ SQUASHFS_MAGIC,	"squashfs" },
#endif
#if defined(ECRYPTFS_SUPER_MAGIC)
	{ ECRYPTFS_SUPER_MAGIC,	"ecryptfs" },
#endif
#if defined(EFS_SUPER_MAGIC)
	{ EFS_SUPER_MAGIC,	"efs" },
#endif
#if defined(EROFS_SUPER_MAGIC_V1)
	{ EROFS_SUPER_MAGIC_V1,	"erofs" },
#endif
#if defined(EXT2_SUPER_MAGIC)
	{ EXT2_SUPER_MAGIC,	"ext2" },
#endif
#if defined(EXT3_SUPER_MAGIC)
	{ EXT3_SUPER_MAGIC,	"ext3" },
#endif
#if defined(XENFS_SUPER_MAGIC)
	{ XENFS_SUPER_MAGIC,	"xenfs" },
#endif
#if defined(EXT4_SUPER_MAGIC)
	{ EXT4_SUPER_MAGIC,	"ext4" },
#endif
#if defined(BTRFS_SUPER_MAGIC)
	{ BTRFS_SUPER_MAGIC,	"btrfs" },
#endif
#if defined(NILFS_SUPER_MAGIC)
	{ NILFS_SUPER_MAGIC,	"nilfs" },
#endif
#if defined(F2FS_SUPER_MAGIC)
	{ F2FS_SUPER_MAGIC,	"f2fs" },
#endif
#if defined(HPFS_SUPER_MAGIC)
	{ HPFS_SUPER_MAGIC,	"hpfs" },
#endif
#if defined(ISOFS_SUPER_MAGIC)
	{ ISOFS_SUPER_MAGIC,	"isofs" },
#endif
#if defined(JFFS2_SUPER_MAGIC)
	{ JFFS2_SUPER_MAGIC,	"jffs2" },
#endif
#if defined(XFS_SUPER_MAGIC)
	{ XFS_SUPER_MAGIC,	"xfs" },
#endif
#if defined(PSTOREFS_MAGIC)
	{ PSTOREFS_MAGIC,	"pstorefs" },
#endif
#if defined(EFIVARFS_MAGIC)
	{ EFIVARFS_MAGIC,	"efivars" },
#endif
#if defined(HOSTFS_SUPER_MAGIC)
	{ HOSTFS_SUPER_MAGIC,	"hostfs" },
#endif
#if defined(OVERLAYFS_SUPER_MAGIC)
	{ OVERLAYFS_SUPER_MAGIC, "overlayfs" },
#endif
#if defined(FUSE_SUPER_MAGIC)
	{ FUSE_SUPER_MAGIC,	"fuse" },
#endif
#if defined(BCACHEFS_STATFS_MAGIC)
	{ BCACHEFS_STATFS_MAGIC, "bcachefs" },
#else
	{ 0xca451a4e,		"bacachefs" },
#endif
#if defined(MINIX_SUPER_MAGIC)
	{ MINIX_SUPER_MAGIC,	"minix" },
#endif
#if defined(MINIX_SUPER_MAGIC2)
	{ MINIX_SUPER_MAGIC2,	"minix" },
#endif
#if defined(MINIX2_SUPER_MAGIC)
	{ MINIX2_SUPER_MAGIC,	"minix2" },
#endif
#if defined(MINIX3_SUPER_MAGIC)
	{ MINIX3_SUPER_MAGIC,	"minix3" },
#endif
#if defined(MSDOS_SUPER_MAGIC)
	{ MSDOS_SUPER_MAGIC,	"msdos" },
#endif
#if defined(EXFAT_SUPER_MAGIC)
	{ EXFAT_SUPER_MAGIC,	"exfat" },
#endif
#if defined(NCP_SUPER_MAGIC)
	{ NCP_SUPER_MAGIC,	"ncp" },
#endif
#if defined(NFS_SUPER_MAGIC)
	{ NFS_SUPER_MAGIC,	"nfs" },
#endif
#if defined(OCFS2_SUPER_MAGIC)
	{ OCFS2_SUPER_MAGIC,	"ocfs2" },
#endif
#if defined(OPENPROM_SUPER_MAGIC)
	{ OPENPROM_SUPER_MAGIC,	"openprom" },
#endif
#if defined(QNX4_SUPER_MAGIC)
	{ QNX4_SUPER_MAGIC,	"qnx4" },
#endif
#if defined(QNX6_SUPER_MAGIC)
	{ QNX6_SUPER_MAGIC,	"qnx6" },
#endif
#if defined(AFS_FS_MAGIC)
	{ AFS_FS_MAGIC,		"afs" },
#endif
#if defined(REISERFS_SUPER_MAGIC)
	{ REISERFS_SUPER_MAGIC,	"reiserfs" },
#endif
#if defined(SMB_SUPER_MAGIC)
	{ SMB_SUPER_MAGIC,	"smb" },
#endif
#if defined(CIFS_SUPER_MAGIC)
	{ CIFS_SUPER_MAGIC,	"cifs" },
#endif
#if defined(SMB2_SUPER_MAGIC)
	{ SMB2_SUPER_MAGIC,	"smb2" },
#endif
#if defined(CGROUP_SUPER_MAGIC)
	{ CGROUP_SUPER_MAGIC,	"cgroup" },
#endif
#if defined(CGROUP2_SUPER_MAGIC)
	{ CGROUP2_SUPER_MAGIC,	"cgroup2" },
#endif
#if defined(RDTGROUP_SUPER_MAGIC)
	{ RDTGROUP_SUPER_MAGIC,	"rdtgroup" },
#endif
#if defined(TRACEFS_MAGIC)
	{ TRACEFS_MAGIC,	"tracefs" },
#endif
#if defined(V9FS_MAGIC)
	{ V9FS_MAGIC,		"v9fs" },
#endif
#if defined(BDEVFS_MAGIC)
	{ BDEVFS_MAGIC,		"bdevfs" },
#endif
#if defined(DAXFS_MAGIC)
	{ DAXFS_MAGIC,		"daxfs" },
#endif
#if defined(BINFMTFS_MAGIC)
	{ BINFMTFS_MAGIC,	"binfmtfs" },
#endif
#if defined(DEVPTS_SUPER_MAGIC)
	{ DEVPTS_SUPER_MAGIC,	"devpts" },
#endif
#if defined(BINDERFS_SUPER_MAGIC)
	{ BINDERFS_SUPER_MAGIC,	"binderfs" },
#endif
#if defined(FUTEXFS_SUPER_MAGIC)
	{ FUTEXFS_SUPER_MAGIC,	"futexfs" },
#endif
#if defined(PIPEFS_MAGIC)
	{ PIPEFS_MAGIC,		"pipefs" },
#endif
#if defined(PROC_SUPER_MAGIC)
	{ PROC_SUPER_MAGIC,	"proc" },
#endif
#if defined(SOCKFS_MAGIC)
	{ SOCKFS_MAGIC,		"sockfs" },
#endif
#if defined(SYSFS_MAGIC)
	{ SYSFS_MAGIC,		"sysfs" },
#endif
#if defined(USBDEVICE_SUPER_MAGIC)
	{ USBDEVICE_SUPER_MAGIC, "usbdev" },
#endif
#if defined(MTD_INODE_FS_MAGIC)
	{ MTD_INODE_FS_MAGIC,	"mtd" },
#endif
#if defined(ANON_INODE_FS_MAGIC)
	{ ANON_INODE_FS_MAGIC,	"anon" },
#endif
#if defined(BTRFS_TEST_MAGIC)
	{ BTRFS_TEST_MAGIC,	"btrfs" },
#endif
#if defined(NSFS_MAGIC)
	{ NSFS_MAGIC,		"nsfs" },
#endif
#if defined(BPF_FS_MAGIC)
	{ BPF_FS_MAGIC,		"bpf_fs" },
#endif
#if defined(AAFS_MAGIC)
	{ AAFS_MAGIC,		"aafs" },
#endif
#if defined(ZONEFS_MAGIC)
	{ ZONEFS_MAGIC,		"zonefs" },
#endif
#if defined(UDF_SUPER_MAGIC)
	{ UDF_SUPER_MAGIC,	"udf" },
#endif
#if defined(DMA_BUF_MAGIC)
	{ DMA_BUF_MAGIC,	"dmabuf" },
#endif
#if defined(DEVMEM_MAGIC)
	{ DEVMEM_MAGIC,		"devmem" },
#endif
#if defined(SECRETMEM_MAGIC)
	{ SECRETMEM_MAGIC,	"secretmem" },
#endif
#if defined(PID_FS_MAGIC)
	{ PID_FS_MAGIC,		"pidfs" },
#endif
#if defined(UBIFS_SUPER_MAGIC)
	{ UBIFS_SUPER_MAGIC,	"ubifs" },
#else
	{ 0x24051905,		"ubifs" },
#endif
	{ 0x1badface,		"bfs" },
#if defined(HFS_SUPER_MAGIC)
	{ HFS_SUPER_MAGIC,	"hfs" },
#else
	{ 0x4244,		"hfs" },
#endif
#if defined(HFSPLUS_SUPER_MAGIC)
	{ HFSPLUS_SUPER_MAGIC,	"hfsplus" },
#else
	{ 0x482b,		"hfsplus" },
#endif
#if defined(JFS_SUPER_MAGIC)
	{ JFS_SUPER_MAGIC,	"jfs" },
#else
	{ 0x3153464a,		"jfs" },
#endif
	{ 0x2fc12fc1,		"zfs" },
	{ 0x53464846,		"wsl" },
};
#endif

typedef struct {
	const int  signum;	/* signal number */
	const char *name;	/* human readable signal name */
} stress_sig_name_t;

#define SIG_NAME(x) { x, #x }

static const stress_sig_name_t sig_names[] = {
#if defined(SIGABRT)
	SIG_NAME(SIGABRT),
#endif
#if defined(SIGALRM)
	SIG_NAME(SIGALRM),
#endif
#if defined(SIGBUS)
	SIG_NAME(SIGBUS),
#endif
#if defined(SIGCHLD)
	SIG_NAME(SIGCHLD),
#endif
#if defined(SIGCLD)
	SIG_NAME(SIGCLD),
#endif
#if defined(SIGCONT)
	SIG_NAME(SIGCONT),
#endif
#if defined(SIGEMT)
	SIG_NAME(SIGEMT),
#endif
#if defined(SIGFPE)
	SIG_NAME(SIGFPE),
#endif
#if defined(SIGHUP)
	SIG_NAME(SIGHUP),
#endif
#if defined(SIGILL)
	SIG_NAME(SIGILL),
#endif
#if defined(SIGINFO)
	SIG_NAME(SIGINFO),
#endif
#if defined(SIGINT)
	SIG_NAME(SIGINT),
#endif
#if defined(SIGIO)
	SIG_NAME(SIGIO),
#endif
#if defined(SIGIOT)
	SIG_NAME(SIGIOT),
#endif
#if defined(SIGKILL)
	SIG_NAME(SIGKILL),
#endif
#if defined(SIGLOST)
	SIG_NAME(SIGLOST),
#endif
#if defined(SIGPIPE)
	SIG_NAME(SIGPIPE),
#endif
#if defined(SIGPOLL)
	SIG_NAME(SIGPOLL),
#endif
#if defined(SIGPROF)
	SIG_NAME(SIGPROF),
#endif
#if defined(SIGPWR)
	SIG_NAME(SIGPWR),
#endif
#if defined(SIGQUIT)
	SIG_NAME(SIGQUIT),
#endif
#if defined(SIGSEGV)
	SIG_NAME(SIGSEGV),
#endif
#if defined(SIGSTKFLT)
	SIG_NAME(SIGSTKFLT),
#endif
#if defined(SIGSTOP)
	SIG_NAME(SIGSTOP),
#endif
#if defined(SIGSYS)
	SIG_NAME(SIGSYS),
#endif
#if defined(SIGTERM)
	SIG_NAME(SIGTERM),
#endif
#if defined(SIGTRAP)
	SIG_NAME(SIGTRAP),
#endif
#if defined(SIGTSTP)
	SIG_NAME(SIGTSTP),
#endif
#if defined(SIGTTIN)
	SIG_NAME(SIGTTIN),
#endif
#if defined(SIGTTOU)
	SIG_NAME(SIGTTOU),
#endif
#if defined(SIGUNUSED)
	SIG_NAME(SIGUNUSED),
#endif
#if defined(SIGURG)
	SIG_NAME(SIGURG),
#endif
#if defined(SIGUSR1)
	SIG_NAME(SIGUSR1),
#endif
#if defined(SIGUSR2)
	SIG_NAME(SIGUSR2),
#endif
#if defined(SIGVTALRM)
	SIG_NAME(SIGVTALRM),
#endif
#if defined(SIGWINCH)
	SIG_NAME(SIGWINCH),
#endif
#if defined(SIGXCPU)
	SIG_NAME(SIGXCPU),
#endif
#if defined(SIGXFSZ)
	SIG_NAME(SIGXFSZ),
#endif
};

static char *stress_temp_path;

/*
 *  stress_temp_path_free()
 *	free and NULLify temporary file path
 */
void stress_temp_path_free(void)
{
	if (stress_temp_path)
		free(stress_temp_path);

	stress_temp_path = NULL;
}

/*
 *  stress_set_temp_path()
 *	set temporary file path, default
 *	is . - current dir
 */
int stress_set_temp_path(const char *path)
{
	static const char *func = "stress_set_temp_path";
	stress_temp_path_free();

	if (!path) {
		(void)fprintf(stderr, "%s: invalid NULL path\n", func);
		return -1;
	}

	stress_temp_path = stress_const_optdup(path);
	if (!stress_temp_path) {
		(void)fprintf(stderr, "%s: aborting: cannot allocate memory for '%s'\n", func, path);
		return -1;
	}
	return 0;
}

/*
 *  stress_get_temp_path()
 *	get temporary file path, return "." if null
 */
const char *stress_get_temp_path(void)
{
	if (!stress_temp_path)
		return ".";
	return stress_temp_path;
}

/*
 *  stress_check_temp_path()
 *	check if temp path is accessible
 */
int stress_check_temp_path(void)
{
	const char *path = stress_get_temp_path();

	if (UNLIKELY(access(path, R_OK | W_OK) < 0)) {
		(void)fprintf(stderr, "aborting: temp-path '%s' must be readable "
			"and writeable\n", path);
		return -1;
	}
	return 0;
}

/*
 *  stress_mk_filename()
 *	generate a full file name from a path and filename
 */
size_t stress_mk_filename(
	char *fullname,
	const size_t fullname_len,
	const char *pathname,
	const char *filename)
{
	/*
	 *  This may not be efficient, but it works. Do not
	 *  be tempted to optimize this, it is not used frequently
	 *  and is not a CPU bottleneck.
	 */
	(void)shim_strscpy(fullname, pathname, fullname_len);
	(void)shim_strlcat(fullname, "/", fullname_len);
	return shim_strlcat(fullname, filename, fullname_len);
}

/*
 *  stress_get_page_size()
 *	get page_size
 */
size_t stress_get_page_size(void)
{
	static size_t page_size = 0;

	/* Use cached size */
	if (LIKELY(page_size > 0))
		return page_size;

#if defined(_SC_PAGESIZE)
	{
		/* Use modern sysconf */
		const long int sz = sysconf(_SC_PAGESIZE);
		if (sz > 0) {
			page_size = (size_t)sz;
			return page_size;
		}
	}
#else
	UNEXPECTED
#endif
#if defined(HAVE_GETPAGESIZE)
	{
		/* Use deprecated getpagesize */
		const long int sz = getpagesize();
		if (sz > 0) {
			page_size = (size_t)sz;
			return page_size;
		}
	}
#endif
	/* Guess */
	page_size = PAGE_4K;
	return page_size;
}

/*
 *  stress_get_processors_online()
 *	get number of processors that are online
 */
int32_t stress_get_processors_online(void)
{
	static int32_t processors_online = 0;

	if (LIKELY(processors_online > 0))
		return processors_online;

#if defined(_SC_NPROCESSORS_ONLN)
	processors_online = (int32_t)sysconf(_SC_NPROCESSORS_ONLN);
	if (processors_online < 0)
		processors_online = 1;
#else
	processors_online = 1;
	UNEXPECTED
#endif
	return processors_online;
}

/*
 *  stress_get_processors_configured()
 *	get number of processors that are configured
 */
int32_t stress_get_processors_configured(void)
{
	static int32_t processors_configured = 0;

	if (LIKELY(processors_configured > 0))
		return processors_configured;

#if defined(_SC_NPROCESSORS_CONF)
	processors_configured = (int32_t)sysconf(_SC_NPROCESSORS_CONF);
	if (processors_configured < 0)
		processors_configured = stress_get_processors_online();
#else
	processors_configured = 1;
	UNEXPECTED
#endif
	return processors_configured;
}

/*
 *  stress_get_ticks_per_second()
 *	get number of ticks perf second
 */
int32_t stress_get_ticks_per_second(void)
{
#if defined(_SC_CLK_TCK)
	static int32_t ticks_per_second = 0;

	if (LIKELY(ticks_per_second > 0))
		return ticks_per_second;

	ticks_per_second = (int32_t)sysconf(_SC_CLK_TCK);
	return ticks_per_second;
#else
	UNEXPECTED
	return -1;
#endif
}

/*
 *  stress_get_meminfo()
 *	wrapper for linux sysinfo
 */
static int stress_get_meminfo(
	size_t *freemem,
	size_t *totalmem,
	size_t *freeswap,
	size_t *totalswap)
{
	if (UNLIKELY(!freemem || !totalmem || !freeswap || !totalswap))
		return -1;
#if defined(HAVE_SYS_SYSINFO_H) &&	\
    defined(HAVE_SYSINFO)
	{
		struct sysinfo info;

		(void)shim_memset(&info, 0, sizeof(info));

		if (LIKELY(sysinfo(&info) == 0)) {
			*freemem = info.freeram * info.mem_unit;
			*totalmem = info.totalram * info.mem_unit;
			*freeswap = info.freeswap * info.mem_unit;
			*totalswap = info.totalswap * info.mem_unit;
			return 0;
		}
	}
#endif
#if defined(__FreeBSD__)
	{
		const size_t page_size = (size_t)stress_bsd_getsysctl_uint("vm.stats.vm.v_page_size");
#if 0
		/*
		 *  Enable total swap only when we can determine free swap
		 */
		const size_t max_size_t = (size_t)-1;
		const uint64_t vm_swap_total = stress_bsd_getsysctl_uint64("vm.swap_total");

		*totalswap = (vm_swap_total >= max_size_t) ? max_size_t : (size_t)vm_swap_total;
#endif
		*freemem = page_size * stress_bsd_getsysctl_uint32("vm.stats.vm.v_free_count");
		*totalmem = page_size *
			(stress_bsd_getsysctl_uint32("vm.stats.vm.v_active_count") +
			 stress_bsd_getsysctl_uint32("vm.stats.vm.v_inactive_count") +
			 stress_bsd_getsysctl_uint32("vm.stats.vm.v_laundry_count") +
			 stress_bsd_getsysctl_uint32("vm.stats.vm.v_wire_count") +
			 stress_bsd_getsysctl_uint32("vm.stats.vm.v_free_count"));
		*freeswap = 0;
		*totalswap = 0;
		return 0;
	}
#endif
#if defined(__NetBSD__) &&	\
    defined(HAVE_UVM_UVM_EXTERN_H)
	{
		struct uvmexp_sysctl u;

		if (stress_bsd_getsysctl("vm.uvmexp2", &u, sizeof(u)) == 0) {
			*freemem = (size_t)u.free * u.pagesize;
			*totalmem = (size_t)u.npages * u.pagesize;
			*totalswap = (size_t)u.swpages * u.pagesize;
			*freeswap = *totalswap - (size_t)u.swpginuse * u.pagesize;
			return 0;
		}
	}
#endif
#if defined(__APPLE__) &&		\
    defined(HAVE_MACH_MACH_H) &&	\
    defined(HAVE_MACH_VM_STATISTICS_H)
	{
		vm_statistics64_data_t vm_stat;
		mach_port_t host = mach_host_self();
		natural_t count = HOST_VM_INFO64_COUNT;
		size_t page_size = stress_get_page_size();
		int ret;

		/* zero vm_stat, keep cppcheck silent */
		(void)shim_memset(&vm_stat, 0, sizeof(vm_stat));
		ret = host_statistics64(host, HOST_VM_INFO64, (host_info64_t)&vm_stat, &count);
		if (ret >= 0) {
			*freemem = page_size * vm_stat.free_count;
			*totalmem = page_size * (vm_stat.active_count +
						 vm_stat.inactive_count +
						 vm_stat.wire_count +
						 vm_stat.zero_fill_count);
			return 0;
		}

	}
#endif
	*freemem = 0;
	*totalmem = 0;
	*freeswap = 0;
	*totalswap = 0;

	return -1;
}

/*
 *  stress_get_memlimits()
 *	get SHMALL and memory in system
 *	these are set to zero on failure
 */
void stress_get_memlimits(
	size_t *shmall,
	size_t *freemem,
	size_t *totalmem,
	size_t *freeswap,
	size_t *totalswap)
{
#if defined(__linux__)
	char buf[64];
#endif
	if (UNLIKELY(!shmall || !freemem || !totalmem || !freeswap || !totalswap))
		return;

	(void)stress_get_meminfo(freemem, totalmem, freeswap, totalswap);
#if defined(__linux__)
	if (LIKELY(stress_system_read("/proc/sys/kernel/shmall", buf, sizeof(buf)) > 0)) {
		if (sscanf(buf, "%zu", shmall) == 1)
			return;
	}
#endif
	*shmall = 0;
}

/*
 *  stress_get_gpu_freq_mhz()
 *	get GPU frequency in MHz, set to 0.0 if not readable
 */
void stress_get_gpu_freq_mhz(double *gpu_freq)
{
	if (UNLIKELY(!gpu_freq))
		return;
#if defined(__linux__)
	{
		char buf[64];

		if (stress_system_read("/sys/class/drm/card0/gt_cur_freq_mhz", buf, sizeof(buf)) > 0) {
			if (sscanf(buf, "%lf", gpu_freq) == 1)
				return;
		} else if (stress_system_read("/sys/class/drm/card0/gt_cur_freq_mhz", buf, sizeof(buf)) > 0) {
			if (sscanf(buf, "%lf", gpu_freq) == 1)
				return;
		}
	}
#endif
	*gpu_freq = 0.0;
}

#if !defined(PR_SET_MEMORY_MERGE)
#define PR_SET_MEMORY_MERGE	(67)
#endif

/*
 *  stress_ksm_memory_merge()
 *	set kernel samepage merging flag (linux only)
 */
void stress_ksm_memory_merge(const int flag)
{
#if defined(__linux__) &&		\
    defined(PR_SET_MEMORY_MERGE) &&	\
    defined(HAVE_SYS_PRCTL_H)
	if ((flag >= 0) && (flag <= 1)) {
		static int prev_flag = -1;

		if (flag != prev_flag) {
			VOID_RET(int, prctl(PR_SET_MEMORY_MERGE, flag));
			prev_flag = flag;
		}
		(void)stress_system_write("/sys/kernel/mm/ksm/run", "1\n", 2);
	}
#else
	(void)flag;
#endif
}

/*
 *  stress_low_memory()
 *	return true if running low on memory
 */
bool stress_low_memory(const size_t requested)
{
	static size_t prev_freemem = 0;
	static size_t prev_freeswap = 0;
	size_t freemem, totalmem, freeswap, totalswap;
	static double threshold = -1.0;
	bool low_memory = false;

	if (stress_get_meminfo(&freemem, &totalmem, &freeswap, &totalswap) == 0) {
		/*
		 *  Threshold not set, then get
		 */
		if (threshold < 0.0) {
			size_t bytes = 0;

			if (stress_get_setting("oom-avoid-bytes", &bytes)) {
				threshold = 100.0 * (double)bytes / (double)freemem;
			} else {
				/* Not specified, then default to 2.5% */
				threshold = 2.5;
			}
		}
		/*
		 *  Stats from previous call valid, then check for memory
		 *  changes
		 */
		if ((prev_freemem + prev_freeswap) > 0) {
			ssize_t delta;

			delta = (ssize_t)prev_freemem - (ssize_t)freemem;
			delta = (delta * 2) + requested;
			/* memory shrinking quickly? */
			if (delta  > (ssize_t)freemem) {
				low_memory = true;
				goto update;
			}
			/* swap shrinking quickly? */
			delta = (ssize_t)prev_freeswap - (ssize_t)freeswap;
			if (delta > 0) {
				low_memory = true;
				goto update;
			}
		}
		/* Not enough for allocation and slop? */
		if (freemem < ((4 * MB) + requested)) {
			low_memory = true;
			goto update;
		}
		/* Less than 3% left? */
		if (((double)freemem * 100.0 / (double)(totalmem - requested)) < threshold) {
			low_memory = true;
			goto update;
		}
		/* Any swap enabled with free memory we are too low? */
		if ((totalswap > 0) && (freeswap + freemem < (requested + (2 * MB)))) {
			low_memory = true;
			goto update;
		}
update:
		prev_freemem = freemem;
		prev_freeswap = freeswap;

		/* low memory? automatically enable ksm memory merging */
		if (low_memory)
			stress_ksm_memory_merge(1);
	}
	return low_memory;
}

#if defined(_SC_AVPHYS_PAGES)
#define STRESS_SC_PAGES	_SC_AVPHYS_PAGES
#elif defined(_SC_PHYS_PAGES)
#define STRESS_SC_PAGES	_SC_PHYS_PAGES
#endif

/*
 *  stress_get_phys_mem_size()
 *	get size of physical memory still available, 0 if failed
 */
uint64_t stress_get_phys_mem_size(void)
{
#if defined(STRESS_SC_PAGES)
	uint64_t phys_pages;
	const size_t page_size = stress_get_page_size();
	const uint64_t max_pages = ~0ULL / page_size;
	long int ret;

	errno = 0;
	ret = sysconf(STRESS_SC_PAGES);
	if (UNLIKELY((ret < 0) && (errno != 0)))
		return 0ULL;

	phys_pages = (uint64_t)ret;
	/* Avoid overflow */
	if (UNLIKELY(phys_pages > max_pages))
		phys_pages = max_pages;
	return phys_pages * page_size;
#else
	UNEXPECTED
	return 0ULL;
#endif
}

/*
 *  stress_get_filesystem_size()
 *	get size of free space still available on the
 *	file system where stress temporary path is located,
 *	return 0 if failed
 */
uint64_t stress_get_filesystem_size(void)
{
#if defined(HAVE_SYS_STATVFS_H)
	int rc;
	struct statvfs buf;
	fsblkcnt_t blocks, max_blocks;
	const char *path = stress_get_temp_path();

	if (UNLIKELY(!path))
		return 0;

	(void)shim_memset(&buf, 0, sizeof(buf));
	rc = statvfs(path, &buf);
	if (UNLIKELY(rc < 0))
		return 0;

	max_blocks = (~(fsblkcnt_t)0) / buf.f_bsize;
	blocks = buf.f_bavail;

	if (blocks > max_blocks)
		blocks = max_blocks;

	return (uint64_t)buf.f_bsize * blocks;
#else
	UNEXPECTED
	return 0ULL;
#endif
}

/*
 *  stress_get_filesystem_available_inodes()
 *	get number of free available inodes on the current stress
 *	temporary path, return 0 if failed
 */
uint64_t stress_get_filesystem_available_inodes(void)
{
#if defined(HAVE_SYS_STATVFS_H)
	int rc;
	struct statvfs buf;
	const char *path = stress_get_temp_path();

	if (UNLIKELY(!path))
		return 0;

	(void)shim_memset(&buf, 0, sizeof(buf));
	rc = statvfs(path, &buf);
	if (UNLIKELY(rc < 0))
		return 0;

	return (uint64_t)buf.f_favail;
#else
	UNEXPECTED
	return 0ULL;
#endif
}

/*
 *  stress_set_nonblock()
 *	try to make fd non-blocking
 */
int stress_set_nonblock(const int fd)
{
	int flags;
#if defined(O_NONBLOCK)

	if ((flags = fcntl(fd, F_GETFL, 0)) < 0)
		flags = 0;
	return fcntl(fd, F_SETFL, O_NONBLOCK | flags);
#else
	UNEXPECTED
	flags = 1;
	return ioctl(fd, FIOBIO, &flags);
#endif
}

/*
 *  stress_get_load_avg()
 *	get load average
 */
int stress_get_load_avg(
	double *min1,
	double *min5,
	double *min15)
{
#if defined(HAVE_GETLOADAVG) &&	\
    !defined(__UCLIBC__)
	int rc;
	double loadavg[3];

	if (UNLIKELY(!min1 || !min5 || !min15))
		return -1;

	loadavg[0] = 0.0;
	loadavg[1] = 0.0;
	loadavg[2] = 0.0;

	rc = getloadavg(loadavg, 3);
	if (UNLIKELY(rc < 0))
		goto fail;

	*min1 = loadavg[0];
	*min5 = loadavg[1];
	*min15 = loadavg[2];

	return 0;
fail:
#elif defined(HAVE_SYS_SYSINFO_H) &&	\
      defined(HAVE_SYSINFO)
	struct sysinfo info;
	const double scale = 1.0 / (double)(1 << SI_LOAD_SHIFT);

	if (UNLIKELY(!min1 || !min5 || !min15))
		return -1;

	if (UNLIKELY(sysinfo(&info) < 0))
		goto fail;

	*min1 = info.loads[0] * scale;
	*min5 = info.loads[1] * scale;
	*min15 = info.loads[2] * scale;

	return 0;
fail:
#else
	if (UNLIKELY(!min1 || !min5 || !min15))
		return -1;
#endif
	*min1 = *min5 = *min15 = 0.0;
	return -1;
}

/*
 *  stress_parent_died_alarm()
 *	send child SIGALRM if the parent died
 */
void stress_parent_died_alarm(void)
{
#if defined(HAVE_PRCTL) &&		\
    defined(HAVE_SYS_PRCTL_H) &&	\
    defined(PR_SET_PDEATHSIG)
	(void)prctl(PR_SET_PDEATHSIG, SIGALRM);
#elif defined(HAVE_SYS_PROCCTL_H) &&	\
      defined(__FreeBSD__) &&		\
      defined(PROC_PDEATHSIG_CTL)
	int sig = SIGALRM;

	(void)procctl(P_PID, 0, PROC_PDEATHSIG_CTL, &sig);
#else
	UNEXPECTED
#endif
}

/*
 *  stress_process_dumpable()
 *	set dumpable flag, e.g. produce a core dump or not,
 *	don't print an error if these fail, it's not that
 *	critical
 */
int stress_process_dumpable(const bool dumpable)
{
	int rc = 0;

	(void)dumpable;

#if defined(RLIMIT_CORE)
	{
		struct rlimit lim;
		int ret;

		ret = getrlimit(RLIMIT_CORE, &lim);
		if (LIKELY(ret == 0)) {
			lim.rlim_cur = 0;
			(void)setrlimit(RLIMIT_CORE, &lim);
		}
		lim.rlim_cur = 0;
		lim.rlim_max = 0;
		(void)setrlimit(RLIMIT_CORE, &lim);
	}
#else
	UNEXPECTED
#endif

	/*
	 *  changing PR_SET_DUMPABLE also affects the
	 *  oom adjust capability, so for now, we disable
	 *  this as I'd rather have a oom'able process when
	 *  memory gets constrained. Don't enable this
	 *  unless one checks that processes able oomable!
	 */
#if 0 && defined(HAVE_PRCTL) &&		\
    defined(HAVE_SYS_PRCTL_H) &&	\
    defined(PR_SET_DUMPABLE)

#if !defined(PR_SET_DISABLE)
#define SUID_DUMP_DISABLE	(0)       /* No setuid dumping */
#endif
#if !defined(SUID_DUMP_USER)
#define SUID_DUMP_USER		(1)       /* Dump as user of process */
#endif

	(void)prctl(PR_SET_DUMPABLE,
		dumpable ? SUID_DUMP_USER : SUID_DUMP_DISABLE);
#endif

#if defined(__linux__)
	{
		char const *str = dumpable ? "0x33" : "0x00";

		if (stress_system_write("/proc/self/coredump_filter", str, strlen(str)) < 0)
			rc = -1;
	}
#endif
	return rc;
}

/*
 *  stress_set_timer_slack_ns()
 *	set timer slack in nanoseconds
 */
int stress_set_timer_slack_ns(const char *opt)
{
#if defined(HAVE_PRCTL_TIMER_SLACK)
	uint32_t timer_slack;

	timer_slack = stress_get_uint32(opt);
	if (UNLIKELY(timer_slack == 0))
		pr_inf("note: setting timer_slack to 0 resets it to the default of 50,000 ns\n");
	(void)stress_set_setting("global", "timer-slack", TYPE_ID_UINT32, &timer_slack);
#else
	UNEXPECTED
	(void)opt;
#endif
	return 0;
}

/*
 *  stress_set_timer_slack()
 *	set timer slack
 */
void stress_set_timer_slack(void)
{
#if defined(HAVE_PRCTL) && 		\
    defined(HAVE_SYS_PRCTL_H) &&	\
    defined(HAVE_PRCTL_TIMER_SLACK)
	uint32_t timer_slack;

	if (stress_get_setting("timer-slack", &timer_slack))
		(void)prctl(PR_SET_TIMERSLACK, timer_slack);
#else
	UNEXPECTED
#endif
}

/*
 *  stress_set_proc_name_init()
 *	init setproctitle if supported
 */
void stress_set_proc_name_init(int argc, char *argv[], char *envp[])
{
#if defined(HAVE_BSD_UNISTD_H) &&	\
    defined(HAVE_SETPROCTITLE)
	(void)setproctitle_init(argc, argv, envp);
#else
	(void)argc;
	(void)argv;
	(void)envp;
	UNEXPECTED
#endif
}

/*
 *  stress_set_proc_name()
 *	Set process name, we don't care if it fails
 */
void stress_set_proc_name(const char *name)
{
	char long_name[64];

	if (UNLIKELY(!name))
		return;

	if (g_opt_flags & OPT_FLAGS_KEEP_NAME)
		return;
	(void)snprintf(long_name, sizeof(long_name), "%s-%s",
			g_app_name, name);

#if defined(HAVE_BSD_UNISTD_H) &&	\
    defined(HAVE_SETPROCTITLE)
	/* Sets argv[0] */
	setproctitle("-%s", long_name);
#endif
#if defined(HAVE_PRCTL) &&		\
    defined(HAVE_SYS_PRCTL_H) &&	\
    defined(PR_SET_NAME)
	/* Sets the comm field */
	(void)prctl(PR_SET_NAME, long_name);
#endif
}

/*
 *  stress_set_proc_state_str
 *	set process name based on run state string, see
 *	macros STRESS_STATE_*
 */
void stress_set_proc_state_str(const char *name, const char *str)
{
	char long_name[64];

	if (UNLIKELY(!name || !str))
		return;

	(void)str;
	if (g_opt_flags & OPT_FLAGS_KEEP_NAME)
		return;
	(void)snprintf(long_name, sizeof(long_name), "%s-%s",
			g_app_name, name);

#if defined(HAVE_BSD_UNISTD_H) &&	\
    defined(HAVE_SETPROCTITLE)
	setproctitle("-%s [%s]", long_name, str);
#endif
#if defined(HAVE_PRCTL) &&		\
    defined(HAVE_SYS_PRCTL_H) &&	\
    defined(PR_SET_NAME)
	/* Sets the comm field */
	(void)prctl(PR_SET_NAME, long_name);
#endif
}

/*
 *  stress_set_proc_state
 *	set process name based on run state, see
 *	macros STRESS_STATE_*
 */
void stress_set_proc_state(const char *name, const int state)
{
	static const char * const stress_states[] = {
		"start",
		"init",
		"run",
		"syncwait",
		"deinit",
		"stop",
		"exit",
		"wait",
		"zombie",
	};

	if (UNLIKELY(!name))
		return;
	if (UNLIKELY((state < 0) || (state >= (int)SIZEOF_ARRAY(stress_states))))
		return;

	stress_set_proc_state_str(name, stress_states[state]);
}

/*
 *  stress_chr_munge()
 *	convert ch _ to -, otherwise don't change it
 */
static inline char PURE stress_chr_munge(const char ch)
{
	return (ch == '_') ? '-' : ch;
}

/*
 *   stress_munge_underscore()
 *	turn '_' to '-' in strings with strscpy api
 */
size_t stress_munge_underscore(char *dst, const char *src, size_t len)
{
	register char *d = dst;
	register const char *s = src;
	register size_t n = len;

	if (n) {
		while (--n) {
			register char c = *s++;

			*d++ = stress_chr_munge(c);
			if (c == '\0')
				break;
		}
	}

	if (!n) {
		if (len)
			*d = '\0';
		while (*s)
			s++;
	}

	return (s - src - 1);
}

/*
 *  stress_strcmp_munged()
 *	compare strings with _ comcompared to -
 */
int stress_strcmp_munged(const char *s1, const char *s2)
{
	for (; *s1 && (stress_chr_munge(*s1) == stress_chr_munge(*s2)); s1++, s2++)
		;

	return (unsigned char)stress_chr_munge(*s1) - (unsigned char)stress_chr_munge(*s2);
}

/*
 *  stress_get_stack_direction_helper()
 *	helper to determine direction of stack
 */
static ssize_t NOINLINE OPTIMIZE0 stress_get_stack_direction_helper(const uint8_t *val1)
{
	const uint8_t val2 = *val1;
	const ssize_t diff = &val2 - (const uint8_t *)val1;

	return (diff > 0) - (diff < 0);
}

/*
 *  stress_get_stack_direction()
 *      determine which way the stack goes, up / down
 *	just pass in any var on the stack before calling
 *	return:
 *		 1 - stack goes down (conventional)
 *		 0 - error
 *	  	-1 - stack goes up (unconventional)
 */
ssize_t stress_get_stack_direction(void)
{
	uint8_t val1 = 0;
	uint8_t waste[64];

	waste[(sizeof waste) - 1] = 0;
	return stress_get_stack_direction_helper(&val1);
}

/*
 *  stress_get_stack_top()
 *	Get the stack top given the start and size of the stack,
 *	offset by a bit of slop. Assumes stack is > 64 bytes
 */
void *stress_get_stack_top(void *start, size_t size)
{
	const size_t offset = stress_get_stack_direction() < 0 ? (size - 64) : 64;

	return (void *)((char *)start + offset);
}

/*
 *  stress_get_uint64_zero()
 *	return uint64 zero in way that force less smart
 *	static analysers to realise we are doing this
 *	to force a division by zero. I'd like to have
 *	a better solution than this ghastly way.
 */
uint64_t stress_get_uint64_zero(void)
{
	return g_shared->zero;
}

/*
 *  stress_get_uint64_zero()
 *	return null in way that force less smart
 *	static analysers to realise we are doing this
 *	to force a division by zero. I'd like to have
 *	a better solution than this ghastly way.
 */
void *stress_get_null(void)
{
	return (void *)(uintptr_t)g_shared->zero;
}

/*
 *  stress_base36_encode_uint64()
 *	encode 64 bit hash of filename into a unique base 36
 *	filename of up to 13 chars long + 1 char eos
 */
static void stress_base36_encode_uint64(char dst[14], uint64_t val)
{
	static const char b36[] = "abcdefghijklmnopqrstuvwxyz0123456789";
	const int b = 36;
	char *ptr = dst;

	while (val) {
		*ptr++ = b36[val % b];
		val /= b;
	}
	*ptr = '\0';
}

/*
 *  stress_temp_hash_truncate()
 *	filenames may be too long for the underlying filesystem
 *	so workaround this by hashing them into a 64 bit hex
 *	filename.
 */
static void stress_temp_hash_truncate(char *filename)
{
	size_t f_namemax = 16;
	size_t len = strlen(filename);
#if defined(HAVE_SYS_STATVFS_H)
	struct statvfs buf;

	(void)shim_memset(&buf, 0, sizeof(buf));
	if (statvfs(stress_get_temp_path(), &buf) == 0)
		f_namemax = buf.f_namemax;
#endif

	if (strlen(filename) > f_namemax) {
		const uint32_t upper = stress_hash_jenkin((uint8_t *)filename, len);
		const uint32_t lower = stress_hash_pjw(filename);
		const uint64_t val = ((uint64_t)upper << 32) | lower;

		stress_base36_encode_uint64(filename, val);
	}
}

/*
 *  stress_temp_filename()
 *      construct a temp filename
 */
int stress_temp_filename(
	char *path,
	const size_t len,
	const char *name,
	const pid_t pid,
	const uint32_t instance,
	const uint64_t magic)
{
	char directoryname[PATH_MAX];
	char filename[PATH_MAX];

	(void)snprintf(directoryname, sizeof(directoryname),
		"tmp-%s-%s-%d-%" PRIu32,
		g_app_name, name, (int)pid, instance);
	stress_temp_hash_truncate(directoryname);

	(void)snprintf(filename, sizeof(filename),
		"%s-%s-%d-%" PRIu32 "-%" PRIu64,
		g_app_name, name, (int)pid, instance, magic);
	stress_temp_hash_truncate(filename);

	return snprintf(path, len, "%s/%s/%s",
		stress_get_temp_path(), directoryname, filename);
}

/*
 *  stress_temp_filename_args()
 *      construct a temp filename using info from args
 */
int stress_temp_filename_args(
	stress_args_t *args,
	char *path,
	const size_t len,
	const uint64_t magic)
{
	return stress_temp_filename(path, len, args->name,
		args->pid, args->instance, magic);
}

/*
 *  stress_temp_dir()
 *	create a temporary directory name
 */
int stress_temp_dir(
	char *path,
	const size_t len,
	const char *name,
	const pid_t pid,
	const uint32_t instance)
{
	char directoryname[256];
	int l;

	(void)snprintf(directoryname, sizeof(directoryname),
		"tmp-%s-%s-%d-%" PRIu32,
		g_app_name, name, (int)pid, instance);
	stress_temp_hash_truncate(directoryname);

	l = snprintf(path, len, "%s/%s",
		stress_get_temp_path(), directoryname);
	return l;
}

/*
 *  stress_temp_dir_args()
 *	create a temporary directory name using info from args
 */
int stress_temp_dir_args(
	stress_args_t *args,
	char *path,
	const size_t len)
{
	return stress_temp_dir(path, len,
		args->name, args->pid, args->instance);
}

/*
 *   stress_temp_dir_mk()
 *	create a temporary directory
 */
int stress_temp_dir_mk(
	const char *name,
	const pid_t pid,
	const uint32_t instance)
{
	int ret;
	char tmp[PATH_MAX];

	stress_temp_dir(tmp, sizeof(tmp), name, pid, instance);
	ret = mkdir(tmp, S_IRWXU);
	if (UNLIKELY(ret < 0)) {
		ret = -errno;
		pr_fail("%s: mkdir '%s' failed, errno=%d (%s)\n",
			name, tmp, errno, strerror(errno));
		(void)shim_rmdir(tmp);
	}

	return ret;
}

/*
 *   stress_temp_dir_mk_args()
 *	create a temporary director using info from args
 */
int stress_temp_dir_mk_args(stress_args_t *args)
{
	return stress_temp_dir_mk(args->name, args->pid, args->instance);
}

/*
 *  stress_temp_dir_rm()
 *	remove a temporary directory
 */
int stress_temp_dir_rm(
	const char *name,
	const pid_t pid,
	const uint32_t instance)
{
	int ret;
	char tmp[PATH_MAX + 1];

	stress_temp_dir(tmp, sizeof(tmp), name, pid, instance);
	ret = shim_rmdir(tmp);
	if (UNLIKELY(ret < 0)) {
		ret = -errno;
		pr_fail("%s: rmdir '%s' failed, errno=%d (%s)\n",
			name, tmp, errno, strerror(errno));
	}

	return ret;
}

/*
 *  stress_temp_dir_rm_args()
 *	remove a temporary directory using info from args
 */
int stress_temp_dir_rm_args(stress_args_t *args)
{
	return stress_temp_dir_rm(args->name, args->pid, args->instance);
}

/*
 *  stress_get_signal_name()
 *	return string version of signal number, NULL if not found
 */
const char PURE *stress_get_signal_name(const int signum)
{
	size_t i;

#if defined(SIGRTMIN) &&	\
    defined(SIGRTMAX)
	if ((signum >= SIGRTMIN) && (signum <= SIGRTMAX)) {
		static char sigrtname[10];

		(void)snprintf(sigrtname, sizeof(sigrtname), "SIGRT%d",
			signum - SIGRTMIN);
		return sigrtname;
	}
#endif
	for (i = 0; i < SIZEOF_ARRAY(sig_names); i++) {
		if (signum == sig_names[i].signum)
			return sig_names[i].name;
	}
	return NULL;
}

/*
 *  stress_strsignal()
 *	signum to human readable string
 */
const char *stress_strsignal(const int signum)
{
	static char buffer[40];
	const char *str = stress_get_signal_name(signum);

	if (str)
		(void)snprintf(buffer, sizeof(buffer), "signal %d '%s'",
			signum, str);
	else
		(void)snprintf(buffer, sizeof(buffer), "signal %d", signum);
	return buffer;
}

/*
 *  stress_little_endian()
 *	returns true if CPU is little endian
 */
bool PURE stress_little_endian(void)
{
	const uint32_t x = 0x12345678;
	const uint8_t *y = (const uint8_t *)&x;

	return *y == 0x78;
}

/*
 *  stress_endian_str()
 *	return endianness as a string
 */
static const char * PURE stress_endian_str(void)
{
	return stress_little_endian() ? "little endian" : "big endian";
}

/*
 *  stress_uint8rnd4()
 *	fill a uint8_t buffer full of random data
 *	buffer *must* be multiple of 4 bytes in size
 */
OPTIMIZE3 void stress_uint8rnd4(uint8_t *data, const size_t len)
{
	register uint32_t *ptr32 = (uint32_t *)shim_assume_aligned(data, 4);
	register const uint32_t *ptr32end = (uint32_t *)(data + len);

	if (UNLIKELY(!data || (len < 4)))
		return;

	if (stress_little_endian()) {
		while (ptr32 < ptr32end)
			*ptr32++ = stress_mwc32();
	} else {
		while (ptr32 < ptr32end)
			*ptr32++ = stress_swap32(stress_mwc32());
	}
}

/*
 *  stress_get_libc_version()
 *	return human readable libc version (where possible)
 */
static char *stress_get_libc_version(void)
{
#if defined(__GLIBC__) &&	\
    defined(__GLIBC_MINOR__)
	static char buf[64];

	(void)snprintf(buf, sizeof(buf), "glibc %d.%d", __GLIBC__, __GLIBC_MINOR__);
	return buf;
#elif defined(__UCLIBC__) &&		\
    defined(__UCLIBC_MAJOR__) &&	\
    defined(__UCLIBC_MINOR__)
	static char buf[64];

	(void)snprintf(buf, sizeof(buf), "uclibc %d.%d", __UCLIBC_MAJOR__, __UCLIBC_MINOR__);
	return buf;
#elif defined(__DARWIN_C_LEVEL)
	return "Darwin libc";
#elif defined(HAVE_CC_MUSL_GCC)
	/* Built with MUSL_GCC, highly probably it's musl libc being used too */
	return "musl libc";
#elif defined(__HAIKU__)
	return "Haiku libc";
#else
	return "unknown libc version";
#endif
}

/*
 *  stress_run_info()
 *	short info about the system we are running stress-ng on
 *	for the -v option
 */
void stress_runinfo(void)
{
	char real_path[PATH_MAX], *real_path_ret;
	const char *temp_path = stress_get_temp_path();
	const char *fs_type = stress_get_fs_type(temp_path);
	size_t freemem, totalmem, freeswap, totalswap;
#if defined(HAVE_UNAME) &&	\
    defined(HAVE_SYS_UTSNAME_H)
	struct utsname uts;
#endif
	if (!(g_opt_flags & OPT_FLAGS_PR_DEBUG))
		return;

	if (sizeof(STRESS_GIT_COMMIT_ID) > 1) {
		pr_dbg("%s %s g%12.12s\n",
			g_app_name, VERSION, STRESS_GIT_COMMIT_ID);
	} else {
		pr_dbg("%s %s\n",
			g_app_name, VERSION);
	}

#if defined(HAVE_UNAME) &&	\
    defined(HAVE_SYS_UTSNAME_H)
	if (LIKELY(uname(&uts) >= 0)) {
		pr_dbg("system: %s %s %s %s %s, %s, %s, %s\n",
			uts.sysname, uts.nodename, uts.release,
			uts.version, uts.machine,
			stress_get_compiler(),
			stress_get_libc_version(),
			stress_endian_str());
	}
#else
	pr_dbg("system: %s, %s, %s\n",
		stress_get_compiler(),
		stress_get_libc_version(),
		stress_endian_str());
#endif
	if (stress_get_meminfo(&freemem, &totalmem, &freeswap, &totalswap) == 0) {
		char ram_t[32], ram_f[32], ram_s[32];

		stress_uint64_to_str(ram_t, sizeof(ram_t), (uint64_t)totalmem);
		stress_uint64_to_str(ram_f, sizeof(ram_f), (uint64_t)freemem);
		stress_uint64_to_str(ram_s, sizeof(ram_s), (uint64_t)freeswap);
		pr_dbg("RAM total: %s, RAM free: %s, swap free: %s\n", ram_t, ram_f, ram_s);
	}
	real_path_ret = realpath(temp_path, real_path);
	pr_dbg("temporary file path: '%s'%s\n", real_path_ret ? real_path : temp_path, fs_type);
}

/*
 *  stress_yaml_runinfo()
 *	log info about the system we are running stress-ng on
 */
void stress_yaml_runinfo(FILE *yaml)
{
#if defined(HAVE_UNAME) &&	\
    defined(HAVE_SYS_UTSNAME_H)
	struct utsname uts;
#endif
#if defined(HAVE_SYS_SYSINFO_H) &&	\
    defined(HAVE_SYSINFO)
	struct sysinfo info;
#endif
	time_t t;
	struct tm *tm = NULL;
	const size_t hostname_len = stress_get_hostname_length();
	char *hostname;
	const char *user = shim_getlogin();

	if (UNLIKELY(!yaml))
		return;

	pr_yaml(yaml, "system-info:\n");
	if (time(&t) != ((time_t)-1))
		tm = localtime(&t);

	pr_yaml(yaml, "      stress-ng-version: " VERSION "\n");
	pr_yaml(yaml, "      run-by: %s\n", user ? user : "unknown");
	if (tm) {
		pr_yaml(yaml, "      date-yyyy-mm-dd: %4.4d:%2.2d:%2.2d\n",
			tm->tm_year + 1900, tm->tm_mon + 1, tm->tm_mday);
		pr_yaml(yaml, "      time-hh-mm-ss: %2.2d:%2.2d:%2.2d\n",
			tm->tm_hour, tm->tm_min, tm->tm_sec);
		pr_yaml(yaml, "      epoch-secs: %ld\n", (long int)t);
	}

	hostname = (char *)malloc(hostname_len + 1);
	if (hostname && !gethostname(hostname, hostname_len)) {
		pr_yaml(yaml, "      hostname: %s\n", hostname);
	} else {
		pr_yaml(yaml, "      hostname: %s\n", "unknown");
	}
	free(hostname);

#if defined(HAVE_UNAME) &&	\
    defined(HAVE_SYS_UTSNAME_H)
	if (uname(&uts) >= 0) {
		pr_yaml(yaml, "      sysname: %s\n", uts.sysname);
		pr_yaml(yaml, "      nodename: %s\n", uts.nodename);
		pr_yaml(yaml, "      release: %s\n", uts.release);
		pr_yaml(yaml, "      version: '%s'\n", uts.version);
		pr_yaml(yaml, "      machine: %s\n", uts.machine);
	}
#endif
	pr_yaml(yaml, "      compiler: '%s'\n", stress_get_compiler());
	pr_yaml(yaml, "      libc: '%s'\n", stress_get_libc_version());
#if defined(HAVE_SYS_SYSINFO_H) &&	\
    defined(HAVE_SYSINFO)
	(void)shim_memset(&info, 0, sizeof(info));
	if (sysinfo(&info) == 0) {
		pr_yaml(yaml, "      uptime: %ld\n", info.uptime);
		pr_yaml(yaml, "      totalram: %lu\n", info.totalram);
		pr_yaml(yaml, "      freeram: %lu\n", info.freeram);
		pr_yaml(yaml, "      sharedram: %lu\n", info.sharedram);
		pr_yaml(yaml, "      bufferram: %lu\n", info.bufferram);
		pr_yaml(yaml, "      totalswap: %lu\n", info.totalswap);
		pr_yaml(yaml, "      freeswap: %lu\n", info.freeswap);
	}
#endif
	pr_yaml(yaml, "      pagesize: %zd\n", stress_get_page_size());
	pr_yaml(yaml, "      cpus: %" PRId32 "\n", stress_get_processors_configured());
	pr_yaml(yaml, "      cpus-online: %" PRId32 "\n", stress_get_processors_online());
	pr_yaml(yaml, "      ticks-per-second: %" PRId32 "\n", stress_get_ticks_per_second());
	pr_yaml(yaml, "\n");
}

/*
 *  stress_cache_alloc()
 *	allocate shared cache buffer
 */
int stress_cache_alloc(const char *name)
{
	stress_cpu_cache_cpus_t *cpu_caches;
	stress_cpu_cache_t *cache = NULL;
	uint16_t max_cache_level = 0, level;
	char cache_info[512];
	const int numa_nodes = stress_numa_nodes();

	cpu_caches = stress_cpu_cache_get_all_details();

	if (g_shared->mem_cache.size > 0)
		goto init_done;

	if (!cpu_caches) {
		if (stress_warn_once())
			pr_dbg("%s: using defaults, cannot determine cache details\n", name);
		g_shared->mem_cache.size = MEM_CACHE_SIZE * numa_nodes;
		goto init_done;
	}

	max_cache_level = stress_cpu_cache_get_max_level(cpu_caches);
	if (max_cache_level == 0) {
		if (stress_warn_once())
			pr_dbg("%s: using defaults, cannot determine cache level details\n", name);
		g_shared->mem_cache.size = MEM_CACHE_SIZE * numa_nodes;
		goto init_done;
	}
	if (g_shared->mem_cache.level > max_cache_level) {
		if (stress_warn_once())
			pr_dbg("%s: using cache maximum level L%d\n", name,
				max_cache_level);
		g_shared->mem_cache.level = max_cache_level;
	}

	cache = stress_cpu_cache_get(cpu_caches, g_shared->mem_cache.level);
	if (!cache) {
		if (stress_warn_once())
			pr_dbg("%s: using built-in defaults as no suitable "
				"cache found\n", name);
		g_shared->mem_cache.size = MEM_CACHE_SIZE * numa_nodes;
		goto init_done;
	}

	if (g_shared->mem_cache.ways > 0) {
		uint64_t way_size;

		if (g_shared->mem_cache.ways > cache->ways) {
			if (stress_warn_once())
				pr_inf("%s: cache way value too high - "
					"defaulting to %d (the maximum)\n",
					name, cache->ways);
			g_shared->mem_cache.ways = cache->ways;
		}
		way_size = cache->size / cache->ways;

		/* only fill the specified number of cache ways */
		g_shared->mem_cache.size = way_size * g_shared->mem_cache.ways * numa_nodes;
	} else {
		/* fill the entire cache */
		g_shared->mem_cache.size = cache->size * numa_nodes;
	}

	if (!g_shared->mem_cache.size) {
		if (stress_warn_once())
			pr_dbg("%s: using built-in defaults as "
				"unable to determine cache size\n", name);
		g_shared->mem_cache.size = MEM_CACHE_SIZE;
	}

	(void)shim_memset(cache_info, 0, sizeof(cache_info));
	for (level = 1; level <= max_cache_level; level++) {
		size_t cache_size = 0, cache_line_size = 0;

		stress_cpu_cache_get_level_size(level, &cache_size, &cache_line_size);
		if ((cache_size > 0) && (cache_line_size > 0)) {
			char tmp[64];

			(void)snprintf(tmp, sizeof(tmp), "%sL%" PRIu16 ": %zdK",
				(level > 1) ? ", " : "", level, cache_size >> 10);
			shim_strlcat(cache_info, tmp, sizeof(cache_info));
		}
	}
	pr_dbg("CPU data cache: %s\n", cache_info);
init_done:

	stress_free_cpu_caches(cpu_caches);
	g_shared->mem_cache.buffer =
		(uint8_t *)stress_mmap_populate(NULL, g_shared->mem_cache.size,
				PROT_READ | PROT_WRITE,
				MAP_SHARED | MAP_ANONYMOUS, -1, 0);
	if (g_shared->mem_cache.buffer == MAP_FAILED) {
		g_shared->mem_cache.buffer = NULL;
		pr_err("%s: failed to mmap shared cache buffer, errno=%d (%s)\n",
			name, errno, strerror(errno));
		return -1;
	}
	stress_set_vma_anon_name(g_shared->mem_cache.buffer, g_shared->mem_cache.size, "mem-cache");

	g_shared->cacheline.size = (size_t)STRESS_PROCS_MAX * sizeof(uint8_t) * 2;
	g_shared->cacheline.buffer =
		(uint8_t *)stress_mmap_populate(NULL, g_shared->cacheline.size,
				PROT_READ | PROT_WRITE,
				MAP_SHARED | MAP_ANONYMOUS, -1, 0);
	if (g_shared->cacheline.buffer == MAP_FAILED) {
		g_shared->cacheline.buffer = NULL;
		pr_err("%s: failed to mmap cacheline buffer, errno=%d (%s)\n",
			name, errno, strerror(errno));
		return -1;
	}
	stress_set_vma_anon_name(g_shared->cacheline.buffer, g_shared->cacheline.size, "cacheline");
	if (stress_warn_once()) {
		if (numa_nodes > 1) {
			pr_dbg("%s: shared cache buffer size: %" PRIu64 "K (LLC size x %d NUMA nodes)\n",
				name, g_shared->mem_cache.size / 1024, numa_nodes);
		} else {
			pr_dbg("%s: shared cache buffer size: %" PRIu64 "K\n",
				name, g_shared->mem_cache.size / 1024);
		}
	}

	return 0;
}

/*
 *  stress_cache_free()
 *	free shared cache buffer
 */
void stress_cache_free(void)
{
	if (g_shared->mem_cache.buffer)
		(void)munmap((void *)g_shared->mem_cache.buffer, g_shared->mem_cache.size);
	if (g_shared->cacheline.buffer)
		(void)munmap((void *)g_shared->cacheline.buffer, g_shared->cacheline.size);
}

/*
 *  stress_system_write()
 *	write a buffer to a /sys or /proc entry
 */
ssize_t stress_system_write(
	const char *path,
	const char *buf,
	const size_t buf_len)
{
	int fd;
	ssize_t ret;

	if (UNLIKELY(!path || !buf))
		return -EINVAL;
	if (UNLIKELY(buf_len == 0))
		return -EINVAL;

	fd = open(path, O_WRONLY);
	if (UNLIKELY(fd < 0))
		return -errno;
	ret = write(fd, buf, buf_len);
	if (ret < (ssize_t)buf_len)
		ret = -errno;
	(void)close(fd);

	return ret;
}

/*
 *  stress_system_discard()
 *	read and discard contents of a given file
 */
ssize_t stress_system_discard(const char *path)
{
	int fd;
	ssize_t ret;

	if (UNLIKELY(!path))
		return -EINVAL;
	fd = open(path, O_RDONLY);
	if (UNLIKELY(fd < 0))
		return -errno;
	ret = stress_read_discard(fd);
	(void)close(fd);

	return ret;
}

/*
 *  stress_system_read()
 *	read a buffer from a /sys or /proc entry
 */
ssize_t stress_system_read(
	const char *path,
	char *buf,
	const size_t buf_len)
{
	int fd;
	ssize_t ret;

	if (UNLIKELY(!path || !buf))
		return -EINVAL;
	if (UNLIKELY(buf_len == 0))
		return -EINVAL;

	(void)shim_memset(buf, 0, buf_len);

	fd = open(path, O_RDONLY);
	if (UNLIKELY(fd < 0))
		return -errno;
	ret = read(fd, buf, buf_len);
	if (UNLIKELY(ret < 0)) {
		buf[0] = '\0';
		ret = -errno;
	}
	(void)close(fd);
	if ((ssize_t)buf_len == ret)
		buf[buf_len - 1] = '\0';

	return ret;
}

/*
 *  stress_is_prime64()
 *      return true if 64 bit value n is prime
 *      http://en.wikipedia.org/wiki/Primality_test
 */
bool PURE stress_is_prime64(const uint64_t n)
{
	register uint64_t i, max;
	double max_d;

	if (n <= 3)
		return n >= 2;
	if ((n % 2 == 0) || (n % 3 == 0))
		return false;
	max_d = 1.0 + shim_sqrt((double)n);
	max = (uint64_t)max_d;
	for (i = 5; i < max; i += 6)
		if ((n % i == 0) || (n % (i + 2) == 0))
			return false;
	return true;
}

/*
 *  stress_get_next_prime64()
 *	find a prime that is not a multiple of n,
 *	used for file name striding. Minimum is 1009,
 *	max is unbounded. Return a prime > n, each
 *	call will return the next prime to keep the
 *	primes different each call.
 */
uint64_t stress_get_next_prime64(const uint64_t n)
{
	static uint64_t p = 1009;
	const uint64_t odd_n = (n & 0x0ffffffffffffffeUL) + 1;
	int i;

	if (p < odd_n)
		p = odd_n;

	/* Search for next prime.. */
	for (i = 0; LIKELY(stress_continue_flag() && (i < 2000)); i++) {
		p += 2;

		if ((n % p) && stress_is_prime64(p))
			return p;
	}
	/* Give up */
	p = 1009;
	return p;
}

/*
 *  stress_get_prime64()
 *	find a prime that is not a multiple of n,
 *	used for file name striding. Minimum is 1009,
 *	max is unbounded. Return a prime > n.
 */
uint64_t stress_get_prime64(const uint64_t n)
{
	uint64_t p = 1009;
	const uint64_t odd_n = (n & 0x0ffffffffffffffeUL) + 1;
	int i;

	if (p < odd_n)
		p = odd_n;

	/* Search for next prime.. */
	for (i = 0; LIKELY(stress_continue_flag() && (i < 2000)); i++) {
		p += 2;

		if ((n % p) && stress_is_prime64(p))
			return p;
	}
	/* Give up */
	return 18446744073709551557ULL;	/* Max 64 bit prime */
}

/*
 *  stress_get_max_file_limit()
 *	get max number of files that the current
 *	process can open not counting the files that
 *	may already been opened.
 */
size_t stress_get_max_file_limit(void)
{
#if defined(HAVE_GETDTABLESIZE)
	int tablesize;
#endif
#if defined(RLIMIT_NOFILE)
	struct rlimit rlim;
#endif
	size_t max_rlim = SIZE_MAX;
	size_t max_sysconf;

#if defined(HAVE_GETDTABLESIZE)
	/* try the simple way first */
	tablesize = getdtablesize();
	if (tablesize > 0)
		return (size_t)tablesize;
#endif
#if defined(RLIMIT_NOFILE)
	if (!getrlimit(RLIMIT_NOFILE, &rlim))
		max_rlim = (size_t)rlim.rlim_cur;
#endif
#if defined(_SC_OPEN_MAX)
	{
		const long int open_max = sysconf(_SC_OPEN_MAX);

		max_sysconf = (open_max > 0) ? (size_t)open_max : SIZE_MAX;
	}
#else
	max_sysconf = SIZE_MAX;
	UNEXPECTED
#endif
	/* return the lowest of these two */
	return STRESS_MINIMUM(max_rlim, max_sysconf);
}

/*
 *  stress_get_open_count(void)
 *	get number of open file descriptors
 */
static inline size_t stress_get_open_count(void)
{
#if defined(__linux__)
	DIR *dir;
	struct dirent *d;
	size_t n = 0;

	dir = opendir("/proc/self/fd");
	if (!dir)
		return (size_t)-1;

	while ((d = readdir(dir)) != NULL) {
		if (isdigit((unsigned char)d->d_name[0]))
			n++;
	}
	(void)closedir(dir);

	/*
	 * opendir used one extra fd that is now
	 * closed, so take that off the total
	 */
	return (n > 1) ? (n - 1) : n;
#else
	return 0;
#endif
}

/*
 *  stress_get_file_limit()
 *	get max number of files that the current
 *	process can open excluding currently opened
 *	files.
 */
size_t stress_get_file_limit(void)
{
	struct rlimit rlim;
	size_t last_opened, opened, max = 65536;	/* initial guess */

	if (!getrlimit(RLIMIT_NOFILE, &rlim))
		max = (size_t)rlim.rlim_cur;

	last_opened = 0;

	opened = stress_get_open_count();
	if (opened == 0) {
		size_t i;

		/* Determine max number of free file descriptors we have */
		for (i = 0; i < max; i++) {
			if (fcntl((int)i, F_GETFL) > -1) {
				opened++;
				last_opened = i;
			} else {
				/*
				 *  Hack: Over 250 contiguously closed files
				 *  most probably indicates we're at the point
				 *  were no more opened file descriptors are
				 *  going to be found, so bail out rather then
				 *  scanning for any more opened files
				 */
				if (i - last_opened > 250)
					break;
			}
		}
	}
	return max - opened;
}

/*
 *  stress_get_bad_fd()
 *	return a fd that will produce -EINVAL when using it
 *	either because it is not open or it is just out of range
 */
int stress_get_bad_fd(void)
{
#if defined(RLIMIT_NOFILE) &&	\
    defined(F_GETFL)
	struct rlimit rlim;

	(void)shim_memset(&rlim, 0, sizeof(rlim));

	if (getrlimit(RLIMIT_NOFILE, &rlim) == 0) {
		if (rlim.rlim_cur < INT_MAX - 1) {
			if (fcntl((int)rlim.rlim_cur, F_GETFL) == -1) {
				return (int)rlim.rlim_cur + 1;
			}
		}
	}
#elif defined(F_GETFL)
	int i;

	for (i = 2048; i > fileno(stdout); i--) {
		if (fcntl((int)i, F_GETFL) == -1)
			return i;
	}
#else
	UNEXPECTED
#endif
	return -1;
}

/*
 *  stress_sigaltstack_no_check()
 *	attempt to set up an alternative signal stack with no
 *	minimum size check on stack
 *	  stack - must be at least MINSIGSTKSZ
 *	  size  - size of stack (- STACK_ALIGNMENT)
 */
int stress_sigaltstack_no_check(void *stack, const size_t size)
{
#if defined(HAVE_SIGALTSTACK)
	stack_t ss;

	if (stack == NULL) {
		ss.ss_sp = NULL;
		ss.ss_size = 0;
		ss.ss_flags = SS_DISABLE;
	} else {
		ss.ss_sp = (void *)stack;
		ss.ss_size = size;
		ss.ss_flags = 0;
	}
	return sigaltstack(&ss, NULL);
#else
	UNEXPECTED
	(void)stack;
	(void)size;
	return 0;
#endif
}

/*
 *  stress_sigaltstack()
 *	attempt to set up an alternative signal stack
 *	  stack - must be at least MINSIGSTKSZ
 *	  size  - size of stack (- STACK_ALIGNMENT)
 */
int stress_sigaltstack(void *stack, const size_t size)
{
#if defined(HAVE_SIGALTSTACK)
	if (stack && (size < (size_t)STRESS_MINSIGSTKSZ)) {
		pr_err("sigaltstack stack size %zu must be more than %zuK\n",
			size, (size_t)STRESS_MINSIGSTKSZ / 1024);
		return -1;
	}

	if (stress_sigaltstack_no_check(stack, size) < 0) {
		pr_fail("sigaltstack failed: errno=%d (%s)\n",
			errno, strerror(errno));
		return -1;
	}
#else
	UNEXPECTED
	(void)stack;
	(void)size;
#endif
	return 0;
}

/*
 *  stress_sigaltstack_disable()
 *	disable the alternative signal stack
 */
void stress_sigaltstack_disable(void)
{
#if defined(HAVE_SIGALTSTACK)
	stack_t ss;

	ss.ss_sp = NULL;
	ss.ss_size = 0;
	ss.ss_flags = SS_DISABLE;

	sigaltstack(&ss, NULL);
#endif
	return;
}

/*
 *  stress_sighandler()
 *	set signal handler in generic way
 */
int stress_sighandler(
	const char *name,
	const int signum,
	void (*handler)(int),
	struct sigaction *orig_action)
{
	struct sigaction new_action;
#if defined(HAVE_SIGALTSTACK)
	{
		static uint8_t *stack = NULL;

		if (stack == NULL) {
			/* Allocate stack, we currently leak this */
			stack = (uint8_t *)stress_mmap_populate(NULL, STRESS_SIGSTKSZ,
					PROT_READ | PROT_WRITE,
					MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
			if (stack == MAP_FAILED) {
				pr_inf("%s: sigaction %s: cannot allocated signal stack, "
					"errno=%d (%s)\n",
					name, stress_strsignal(signum),
					errno, strerror(errno));
				return -1;
			}
			stress_set_vma_anon_name(stack, STRESS_SIGSTKSZ, "sigstack");
			if (stress_sigaltstack(stack, STRESS_SIGSTKSZ) < 0)
				return -1;
		}
	}
#endif
	(void)shim_memset(&new_action, 0, sizeof new_action);
	new_action.sa_handler = handler;
	(void)sigemptyset(&new_action.sa_mask);
	new_action.sa_flags = SA_NOCLDSTOP;
#if defined(HAVE_SIGALTSTACK)
	new_action.sa_flags |= SA_ONSTACK;
#endif

	if (sigaction(signum, &new_action, orig_action) < 0) {
		pr_fail("%s: sigaction %s: errno=%d (%s)\n",
			name, stress_strsignal(signum), errno, strerror(errno));
		return -1;
	}
	return 0;
}

/*  stress_sigchld_helper_handler()
 *	parent is informed child has terminated and
 * 	it's time to stop
 */
static void MLOCKED_TEXT stress_sigchld_helper_handler(int signum)
{
	if (signum == SIGCHLD)
		stress_continue_set_flag(false);
}

/*
 *  stress_sigchld_set_handler()
 *	set sigchld handler
 */
int stress_sigchld_set_handler(stress_args_t *args)
{
	return stress_sighandler(args->name, SIGCHLD, stress_sigchld_helper_handler, NULL);
}

/*
 *  stress_sighandler_default
 *	restore signal handler to default handler
 */
int stress_sighandler_default(const int signum)
{
	struct sigaction new_action;

	(void)shim_memset(&new_action, 0, sizeof new_action);
	new_action.sa_handler = SIG_DFL;

	return sigaction(signum, &new_action, NULL);
}

/*
 *  stress_handle_stop_stressing()
 *	set flag to indicate to stressor to stop stressing
 */
void stress_handle_stop_stressing(const int signum)
{
	(void)signum;

	stress_continue_set_flag(false);
	/*
	 * Trigger another SIGARLM until stressor gets the message
	 * that it needs to terminate
	 */
	(void)alarm(1);
}

/*
 *  stress_sig_stop_stressing()
 *	install a handler that sets the global flag
 *	to indicate to a stressor to stop stressing
 */
int stress_sig_stop_stressing(const char *name, const int sig)
{
	return stress_sighandler(name, sig, stress_handle_stop_stressing, NULL);
}

/*
 *  stress_sigrestore()
 *	restore a handler
 */
int stress_sigrestore(
	const char *name,
	const int signum,
	struct sigaction *orig_action)
{
	if (sigaction(signum, orig_action, NULL) < 0) {
		pr_fail("%s: sigaction %s restore: errno=%d (%s)\n",
			name, stress_strsignal(signum), errno, strerror(errno));
		return -1;
	}
	return 0;
}

/*
 *  stress_get_cpu()
 *	get cpu number that process is currently on
 */
unsigned int stress_get_cpu(void)
{
#if defined(HAVE_SCHED_GETCPU)
#if defined(__PPC64__) ||	\
    defined(__s390x__)
	unsigned int cpu, node;

	if (UNLIKELY(shim_getcpu(&cpu, &node, NULL) < 0))
		return 0;
	return cpu;
#else
	const int cpu = sched_getcpu();

	return (unsigned int)((cpu < 0) ? 0 : cpu);
#endif
#else
	unsigned int cpu, node;

	if (UNLIKELY(shim_getcpu(&cpu, &node, NULL) < 0))
		return 0;
	return cpu;
#endif
}

#define XSTRINGIFY(s) STRINGIFY(s)
#define STRINGIFY(s) #s

/*
 *  stress_get_compiler()
 *	return compiler info
 */
const char PURE *stress_get_compiler(void)
{
#if   defined(HAVE_COMPILER_ICC) &&	\
      defined(__INTEL_COMPILER) &&	\
      defined(__INTEL_COMPILER_UPDATE) && \
      defined(__INTEL_COMPILER_BUILD_DATE)
	static const char cc[] = "icc " XSTRINGIFY(__INTEL_COMPILER) "." XSTRINGIFY(__INTEL_COMPILER_UPDATE) " Build " XSTRINGIFY(__INTEL_COMPILER_BUILD_DATE) "";
#elif defined(HAVE_COMPILER_ICC) && 		\
      defined(__INTEL_COMPILER) &&	\
      defined(__INTEL_COMPILER_UPDATE)
	static const char cc[] = "icc " XSTRINGIFY(__INTEL_COMPILER) "." XSTRINGIFY(__INTEL_COMPILER_UPDATE) "";
#elif defined(__INTEL_CLANG_COMPILER)
	static const char cc[] = "icx " XSTRINGIFY(__INTEL_CLANG_COMPILER) "";
#elif defined(__INTEL_LLVM_COMPILER)
	static const char cc[] = "icx " XSTRINGIFY(__INTEL_LLVM_COMPILER) "";
#elif defined(__TINYC__)
	static const char cc[] = "tcc " XSTRINGIFY(__TINYC__) "";
#elif defined(__PCC__) &&			\
       defined(__PCC_MINOR__)
	static const char cc[] = "pcc " XSTRINGIFY(__PCC__) "." XSTRINGIFY(__PCC_MINOR__) "." XSTRINGIFY(__PCC_MINORMINOR__) "";
#elif defined(__clang_major__) &&	\
      defined(__clang_minor__) &&	\
      defined(__clang_patchlevel__)
	static const char cc[] = "clang " XSTRINGIFY(__clang_major__) "." XSTRINGIFY(__clang_minor__) "." XSTRINGIFY(__clang_patchlevel__) "";
#elif defined(__clang_major__) &&	\
      defined(__clang_minor__)
	static const char cc[] = "clang " XSTRINGIFY(__clang_major__) "." XSTRINGIFY(__clang_minor__) "";
#elif defined(__GNUC__) &&		\
      defined(__GNUC_MINOR__) &&	\
      defined(__GNUC_PATCHLEVEL__) &&	\
      defined(HAVE_COMPILER_MUSL)
	static const char cc[] = "musl-gcc " XSTRINGIFY(__GNUC__) "." XSTRINGIFY(__GNUC_MINOR__) "." XSTRINGIFY(__GNUC_PATCHLEVEL__) "";
#elif defined(__GNUC__) &&		\
      defined(__GNUC_MINOR__) &&	\
      defined(HAVE_COMPILER_MUSL)
	static const char cc[] = "musl-gcc " XSTRINGIFY(__GNUC__) "." XSTRINGIFY(__GNUC_MINOR__) "";
#elif defined(__GNUC__) &&		\
      defined(__GNUC_MINOR__) &&	\
      defined(__GNUC_PATCHLEVEL__) &&	\
      defined(HAVE_COMPILER_GCC)
	static const char cc[] = "gcc " XSTRINGIFY(__GNUC__) "." XSTRINGIFY(__GNUC_MINOR__) "." XSTRINGIFY(__GNUC_PATCHLEVEL__) "";
#elif defined(__GNUC__) &&		\
      defined(__GNUC_MINOR__) &&	\
      defined(HAVE_COMPILER_GCC)
	static const char cc[] = "gcc " XSTRINGIFY(__GNUC__) "." XSTRINGIFY(__GNUC_MINOR__) "";
#else
	static const char cc[] = "cc unknown";
#endif
	return cc;
}

/*
 *  stress_get_uname_info()
 *	return uname information
 */
const char *stress_get_uname_info(void)
{
#if defined(HAVE_UNAME) &&	\
    defined(HAVE_SYS_UTSNAME_H)
	struct utsname buf;

	if (LIKELY(uname(&buf) >= 0)) {
		static char str[sizeof(buf.machine) +
	                        sizeof(buf.sysname) +
				sizeof(buf.release) + 3];

		(void)snprintf(str, sizeof(str), "%s %s %s", buf.machine, buf.sysname, buf.release);
		return str;
	}
#else
	UNEXPECTED
#endif
	return "unknown";
}

/*
 *  stress_unimplemented()
 *	report that a stressor is not implemented
 *	on a particular arch or kernel
 */
int PURE stress_unimplemented(stress_args_t *args)
{
	(void)args;

	return EXIT_NOT_IMPLEMENTED;
}

#if defined(F_SETPIPE_SZ)
/*
 *  stress_check_max_pipe_size()
 *	check if the given pipe size is allowed
 */
static inline int stress_check_max_pipe_size(
	const size_t sz,
	const size_t page_size)
{
	int fds[2], rc = 0;

	if (UNLIKELY(sz < page_size))
		return -1;

	if (UNLIKELY(pipe(fds) < 0))
		return -1;

	if (fcntl(fds[0], F_SETPIPE_SZ, sz) < 0)
		rc = -1;

	(void)close(fds[0]);
	(void)close(fds[1]);
	return rc;
}
#endif

/*
 *  stress_probe_max_pipe_size()
 *	determine the maximum allowed pipe size
 */
size_t stress_probe_max_pipe_size(void)
{
	static size_t max_pipe_size;

#if defined(F_SETPIPE_SZ)
	ssize_t ret;
	size_t i, prev_sz, sz, min, max;
	char buf[64];
	size_t page_size;
#endif
	/* Already determined? returned cached size */
	if (max_pipe_size)
		return max_pipe_size;

#if defined(F_SETPIPE_SZ)
	page_size = stress_get_page_size();

	/*
	 *  Try and find maximum pipe size directly
	 */
	ret = stress_system_read("/proc/sys/fs/pipe-max-size", buf, sizeof(buf));
	if (ret > 0) {
		if (sscanf(buf, "%zu", &sz) == 1)
			if (!stress_check_max_pipe_size(sz, page_size))
				goto ret;
	}

	/*
	 *  Need to find size by binary chop probing
	 */
	min = page_size;
	max = INT_MAX;
	prev_sz = 0;
	sz = 0;
	for (i = 0; i < 64; i++) {
		sz = min + (max - min) / 2;
		if (prev_sz == sz)
			return sz;
		prev_sz = sz;
		if (stress_check_max_pipe_size(sz, page_size) == 0) {
			min = sz;
		} else {
			max = sz;
		}
	}
ret:
	max_pipe_size = sz;
#else
	max_pipe_size = stress_get_page_size();
#endif
	return max_pipe_size;
}

/*
 *  stress_align_address
 *	align address to alignment, alignment MUST be a power of 2
 */
void PURE *stress_align_address(const void *addr, const size_t alignment)
{
	const uintptr_t uintptr =
		((uintptr_t)addr + alignment) & ~(alignment - 1);

	return (void *)uintptr;
}

/*
 *  stress_sigalrm_pending()
 *	return true if SIGALRM is pending
 */
bool stress_sigalrm_pending(void)
{
	sigset_t set;

	(void)sigemptyset(&set);
	(void)sigpending(&set);
	return sigismember(&set, SIGALRM);

}

/*
 *  stress_uint64_to_str()
 *	turn 64 bit size to human readable string
 */
char *stress_uint64_to_str(char *str, size_t len, const uint64_t val)
{
	typedef struct {
		const uint64_t size;
		const char *suffix;
	} stress_size_info_t;

	static const stress_size_info_t size_info[] = {
		{ EB, "E" },
		{ PB, "P" },
		{ TB, "T" },
		{ GB, "G" },
		{ MB, "M" },
		{ KB, "K" },
	};
	size_t i;
	const char *suffix = "";
	uint64_t scale = 1;

	if (UNLIKELY((!str) || (len < 1)))
		return str;

	for (i = 0; i < SIZEOF_ARRAY(size_info); i++) {
		const uint64_t scaled = val / size_info[i].size;

		if ((scaled >= 1) && (scaled < 1024)) {
			suffix = size_info[i].suffix;
			scale = size_info[i].size;
			break;
		}
	}

	(void)snprintf(str, len, "%.1f%s", (double)val / (double)scale, suffix);

	return str;
}

/*
 *  stress_check_root()
 *	returns true if root
 */
static inline bool stress_check_root(void)
{
	if (geteuid() == 0)
		return true;

#if defined(__CYGWIN__)
	{
		/*
		 * Cygwin would only return uid 0 if the Windows user is mapped
		 * to this uid by a custom /etc/passwd file.  Regardless of uid,
		 * a process has administrator privileges if the local
		 * administrator group (S-1-5-32-544) is present in the process
		 * token.  By default, Cygwin maps this group to gid 544 but it
		 * may be mapped to gid 0 by a custom /etc/group file.
		 */
		gid_t *gids;
		long int gids_max;
		int ngids;

#if defined(_SC_NGROUPS_MAX)
		gids_max = sysconf(_SC_NGROUPS_MAX);
		if ((gids_max < 0) || (gids_max > 65536))
			gids_max = 65536;
#else
		gids_max = 65536;
#endif
		gids = (gid_t *)calloc((size_t)gids_max, sizeof(*gids));
		if (!gids)
			return false;

		ngids = getgroups((int)gids_max, gids);
		if (ngids > 0) {
			int i;

			for (i = 0; i < ngids; i++) {
				if ((gids[i] == 0) || (gids[i] == 544)) {
					free(gids);
					return true;
				}
			}
		}
		free(gids);
	}
#endif
	return false;
}

#if defined(HAVE_SYS_CAPABILITY_H)
void stress_getset_capability(void)
{
	struct __user_cap_header_struct uch;
	struct __user_cap_data_struct ucd[_LINUX_CAPABILITY_U32S_3];

	uch.version = _LINUX_CAPABILITY_VERSION_3;
	uch.pid = getpid();

	if (capget(&uch, ucd) < 0)
		return;
	(void)capset(&uch, ucd);
}
#endif

#if defined(HAVE_SYS_CAPABILITY_H)
/*
 *  stress_check_capability()
 *	returns true if process has the given capability,
 *	if capability is SHIM_CAP_IS_ROOT then just check if process is
 *	root.
 */
bool stress_check_capability(const int capability)
{
	int ret;
	struct __user_cap_header_struct uch;
	struct __user_cap_data_struct ucd[_LINUX_CAPABILITY_U32S_3];
	uint32_t mask;
	size_t idx;

	if (capability == SHIM_CAP_IS_ROOT)
		return stress_check_root();

	(void)shim_memset(&uch, 0, sizeof uch);
	(void)shim_memset(ucd, 0, sizeof ucd);

	uch.version = _LINUX_CAPABILITY_VERSION_3;
	uch.pid = getpid();

	ret = capget(&uch, ucd);
	if (ret < 0)
		return stress_check_root();

	idx = (size_t)CAP_TO_INDEX(capability);
	mask = CAP_TO_MASK(capability);

	return (ucd[idx].permitted &= mask) ? true : false;
}
#else
bool stress_check_capability(const int capability)
{
	(void)capability;

	return stress_check_root();
}
#endif

/*
 *  stress_drop_capabilities()
 *	drop all capabilities and disable any new privileges
 */
#if defined(HAVE_SYS_CAPABILITY_H)
int stress_drop_capabilities(const char *name)
{
	int ret;
	uint32_t i;
	struct __user_cap_header_struct uch;
	struct __user_cap_data_struct ucd[_LINUX_CAPABILITY_U32S_3];

	(void)shim_memset(&uch, 0, sizeof uch);
	(void)shim_memset(ucd, 0, sizeof ucd);

	uch.version = _LINUX_CAPABILITY_VERSION_3;
	uch.pid = getpid();

	ret = capget(&uch, ucd);
	if (UNLIKELY(ret < 0)) {
		pr_fail("%s: capget on PID %" PRIdMAX " failed: errno=%d (%s)\n",
			name, (intmax_t)uch.pid, errno, strerror(errno));
		return -1;
	}

	/*
	 *  We could just memset ucd to zero, but
	 *  lets explicitly set all the capability
	 *  bits to zero to show the intent
	 */
	for (i = 0; i <= CAP_LAST_CAP; i++) {
		const uint32_t idx = CAP_TO_INDEX(i);
		const uint32_t mask = CAP_TO_MASK(i);

		ucd[idx].inheritable &= ~mask;
		ucd[idx].permitted &= ~mask;
		ucd[idx].effective &= ~mask;
	}

	ret = capset(&uch, ucd);
	if (UNLIKELY(ret < 0)) {
		pr_fail("%s: capset on PID %" PRIdMAX " failed: errno=%d (%s)\n",
			name, (intmax_t)uch.pid, errno, strerror(errno));
		return -1;
	}
#if defined(HAVE_PRCTL) &&		\
    defined(HAVE_SYS_PRCTL_H) &&	\
    defined(PR_SET_NO_NEW_PRIVS)
	ret = prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0);
	if (UNLIKELY(ret < 0)) {
		/* Older kernels that don't support this prctl throw EINVAL */
		if (errno != EINVAL) {
			pr_inf("%s: prctl PR_SET_NO_NEW_PRIVS on PID %" PRIdMAX " failed: "
				"errno=%d (%s)\n",
				name, (intmax_t)uch.pid, errno, strerror(errno));
		}
		return -1;
	}
#endif
	return 0;
}
#else
int stress_drop_capabilities(const char *name)
{
	(void)name;

	return 0;
}
#endif

/*
 *  stress_is_dot_filename()
 *	is filename "." or ".."
 */
bool PURE stress_is_dot_filename(const char *name)
{
	if (UNLIKELY(!name))
		return false;
	if (!strcmp(name, "."))
		return true;
	if (!strcmp(name, ".."))
		return true;
	return false;
}

/*
 *  stress_const_optdup(const char *opt)
 *	duplicate a modifiable copy of a const option string opt
 */
char *stress_const_optdup(const char *opt)
{
	char *str;

	if (UNLIKELY(!opt))
		return NULL;

	str = strdup(opt);
	if (!str)
		(void)fprintf(stderr, "out of memory duplicating option '%s'\n", opt);

	return str;
}

/*
 *  stress_get_exec_text_addr()
 *	return length and start/end addresses of text segment
 */
size_t stress_exec_text_addr(char **start, char **end)
{
#if defined(HAVE_EXECUTABLE_START)
	extern char __executable_start;
	intptr_t text_start = (intptr_t)&__executable_start;
#elif defined(__APPLE__)
	extern char _mh_execute_header;
	intptr_t text_start = (intptr_t)&_mh_execute_header;
#elif defined(__OpenBSD__)
	extern char _start[];
	intptr_t text_start = (intptr_t)&_start[0];
#elif defined(HAVE_COMPILER_TCC)
	extern char _start;
	intptr_t text_start = (intptr_t)&_start;
#elif defined(__CYGWIN__)
	extern char WinMainCRTStartup;
	intptr_t text_start = (intptr_t)&WinMainCRTStartup;
#else
	extern char _start;
	intptr_t text_start = (intptr_t)&_start;
#endif

#if defined(__APPLE__)
	extern void *get_etext(void);
	intptr_t text_end = (intptr_t)get_etext();
#elif defined(HAVE_COMPILER_TCC)
	extern char _etext;
	intptr_t text_end = (intptr_t)&_etext;
#else
	extern char etext;
	intptr_t text_end = (intptr_t)&etext;
#endif
	if (UNLIKELY(text_end <= text_start))
		return 0;

	if (UNLIKELY((start == NULL) || (end == NULL) || (text_start >= text_end)))
		return 0;

	*start = (char *)text_start;
	*end = (char *)text_end;
	return (size_t)(text_end - text_start);
}

/*
 *  stress_is_dev_tty()
 *	return true if fd is on a /dev/ttyN device. If it can't
 *	be determined than default to assuming it is.
 */
bool stress_is_dev_tty(const int fd)
{
#if defined(HAVE_TTYNAME)
	const char *name = ttyname(fd);

	if (UNLIKELY(!name))
		return true;
	return !strncmp("/dev/tty", name, 8);
#else
	UNEXPECTED
	(void)fd;

	/* Assume it is */
	return true;
#endif
}

/*
 *  stress_dirent_list_free()
 *	free dirent list
 */
void stress_dirent_list_free(struct dirent **dlist, const int n)
{
	if (LIKELY(dlist != NULL)) {
		int i;

		for (i = 0; i < n; i++) {
			if (dlist[i])
				free(dlist[i]);
		}
		free(dlist);
	}
}

/*
 *  stress_dirent_list_prune()
 *	remove . and .. files from directory list
 */
int stress_dirent_list_prune(struct dirent **dlist, const int n)
{
	int i, j;

	if (UNLIKELY(!dlist))
		return -1;

	for (i = 0, j = 0; i < n; i++) {
		if (dlist[i]) {
			if (stress_is_dot_filename(dlist[i]->d_name)) {
				free(dlist[i]);
				dlist[i] = NULL;
			} else {
				dlist[j] = dlist[i];
				j++;
			}
		}
	}
	return j;
}

/*
 *  stress_warn_once_hash()
 *	computes a hash for a filename and a line and stores it,
 *	returns true if this is the first time this has been
 *	called for that specific filename and line
 *
 *	Without libpthread this is potentially racy.
 */
bool stress_warn_once_hash(const char *filename, const int line)
{
	uint32_t free_slot, i, j, h = (stress_hash_pjw(filename) + (uint32_t)line);
	bool not_warned_yet = true;

	if (UNLIKELY(!g_shared))
		return true;

	if (stress_lock_acquire(g_shared->warn_once.lock) < 0)
		return true;
	free_slot = STRESS_WARN_HASH_MAX;

	/*
	 * Ensure hash is never zero so that it does not
	 * match and empty slot value of zero
	 */
	if (h == 0)
		h += STRESS_WARN_HASH_MAX;

	j = h % STRESS_WARN_HASH_MAX;
	for (i = 0; i < STRESS_WARN_HASH_MAX; i++) {
		if (g_shared->warn_once.hash[j] == h) {
			not_warned_yet = false;
			goto unlock;
		}
		if ((free_slot == STRESS_WARN_HASH_MAX) &&
		    (g_shared->warn_once.hash[j] == 0)) {
			free_slot = j;
		}
		j = (j + 1) % STRESS_WARN_HASH_MAX;
	}
	if (free_slot != STRESS_WARN_HASH_MAX) {
		g_shared->warn_once.hash[free_slot] = h;
	}
unlock:
	stress_lock_release(g_shared->warn_once.lock);

	return not_warned_yet;
}

/*
 *  stress_ipv4_checksum()
 *	ipv4 data checksum
 */
uint16_t PURE OPTIMIZE3 stress_ipv4_checksum(uint16_t *ptr, const size_t sz)
{
	register uint32_t sum = 0;
	register size_t n = sz;

	if (UNLIKELY(!ptr))
		return 0;

	while (n > 1) {
		sum += *ptr++;
		n -= 2;
	}

	if (n)
		sum += *(uint8_t*)ptr;
	sum = (sum >> 16) + (sum & 0xffff);
	sum += (sum >> 16);

	return (uint16_t)~sum;
}

/*
 *  stress_uid_comp()
 *	uid comparison for sorting
 */
#if defined(HAVE_SETPWENT) &&	\
    defined(HAVE_GETPWENT) &&	\
    defined(HAVE_ENDPWENT) &&	\
    !defined(BUILD_STATIC)
static PURE int stress_uid_comp(const void *p1, const void *p2)
{
	const uid_t *uid1 = (const uid_t *)p1;
	const uid_t *uid2 = (const uid_t *)p2;

	if (*uid1 > *uid2)
		return 1;
	else if (*uid1 < *uid2)
		return -1;
	else
		return 0;
}

/*
 *  stress_get_unused_uid()
 *	find the lowest free unused UID greater than 250,
 *	returns -1 if it can't find one and uid is set to 0;
 *      if successful it returns 0 and sets uid to the free uid.
 *
 *	This also caches the uid so this can be called
 *	frequently. If the cached uid is in use it will
 *	perform the expensive lookup again.
 */
int stress_get_unused_uid(uid_t *uid)
{
	static uid_t cached_uid = 0;
	uid_t *uids;

	if (!uid)
		return -1;
	*uid = 0;

	/*
	 *  If we have a cached unused uid and it's no longer
	 *  unused then force a rescan for a new one
	 */
	if ((cached_uid != 0) && (getpwuid(cached_uid) != NULL))
		cached_uid = 0;

	if (cached_uid == 0) {
		struct passwd *pw;
		size_t i, n;

		setpwent();
		for (n = 0; getpwent() != NULL; n++) {
		}
		endpwent();

		uids = (uid_t *)calloc(n, sizeof(*uids));
		if (!uids)
			return -1;

		setpwent();
		for (i = 0; i < n && (pw = getpwent()) != NULL; i++) {
			uids[i] = pw->pw_uid;
		}
		endpwent();
		n = i;

		qsort(uids, n, sizeof(*uids), stress_uid_comp);

		/* Look for a suitable gap from uid 250 upwards */
		for (i = 0; i < n - 1; i++) {
			/*
			 *  Add a large gap in case new uids
			 *  are added to reduce free uid race window
			 */
			const uid_t uid_try = uids[i] + 250;

			if (uids[i + 1] > uid_try) {
				if (getpwuid(uid_try) == NULL) {
					cached_uid = uid_try;
					break;
				}
			}
		}
		free(uids);
	}

	/*
	 *  Not found?
	 */
	if (cached_uid == 0)
		return -1;

	*uid = cached_uid;

	return 0;
}
#else
int stress_get_unused_uid(uid_t *uid)
{
	if (uid)
		*uid = 0;

	return -1;
}
#endif

/*
 *  stress_read_discard(cont int fd)
 *	read and discard contents of file fd
 */
ssize_t stress_read_discard(const int fd)
{
	ssize_t rbytes = 0, ret;

	do {
		char buffer[4096];

		ret = read(fd, buffer, sizeof(buffer));
		if (ret > 0)
			rbytes += ret;
	} while (ret > 0);

	return rbytes;
}

/*
 *  stress_read_buffer()
 *	In addition to read() this function makes sure all bytes have been
 *	read. You're also able to ignore EINTR signals which could happen
 *	on alarm() in the parent process.
 */
ssize_t stress_read_buffer(
	const int fd,
	void *buffer,
	const ssize_t size,
	const bool ignore_sig_eintr)
{
	ssize_t rbytes = 0, ret;

	if (UNLIKELY(!buffer || (size < 1)))
		return -1;
	do {
		char *ptr = ((char *)buffer) + rbytes;
ignore_eintr:
		ret = read(fd, (void *)ptr, (size_t)(size - rbytes));
		if (ignore_sig_eintr && (ret < 0) && (errno == EINTR))
			goto ignore_eintr;
		if (ret > 0)
			rbytes += ret;
	} while ((ret > 0) && (rbytes != size));

	return (ret <= 0) ? ret : rbytes;
}

/*
 *  stress_write_buffer()
 *	In addition to write() this function makes sure all bytes have been
 *	written. You're also able to ignore EINTR interrupts which could happen
 *	on alarm() in the parent process.
 */
ssize_t stress_write_buffer(
	const int fd,
	const void *buffer,
	const ssize_t size,
	const bool ignore_sig_eintr)
{
	ssize_t wbytes = 0, ret;

	if (UNLIKELY(!buffer || (size < 1)))
		return -1;

	do {
		const void *ptr = (void *)((uintptr_t)buffer + wbytes);
ignore_eintr:
		ret = write(fd, ptr, (size_t)(size - wbytes));
		/* retry if interrupted */
		if (ignore_sig_eintr && (ret < 0) && (errno == EINTR))
			goto ignore_eintr;
		if (ret > 0)
			wbytes += ret;
	} while ((ret > 0) && (wbytes != size));

	return (ret <= 0) ? ret : wbytes;
}

/*
 *  stress_kernel_release()
 *	turn release major.minor.patchlevel triplet into base 100 value
 */
int PURE stress_kernel_release(const int major, const int minor, const int patchlevel)
{
	return (major * 10000) + (minor * 100) + patchlevel;
}

/*
 *  stress_get_kernel_release()
 *	return kernel release number in base 100, e.g.
 *	 4.15.2 -> 401502, return -1 if failed.
 */
int stress_get_kernel_release(void)
{
#if defined(HAVE_UNAME) &&	\
    defined(HAVE_SYS_UTSNAME_H)
	struct utsname buf;
	int major = 0, minor = 0, patchlevel = 0;

	if (UNLIKELY(uname(&buf) < 0))
		return -1;

	if (sscanf(buf.release, "%d.%d.%d\n", &major, &minor, &patchlevel) < 1)
		return -1;

	return stress_kernel_release(major, minor, patchlevel);
#else
	UNEXPECTED
	return -1;
#endif
}

/*
 *  stress_get_unused_pid_racy()
 *	try to find an unused pid. This is racy and may actually
 *	return pid that is unused at test time but will become
 *	used by the time the pid is accessed.
 */
pid_t stress_get_unused_pid_racy(const bool fork_test)
{
#if defined(PID_MAX_LIMIT)
	pid_t max_pid = STRESS_MAXIMUM(PID_MAX_LIMIT, 1024);
#elif defined(PID_MAX)
	pid_t max_pid = STRESS_MAXIMUM(PID_MAX, 1024);
#elif defined(PID_MAX_DEFAULT)
	pid_t max_pid = STRESS_MAXIMUM(PID_MAX_DEFAULT, 1024);
#else
	pid_t max_pid = 32767;
#endif
	int i;
	pid_t pid;
	uint32_t n;
	char buf[64];

	/*
	 *  Create a child, terminate it, use this pid as an unused
	 *  pid. Slow but should be OK if system doesn't recycle PIDs
	 *  quickly.
	 */
	if (fork_test) {
		pid = fork();
		if (pid == 0) {
			_exit(0);
		} else if (pid > 0) {
			int status, ret;

			ret = waitpid(pid, &status, 0);
			if ((ret == pid) &&
			    ((shim_kill(pid, 0) < 0) && (errno == ESRCH))) {
				return pid;
			}
		}
	}

	/*
	 *  Make a random PID guess.
	 */
	n = (uint32_t)max_pid - 1023;
	for (i = 0; i < 10; i++) {
		pid = (pid_t)stress_mwc32modn(n) + 1023;

		if ((shim_kill(pid, 0) < 0) && (errno == ESRCH))
			return pid;
	}

	(void)shim_memset(buf, 0, sizeof(buf));
	if (stress_system_read("/proc/sys/kernel/pid_max", buf, sizeof(buf) - 1) > 0)
		max_pid = STRESS_MAXIMUM(atoi(buf), 1024);

	n = (uint32_t)max_pid - 1023;
	for (i = 0; i < 10; i++) {
		pid = (pid_t)stress_mwc32modn(n) + 1023;

		if ((shim_kill(pid, 0) < 0) && (errno == ESRCH))
			return pid;
	}

	/*
	 *  Give up.
	 */
	return max_pid;
}

/*
 *  stress_read_fdinfo()
 *	read the fdinfo for a specific pid's fd, Linux only
 */
int stress_read_fdinfo(const pid_t pid, const int fd)
{
#if defined(__linux__)
	char path[PATH_MAX];
	char buf[4096];

	(void)snprintf(path, sizeof(path), "/proc/%d/fdinfo/%d",
		(int)pid, fd);

	return (int)stress_system_read(path, buf, sizeof(buf));
#else
	(void)pid;
	(void)fd;

	return 0;
#endif
}

/*
 *  stress_get_hostname_length()
 *	return the maximum allowed hostname length
 */
size_t stress_get_hostname_length(void)
{
#if defined(HOST_NAME_MAX)
	return HOST_NAME_MAX + 1;
#elif defined(HAVE_UNAME) && \
      defined(HAVE_SYS_UTSNAME_H)
	struct utsname uts;

	return sizeof(uts.nodename);	/* Linux */
#else
	return 255 + 1;			/* SUSv2 */
#endif
}

/*
 *  stress_get_min_aux_sig_stack_size()
 *	For ARM we should check AT_MINSIGSTKSZ as this
 *	also includes SVE register saving overhead
 *	https://blog.linuxplumbersconf.org/2017/ocw/system/presentations/4671/original/plumbers-dm-2017.pdf
 */
static inline long int stress_get_min_aux_sig_stack_size(void)
{
#if defined(HAVE_SYS_AUXV_H) && \
    defined(HAVE_GETAUXVAL) &&	\
    defined(AT_MINSIGSTKSZ)
	const long int sz = (long int)getauxval(AT_MINSIGSTKSZ);

	if (LIKELY(sz > 0))
		return sz;
#else
	UNEXPECTED
#endif
	return -1;
}

/*
 *  stress_get_sig_stack_size()
 *	wrapper for STRESS_SIGSTKSZ, try and find
 *	stack size required
 */
size_t stress_get_sig_stack_size(void)
{
	static long int sz = -1;
	long int min;
#if defined(_SC_SIGSTKSZ) ||	\
    defined(SIGSTKSZ)
	long int tmp;
#endif

	/* return cached copy */
	if (LIKELY(sz > 0))
		return (size_t)sz;

	min = stress_get_min_aux_sig_stack_size();
#if defined(_SC_SIGSTKSZ)
	tmp = sysconf(_SC_SIGSTKSZ);
	if (tmp > 0)
		min = STRESS_MAXIMUM(tmp, min);
#endif
#if defined(SIGSTKSZ)
	tmp = SIGSTKSZ;
	if (tmp > 0)
		min = STRESS_MAXIMUM(tmp, min);
#endif
	sz = STRESS_MAXIMUM(STRESS_ABS_MIN_STACK_SIZE, min);
	return (size_t)sz;
}

/*
 *  stress_get_min_sig_stack_size()
 *	wrapper for STRESS_MINSIGSTKSZ
 */
size_t stress_get_min_sig_stack_size(void)
{
	static long int sz = -1;
	long int min;
#if defined(_SC_MINSIGSTKSZ) ||	\
    defined(SIGSTKSZ)
	long int tmp;
#endif

	/* return cached copy */
	if (sz > 0)
		return (size_t)sz;

	min = stress_get_min_aux_sig_stack_size();
#if defined(_SC_MINSIGSTKSZ)
	tmp = sysconf(_SC_MINSIGSTKSZ);
	if (tmp > 0)
		min = STRESS_MAXIMUM(tmp, min);
#endif
#if defined(SIGSTKSZ)
	tmp = SIGSTKSZ;
	if (tmp > 0)
		min = STRESS_MAXIMUM(tmp, min);
#endif
	sz = STRESS_MAXIMUM(STRESS_ABS_MIN_STACK_SIZE, min);
	return (size_t)sz;
}

/*
 *  stress_get_min_pthread_stack_size()
 *	return the minimum size of stack for a pthread
 */
size_t stress_get_min_pthread_stack_size(void)
{
	static long int sz = -1;
	long int min, tmp;

	/* return cached copy */
	if (sz > 0)
		return (size_t)sz;

	min = stress_get_min_aux_sig_stack_size();
#if defined(__SC_THREAD_STACK_MIN_VALUE)
	tmp = sysconf(__SC_THREAD_STACK_MIN_VALUE);
	if (tmp > 0)
		min = STRESS_MAXIMUM(tmp, min);
#endif
#if defined(_SC_THREAD_STACK_MIN_VALUE)
	tmp = sysconf(_SC_THREAD_STACK_MIN_VALUE);
	if (tmp > 0)
		min = STRESS_MAXIMUM(tmp, min);
#endif
#if defined(PTHREAD_STACK_MIN)
	tmp = PTHREAD_STACK_MIN;
	if (tmp > 0)
		tmp = STRESS_MAXIMUM(tmp, 8192);
	else
		tmp = 8192;
#else
	tmp = 8192;
#endif
	sz = STRESS_MAXIMUM(tmp, min);
	return (size_t)sz;
}

/*
 *  stress_sig_handler_exit()
 *	signal handler that exits a process via _exit(0) for
 *	immediate dead stop termination.
 */
void NORETURN MLOCKED_TEXT stress_sig_handler_exit(int signum)
{
	(void)signum;

	_exit(0);
}

/*
 *  __stack_chk_fail()
 *	override stack smashing callback
 */
#if defined(HAVE_COMPILER_GCC_OR_MUSL) &&	\
    !defined(HAVE_COMPILER_CLANG) &&		\
    defined(HAVE_WEAK_ATTRIBUTE)
extern void __stack_chk_fail(void);

NORETURN WEAK void __stack_chk_fail(void)
{
	if (stress_stack_check_flag) {
		(void)fprintf(stderr, "Stack overflow detected! Aborting stress-ng.\n");
		(void)fflush(stderr);
		abort();
	}
	/* silently exit */
	_exit(0);
}
#endif

/*
 *  stress_set_stack_smash_check_flag()
 *	set flag, true = report flag, false = silently ignore
 */
void stress_set_stack_smash_check_flag(const bool flag)
{
	stress_stack_check_flag = flag;
}

/*
 *  stress_get_tty_width()
 *	get tty column width
 */
int stress_get_tty_width(void)
{
	const int max_width = 80;
#if defined(HAVE_WINSIZE) &&	\
    defined(TIOCGWINSZ)
	struct winsize ws;
	int ret;

	ret = ioctl(fileno(stdout), TIOCGWINSZ, &ws);
	if (UNLIKELY(ret < 0))
		return max_width;
	ret = (int)ws.ws_col;
	if (UNLIKELY((ret <= 0) || (ret > 1024)))
		return max_width;
	return ret;
#else
	UNEXPECTED
	return max_width;
#endif
}

/*
 *  stress_get_extents()
 *	try to determine number extents in a file
 */
size_t stress_get_extents(const int fd)
{
#if defined(FS_IOC_FIEMAP) &&	\
    defined(HAVE_LINUX_FIEMAP_H)
	struct fiemap fiemap;

	(void)shim_memset(&fiemap, 0, sizeof(fiemap));
	fiemap.fm_length = ~0UL;

	/* Find out how many extents there are */
	if (ioctl(fd, FS_IOC_FIEMAP, &fiemap) < 0)
		return 0;

	return fiemap.fm_mapped_extents;
#else
	UNEXPECTED
	(void)fd;

	return 0;
#endif
}

/*
 *  stress_redo_fork()
 *	check fork errno (in err) and return true if
 *	an immediate fork can be retried due to known
 *	error cases that are retryable. Also force a
 *	scheduling yield.
 */
bool stress_redo_fork(stress_args_t *args, const int err)
{
	/* Timed out! */
	if (UNLIKELY(stress_time_now() > args->time_end)) {
		stress_continue_set_flag(false);
		return false;
	}
	/* More bogo-ops to go and errors indicate a fork retry? */
	if (LIKELY(stress_continue(args)) &&
	    ((err == EAGAIN) || (err == EINTR) || (err == ENOMEM))) {
		(void)shim_sched_yield();
		return true;
	}
	return false;
}

/*
 *  stress_sighandler_nop()
 *	no-operation signal handler
 */
void stress_sighandler_nop(int sig)
{
	(void)sig;
}

/*
 *  stress_clear_warn_once()
 *	clear the linux warn once warnings flag, kernel warn once
 *	messages can be re-issued
 */
void stress_clear_warn_once(void)
{
#if defined(__linux__)
	if (stress_check_capability(SHIM_CAP_IS_ROOT))
		(void)stress_system_write("/sys/kernel/debug/clear_warn_once", "1", 1);
#endif
}

/*
 *  stress_flag_permutation()
 *	given flag mask in flags, generate all possible permutations
 *	of bit flags. e.g.
 *		flags = 0x81;
 *			-> b00000000
 *			   b00000001
 *			   b10000000
 *			   b10000001
 */
size_t stress_flag_permutation(const int flags, int **permutations)
{
	unsigned int flag_bits;
	unsigned int n_bits;
	register unsigned int j, n_flags;
	int *perms;

	if (UNLIKELY(!permutations))
		return 0;

	*permutations = NULL;

	for (n_bits = 0, flag_bits = (unsigned int)flags; flag_bits; flag_bits >>= 1U)
		n_bits += (flag_bits & 1U);

	if (n_bits > STRESS_MAX_PERMUTATIONS)
		n_bits = STRESS_MAX_PERMUTATIONS;

	n_flags = 1U << n_bits;
	perms = (int *)calloc((size_t)n_flags, sizeof(*perms));
	if (!perms)
		return 0;

	/*
	 *  Generate all the possible flag settings in order
	 */
	for (j = 0; j < n_flags; j++) {
		register int i;
		register unsigned int j_mask = 1U;

		for (i = 0; i < 32; i++) {
			const int i_mask = (int)(1U << i);

			if (flags & i_mask) {
				if (j & j_mask)
					perms[j] |= i_mask;
				j_mask <<= 1U;
			}
		}
	}
	*permutations = perms;
	return (size_t)n_flags;
}

#if defined(HAVE_LINUX_MAGIC_H) &&	\
    defined(HAVE_SYS_STATFS_H)
/*
 *  stress_fs_magic_to_name()
 *	return the human readable file system type based on fs type magic
 */
static const char *stress_fs_magic_to_name(const unsigned long int fs_magic)
{
	static char unknown[32];
	size_t i;

	for (i = 0; i < SIZEOF_ARRAY(stress_fs_names); i++) {
		if (stress_fs_names[i].fs_magic == fs_magic)
			return stress_fs_names[i].fs_name;
	}
	(void)snprintf(unknown, sizeof(unknown), "unknown 0x%lx", fs_magic);

	return unknown;
}
#endif

/*
 *  stress_get_fs_info()
 *	for a given filename, determine the filesystem it is stored
 *	on and return filesystem type and number of blocks
 */
const char *stress_get_fs_info(const char *filename, uintmax_t *blocks)
{
#if defined(HAVE_LINUX_MAGIC_H) &&	\
    defined(HAVE_SYS_STATFS_H)
	struct statfs buf;

	*blocks = (intmax_t)0;
	if (UNLIKELY(!filename))
		return NULL;
	if (UNLIKELY(statfs(filename, &buf) != 0))
		return NULL;
	*blocks = (uintmax_t)buf.f_bavail;
	return stress_fs_magic_to_name((unsigned long int)buf.f_type);
#elif (defined(__FreeBSD__) &&		\
       defined(HAVE_SYS_MOUNT_H) &&	\
       defined(HAVE_SYS_PARAM_H)) ||	\
      (defined(__OpenBSD__) &&		\
       defined(HAVE_SYS_MOUNT_H))
	struct statfs buf;
	static char tmp[80];

	*blocks = (intmax_t)0;
	if (statfs(filename, &buf) != 0)
		return NULL;
	*blocks = (uintmax_t)buf.f_bavail;
	(void)shim_strscpy(tmp, buf.f_fstypename, sizeof(tmp));
	return tmp;
#else
	(void)filename;
	*blocks = (intmax_t)0;
	return NULL;
#endif
}

/*
 *  stress_get_fs_type()
 *	return the file system type that the given filename is in
 */
const char *stress_get_fs_type(const char *filename)
{
	uintmax_t blocks;
	const char *fs_name = stress_get_fs_info(filename, &blocks);

	if (fs_name) {
		static char tmp[256];

		(void)snprintf(tmp, sizeof(tmp), ", filesystem type: %s (%" PRIuMAX" blocks available)",
			fs_name, blocks);
		return tmp;
	}
	return "";
}

/*
 *  Indicate a stress test failed because of limited resources
 *  rather than a failure of the tests during execution.
 *  err is the errno of the failure.
 */
int PURE stress_exit_status(const int err)
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
 *  stress_get_proc_self_exe_path()
 *	get process' executable path via readlink
 */
static char *stress_get_proc_self_exe_path(char *path, const char *proc_path, const size_t path_len)
{
	ssize_t len;

	if (UNLIKELY(!path || !proc_path))
		return NULL;

	len = shim_readlink(proc_path, path, path_len);
	if (UNLIKELY((len < 0) || (len >= PATH_MAX)))
		return NULL;
	path[len] = '\0';

	return path;
}

/*
 *  stress_get_proc_self_exe()
 *  	determine the path to the executable, return NULL if not possible/failed
 */
char *stress_get_proc_self_exe(char *path, const size_t path_len)
{
#if defined(__linux__)
	return stress_get_proc_self_exe_path(path, "/proc/self/exe", path_len);
#elif defined(__NetBSD__)
	return stress_get_proc_self_exe_path(path, "/proc/curproc/exe", path_len);
#elif defined(__DragonFly__)
	return stress_get_proc_self_exe_path(path, "/proc/curproc/file", path_len);
#elif defined(__FreeBSD__)
#if defined(CTL_KERN) &&	\
    defined(KERN_PROC) &&	\
    defined(KERN_PROC_PATHNAME)
	static int mib[] = { CTL_KERN, KERN_PROC, KERN_PROC_PATHNAME, -1 };
	size_t tmp_path_len = path_len;
	int ret;

	if (UNLIKELY(!path))
		return NULL;

	ret = sysctl(mib, SIZEOF_ARRAY(mib), (void *)path, &tmp_path_len, NULL, 0);
	if (ret < 0) {
		/* fall back to procfs */
		return stress_get_proc_self_exe_path(path, "/proc/curproc/file", path_len);
	}
	return path;
#else
	/* fall back to procfs */
	if (UNLIKELY(!path))
		return NULL;
	return stress_get_proc_self_exe_path(path, "/proc/curproc/file", path_len);
#endif
#elif defined(__sun__) && 	\
      defined(HAVE_GETEXECNAME)
	const char *execname = getexecname();

	if (UNLIKELY(!path))
		return NULL;
	(void)stress_get_proc_self_exe_path;

	if (UNLIKELY(!execname))
		return NULL;
	/* Need to perform a string copy to deconstify execname */
	(void)shim_strscpy(path, execname, path_len);
	return path;
#elif defined(HAVE_PROGRAM_INVOCATION_NAME)
	if (UNLIKELY(!path))
		return NULL;

	(void)stress_get_proc_self_exe_path;

	/* this may return the wrong name if it's been argv modified */
	(void)shim_strscpy(path, program_invocation_name, path_len);
	return path;
#else
	if (UNLIKELY(!path))
		return NULL;
	(void)stress_get_proc_self_exe_path;
	(void)path;
	(void)path_len;
	return NULL;
#endif
}

#if defined(__FreeBSD__) ||	\
    defined(__NetBSD__) ||	\
    defined(__APPLE__)
/*
 *  stress_bsd_getsysctl()
 *	get sysctl using name, ptr to obj, size = size of obj
 */
int stress_bsd_getsysctl(const char *name, void *ptr, size_t size)
{
	int ret;
	size_t nsize = size;

	if (UNLIKELY(!ptr || !name))
		return -1;

	(void)shim_memset(ptr, 0, size);

	ret = sysctlbyname(name, ptr, &nsize, NULL, 0);
	if ((ret < 0) || (nsize != size)) {
		(void)shim_memset(ptr, 0, size);
		return -1;
	}
	return 0;
}

/*
 *  stress_bsd_getsysctl_uint64()
 *	get sysctl by name, return uint64 value
 */
uint64_t stress_bsd_getsysctl_uint64(const char *name)
{
	uint64_t val;

	if (UNLIKELY(!name))
		return 0ULL;
	if (stress_bsd_getsysctl(name, &val, sizeof(val)) == 0)
		return val;
	return 0ULL;
}

/*
 *  stress_bsd_getsysctl_uint32()
 *	get sysctl by name, return uint32 value
 */
uint32_t stress_bsd_getsysctl_uint32(const char *name)
{
	uint32_t val;

	if (UNLIKELY(!name))
		return 0UL;
	if (stress_bsd_getsysctl(name, &val, sizeof(val)) == 0)
		return val;
	return 0UL;
}

/*
 *  stress_bsd_getsysctl_uint()
 *	get sysctl by name, return unsigned int value
 */
unsigned int stress_bsd_getsysctl_uint(const char *name)
{
	unsigned int val;

	if (UNLIKELY(!name))
		return 0;
	if (stress_bsd_getsysctl(name, &val, sizeof(val)) == 0)
		return val;
	return 0;
}

/*
 *  stress_bsd_getsysctl_int()
 *	get sysctl by name, return int value
 */
int stress_bsd_getsysctl_int(const char *name)
{
	int val;

	if (UNLIKELY(!name))
		return 0;
	if (stress_bsd_getsysctl(name, &val, sizeof(val)) == 0)
		return val;
	return 0;
}
#else

int PURE stress_bsd_getsysctl(const char *name, void *ptr, size_t size)
{
	(void)name;
	(void)ptr;
	(void)size;

	return 0;
}

uint64_t PURE stress_bsd_getsysctl_uint64(const char *name)
{
	(void)name;

	return 0ULL;
}

uint32_t PURE stress_bsd_getsysctl_uint32(const char *name)
{
	(void)name;

	return 0UL;
}

unsigned int PURE stress_bsd_getsysctl_uint(const char *name)
{
	(void)name;

	return 0;
}

int PURE stress_bsd_getsysctl_int(const char *name)
{
	(void)name;

	return 0;
}
#endif

/*
 *  stress_close_fds()
 *	close an array of file descriptors
 */
void stress_close_fds(int *fds, const size_t n)
{
	size_t i, j;

	if (UNLIKELY(!fds))
		return;
	if (UNLIKELY(n < 1))
		return;

	qsort(fds, n, sizeof(*fds), stress_sort_cmp_fwd_int);
	for (j = 0; j < n - 1; j++) {
		if (fds[j] >= 0)
			break;
	}
	for (i = j; i < n - 1; i++) {
		if (fds[i] + 1 != fds[i + 1])
			goto close_slow;
	}
	if (shim_close_range(fds[j], fds[n - 1], 0) == 0)
		return;

close_slow:
	for (i = j; i < n; i++)
		(void)close(fds[i]);
}

/*
 *  stress_file_rw_hint_short()
 *	hint that file data opened on fd hash short lifetime
 */
void stress_file_rw_hint_short(const int fd)
{
#if defined(F_SET_FILE_RW_HINT) &&	\
    defined(RWH_WRITE_LIFE_SHORT)
	uint64_t hint = RWH_WRITE_LIFE_SHORT;

	VOID_RET(int, fcntl(fd, F_SET_FILE_RW_HINT, &hint));
#else
	(void)fd;
#endif
}

/*
 *  stress_set_vma_anon_name()
 *	set a name to an anonymously mapped vma
 */
void stress_set_vma_anon_name(const void *addr, const size_t size, const char *name)
{
#if defined(HAVE_SYS_PRCTL_H) &&	\
    defined(HAVE_PRCTL) &&		\
    defined(PR_SET_VMA) &&		\
    defined(PR_SET_VMA_ANON_NAME)
	VOID_RET(int, prctl(PR_SET_VMA, PR_SET_VMA_ANON_NAME,
			(unsigned long int)addr,
			(unsigned long int)size,
			(unsigned long int)name));
#else
	(void)addr;
	(void)size;
	(void)name;
#endif
}

/*
 *  stress_x86_readmsr()
 *	64 bit read an MSR on a specified x86 CPU
 */
int stress_x86_readmsr64(const int cpu, const uint32_t reg, uint64_t *val)
{
#if defined(STRESS_ARCH_X86)
	char buffer[PATH_MAX];
	uint64_t value = 0;
	int fd;
	ssize_t ret;

	if (UNLIKELY(!val))
		return -1;
	*val = ~0ULL;
	(void)snprintf(buffer, sizeof(buffer), "/dev/cpu/%d/msr", cpu);
	if ((fd = open(buffer, O_RDONLY)) < 0)
		return -1;

	ret = pread(fd, &value, 8, reg);
	(void)close(fd);
	if (ret < 0)
		return -1;

	*val = value;
	return 0;
#else
	(void)cpu;
	(void)reg;
	(void)val;

	if (val)
		*val = ~0ULL;
	return -1;
#endif
}

/*
 *  stress_unset_chattr_flags()
 *	disable all chattr flags including the immutable flas
 */
void stress_unset_chattr_flags(const char *pathname)
{
#if defined(__linux__) &&	\
    defined(_IOW)

#define SHIM_EXT2_IMMUTABLE_FL		0x00000010
#define SHIM_EXT2_IOC_SETFLAGS		_IOW('f', 2, long int)

	int fd;
	unsigned long int flags = 0;

	if (UNLIKELY(!pathname))
		return;
	fd = open(pathname, O_RDONLY);
	if (UNLIKELY(fd < 0))
		return;

	VOID_RET(int, ioctl(fd, SHIM_EXT2_IOC_SETFLAGS, &flags));
	(void)close(fd);

#undef SHIM_EXT2_IMMUTABLE_FL
#undef SHIM_EXT2_IOC_SETFLAGS

#else
	(void)pathname;
#endif
}

/*
 *  stress_munmap_retry_enomem()
 *	retry munmap on ENOMEM errors as these can be due
 *	to low memory not allowing memory to be released
 */
int stress_munmap_retry_enomem(void *addr, size_t length)
{
	int ret, i;

	for (i = 1; i <= 10; i++) {
		int saved_errno;

		ret = munmap(addr, length);
		if (LIKELY(ret == 0))
			break;
		if (errno != ENOMEM)
			break;
		saved_errno = errno;
		(void)shim_usleep(10000 * i);
		errno = saved_errno;
	}
	return ret;
}

/*
 *  stress_swapoff()
 *	swapoff and retry if EINTR occurs
 */
int stress_swapoff(const char *path)
{
#if defined(HAVE_SYS_SWAP_H) && \
    defined(HAVE_SWAP)
	int i;

	if (UNLIKELY(!path)) {
		errno = EINVAL;
		return -1;
	}

	for (i = 0; i < 25; i++) {
		int ret;

		errno = 0;
		ret = swapoff(path);
		if (ret == 0)
			return ret;
		if ((ret < 0) && (errno != EINTR))
			break;
	}
	return -1;
#else
	if (!path) {
		errno = EINVAL;
		return -1;
	}
	errno = ENOSYS;
	return -1;
#endif
}

/*
 *  Filter out dot files . and ..
 */
static int PURE stress_dot_filter(const struct dirent *d)
{
	if (d->d_name[0] == '.') {
		if (d->d_name[1] == '\0')
			return 0;
		if ((d->d_name[1] == '.') && (d->d_name[2] == '\0'))
			return 0;
	}
	return 1;
}

/*
 *  stress_unset_inode_flags()
 *	unset the inode flag bits specified in flag
 */
static void stress_unset_inode_flags(const char *filename, const int flag)
{
#if defined(FS_IOC_SETFLAGS)
	int fd;
        const long int new_flag = 0;

	if (UNLIKELY(!filename))
		return;
	fd = open(filename, O_RDWR | flag);
	if (UNLIKELY(fd < 0))
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
	const char *end = path + PATH_MAX;
	int n;
	struct dirent **names = NULL;

	if (UNLIKELY(!temp_path || !path))
		return;

	if (UNLIKELY(shim_stat(path, &statbuf) < 0)) {
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
			stress_unset_chattr_flags(path);
			stress_clean_dir_files(temp_path, temp_path_len, path, path_posn + name_len);
			(void)shim_rmdir(path);
			break;
		case DT_LNK:
		case DT_REG:
			free(names[n]);
			stress_unset_inode_flags(temp_path, 0);
			stress_unset_chattr_flags(path);
			if (strstr(path, "swap"))
				(void)stress_swapoff(path);
			(void)shim_unlink(path);
			break;
		default:
			free(names[n]);
			break;
		}
#else
		/* Slower stat method */
		free(names[n]);
		ret = shim_stat(path, &statbuf);
		if (ret < 0)
			continue;

		if ((statbuf.st_mode & S_IFMT) == S_IFDIR) {
#if defined(O_DIRECTORY)
			stress_unset_inode_flags(temp_path, O_DIRECTORY);
#endif
			stress_unset_chattr_flags(temp_path);
			stress_clean_dir_files(temp_path, temp_path_len, path, path_posn + name_len);
			(void)shim_rmdir(path);
		} else if (((statbuf.st_mode & S_IFMT) == S_IFLNK) ||
			   ((statbuf.st_mode & S_IFMT) == S_IFREG)) {
			stress_unset_inode_flags(temp_path, 0);
			stress_unset_chattr_flags(temp_path);
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
void stress_clean_dir(
	const char *name,
	const pid_t pid,
	const uint32_t instance)
{
	char path[PATH_MAX];
	const char *temp_path = stress_get_temp_path();
	const size_t temp_path_len = strlen(temp_path);

	if (LIKELY(name != NULL)) {
		(void)stress_temp_dir(path, sizeof(path), name, pid, instance);
		if (access(path, F_OK) == 0) {
			pr_dbg("%s: removing temporary files in %s\n", name, path);
			stress_clean_dir_files(temp_path, temp_path_len, path, strlen(path));
		}
	}
}

/*
 *  stress_random_small_sleep()
 *	0..5000 us sleep, used in pthreads to add some
 *	small delay into startup to randomize any racy
 *	conditions
 */
void stress_random_small_sleep(void)
{
	shim_usleep_interruptible(stress_mwc32modn(5000));
}

/*
 *  stress_yield_sleep_ms()
 *	force a yield, sleep if the yield was less than 1ms,
 *      and repeat if sleep was less than 1ms
 */
void stress_yield_sleep_ms(void)
{
	const double t = stress_time_now();

	do {
		double duration;

		(void)shim_sched_yield();
		duration = stress_time_now() - t;
		if (duration > 0.001)
			break;
		(void)shim_usleep(1000);
	} while (stress_continue_flag());
}

static void stress_dbg(const char *fmt, ...) FORMAT(printf, 1, 2);

/*
 *  stress_dbg()
 *	simple debug, messages must be less than 256 bytes
 */
static void stress_dbg(const char *fmt, ...)
{
	va_list ap;
	int n, sz;
	static char buf[256];
	n = snprintf(buf, sizeof(buf), "stress-ng: debug: [%" PRIdMAX"] ", (intmax_t)getpid());
	if (n < 0)
		return;
	sz = n;
	va_start(ap, fmt);
	n = vsnprintf(buf + sz, sizeof(buf) - sz, fmt, ap);
	va_end(ap);
	sz += n;

	VOID_RET(ssize_t, write(fileno(stdout), buf, (size_t)sz));
}

/*
 *  stress_addr_readable()
 *	portable way to check if memory addr[0]..addr[len - 1] is readable,
 *	create pipe, see if write of the memory range works, failure (with
 *	EFAULT) will be used to indicate address range is not readable.
 */
bool stress_addr_readable(const void *addr, const size_t len)
{
	int fds[2];
	bool ret = false;

	if (UNLIKELY(pipe(fds) < 0))
		return ret;
	if (write(fds[1], addr, len) == (ssize_t)len)
		ret = true;
	(void)close(fds[0]);
	(void)close(fds[1]);

	return ret;
}

/*
 *  stress_dump_data()
 *	dump to stdout 16 bytes of data code if it is readable. SIGILL address
 *	data is indicated with < > around it.
 */
static void stress_dump_data(
	const uint8_t *addr,
	const uint8_t *fault_addr,
	const size_t len)
{
	char buf[128];

	if (stress_addr_readable(addr, len)) {
		size_t i;
		bool show_opcode = false;
		int n, sz = 0;

		n = snprintf(buf + sz, sizeof(buf) - sz, "stress-ng: info: 0x%16.16" PRIxPTR ":", (uintptr_t)addr);
		if (n < 0)
			return;
		sz += n;

		for (i = 0; i < len; i++) {
			if (&addr[i] == fault_addr) {
				n = snprintf(buf + sz, sizeof(buf) - sz, "<%-2.2x>", addr[i]);
				if (n < 0)
					return;
				sz += n;
				show_opcode = true;
			} else {
				n = snprintf(buf + sz, sizeof(buf) - sz, "%s%-2.2x", show_opcode ? "" : " ", addr[i]);
				if (n < 0)
					return;
				sz += n;
				show_opcode = false;
			}
		}
		stress_dbg("%s\n", buf);
	} else {
		stress_dbg("stress-ng: info: 0x%16.16" PRIxPTR " not readable\n", (uintptr_t)addr);
	}
}

/*
 *  stress_catch_sig_si_code()
 *	convert signal and si_code into human readable form
 */
static const PURE char *stress_catch_sig_si_code(const int sig, const int sig_code)
{
	static const char unknown[] = "UNKNOWN";

	switch (sig) {
	case SIGILL:
		switch (sig_code) {
#if defined(ILL_ILLOPC)
		case ILL_ILLOPC:
			return "ILL_ILLOPC";
#endif
#if defined(ILL_ILLOPN)
		case ILL_ILLOPN:
			return "ILL_ILLOPN";
#endif
#if defined(ILL_ILLADR)
		case ILL_ILLADR:
			return "ILL_ILLADR";
#endif
#if defined(ILL_ILLTRP)
		case ILL_ILLTRP:
			return "ILL_ILLTRP";
#endif
#if defined(ILL_PRVOPC)
		case ILL_PRVOPC:
			return "ILL_PRVOPC";
#endif
#if defined(ILL_PRVREG)
		case ILL_PRVREG:
			return "ILL_PRVREG";
#endif
#if defined(ILL_COPROC)
		case ILL_COPROC:
			return "ILL_COPROC";
#endif
#if defined(ILL_BADSTK)
		case ILL_BADSTK:
			return "ILL_BADSTK";
#endif
		default:
			return unknown;
		}
		break;
	case SIGSEGV:
		switch (sig_code) {
#if defined(SEGV_MAPERR)
		case SEGV_MAPERR:
			return "SEGV_MAPERR";
#endif
#if defined(SEGV_ACCERR)
		case SEGV_ACCERR:
			return "SEGV_ACCERR";
#endif
#if defined(SEGV_BNDERR)
		case SEGV_BNDERR:
			return "SEGV_BNDERR";
#endif
#if defined(SEGV_PKUERR)
		case SEGV_PKUERR:
			return "SEGV_PKUERR";
#endif
		default:
			return unknown;
		}
		break;
	}
	return unknown;
}

/*
 *  stress_dump_readable_data()
 *	3 lines of memory hexdump, aligned to 16 bytes boundary
 */
static void stress_dump_readable_data(uint8_t *fault_addr)
{
	int i;
	uint8_t *addr = (uint8_t *)((uintptr_t)fault_addr & ~0xf);

	for (i = 0; i < 3; i++, addr += 16) {
		stress_dump_data(addr, fault_addr, 16);
	}
}

/*
 *  stress_dump_map_info()
 *	find fault address in /proc/self/maps, dump out map info
 */
static void stress_dump_map_info(uint8_t *fault_addr)
{
#if defined(__linux__)
	FILE *fp;
	char buf[1024];

	fp = fopen("/proc/self/maps", "r");
	if (UNLIKELY(!fp))
		return;
	while ((fgets(buf, sizeof(buf), fp)) != NULL) {
		uintptr_t begin, end;

		if (sscanf(buf, "%" SCNxPTR "-%" SCNxPTR, &begin, &end) == 2) {
			if (((uintptr_t)fault_addr >= begin) &&
			    ((uintptr_t)fault_addr <= end)) {
				char *ptr1, *ptr2;

				/* truncate to first \n found */
				ptr1 = strchr(buf, (int)'\n');
				if (ptr1)
					*ptr1 = '\0';

				/* squeeze out duplicated spaces */
				for (ptr1 = buf, ptr2 = buf; *ptr1; ptr1++) {
					if ((*ptr1 == ' ') && (*(ptr1 + 1) == ' '))
						continue;
					*ptr2 = *ptr1;
					ptr2++;

				}
				*ptr2 = '\0';
				stress_dbg("stress-ng: info: %s\n", buf);
				break;
			}
		}
	}
	(void)fclose(fp);
#else
	(void)fault_addr;
#endif
}

/*
 *  stress_catch_sig_handler()
 *	handle signal, dump 16 bytes before and after the illegal opcode
 *	and terminate immediately to avoid any recursive signal handling
 */
static void stress_catch_sig_handler(
	int sig,
	siginfo_t *info,
	void *ucontext,
	const int sig_expected,
	const char *sig_expected_name)
{
	static bool handled = false;

	(void)sig;
	(void)ucontext;

	if (handled)
		_exit(EXIT_FAILURE);
	handled = true;
	if (sig == sig_expected) {
		if (info) {
			stress_dbg("caught %s, address 0x%16.16" PRIxPTR " (%s)\n",
				sig_expected_name, (uintptr_t)info->si_addr,
				stress_catch_sig_si_code(sig, info->si_code));
			stress_dump_readable_data((uint8_t *)info->si_addr);
			stress_dump_map_info((uint8_t *)info->si_addr);
		} else {
			stress_dbg("caught %s, unknown address\n", sig_expected_name);
		}
	} else {
		if (info) {
			stress_dbg("caught unexpected SIGNAL %d, address 0x%16.16" PRIxPTR "\n",
				sig, (uintptr_t)info->si_addr);
			stress_dump_readable_data((uint8_t *)info->si_addr);
			stress_dump_map_info((uint8_t *)info->si_addr);
		} else {
			stress_dbg("caught unexpected SIGNAL %d, unknown address\n", sig);
		}
	}
	/* Big fat abort */
	_exit(EXIT_FAILURE);
}

/*
 *  stress_catch_sigill_handler()
 *	handler for SIGILL
 */
static void stress_catch_sigill_handler(
	int sig,
	siginfo_t *info,
	void *ucontext)
{
	stress_catch_sig_handler(sig, info, ucontext, SIGILL, "SIGILL");
}

/*
 *  stress_catch_sigsegv_handler()
 *	handler for SIGSEGV
 */
static void stress_catch_sigsegv_handler(
	int sig,
	siginfo_t *info,
	void *ucontext)
{
	stress_catch_sig_handler(sig, info, ucontext, SIGSEGV, "SIGSEGV");
}

/*
 *  stress_catch_sig()
 *	add signal handler to catch and dump illegal instructions,
 *	this is mainly to be used by any code using target clones
 *	just in case the compiler emits code that the target cannot
 *	actually execute.
 */
static void stress_catch_sig(
	const int sig,
	void (*handler)(int sig, siginfo_t *info, void *ucontext)
)
{
	struct sigaction sa;

	(void)shim_memset(&sa, 0, sizeof(sa));

	sa.sa_sigaction = handler;
#if defined(SA_SIGINFO)
	sa.sa_flags = SA_SIGINFO;
#endif
	(void)sigaction(sig, &sa, NULL);
}

/*
 *  stress_catch_sigill()
 *	catch and dump SIGILL signals
 */
void stress_catch_sigill(void)
{
	stress_catch_sig(SIGILL, stress_catch_sigill_handler);
}

/*
 *  stress_catch_sigsegv()
 *	catch and dump SIGSEGV signals
 */
void stress_catch_sigsegv(void)
{
	stress_catch_sig(SIGSEGV, stress_catch_sigsegv_handler);
}

#if defined(__linux__)
/*
 *  stress_process_info_dump()
 *	dump out /proc/$PID/filename data in human readable format
 */
static void stress_process_info_dump(
	stress_args_t *args,
	const pid_t pid,
	const char *filename)
{
	char path[4096];
	char buf[8192];
	char *ptr, *end, *begin, *emit;
	ssize_t ret;

	if (UNLIKELY(!filename))
		return;

	(void)snprintf(path, sizeof(path), "/proc/%" PRIdMAX "/%s", (intmax_t)pid, filename);
	ret = stress_system_read(path, buf, sizeof(buf));
	if (ret < 0)
		return;

	end = buf + ret;
	/* data like /proc/$PID/cmdline has '\0' chars - replace with spaces */
	for (ptr = buf; ptr < end; ptr++)
		if (*ptr == '\0')
			*ptr = ' ';

	ptr = buf;
	begin = ptr;
	emit = NULL;
	while (ptr < end) {
		while (ptr < end) {
			/* new line or eos, flush */
			if (*ptr == '\n' || *ptr == '\0') {
				*ptr = '\0';
				emit = begin;
				ptr++;
				begin = ptr;
			}
			ptr++;
			/* reached end, flush residual data out */
			if (ptr == end)
				emit = begin;
			if (emit) {
				pr_dbg("%s: [%" PRIdMAX "] %s: %s\n", args ? args->name : "main", (intmax_t)pid, filename, emit);
				emit = NULL;
			}
		}
	}
}
#endif

/*
 *  stress_process_info()
 *	dump out process specific debug from /proc
 */
void stress_process_info(stress_args_t *args, const pid_t pid)
{
#if defined(__linux__)
	pr_block_begin();
	stress_process_info_dump(args, pid, "cmdline");
	stress_process_info_dump(args, pid, "syscall");
	stress_process_info_dump(args, pid, "stack");
	stress_process_info_dump(args, pid, "wchan");
	pr_block_end();
#else
	(void)args;
	(void)pid;
#endif
}

/*
 *  stress_mmap_populate()
 *	try mmap with MAP_POPULATE option, if it fails
 *	retry without MAP_POPULATE. This prefaults pages
 *	into memory to avoid faulting during stressor
 *	execution. Useful for mappings that get accessed
 *	immediately after being mmap'd.
 */
void *stress_mmap_populate(
	void *addr,
	size_t length,
	int prot,
	int flags,
	int fd,
	off_t offset)
{
#if defined(MAP_POPULATE)
	void *ret;

	flags |= MAP_POPULATE;
	ret = mmap(addr, length, prot, flags, fd, offset);
	if (ret != MAP_FAILED)
		return ret;
	flags &= ~MAP_POPULATE;
#endif
	return mmap(addr, length, prot, flags, fd, offset);
}

/*
 *  stress_get_machine_id()
 *	try to get a unique 64 bit machine id number
 */
uint64_t stress_get_machine_id(void)
{
	uint64_t id = 0;

#if defined(__linux__)
	{
		char buf[17];

		/* Try machine id from /etc */
		if (stress_system_read("/etc/machine-id", buf, sizeof(buf)) > 0) {
			buf[16] = '\0';
			return (uint64_t)strtoll(buf, NULL, 16);
		}
	}
#endif
#if defined(__linux__)
	{
		char buf[17];

		/* Try machine id from /var/lib */
		if (stress_system_read("/var/lib/dbus/machine-id", buf, sizeof(buf)) > 0) {
			buf[16] = '\0';
			return (uint64_t)strtoll(buf, NULL, 16);
		}
	}
#endif
#if defined(HAVE_GETHOSTID)
	{
		/* Mangle 32 bit hostid to 64 bit */
		uint64_t hostid = (uint64_t)gethostid();

		id = hostid ^ ((~hostid) << 32);
	}
#endif
#if defined(HAVE_GETHOSTNAME)
	{
		char buf[256];

		/* Mangle hostname to 64 bit value */
		if (gethostname(buf, sizeof(buf)) == 0) {
			id ^= stress_hash_crc32c(buf) |
			      ((uint64_t)stress_hash_x17(buf) << 32);
		}
	}
#endif
	return id;
}

/*
 *  stress_zero_metrics()
 *	initialize metrics array 0..n-1 items
 */
void stress_zero_metrics(stress_metrics_t *metrics, const size_t n)
{
	size_t i;

	for (i = 0; i < n; i++) {
		metrics[i].lock = NULL;
		metrics[i].duration = 0.0;
		metrics[i].count = 0.0;
		metrics[i].t_start = 0.0;
	}
}

/*
 *  stress_backtrace
 *	dump stack trace to stdout, this could be called
 *	from a signal context so try to keep buffer small
 *	and fflush on all printfs to ensure we dump as
 *	much as possible.
 */
void stress_backtrace(void)
{
#if defined(HAVE_EXECINFO_H) &&	\
    defined(HAVE_BACKTRACE)
	int i, n_ptrs;
	void *buffer[BACKTRACE_BUF_SIZE];
	char **strings;

	n_ptrs = backtrace(buffer, BACKTRACE_BUF_SIZE);
	if (n_ptrs < 1)
		return;

	strings = backtrace_symbols(buffer, n_ptrs);
	if (!strings)
		return;

	printf("backtrace:\n");
	fflush(stdout);

	for (i = 0; i < n_ptrs; i++) {
		printf("  %s\n", strings[i]);
		fflush(stdout);
	}
	free(strings);
#endif
}

/*
 *  stress_data_is_not_zero()
 *	checks if buffer is zero, buffer must be 128 bit aligned
 */
bool OPTIMIZE3 stress_data_is_not_zero(uint64_t *buffer, const size_t len)
{
	register const uint64_t *end64 = buffer + (len / sizeof(uint64_t));
	register uint64_t *ptr64;
	register const uint8_t *end8;
	register uint8_t *ptr8;

PRAGMA_UNROLL_N(8)
	for (ptr64 = buffer; ptr64 < end64; ptr64++) {
		if (UNLIKELY(*ptr64))
			return true;
	}

	end8 = ((uint8_t *)buffer) + len;
PRAGMA_UNROLL_N(8)
	for (ptr8 = (uint8_t *)ptr64; ptr8 < end8; ptr8++) {
		if (UNLIKELY(*ptr8))
			return true;
	}
	return false;
}
