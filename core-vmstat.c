/*
 * Copyright (C)      2021 Canonical, Ltd.
 * Copyright (C) 2021-2025 Colin Ian King
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
#include "core-cpu-freq.h"
#include "core-killpid.h"
#include "core-pragma.h"
#include "core-rapl.h"
#include "core-thermal-zone.h"
#include "core-vmstat.h"

#include <ctype.h>
#include <math.h>
#include <sched.h>

#if defined(HAVE_MACH_MACH_H)
#include <mach/mach.h>
#endif

#if defined(HAVE_MACH_MACHINE_H)
#include <mach/machine.h>
#endif

#if defined(HAVE_MACH_VM_STATISTICS_H)
#include <mach/vm_statistics.h>
#endif

#if defined(HAVE_SYS_SYSCTL_H) &&	\
    !defined(__linux__)
#include <sys/sysctl.h>
#endif

#include <float.h>

#if defined(HAVE_SYS_SYSMACROS_H)
#include <sys/sysmacros.h>
#endif

#if defined(HAVE_SYS_MKDEV_H)
#include <sys/mkdev.h>
#endif

#if defined(HAVE_SYS_VMMETER_H)
#include <sys/vmmeter.h>
#endif

#if defined(HAVE_UVM_UVM_EXTERN_H)
#include <uvm/uvm_extern.h>
#endif

#if defined(HAVE_MNTENT_H)
#include <mntent.h>
#endif

#if defined(__OpenBSD__)
#include <sys/sched.h>
#endif

/* vmstat information */
typedef struct {			/* vmstat column */
	uint64_t	procs_running;	/* r */
	uint64_t	procs_blocked;	/* b */
	uint64_t	swap_total;	/* swpd info, total */
	uint64_t	swap_free;	/* swpd info, free */
	uint64_t	swap_used;	/* swpd used = total - free */
	uint64_t	memory_free;	/* free */
	uint64_t	memory_buff;	/* buff */
	uint64_t	memory_cached;	/* cached */
	uint64_t	memory_reclaimable; /* reclaimabled cached */
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
} stress_iostat_t;

static uint64_t vmstat_units_kb = 1;	/* kilobytes */

static int32_t status_delay = 0;
static int32_t vmstat_delay = 0;
static int32_t thermalstat_delay = 0;
static int32_t iostat_delay = 0;
static int32_t raplstat_delay = 0;


#if defined(__FreeBSD__)
/*
 *  freebsd_get_cpu_time()
 *	get user, system, idle times; FreeBSD variant
 */
static void freebsd_get_cpu_time(
	uint64_t *user_time,
	uint64_t *system_time,
	uint64_t *idle_time)
{
	const int cpus = stress_bsd_getsysctl_int("kern.smp.cpus");
	long int *vals;
	int i;

	*user_time = 0;
	*system_time = 0;
	*idle_time = 0;

	vals = (long int *)calloc(cpus * 5, sizeof(*vals));
	if (UNLIKELY(!vals))
		return;

	if (stress_bsd_getsysctl("kern.cp_times", vals, cpus * 5 * sizeof(*vals)) < 0)
		return;
	for (i = 0; i < cpus * 5; i += 5) {
		*user_time += vals[i];
		*system_time += vals[i + 2];
		*idle_time += vals[i + 4];
	}
	free(vals);
}
#endif

#if defined(__NetBSD__)
/*
 *  freebsd_get_cpu_time()
 *	get user, system, idle times; NetBSD variant
 */
static void netbsd_get_cpu_time(
	uint64_t *user_time,
	uint64_t *system_time,
	uint64_t *idle_time)
{
	long int vals[5];

	*user_time = 0;
	*system_time = 0;
	*idle_time = 0;

	if (stress_bsd_getsysctl("kern.cp_time", vals, sizeof(vals)) < 0)
		return;
	*user_time = vals[0];
	*system_time = vals[2];
	*idle_time = vals[4];
}
#endif

/*
 *  stress_set_generic_stat()
 *	parse and check op for valid time range
 */
static int stress_set_generic_stat(
	const char *const opt,
	const char *name,
	int32_t *delay)
{
	const uint64_t delay64 = stress_get_uint64_time(opt);

        if (UNLIKELY((delay64 < 1) || (delay64 > 3600))) {
                (void)fprintf(stderr, "%s must in the range 1 to 3600 seconds.\n", name);
                _exit(EXIT_FAILURE);
        }
	*delay = (int32_t)(delay64 & 0x7fffffff);
	return 0;
}

/*
 *  stress_set_status()
 *	parse --status option
 */
int stress_set_status(const char *const opt)
{
	return stress_set_generic_stat(opt, "status", &status_delay);
}

/*
 *  stress_set_vmstat()
 *	parse --vmstat option
 */
int stress_set_vmstat(const char *const opt)
{
	return stress_set_generic_stat(opt, "vmstat", &vmstat_delay);
}

void stress_set_vmstat_units(const char *const opt)
{
	vmstat_units_kb = stress_get_uint64_byte_scale(opt) / 1024;
}

/*
 *  stress_set_thermalstat()
 *	parse --thermalstat option
 */
int stress_set_thermalstat(const char *const opt)
{
	g_opt_flags |= OPT_FLAGS_TZ_INFO;
	return stress_set_generic_stat(opt, "thermalstat", &thermalstat_delay);
}

/*
 *  stress_set_iostat()
 *	parse --iostat option
 */
int stress_set_iostat(const char *const opt)
{
	return stress_set_generic_stat(opt, "iostat", &iostat_delay);
}

/*
 *  stress_set_raplstat()
 *	parse --raplstat option
 */
int stress_set_raplstat(const char *const opt)
{
	return stress_set_generic_stat(opt, "raplstat", &raplstat_delay);
}

/*
 *  stress_find_mount_dev()
 *	find the path of the device that the file is located on
 */
char *stress_find_mount_dev(const char *name)
{
#if defined(__linux__) && 	\
    defined(HAVE_GETMNTENT) &&	\
    defined(HAVE_ENDMNTENT) &&	\
    defined(HAVE_SETMNTENT)
	static char dev_path[PATH_MAX];
	struct stat statbuf;
	dev_t dev;
	FILE *mtab_fp;
	struct mntent *mnt;

	if (shim_stat(name, &statbuf) < 0)
		return NULL;

	/* Cater for UBI char mounts */
	if (S_ISBLK(statbuf.st_mode) || S_ISCHR(statbuf.st_mode))
		dev = statbuf.st_rdev;
	else
		dev = statbuf.st_dev;

	/* Try /proc/mounts then /etc/mtab */
	mtab_fp = setmntent("/proc/mounts", "r");
	if (!mtab_fp) {
		mtab_fp = setmntent("/etc/mtab", "r");
		if (!mtab_fp)
			return NULL;
	}

	while ((mnt = getmntent(mtab_fp))) {
		if ((!strcmp(name, mnt->mnt_dir)) ||
		    (!strcmp(name, mnt->mnt_fsname)))
			break;

		if ((mnt->mnt_fsname[0] == '/') &&
		    (shim_stat(mnt->mnt_fsname, &statbuf) == 0) &&
		    (statbuf.st_rdev == dev))
			break;

		if ((shim_stat(mnt->mnt_dir, &statbuf) == 0) &&
		    (statbuf.st_dev == dev))
			break;
	}
	(void)endmntent(mtab_fp);

	if (!mnt)
		return NULL;
	if (!mnt->mnt_fsname)
		return NULL;
	return realpath(mnt->mnt_fsname, dev_path);
#elif defined(HAVE_SYS_SYSMACROS_H)
	static char dev_path[PATH_MAX];
	struct stat statbuf;
	dev_t dev;
	DIR *dir;
	struct dirent *d;
	dev_t majdev;

	if (shim_stat(name, &statbuf) < 0)
		return NULL;

	/* Cater for UBI char mounts */
	if (S_ISBLK(statbuf.st_mode) || S_ISCHR(statbuf.st_mode))
		dev = statbuf.st_rdev;
	else
		dev = statbuf.st_dev;

#if defined(__QNXNTO__)
	majdev = makedev(0, major(dev), 0);
#else
	majdev = makedev(major(dev), 0);
#endif

	dir = opendir("/dev");
	if (!dir)
		return NULL;

	while ((d = readdir(dir)) != NULL) {
		int ret;
		struct stat stat_buf;

		stress_mk_filename(dev_path, sizeof(dev_path), "/dev", d->d_name);
		ret = shim_stat(dev_path, &stat_buf);
		if ((ret == 0) &&
		    (S_ISBLK(stat_buf.st_mode)) &&
		    (stat_buf.st_rdev == majdev)) {
			(void)closedir(dir);
			return dev_path;
		}
	}
	(void)closedir(dir);

	return NULL;
#else
	(void)name;

	return NULL;
#endif
}

static pid_t vmstat_pid;

#if defined(HAVE_SYS_SYSMACROS_H) &&	\
    defined(__linux__)

/*
 *  stress_iostat_iostat_name()
 *	from the stress-ng temp file path try to determine
 *	the iostat file /sys/block/$dev/stat for that file.
 */
static char *stress_iostat_iostat_name(
	char *iostat_name,
	const size_t iostat_name_len)
{
	char *temp_path, *dev, *ptr;
	struct stat statbuf;

	/* Resolve links */
	temp_path = realpath(stress_get_temp_path(), NULL);
	if (UNLIKELY(!temp_path))
		return NULL;

	/* Find device */
	dev = stress_find_mount_dev(temp_path);
	if (!dev)
		return NULL;

	/* Skip over leading /dev */
	if (!strncmp(dev, "/dev", 4))
		dev += 4;
	if (*dev == '/')
		dev++;

	ptr = dev + strlen(dev) - 1;

	/*
	 *  Try /dev/sda12, then /dev/sda1, then /dev/sda, then terminate
	 */
	while (ptr >= dev) {
		(void)snprintf(iostat_name, iostat_name_len, "/sys/block/%s/stat", dev);
		if (shim_stat(iostat_name, &statbuf) == 0)
			return iostat_name;
		if (!isdigit((unsigned char)*ptr))
			break;
		*ptr = '\0';
		ptr--;
	}

	return NULL;
}

/*
 *  stress_read_iostat()
 *	read the stats from an iostat stat file, linux variant
 */
static void stress_read_iostat(const char *iostat_name, stress_iostat_t *iostat)
{
	FILE *fp;

	fp = fopen(iostat_name, "r");
	if (fp) {
		int ret;

		ret = fscanf(fp,
			    "%" SCNu64 " %" SCNu64
			    " %" SCNu64 " %" SCNu64
			    " %" SCNu64 " %" SCNu64
			    " %" SCNu64 " %" SCNu64
			    " %" SCNu64 " %" SCNu64
			    " %" SCNu64,
			&iostat->read_io, &iostat->read_merges,
			&iostat->read_sectors, &iostat->read_ticks,
			&iostat->write_io, &iostat->write_merges,
			&iostat->write_sectors, &iostat->write_ticks,
			&iostat->in_flight, &iostat->io_ticks,
			&iostat->time_in_queue);
		(void)fclose(fp);

		if (ret != 11)
			(void)shim_memset(iostat, 0, sizeof(*iostat));
	}
}

#define STRESS_IOSTAT_DELTA(field)					\
	iostat->field = ((iostat_current.field > iostat_prev.field) ?	\
	(iostat_current.field - iostat_prev.field) : 0)

/*
 *  stress_get_iostat()
 *	read and compute delta since last read of iostats
 */
static void stress_get_iostat(const char *iostat_name, stress_iostat_t *iostat)
{
	static stress_iostat_t iostat_prev;
	stress_iostat_t iostat_current;

	(void)shim_memset(&iostat_current, 0, sizeof(iostat_current));
	stress_read_iostat(iostat_name, &iostat_current);
	STRESS_IOSTAT_DELTA(read_io);
	STRESS_IOSTAT_DELTA(read_merges);
	STRESS_IOSTAT_DELTA(read_sectors);
	STRESS_IOSTAT_DELTA(read_ticks);
	STRESS_IOSTAT_DELTA(write_io);
	STRESS_IOSTAT_DELTA(write_merges);
	STRESS_IOSTAT_DELTA(write_sectors);
	STRESS_IOSTAT_DELTA(write_ticks);
	STRESS_IOSTAT_DELTA(in_flight);
	STRESS_IOSTAT_DELTA(io_ticks);
	STRESS_IOSTAT_DELTA(time_in_queue);
	(void)shim_memcpy(&iostat_prev, &iostat_current, sizeof(iostat_prev));
}
#endif

#if defined(__linux__)
/*
 *  stress_next_field()
 *	skip to next field, returns false if end of
 *	string and/or no next field.
 */
static bool stress_next_field(char **str)
{
	char *ptr = *str;

	while (*ptr && (*ptr != ' '))
		ptr++;

	if (!*ptr)
		return false;

	while (*ptr == ' ')
		ptr++;

	if (!*ptr)
		return false;

	*str = ptr;
	return true;
}

/*
 *  stress_read_vmstat()
 *	read vmstat statistics
 */
static void stress_read_vmstat(stress_vmstat_t *vmstat)
{
	FILE *fp;
	char buffer[1024];

	fp = fopen("/proc/stat", "r");
	if (fp) {
		while (fgets(buffer, sizeof(buffer), fp)) {
			char *ptr = buffer;

			if (!strncmp(buffer, "cpu ", 4))
				continue;
			if (!strncmp(buffer, "cpu", 3)) {
				if (!stress_next_field(&ptr))
					continue;
				/* user time */
				vmstat->user_time += (uint64_t)atoll(ptr);
				if (!stress_next_field(&ptr))
					continue;

				/* user time nice */
				vmstat->user_time += (uint64_t)atoll(ptr);
				if (!stress_next_field(&ptr))
					continue;

				/* system time */
				vmstat->system_time += (uint64_t)atoll(ptr);
				if (!stress_next_field(&ptr))
					continue;

				/* idle time */
				vmstat->idle_time += (uint64_t)atoll(ptr);
				if (!stress_next_field(&ptr))
					continue;

				/* iowait time */
				vmstat->wait_time += (uint64_t)atoll(ptr);
				if (!stress_next_field(&ptr))
					continue;

				/* irq time, account in system time */
				vmstat->system_time += (uint64_t)atoll(ptr);
				if (!stress_next_field(&ptr))
					continue;

				/* soft time, account in system time */
				vmstat->system_time += (uint64_t)atoll(ptr);
				if (!stress_next_field(&ptr))
					continue;

				/* stolen time */
				vmstat->stolen_time += (uint64_t)atoll(ptr);
				if (!stress_next_field(&ptr))
					continue;

				/* guest time, add to stolen stats */
				vmstat->stolen_time += (uint64_t)atoll(ptr);
				if (!stress_next_field(&ptr))
					continue;

				/* guest_nice time, add to stolen stats */
				vmstat->stolen_time += (uint64_t)atoll(ptr);
				if (!stress_next_field(&ptr))
					continue;
			}

			if (!strncmp(buffer, "intr", 4)) {
				if (!stress_next_field(&ptr))
					continue;
				/* interrupts */
				vmstat->interrupt = (uint64_t)atoll(ptr);
			}
			if (!strncmp(buffer, "ctxt", 4)) {
				if (!stress_next_field(&ptr))
					continue;
				/* context switches */
				vmstat->context_switch = (uint64_t)atoll(ptr);
			}
			if (!strncmp(buffer, "procs_running", 13)) {
				if (!stress_next_field(&ptr))
					continue;
				/* processes running */
				vmstat->procs_running = (uint64_t)atoll(ptr);
			}
			if (!strncmp(buffer, "procs_blocked", 13)) {
				if (!stress_next_field(&ptr))
					continue;
				/* procesess blocked */
				vmstat->procs_blocked = (uint64_t)atoll(ptr);
			}
			if (!strncmp(buffer, "swap", 4)) {
				if (!stress_next_field(&ptr))
					continue;
				/* swap in */
				vmstat->swap_in = (uint64_t)atoll(ptr);

				if (!stress_next_field(&ptr))
					continue;
				/* swap out */
				vmstat->swap_out = (uint64_t)atoll(ptr);
			}
		}
		(void)fclose(fp);
	}

	fp = fopen("/proc/meminfo", "r");
	if (fp) {
		while (fgets(buffer, sizeof(buffer), fp)) {
			char *ptr = buffer;

			if (!strncmp(buffer, "MemFree", 7)) {
				if (!stress_next_field(&ptr))
					continue;
				vmstat->memory_free = (uint64_t)atoll(ptr);
			}
			if (!strncmp(buffer, "Buffers", 7)) {
				if (!stress_next_field(&ptr))
					continue;
				vmstat->memory_buff = (uint64_t)atoll(ptr);
			}
			if (!strncmp(buffer, "Cached", 6)) {
				if (!stress_next_field(&ptr))
					continue;
				vmstat->memory_cached = (uint64_t)atoll(ptr);
			}
			if (!strncmp(buffer, "KReclaimable", 12)) {
				if (!stress_next_field(&ptr))
					continue;
				vmstat->memory_reclaimable = (uint64_t)atoll(ptr);
			}
			if (!strncmp(buffer, "SwapTotal", 9)) {
				if (!stress_next_field(&ptr))
					continue;
				vmstat->swap_total = (uint64_t)atoll(ptr);
			}
			if (!strncmp(buffer, "SwapFree", 8)) {
				if (!stress_next_field(&ptr))
					continue;
				vmstat->swap_free = (uint64_t)atoll(ptr);
			}
			if (!strncmp(buffer, "SwapUsed", 8)) {
				if (!stress_next_field(&ptr))
					continue;
				vmstat->swap_used = (uint64_t)atoll(ptr);
			}
		}
		(void)fclose(fp);

		if ((vmstat->swap_used == 0) &&
		    (vmstat->swap_free > 0) &&
		    (vmstat->swap_total > 0)) {
			vmstat->swap_used = vmstat->swap_total - vmstat->swap_free;
		}
	}

	fp = fopen("/proc/vmstat", "r");
	if (fp) {
		while (fgets(buffer, sizeof(buffer), fp)) {
			char *ptr = buffer;

			if (!strncmp(buffer, "pgpgin", 6)) {
				if (!stress_next_field(&ptr))
					continue;
				vmstat->block_in = (uint64_t)atoll(ptr);
			}
			if (!strncmp(buffer, "pgpgout", 7)) {
				if (!stress_next_field(&ptr))
					continue;
				vmstat->block_out = (uint64_t)atoll(ptr);
			}
			if (!strncmp(buffer, "pswpin", 6)) {
				if (!stress_next_field(&ptr))
					continue;
				vmstat->swap_in = (uint64_t)atoll(ptr);
			}
			if (!strncmp(buffer, "pswpout", 7)) {
				if (!stress_next_field(&ptr))
					continue;
				vmstat->swap_out = (uint64_t)atoll(ptr);
			}
		}
		(void)fclose(fp);
	}
}
#elif defined(__FreeBSD__)
/*
 *  stress_read_vmstat()
 *	read vmstat statistics, FreeBSD variant, partially implemented
 */
static void stress_read_vmstat(stress_vmstat_t *vmstat)
{
#if defined(HAVE_SYS_VMMETER_H)
	struct vmtotal t;
#endif

	vmstat->interrupt = stress_bsd_getsysctl_uint64("vm.stats.sys.v_intr");
	vmstat->context_switch = stress_bsd_getsysctl_uint64("vm.stats.sys.v_swtch");
	vmstat->swap_in = stress_bsd_getsysctl_uint64("vm.stats.vm.v_swapin");
	vmstat->swap_out = stress_bsd_getsysctl_uint64("vm.stats.vm.v_swapout");
	vmstat->block_in = stress_bsd_getsysctl_uint64("vm.stats.vm.v_vnodepgsin");
	vmstat->block_out = stress_bsd_getsysctl_uint64("vm.stats.vm.v_vnodepgsin");
	vmstat->memory_free = (uint64_t)stress_bsd_getsysctl_uint32("vm.stats.vm.v_free_count");
	vmstat->memory_cached = (uint64_t)stress_bsd_getsysctl_uint("vm.stats.vm.v_cache_count");

	freebsd_get_cpu_time(&vmstat->user_time, &vmstat->system_time, &vmstat->idle_time);

#if defined(HAVE_SYS_VMMETER_H)
	if (stress_bsd_getsysctl("vm.vmtotal", &t, sizeof(t)) == 0) {
		vmstat->procs_running = t.t_rq - 1;
		vmstat->procs_blocked = t.t_dw + t.t_pw;
	}
#endif
}
#elif defined(__NetBSD__)
/*
 *  stress_read_vmstat()
 *	read vmstat statistics, NetBSD variant, partially implemented
 */
static void stress_read_vmstat(stress_vmstat_t *vmstat)
{
#if defined(HAVE_SYS_VMMETER_H)
	struct vmtotal t;
#endif
#if defined(HAVE_UVM_UVM_EXTERN_H)
	struct uvmexp_sysctl u;
#endif
	netbsd_get_cpu_time(&vmstat->user_time, &vmstat->system_time, &vmstat->idle_time);
#if defined(HAVE_UVM_UVM_EXTERN_H)
	if (stress_bsd_getsysctl("vm.uvmexp2", &u, sizeof(u)) == 0) {
		vmstat->memory_cached = u.filepages;	/* Guess */
		vmstat->interrupt = u.intrs;
		vmstat->context_switch = u.swtch;
		vmstat->swap_in = u.pgswapin;
		vmstat->swap_out = u.pgswapout;
		vmstat->swap_used = u.swpginuse;
		vmstat->memory_free = u.free;
	}
#endif

#if defined(HAVE_SYS_VMMETER_H)
	if (stress_bsd_getsysctl("vm.vmmeter", &t, sizeof(t)) == 0) {
		vmstat->procs_running = t.t_rq - 1;
		vmstat->procs_blocked = t.t_dw + t.t_pw;
	}
#endif
}
#elif defined(__OpenBSD__)
/*
 *  stress_read_vmstat()
 *	read vmstat statistics, OS X variant, partially implemented
 */
static void stress_read_vmstat(stress_vmstat_t *vmstat)
{
	int mib[2];
	struct uvmexp u;
	size_t size;
	long int cp_time[CPUSTATES];
	struct vmtotal t;

	mib[0] = CTL_VM;
	mib[1] = VM_METER;
	size = sizeof(t);
	if (sysctl(mib, 2, &t, &size, NULL, 0) == 0) {
		vmstat->procs_running = t.t_rq - 1;
		vmstat->procs_blocked = t.t_sl;
	}

	mib[0] = CTL_VM;
	mib[1] = VM_UVMEXP;
	size = sizeof(struct uvmexp);

	if (sysctl(mib, 2, &u, &size, NULL, 0) == 0) {
		vmstat->memory_cached = 0;
		vmstat->interrupt = u.intrs;
		vmstat->context_switch = u.swtch;
		vmstat->swap_in = u.pageins;
		vmstat->swap_out = u.pdpageouts;
		vmstat->swap_used = (u.swpginuse * (u.pagesize >> 10));
		vmstat->memory_free = (u.free * (u.pagesize >> 10));
	}

	mib[0] = CTL_KERN;
	mib[1] = KERN_CPTIME;
	size = sizeof(cp_time);

	if (sysctl(mib, 2, cp_time, &size, NULL, 0) == 0) {
		vmstat->user_time = (double)(cp_time[CP_USER] + cp_time[CP_NICE]);
#if defined(CP_SPIN)
		vmstat->system_time = (double)(cp_time[CP_SYS] + cp_time[CP_SPIN] + cp_time[CP_INTR]);
#else
		vmstat->system_time = (double)(cp_time[CP_SYS] + cp_time[CP_INTR]);
#endif
		vmstat->idle_time = (double)cp_time[CP_IDLE];
	}
}

#elif defined(__APPLE__) &&		\
      defined(HAVE_SYS_SYSCTL_H) &&	\
      defined(HAVE_MACH_MACH_H) &&	\
      defined(HAVE_MACH_VM_STATISTICS_H)
/*
 *  stress_read_vmstat()
 *	read vmstat statistics, OS X variant, partially implemented
 */
static void stress_read_vmstat(stress_vmstat_t *vmstat)
{
	vm_statistics64_data_t vm_stat;
	struct xsw_usage xsu;
	mach_port_t host = mach_host_self();
	natural_t count = HOST_VM_INFO64_COUNT;
	size_t page_size = stress_get_page_size();
	int ret;

	(void)shim_memset(&vm_stat, 0, sizeof(vmstat));
	ret = host_statistics64(host, HOST_VM_INFO64, (host_info64_t)&vm_stat, &count);
	if (ret >= 0) {
		vmstat->swap_in = vm_stat.pageins;
		vmstat->swap_out = vm_stat.pageouts;
		vmstat->memory_free = (page_size / 1024) * vm_stat.free_count;
	}
	ret = stress_bsd_getsysctl("vm.swapusage", &xsu, sizeof(xsu));
	if (ret >= 0) {
		vmstat->swap_total = xsu.xsu_total;
		vmstat->swap_used = xsu.xsu_used;
		vmstat->swap_free = xsu.xsu_avail;
	}
	vmstat->user_time = 0;
	vmstat->system_time= 0;
	vmstat->idle_time = 0;
	vmstat->wait_time = 0;
	vmstat->stolen_time = 0;
#if defined(HAVE_MACH_MACH_H) &&	\
    defined(PROCESSOR_CPU_LOAD_INFO) && \
    defined(CPU_STATE_USER) &&		\
    defined(CPU_STATE_SYSTEM) &&	\
    defined(CPU_STATE_IDLE)
	{
		natural_t pcount, pi_array_count;
		processor_info_array_t pi_array;

		ret = host_processor_info(host, PROCESSOR_CPU_LOAD_INFO, &pcount, &pi_array, &pi_array_count);
		if (ret >= 0) {
			natural_t i;

			for (i = 0; i < pi_array_count; i++) {
				const integer_t *cpu_ticks = &pi_array[i * CPU_STATE_MAX];

				vmstat->user_time += cpu_ticks[CPU_STATE_USER];
				vmstat->system_time += cpu_ticks[CPU_STATE_SYSTEM];
				vmstat->idle_time += cpu_ticks[CPU_STATE_IDLE];
			}
		}
	}
#endif
#if defined(HAVE_SYS_SYSCTL_H) &&	\
    defined(CTL_KERN) &&		\
    defined(KERN_PROC) &&		\
    defined(KERN_PROC_ALL) &&		\
    defined(SRUN)
	{
		size_t length;
		/* name must not be const, sysctl does not take const name param */
		static int name[] = { CTL_KERN, KERN_PROC, KERN_PROC_ALL, 0 };

		vmstat->procs_running = 0;
		vmstat->procs_blocked = 0;

		for (;;) {
			struct kinfo_proc *result;
			size_t i, n;

			ret = sysctl((int *)name, (sizeof(name)/sizeof(*name))-1, NULL,
				&length, NULL, 0);
			if (ret < 0)
				break;

			result = (struct kinfo_proc *)malloc(length);
			if (UNLIKELY(!result))
				break;

			ret = sysctl((int *)name, (sizeof(name)/sizeof(*name))-1, result,
				&length, NULL, 0);
			if (ret < 0) {
				free(result);
				break;
			}

			n = length / sizeof(struct kinfo_proc);
			for (i = 0; i < n; i++) {
				if (result[i].kp_proc.p_flag & P_SYSTEM)
					continue;
				switch (result[i].kp_proc.p_stat) {
				case SRUN:
					vmstat->procs_running++;
					break;
				default:
					vmstat->procs_blocked++;
					break;
				}
			}
			free(result);
			if (ret == 0)
				break;
		}
	}
#endif
}
#else
/*
 *  stress_read_vmstat()
 *	read vmstat statistics, no-op
 */
static void stress_read_vmstat(stress_vmstat_t *vmstat)
{
	(void)vmstat;
}
#endif

#define STRESS_VMSTAT_COPY(field)	vmstat->field = (vmstat_current.field)
#define STRESS_VMSTAT_DELTA(field)					\
	vmstat->field = ((vmstat_current.field > vmstat_prev.field) ?	\
	(vmstat_current.field - vmstat_prev.field) : 0)

/*
 *  stress_get_vmstat()
 *	collect vmstat data, zero for initial read
 */
static void stress_get_vmstat(stress_vmstat_t *vmstat)
{
	static stress_vmstat_t vmstat_prev;
	stress_vmstat_t vmstat_current;

	(void)shim_memset(&vmstat_current, 0, sizeof(vmstat_current));
	(void)shim_memset(vmstat, 0, sizeof(*vmstat));
	stress_read_vmstat(&vmstat_current);
	STRESS_VMSTAT_COPY(procs_running);
	STRESS_VMSTAT_COPY(procs_blocked);
	STRESS_VMSTAT_COPY(swap_total);
	STRESS_VMSTAT_COPY(swap_used);
	STRESS_VMSTAT_COPY(swap_free);
	STRESS_VMSTAT_COPY(memory_free);
	STRESS_VMSTAT_COPY(memory_buff);
	STRESS_VMSTAT_COPY(memory_cached);
	STRESS_VMSTAT_COPY(memory_reclaimable);
	STRESS_VMSTAT_DELTA(swap_in);
	STRESS_VMSTAT_DELTA(swap_out);
	STRESS_VMSTAT_DELTA(block_in);
	STRESS_VMSTAT_DELTA(block_out);
	STRESS_VMSTAT_DELTA(interrupt);
	STRESS_VMSTAT_DELTA(context_switch);
	STRESS_VMSTAT_DELTA(user_time);
	STRESS_VMSTAT_DELTA(system_time);
	STRESS_VMSTAT_DELTA(idle_time);
	STRESS_VMSTAT_DELTA(wait_time);
	STRESS_VMSTAT_DELTA(stolen_time);
	(void)shim_memcpy(&vmstat_prev, &vmstat_current, sizeof(vmstat_prev));
}

#if defined(__linux__)
/*
 *  stress_get_tz_info()
 *	get temperature in degrees C from a thermal zone
 */
static double stress_get_tz_info(const stress_tz_info_t *tz_info)
{
	double temp = 0.0;
	FILE *fp;
	char path[PATH_MAX];

	(void)snprintf(path, sizeof(path),
		"/sys/class/thermal/%s/temp",
		tz_info->path);

	if ((fp = fopen(path, "r")) != NULL) {
		if (fscanf(fp, "%lf", &temp) == 1)
			temp /= 1000.0;
		(void)fclose(fp);
	}
	return temp;
}
#endif

/*
 *  stress_vmstat_start()
 *	start vmstat statistics (1 per second)
 */
void stress_vmstat_start(void)
{
	stress_vmstat_t vmstat;
	size_t tz_num = 0;
	stress_tz_info_t *tz_info;
	int32_t vmstat_sleep, thermalstat_sleep, iostat_sleep, status_sleep, raplstat_sleep;
	double t1, t2, t_start;
#if defined(HAVE_SYS_SYSMACROS_H) &&	\
    defined(__linux__)
	char iostat_name[PATH_MAX];
	stress_iostat_t iostat;
#endif
	bool thermalstat_zero = true;

	if ((vmstat_delay == 0) &&
	    (thermalstat_delay == 0) &&
	    (iostat_delay == 0) &&
	    (status_delay == 0) &&
	    (raplstat_delay == 0))
		return;

	vmstat_sleep = vmstat_delay;
	thermalstat_sleep = thermalstat_delay;
	iostat_sleep = iostat_delay;
	status_sleep = status_delay;
	raplstat_sleep = raplstat_delay;

	vmstat_pid = fork();
	if ((vmstat_pid < 0) || (vmstat_pid > 0))
		return;

	stress_parent_died_alarm();
	stress_set_proc_name("stat [periodic]");

	if (vmstat_delay)
		stress_get_vmstat(&vmstat);

	if (thermalstat_delay) {
		for (tz_info = g_shared->tz_info; tz_info; tz_info = tz_info->next)
			tz_num++;
	}
#if defined(STRESS_RAPL)
	if (raplstat_delay && (g_opt_flags & OPT_FLAGS_RAPL_REQUIRED))
		stress_rapl_get_power_raplstat(g_shared->rapl_domains);
#endif

#if defined(HAVE_SYS_SYSMACROS_H) &&	\
    defined(__linux__)
	if (stress_iostat_iostat_name(iostat_name, sizeof(iostat_name)) == NULL)
		iostat_sleep = 0;
	if (iostat_delay)
		stress_get_iostat(iostat_name, &iostat);
#endif

#if defined(SCHED_DEADLINE)
	VOID_RET(int, stress_set_sched(getpid(), SCHED_DEADLINE, 99, true));
#endif

	t_start = stress_time_now();
	t1 = t_start;

	while (stress_continue_flag()) {
		int32_t sleep_delay = INT_MAX;
		double delta;

		if (vmstat_delay > 0)
			sleep_delay = STRESS_MINIMUM(vmstat_delay, sleep_delay);
		if (thermalstat_delay > 0)
			sleep_delay = thermalstat_zero ? 0 : STRESS_MINIMUM(thermalstat_delay, sleep_delay);
#if defined(HAVE_SYS_SYSMACROS_H) &&	\
    defined(__linux__)
		if (iostat_delay > 0)
			sleep_delay = STRESS_MINIMUM(iostat_delay, sleep_delay);
#endif
		if (status_delay > 0)
			sleep_delay = STRESS_MINIMUM(status_delay, sleep_delay);
		if (raplstat_delay > 0)
			sleep_delay = STRESS_MINIMUM(raplstat_delay, raplstat_delay);
		t1 += sleep_delay;
		t2 = stress_time_now();

		delta = t1 - t2;
		if (delta > 0) {
			const uint64_t nsec = (uint64_t)(delta * STRESS_DBL_NANOSECOND);

			(void)shim_nanosleep_uint64(nsec);
		}

		vmstat_sleep -= sleep_delay;
		thermalstat_sleep -= sleep_delay;
		iostat_sleep -= sleep_delay;
		status_sleep -= sleep_delay;

		if ((vmstat_delay > 0) && (vmstat_sleep <= 0))
			vmstat_sleep = vmstat_delay;
		if ((iostat_delay > 0) && (iostat_sleep <= 0))
			iostat_sleep = iostat_delay;
		if ((status_delay > 0) && (status_sleep <= 0))
			status_sleep = status_delay;
		if ((raplstat_delay > 0) && (raplstat_sleep <= 0))
			raplstat_sleep = raplstat_delay;
		if ((thermalstat_delay > 0) && (thermalstat_sleep <= 0))
			thermalstat_sleep = thermalstat_delay;

		if ((sleep_delay > 0) && (vmstat_sleep == vmstat_delay)) {
			static uint32_t vmstat_count = 0;
			double total_ticks, percent;

			stress_get_vmstat(&vmstat);

			pr_block_begin();
			if (vmstat_count == 0)
				pr_inf("vmstat: %3s %3s %9s %9s %9s %9s "
					"%4s %4s %6s %6s %4s %4s %2s %2s "
					"%2s %2s %2s\n",
					"r", "b", "swpd", "free", "buff",
					"cache", "si", "so", "bi", "bo",
					"in", "cs", "us", "sy", "id",
					"wa", "st");

			total_ticks = (double)vmstat.user_time +
				      (double)vmstat.system_time +
				      (double)vmstat.idle_time +
				      (double)vmstat.wait_time +
				      (double)vmstat.stolen_time;
			percent = (total_ticks > 0.0) ? 100.0 / total_ticks : 0.0;

			pr_inf("vmstat: %3" PRIu64 " %3" PRIu64 /* procs */
			       " %9" PRIu64 " %9" PRIu64	/* vm used */
			       " %9" PRIu64 " %9" PRIu64	/* memory_buff */
			       " %4" PRIu64 " %4" PRIu64	/* si, so*/
			       " %6" PRIu64 " %6" PRIu64	/* bi, bo*/
			       " %4" PRIu64 " %4" PRIu64	/* int, cs*/
			       " %2.0f %2.0f" 			/* us, sy */
			       " %2.0f %2.0f" 			/* id, wa */
			       " %2.0f\n",			/* st */
				vmstat.procs_running,
				vmstat.procs_blocked,
				vmstat.swap_used / vmstat_units_kb,
				vmstat.memory_free / vmstat_units_kb,
				vmstat.memory_buff / vmstat_units_kb,
				(vmstat.memory_cached + vmstat.memory_reclaimable) / vmstat_units_kb,
				vmstat.swap_in / (uint64_t)vmstat_delay,
				vmstat.swap_out / (uint64_t)vmstat_delay,
				vmstat.block_in / (uint64_t)vmstat_delay,
				vmstat.block_out / (uint64_t)vmstat_delay,
				vmstat.interrupt / (uint64_t)vmstat_delay,
				vmstat.context_switch / (uint64_t)vmstat_delay,
				percent * (double)vmstat.user_time,
				percent * (double)vmstat.system_time,
				percent * (double)vmstat.idle_time,
				percent * (double)vmstat.wait_time,
				percent * (double)vmstat.stolen_time);
			pr_block_end();

			vmstat_count++;
			if (vmstat_count >= 25)
				vmstat_count = 0;
		}

		if (thermalstat_delay == thermalstat_sleep) {
			double min1, min5, min15, avg_ghz, min_ghz, max_ghz;
			size_t therms_len = 1 + (tz_num * 7);
			char *therms;
			char cpuspeed[19];
#if defined(__linux__)
			char *ptr;
#endif
			static uint32_t thermalstat_count = 0;

			thermalstat_zero = false;
			therms = (char *)calloc(therms_len, sizeof(*therms));
			if (therms) {
#if defined(__linux__)
				for (ptr = therms, tz_info = g_shared->tz_info; tz_info; tz_info = tz_info->next) {
					(void)snprintf(ptr, 8, " %6.6s", tz_info->type);
					ptr += 7;
				}
#endif

				stress_get_cpu_freq(&avg_ghz, &min_ghz, &max_ghz);
				if (avg_ghz > 0.0)
					(void)snprintf(cpuspeed, sizeof(cpuspeed), "%5.2f %5.2f %5.2f",
						avg_ghz, min_ghz, max_ghz);
				else
					(void)snprintf(cpuspeed, sizeof(cpuspeed), "%5.5s %5.5s %5.5s",
						" n/a ", " n/a ", " n/a ");

				pr_block_begin();
				if (thermalstat_count == 0)
					pr_inf("therm: AvGHz MnGHz MxGHz  LdA1  LdA5 LdA15 %s\n", therms);

#if defined(__linux__)
				for (ptr = therms, tz_info = g_shared->tz_info; tz_info; tz_info = tz_info->next) {
					(void)snprintf(ptr, 8, " %6.2f", stress_get_tz_info(tz_info));
					ptr += 7;
				}
#endif
				if (stress_get_load_avg(&min1, &min5, &min15) < 0)  {
					pr_inf("therm: %18s %5.5s %5.5s %5.5s %s\n",
						cpuspeed, "n/a", "n/a", "n/a", therms);
				} else {
					pr_inf("therm: %5s %5.2f %5.2f %5.2f %s\n",
						cpuspeed, min1, min5, min15, therms);
				}
				pr_block_end();
				free(therms);

				thermalstat_count++;
				if (thermalstat_count >= 25)
					thermalstat_count = 0;
			}
		}

#if defined(HAVE_SYS_SYSMACROS_H) &&	\
    defined(__linux__)
		if ((sleep_delay > 0) && (iostat_delay == iostat_sleep)) {
			double clk_scale = (iostat_delay > 0) ? 1.0 / iostat_delay : 0.0;
			static uint32_t iostat_count = 0;

			stress_get_iostat(iostat_name, &iostat);

			pr_block_begin();
			if (iostat_count == 0)
				pr_inf("iostat: Inflght   Rd K/s   Wr K/s     Rd/s     Wr/s\n");

			/* sectors are 512 bytes, so >> 1 to get stats in 1024 bytes */
			pr_inf("iostat: %7.0f %8.0f %8.0f %8.0f %8.0f\n",
				(double)iostat.in_flight * clk_scale,
				(double)(iostat.read_sectors >> 1) * clk_scale,
				(double)(iostat.write_sectors >> 1) * clk_scale,
				(double)iostat.read_io * clk_scale,
				(double)iostat.write_io * clk_scale);
			pr_block_end();

			iostat_count++;
			if (iostat_count >= 25)
				iostat_count = 0;
		}
#endif
		if (status_sleep == status_delay) {
			const double runtime = round(stress_time_now() - g_shared->time_started);

			pr_inf("status: %" PRIu32 " run, %" PRIu32 " exit, %" PRIu32 " reap, %" PRIu32 " fail, %" PRIu32 " sigalarm, %s\n",
				g_shared->instance_count.started,
				g_shared->instance_count.exited,
				g_shared->instance_count.reaped,
				g_shared->instance_count.failed,
				g_shared->instance_count.alarmed,
				stress_duration_to_str(runtime, false, true));
		}
#if defined(STRESS_RAPL)
		if ((sleep_delay > 0) &&
		    (raplstat_delay > 0) &&
		    (raplstat_sleep == raplstat_delay) &&
		    (g_opt_flags & OPT_FLAGS_RAPL_REQUIRED)) {
			int ret;

			ret = stress_rapl_get_power_raplstat(g_shared->rapl_domains);
			if (ret == 0) {
				char buf[256], *ptr;
				stress_rapl_domain_t *rapl;
				size_t len;
				static uint32_t raplstat_count = 0;

				if (raplstat_count == 0) {
					ptr = buf;
					len = sizeof(buf);
					for (rapl = g_shared->rapl_domains; rapl; rapl = rapl->next) {
						ret = snprintf(ptr, len, " %7.7s", rapl->domain_name);
						if (ret > 0) {
							ptr += ret;
							len -= ret;
						}
					}
					pr_inf("raplstat: %s\n", buf);
				}

				ptr = buf;
				len = sizeof(buf);
				for (rapl = g_shared->rapl_domains; rapl; rapl = rapl->next) {
					ret = snprintf(ptr, len, " %7.2f", rapl->data[STRESS_RAPL_DATA_RAPLSTAT].power_watts);
					if (ret > 0) {
						ptr += ret;
						len -= ret;
					}
				}
				pr_inf("raplstat: %s\n", buf);
				raplstat_count++;
				if (raplstat_count >= 25)
					raplstat_count = 0;
			}
		}
#endif
	}
	_exit(0);
}

/*
 *  stress_vmstat_stop()
 *	stop vmstat statistics
 */
void stress_vmstat_stop(void)
{
	if (vmstat_pid > 0)
		(void)stress_kill_pid_wait(vmstat_pid, NULL);
}
