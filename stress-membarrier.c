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
	{ NULL,	"membarrier N",		"start N workers performing membarrier system calls" },
	{ NULL,	"membarrier-ops N",	"stop after N membarrier bogo operations" },
	{ NULL,	NULL,			NULL }
};

#if defined(HAVE_LIB_PTHREAD) && \
    defined(HAVE_MEMBARRIER)

#define MAX_MEMBARRIER_THREADS	(4)

static volatile bool keep_running;
static sigset_t set;

#if !defined(HAVE_LINUX_MEMBARRIER_H)
enum membarrier_cmd {
	MEMBARRIER_CMD_QUERY = 0,
	MEMBARRIER_CMD_SHARED = (1 << 0),
	MEMBARRIER_CMD_PRIVATE_EXPEDITED = (1 << 3),
	MEMBARRIER_CMD_REGISTER_PRIVATE_EXPEDITED = (1 << 4)
};
#endif

static void *stress_membarrier_thread(void *parg)
{
	static void *nowt = NULL;
	uint8_t stack[SIGSTKSZ + STACK_ALIGNMENT];
	const args_t *args = ((pthread_args_t *)parg)->args;

	/*
	 *  Block all signals, let controlling thread
	 *  handle these
	 */
	(void)sigprocmask(SIG_BLOCK, &set, NULL);

	/*
	 *  According to POSIX.1 a thread should have
	 *  a distinct alternative signal stack.
	 *  However, we block signals in this thread
	 *  so this is probably just totally unncessary.
	 */
	(void)memset(stack, 0, sizeof(stack));
	if (stress_sigaltstack(stack, SIGSTKSZ) < 0)
		return &nowt;

	while (keep_running && g_keep_stressing_flag) {
		int ret;

		ret = shim_membarrier(MEMBARRIER_CMD_QUERY, 0);
		if (ret < 0) {
			pr_fail_err("membarrier CMD QUERY");
			break;
		}
		/* CMD SHARED not available; skip it */
		if (!(ret & MEMBARRIER_CMD_SHARED))
			continue;
		if (shim_membarrier(MEMBARRIER_CMD_SHARED, 0) < 0) {
			pr_fail_err("membarrier CMD SHARED");
			break;
		}
	}

	return &nowt;
}

/*
 *  stress on membarrier()
 *	stress system by IO sync calls
 */
static int stress_membarrier(const args_t *args)
{
	int ret;
	pthread_t pthreads[MAX_MEMBARRIER_THREADS];
	size_t i;
	int pthread_ret[MAX_MEMBARRIER_THREADS];
	pthread_args_t pargs = { args, NULL };

	ret = shim_membarrier(MEMBARRIER_CMD_QUERY, 0);
	if (ret < 0) {
		if (errno == ENOSYS) {
			pr_inf("%s: stressor will be skipped, "
				"membarrier not supported\n",
				args->name);
			return EXIT_NOT_IMPLEMENTED;
		}
		pr_err("%s: membarrier failed: errno=%d: (%s)\n",
			args->name, errno, strerror(errno));
		return EXIT_FAILURE;
	}
	if (!(ret & MEMBARRIER_CMD_SHARED)) {
		pr_inf("%s: membarrier MEMBARRIER_CMD_SHARED "
			"not supported\n", args->name);
		return EXIT_FAILURE;
	}

	(void)sigfillset(&set);
	(void)memset(pthread_ret, 0, sizeof(pthread_ret));
	keep_running = true;

	for (i = 0; i < MAX_MEMBARRIER_THREADS; i++) {
		pthread_ret[i] =
			pthread_create(&pthreads[i], NULL,
				stress_membarrier_thread, (void *)&pargs);
	}

	do {
		ret = shim_membarrier(MEMBARRIER_CMD_SHARED, 0);
		if (ret < 0) {
			pr_err("%s: membarrier failed: errno=%d: (%s)\n",
				args->name, errno, strerror(errno));
		}
		inc_counter(args);
	} while (keep_stressing());

	keep_running = false;

	for (i = 0; i < MAX_MEMBARRIER_THREADS; i++) {
		if (pthread_ret[i] == 0)
			(void)pthread_join(pthreads[i], NULL);
	}
	return EXIT_SUCCESS;
}

stressor_info_t stress_membarrier_info = {
	.stressor = stress_membarrier,
	.class = CLASS_CPU_CACHE | CLASS_MEMORY,
	.help = help
};
#else
stressor_info_t stress_membarrier_info = {
	.stressor = stress_not_implemented,
	.class = CLASS_CPU_CACHE | CLASS_MEMORY,
	.help = help
};
#endif
