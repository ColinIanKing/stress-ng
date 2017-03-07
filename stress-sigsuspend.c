/*
 * Copyright (C) 2013-2017 Canonical, Ltd.
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

#define CACHE_STRIDE_SHIFT	(6)

/*
 *  stress_sigsuspend
 *	stress sigsuspend
 */
int stress_sigsuspend(const args_t *args)
{
	pid_t pid[MAX_SIGSUSPEND_PIDS];
	size_t n, i;
	sigset_t mask;
	int status;
	uint64_t *counters, c;
	volatile uint64_t *v_counters;
	const size_t counters_size =
		(sizeof(uint64_t) * MAX_SIGSUSPEND_PIDS) << CACHE_STRIDE_SHIFT;

	v_counters = counters = (uint64_t *)mmap(NULL, counters_size,
			PROT_READ | PROT_WRITE,
			MAP_SHARED | MAP_ANONYMOUS, -1, 0);
	if (counters == MAP_FAILED) {
		pr_fail_dbg("mmap");
		return EXIT_FAILURE;
	}
	memset(counters, 0, counters_size);

	sigfillset(&mask);
	sigdelset(&mask, SIGUSR1);

	for (n = 0; n < MAX_SIGSUSPEND_PIDS; n++) {
again:
		pid[n] = fork();
		if (pid[n] < 0) {
			if (g_keep_stressing_flag && (errno == EAGAIN))
				goto again;
			pr_fail_dbg("fork");
			goto reap;
		} else if (pid[n] == 0) {
			(void)setpgid(0, g_pgrp);
			stress_parent_died_alarm();

			while (g_keep_stressing_flag) {
				sigsuspend(&mask);
				v_counters[n << CACHE_STRIDE_SHIFT]++;
			}
			_exit(0);
		}
		(void)setpgid(pid[n], g_pgrp);
	}

	/* Parent */
	do {
		c = 0;
		for (i = 0; i < n; i++) {
			c += v_counters[i << CACHE_STRIDE_SHIFT];
			(void)kill(pid[i], SIGUSR1);
		}
	} while (keep_stressing());

	*args->counter = c;

reap:
	for (i = 0; i < n; i++) {
		/* terminate child */
		(void)kill(pid[i], SIGKILL);
		(void)waitpid(pid[i], &status, 0);
	}
	(void)munmap((void *)counters, counters_size);

	return EXIT_SUCCESS;
}
