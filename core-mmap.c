/*
 * Copyright (C) 2017-2021 Canonical, Ltd.
 * Copyright (C) 2022-2026 Colin Ian King.
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
#include "core-cpu-cache.h"
#include "core-mmap.h"
#include "core-put.h"

#if defined(HAVE_SYS_SHM_H)
#include <sys/shm.h>
#endif

#if defined(HAVE_ASM_X86_REP_STOSQ) &&  \
    !defined(__ILP32__)
#define USE_ASM_X86_REP_STOSQ
#endif

/*
 *  stress_mmap_set()
 *	set mmap'd data, touching pages in
 *	a specific pattern - check with
 *	stress_mmap_check().
 */
void OPTIMIZE3 stress_mmap_set(
	uint8_t *buf,
	const size_t sz,
	const size_t page_size)
{
	register uint64_t val = stress_mwc64();
	register uint64_t *ptr = (uint64_t *)shim_assume_aligned(buf, 8);
	register const uint64_t *end = (uint64_t *)shim_assume_aligned((buf + sz), 8);
#if defined(USE_ASM_X86_REP_STOSQ)
        register const uint32_t loops = (uint32_t)(page_size / sizeof(uint64_t));
#endif

	while (ptr < end) {
#if !defined(USE_ASM_X86_REP_STOSQ)
		register const uint64_t *page_end = (uint64_t *)((uintptr_t)ptr + page_size);
#endif

		if (UNLIKELY(!stress_continue_flag()))
			break;

		/*
		 *  ..and fill a page with uint64_t values
		 */
#if defined(USE_ASM_X86_REP_STOSQ)
        __asm__ __volatile__(
                "mov %0,%%rax\n;"
                "mov %1,%%rdi\n;"
                "mov %2,%%ecx\n;"
                "rep stosq %%rax,%%es:(%%rdi);\n"
                :
                : "r" (val),
		  "r" (ptr),
                  "r" (loops)
                : "ecx","rdi","rax");
		ptr += loops;
#else
		page_end = (const uint64_t *)STRESS_MINIMUM(end, page_end);

		while (ptr < page_end) {
			ptr[0x00] = val;
			ptr[0x01] = val;
			ptr[0x02] = val;
			ptr[0x03] = val;
			ptr[0x04] = val;
			ptr[0x05] = val;
			ptr[0x06] = val;
			ptr[0x07] = val;
			ptr[0x08] = val;
			ptr[0x09] = val;
			ptr[0x0a] = val;
			ptr[0x0b] = val;
			ptr[0x0c] = val;
			ptr[0x0d] = val;
			ptr[0x0e] = val;
			ptr[0x0f] = val;
			ptr += 16;
		}
#endif
		val++;
	}
}

/*
 *  stress_mmap_check()
 *	check if mmap'd data is sane
 */
int OPTIMIZE3 stress_mmap_check(
	uint8_t *buf,
	const size_t sz,
	const size_t page_size)
{
	register uint64_t *ptr = (uint64_t *)shim_assume_aligned(buf, 8);
	register const uint64_t *end = (uint64_t *)shim_assume_aligned((buf + sz), 8);

	while (LIKELY((ptr < end) && stress_continue_flag())) {
		register const uint64_t *page_end = (uint64_t *)((uintptr_t)ptr + page_size);

		while (ptr < page_end) {
			register uint64_t sum;

			sum  = ptr[0x00];
			sum ^= ptr[0x01];
			sum ^= ptr[0x02];
			sum ^= ptr[0x03];
			sum ^= ptr[0x04];
			sum ^= ptr[0x05];
			sum ^= ptr[0x06];
			sum ^= ptr[0x07];
			sum ^= ptr[0x08];
			sum ^= ptr[0x09];
			sum ^= ptr[0x0a];
			sum ^= ptr[0x0b];
			sum ^= ptr[0x0c];
			sum ^= ptr[0x0d];
			sum ^= ptr[0x0e];
			sum ^= ptr[0x0f];
			sum ^= ptr[0x10];
			sum ^= ptr[0x11];
			sum ^= ptr[0x12];
			sum ^= ptr[0x13];
			sum ^= ptr[0x14];
			sum ^= ptr[0x15];
			sum ^= ptr[0x16];
			sum ^= ptr[0x17];
			sum ^= ptr[0x18];
			sum ^= ptr[0x19];
			sum ^= ptr[0x1a];
			sum ^= ptr[0x1b];
			sum ^= ptr[0x1c];
			sum ^= ptr[0x1d];
			sum ^= ptr[0x1e];
			sum ^= ptr[0x1f];
			ptr += 32;
			if (UNLIKELY(sum))
				return -1;
		}
	}
	return 0;
}

/*
 *  stress_mmap_set_light()
 *	set mmap'd data, touching pages in
 *	a specific pattern at start of each page - check with
 *	stress_mmap_check_light().
 */
void OPTIMIZE3 stress_mmap_set_light(
	uint8_t *buf,
	const size_t sz,
	const size_t page_size)
{
	register uint64_t *ptr = (uint64_t *)shim_assume_aligned(buf, 8);
	register uint64_t val = stress_mwc64();
	register const uint64_t *end = (uint64_t *)shim_assume_aligned((buf + sz), 8);
	register const size_t ptr_inc = page_size / sizeof(*ptr);

	while (LIKELY(ptr < end)) {
		*ptr = val;
		ptr += ptr_inc;
		val++;
	}
}

/*
 *  stress_mmap_check_light()
 *	check if mmap'd data is sane
 */
int OPTIMIZE3 stress_mmap_check_light(
	uint8_t *buf,
	const size_t sz,
	const size_t page_size)
{
	register uint64_t *ptr = (uint64_t *)shim_assume_aligned(buf, 8);
	register uint64_t val = *ptr;
	register const uint64_t *end = (uint64_t *)shim_assume_aligned((buf + sz), 8);
	register const size_t ptr_inc = page_size / sizeof(*ptr);

	while (LIKELY(ptr < end)) {
		if (UNLIKELY(*ptr != val))
			return -1;
		ptr += ptr_inc;
		val++;
	}
	return 0;
}

/*
 *  stress_mmap_populate()
 *	try mmap with MAP_POPULATE option, if it fails
 *	retry without MAP_POPULATE. This prefaults pages
 *	into memory to avoid faulting during stressor
 *	execution. Useful for mappings that get accessed
 *	immediately after being mmap'd.
 *
 *	Don't populate if MAP_POPULATE is not available
 *	and if fd is used to avoid potential SIGBUS faults
 *	if the file can't back the pages.
 */
void *stress_mmap_populate(
	void *addr,
	size_t length,
	int prot,
	int flags,
	int fd,
	off_t offset)
{
	void *ret;

#if defined(MAP_POPULATE)
	flags |= MAP_POPULATE;
	ret = mmap(addr, length, prot, flags, fd, offset);
	if (ret != MAP_FAILED)
		return ret;
	/* remove flag, try again */
	flags &= ~MAP_POPULATE;
#endif
	ret = mmap(addr, length, prot, flags, fd, offset);
	if (ret == MAP_FAILED)
		return ret;
	if (fd < 0)
		stress_mmap_populate_forward(ret, length, prot);
	return ret;
}

/*
 *  stress_mmap_anon_shared()
 *	simplified anonymous shared mmap, use SYSV shm for
 *	systems that don't support this feature
 */
void *stress_mmap_anon_shared(size_t length, int prot)
{
#if defined(HAVE_SYS_SHM_H) &&	\
    defined(__fiwix__)
	int shmid;
	void *addr;
	int shm_flag = IPC_CREAT | IPC_EXCL | S_IRUSR | S_IWUSR;
	const int prot_flag = prot & (PROT_READ | PROT_WRITE | PROT_EXEC);
	int saved_errno;

	shmid = shmget(IPC_PRIVATE, length, shm_flag);
	if (shmid < 0)
		return NULL;
        addr = shmat(shmid, NULL, 0);
        if (shmctl(shmid, IPC_RMID, NULL) < 0) {
		if (addr != (void *)-1)
			(void)shmdt(addr);
		return NULL;
	}
	if (addr == (void *)-1)
		return MAP_FAILED;

	saved_errno = errno;
	(void)mprotect(addr, length, prot_flag);
	errno = saved_errno;
	return addr;
#else
	const int prot_flag = prot & (PROT_READ | PROT_WRITE | PROT_EXEC);

	return mmap(NULL, length, prot_flag, MAP_ANONYMOUS | MAP_SHARED, -1, 0);
#endif
}

/*
 *  stress_munmap_anon_shared()
 *	unmap for stress_mmap_anon_shared, use SYSV shm for
 *	systems that don't support this feature
 */
int stress_munmap_anon_shared(void *addr, size_t length)
{
#if defined(HAVE_SYS_SHM_H) &&	\
    defined(__fiwix__)
	(void)length;

	return shmdt(addr);
#else
	return munmap(addr, length);
#endif
}

#define STRESS_PAGE_SOFT_DIRTY	(STRESS_BIT_UL(55))
#define STRESS_PAGE_EXCLUSIVE	(STRESS_BIT_UL(56))
#define STRESS_PAGE_SWAPPED	(STRESS_BIT_UL(62))
#define STRESS_PAGE_PRESENT	(STRESS_BIT_UL(63))
#define STRESS_PAGE_PFN_MASK	(STRESS_BIT_UL(55) - 1)

/*
 *  stress_mmap_pread()
 *	perform pread if available, otherwise seek + read
 */
static inline ssize_t stress_mmap_pread(int fd, void *buf, size_t count, off_t offset)
{
#if defined(HAVE_PREADV) && 0
	return pread(fd, buf, count, offset);
#else
	if (lseek(fd, offset, SEEK_SET) == (off_t)-1)
		return (ssize_t)-1;
	return read(fd, buf, count);
#endif
}

#define PHYS_ADDR_UNKNOWN	(~(uintptr_t)0)

/*
 *  stress_mmap_stats()
 *	attempt to read physical page statistics on all pages in a mapping
 */
int stress_mmap_stats(void *addr, const size_t length, stress_mmap_stats_t *stats)
{
#if defined(__linux__)
	int fd;
	const size_t page_size = stress_get_page_size();
	uintptr_t virt_addr, phys_addr, prev_phys_addr = PHYS_ADDR_UNKNOWN;
	const uintptr_t virt_begin = (uintptr_t)addr;
	const uintptr_t virt_end = virt_begin + length;
	off_t offset = (off_t)(sizeof(uint64_t) * (virt_begin / page_size));

	if (!stats)
		return -1;

	(void)memset(stats, 0, sizeof(*stats));
	stats->pages_mapped = length / page_size;

	fd = open("/proc/self/pagemap", O_RDONLY);
	if (fd < 0)
		return -1;

	for (virt_addr = virt_begin; virt_addr < virt_end; virt_addr += page_size, offset += sizeof(uint64_t)) {
		uint64_t info;

		if (stress_mmap_pread(fd, &info, sizeof(info), offset) != sizeof(info)) {
			stats->pages_unknown++;
		} else {
			if (info & STRESS_PAGE_SOFT_DIRTY)
				stats->pages_dirtied++;
			if (info & STRESS_PAGE_EXCLUSIVE)
				stats->pages_exclusive++;
			if (info & STRESS_PAGE_SWAPPED)
				stats->pages_swapped++;
			if (info & STRESS_PAGE_PRESENT) {
				phys_addr = (info & STRESS_PAGE_PFN_MASK) * page_size;
				if (phys_addr == 0)
					stats->pages_null++;
				else if ((prev_phys_addr == PHYS_ADDR_UNKNOWN) && (phys_addr != 0)) {
					stats->pages_contiguous++;
					stats->pages_present++;
				} else if (phys_addr == prev_phys_addr + page_size) {
					stats->pages_contiguous++;
					stats->pages_present++;
				} else {
					stats->pages_present++;
				}
				prev_phys_addr = phys_addr;
			}
		}
	}
	(void)close(fd);

	return 0;
#else
	(void)addr;
	(void)length;

	if (stats)
		(void)memset(stats, 0, sizeof(*stats));

	return -1;
#endif
}

/*
 *  stress_mmap_stats_sum()
 *	sum stats into stats_total for running totals
 */
void stress_mmap_stats_sum(
	stress_mmap_stats_t *stats_total,
	const stress_mmap_stats_t *stats)
{
	stats_total->pages_mapped += stats->pages_mapped;
	stats_total->pages_present += stats->pages_present;
	stats_total->pages_swapped += stats->pages_swapped;
	stats_total->pages_dirtied += stats->pages_dirtied;
	stats_total->pages_exclusive += stats->pages_exclusive;
	stats_total->pages_unknown += stats->pages_unknown;
	stats_total->pages_null += stats->pages_null;
	stats_total->pages_contiguous += stats->pages_contiguous;
}

/*
 *  stress_mmap_stats_report()
 *	report mmap region stats
 */
void stress_mmap_stats_report(
	stress_args_t *args,
	const stress_mmap_stats_t *stats,
	int *metric_index,
	int flags)
{
	if (stats->pages_mapped > 0) {
		double pc;

		if (flags & STRESS_MMAP_REPORT_FLAGS_TOTAL) {
			stress_metrics_set(args, *metric_index, "pages mmapped", (double)stats->pages_mapped, STRESS_METRIC_GEOMETRIC_MEAN);
			(*metric_index)++;
		}

		if (flags & STRESS_MMAP_REPORT_FLAGS_PRESENT) {
			pc = 100.0 * (double)stats->pages_present / (double)stats->pages_mapped;
			stress_metrics_set(args, *metric_index, "% pages present", pc, STRESS_METRIC_GEOMETRIC_MEAN);
			(*metric_index)++;
		}

		if (flags & STRESS_MMAP_REPORT_FLAGS_SWAPPED) {
			pc = 100.0 * (double)stats->pages_swapped / (double)stats->pages_mapped;
			stress_metrics_set(args, *metric_index, "% pages swapped", pc, STRESS_METRIC_GEOMETRIC_MEAN);
			(*metric_index)++;
		}

		if (flags & STRESS_MMAP_REPORT_FLAGS_DIRTIED) {
			pc = 100.0 * (double)stats->pages_dirtied / (double)stats->pages_mapped;
			stress_metrics_set(args, *metric_index, "% pages dirtied", pc, STRESS_METRIC_GEOMETRIC_MEAN);
			(*metric_index)++;
		}

		if (flags & STRESS_MMAP_REPORT_FLAGS_EXCLUSIVE) {
			pc = 100.0 * (double)stats->pages_exclusive / (double)stats->pages_mapped;
			stress_metrics_set(args, *metric_index, "% pages exclusive", pc, STRESS_METRIC_GEOMETRIC_MEAN);
			(*metric_index)++;
		}

		if (flags & STRESS_MMAP_REPORT_FLAGS_UKNOWN) {
			pc = 100.0 * (double)stats->pages_unknown / (double)stats->pages_mapped;
			stress_metrics_set(args, *metric_index, "% pages unknown", pc, STRESS_METRIC_GEOMETRIC_MEAN);
			(*metric_index)++;
		}

		if (flags & STRESS_MMAP_REPORT_FLAGS_NULL) {
			pc = 100.0 * (double)stats->pages_null / (double)stats->pages_mapped;
			stress_metrics_set(args, *metric_index, "% pages null", pc, STRESS_METRIC_GEOMETRIC_MEAN);
			(*metric_index)++;
		}

		if ((flags & STRESS_MMAP_REPORT_FLAGS_CONTIGUOUS) &&
		    (stats->pages_null == 0) && (stats->pages_present > 0)) {
			pc = 100.0 * (double)stats->pages_contiguous / (double)stats->pages_mapped;
			stress_metrics_set(args, *metric_index, "% pages physically contiguous", pc, STRESS_METRIC_GEOMETRIC_MEAN);
			(*metric_index)++;
		}
	}
}

/*
 *  stress_mmap_populate_forward()
 *	populate pages in forward direction in a mmap'd region,
 *	if PROT_WRITE then read/write data,
 *	if PROT_READ, read data
 *	if no read/write protection, don't do anything
 */
void OPTIMIZE3 stress_mmap_populate_forward(
	void *addr,
	const size_t len,
	const int prot)
{
	register const size_t page_size = stress_get_page_size();
	register volatile uint8_t *ptr = (uint8_t *)addr;
	register const uint8_t *ptr_end = (uint8_t *)addr + len;

	if ((prot & (PROT_READ | PROT_WRITE)) == (PROT_READ | PROT_WRITE))  {
		while (LIKELY((ptr < ptr_end) && stress_continue_flag())) {
			register uint8_t val;

			val = *ptr;
			stress_asm_mb();
			*ptr = val + 1;
			stress_asm_mb();
			*ptr = val;
			ptr += page_size;
		}
	} else if (prot & PROT_READ) {
		while (LIKELY((ptr < ptr_end) && stress_continue_flag())) {
			(void)*ptr;
			stress_asm_mb();
			ptr += page_size;
		}
	}
}

/*
 *  stress_mmap_populate_reverse()
 *	populate pages in reverse direction in a mmap'd region,
 *	if PROT_WRITE then read/write data,
 *	if PROT_READ, read data
 *	if no read/write protection, don't do anything
 */
void OPTIMIZE3 stress_mmap_populate_reverse(
	void *addr,
	const size_t len,
	const int prot)
{
	register const size_t page_size = stress_get_page_size();
	register volatile uint8_t *ptr = (uint8_t *)(((uintptr_t)addr + len - 1) & ~(page_size - 1));
	register const uint8_t *ptr_start = (uint8_t *)addr;

	if ((prot & (PROT_READ | PROT_WRITE)) == (PROT_READ | PROT_WRITE))  {
		while ((ptr >= ptr_start) && stress_continue_flag()) {
			register uint8_t val;

			val = *ptr;
			stress_asm_mb();
			*ptr = val + 1;
			stress_asm_mb();
			*ptr = val;
			ptr -= page_size;
		}
	} else if (prot & PROT_READ) {
		while ((ptr >= ptr_start) && stress_continue_flag()) {

			(void)*ptr;
			stress_asm_mb();
			ptr -= page_size;
		}
	}
}
