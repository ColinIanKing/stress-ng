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

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <inttypes.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>

#include "stress-ng.h"

static uint64_t opt_bigheap_growth = DEFAULT_BIGHEAP_GROWTH;
static bool set_bigheap_growth = false;

/*
 *  stress_set_bigheap_growth()
 *  	Set bigheap growth from given opt arg string
 */
void stress_set_bigheap_growth(const char *optarg)
{
	set_bigheap_growth = true;
	opt_bigheap_growth = get_uint64_byte(optarg);
	check_range("bigheap-growth", opt_bigheap_growth,
		MIN_BIGHEAP_GROWTH, MAX_BIGHEAP_GROWTH);
}

/*
 *  stress_bigheap()
 *	stress heap allocation
 */
int stress_bigheap(
	uint64_t *const counter,
	const uint32_t instance,
	const uint64_t max_ops,
	const char *name)
{
	void *ptr = NULL, *last_ptr = NULL;
	const size_t page_size = stress_get_pagesize();
	const size_t stride = page_size;
	size_t size = 0;
	uint32_t ooms = 0, segvs = 0, nomems = 0;
	pid_t pid;
	uint8_t *last_ptr_end = NULL;

	if (!set_bigheap_growth) {
		if (opt_flags & OPT_FLAGS_MAXIMIZE)
			opt_bigheap_growth = MAX_BIGHEAP_GROWTH;
		if (opt_flags & OPT_FLAGS_MINIMIZE)
			opt_bigheap_growth = MIN_BIGHEAP_GROWTH;
	}
again:
	if (!opt_do_run)
		return EXIT_SUCCESS;
	pid = fork();
	if (pid < 0) {
		if (errno == EAGAIN)
			goto again;
		pr_err(stderr, "%s: fork failed: errno=%d: (%s)\n",
			name, errno, strerror(errno));
	} else if (pid > 0) {
		int status, ret;

		setpgid(pid, pgrp);
		/* Parent, wait for child */
		ret = waitpid(pid, &status, 0);
		if (ret < 0) {
			if (errno != EINTR)
				pr_dbg(stderr, "%s: waitpid(): errno=%d (%s)\n",
					name, errno, strerror(errno));
			(void)kill(pid, SIGTERM);
			(void)kill(pid, SIGKILL);
			(void)waitpid(pid, &status, 0);
		} else if (WIFSIGNALED(status)) {
			pr_dbg(stderr, "%s: child died: %s (instance %d)\n",
				name, stress_strsignal(WTERMSIG(status)),
				instance);
			/* If we got killed by OOM killer, re-start */
			if (WTERMSIG(status) == SIGKILL) {
				log_system_mem_info();
				pr_dbg(stderr, "%s: assuming killed by OOM "
					"killer, restarting again "
					"(instance %d)\n",
					name, instance);
				ooms++;
				goto again;
			}
			/* If we got killed by sigsegv, re-start */
			if (WTERMSIG(status) == SIGSEGV) {
				pr_dbg(stderr, "%s: killed by SIGSEGV, "
					"restarting again "
					"(instance %d)\n",
					name, instance);
				segvs++;
				goto again;
			}
		}
	} else if (pid == 0) {
		setpgid(0, pgrp);
		stress_parent_died_alarm();

		/* Make sure this is killable by OOM killer */
		set_oom_adjustment(name, true);

		do {
			void *old_ptr = ptr;
			size += (size_t)opt_bigheap_growth;

			/*
			 * With many instances running it is wise to
			 * double check before the next realloc as
			 * sometimes process start up is delayed for
			 * some time and we should bail out before
			 * exerting any more memory pressure
			 */
			if (!opt_do_run)
				goto abort;

			ptr = realloc(old_ptr, size);
			if (ptr == NULL) {
				pr_dbg(stderr, "%s: out of memory at %" PRIu64
					" MB (instance %d)\n",
					name, (uint64_t)(4096ULL * size) >> 20,
					instance);
				free(old_ptr);
				size = 0;
				nomems++;
			} else {
				size_t i, n;
				uint8_t *u8ptr, *tmp;

				if (last_ptr == ptr) {
					tmp = u8ptr = last_ptr_end;
					n = (size_t)opt_bigheap_growth;
				} else {
					tmp = u8ptr = ptr;
					n = size;
				}

				if (page_size > 0) {
					size_t sz = page_size - 1;
					uintptr_t pg_ptr = ((uintptr_t)ptr + sz) & ~sz;
					size_t len = size - (pg_ptr - (uintptr_t)ptr);
					(void)mincore_touch_pages((void *)pg_ptr, len);
				}

				for (i = 0; i < n; i+= stride, u8ptr += stride) {
					if (!opt_do_run)
						goto abort;
					*u8ptr = (uint8_t)i;
				}

				if (opt_flags & OPT_FLAGS_VERIFY) {
					for (i = 0; i < n; i+= stride, tmp += stride) {
						if (!opt_do_run)
							goto abort;
						if (*tmp != (uint8_t)i)
							pr_fail(stderr, "%s: byte at location %p was 0x%" PRIx8
								" instead of 0x%" PRIx8 "\n",
								name, u8ptr, *tmp, (uint8_t)i);
					}
				}
				last_ptr = ptr;
				last_ptr_end = u8ptr;
			}
			(*counter)++;
		} while (opt_do_run && (!max_ops || *counter < max_ops));
abort:
		free(ptr);
	}
	if (ooms + segvs + nomems > 0)
		pr_dbg(stderr, "%s: OOM restarts: %" PRIu32
			", SEGV restarts: %" PRIu32
			", out of memory restarts: %" PRIu32 ".\n",
			name, ooms, segvs, nomems);

	return EXIT_SUCCESS;
}
