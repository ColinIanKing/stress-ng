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
#include "core-affinity.h"
#include "core-builtin.h"
#include "core-cpu-cache.h"
#include "core-killpid.h"
#include "core-out-of-memory.h"
#include "core-pragma.h"
#include "core-prime.h"

#include <ctype.h>
#include <sched.h>

static const stress_help_t help[] = {
	{ NULL,	"tlb-shootdown N",	"start N workers that force TLB shootdowns" },
	{ NULL,	"tlb-shootdown-ops N",	"stop after N TLB shootdown bogo ops" },
	{ NULL,	NULL,			NULL }
};

#if defined(HAVE_SCHED_GETAFFINITY) && 	\
    defined(HAVE_MPROTECT)

#define MAX_TLB_PROCS		(8)
#define MIN_TLB_PROCS		(2)
#define MMAP_PAGES		(512)
#define MMAP_FD_PAGES		(4)
#define STRESS_CACHE_LINE_SHIFT	(6)	/* Typical 64 byte size */
#define STRESS_CACHE_LINE_SIZE	(1 << STRESS_CACHE_LINE_SHIFT)

/*
 * stress_tlb_interrupts()
 *	parse /proc/interrupts for per CPU TLB shootdown count
 */
static uint64_t stress_tlb_interrupts(void)
{
#if defined(__linux__)
	FILE *fp;
	char buffer[8192];
	uint64_t total = 0;

	fp = fopen("/proc/interrupts", "r");
	if (!fp)
		return 0ULL;

	(void)shim_memset(buffer, 0, sizeof(buffer));
	while (fgets(buffer, sizeof(buffer), fp) != NULL) {
		char *ptr;
		char *eptr;

		ptr = strstr(buffer, "TLB:");
		if (!ptr)
			continue;

		ptr += 4; /* skip over TLB: */
		while (*ptr) {
			long long val;

			/* skip over spaces */
			while (*ptr == ' ')
				ptr++;
			/* end of string? */
			if (!*ptr)
				break;
			/* not a digit? */
			if (!isdigit((int)*ptr))
				break;

			eptr = NULL;
			val = strtoll(ptr, &eptr, 10);
			/* no number parsed? */
			if (!eptr)
				break;
			/* should be positive */
			if (val < 0)
				break;
			/* sum per CPU TLB shootdown count */
			total += (uint64_t)val;
			ptr = eptr;
		}
		break;
	}
	(void)fclose(fp);

	return total;
#else
	return 0ULL;
#endif
}

/*
 *  stress_tlb_shootdown_read_mem()
 *	read from every cache line in mem
 */
static inline void ALWAYS_INLINE stress_tlb_shootdown_read_mem(
	const uint8_t *mem,
	const size_t size,
	const size_t page_size)
{
	const volatile uint8_t *vmem;

	for (vmem = mem; vmem < mem + size; vmem += page_size) {
		register size_t m;

		for (m = 0; m < page_size; ) {
			(void)vmem[m];
			m += STRESS_CACHE_LINE_SIZE;
			(void)vmem[m];
			m += STRESS_CACHE_LINE_SIZE;
			(void)vmem[m];
			m += STRESS_CACHE_LINE_SIZE;
			(void)vmem[m];
			m += STRESS_CACHE_LINE_SIZE;
			(void)vmem[m];
			m += STRESS_CACHE_LINE_SIZE;
			(void)vmem[m];
			m += STRESS_CACHE_LINE_SIZE;
			(void)vmem[m];
			m += STRESS_CACHE_LINE_SIZE;
			(void)vmem[m];
			m += STRESS_CACHE_LINE_SIZE;
		}
	}
}

/*
 *  stress_tlb_shootdown_read_mem()
 *	write to every cache line in mem
 */
static inline void ALWAYS_INLINE stress_tlb_shootdown_write_mem(
	uint8_t *mem,
	const size_t size,
	const size_t page_size)
{
	volatile uint8_t *vmem;
	const uint8_t rnd8 = stress_mwc8();

	for (vmem = mem; vmem < mem + size; vmem += page_size) {
		register size_t m;

		for (m = 0; m < page_size; ) {
			vmem[m] = m + rnd8;
			m += STRESS_CACHE_LINE_SIZE;
			vmem[m] = m + rnd8;
			m += STRESS_CACHE_LINE_SIZE;
			vmem[m] = m + rnd8;
			m += STRESS_CACHE_LINE_SIZE;
			vmem[m] = m + rnd8;
			m += STRESS_CACHE_LINE_SIZE;
			vmem[m] = m + rnd8;
			m += STRESS_CACHE_LINE_SIZE;
			vmem[m] = m + rnd8;
			m += STRESS_CACHE_LINE_SIZE;
			vmem[m] = m + rnd8;
			m += STRESS_CACHE_LINE_SIZE;
			vmem[m] = m + rnd8;
			m += STRESS_CACHE_LINE_SIZE;
		}
	}
	stress_cpu_data_cache_flush((void *)mem, size);
}

/*
 *  stress_tlb_shootdown_read_mem()
 *	mmap with retries
 */
static void *stress_tlb_shootdown_mmap(
	stress_args_t *args,
	void *addr,
	size_t length,
	int prot,
	int flags,
	int fd,
	off_t offset)
{
	int retry = 128;
	void *mem;

	do {
		mem = mmap(addr, length, prot, flags, fd, offset);
		if (LIKELY((void *)mem != MAP_FAILED))
			return mem;
		if ((errno == EAGAIN) ||
		    (errno == ENOMEM) ||
		    (errno == ENFILE)) {
			retry--;
		} else {
			break;
		}
	} while (retry > 0);

	pr_inf_skip("%s: failed to mmap %zu bytes%s, errno=%d (%s), skipping stressor\n",
		args->name, length, stress_get_memfree_str(),
		errno, strerror(errno));
	return mem;
}

static void OPTIMIZE3 stress_tlb_shootdown_child(
	stress_args_t *args,
	const uint32_t n_cpus,
	const uint32_t i,
	const size_t stride,
	const size_t mmap_size,
	const size_t mmap_mask,
	const size_t page_size,
	const size_t page_mask,
#if defined(HAVE_MADVISE) &&	\
    defined(MADV_DONTNEED)
	const size_t mmapfd_size,
	const size_t mmapfd_mask,
	uint8_t *memfd,
#endif
	stress_pid_t *s_pids,
	uint8_t *mem,
	uint32_t *cpus)
{
	cpu_set_t mask;
	double t_start, t_next;
	uint32_t cpu_idx = 0;
	size_t offset;
	const size_t cache_lines = mmap_size >> STRESS_CACHE_LINE_SHIFT;

	s_pids[i].pid = getpid();

	stress_parent_died_alarm();
	(void)sched_settings_apply(true);

	/* Make sure this is killable by OOM killer */
	stress_set_oom_adjustment(args, true);

	stress_sync_start_wait_s_pid(&s_pids[i]);

	if (LIKELY(n_cpus > 0)) {
		CPU_ZERO(&mask);
		CPU_SET((int)cpus[cpu_idx], &mask);
		(void)sched_setaffinity(args->pid, sizeof(mask), &mask);
	}

	t_start = stress_time_now();
	t_next = t_start + 1.0;

	do {
		size_t l;
		size_t k = stress_mwc32() & mmap_mask;
		const uint8_t rnd8 = stress_mwc8();
		volatile uint8_t *vmem;

		offset = (stress_mwc32() & mmap_mask) & page_mask;
		(void)mprotect(mem + offset, page_size, PROT_READ);
		stress_tlb_shootdown_read_mem(mem + offset, page_size, page_size);

		(void)mprotect(mem + offset, page_size, PROT_WRITE);
		stress_tlb_shootdown_write_mem(mem + offset, page_size, page_size);

		vmem = mem;
		(void)mprotect(mem, mmap_size, PROT_READ);
PRAGMA_UNROLL_N(8)
		for (l = 0; l < cache_lines; l++) {
			(void)vmem[k];
			k = (k + stride) & mmap_mask;
		}
		(void)mprotect(mem, mmap_size, PROT_WRITE);
PRAGMA_UNROLL_N(8)
		for (l = 0; l < cache_lines; l++) {
			vmem[k] = (uint8_t)(k + rnd8);
			k = (k + stride) & mmap_mask;
		}
		(void)mprotect(mem, mmap_size, PROT_READ | PROT_WRITE);
#if defined(HAVE_MADVISE) &&	\
    defined(SHIM_MADV_DONTNEED)
		offset = (stress_mwc32() & mmapfd_mask) & mmap_mask;

		(void)shim_madvise(mem + offset, page_size, SHIM_MADV_DONTNEED);
		stress_tlb_shootdown_read_mem(mem + offset, page_size, page_size);

		(void)shim_madvise(memfd + offset, page_size, SHIM_MADV_DONTNEED);
		stress_tlb_shootdown_write_mem(memfd, page_size, page_size);
		shim_msync(memfd, mmapfd_size, MS_ASYNC);
#endif
		stress_bogo_inc(args);

		/*
		 *  periodically change cpu affinity
		 */
		if (UNLIKELY((stress_time_now() >= t_next) && (n_cpus > 0))) {
			cpu_idx++;
			cpu_idx = (cpu_idx >= n_cpus) ? 0 : cpu_idx;

			CPU_ZERO(&mask);
			CPU_SET(cpus[cpu_idx], &mask);
			(void)sched_setaffinity(args->pid, sizeof(mask), &mask);
			t_next += 1.0;
		}
	} while (stress_continue(args));
}

/*
 *  stress_tlb_shootdown()
 *	stress out TLB shootdowns
 */
static int stress_tlb_shootdown(stress_args_t *args)
{
	double rate, t_begin, duration;
	uint64_t tlb_begin, tlb_end;
	const size_t page_size = args->page_size;
	const size_t page_mask = ~(page_size - 1);
	const size_t mmap_size = page_size * MMAP_PAGES;
	const size_t mmap_mask = mmap_size - 1;
	const size_t cache_lines = mmap_size >> STRESS_CACHE_LINE_SHIFT;
	uint32_t *cpus;
	const uint32_t n_cpus = stress_get_usable_cpus(&cpus, true);
	stress_pid_t *s_pids, *s_pids_head = NULL;
	const pid_t pid = getpid();
	int rc = EXIT_SUCCESS;
	uint32_t tlb_procs, i;
	uint8_t *mem;
#if defined(HAVE_MADVISE) &&	\
    defined(MADV_DONTNEED)
	int fd, ret;
	uint8_t *memfd;
	const size_t mmapfd_size = page_size * MMAP_FD_PAGES;
	const size_t mmapfd_mask = mmapfd_size - 1;
	char filename[PATH_MAX];
#endif

	s_pids = stress_sync_s_pids_mmap(MAX_TLB_PROCS);
	if (s_pids == MAP_FAILED) {
		pr_inf_skip("%s: failed to mmap %d PIDs%s, skipping stressor\n",
			args->name, MAX_TLB_PROCS, stress_get_memfree_str());
		rc = EXIT_NO_RESOURCE;
		goto err_free_cpus;
	}

#if defined(HAVE_MADVISE) &&	\
    defined(MADV_DONTNEED)
	ret = stress_temp_dir_mk_args(args);
	if (ret < 0) {
		rc = stress_exit_status(-ret);
		goto err_s_pids;
	}
	(void)stress_temp_filename_args(args,
		filename, sizeof(filename), stress_mwc32());
	if ((fd = open(filename, O_CREAT | O_RDWR, S_IRUSR | S_IWUSR)) < 0) {
		ret = stress_exit_status(errno);
		pr_fail("%s: open on %s failed, errno=%d (%s)\n",
			args->name, filename, errno, strerror(errno));
		rc = ret;
		goto err_rmdir;
	}
	(void)shim_unlink(filename);
	if (ftruncate(fd, mmapfd_size) < 0) {
		pr_fail("%s: ftruncate to %zu bytes on %s failed, errno=%d (%s)\n",
			args->name, mmapfd_size, filename, errno, strerror(errno));
		rc = EXIT_NO_RESOURCE;
		goto err_close;
	}
	memfd = stress_tlb_shootdown_mmap(args, NULL, mmapfd_size,
			PROT_WRITE | PROT_READ, MAP_SHARED, fd, 0);
	if ((void *)memfd == MAP_FAILED) {
		rc = EXIT_NO_RESOURCE;
		goto err_close;
	}
#if defined(MADV_NOHUGEPAGE)
	(void)shim_madvise(memfd, mmapfd_size, MADV_NOHUGEPAGE);
#endif
#endif

	mem = stress_tlb_shootdown_mmap(args, NULL, mmap_size,
			PROT_WRITE | PROT_READ,
			MAP_SHARED | MAP_ANONYMOUS, -1, 0);
	if ((void *)mem == MAP_FAILED) {
		rc = EXIT_NO_RESOURCE;
		goto err_munmap_memfd;
	}
#if defined(MADV_NOHUGEPAGE)
	(void)shim_madvise(mem, mmap_size, MADV_NOHUGEPAGE);
#endif
	stress_set_vma_anon_name(mem, mmap_size, "tlb-shootdown-buffer");
	(void)shim_memset(mem, 0xff, mmap_size);

	tlb_procs = n_cpus;
	if (tlb_procs > MAX_TLB_PROCS)
		tlb_procs = MAX_TLB_PROCS;
	if (tlb_procs < MIN_TLB_PROCS)
		tlb_procs = MIN_TLB_PROCS;

	t_begin = stress_time_now();
	tlb_begin = stress_tlb_interrupts();

	for (i = 0; i < tlb_procs; i++)
		stress_sync_start_init(&s_pids[i]);

	for (i = 0; i < tlb_procs; i++) {
		const size_t stride = (137 + (size_t)stress_get_next_prime64((uint64_t)cache_lines)) << STRESS_CACHE_LINE_SHIFT;

		s_pids[i].pid = fork();
		if (s_pids[i].pid < 0) {
			continue;
		} else if (s_pids[i].pid == 0) {
			stress_set_proc_state(args->name, STRESS_STATE_RUN);
			stress_tlb_shootdown_child(args, n_cpus, i, stride,
					mmap_size, mmap_mask,
					page_size, page_mask,
#if defined(HAVE_MADVISE) &&	\
    defined(MADV_DONTNEED)
					mmapfd_size, mmapfd_mask, memfd,
#endif
					s_pids, mem, cpus);

			(void)shim_kill(pid, SIGALRM);
			_exit(0);
		} else {
			stress_sync_start_s_pid_list_add(&s_pids_head, &s_pids[i]);
		}
	}

	stress_set_proc_state(args->name, STRESS_STATE_SYNC_WAIT);
	stress_sync_start_wait(args);
	stress_sync_start_cont_list(s_pids_head);
	stress_set_proc_state(args->name, STRESS_STATE_RUN);

	do {
#if defined(HAVE_MADVISE) &&	\
    defined(SHIM_MADV_DONTNEED)
		size_t offset;

		offset = (stress_mwc32() & mmapfd_mask) & page_mask;
		(void)shim_madvise(memfd + offset, page_size, SHIM_MADV_DONTNEED);
		stress_tlb_shootdown_write_mem(memfd, page_size, page_size);
		(void)shim_msync(memfd, mmapfd_size, MS_SYNC);

		(void)shim_madvise(memfd + offset, page_size, SHIM_MADV_DONTNEED);
		stress_tlb_shootdown_read_mem(memfd + offset, page_size, page_size);
		(void)shim_msync(memfd, mmapfd_size, MS_SYNC);

		offset = (stress_mwc32() & mmap_mask) & page_mask;
		(void)shim_madvise(mem + offset, page_size, SHIM_MADV_DONTNEED);
		stress_tlb_shootdown_read_mem(mem + offset, page_size, page_size);

		(void)shim_madvise(mem + offset, page_size, SHIM_MADV_DONTNEED);
		stress_tlb_shootdown_write_mem(mem + offset, page_size, page_size);
#endif
#if defined(__linux__)
		{
			static const char flush_ceiling[] = "/sys/kernel/debug/x86/tlb_single_page_flush_ceiling";
			char buf[64];
			ssize_t rd_ret;

			rd_ret = stress_system_read(flush_ceiling, buf, sizeof(buf));
			if (rd_ret > 0)
				VOID_RET(ssize_t, stress_system_write(flush_ceiling, buf, rd_ret));
		}
#endif
#if defined(HAVE_MADVISE) &&	\
    defined(MADV_NOHUGEPAGE) && 	\
    defined(MADV_COLLAPSE)
		(void)shim_madvise(mem, mmap_size, MADV_COLLAPSE);
		(void)shim_madvise(mem, mmap_size, MADV_NOHUGEPAGE);

		(void)shim_madvise(memfd, mmapfd_size, MADV_COLLAPSE);
		(void)shim_madvise(memfd, mmapfd_size, MADV_NOHUGEPAGE);
		(void)shim_msync(memfd, mmapfd_size, MS_SYNC);
#endif

		stress_bogo_inc(args);
	} while (stress_continue(args));

	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);
	tlb_end = stress_tlb_interrupts();
	duration = stress_time_now() - t_begin;

	rate = (duration > 0.0) ? (double)(tlb_end - tlb_begin) / duration : 0.0;
	if (rate > 0.0)
		stress_metrics_set(args, 0, "TLB shootdowns/sec", rate, STRESS_METRIC_GEOMETRIC_MEAN);

	stress_kill_and_wait_many(args, s_pids, tlb_procs, SIGALRM, true);

	(void)munmap((void *)mem, mmap_size);
err_munmap_memfd:
#if defined(HAVE_MADVISE) &&	\
    defined(MADV_DONTNEED)
	(void)munmap((void *)memfd, mmapfd_size);
err_close:
	(void)close(fd);
err_rmdir:
	(void)stress_temp_dir_rm_args(args);
err_s_pids:
	(void)stress_sync_s_pids_munmap(s_pids, MAX_TLB_PROCS);
#endif
err_free_cpus:
	stress_free_usable_cpus(&cpus);

	return rc;
}

const stressor_info_t stress_tlb_shootdown_info = {
	.stressor = stress_tlb_shootdown,
	.classifier = CLASS_OS | CLASS_MEMORY,
	.help = help
};
#else
const stressor_info_t stress_tlb_shootdown_info = {
	.stressor = stress_unimplemented,
	.classifier = CLASS_OS | CLASS_MEMORY,
	.help = help,
	.unimplemented_reason = "built without sched_getaffinity() or mprotect() system calls"
};
#endif
