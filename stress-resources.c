// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2013-2021 Canonical, Ltd.
 * Copyright (C) 2022-2023 Colin Ian King.
 *
 */
#include "stress-ng.h"
#include "core-builtin.h"
#include "core-killpid.h"
#include "core-out-of-memory.h"
#include "core-resources.h"

#define MIN_MEM_FREE	(16 * MB)
#define MAX_PIDS 	(2048)
#define MAX_LOOPS	(2048)

static const stress_help_t help[] = {
	{ NULL,	"resources N",	   "start N workers consuming system resources" },
	{ NULL,	"resources-mlock", "attempt to mlock pages into memory" },
	{ NULL,	"resources-ops N", "stop after N resource bogo operations" },
	{ NULL,	NULL,		   NULL }
};

static int stress_set_resources_mlock(const char *opt)
{
	return stress_set_setting_true("resources-mlock", opt);
}

/*
 *  stress_resources()
 *	stress by forking and exiting
 */
static int stress_resources(const stress_args_t *args)
{
	const size_t pipe_size = stress_probe_max_pipe_size();
	size_t min_mem_free, shmall, freemem, totalmem, freeswap, totalswap;
	size_t num_pids = MAX_PIDS;
	stress_resources_t *resources;
	const size_t num_resources = MAX_LOOPS;
	pid_t *pids;
	bool resources_mlock = false;

	(void)stress_get_setting("resources-mlock", &resources_mlock);

	stress_get_memlimits(&shmall, &freemem, &totalmem, &freeswap, &totalswap);
	min_mem_free = (freemem / 100) * 2;
	if (min_mem_free < MIN_MEM_FREE)
		min_mem_free = MIN_MEM_FREE;

#if defined(MCL_FUTURE)
	if (resources_mlock)
		(void)shim_mlockall(MCL_FUTURE);
#else
	UNEXPECTED
#endif

	pids = malloc(num_pids * sizeof(*pids));
	if (!pids) {
		pr_inf_skip("%s: cannot allocate %zd process ids, skipping stressor\n",
			args->name, num_pids);
		return EXIT_NO_RESOURCE;
	}

	resources = malloc(num_resources * sizeof(*resources));
	if (!resources) {
		pr_inf_skip("%s: cannot allocate %zd resource structures, skipping stressor\n",
			args->name, num_resources);
		free(pids);
		return EXIT_NO_RESOURCE;
	}

	stress_set_proc_state(args->name, STRESS_STATE_RUN);

	do {
		unsigned int i;

		(void)shim_memset(pids, 0, sizeof(*pids));
		for (i = 0; i < num_pids; i++) {
			pid_t pid;

			stress_get_memlimits(&shmall, &freemem, &totalmem, &freeswap, &totalswap);
			if ((freemem > 0) && (freemem < min_mem_free))
				break;
			pid = fork();
			if (pid == 0) {
				size_t n;

				stress_set_oom_adjustment(args, true);
				VOID_RET(int, stress_drop_capabilities(args->name));
				(void)sched_settings_apply(true);

				n = stress_resources_allocate(args, resources, num_resources, pipe_size, min_mem_free, true);
				(void)stress_resources_free(args, resources, n);

				_exit(0);
			}

			pids[i] = pid;
			if (!stress_continue(args))
				break;
			stress_bogo_inc(args);
		}
		stress_kill_and_wait_many(args, pids, num_pids, SIGALRM, true);
	} while (stress_continue(args));

	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);
	free(resources);
	free(pids);

	return EXIT_SUCCESS;
}

static const stress_opt_set_func_t opt_set_funcs[] = {
	{ OPT_resources_mlock,	stress_set_resources_mlock },
	{ 0,			NULL },
};

stressor_info_t stress_resources_info = {
	.stressor = stress_resources,
	.class = CLASS_MEMORY | CLASS_OS,
	.opt_set_funcs = opt_set_funcs,
	.help = help
};
