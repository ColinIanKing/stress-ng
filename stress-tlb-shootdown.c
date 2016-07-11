/*
 * Copyright (C) 2013-2016 Canonical, Ltd.
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
#define _GNU_SOURCE

#include "stress-ng.h"

#if defined(STRESS_TLB_SHOOTDOWN)

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <signal.h>
#include <sched.h>

#define MAX_TLB_PROCS	(4)
#define MMAP_PAGES	(512)


/*
 *  stress_tlb_shootdown()
 *	stress out TLB shootdowns
 */
int stress_tlb_shootdown(
	uint64_t *const counter,
	const uint32_t instance,
	const uint64_t max_ops,
	const char *name)
{
	const size_t page_size = stress_get_pagesize();
	const size_t mmap_size = page_size * MMAP_PAGES;
	pid_t pids[MAX_TLB_PROCS];

	(void)instance;

	do {
		uint8_t *mem, *ptr;
		int retry = 128;
		cpu_set_t proc_mask;
		int32_t cpus, tlb_procs, i;
		const int32_t max_cpus = stress_get_processors_configured();

		if (sched_getaffinity(0, sizeof(proc_mask), &proc_mask) < 0) {
			pr_fail(stderr, "%s: could not get CPU affinity: "
				"errno=%d, (%s)\n",
				name, errno, strerror(errno));
			return EXIT_FAILURE;
		}
		cpus = CPU_COUNT(&proc_mask);
		tlb_procs = STRESS_MAXIMUM(cpus, MAX_TLB_PROCS);

		for (;;) {
			mem = mmap(NULL, mmap_size, PROT_WRITE | PROT_READ,
			MAP_SHARED | MAP_ANONYMOUS, -1, 0);
			if ((void *)mem == MAP_FAILED) {
				if ((errno == EAGAIN) || (errno == ENOMEM)) {
					if (--retry < 0)
						return EXIT_NO_RESOURCE;
				} else {
					pr_fail(stderr, "%s: mmap failed: "
						"errno=%d (%s)\n",
						name, errno, strerror(errno));
				}
			} else {
				break;
			}
		}
		memset(mem, 0, mmap_size);

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

				CPU_ZERO(&mask);
				CPU_SET(cpu % max_cpus, &mask);
				(void)sched_setaffinity(getpid(), sizeof(mask), &mask);

				for (ptr = mem; ptr < mem + mmap_size; ptr += page_size) {
					/* Force tlb shoot down on page */
					(void)mprotect(ptr, page_size, PROT_READ);
					memcpy(buffer, ptr, page_size);
					(void)munmap(ptr, page_size);
				}
				_exit(0);
			}
		}

		for (i = 0; i < tlb_procs; i++) {
			if (pids[i] != -1) {
				int status;

				kill(pids[i], SIGKILL);
				(void)waitpid(pids[i], &status, 0);
			}
		}
		(void)munmap(mem, mmap_size);
		(*counter)++;
	} while (opt_do_run && (!max_ops || *counter < max_ops));

	return EXIT_SUCCESS;
}

#endif
