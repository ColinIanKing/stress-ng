/*
 * Copyright (C) 2014-2021 Canonical, Ltd.
 * Copyright (C) 2022-2023 Colin Ian King.
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
#include "core-builtin.h"
#include "core-capabilities.h"
#include "core-cpu-cache.h"
#include "core-hash.h"
#include "core-pragma.h"
#include "core-sort.h"

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

#if defined(HAVE_SYS_STATVFS_H)
#include <sys/statvfs.h>
#endif

#if defined(HAVE_SYS_SYSCTL_H)
STRESS_PRAGMA_PUSH
STRESS_PRAGMA_WARN_CPP_OFF
#include <sys/sysctl.h>
STRESS_PRAGMA_POP
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

#if defined(__TINYC__) || defined(__PCC__)
int __dso_handle;
#endif

#define PAGE_4K_SHIFT			(12)
#define PAGE_4K				(1 << PAGE_4K_SHIFT)

#define STRESS_ABS_MIN_STACK_SIZE	(64 * 1024)

const char ALIGN64 stress_ascii64[64] =
	"0123456789ABCDEFGHIJKLMNOPQRSTUV"
	"WXYZabcdefghijklmnopqrstuvwxyz@!";

static bool stress_stack_check_flag;

typedef struct {
	const unsigned long	fs_magic;
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
#if defined(MINIX_SUPER_MAGIC)
	{ MINIX_SUPER_MAGIC,	"minix" },
#endif
#if defined(MINIX_SUPER_MAGIC2)
	{ MINIX_SUPER_MAGIC2,	"minix" },
#endif
#if defined(MINIX2_SUPER_MAGIC)
	{ MINIX2_SUPER_MAGIC,	"minix2" },
#endif
#if defined(MINIX2_SUPER_MAGIC2)
	{ MINIX2_SUPER_MAGIC2,	"minix2" },
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
};
#endif

typedef struct {
	const int  signum;
	const char *name;
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
#if defined(SIGTSTP)
	SIG_NAME(SIGTSTP),
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
#if defined(SIGXCPU)
	SIG_NAME(SIGXCPU),
#endif
#if defined(SIGXFSZ)
	SIG_NAME(SIGXFSZ),
#endif
#if defined(SIGWINCH)
	SIG_NAME(SIGWINCH),
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
	stress_temp_path_free();

	stress_temp_path = stress_const_optdup(path);
	if (!stress_temp_path) {
		(void)fprintf(stderr, "aborting: cannot allocate memory for '%s'\n", path);
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

	if (access(path, R_OK | W_OK) < 0) {
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
	(void)shim_strlcpy(fullname, pathname, fullname_len);
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
	if (page_size > 0)
		return page_size;

#if defined(_SC_PAGESIZE)
	{
		/* Use modern sysconf */
		const long sz = sysconf(_SC_PAGESIZE);
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
		const long sz = getpagesize();
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

	if (processors_online > 0)
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

	if (processors_configured > 0)
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

	if (ticks_per_second > 0)
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
#if defined(HAVE_SYS_SYSINFO_H) &&	\
    defined(HAVE_SYSINFO)
	struct sysinfo info;

	(void)memset(&info, 0, sizeof(info));

	if (sysinfo(&info) == 0) {
		*freemem = info.freeram * info.mem_unit;
		*totalmem = info.totalram * info.mem_unit;
		*freeswap = info.freeswap * info.mem_unit;
		*totalswap = info.totalswap * info.mem_unit;
		return 0;
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
		(void)memset(&vm_stat, 0, sizeof(vm_stat));
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
	FILE *fp;

	(void)stress_get_meminfo(freemem, totalmem, freeswap, totalswap);

	*shmall = 0;
	fp = fopen("/proc/sys/kernel/shmall", "r");
	if (!fp)
		return;

	if (fscanf(fp, "%zu", shmall) != 1) {
		(void)fclose(fp);
		return;
	}
	(void)fclose(fp);
}

#define PR_SET_MEMORY_MERGE 67

/*
 *  stress_ksm_memory_merge()
 *	set kernel samepage merging flag (linux only)
 */
void stress_ksm_memory_merge(const int flag)
{
#if defined(__linux__) &&	\
    defined(PR_SET_MEMORY_MERGE)
	if ((flag >= 0) && (flag <= 1)) {
		static int prev_flag = -1;

		if (flag != prev_flag){
			VOID_RET(int, prctl(PR_SET_MEMORY_MERGE, flag));
			prev_flag = flag;
		}
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
		/* Any swap enabled and is it running low? */
		if ((totalswap > 0) && (freeswap < (2 * MB) + requested)) {
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
	uint64_t phys_pages = 0;
	const size_t page_size = stress_get_page_size();
	const uint64_t max_pages = ~0ULL / page_size;

	phys_pages = (uint64_t)sysconf(STRESS_SC_PAGES);
	/* Avoid overflow */
	if (phys_pages > max_pages)
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

	if (!path)
		return 0;

	(void)memset(&buf, 0, sizeof(buf));
	rc = statvfs(path, &buf);
	if (rc < 0)
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

	if (!path)
		return 0;

	(void)memset(&buf, 0, sizeof(buf));
	rc = statvfs(path, &buf);
	if (rc < 0)
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

	loadavg[0] = 0.0;
	loadavg[1] = 0.0;
	loadavg[2] = 0.0;

	rc = getloadavg(loadavg, 3);
	if (rc < 0)
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

	if (sysinfo(&info) < 0)
		goto fail;

	*min1 = info.loads[0] * scale;
	*min5 = info.loads[1] * scale;
	*min15 = info.loads[2] * scale;

	return 0;
fail:
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
	int fd, rc = 0;

#if defined(RLIMIT_CORE)
	{
		struct rlimit lim;
		int ret;

		ret = getrlimit(RLIMIT_CORE, &lim);
		if (ret == 0) {
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
	if ((fd = open("/proc/self/coredump_filter", O_WRONLY)) >= 0) {
		char const *str =
			dumpable ? "0x33" : "0x00";

		if (write(fd, str, strlen(str)) < 0)
			rc = -1;
		(void)close(fd);
	}
	return rc;
}

/*
 *  stress_set_timer_slackigned_longns()
 *	set timer slack in nanoseconds
 */
int stress_set_timer_slack_ns(const char *opt)
{
#if defined(HAVE_PRCTL_TIMER_SLACK)
	uint32_t timer_slack;

	timer_slack = stress_get_uint32(opt);
	(void)stress_set_setting("timer-slack", TYPE_ID_UINT32, &timer_slack);
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
		"deinit",
		"stop",
		"exit",
		"wait",
		"zombie",
	};

	if ((state < 0) || (state >= (int)SIZEOF_ARRAY(stress_states)))
		return;

	stress_set_proc_state_str(name, stress_states[state]);
}

/*
 *  stress_chr_munge()
 *	convert ch _ to -, otherwise don't change it
 */
static inline char stress_chr_munge(const char ch)
{
	return (ch == '_') ? '-' : ch;
}

/*
 *   stress_munge_underscore()
 *	turn '_' to '-' in strings with strlcpy api
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
	const uint8_t val2 = 0;
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
 *  stress_uint64_zero()
 *	return uint64 zero in way that force less smart
 *	static analysers to realise we are doing this
 *	to force a division by zero. I'd like to have
 *	a better solution than this ghastly way.
 */
uint64_t stress_uint64_zero(void)
{
	return g_shared->zero;
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

	(void)memset(&buf, 0, sizeof(buf));
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
	const stress_args_t *args,
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
	const stress_args_t *args,
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
	if (ret < 0) {
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
int stress_temp_dir_mk_args(const stress_args_t *args)
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
	if (ret < 0) {
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
int stress_temp_dir_rm_args(const stress_args_t *args)
{
	return stress_temp_dir_rm(args->name, args->pid, args->instance);
}

/*
 *  stress_cwd_readwriteable()
 *	check if cwd is read/writeable
 */
void stress_cwd_readwriteable(void)
{
	char path[PATH_MAX];

	if (getcwd(path, sizeof(path)) == NULL) {
		pr_dbg("cwd: Cannot determine current working directory\n");
		return;
	}
	if (access(path, R_OK | W_OK)) {
		pr_inf("Working directory %s is not read/writeable, "
			"some I/O tests may fail\n", path);
		return;
	}
}

/*
 *  stress_signal_name()
 *	return string version of signal number, NULL if not found
 */
const char *stress_signal_name(const int signum)
{
	size_t i;

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
	const char *str = stress_signal_name(signum);

	if (str)
		(void)snprintf(buffer, sizeof(buffer), "signal %d '%s'",
			signum, str);
	else
		(void)snprintf(buffer, sizeof(buffer), "signal %d", signum);
	return buffer;
}

/*
 *  stress_rndstr()
 *	generate pseudorandom string
 */
void stress_rndstr(char *str, size_t len)
{
	/*
	 * base64url alphabet.
	 * Be careful if expanding this alphabet, some of this function's users
	 * use it to generate random filenames.
	 */
	static const char alphabet[64] =
		"ABCDEFGHIJKLMNOPQRSTUVWXYZ"
		"abcdefghijklmnopqrstuvwxyz"
		"0123456789" "-_";
	size_t i;
	uint32_t r = 0;		/* Silence a gcc warning */
	if (len == 0)
		return;

	len--; /* Leave one byte for the terminator. */
	for (i = 0; i < len; i++) {
		/* If we don't have any random bits in r, get some more. */
		if (i % (sizeof(r) * CHAR_BIT / 6) == 0)
			r = stress_mwc32();

		/*
		 * Use 6 bits from the 32-bit integer at a time.
		 * This means 2 bits from each 32-bit integer are wasted.
		 */
		str[i] = alphabet[r & 0x3F];
		r >>= 6;
	}
	str[i] = '\0';
}

/*
 *  stress_rndbuf()
 *	fill buffer with pseudorandom bytes
 */
void stress_rndbuf(void *buf, size_t len)
{
	register char *ptr = (char *)buf;
	register const char *end = ptr + len;

	while (ptr < end)
		*ptr++ = stress_mwc8();
}

/*
 *  stress_little_endian()
 *	returns true if CPU is little endian
 */
bool stress_little_endian(void)
{
	const uint32_t x = 0x12345678;
	const uint8_t *y = (const uint8_t *)&x;

	return *y == 0x78;
}

/*
 *  stress_swap32()
 *	swap order of bytes of a uint32_t value
 */
static inline uint32_t OPTIMIZE3 stress_swap32(uint32_t val)
{
#if defined(HAVE_BUILTIN_BSWAP32)
	return __builtin_bswap32(val);
#else
	return ((val >> 24) & 0x000000ff) |
	       ((val << 8)  & 0x00ff0000) |
	       ((val >> 8)  & 0x0000ff00) |
	       ((val << 24) & 0xff000000);
#endif
}

/*
 *  stress_uint8rnd4()
 *	fill a uint8_t buffer full of random data
 *	buffer *must* be multiple of 4 bytes in size
 */
void OPTIMIZE3 stress_uint8rnd4(uint8_t *data, const size_t len)
{
	register uint32_t *ptr32 = (uint32_t *)data;
	register uint32_t *ptr32end = (uint32_t *)(data + len);

	if (stress_little_endian()) {
		while (ptr32 < ptr32end)
			*ptr32++ = stress_mwc32();
	} else {
		while (ptr32 < ptr32end)
			*ptr32++ = stress_swap32(stress_mwc32());
	}
}

/*
 *  pr_run_info()
 *	short info about the system we are running stress-ng on
 *	for the -v option
 */
void pr_runinfo(void)
{
	const char *temp_path = stress_get_temp_path();
	const char *fs_type = stress_fs_type(temp_path);
	size_t freemem, totalmem, freeswap, totalswap;
#if defined(HAVE_UNAME) &&	\
    defined(HAVE_SYS_UTSNAME_H)
	struct utsname uts;
#endif
	if (!(g_opt_flags & PR_DEBUG))
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
	if (uname(&uts) >= 0) {
		pr_dbg("system: %s %s %s %s %s\n",
			uts.sysname, uts.nodename, uts.release,
			uts.version, uts.machine);
	}
#endif
	if (stress_get_meminfo(&freemem, &totalmem, &freeswap, &totalswap) == 0) {
		char ram_t[32], ram_f[32], ram_s[32];

		stress_uint64_to_str(ram_t, sizeof(ram_t), (uint64_t)totalmem);
		stress_uint64_to_str(ram_f, sizeof(ram_f), (uint64_t)freemem);
		stress_uint64_to_str(ram_s, sizeof(ram_s), (uint64_t)freeswap);
		pr_dbg("RAM total: %s, RAM free: %s, swap free: %s\n", ram_t, ram_f, ram_s);
	}

	pr_dbg("temporary file path: '%s'%s\n", temp_path, fs_type);
}

/*
 *  pr_yaml_runinfo()
 *	log info about the system we are running stress-ng on
 */
void pr_yaml_runinfo(FILE *yaml)
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
	const size_t hostname_len = stress_hostname_length();
	char *hostname;
	const char *user = shim_getlogin();

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
		pr_yaml(yaml, "      epoch-secs: %ld\n", (long)t);
	}

	hostname = malloc(hostname_len + 1);
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
#if defined(HAVE_SYS_SYSINFO_H) &&	\
    defined(HAVE_SYSINFO)
	(void)memset(&info, 0, sizeof(info));
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
	uint16_t max_cache_level = 0;

	cpu_caches = stress_cpu_cache_get_all_details();
	if (!cpu_caches) {
		if (stress_warn_once())
			pr_dbg("%s: using defaults, cannot determine cache details\n", name);
		g_shared->mem_cache_size = MEM_CACHE_SIZE;
		goto init_done;
	}

	max_cache_level = stress_cpu_cache_get_max_level(cpu_caches);
	if (max_cache_level == 0) {
		if (stress_warn_once())
			pr_dbg("%s: using defaults, cannot determine cache level details\n", name);
		g_shared->mem_cache_size = MEM_CACHE_SIZE;
		goto init_done;
	}
	if (g_shared->mem_cache_level > max_cache_level) {
		if (stress_warn_once())
			pr_dbg("%s: using cache maximum level L%d\n", name,
				max_cache_level);
		g_shared->mem_cache_level = max_cache_level;
	}

	cache = stress_cpu_cache_get(cpu_caches, g_shared->mem_cache_level);
	if (!cache) {
		if (stress_warn_once())
			pr_dbg("%s: using built-in defaults as no suitable "
				"cache found\n", name);
		g_shared->mem_cache_size = MEM_CACHE_SIZE;
		goto init_done;
	}

	if (g_shared->mem_cache_ways > 0) {
		uint64_t way_size;

		if (g_shared->mem_cache_ways > cache->ways) {
			if (stress_warn_once())
				pr_inf("%s: cache way value too high - "
					"defaulting to %d (the maximum)\n",
					name, cache->ways);
			g_shared->mem_cache_ways = cache->ways;
		}
		way_size = cache->size / cache->ways;

		/* only fill the specified number of cache ways */
		g_shared->mem_cache_size = way_size * g_shared->mem_cache_ways;
	} else {
		/* fill the entire cache */
		g_shared->mem_cache_size = cache->size;
	}

	if (!g_shared->mem_cache_size) {
		if (stress_warn_once())
			pr_dbg("%s: using built-in defaults as "
				"unable to determine cache size\n", name);
		g_shared->mem_cache_size = MEM_CACHE_SIZE;
	}
init_done:
	stress_free_cpu_caches(cpu_caches);
	g_shared->mem_cache =
		(uint8_t *)mmap(NULL, g_shared->mem_cache_size,
				PROT_READ | PROT_WRITE,
				MAP_SHARED | MAP_ANONYMOUS, -1, 0);
	if (g_shared->mem_cache == MAP_FAILED) {
		g_shared->mem_cache = NULL;
		pr_err("%s: failed to mmap shared cache buffer, errno=%d (%s)\n",
			name, errno, strerror(errno));
		return -1;
	}

	g_shared->cacheline_size = (size_t)STRESS_PROCS_MAX * sizeof(uint8_t) * 2;
	g_shared->cacheline =
		(uint8_t *)mmap(NULL, g_shared->cacheline_size,
				PROT_READ | PROT_WRITE,
				MAP_SHARED | MAP_ANONYMOUS, -1, 0);

	if (g_shared->cacheline == MAP_FAILED) {
		g_shared->cacheline = NULL;
		pr_err("%s: failed to mmap cacheline buffer, errno=%d (%s)\n",
			name, errno, strerror(errno));
		return -1;
	}
	if (stress_warn_once())
		pr_dbg("%s: shared cache buffer size: %" PRIu64 "K\n",
			name, g_shared->mem_cache_size / 1024);

	return 0;
}

/*
 *  stress_cache_free()
 *	free shared cache buffer
 */
void stress_cache_free(void)
{
	if (g_shared->mem_cache)
		(void)munmap((void *)g_shared->mem_cache, g_shared->mem_cache_size);
	if (g_shared->cacheline)
		(void)munmap((void *)g_shared->cacheline, g_shared->cacheline_size);
}

/*
 *  system_write()
 *	write a buffer to a /sys or /proc entry
 */
ssize_t system_write(
	const char *path,
	const char *buf,
	const size_t buf_len)
{
	int fd;
	ssize_t ret;

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
 *  system_read()
 *	read a buffer from a /sys or /proc entry
 */
ssize_t system_read(
	const char *path,
	char *buf,
	const size_t buf_len)
{
	int fd;
	ssize_t ret;

	(void)memset(buf, 0, buf_len);

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
	else
		buf[ret] = '\0';

	return ret;
}

/*
 *  stress_is_prime64()
 *      return true if 64 bit value n is prime
 *      http://en.wikipedia.org/wiki/Primality_test
 */
bool stress_is_prime64(const uint64_t n)
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
	for (i = 0; keep_stressing_flag() && (i < 2000); i++) {
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
	for (i = 0; keep_stressing_flag() && (i < 2000); i++) {
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
#if defined(RLIMIT_NOFILE)
	struct rlimit rlim;
#endif
	size_t max_rlim = SIZE_MAX;
	size_t max_sysconf;

#if defined(RLIMIT_NOFILE)
	if (!getrlimit(RLIMIT_NOFILE, &rlim))
		max_rlim = (size_t)rlim.rlim_cur;
#endif
#if defined(_SC_OPEN_MAX)
	{
		const long open_max = sysconf(_SC_OPEN_MAX);

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
		if (isdigit((int)d->d_name[0]))
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

	(void)memset(&rlim, 0, sizeof(rlim));

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

	ss.ss_sp = (void *)stack;
	ss.ss_size = size;
	ss.ss_flags = 0;
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
	if (size < (size_t)STRESS_MINSIGSTKSZ) {
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
			stack = (uint8_t *)mmap(NULL, STRESS_SIGSTKSZ, PROT_READ | PROT_WRITE,
					MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
			if (stack == MAP_FAILED) {
				pr_inf("%s: sigaction %s: cannot allocated signal stack, "
					"errno = %d (%s)\n",
					name, stress_strsignal(signum),
					errno, strerror(errno));
				return -1;
			}
			if (stress_sigaltstack(stack, STRESS_SIGSTKSZ) < 0)
				return -1;
		}
	}
#endif
	(void)memset(&new_action, 0, sizeof new_action);
	new_action.sa_handler = handler;
	(void)sigemptyset(&new_action.sa_mask);
	new_action.sa_flags = SA_ONSTACK;

	if (sigaction(signum, &new_action, orig_action) < 0) {
		pr_fail("%s: sigaction %s: errno=%d (%s)\n",
			name, stress_strsignal(signum), errno, strerror(errno));
		return -1;
	}
	return 0;
}

/*
 *  stress_sighandler_default
 *	restore signal handler to default handler
 */
int stress_sighandler_default(const int signum)
{
	struct sigaction new_action;

	(void)memset(&new_action, 0, sizeof new_action);
	new_action.sa_handler = SIG_DFL;

	return sigaction(signum, &new_action, NULL);
}

/*
 *  stress_handle_stop_stressing()
 *	set flag to indicate to stressor to stop stressing
 */
void stress_handle_stop_stressing(int signum)
{
	(void)signum;

	keep_stressing_set_flag(false);
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
#if defined(HAVE_SCHED_GETCPU) &&	\
    !defined(__PPC64__) &&		\
    !defined(__s390x__)
	const int cpu = sched_getcpu();

	return (unsigned int)((cpu < 0) ? 0 : cpu);
#else
	return 0;
#endif
}

#define XSTRINGIFY(s) STRINGIFY(s)
#define STRINGIFY(s) #s

/*
 *  stress_get_compiler()
 *	return compiler info
 */
const char *stress_get_compiler(void)
{
#if   defined(__ICC) && 		\
      defined(__INTEL_COMPILER) &&	\
      defined(__INTEL_COMPILER_UPDATE) && \
      defined(__INTEL_COMPILER_BUILD_DATE)
	static const char cc[] = "icc " XSTRINGIFY(__INTEL_COMPILER) "." XSTRINGIFY(__INTEL_COMPILER_UPDATE) " Build " XSTRINGIFY(__INTEL_COMPILER_BUILD_DATE) "";
#elif defined(__ICC) && 		\
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
      defined(__GNUC_PATCHLEVEL__)
	static const char cc[] = "gcc " XSTRINGIFY(__GNUC__) "." XSTRINGIFY(__GNUC_MINOR__) "." XSTRINGIFY(__GNUC_PATCHLEVEL__) "";
#elif defined(__GNUC__) &&		\
      defined(__GNUC_MINOR__)
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

	if (uname(&buf) >= 0) {
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
int stress_unimplemented(const stress_args_t *args)
{
	static const char msg[] = "this stressor is not implemented on "
				  "this system";
	if (args->instance == 0) {
#if defined(HAVE_UNAME) &&	\
    defined(HAVE_SYS_UTSNAME_H)
		struct utsname buf;

		if (uname(&buf) >= 0) {
			if (args->info->unimplemented_reason) {
				pr_inf_skip("%s: %s: %s %s (%s)\n",
					args->name, msg, stress_get_uname_info(),
					stress_get_compiler(),
					args->info->unimplemented_reason);
			} else {
				pr_inf_skip("%s: %s: %s %s\n",
					args->name, msg, stress_get_uname_info(),
					stress_get_compiler());
			}
			return EXIT_NOT_IMPLEMENTED;
		}
#endif
		if (args->info->unimplemented_reason) {
			pr_inf_skip("%s: %s: %s (%s)\n",
				args->name, msg, stress_get_compiler(),
				args->info->unimplemented_reason);
		} else {
			pr_inf_skip("%s: %s: %s\n",
				args->name, msg, stress_get_compiler());
		}
	}
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
	int fds[2];

	if (sz < page_size)
		return -1;

	if (pipe(fds) < 0)
		return -1;

	if (fcntl(fds[0], F_SETPIPE_SZ, sz) < 0)
		return -1;

	(void)close(fds[0]);
	(void)close(fds[1]);
	return 0;
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
	ret = system_read("/proc/sys/fs/pipe-max-size", buf, sizeof(buf));
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
void *stress_align_address(const void *addr, const size_t alignment)
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
	return (geteuid() == 0);
}

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

	(void)memset(&uch, 0, sizeof uch);
	(void)memset(ucd, 0, sizeof ucd);

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

	(void)memset(&uch, 0, sizeof uch);
	(void)memset(ucd, 0, sizeof ucd);

	uch.version = _LINUX_CAPABILITY_VERSION_3;
	uch.pid = getpid();

	ret = capget(&uch, ucd);
	if (ret < 0) {
		pr_fail("%s: capget on pid %d failed: errno=%d (%s)\n",
			name, uch.pid, errno, strerror(errno));
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
	if (ret < 0) {
		pr_fail("%s: capset on pid %d failed: errno=%d (%s)\n",
			name, uch.pid, errno, strerror(errno));
		return -1;
	}
#if defined(HAVE_PRCTL) &&		\
    defined(HAVE_SYS_PRCTL_H) &&	\
    defined(PR_SET_NO_NEW_PRIVS)
	ret = prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0);
	if (ret < 0) {
		/* Older kernels that don't support this prctl throw EINVAL */
		if (errno != EINVAL) {
			pr_inf("%s: prctl PR_SET_NO_NEW_PRIVS on pid %d failed: "
				"errno=%d (%s)\n",
				name, uch.pid, errno, strerror(errno));
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
bool stress_is_dot_filename(const char *name)
{
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
	char *str = strdup(opt);

	if (!str)
		(void)fprintf(stderr, "out of memory duplicating option '%s'\n", opt);

	return str;
}

/*
 *  stress_text_addr()
 *	return length and start/end addresses of text segment
 */
size_t stress_text_addr(char **start, char **end)
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
#elif defined(__TINYC__)
	extern char _start;
	intptr_t text_start = (intptr_t)&_start;
#else
	extern char _start;
	intptr_t text_start = (intptr_t)&_start;
#endif

#if defined(__APPLE__)
	extern void *get_etext(void);
	intptr_t text_end = (intptr_t)get_etext();
#elif defined(__TINYC__)
	extern char _etext;
	intptr_t text_end = (intptr_t)&_etext;
#else
	extern char etext;
	intptr_t text_end = (intptr_t)&etext;
#endif
	const size_t text_len = (size_t)(text_end - text_start);

	if ((start == NULL) || (end == NULL) || (text_start >= text_end))
		return 0;

	*start = (char *)text_start;
	*end = (char *)text_end;

	return text_len;
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

	if (!name)
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
	if (dlist) {
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
	if (!g_shared)
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
uint16_t HOT OPTIMIZE3 stress_ipv4_checksum(uint16_t *ptr, const size_t sz)
{
	register uint32_t sum = 0;
	register size_t n = sz;

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
static int stress_uid_comp(const void *p1, const void *p2)
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

		uids = calloc(n, sizeof(*uids));
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
	*uid = 0;

	return -1;
}
#endif

/*
 *  stress_read_buffer()
 *	In addition to read() this function makes sure all bytes have been
 *	read. You're also able to ignore EINTR interrupts which could happen
 *	on alarm() in the parent process.
 */
ssize_t stress_read_buffer(int fd, void* buffer, ssize_t size, bool ignore_int)
{
	ssize_t rbytes = 0, ret;

	do {
		char *ptr = ((char *)buffer) + rbytes;
ignore_eintr:

		ret = read(fd, (void *)ptr, (size_t)(size - rbytes));
		if (ignore_int && (ret < 0) && (errno == EINTR))
			goto ignore_eintr;
		if (ret > 0)
			rbytes += ret;
	} while (ret > 0 && (rbytes != size));

	return (ret <= 0)? ret : rbytes;
}

/*
 *  stress_write_buffer()
 *	In addition to write() this function makes sure all bytes have been
 *	written. You're also able to ignore EINTR interrupts which could happen
 *	on alarm() in the parent process.
 */
ssize_t stress_write_buffer(int fd, void* buffer, ssize_t size, bool ignore_int)
{
	ssize_t wbytes = 0, ret;

	do {
		char *ptr = ((char *)buffer) + wbytes;
ignore_eintr:
		ret = write(fd, (void *)ptr, (size_t)(size - wbytes));
		/* retry if interrupted */
		if (ignore_int && (ret < 0) && (errno == EINTR))
			goto ignore_eintr;
		if (ret > 0)
			wbytes += ret;
	} while (ret > 0 && (wbytes != size));

	return (ret <= 0)? ret : wbytes;
}

/*
 *  stress_kernel_release()
 *	turn release major.minor.patchlevel triplet into base 100 value
 */
int stress_kernel_release(const int major, const int minor, const int patchlevel)
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
#if defined(HAVE_UNAME)
	struct utsname buf;
	int major = 0, minor = 0, patchlevel = 0;

	if (uname(&buf) < 0)
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
	char buf[64];
#if defined(PID_MAX_LIMIT)
	pid_t max_pid = PID_MAX_LIMIT;
#elif defined(PID_MAX)
	pid_t max_pid = PID_MAX;
#elif defined(PID_MAX_DEFAULT)
	pid_t max_pid = PID_MAX_DEFAULT;
#else
	pid_t max_pid = 32767;
#endif
	int i;
	pid_t pid;
	uint32_t n;

	(void)memset(buf, 0, sizeof(buf));
	if (system_read("/proc/sys/kernel/pid_max", buf, sizeof(buf) - 1) > 0) {
		max_pid = atoi(buf);
	}
	if (max_pid < 1024)
		max_pid = 1024;

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
			    ((kill(pid, 0) < 0) && (errno == ESRCH))) {
				return pid;
			}
		}
	}

	/*
	 *  Make a random PID guess.
	 */
	n = (uint32_t)max_pid - 1023;
	for (i = 0; i < 20; i++) {
		pid = (pid_t)stress_mwc32modn(n) + 1023;

		if ((kill(pid, 0) < 0) && (errno == ESRCH))
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

	return (int)system_read(path, buf, sizeof(buf));
#else
	(void)pid;
	(void)fd;

	return 0;
#endif
}

/*
 *  stress_hostname_length()
 *	return the maximum allowed hostname length
 */
size_t stress_hostname_length(void)
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
 *  stress_min_aux_sig_stack_size()
 *	For ARM we should check AT_MINSIGSTKSZ as this
 *	also includes SVE register saving overhead
 *	https://blog.linuxplumbersconf.org/2017/ocw/system/presentations/4671/original/plumbers-dm-2017.pdf
 */
static inline long stress_min_aux_sig_stack_size(void)
{
#if defined(HAVE_SYS_AUXV_H) && \
    defined(HAVE_GETAUXVAL) &&	\
    defined(AT_MINSIGSTKSZ)
	const long sz = (long)getauxval(AT_MINSIGSTKSZ);

	if (sz > 0)
		return sz;
#else
	UNEXPECTED
#endif
	return -1;
}

/*
 *  stress_sig_stack_size()
 *	wrapper for STRESS_SIGSTKSZ, try and find
 *	stack size required
 */
size_t stress_sig_stack_size(void)
{
	static long sz = -1, min;

	/* return cached copy */
	if (sz > 0)
		return (size_t)sz;

	min = stress_min_aux_sig_stack_size();
#if defined(_SC_SIGSTKSZ)
	sz = sysconf(_SC_SIGSTKSZ);
	min = STRESS_MAXIMUM(sz, min);
#endif
#if defined(SIGSTKSZ)
	min = STRESS_MAXIMUM(SIGSTKSZ, min);
#endif
	sz = STRESS_MAXIMUM(STRESS_ABS_MIN_STACK_SIZE, min);
	return (size_t)sz;
}

/*
 *  stress_min_sig_stack_size()
 *	wrapper for STRESS_MINSIGSTKSZ
 */
size_t stress_min_sig_stack_size(void)
{
	static long sz = -1, min;

	/* return cached copy */
	if (sz > 0)
		return (size_t)sz;

	min = stress_min_aux_sig_stack_size();
#if defined(_SC_MINSIGSTKSZ)
	sz = sysconf(_SC_MINSIGSTKSZ);
	min = STRESS_MAXIMUM(sz, min);
#endif
#if defined(SIGSTKSZ)
	min = STRESS_MAXIMUM(SIGSTKSZ, min);
#endif
	sz = STRESS_MAXIMUM(STRESS_ABS_MIN_STACK_SIZE, min);
	return (size_t)sz;
}

/*
 *  stress_min_pthread_stack_size()
 *	return the minimum size of stack for a pthread
 */
size_t stress_min_pthread_stack_size(void)
{
	static long sz = -1, min;

	/* return cached copy */
	if (sz > 0)
		return (size_t)sz;

	min = stress_min_aux_sig_stack_size();
#if defined(__SC_THREAD_STACK_MIN_VALUE)
	sz = sysconf(__SC_THREAD_STACK_MIN_VALUE);
	min = STRESS_MAXIMUM(sz, min);
#endif
#if defined(_SC_THREAD_STACK_MIN_VALUE)
	sz = sysconf(_SC_THREAD_STACK_MIN_VALUE);
	min = STRESS_MAXIMUM(sz, min);
#endif
#if defined(PTHREAD_STACK_MIN)
	sz = STRESS_MAXIMUM(PTHREAD_STACK_MIN, 8192);
#else
	sz = 8192;
#endif
	sz = STRESS_MAXIMUM(sz, min);
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
#if defined(__GNUC__) &&	\
    !defined(__clang__) &&	\
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
 *  stress_tty_width()
 *	get tty column width
 */
int stress_tty_width(void)
{
	const int max_width = 80;
#if defined(HAVE_WINSIZE) &&	\
    defined(TIOCGWINSZ)
	struct winsize ws;
	int ret;

	ret = ioctl(fileno(stdout), TIOCGWINSZ, &ws);
	if (ret < 0)
		return max_width;
	ret = (int)ws.ws_col;
	if ((ret <= 0) || (ret > 1024))
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

	(void)memset(&fiemap, 0, sizeof(fiemap));
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
bool stress_redo_fork(const int err)
{
	if (keep_stressing_flag() &&
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
		(void)system_write("/sys/kernel/debug/clear_warn_once", "1", 1);
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

	*permutations = NULL;

	for (n_bits = 0, flag_bits = (unsigned int)flags; flag_bits; flag_bits >>= 1U)
		n_bits += (flag_bits & 1U);

	n_flags = 1U << n_bits;
	perms = calloc((size_t)n_flags, sizeof(*perms));
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

/*
 *  stress_fs_magic_to_name()
 *	return the human readable file system type based on fs type magic
 */
const char *stress_fs_magic_to_name(const unsigned long fs_magic)
{
	static char unknown[32];
#if defined(HAVE_LINUX_MAGIC_H) &&	\
    defined(HAVE_SYS_STATFS_H)
	size_t i;

	for (i = 0; i < SIZEOF_ARRAY(stress_fs_names); i++) {
		if (stress_fs_names[i].fs_magic == fs_magic)
			return stress_fs_names[i].fs_name;
	}
#endif
	(void)snprintf(unknown, sizeof(unknown), "unknown 0x%lx", fs_magic);

	return unknown;
}

/*
 *  stress_fs_type()
 *	return the file system type that the given filename is in
 */
const char *stress_fs_type(const char *filename)
{
#if defined(HAVE_LINUX_MAGIC_H) &&	\
    defined(HAVE_SYS_STATFS_H)
	struct statfs buf;
	static char tmp[64];

	if (statfs(filename, &buf) != 0)
		return "";
	(void)snprintf(tmp, sizeof(tmp), ", filesystem type: %s", stress_fs_magic_to_name((unsigned long)buf.f_type));
	return tmp;
#elif (defined(__FreeBSD__) &&		\
       defined(HAVE_SYS_MOUNT_H) &&	\
       defined(HAVE_SYS_PARAM_H)) ||	\
      (defined(__OpenBSD__) &&		\
       defined(HAVE_SYS_MOUNT_H))
	struct statfs buf;
	static char tmp[64];

	if (statfs(filename, &buf) != 0)
		return "";
	(void)snprintf(tmp, sizeof(tmp), ", filesystem type: %s", buf.f_fstypename);
	return tmp;
#else
	(void)filename;

	return "";
#endif
}

/*
 *  Indicate a stress test failed because of limited resources
 *  rather than a failure of the tests during execution.
 *  err is the errno of the failure.
 */
int stress_exit_status(const int err)
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
 *  stress_proc_self_exe_path()
 *	get process' executable path via readlink
 */
static char *stress_proc_self_exe_path(char *path, const char *proc_path, const size_t path_len)
{
	ssize_t len;

	len = shim_readlink(proc_path, path, path_len);
	if ((len < 0) || (len >= PATH_MAX))
		return NULL;
	path[len] = '\0';

	return path;
}

/*
 *  stress_proc_self_exe()
 *  	determine the path to the executable, return NULL if not possible/failed
 */
char *stress_proc_self_exe(char *path, const size_t path_len)
{
#if defined(__linux__)
	return stress_proc_self_exe_path(path, "/proc/self/exe", path_len);
#elif defined(__NetBSD__)
	return stress_proc_self_exe_path(path, "/proc/curproc/exe", path_len);
#elif defined(__DragonFly__)
	return stress_proc_self_exe_path(path, "/proc/curproc/file", path_len);
#elif defined(__FreeBSD__)
	return stress_proc_self_exe_path(path, "/proc/curproc/file", path_len);
#elif defined(__sun__) && 	\
      defined(HAVE_GETEXECNAME)
	const char *execname = getexecname();

	if (!execname)
		return NULL;
	/* Need to perform a string copy to deconstify execname */
	(void)shim_strlcpy(path, execname, path_len);
	return path;
#else
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
	if (!ptr)
		return -1;

	(void)memset(ptr, 0, size);

	ret = sysctlbyname(name, ptr, &nsize, NULL, 0);
	if ((ret < 0) || (nsize != size)) {
		(void)memset(ptr, 0, size);
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

	if (stress_bsd_getsysctl(name, &val, sizeof(val)) == 0)
		return val;
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

	if (n < 1)
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
			(unsigned long)addr,
			(unsigned long)size,
			(unsigned long)name));
#else
	(void)addr;
	(void)size;
	(void)name;
#endif
}

