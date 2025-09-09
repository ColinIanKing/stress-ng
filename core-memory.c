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
#include "core-builtin.h"

#if defined(HAVE_MACH_MACH_H)
#include <mach/mach.h>
#endif

#if defined(HAVE_MACH_VM_STATISTICS_H)
#include <mach/vm_statistics.h>
#endif

#if defined(__FreeBSD__) &&	\
    defined(HAVE_SYS_PARAM_H)
#include <sys/param.h>
#endif

#if defined(HAVE_SYS_PRCTL_H)
#include <sys/prctl.h>
#endif

#if defined(HAVE_SYS_SWAP_H) &&	\
    !defined(__sun__)
#include <sys/swap.h>
#endif

#if defined(HAVE_UVM_UVM_EXTERN_H)
#include <uvm/uvm_extern.h>
#endif

#define PAGE_4K_SHIFT			(12)
#define PAGE_4K				(1 << PAGE_4K_SHIFT)

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
 *  stress_get_meminfo()
 *	wrapper for linux sysinfo
 */
int stress_get_meminfo(
	size_t *freemem,
	size_t *totalmem,
	size_t *freeswap,
	size_t *totalswap)
{
	if (UNLIKELY(!freemem || !totalmem || !freeswap || !totalswap))
		return -1;
#if defined(HAVE_SYS_SYSINFO_H) &&	\
    defined(HAVE_SYSINFO) &&		\
    !defined(__fiwix)
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
 *  stress_get_memfree_str()
 *	get size of memory that's free in a string, non-reentrant
 *	note the ' ' space is prefixed before valid strings so the
 *	output can be used in messages such as:
 *	   pr_fail("out of memory%s\n", stress_uint64_to_str());
 */
char *stress_get_memfree_str(void)
{
        size_t freemem = 0, totalmem = 0, freeswap = 0, totalswap = 0;
	char freemem_str[32], freeswap_str[32];
	static char buf[96];

	(void)shim_memset(buf, 0, sizeof(buf));
	if (stress_get_meminfo(&freemem, &totalmem, &freeswap, &totalswap) < 0)
		return buf;

	if ((freemem == 0) && (totalmem == 0) && (freeswap == 0) && (totalswap == 0))
		return buf;

	(void)stress_uint64_to_str(freemem_str, sizeof(freemem_str), (uint64_t)freemem, 0, true);
	(void)stress_uint64_to_str(freeswap_str, sizeof(freeswap_str), (uint64_t)freeswap, 0, true);
	(void)snprintf(buf, sizeof(buf), " (%s mem free, %s swap free)", freemem_str, freeswap_str);
	return buf;
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
			if (delta > (ssize_t)freeswap / 8) {
				low_memory = true;
				goto update;
			}
		}
		/* Not enough for allocation and slop? */
		if (freemem < ((4 * MB) + requested)) {
			low_memory = true;
			goto update;
		}
		/* Less than threshold left? */
		if (((double)(freemem - requested) * 100.0 / (double)totalmem) < threshold) {
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
 *  stress_usage_bytes()
 *	report how much memory is used per instance
 *	and in total compared to physical memory available
 */
void stress_usage_bytes(
	stress_args_t *args,
	const size_t vm_per_instance,
	const size_t vm_total)
{
	const uint64_t total_phys_mem = stress_get_phys_mem_size();
	char s1[32], s2[32], s3[32];

	pr_inf("%s: using %s per stressor instance (total %s of %s available memory)\n",
		args->name,
		stress_uint64_to_str(s1, sizeof(s1), (uint64_t)vm_per_instance, 2, true),
		stress_uint64_to_str(s2, sizeof(s2), (uint64_t)vm_total, 2, true),
		stress_uint64_to_str(s3, sizeof(s3), total_phys_mem, 2, true));
}

/*
 *  stress_align_address
 *	align address to alignment, alignment MUST be a power of 2
 */
void CONST *stress_align_address(const void *addr, const size_t alignment)
{
	const uintptr_t uintptr =
		((uintptr_t)addr + alignment) & ~(alignment - 1);

	return (void *)uintptr;
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

#if defined(MAP_HUGETLB) &&	\
    (defined(MAP_HUGE_2MB) ||	\
     defined(MAP_HUGE_1GB))
/*
 *  stress_mapping_hugetlb_size()
 *	check if a mapping starting at address addr is a 2MB or 1GB
 *	HUGETLB mapped region
 */
static size_t stress_mapping_hugetlb_size(void *addr)
{
#if defined(__linux__)
	FILE *fp;
	const pid_t pid = getpid();
	char path[PATH_MAX];
	char buf[4096];
	size_t hugetlb_size = 0;
	bool addr_match = false;
	uintptr_t addr_begin = 0, addr_end = 0;

	(void)snprintf(path, sizeof(path), "/proc/%" PRIdMAX "/smaps", (intmax_t)pid);
	fp = fopen(path, "r");
	if (!fp) {
		/* can't open, can't assume it's huge */
		return 0;
	}
	while (fgets(buf, sizeof(buf), fp) != NULL) {
		if (addr_match) {
			/* VmFlags has ht if is HUGETLB mapped region */
			if (!strncmp(buf, "VmFlags:", 7) &&
			    strstr(buf + 8, " ht")) {
				hugetlb_size = (size_t)(addr_end - addr_begin);
				break;
			}
		} else {
			/* Scan for a matching start address */
			if ((sscanf(buf, "%" SCNxPTR "-%" SCNxPTR "%*s", &addr_begin, &addr_end) != 2) &&
			    (addr_begin == (uintptr_t)addr) &&
			    (addr_begin < addr_end))
				addr_match = true;
		}
	}

	(void)fclose(fp);

	return hugetlb_size;
#else
	(void)addr;

	return 0;
#endif
}
#endif

/*
 *  stress_munmap_force()
 *	force retry munmap on ENOMEM or EINVAL errors as these can be due
 *	to low memory not allowing memory to be released
 *
 *	this is not a munmap shim, it's a forceful munmap to cope
 *	with unexpected low memory or hugetlb umapping errors
 */
int stress_munmap_force(void *addr, size_t length)
{
	int ret, i;
#if defined(MAP_HUGETLB) && defined(MAP_HUGE_2MB)
	const uintptr_t size2MB = (1ULL << 21);
#endif
#if defined(MAP_HUGETLB) && defined(MAP_HUGE_1GB)
	const uintptr_t size1GB = (1ULL << 30);
#endif

	for (i = 1; i <= 10; i++) {
		int saved_errno;

		ret = munmap(addr, length);
		if (LIKELY(ret == 0))
			break;
		saved_errno = errno;
		/*
		 *  EINVAL on munmap can occur if a huge page has
		 *  been mapped with a size less than the huge page.
		 *  In these cases, check the alignment and if
		 *  huge page aligned then redo the munmap with the
		 *  huge page sizes to unmap them.
		 */
#if defined(MAP_HUGETLB)
		if (saved_errno == EINVAL) {
#if defined(MAP_HUGE_1GB) ||	\
    defined(MAP_HUGE_2MB)
			size_t hugetlb_size = stress_mapping_hugetlb_size(addr);
#endif

#if (MAP_HUGE_1GB)
			if ((length < size1GB) &&
			    (((uintptr_t)addr & (size1GB - 1)) == 0) &&
			    ((hugetlb_size & (size1GB - 1)) == 0)) {
				ret = munmap(addr, hugetlb_size);
				if (ret == 0)
					break;
				saved_errno = errno;
			}
#endif
#if defined(MAP_HUGE_2MB)
			if ((length < size2MB) &&
			    (((uintptr_t)addr & (size2MB - 1)) == 0) &&
			    ((hugetlb_size & (size2MB - 1)) == 0)) {
				ret = munmap(addr, hugetlb_size);
				if (ret == 0)
					break;
				saved_errno = errno;
			}
#endif
		}
#endif
		if (saved_errno != ENOMEM)
			break;
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
 *  stress_get_pid_memory_usage()
 *	get total, resident and shared memory (in bytes)
 *	used by process with PID pid
 */
int stress_get_pid_memory_usage(
	const pid_t pid,
	size_t *total,
	size_t *resident,
	size_t *shared)
{
#if defined(__linux__)
	FILE *fp;
	char path[PATH_MAX];
	size_t page_size;

	(void)snprintf(path, sizeof(path), "/proc/%" PRIdMAX "/statm", (intmax_t)pid);
	fp = fopen(path, "r");
	if (!fp) {
		*total = 0;
		*resident = 0;
		*shared = 0;
		return -1;
	}
	if (fscanf(fp, "%zu %zu %zu", total, resident, shared) != 3) {
		(void)fclose(fp);
		return -1;
	}
	(void)fclose(fp);

	page_size = stress_get_page_size();
	*total *= page_size;
	*resident *= page_size;
	*shared *= page_size;

	return 0;
#else
	(void)pid;

	*total = 0;
	*resident = 0;
	*shared = 0;
	return -1;
#endif
}
