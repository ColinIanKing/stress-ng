/*
 * Copyright (C) 2013-2015 Canonical, Ltd.
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

static size_t opt_malloc_bytes = DEFAULT_MALLOC_BYTES;
static size_t opt_malloc_max = DEFAULT_MALLOC_MAX;

void stress_set_malloc_bytes(const char *optarg)
{
	opt_malloc_bytes = (size_t)get_uint64_byte(optarg);
	check_range("malloc-bytes", opt_malloc_bytes,
	MIN_MALLOC_BYTES, MAX_MALLOC_BYTES);
}

void stress_set_malloc_max(const char *optarg)
{
	opt_malloc_max = (size_t)get_uint64_byte(optarg);
	check_range("malloc-max", opt_malloc_max,
	MIN_MALLOC_MAX, MAX_MALLOC_MAX);
}

/*
 *  stress_alloc_size()
 *	get a new allocation size, ensuring
 *	it is never zero bytes.
 */
static inline size_t stress_alloc_size(void)
{
	size_t len = mwc() % opt_malloc_bytes;

	return len ? len : 1;
}

/*
 *  stress_malloc()
 *	stress malloc by performing a mix of
 *	allocation and frees
 */
int stress_malloc(
	uint64_t *const counter,
	const uint32_t instance,
	const uint64_t max_ops,
	const char *name)
{
	pid_t pid;
	uint32_t restarts = 0, nomems = 0;

again:
	pid = fork();
	if (pid < 0) {
		pr_err(stderr, "%s: fork failed: errno=%d: (%s)\n",
			name, errno, strerror(errno));
	} else if (pid > 0) {
		int status, ret;
		/* Parent, wait for child */
		ret = waitpid(pid, &status, 0);
		if (ret < 0) {
			if (errno != EINTR)
				pr_dbg(stderr, "%s: waitpid(): errno=%d (%s)\n",
					name, errno, strerror(errno));
			(void)kill(pid, SIGTERM);
			(void)kill(pid, SIGKILL);
			waitpid(pid, &status, 0);
		} else if (WIFSIGNALED(status)) {
			pr_dbg(stderr, "%s: child died: %d (instance %d)\n",
				name, WTERMSIG(status), instance);
			/* If we got killed by OOM killer, re-start */
			if (WTERMSIG(status) == SIGKILL) {
				pr_dbg(stderr, "%s: assuming killed by OOM killer, "
					"restarting again (instance %d)\n",
					name, instance);
				restarts++;
				goto again;
			}
		}
	} else if (pid == 0) {
		void *addr[opt_malloc_max];
		size_t j;

		memset(addr, 0, sizeof(addr));

		/* Make sure this is killable by OOM killer */
		set_oom_adjustment(name, true);

		do {
			unsigned int rnd = mwc();
			unsigned int i = rnd % opt_malloc_max;
			unsigned int action = (rnd >> 12) & 1;
			unsigned int do_calloc = (rnd >> 14) & 0x1f;

			/*
			 * With many instances running it is wise to
			 * double check before the next allocation as
			 * sometimes process start up is delayed for
			 * some time and we should bail out before
			 * exerting any more memory pressure
			 */
			if (!opt_do_run)
				goto abort;

			if (addr[i]) {
				/* 50% free, 50% realloc */
				if (action) {
					free(addr[i]);
					addr[i] = NULL;
					(*counter)++;
				} else {
					void *tmp;
					size_t len = stress_alloc_size();

					tmp = realloc(addr[i], len);
					if (tmp) {
						addr[i] = tmp;
						(void)mincore_touch_pages(addr[i], len);
						(*counter)++;
					}
					
				}
			} else {
				/* 50% free, 50% alloc */
				if (action) {
					size_t len = stress_alloc_size();

					if (do_calloc == 0) {
						size_t n = ((rnd >> 15) % 17) + 1;
						addr[i] = calloc(len / n, len * n);
					} else {
						addr[i] = malloc(len);
					}
					if (addr[i]) {
						(*counter)++;
						(void)mincore_touch_pages(addr[i], len);
					}
				}
			}
		} while (opt_do_run && (!max_ops || *counter < max_ops));
abort:
		for (j = 0; j < opt_malloc_max; j++) {
			free(addr[j]);
		}
	}
	if (restarts + nomems > 0)
		pr_dbg(stderr, "%s: OOM restarts: %" PRIu32 ", out of memory restarts: %" PRIu32 ".\n",
			name, restarts, nomems);

	return EXIT_SUCCESS;
}
