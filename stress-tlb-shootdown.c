/*
 * Copyright (C) 2013-2021 Canonical, Ltd.
 * Copyright (C)      2022 Colin Ian King.
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
 *  stress_tlb_shootdown()
 *	stress out TLB shootdowns
 */
static int stress_tlb_shootdown(const stress_args_t *args)
{
	const size_t page_size = args->page_size;
	const size_t mmap_size = page_size * MMAP_PAGES;
	const size_t cache_lines = mmap_size >> STRESS_CACHE_LINE_SHIFT;
	const int32_t max_cpus = stress_get_processors_configured();
	pid_t pids[MAX_TLB_PROCS];
	const pid_t pid = getpid();
	cpu_set_t proc_mask_initial;
	int retry = 128;
	cpu_set_t proc_mask;
	int32_t tlb_procs, i;
	uint8_t *mem;

	for (;;) {
		mem = mmap(NULL, mmap_size, PROT_WRITE | PROT_READ,
			MAP_SHARED | MAP_ANONYMOUS, -1, 0);
		if ((void *)mem == MAP_FAILED) {
			if ((errno == EAGAIN) ||
			    (errno == ENOMEM) ||
			    (errno == ENFILE)) {
				if (--retry < 0)
					return EXIT_NO_RESOURCE;
			} else {
				pr_fail("%s: mmap failed, errno=%d (%s)\n",
					args->name, errno, strerror(errno));
				return EXIT_NO_RESOURCE;
			}
		} else {
			break;
		}
	}
	(void)memset(mem, 0xff, mmap_size);

	if (sched_getaffinity(0, sizeof(proc_mask_initial), &proc_mask_initial) < 0) {
		pr_fail("%s: sched_getaffinity could not get CPU affinity, errno=%d (%s)\n",
			args->name, errno, strerror(errno));
		return EXIT_FAILURE;
	}

	tlb_procs = max_cpus;
	if (tlb_procs > MAX_TLB_PROCS)
		tlb_procs = MAX_TLB_PROCS;
	if (tlb_procs < MIN_TLB_PROCS)
		tlb_procs = MIN_TLB_PROCS;

	CPU_ZERO(&proc_mask);
	CPU_OR(&proc_mask, &proc_mask_initial, &proc_mask);

	for (i = 0; i < tlb_procs; i++)
		pids[i] = -1;

	stress_set_proc_state(args->name, STRESS_STATE_RUN);

	for (i = 0; i < tlb_procs; i++) {
		int32_t j, cpu = -1;
		const size_t stride = (137 + (size_t)stress_get_prime64((uint64_t)cache_lines)) << STRESS_CACHE_LINE_SHIFT;
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

		pids[i] = fork();
		if (pids[i] < 0) {
			continue;
		} else if (pids[i] == 0) {
			cpu_set_t mask;

			stress_parent_died_alarm();
			(void)sched_settings_apply(true);

			/* Make sure this is killable by OOM killer */
			stress_set_oom_adjustment(args->name, true);

			CPU_ZERO(&mask);
			CPU_SET(cpu % max_cpus, &mask);
			(void)sched_setaffinity(args->pid, sizeof(mask), &mask);

			do {
				size_t l;
				size_t k = stress_mwc32() & mem_mask;
				volatile uint8_t *vmem;

				(void)mprotect(mem, mmap_size, PROT_READ);
				for (vmem = mem; vmem < mem + mmap_size; vmem += page_size) {
					size_t m;

					for (m = 0; m < page_size; m += STRESS_CACHE_LINE_SIZE)
						(void)vmem[m];
				}
				(void)mprotect(mem, mmap_size, PROT_WRITE);
				for (vmem = mem; vmem < mem + mmap_size; vmem += page_size) {
					size_t m;

					for (m = 0; m < page_size; m += STRESS_CACHE_LINE_SIZE)
						vmem[m] = m;
				}

				vmem = mem;
				(void)mprotect(mem, mmap_size, PROT_READ);
				for (l = 0; l < cache_lines; l++) {
					(void)vmem[k];
					k = (k + stride) & mem_mask;
				}
				(void)mprotect(mem, mmap_size, PROT_WRITE);
				for (l = 0; l < cache_lines; l++) {
					vmem[k] = (uint8_t)k;
					k = (k + stride) & mem_mask;
				}
				inc_counter(args);
			} while (keep_stressing(args));

			(void)kill(pid, SIGALRM);
			_exit(0);
		}
	}

	for (i = 0; i < tlb_procs; i++) {
		int status, ret;

		if (pids[i] == -1)
			continue;

		ret = waitpid(pids[i], &status, 0);
		if ((ret < 0) && (errno == EINTR)) {
			int j;

			/*
			 * We got interrupted, so assume
			 * it was the alarm (timedout) or
			 * SIGINT so force terminate
			 */
			for (j = i; j < tlb_procs; j++) {
				if (pids[j] != -1)
					(void)kill(pids[j], SIGKILL);
			}

			/* re-wait on the failed wait */
			(void)waitpid(pids[i], &status, 0);

			/* and continue waitpid on the pids */
		}
	}

	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);

	(void)munmap(mem, mmap_size);

	return EXIT_SUCCESS;
}

stressor_info_t stress_tlb_shootdown_info = {
	.stressor = stress_tlb_shootdown,
	.class = CLASS_OS | CLASS_MEMORY,
	.help = help
};
#else
stressor_info_t stress_tlb_shootdown_info = {
	.stressor = stress_unimplemented,
	.class = CLASS_OS | CLASS_MEMORY,
	.help = help,
	.unimplemented_reason = "built without sched_getaffinity() or mprotect() system calls"
};
#endif
