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
#include "stress-ng.h"

static const stress_help_t help[] = {
	{ NULL,	"physpage N",	  "start N workers performing physical page lookup" },
	{ NULL,	"physpage-ops N", "stop after N physical page bogo operations" },
	{ NULL,	NULL,		  NULL }
};

#if defined(__linux__)

#define PAGE_PRESENT	(1ULL << 63)
#define PFN_MASK	((1ULL << 54) - 1)

/*
 *  stress_physpage_supported()
 *      check if we can run this with SHIM_CAP_SYS_ADMIN capability
 */
static int stress_physpage_supported(const char *name)
{
	if (!stress_check_capability(SHIM_CAP_SYS_ADMIN)) {
		pr_inf("%s stressor will be skipped, "
			"need to be running with CAP_SYS_ADMIN "
			"rights for this stressor\n", name);
		return -1;
	}
	return 0;
}

static int stress_virt_to_phys(
	const stress_args_t *args,
	const size_t page_size,
	const int fd_pm,
	const int fd_pc,
	const int fd_mem,
	const uintptr_t virt_addr)
{
	off_t offset;
	uint64_t pageinfo;
	ssize_t n;

	offset = (virt_addr / page_size) * sizeof(uint64_t);
	if (lseek(fd_pm, offset, SEEK_SET) != offset) {
		pr_err("%s: cannot seek on address %p in /proc/self/pagemap, errno=%d (%s)\n",
			args->name, (void *)virt_addr, errno, strerror(errno));
		goto err;
	}
	n = read(fd_pm, &pageinfo, sizeof(pageinfo));
	if (n < 0) {
		pr_err("%s: cannot read address %p in /proc/self/pagemap, errno=%d (%s)\n",
			args->name, (void *)virt_addr, errno, strerror(errno));
		goto err;
	} else if (n != (ssize_t)sizeof(pageinfo)) {
		pr_err("%s: read address %p in /proc/self/pagemap returned %zd bytes, expected %zd\n",
			args->name, (void *)virt_addr, (size_t)n, sizeof(pageinfo));
		goto err;
	}

	if (pageinfo & PAGE_PRESENT) {
		uint64_t page_count;
		const uint64_t pfn = pageinfo & PFN_MASK;
		uintptr_t phys_addr = pfn * page_size;

		phys_addr |= (virt_addr & (page_size - 1));
		offset = pfn * sizeof(uint64_t);

		if (phys_addr == 0)
			return 0;
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

		/*
		 *  Try to exercise /dev/mem, seek may work, read most probably
		 *  will fail with -EPERM. Ignore any errors, the aim here is
		 *  just to try and exercise this interface.
		 */
		if (fd_mem >= 0) {
			off_t offret;

			offret = lseek(fd_mem, (off_t)phys_addr, SEEK_SET);
			if (offret != (off_t)-1) {
				ssize_t rdret;
				char data[16];

				rdret = read(fd_mem, data, sizeof(data));
				(void)rdret;
			}
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
static int stress_physpage(const stress_args_t *args)
{
	int fd_pm, fd_pc, fd_mem;
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

	/*
	 *  this may fail, silently ignore failures
	 */
	fd_mem = open("/dev/mem", O_RDONLY);

	stress_set_proc_state(args->name, STRESS_STATE_RUN);

	do {
		void *nptr;

		nptr = mmap(ptr, page_size, PROT_READ | PROT_WRITE,
			MAP_POPULATE | MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
		if (nptr != MAP_FAILED) {
			(void)stress_virt_to_phys(args, page_size, fd_pm, fd_pc, fd_mem, (uintptr_t)nptr);
			(void)munmap(nptr, page_size);
			(void)stress_virt_to_phys(args, page_size, fd_pm, fd_pc, fd_mem, (uintptr_t)g_shared->stats);

		}
		ptr += page_size;
		inc_counter(args);
	} while (keep_stressing(args));

	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);

	if (fd_mem > 0)
		(void)close(fd_mem);
	if (fd_pc > 0)
		(void)close(fd_pc);
	(void)close(fd_pm);

	return EXIT_SUCCESS;
}

stressor_info_t stress_physpage_info = {
	.stressor = stress_physpage,
	.supported = stress_physpage_supported,
	.class = CLASS_VM,
	.help = help
};
#else
stressor_info_t stress_physpage_info = {
	.stressor = stress_not_implemented,
	.class = CLASS_VM,
	.help = help
};
#endif
