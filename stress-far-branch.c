/*
 * Copyright (C) 2022-2025 Colin Ian King
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
#include "core-asm-ret.h"
#include "core-builtin.h"
#include "core-cpu-cache.h"
#include "core-madvise.h"
#include "core-mmap.h"
#include "core-pragma.h"

#define MIN_FAR_BRANCH_PAGES	(1)
#define MAX_FAR_BRANCH_PAGES	(65536)

static const stress_help_t help[] = {
	{ NULL,	"far-branch N",		"start N far branching workers" },
	{ NULL, "far-branch-flush",	"periodically flush instruction cache" },
	{ NULL,	"far-branch-ops N",	"stop after N far branching bogo operations" },
	{ NULL, "far-branch-pageout",	"soft offline and/or pageout object code pages" },
	{ NULL, "far-branch-pages N",	"number of pages to populate with functions" },
	{ NULL,	NULL,			NULL }
};

static const stress_opt_t opts[] = {
	{ OPT_far_branch_flush, "far-branch-flush", TYPE_ID_BOOL, 0, 1, NULL },
	{ OPT_far_branch_pageout, "far-branch-pageout", TYPE_ID_BOOL, 0, 1, NULL },
	{ OPT_far_branch_pages, "far-branch-pages", TYPE_ID_SIZE_T, MIN_FAR_BRANCH_PAGES, MAX_FAR_BRANCH_PAGES, NULL },
	END_OPT,
};

#define PAGE_MULTIPLES	(8)

#if defined(HAVE_MPROTECT) &&	\
    !defined(__NetBSD__)

static const int sigs[] = {
#if defined(SIGILL)
	SIGILL,
#endif
#if defined(SIGSEGV)
	SIGSEGV,
#endif
#if defined(SIGBUS)
	SIGBUS,
#endif
};

static void *sig_addr = NULL;
static int sig_num = -1;
static sigjmp_buf jmp_env;
static bool check_flag;

static void MLOCKED_TEXT stress_sig_handler(
        int sig,
        siginfo_t *info,
        void *ucontext)
{
	(void)ucontext;

	sig_num = sig;
	sig_addr = (info) ? info->si_addr : (void *)~(uintptr_t)0;

	stress_continue_set_flag(false);

	siglongjmp(jmp_env, 1);
	stress_no_return();
}

static inline void stress_far_branch_page_flush(void *page, const size_t page_size)
{
	if (mprotect(page, page_size, PROT_READ | PROT_WRITE | PROT_EXEC) == 0) {
		size_t i;
		uint8_t *data = (uint8_t *)page;

		for (i = 0; i < 64; i++)
			data[i] ^= 0xff;
		for (i = 0; i < 64; i++)
			data[i] ^= 0xff;
	}

	shim_flush_icache((char *)page, (char *)page + page_size);
	(void)shim_cacheflush((char *)page, page_size, SHIM_ICACHE);
	(void)mprotect(page, page_size, PROT_READ | PROT_EXEC);
}

#if defined(MAP_FIXED_NOREPLACE) ||	\
    (defined(HAVE_MSYNC) &&		\
     defined(MAP_FIXED))
/*
 *  stress_far_mmap_try32()
 *	try to mmap, if address is in 32 bit address space and
 *	x86-64 then try to force lower 32 bit address first
 */
static void *stress_far_mmap_try32(
	void *addr,
	size_t length,
	int prot,
	int flags,
	int fd,
	off_t offset)
{
	void *ptr;

#if defined(STRESS_ARCH_X86_64) &&	\
    defined(MAP_32BIT)
	/*
	 *  x86-64, offset in 32 bit address space, then try
	 *  MAP_32BIT first
	 */
	if (((uintptr_t)addr >> 32U) == 0)
		ptr = stress_mmap_populate(addr, length, prot, flags | MAP_32BIT, fd, offset);
	else
		ptr = stress_mmap_populate(addr, length, prot, flags, fd, offset);
#else
	ptr = stress_mmap_populate(addr, length, prot, flags, fd, offset);
#endif
	/* If mapped to a file, add random and willneed read hints */
	if ((ptr != MAP_FAILED) && (fd != -1)) {
		(void)stress_madvise_random(ptr, length);
		(void)stress_madvise_willneed(ptr, length);
	}
	return ptr;
}
#endif

static void *stress_far_try_mmap(void *addr, size_t length, const int fd, const off_t offset)
{
	const int anon = (fd == -1) ? MAP_ANONYMOUS : 0;

#if defined(MAP_FIXED_NOREPLACE)
	{
		void *ptr;

		ptr = (uint8_t *)stress_far_mmap_try32(addr, length, PROT_READ | PROT_WRITE,
			anon | MAP_SHARED | MAP_FIXED_NOREPLACE, fd, offset);
		if (ptr != MAP_FAILED) {
			(void)stress_madvise_mergeable(ptr, length);
			/* If mapped to a file, add random and willneed read hints */
			if (fd != -1) {
				(void)stress_madvise_random(ptr, length);
				(void)stress_madvise_willneed(ptr, length);
			}
			return ptr;
		}
	}
#endif
#if defined(HAVE_MSYNC) &&	\
    defined(MAP_FIXED)
	if ((msync(addr, length, MS_SYNC) < 0) &&
	    (errno == ENOMEM)) {
		void *ptr;

		ptr = (uint8_t *)stress_far_mmap_try32(addr, length, PROT_READ | PROT_WRITE,
			anon | MAP_SHARED | MAP_FIXED, fd, offset);
		if (ptr != MAP_FAILED) {
			(void)stress_madvise_mergeable(ptr, length);
			/* If mapped to a file, add random and willneed read hints */
			if (fd != -1) {
				(void)stress_madvise_random(ptr, length);
				(void)stress_madvise_willneed(ptr, length);
			}
			return ptr;
		}
	}
#endif
	(void)anon;
	(void)addr;
	(void)length;
	(void)offset;

	return MAP_FAILED;
}

/*
 *  stress_far_mmap()
 *	mmap and fill pages with return op codes to simulate
 *	functions that are spread around the address space
 */
static void *stress_far_mmap(
	const int fd,			/* Map file fd */
	const size_t page_size,		/* Page size */
	const uintptr_t base,		/* Base address (stress_far_branch) */
	size_t offset, 			/* Desired offset from base */
	stress_ret_func_t *funcs,	/* Array of function pointers */
	size_t *total_funcs,		/* Total number of functions */
	size_t *total_file_mapped_funcs)/* Total number of file mapped functions */
{
	uint8_t *ptr = MAP_FAILED;
	uintptr_t addr = (uintptr_t)NULL;
	size_t i, n;
	static size_t count = 0;

	/*
	 *  Have several attempts to mmap to a chosen location,
	 *  and shift and change offset if mapping failed (e.g.
	 *  due to clash or unmapped fixed location
	 */
	if (stress_mwc1()) {
		for (i = 0; offset && (i < 10); i++) {
			size_t j;

			for (j = 0; j < 10; j++) {
				offset += (stress_mwc8() * 4096);
				addr = base + offset;

				ptr = (uint8_t *)stress_far_try_mmap((void *)addr, page_size, -1, 0);
				if (ptr != MAP_FAILED)
					goto use_page;
				offset <<= 1;
			}
		}
	}

	/*
	 *  failed to mmap to a fixed address, try anywhere in
	 *  the entire address space
	 */
	for (i = 0; i < 10; i++) {
		if (sizeof(void *) > 4) {
			addr = (uintptr_t)stress_mwc64() >> (stress_mwc8modn(32));
		} else {
			addr = (uintptr_t)stress_mwc32() >> (stress_mwc8modn(12));
		}
		addr &= ~(uintptr_t)(page_size - 1);
		ptr = (uint8_t *)stress_far_try_mmap((void *)addr, page_size, -1, 0);
		if (ptr != MAP_FAILED)
			goto use_page;
	}

	/*
	 *  still no luck, try anything mmap gives us
	 */
	if (ptr == MAP_FAILED) {
		ptr = (uint8_t *)mmap(NULL, page_size, PROT_READ | PROT_WRITE,
					MAP_ANONYMOUS | MAP_SHARED, -1, 0);
		if (ptr == MAP_FAILED)
			return ptr;	/* Give up */
		(void)stress_madvise_mergeable(ptr, page_size);
	}

use_page:
	stress_set_vma_anon_name(ptr, page_size, "far-branch-returns");
	for (i = 0, n = 0; i < page_size; i += stress_ret_opcode.stride, n++) {
		(void)shim_memcpy((ptr + i), stress_ret_opcode.opcodes, stress_ret_opcode.len);
		funcs[*total_funcs + n] = (stress_ret_func_t)(ptr + i);
	}

	/*
	 *  1 in 16 pages are randomly chosen to be file mapped. The existing
	 *  mmap'd page is written to file and then unmapped. It is then re-mapped
	 *  back from the file backed data.
	 */
	if ((count++ & 0xf) == 0xf) {
		ssize_t ret;
		off_t seek_offset;

		/* Get current file position */
		seek_offset = lseek(fd, 0, SEEK_CUR);
		if (seek_offset == (off_t) -1)
			goto do_prot;

		/* Seek to page boundary */
		seek_offset &= ~(page_size - 1);
		seek_offset = lseek(fd, seek_offset, SEEK_SET);
		if (seek_offset == (off_t) -1)
			goto do_prot;

		/* Write page instructions to file */
		ret = write(fd, ptr, page_size);
		if (ret != (ssize_t)page_size)
			goto do_prot;

		/* Sync data and unmap */
		(void)shim_fsync(fd);
		if (munmap((void *)ptr, page_size) < 0)
			goto do_prot;

		/* Now map file data back as file backed mapping */
		ptr = (uint8_t *)mmap(NULL, page_size, PROT_READ | PROT_WRITE,
						MAP_SHARED, fd, seek_offset);
		if (ptr == MAP_FAILED)
			return MAP_FAILED;	/* Give up */
		(void)stress_madvise_mergeable(ptr, page_size);

		/* re-do func refs since mapping may be at new address */
		for (i = 0, n = 0; i < page_size; i += stress_ret_opcode.stride, n++)
			funcs[*total_funcs + n] = (stress_ret_func_t)(ptr + i);

		(*total_file_mapped_funcs) += n;
	}

do_prot:
	(void)mprotect((void *)ptr, page_size, PROT_READ | PROT_EXEC);
	(void)shim_cacheflush((char *)ptr, page_size, SHIM_ICACHE);
	(*total_funcs) += n;
	return ptr;
}

static void stress_far_branch_check(void)
{
	check_flag = true;
}

/*
 *  stress_far_branch_shuffle()
 *	Shuffle function pointers to get a fairly good
 *	random spread of address ranges to branch to
 *	on function call/returns
 */
static inline void stress_far_branch_shuffle(
	stress_ret_func_t *funcs,
	const size_t total_funcs,
	const size_t stride)
{
	register size_t i;

	for (i = 0; i < total_funcs; i += stride) {
		register const size_t k = stress_mwc32modn(total_funcs);
		register stress_ret_func_t tmp;

		tmp = funcs[i];
		funcs[i] = funcs[k];
		funcs[k] = tmp;
	}
}

#if defined(MADV_SOFT_OFFLINE) ||	\
    defined(MADV_PAGEOUT)
static inline void stress_far_branch_pageout(void *addr, const size_t page_size)
{
	(void)addr;
	(void)page_size;

#if defined(MADV_SOFT_OFFLINE)
	VOID_RET(int, madvise(addr, page_size, MADV_SOFT_OFFLINE));
#endif
#if defined(MADV_PAGEOUT)
	VOID_RET(int, madvise(addr, page_size, MADV_PAGEOUT));
#endif
}
#endif

/*
 *  stress_far_branch()
 *	exercise a broad randomized set of branches to functions
 *	that are spread around the entire address space; try to
 *	exercise branches that are relatively far from the stressor
 */
static int OPTIMIZE3 stress_far_branch(stress_args_t *args)
{
	size_t i, j, k;
	const size_t bits = sizeof(void *) * 8;
	size_t n_pages = (bits - 16) * PAGE_MULTIPLES;
	const size_t page_size = args->page_size;
	uintptr_t base = 0;
	size_t max_funcs;
	double t_start, t_next, duration, rate;
	struct sigaction sa;
	int ret, fd;
	NOCLOBBER stress_ret_func_t *funcs = NULL;
	NOCLOBBER void **pages = NULL;
	NOCLOBBER size_t total_funcs = 0, total_file_mapped_funcs = 0;
	NOCLOBBER size_t n_pages_failed, n_pages_mapped = 0;
	NOCLOBBER double calls = 0.0;
	NOCLOBBER bool far_branch_flush = false;
	NOCLOBBER bool far_branch_pageout = false;
	char filename[PATH_MAX];

	ret = stress_temp_dir_mk_args(args);
	if (ret < 0)
		return stress_exit_status(-ret);
	(void)stress_temp_filename_args(args,
		filename, sizeof(filename), stress_mwc32());
	if ((fd = open(filename, O_CREAT | O_RDWR, S_IRUSR | S_IWUSR)) < 0) {
		pr_fail("%s: open %s failed, errno=%d (%s)\n",
			args->name, filename, errno, strerror(errno));
		(void)stress_temp_dir_rm_args(args);
		return EXIT_NO_RESOURCE;
	}
	(void)shim_unlink(filename);

	(void)stress_get_setting("far-branch-flush", &far_branch_flush);
	(void)stress_get_setting("far-branch-pageout", &far_branch_pageout);
	if (!stress_get_setting("far-branch-pages", &n_pages)) {
		if (g_opt_flags & OPT_FLAGS_MAXIMIZE)
			n_pages = MAX_FAR_BRANCH_PAGES;
		if (g_opt_flags & OPT_FLAGS_MINIMIZE)
			n_pages = MIN_FAR_BRANCH_PAGES;
	}
	max_funcs = (n_pages * page_size) / stress_ret_opcode.stride;

	ret = sigsetjmp(jmp_env, 1);
	if (ret) {
		const char *sig_name = stress_get_signal_name(sig_num);
		static bool dumped = false;

		if (dumped)
			goto cleanup;

		dumped = true;
		if (!sig_name)
			sig_name = "(unknown)";

		pr_inf("%s: caught signal %d %s at %p\n",
			args->name, sig_num, sig_name, sig_addr);
		if (sig_num == SIGILL) {
			char buffer[256], *ptr = buffer;
			uint8_t *data = (uint8_t *)sig_addr;
			size_t buflen = sizeof(buffer);
			int len;

			len = snprintf(ptr, buflen, "%s: %p:", args->name, sig_addr);
			if (len < 0)
				goto cleanup;

			buflen -= len;
			ptr += len;
			for (i = 0; i < 8; i++, data++) {
				len = snprintf(ptr, buflen, " %2.2x", *data);
				if (len < 0)
					goto cleanup;
				buflen -= len;
				ptr += len;
			}
			pr_inf("%s\n", buffer);
		}
		goto cleanup;
	}

	if (stress_instance_zero(args))
		pr_dbg("%s: using assembler '%s' as function return code\n", args->name, stress_ret_opcode.assembler);

	(void)shim_memset(&sa, 0, sizeof(sa));
	sa.sa_sigaction = stress_sig_handler;
#if defined(SA_SIGINFO)
	sa.sa_flags = SA_SIGINFO;
#endif
	for (i = 0; i < SIZEOF_ARRAY(sigs); i++) {
		if (sigaction(sigs[i], &sa, NULL) < 0) {
			pr_err("%s: cannot install signal handler, errno=%d (%s)\n",
				args->name, errno, strerror(errno));
			(void)close(fd);
			return EXIT_FAILURE;
		}
	}

	funcs = (stress_ret_func_t *)calloc(max_funcs, sizeof(*funcs));
	if (!funcs) {
		pr_inf_skip("%s: cannot allocate %zu function "
			"pointers%s, skipping stressor\n",
			args->name, max_funcs, stress_get_memfree_str());
		(void)close(fd);
		return EXIT_NO_RESOURCE;
	}
	pages = (void **)calloc(n_pages, sizeof(*pages));
	if (!pages) {
		pr_inf_skip("%s: cannot allocate %zu page "
			"pointers%s, skipping stressor\n",
			args->name, n_pages, stress_get_memfree_str());
		free(funcs);
		(void)close(fd);
		return EXIT_NO_RESOURCE;
	}

	/*
	 *  Allocate pages and populate with simple return
	 *  functions spread across each page
	 */
	for (k = 0, i = 0; i < PAGE_MULTIPLES; i++) {
		for (j = 0; k < n_pages; j++, k++) {
			const size_t shift = (16 + j) & 0xf;
			size_t offset = ((uintptr_t)1 << shift) + (4 * page_size * (i + 4));

			pages[k] = stress_far_mmap(fd, page_size, base, offset,
						funcs, &total_funcs, &total_file_mapped_funcs);
			if (pages[k] != MAP_FAILED) {
				stress_set_vma_anon_name(pages[k], page_size, "functions-page");
				n_pages_mapped++;
			} else {
				n_pages_failed++;
			}
		}
	}

	if (n_pages_mapped == 0) {
		pr_inf("%s: failed to mmap pages%s, skipping stressor\n",
			args->name, stress_get_memfree_str());
	}
	if (n_pages_failed > 0) {
		pr_inf("fixing failed mappings\n");
		for (i = 0; i < n_pages; i++) {
			if (pages[i] != MAP_FAILED)
				continue;
			k = stress_mwc32modn((uint32_t)n_pages);
			for (j = 0; j < n_pages; j++)  {
				if (pages[k] != MAP_FAILED) {
					pages[i] = pages[j];
					break;
				}
				k++;
				if (k >= n_pages)
					k = 0;
			}
		}
	}

	total_funcs &= ~((size_t)0x1f);

	if (stress_instance_zero(args))
		pr_inf("%s: %zu x %zu byte functions over %zu x %zuK pages (using %zu file mapped functions)\n",
			args->name, total_funcs, stress_ret_opcode.stride,
			n_pages, page_size >> 10,
			total_file_mapped_funcs);

	funcs[0] = stress_far_branch_check;

	stress_far_branch_shuffle(funcs, total_funcs, 2);

	check_flag = false;

	stress_set_proc_state(args->name, STRESS_STATE_SYNC_WAIT);
	stress_sync_start_wait(args);
	stress_set_proc_state(args->name, STRESS_STATE_RUN);

	t_start = stress_time_now();
	t_next = t_start + 3.0;
	do {
#if defined(MADV_SOFT_OFFLINE) ||	\
    defined(MADV_PAGEOUT)
#if defined(HAVE_LABEL_AS_VALUE)
l1:
#endif
#endif
		for (i = 0; LIKELY(i < (total_funcs)) && UNLIKELY(stress_continue_flag()); i += 32) {
			funcs[i + 0x00]();
			funcs[i + 0x01]();
			funcs[i + 0x02]();
			funcs[i + 0x03]();
			funcs[i + 0x04]();
			funcs[i + 0x05]();
			funcs[i + 0x06]();
			funcs[i + 0x07]();
			funcs[i + 0x08]();
			funcs[i + 0x09]();
			funcs[i + 0x0a]();
			funcs[i + 0x0b]();
			funcs[i + 0x0c]();
			funcs[i + 0x0d]();
			funcs[i + 0x0e]();
			funcs[i + 0x0f]();
			funcs[i + 0x10]();
			funcs[i + 0x11]();
			funcs[i + 0x12]();
			funcs[i + 0x13]();
			funcs[i + 0x14]();
			funcs[i + 0x15]();
			funcs[i + 0x16]();
			funcs[i + 0x17]();
			funcs[i + 0x18]();
			funcs[i + 0x19]();
			funcs[i + 0x1a]();
			funcs[i + 0x1b]();
			funcs[i + 0x1c]();
			funcs[i + 0x1d]();
			funcs[i + 0x1e]();
			funcs[i + 0x1f]();
		}
		stress_bogo_inc(args);
		calls += (double)i;

		if (UNLIKELY(far_branch_flush)) {
			for (i = 0; i < n_pages; i++)
				stress_far_branch_page_flush(pages[i], page_size);
		}

#if defined(MADV_SOFT_OFFLINE) ||	\
    defined(MADV_PAGEOUT)
		if (UNLIKELY(far_branch_pageout)) {
			uintptr_t addr1, addr2;

			const size_t n = stress_mwc32modn((uint32_t)(n_pages >> 4)) + 1;

#if defined(HAVE_LABEL_AS_VALUE)
l2:
#endif
			for (i = 0; i < n; i++) {
				const size_t page = stress_mwc32modn((uint32_t)n_pages);

				stress_far_branch_pageout(pages[page], page_size);
			}

#if defined(HAVE_LABEL_AS_VALUE)
			addr1 = ((uintptr_t)&&l1) & ~(page_size - 1);
			addr2 = ((uintptr_t)&&l2) & ~(page_size - 1);

			stress_far_branch_pageout((void *)addr1, page_size);
			if (addr1 != addr2)
				stress_far_branch_pageout((void *)addr2, page_size);
#endif
		}
#endif
		/* Periodic branch address shuffle */
		if (stress_time_now() > t_next) {
			stress_far_branch_shuffle(funcs, total_funcs, ((stress_mwc8() & 0x1f) + 7));
			t_next = stress_time_now() + 3.0;
		}
	} while (stress_continue(args));
	duration = stress_time_now() - t_start;

	rate = (duration > 0.0) ? calls / duration : 0.0;
	stress_metrics_set(args, 0, "function calls per sec",
		rate, STRESS_METRIC_HARMONIC_MEAN);
	rate = (calls > 0.0) ? duration / calls: 0.0;
	stress_metrics_set(args, 1, "nanosecs per call/return",
		rate * STRESS_DBL_NANOSECOND, STRESS_METRIC_HARMONIC_MEAN);

	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);

cleanup:
	if (pages) {
		for (i = 0; i < n_pages; i++) {
			if (pages[i])
				(void)munmap((void *)pages[i], page_size);
		}
		free(pages);
	}
	free(funcs);
	(void)close(fd);

	(void)stress_temp_dir_rm_args(args);

	if (!check_flag && (calls > (double)total_funcs)) {
		pr_fail("%s: failed to execute check function\n", args->name);
		return EXIT_FAILURE;
	}
	return EXIT_SUCCESS;
}

const stressor_info_t stress_far_branch_info = {
	.stressor = stress_far_branch,
	.classifier = CLASS_CPU_CACHE,
	.verify = VERIFY_ALWAYS,
	.supported = stress_asm_ret_supported,
	.opts = opts,
	.help = help
};
#else
const stressor_info_t stress_far_branch_info = {
	.stressor = stress_unimplemented,
	.classifier = CLASS_CPU_CACHE,
	.verify = VERIFY_ALWAYS,
	.supported = stress_asm_ret_supported,
	.opts = opts,
	.help = help,
#if defined(__NetBSD__)
	.unimplemented_reason = "denied by NetBSD exploit mitigation features"
#else
	.unimplemented_reason = "built without mprotect() support or architecture not supported"
#endif
};
#endif
