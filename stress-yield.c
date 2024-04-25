/*
 * Copyright (C) 2013-2021 Canonical, Ltd.
 * Copyright (C) 2022-2024 Colin Ian King.
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
#include "core-killpid.h"

#include <sched.h>

#define MIN_YIELD_PROCS	(1)
#define MAX_YIELD_PROCS	(65536)

static const stress_help_t help[] = {
	{ "y N", "yield N",	  "start N workers doing sched_yield() calls" },
	{ NULL,	 "yield-ops N",	  "stop after N bogo yield operations" },
	{ NULL,	 "yield-procs N", "specify number of yield processes per stressor" },
	{ NULL,	 NULL,		  NULL }
};

/*
 *  stress_set_yield_procs()
 *	set maximum number of processes allowed
 */
static int stress_set_yield_procs(const char *opt)
{
	uint32_t yield_procs;

	yield_procs = stress_get_uint32(opt);
	stress_check_range("yield-procs", (uint64_t)yield_procs,
		MIN_YIELD_PROCS, MAX_YIELD_PROCS);
	return stress_set_setting("yield-procs", TYPE_ID_UINT32, &yield_procs);
}

static const stress_opt_set_func_t opt_set_funcs[] = {
	{ OPT_yield_procs,	stress_set_yield_procs },
	{ 0,			NULL }
};

#if defined(_POSIX_PRIORITY_SCHEDULING) &&	\
    !defined(__minix__)

/*
 *  stress on sched_yield()
 *	stress system by sched_yield
 */
static int stress_yield(stress_args_t *args)
{
	stress_metrics_t *metrics;
	size_t metrics_size;
	uint64_t max_ops_per_yielder;
	int32_t cpus = stress_get_processors_configured();
	const uint32_t instances = args->num_instances;
	uint32_t yielders = 2, yield_procs = 0;
	double count, duration, ns;
#if defined(HAVE_SCHED_GETAFFINITY)
	cpu_set_t mask;
#endif
	pid_t *pids;
	size_t i;

	(void)stress_get_setting("yield-procs", &yield_procs);

#if defined(HAVE_SCHED_GETAFFINITY)
	/*
	 *  If the process is limited to a subset of cores
	 *  then make sure we do not create too many yielders
	 */
	if (sched_getaffinity(0, sizeof(mask), &mask) < 0) {
		pr_dbg("%s: can't get sched affinity, defaulting to %"
			PRId32 " yielder%s (instance %" PRIu32 ")\n",
			args->name, cpus, (cpus == 1) ? "" : "s", args->instance);
	} else {
		if (CPU_COUNT(&mask) < (int)cpus)
			cpus = (int32_t)CPU_COUNT(&mask);
#if 0
		pr_dbg("%s: limiting to %" PRId32 " child yielder%s (instance %"
			PRIu32 ")\n", args->name, cpus, (cpus == 1) ? "" : "s", args->instance);
#endif
	}
#else
	UNEXPECTED
#endif

	if (yield_procs == 0) {
		/*
		 *  Ensure we always have at least 2 yielders per
		 *  CPU available to force context switching on yields
		 */
		if (cpus > 0) {
			cpus *= 2;
			yielders = cpus / instances;
			if (yielders < 1)
				yielders = 1;
			if (!args->instance) {
				/* residual may be -ve, ensure it is signed */
				int32_t residual = cpus - (int32_t)(yielders * instances);
	
				if (residual > 0)
					yielders += residual;
			}
		}
	} else {
		yielders = yield_procs;
	}
	max_ops_per_yielder = args->max_ops / yielders;

	pids = calloc(yielders, sizeof(*pids));
	if (!pids) {
		pr_inf_skip("%s: calloc failed allocating %" PRIu32
			" pids, skipping stressor\n", args->name, yielders);
		return EXIT_NO_RESOURCE;
	}

	metrics_size = yielders * sizeof(*metrics);
	metrics = (stress_metrics_t *)
		stress_mmap_populate(NULL, metrics_size,
			PROT_READ | PROT_WRITE,
			MAP_SHARED | MAP_ANONYMOUS, -1, 0);
	if (metrics == MAP_FAILED) {
		pr_err("%s: mmap failed, count not allocate %zd bytes, errno=%d (%s)\n",
			args->name, metrics_size, errno, strerror(errno));
		free(pids);
		return EXIT_NO_RESOURCE;
	}
	for (i = 0; i < yielders; i++) {
		metrics[i].count = 0.0;
		metrics[i].duration = 0.0;
	}

	stress_set_proc_state(args->name, STRESS_STATE_RUN);

	for (i = 0; stress_continue_flag() && (i < yielders); i++) {
		pids[i] = fork();
		if (pids[i] < 0) {
			pr_dbg("%s: fork failed (instance %" PRIu32
				", yielder %zd): errno=%d (%s)\n",
				args->name, args->instance, i, errno, strerror(errno));
		} else if (pids[i] == 0) {
			stress_parent_died_alarm();
			(void)sched_settings_apply(true);

			do {
				int ret;
				double t = stress_time_now();

				ret = shim_sched_yield();
				if (ret == 0) {
					metrics[i].count += 1.0;
					metrics[i].duration += (stress_time_now() - t);
				} else if ((ret < 0) && (g_opt_flags & OPT_FLAGS_VERIFY)) {
					pr_fail("%s: sched_yield failed, errno=%d (%s)\n",
						args->name, errno, strerror(errno));
				}
			} while (stress_continue_flag() && (!max_ops_per_yielder || (metrics[i].count < max_ops_per_yielder)));
			_exit(EXIT_SUCCESS);
		}
	}

	do {
#if defined(__FreeBSD__)
		VOID_RET(int, shim_sched_yield());
		stress_bogo_inc(args);
#else
		VOID_RET(int, shim_usleep(100000));
#endif
	} while (stress_continue(args));

	/* Parent, wait for children */

	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);
	for (duration = 0.0, count = 0.0, i = 0; i < yielders; i++) {
		if (pids[i] > 0) {
			(void)stress_kill_pid_wait(pids[i], NULL);
			duration += metrics[i].duration;
			count += metrics[i].count;
		}
	}
	stress_bogo_add(args, (uint64_t)count);

	ns = count > 0.0 ? (STRESS_DBL_NANOSECOND * duration) / count : 0.0;
	stress_metrics_set(args, 0, "ns duration per sched_yield call",
		ns, STRESS_HARMONIC_MEAN);

	(void)munmap((void *)metrics, metrics_size);
	free(pids);

	return EXIT_SUCCESS;
}

stressor_info_t stress_yield_info = {
	.stressor = stress_yield,
	.class = CLASS_SCHEDULER | CLASS_OS,
	.opt_set_funcs = opt_set_funcs,
	.verify = VERIFY_OPTIONAL,
	.help = help
};
#else
stressor_info_t stress_yield_info = {
	.stressor = stress_unimplemented,
	.class = CLASS_SCHEDULER | CLASS_OS,
	.opt_set_funcs = opt_set_funcs,
	.verify = VERIFY_OPTIONAL,
	.help = help,
	.unimplemented_reason = "built without scheduling support"
};
#endif
