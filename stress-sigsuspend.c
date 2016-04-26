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

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>
#include <pthread.h>

#define CACHE_STRIDE_SHIFT	(6)

/*
 *  stress_sigsuspend
 *	stress sigsuspend
 */
int stress_sigsuspend(
	uint64_t *const counter,
	const uint32_t instance,
	const uint64_t max_ops,
	const char *name)
{
	pid_t pid[MAX_SIGSUSPEND_PIDS];
	size_t n, i;
	sigset_t mask;
	int status;
	uint64_t *counters, c;
	volatile uint64_t *v_counters;
	const size_t counters_size =
		(sizeof(uint64_t) * MAX_SIGSUSPEND_PIDS) << CACHE_STRIDE_SHIFT;

	(void)instance;

	v_counters = counters = (uint64_t *)mmap(NULL, counters_size,
			PROT_READ | PROT_WRITE,
			MAP_SHARED | MAP_ANONYMOUS, -1, 0);
	if (counters == MAP_FAILED) {
		pr_fail_dbg(name, "mmap");
		return EXIT_FAILURE;
	}
	memset(counters, 0, counters_size);

	sigfillset(&mask);
	sigdelset(&mask, SIGUSR1);

	for (n = 0; n < MAX_SIGSUSPEND_PIDS; n++) {
again:
		pid[n] = fork();
		if (pid[n] < 0) {
			if (opt_do_run && (errno == EAGAIN))
				goto again;
			pr_fail_dbg(name, "fork");
			goto reap;
		} else if (pid[n] == 0) {
			setpgid(0, pgrp);
			stress_parent_died_alarm();

			while (opt_do_run) {
				sigsuspend(&mask);
				v_counters[n << CACHE_STRIDE_SHIFT]++;
			}
			_exit(0);
		}
		setpgid(pid[n], pgrp);
	}

	/* Parent */
	do {
		c = 0;
		for (i = 0; i < n; i++) {
			c += v_counters[i << CACHE_STRIDE_SHIFT];
			kill(pid[i], SIGUSR1);
		}
	} while (opt_do_run && (!max_ops || c < max_ops));

	*counter = c;

reap:
	for (i = 0; i < n; i++) {
		/* terminate child */
		(void)kill(pid[i], SIGKILL);
		(void)waitpid(pid[i], &status, 0);
	}
	(void)munmap((void *)counters, counters_size);

	return EXIT_SUCCESS;
}
