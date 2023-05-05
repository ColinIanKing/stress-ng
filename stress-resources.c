/*
 * Copyright (C) 2013-2021 Canonical, Ltd.
 * Copyright (C) 2022-2023 Colin Ian King.
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
 */
#include "stress-ng.h"
#include "core-resources.h"

#define MIN_MEM_FREE	(16 * MB)
#define MAX_PIDS 	(2048)
#define MAX_LOOPS	(2048)

static const stress_help_t help[] = {
	{ NULL,	"resources N",	   "start N workers consuming system resources" },
	{ NULL,	"resources-ops N", "stop after N resource bogo operations" },
	{ NULL,	NULL,		   NULL }
};

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

	stress_get_memlimits(&shmall, &freemem, &totalmem, &freeswap, &totalswap);
	min_mem_free = (freemem / 100) * 2;
	if (min_mem_free < MIN_MEM_FREE)
		min_mem_free = MIN_MEM_FREE;

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

		(void)memset(pids, 0, sizeof(*pids));
		for (i = 0; i < num_pids; i++) {
			pid_t pid;

			stress_get_memlimits(&shmall, &freemem, &totalmem, &freeswap, &totalswap);
			if ((freemem > 0) && (freemem < min_mem_free))
				break;
			pid = fork();
			if (pid == 0) {
				size_t n;

				stress_set_oom_adjustment(args->name, true);
				VOID_RET(int, stress_drop_capabilities(args->name));
				(void)sched_settings_apply(true);

				n = stress_resources_allocate(args, resources, num_resources, pipe_size, min_mem_free, true);
				(void)stress_resources_free(args, resources, n);

				_exit(0);
			}

			pids[i] = pid;
			if (!keep_stressing(args))
				break;
			inc_counter(args);
		}
		/* Signal all pids, fast turnaround */
		for (i = 0; i < num_pids; i++) {
			if (pids[i] > 1)
				(void)kill(pids[i], SIGALRM);
		}
		/* Re-signal, slow reap */
		for (i = 0; i < num_pids; i++) {
			if (pids[i] > 1)
				stress_kill_and_wait(args, pids[i], SIGALRM, true);
		}
	} while (keep_stressing(args));

	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);
	free(resources);
	free(pids);

	return EXIT_SUCCESS;
}

stressor_info_t stress_resources_info = {
	.stressor = stress_resources,
	.class = CLASS_MEMORY | CLASS_OS,
	.help = help
};
