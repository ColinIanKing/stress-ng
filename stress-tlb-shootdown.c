/*
 * Copyright (C) 2013-2021 Canonical, Ltd.
 * Copyright (C) 2022-2024 Colin Ian King.
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
#include "core-killpid.h"
#include "core-out-of-memory.h"
#include "core-pragma.h"

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
#define STRESS_CACHE_LINE_SHIFT	(6)	/* Typical 64 byte size */
#define STRESS_CACHE_LINE_SIZE	(1 << STRESS_CACHE_LINE_SHIFT)

/*
 *  stress_tlb_shootdown_read_mem()
 *	read from every cache line in mem
 */
static inline void OPTIMIZE3 stress_tlb_shootdown_read_mem(
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
static inline void OPTIMIZE3 stress_tlb_shootdown_write_mem(
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
	(void)shim_cacheflush((char *)mem, (int)size, SHIM_DCACHE);
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
		if ((void *)mem != MAP_FAILED)
			return mem;
		if ((errno == EAGAIN) ||
		    (errno == ENOMEM) ||
		    (errno == ENFILE)) {
			retry--;
		} else {
			break;
		}
	} while (retry > 0);

	pr_inf_skip("%s: mmap failed, errno=%d (%s), skipping stressor\n",
		args->name, errno, strerror(errno));
	return mem;
}

/*
 *  stress_tlb_shootdown()
 *	stress out TLB shootdowns
 */
static int stress_tlb_shootdown(stress_args_t *args)
{
	const size_t page_size = args->page_size;
	const size_t mmap_size = page_size * MMAP_PAGES;
	const size_t cache_lines = mmap_size >> STRESS_CACHE_LINE_SHIFT;
	const int32_t max_cpus = stress_get_processors_configured();
	stress_pid_t *s_pids, *s_pids_head = NULL;
	const pid_t pid = getpid();
	cpu_set_t proc_mask_initial;
	int rc = EXIT_SUCCESS;
	cpu_set_t proc_mask;
	int32_t tlb_procs, i;
	uint8_t *mem;
#if defined(HAVE_MADVISE) &&	\
    defined(MADV_DONTNEED)
	int fd, ret;
	uint8_t *memfd;
	const size_t mmapfd_size = page_size * 4;
	char filename[PATH_MAX];
#endif

	s_pids = stress_s_pids_mmap(MAX_TLB_PROCS);
	if (s_pids == MAP_FAILED) {
		pr_inf_skip("%s: failed to mmap %d PIDs, skipping stressor\n", args->name, MAX_TLB_PROCS);
		return EXIT_NO_RESOURCE;
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
#endif

	mem = stress_tlb_shootdown_mmap(args, NULL, mmap_size,
			PROT_WRITE | PROT_READ,
			MAP_SHARED | MAP_ANONYMOUS, -1, 0);
	if ((void *)mem == MAP_FAILED) {
		rc = EXIT_NO_RESOURCE;
		goto err_munmap_memfd;
	}
	(void)shim_memset(mem, 0xff, mmap_size);

	if (sched_getaffinity(0, sizeof(proc_mask_initial), &proc_mask_initial) < 0) {
		pr_fail("%s: sched_getaffinity could not get CPU affinity, errno=%d (%s)\n",
			args->name, errno, strerror(errno));
		rc = EXIT_FAILURE;
		goto err_munmap_mem;
	}

	tlb_procs = max_cpus;
	if (tlb_procs > MAX_TLB_PROCS)
		tlb_procs = MAX_TLB_PROCS;
	if (tlb_procs < MIN_TLB_PROCS)
		tlb_procs = MIN_TLB_PROCS;

	CPU_ZERO(&proc_mask);
	CPU_OR(&proc_mask, &proc_mask_initial, &proc_mask);

	for (i = 0; i < tlb_procs; i++)
		stress_sync_start_init(&s_pids[i]);

	for (i = 0; i < tlb_procs; i++) {
		int32_t j, cpu = -1;
		const size_t stride = (137 + (size_t)stress_get_next_prime64((uint64_t)cache_lines)) << STRESS_CACHE_LINE_SHIFT;
		const size_t mem_mask = (mmap_size - 1);

		for (j = 0; j < max_cpus; j++) {
			if (CPU_ISSET(j, &proc_mask)) {
				cpu = j;
				CPU_CLR(j, &proc_mask);
				break;
			}
		}
		if (cpu == -1)
			break;

		s_pids[i].pid = fork();
		if (s_pids[i].pid < 0) {
			continue;
		} else if (s_pids[i].pid == 0) {
			cpu_set_t mask;
			double t_start, t_next;

			s_pids[i].pid = getpid();

			stress_parent_died_alarm();
			(void)sched_settings_apply(true);

			/* Make sure this is killable by OOM killer */
			stress_set_oom_adjustment(args, true);

                        stress_sync_start_wait_s_pid(&s_pids[i]);

			CPU_ZERO(&mask);
			CPU_SET(cpu % max_cpus, &mask);
			(void)sched_setaffinity(args->pid, sizeof(mask), &mask);

			t_start = stress_time_now();
			t_next = t_start + 1.0;

			do {
				size_t l;
				size_t k = stress_mwc32() & mem_mask;
				const uint8_t rnd8 = stress_mwc8();
				volatile uint8_t *vmem;

				(void)mprotect(mem, mmap_size, PROT_READ);
				stress_tlb_shootdown_read_mem(mem, mmap_size, page_size);

				(void)mprotect(mem, mmap_size, PROT_WRITE);
				stress_tlb_shootdown_write_mem(mem, mmap_size, page_size);

				vmem = mem;
				(void)mprotect(mem, mmap_size, PROT_READ);
PRAGMA_UNROLL_N(8)
				for (l = 0; l < cache_lines; l++) {
					(void)vmem[k];
					k = (k + stride) & mem_mask;
				}
				(void)mprotect(mem, mmap_size, PROT_WRITE);
PRAGMA_UNROLL_N(8)
				for (l = 0; l < cache_lines; l++) {
					vmem[k] = (uint8_t)(k + rnd8);
					k = (k + stride) & mem_mask;
				}
				stress_bogo_inc(args);

				/*
				 *  periodically change cpu affinity
				 */
				if (stress_time_now() >= t_next) {
					cpu++;
					cpu %= max_cpus;
					CPU_ZERO(&mask);
					CPU_SET(cpu, &mask);
					(void)sched_setaffinity(args->pid, sizeof(mask), &mask);
					t_next += 1.0;
				}
			} while (stress_continue(args));

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
    defined(MADV_DONTNEED)
		(void)madvise(memfd, mmapfd_size, MADV_DONTNEED);
		stress_tlb_shootdown_write_mem(memfd, mmapfd_size, page_size);

		(void)madvise(memfd, mmapfd_size, MADV_DONTNEED);
		stress_tlb_shootdown_read_mem(memfd, mmapfd_size, page_size);
#endif

		(void)madvise(mem, mmap_size, MADV_DONTNEED);
		stress_tlb_shootdown_read_mem(mem, mmap_size, page_size);

		(void)madvise(mem, mmap_size, MADV_DONTNEED);
		stress_tlb_shootdown_write_mem(mem, mmap_size, page_size);

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

		stress_bogo_inc(args);
	} while(stress_continue(args));

	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);

	stress_kill_and_wait_many(args, s_pids, tlb_procs, SIGALRM, true);
err_munmap_mem:
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
	(void)stress_s_pids_munmap(s_pids, MAX_TLB_PROCS);
#endif
	return rc;
}

const stressor_info_t stress_tlb_shootdown_info = {
	.stressor = stress_tlb_shootdown,
	.class = CLASS_OS | CLASS_MEMORY,
	.help = help
};
#else
const stressor_info_t stress_tlb_shootdown_info = {
	.stressor = stress_unimplemented,
	.class = CLASS_OS | CLASS_MEMORY,
	.help = help,
	.unimplemented_reason = "built without sched_getaffinity() or mprotect() system calls"
};
#endif
