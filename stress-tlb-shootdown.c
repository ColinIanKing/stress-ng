/*
 * Copyright (C) 2013-2019 Canonical, Ltd.
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

static const help_t help[] = {
	{ NULL,	"tlb-shootdown N",	"start N workers that force TLB shootdowns" },
	{ NULL,	"tlb-shootdown-ops N",	"stop after N TLB shootdown bogo ops" },
	{ NULL,	NULL,			NULL }
};

#if defined(HAVE_SCHED_GETAFFINITY) && 	\
    defined(HAVE_MPROTECT)

#define MAX_TLB_PROCS	(8)
#define MIN_TLB_PROCS	(2)
#define MMAP_PAGES	(512)

/*
 *  stress_tlb_shootdown()
 *	stress out TLB shootdowns
 */
static int stress_tlb_shootdown(const args_t *args)
{
	const size_t page_size = args->page_size;
	const size_t mmap_size = page_size * MMAP_PAGES;
	pid_t pids[MAX_TLB_PROCS];
	cpu_set_t proc_mask_initial;

	if (sched_getaffinity(0, sizeof(proc_mask_initial), &proc_mask_initial) < 0) {
		pr_fail_err("could not get CPU affinity");
		return EXIT_FAILURE;
	}

	do {
		uint8_t *mem, *ptr;
		int retry = 128;
		cpu_set_t proc_mask;
		int32_t tlb_procs, i;
		const int32_t max_cpus = stress_get_processors_configured();

		CPU_ZERO(&proc_mask);
		CPU_OR(&proc_mask, &proc_mask_initial, &proc_mask);

		tlb_procs = max_cpus;
		if (tlb_procs > MAX_TLB_PROCS)
			tlb_procs = MAX_TLB_PROCS;
		if (tlb_procs < MIN_TLB_PROCS)
			tlb_procs = MIN_TLB_PROCS;

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
					pr_fail_err("mmap");
				}
			} else {
				break;
			}
		}
		(void)memset(mem, 0, mmap_size);

		for (i = 0; i < tlb_procs; i++)
			pids[i] = -1;

		for (i = 0; i < tlb_procs; i++) {
			int32_t j, cpu = -1;

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
			if (pids[i] < 0)
				break;
			if (pids[i] == 0) {
				cpu_set_t mask;
				char buffer[page_size];

				(void)setpgid(0, g_pgrp);
				stress_parent_died_alarm();

				/* Make sure this is killable by OOM killer */
				set_oom_adjustment(args->name, true);

				CPU_ZERO(&mask);
				CPU_SET(cpu % max_cpus, &mask);
				(void)sched_setaffinity(args->pid, sizeof(mask), &mask);

				for (ptr = mem; ptr < mem + mmap_size; ptr += page_size) {
					/* Force tlb shoot down on page */
					(void)mprotect(ptr, page_size, PROT_READ);
					(void)memcpy(buffer, ptr, page_size);
					(void)munmap(ptr, page_size);
				}
				_exit(0);
			}
		}

		for (i = 0; i < tlb_procs; i++) {
			if (pids[i] != -1) {
				int status, ret;

				ret = shim_waitpid(pids[i], &status, 0);
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
					(void)shim_waitpid(pids[i], &status, 0);

					/* and continue waitpid on the pids */
				}
			}
		}
		(void)munmap(mem, mmap_size);
		(void)sched_setaffinity(0, sizeof(proc_mask_initial), &proc_mask_initial);
		inc_counter(args);
	} while (keep_stressing());

	return EXIT_SUCCESS;
}

stressor_info_t stress_tlb_shootdown_info = {
	.stressor = stress_tlb_shootdown,
	.class = CLASS_OS | CLASS_MEMORY,
	.help = help
};
#else
stressor_info_t stress_tlb_shootdown_info = {
	.stressor = stress_not_implemented,
	.class = CLASS_OS | CLASS_MEMORY,
	.help = help
};
#endif
