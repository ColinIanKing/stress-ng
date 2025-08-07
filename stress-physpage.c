/*
 * Copyright (C) 2013-2021 Canonical, Ltd.
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
#include "core-capabilities.h"
#include "core-mmap.h"

#include <sys/ioctl.h>

#if defined(HAVE_ASM_MTRR_H)
#include <asm/mtrr.h>
#endif

static const stress_help_t help[] = {
	{ NULL,	"physpage N",	  "start N workers performing physical page lookup" },
	{ NULL,	"physpage-ops N", "stop after N physical page bogo operations" },
	{ NULL,	"physpage-mtrr",  "enable modification of MTRR types on physical page" },
	{ NULL,	NULL,		  NULL }
};

static const stress_opt_t opts[] = {
	{ OPT_physpage_mtrr, "physpage-mtrr", TYPE_ID_BOOL, 0, 1, NULL },
	END_OPT,
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
		pr_inf_skip("%s stressor will be skipped, "
			"need to be running with CAP_SYS_ADMIN "
			"rights for this stressor\n", name);
		return -1;
	}
	return 0;
}

#if defined(STRESS_ARCH_X86) &&		\
    defined(HAVE_ASM_MTRR_H) &&		\
    defined(HAVE_MTRR_GENTRY) &&	\
    defined(HAVE_MTRR_SENTRY) &&	\
    defined(MTRRIOC_GET_ENTRY)
static void stress_physpage_mtrr(
	stress_args_t *args,
	const uintptr_t phys_addr,
	const size_t page_size,
	bool *success)
{
	int fd;
	size_t i;
	static const uint32_t mtrr_types[] = {
		0, 	/* uncachable */
		1, 	/* write-combining */
		4, 	/* write-through */
		5, 	/* write-protect */
		6, 	/* write-back */
		0xff,	/* Illegal */
	};

	fd = open("/proc/mtrr", O_RDWR);
	if (fd < 0)
		return;

	for (i = 0; i < SIZEOF_ARRAY(mtrr_types); i++) {
		struct mtrr_sentry sentry;
		int ret;
		bool found = false;

		sentry.base = phys_addr;
		sentry.size = (uint32_t)page_size;
		sentry.type = mtrr_types[i];

		ret = ioctl(fd, MTRRIOC_ADD_ENTRY, &sentry);
		if (UNLIKELY(ret != 0))
			goto err;
		if (lseek(fd, 0, SEEK_SET) == 0) {
			struct mtrr_gentry gentry;

			gentry.regnum = 0;
			while (ioctl(fd, MTRRIOC_GET_ENTRY, &gentry) == 0) {
				if ((gentry.size == sentry.size) &&
				    (gentry.base == sentry.base) &&
				    (gentry.type == sentry.type)) {
					found = true;
				}
				gentry.regnum++;
			}
			if (UNLIKELY(!found)) {
				pr_fail("%s: cannot find mtrr entry at %p, size %zd, type %d\n",
					args->name, (void *)phys_addr, page_size, mtrr_types[i]);
				*success = false;
			}
		}
err:
		ret = ioctl(fd, MTRRIOC_DEL_ENTRY, &sentry);
		if (ret < 0)
			break;
	}
	(void)close(fd);
}
#endif

static int stress_virt_to_phys(
	stress_args_t *args,
	const size_t page_size,
	const int fd_pm,
	const int fd_pc,
	const int fd_mem,
	const uintptr_t virt_addr,
	const bool physpage_mtrr,
	const bool writable,
	bool *success)
{
	off_t offset;
	uint64_t pageinfo;
	ssize_t n;

	offset = (off_t)((virt_addr / page_size) * sizeof(uint64_t));
	if (UNLIKELY(lseek(fd_pm, offset, SEEK_SET) != offset)) {
		pr_err("%s: cannot seek on address %p in /proc/self/pagemap, errno=%d (%s)\n",
			args->name, (void *)virt_addr, errno, strerror(errno));
		goto err;
	}
	n = read(fd_pm, &pageinfo, sizeof(pageinfo));
	if (UNLIKELY(n < 0)) {
		pr_err("%s: cannot read address %p in /proc/self/pagemap, errno=%d (%s)\n",
			args->name, (void *)virt_addr, errno, strerror(errno));
		goto err;
	} else if (UNLIKELY(n != (ssize_t)sizeof(pageinfo))) {
		pr_fail("%s: read address %p in /proc/self/pagemap returned %zd bytes, expected %zd\n",
			args->name, (void *)virt_addr, (size_t)n, sizeof(pageinfo));
		goto err;
	}

	if (pageinfo & PAGE_PRESENT) {
		uint64_t page_count;
		const uint64_t pfn = pageinfo & PFN_MASK;
		uint64_t phys_addr = pfn * page_size;

		phys_addr |= (virt_addr & (page_size - 1));
		offset = (off_t)(pfn * sizeof(uint64_t));

		if (UNLIKELY(phys_addr == 0))
			return 0;
		if (UNLIKELY(fd_pc < 0))
			return 0;

		if (UNLIKELY(lseek(fd_pc, offset, SEEK_SET) != offset)) {
			pr_err("%s: cannot seek on address %p in /proc/kpagecount, errno=%d (%s)\n",
				args->name, (void *)virt_addr, errno, strerror(errno));
			goto err;
		}
		if (UNLIKELY(read(fd_pc, &page_count, sizeof(page_count)) != sizeof(page_count))) {
			pr_err("%s: cannot read page count for address %p in /proc/kpagecount, errno=%d (%s)\n",
				args->name, (void *)virt_addr, errno, strerror(errno));
			goto err;
		}
		if (UNLIKELY(page_count < 1)) {
			pr_fail("%s: got zero page count for physical address 0x%" PRIx64 "\n",
				args->name, phys_addr);
			goto err;
		}

		phys_addr = pfn * page_size;
		/*
		 *  Try to exercise /dev/mem, seek may work, read most probably
		 *  will fail with -EPERM. Ignore any errors, the aim here is
		 *  just to try and exercise this interface.
		 *  Modern systems fail /dev/mem reads with -EPERM because
		 *  of CONFIG_STRICT_DEVMEM being enabled.
		 */
		if (fd_mem >= 0) {
			off_t offret;
			uint8_t *ptr;

			offret = lseek(fd_mem, (off_t)phys_addr, SEEK_SET);
			if (offret != (off_t)-1) {
				char data[16];

				VOID_RET(ssize_t, read(fd_mem, data, sizeof(data)));
			}

			ptr = (uint8_t *)mmap(NULL, page_size, PROT_READ,
				MAP_SHARED, fd_mem, (off_t)phys_addr);
			if (ptr != MAP_FAILED) {
				stress_set_vma_anon_name(ptr, page_size, "ro-dev-mem");
				(void)stress_munmap_force((void *)ptr, page_size);
			}
			if (writable) {
				ptr = (uint8_t *)mmap(NULL, page_size, PROT_READ | PROT_WRITE,
					MAP_SHARED, fd_mem, (off_t)phys_addr);
				if (ptr != MAP_FAILED) {
					uint8_t val = *ptr;

					stress_set_vma_anon_name(ptr, page_size, "rw-dev-mem");
					*(volatile uint8_t *)ptr = val;
					(void)stress_munmap_force((void *)ptr, page_size);
				}
			}
		}

#if defined(STRESS_ARCH_X86) &&		\
    defined(HAVE_ASM_MTRR_H) &&		\
    defined(HAVE_MTRR_GENTRY) &&	\
    defined(HAVE_MTRR_SENTRY) &&	\
    defined(MTRRIOC_GET_ENTRY)
		if (UNLIKELY(physpage_mtrr))
			stress_physpage_mtrr(args, phys_addr, page_size, success);
#else
		(void)success;
		(void)physpage_mtrr;
#endif
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
static int stress_physpage(stress_args_t *args)
{
	int fd_pm, fd_pc, fd_mem;
	const size_t page_size = args->page_size;
	uint8_t *ptr = NULL;
	bool success = true;
	bool physpage_mtrr = false;

	(void)stress_get_setting("physpage-mtrr", &physpage_mtrr);

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
		if (stress_instance_zero(args))
			pr_dbg("%s: cannot open /proc/kpagecount, errno=%d (%s)\n",
				args->name, errno, strerror(errno));
		fd_pc = -1;
	}

	/*
	 *  this may fail, silently ignore failures
	 */
	fd_mem = open("/dev/mem", O_RDWR);
	if (fd_mem < 0)
		fd_mem = open("/dev/mem", O_RDONLY);

	stress_set_proc_state(args->name, STRESS_STATE_SYNC_WAIT);
	stress_sync_start_wait(args);
	stress_set_proc_state(args->name, STRESS_STATE_RUN);

	do {
		void *nptr;

		nptr = stress_mmap_populate((void *)ptr, page_size, PROT_READ | PROT_WRITE,
					MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
		if (nptr != MAP_FAILED) {
			stress_set_vma_anon_name(nptr, page_size, "rw-page");
			(void)stress_virt_to_phys(args, page_size, fd_pm, fd_pc, fd_mem,
				(uintptr_t)nptr, physpage_mtrr, true, &success);
			(void)stress_munmap_force(nptr, page_size);
			(void)stress_virt_to_phys(args, page_size, fd_pm, fd_pc, fd_mem,
				(uintptr_t)g_shared->stats, physpage_mtrr, false, &success);

		}
		ptr += page_size;
		stress_bogo_inc(args);
	} while (success && stress_continue(args));

	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);

	if (fd_mem >= 0)
		(void)close(fd_mem);
	if (fd_pc >= 0)
		(void)close(fd_pc);
	(void)close(fd_pm);

	return success ? EXIT_SUCCESS : EXIT_FAILURE;
}

const stressor_info_t stress_physpage_info = {
	.stressor = stress_physpage,
	.supported = stress_physpage_supported,
	.opts = opts,
	.classifier = CLASS_VM,
	.verify = VERIFY_ALWAYS,
	.help = help
};
#else
const stressor_info_t stress_physpage_info = {
	.stressor = stress_unimplemented,
	.opts = opts,
	.classifier = CLASS_VM,
	.verify = VERIFY_ALWAYS,
	.help = help,
	.unimplemented_reason = "only supported on Linux"
};
#endif
