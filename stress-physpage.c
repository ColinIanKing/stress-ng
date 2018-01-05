/*
 * Copyright (C) 2013-2018 Canonical, Ltd.
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
#include "stress-ng.h"

/*
 *  stress_physpage_supported()
 *      check if we can run this as root
 */
int stress_physpage_supported(void)
{
        if (geteuid() != 0) {
		pr_inf("stress-physpage stressor needs to be run as root to access page information\n");
                return -1;
        }
        return 0;
}

#if defined(__linux__)

#define PAGE_PRESENT	(1ULL << 63)
#define PFN_MASK	((1ULL << 54) - 1)

static int stress_virt_to_phys(
	const args_t *args,
	const size_t page_size,
	const int fd_pm,
	const int fd_pc,
	const uintptr_t virt_addr)
{
	off_t offset;
	uint64_t pageinfo;

	offset = (virt_addr / page_size) * sizeof(uint64_t);
	if (lseek(fd_pm, offset, SEEK_SET) != offset) {
		pr_err("%s: cannot seek on address %p in /proc/self/pagemap, errno=%d (%s)\n",
			args->name, (void *)virt_addr, errno, strerror(errno));
		goto err;
	}
	if (read(fd_pm, &pageinfo, sizeof(pageinfo)) != sizeof(pageinfo)) {
		pr_err("%s: cannot read address %p in /proc/self/pagemap, errno=%d (%s)\n",
			args->name, (void *)virt_addr, errno, strerror(errno));
		goto err;
	}

	if (pageinfo & PAGE_PRESENT) {
		uint64_t page_count;
		const uint64_t pfn = pageinfo & PFN_MASK;
		uintptr_t phys_addr = pfn * page_size;

		phys_addr |= (virt_addr & (page_size - 1));
		offset = pfn * sizeof(uint64_t);

		if (phys_addr == 0) {
			pr_err("%s: got zero physical address from virtual address %p\n",
				args->name, (void *)virt_addr);
			goto err;
		}

		if (fd_pc < 0)
			return 0;

		if (lseek(fd_pc, offset, SEEK_SET) != offset) {
			pr_err("%s: cannot seek on address %p in /proc/kpagecount, errno=%d (%s)\n",
				args->name, (void *)virt_addr, errno, strerror(errno));
			goto err;
		}
		if (read(fd_pc, &page_count, sizeof(page_count)) != sizeof(page_count)) {
			pr_err("%s: cannot read page count for address %p in /proc/kpagecount, errno=%d (%s)\n",
				args->name, (void *)virt_addr, errno, strerror(errno));
			goto err;
		}
		if (page_count < 1) {
			pr_err("%s: got zero page count for physical address %p\n",
				args->name, (void *)phys_addr);
			goto err;
		}
		return 0;
	} else {
		/*
		 * Page is not present, it may have been swapped
		 * out, so this is not an error, just highly unlikely
		 */
		return 0;
	}
err:
	return -1;
}

/*
 *  stress_physpage()
 *	stress physical page lookups
 */
int stress_physpage(const args_t *args)
{
	int fd_pm, fd_pc;
	const size_t page_size = args->page_size;
	uint8_t *ptr = NULL;

	fd_pm = open("/proc/self/pagemap", O_RDONLY);
	if (fd_pm < 0) {
		pr_err("%s: cannot open /proc/self/pagemap, errno=%d (%s)\n",
			args->name, errno, strerror(errno));
		return EXIT_FAILURE;
	}

	/*
	 *  this interface may not exist, don't make it a failure
	 */
	fd_pc = open("/proc/kpagecount", O_RDONLY);
	if (fd_pc < 0) {
		if (args->instance == 0)
			pr_dbg("%s: cannot open /proc/kpagecount, errno=%d (%s)\n",
				args->name, errno, strerror(errno));
		fd_pc = -1;
	}

	do {
		ptr = mmap(ptr + page_size, page_size, PROT_READ | PROT_WRITE,
			MAP_POPULATE | MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
		if (ptr != MAP_FAILED) {
			(void)stress_virt_to_phys(args, page_size, fd_pm, fd_pc, (uintptr_t)ptr);
			(void)munmap(ptr, page_size);
			(void)stress_virt_to_phys(args, page_size, fd_pm, fd_pc, (uintptr_t)g_shared->stats);

		}
		inc_counter(args);
	} while (keep_stressing());

	if (fd_pc > 0)
		(void)close(fd_pc);
	(void)close(fd_pm);

	return EXIT_SUCCESS;
}
#else
int stress_physpage(const args_t *args)
{
	return stress_not_implemented(args);
}
#endif
