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

#if defined(STRESS_MEMBARRIER)

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <pthread.h>
#include <signal.h>

#define MAX_MEMBARRIER_THREADS	(4)

static volatile bool keep_running;
static sigset_t set;

enum membarrier_cmd {
	MEMBARRIER_CMD_QUERY = 0,
	MEMBARRIER_CMD_SHARED = (1 << 0),
};

static int sys_membarrier(int cmd, int flags)
{
#if defined(__NR_membarrier)
	return syscall(__NR_membarrier, cmd, flags);
#else
	errno = ENOSYS;
	return -1;
#endif
}

static void *stress_membarrier_thread(void *ctxt)
{
	static void *nowt = NULL;
	uint8_t stack[SIGSTKSZ];
	stack_t ss;
	char *name = (char *)ctxt;

	(void)ctxt;

	/*
	 *  Block all signals, let controlling thread
	 *  handle these
	 */
	sigprocmask(SIG_BLOCK, &set, NULL);

	/*
	 *  According to POSIX.1 a thread should have
	 *  a distinct alternative signal stack.
	 *  However, we block signals in this thread
	 *  so this is probably just totally unncessary.
	 */
	ss.ss_sp = (void *)stack;
	ss.ss_size = SIGSTKSZ;
	ss.ss_flags = 0;
	if (sigaltstack(&ss, NULL) < 0) {
		pr_fail_err("pthread", "sigaltstack");
		return &nowt;
	}
	while (keep_running && opt_do_run) {
		if (sys_membarrier(MEMBARRIER_CMD_SHARED, 0) < 0) {
			pr_err(stderr, "%s: membarrier failed: errno=%d: (%s)\n",
				name, errno, strerror(errno));
			break;
		}
	}

	return &nowt;
}

/*
 *  stress on membarrier()
 *	stress system by IO sync calls
 */
int stress_membarrier(
	uint64_t *const counter,
	const uint32_t instance,
	const uint64_t max_ops,
	const char *name)
{
	int ret;
	pthread_t pthreads[MAX_MEMBARRIER_THREADS];
	size_t i;
	int pthread_ret[MAX_MEMBARRIER_THREADS];

	(void)instance;

	ret = sys_membarrier(MEMBARRIER_CMD_QUERY, 0);
	if (ret < 0) {
		pr_err(stderr, "%s: membarrier failed: errno=%d: (%s)\n",
			name, errno, strerror(errno));
		return EXIT_FAILURE;
	}
	if (!(ret & MEMBARRIER_CMD_SHARED)) {
		pr_inf(stderr, "%s: membarrier MEMBARRIER_CMD_SHARED "
			"not supported\n", name);
		return EXIT_FAILURE;
	}

	sigfillset(&set);
	memset(pthread_ret, 0, sizeof(ret));
	keep_running = true;

	for (i = 0; i < MAX_MEMBARRIER_THREADS; i++) {
		pthread_ret[i] =
			pthread_create(&pthreads[i], NULL,
				stress_membarrier_thread, (void *)name);
	}

	do {
		ret = sys_membarrier(MEMBARRIER_CMD_SHARED, 0);
		if (ret < 0) {
			pr_err(stderr, "%s: membarrier failed: errno=%d: (%s)\n",
				name, errno, strerror(errno));
		}
		(*counter)++;
	} while (opt_do_run && (!max_ops || *counter < max_ops));

	keep_running = false;

	for (i = 0; i < MAX_MEMBARRIER_THREADS; i++) {
		if (pthread_ret[i] == 0)
			pthread_join(pthreads[i], NULL);
	}
	return EXIT_SUCCESS;
}

#endif
