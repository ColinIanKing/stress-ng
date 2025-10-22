/*
 * Copyright (C) 2013-2021 Canonical, Ltd.
 * Copyright (C) 2022-2025 Colin Ian King.
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
#include "core-builtin.h"
#include "core-capabilities.h"
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

/*
 *  stress_resources()
 *	stress by forking and exiting
 */
static int stress_resources(stress_args_t *args)
{
	const size_t pipe_size = stress_probe_max_pipe_size();
	size_t min_mem_free, shmall, freemem, totalmem, freeswap, totalswap;
	size_t num_pids = MAX_PIDS;
	stress_resources_t *resources;
	const size_t num_resources = MAX_LOOPS;
	stress_pid_t *s_pids;
	bool resources_mlock = false;

	if (!stress_get_setting("resources-mlock", &resources_mlock)) {
		if (g_opt_flags & OPT_FLAGS_AGGRESSIVE)
			resources_mlock = true;
	}

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

	s_pids = stress_sync_s_pids_mmap(num_pids);
	if (s_pids == MAP_FAILED) {
		pr_inf_skip("%s: failed to mmap %zu PIDs%s, skipping stressor\n",
			args->name, num_pids, stress_get_memfree_str());
		return EXIT_NO_RESOURCE;
	}

	resources = (stress_resources_t *)malloc(num_resources * sizeof(*resources));
	if (!resources) {
		pr_inf_skip("%s: cannot allocate %zu resource structures%s, skipping stressor\n",
			args->name, num_resources, stress_get_memfree_str());
		(void)stress_sync_s_pids_munmap(s_pids, num_pids);
		return EXIT_NO_RESOURCE;
	}

	stress_set_proc_state(args->name, STRESS_STATE_SYNC_WAIT);
	stress_sync_start_wait(args);
	stress_set_proc_state(args->name, STRESS_STATE_RUN);

	do {
		unsigned int i;

		(void)shim_memset(s_pids, 0, sizeof(*s_pids));
		for (i = 0; i < num_pids; i++) {
			pid_t pid;

			stress_get_memlimits(&shmall, &freemem, &totalmem, &freeswap, &totalswap);
			if ((freemem > 0) && (freemem < min_mem_free))
				break;
			pid = fork();
			if (pid == 0) {
				size_t n;

				stress_set_proc_state(args->name, STRESS_STATE_RUN);
				stress_set_oom_adjustment(args, true);
				VOID_RET(int, stress_drop_capabilities(args->name));
				(void)sched_settings_apply(true);

				n = stress_resources_allocate(args, resources, num_resources, pipe_size, min_mem_free, true);
				stress_resources_access(args, resources, n);
				stress_resources_free(args, resources, n);

				_exit(0);
			}

			s_pids[i].pid = pid;
			if (UNLIKELY(!stress_continue(args)))
				break;
			stress_bogo_inc(args);
		}
		stress_kill_and_wait_many(args, s_pids, num_pids, SIGALRM, true);
	} while (stress_continue(args));

	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);
	free(resources);
	(void)stress_sync_s_pids_munmap(s_pids, num_pids);

	return EXIT_SUCCESS;
}


static const stress_opt_t opts[] = {
	{ OPT_resources_mlock, "resources-mlock", TYPE_ID_BOOL, 0, 1, NULL },
	END_OPT,
};

const stressor_info_t stress_resources_info = {
	.stressor = stress_resources,
	.classifier = CLASS_MEMORY | CLASS_OS,
	.opts = opts,
	.help = help
};
