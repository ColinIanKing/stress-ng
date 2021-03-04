/*
 * Copyright (C) 2013-2021 Canonical, Ltd.
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

static const stress_help_t help[] = {
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
	MEMBARRIER_CMD_GLOBAL = (1 << 0),
	MEMBARRIER_CMD_SHARED = MEMBARRIER_CMD_GLOBAL,
	MEMBARRIER_CMD_GLOBAL_EXPEDITED = (1 << 1),
	MEMBARRIER_CMD_REGISTER_GLOBAL_EXPEDITED = (1 << 2),
	MEMBARRIER_CMD_PRIVATE_EXPEDITED = (1 << 3),
	MEMBARRIER_CMD_REGISTER_PRIVATE_EXPEDITED = (1 << 4),
	MEMBARRIER_CMD_PRIVATE_EXPEDITED_SYNC_CORE = (1 << 5),
	MEMBARRIER_CMD_REGISTER_PRIVATE_EXPEDITED_SYNC_CORE = (1 << 6),
};

#endif

static int stress_membarrier_exercise(const stress_args_t *args)
{
	int ret;
	unsigned int i, mask;

	ret = shim_membarrier(MEMBARRIER_CMD_QUERY, 0);
	if (ret < 0) {
		pr_fail("%s: membarrier CMD QUERY failed, errno=%d (%s)\n",
			args->name, errno, strerror(errno));
		return -1;
	}
	mask = (unsigned int)ret;
	for (i = 1; i; i <<= 1) {
		if (i & mask) {
			ret = shim_membarrier(i, 0);
			(void)ret;

			/* Exercise illegal flags */
			ret = shim_membarrier(i, ~0);
			(void)ret;
		}
	}

	/* Exercise illegal command */
	for (i = 1; i; i <<= 1) {
		if (!(i & mask)) {
			ret = shim_membarrier(i, 0);
			(void)ret;
			break;
		}
	}
	return 0;
}

static void *stress_membarrier_thread(void *parg)
{
	static void *nowt = NULL;
	const stress_args_t *args = ((stress_pthread_args_t *)parg)->args;

	/*
	 *  Block all signals, let controlling thread
	 *  handle these
	 */
	(void)sigprocmask(SIG_BLOCK, &set, NULL);

	while (keep_running && keep_stressing_flag()) {
		if (stress_membarrier_exercise(args) < 0)
			break;
	}

	return &nowt;
}

/*
 *  stress on membarrier()
 *	stress system by IO sync calls
 */
static int stress_membarrier(const stress_args_t *args)
{
	int ret;
	pthread_t pthreads[MAX_MEMBARRIER_THREADS];
	size_t i;
	int pthread_ret[MAX_MEMBARRIER_THREADS];
	stress_pthread_args_t pargs = { args, NULL, 0 };

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
		return EXIT_NOT_IMPLEMENTED;
	}

	(void)sigfillset(&set);
	(void)memset(pthread_ret, 0, sizeof(pthread_ret));
	keep_running = true;

	for (i = 0; i < MAX_MEMBARRIER_THREADS; i++) {
		pthread_ret[i] =
			pthread_create(&pthreads[i], NULL,
				stress_membarrier_thread, (void *)&pargs);
	}

	stress_set_proc_state(args->name, STRESS_STATE_RUN);

	do {
		if (stress_membarrier_exercise(args) < 0) {
			pr_err("%s: membarrier failed: errno=%d: (%s)\n",
				args->name, errno, strerror(errno));
		}
		inc_counter(args);
	} while (keep_stressing(args));

	keep_running = false;

	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);

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
