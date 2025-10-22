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
#include "core-killpid.h"
#include "core-mmap.h"
#include "core-sched.h"

#include <sched.h>

#define MIN_YIELD_PROCS	(1)
#define MAX_YIELD_PROCS	(65536)

static const stress_help_t help[] = {
	{ "y N", "yield N",	  "start N workers doing sched_yield() calls" },
	{ NULL,	 "yield-ops N",	  "stop after N bogo yield operations" },
	{ NULL,	 "yield-procs N", "specify number of yield processes per stressor" },
	{ NULL,  "yield-sched P", "select scheduler policy [idle, fifo, rr, other, batch, deadline]" },
	{ NULL,	 NULL,		  NULL }
};

static const char *stress_yield_sched(const size_t i)
{
	return (i < stress_sched_types_length) ? stress_sched_types[i].sched_name : NULL;
}

static const stress_opt_t opts[] = {
	{ OPT_yield_procs, "yield-procs", TYPE_ID_UINT32, MIN_YIELD_PROCS, MAX_YIELD_PROCS, NULL },
	{ OPT_yield_sched, "yield-sched", TYPE_ID_SIZE_T_METHOD, 0, 0, stress_yield_sched },
	END_OPT,
};

#if defined(HAVE_SCHED_SETAFFINITY) &&		\
    (defined(_POSIX_PRIORITY_SCHEDULING) || 	\
     defined(__linux__)) &&			\
    (defined(SCHED_BATCH) ||			\
     defined(SCHED_DEADLINE) ||			\
     defined(SCHED_EXT) ||			\
     defined(SCHED_IDLE) ||			\
     defined(SCHED_FIFO) ||			\
     defined(SCHED_OTHER) ||			\
     defined(SCHED_RR)) &&			\
    !defined(__OpenBSD__) &&			\
    !defined(__minix__) &&			\
    !defined(__APPLE__)
/*
 *  stress_yield_sched_policy()
 *	attempt to apply a scheduling policy, ignore if yield_sched out of bounds
 *	or if policy cannot be applied (e.g. not enough privilege).
 */
static void stress_yield_sched_policy(stress_args_t *args, const size_t yield_sched)
{
	struct sched_param param;
	int ret = 0;
	int max_prio, min_prio, rng_prio, policy;
	const char *policy_name;

	if (UNLIKELY(yield_sched >= stress_sched_types_length))
		return;

	policy = stress_sched_types[yield_sched].sched;
	policy_name = stress_sched_types[yield_sched].sched_name;

	errno = 0;
	switch (policy) {
#if defined(SCHED_DEADLINE) &&		\
    defined(HAVE_SCHED_GETATTR) &&	\
    defined(HAVE_SCHED_SETATTR)
	case SCHED_DEADLINE:
		/*
		 *  Only have 1 RT deadline instance running
		 */
		if (stress_instance_zero(args)) {
			struct shim_sched_attr attr;

			(void)shim_memset(&attr, 0, sizeof(attr));
			attr.size = sizeof(attr);
			attr.sched_flags = 0;
			attr.sched_nice = 0;
			attr.sched_priority = 0;
			attr.sched_policy = SCHED_DEADLINE;
			/* runtime <= deadline <= period */
			attr.sched_runtime = 40 * 100000;
			attr.sched_deadline = 80 * 100000;
			attr.sched_period = 160 * 100000;

			ret = shim_sched_setattr(0, &attr, 0);
			break;
		}
		param.sched_priority = 0;
		ret = sched_setscheduler(0, policy, &param);
		break;
#endif
#if defined(SCHED_BATCH)
	case SCHED_BATCH:
#endif
#if defined(SCHED_EXT)
	case SCHED_EXT:
#endif
#if defined(SCHED_IDLE)
	case SCHED_IDLE:
#endif
#if defined(SCHED_OTHER)
	case SCHED_OTHER:
#endif
		param.sched_priority = 0;
		ret = sched_setscheduler(0, policy, &param);
		break;
#if defined(SCHED_RR)
	case SCHED_RR:
#if defined(HAVE_SCHED_RR_GET_INTERVAL)
		{
			struct timespec t;

			VOID_RET(int, sched_rr_get_interval(0, &t));
		}
#endif
		goto case_sched_fifo;
#endif
#if defined(SCHED_FIFO)
	case SCHED_FIFO:
#endif
case_sched_fifo:
		min_prio = sched_get_priority_min(policy);
		max_prio = sched_get_priority_max(policy);

		/* Check if min/max is supported or not */
		if (UNLIKELY((min_prio == -1) || (max_prio == -1)))
			return;

		rng_prio = max_prio - min_prio;
		if (UNLIKELY(rng_prio == 0)) {
			pr_dbg("%s: invalid min/max priority "
				"range for scheduling policy %s "
				"(min=%d, max=%d)\n",
				args->name,
				policy_name,
				min_prio, max_prio);
			break;
		}
		param.sched_priority = (int)stress_mwc32modn(rng_prio) + min_prio;
		ret = sched_setscheduler(0, policy, &param);
		break;
	default:
		/* Should never get here */
		break;
	}
	if (UNLIKELY(ret < 0)) {
		/*
		 *  Some systems return EINVAL for non-POSIX
		 *  scheduling policies, silently ignore these
		 *  failures.
		 */
		if ((errno != EINVAL) &&
		    (errno != EINTR) &&
		    (errno != ENOSYS) &&
		    (errno != EBUSY)) {
			pr_dbg("%s: sched_setscheduler "
				"failed, errno=%d (%s) "
				"for scheduler policy %s\n",
				args->name, errno, strerror(errno),
				policy_name);
		}
	}
}

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
	const uint32_t instances = args->instances;
	uint32_t yielders = 2, yield_procs = 0;
	double count, duration, ns;
#if defined(HAVE_SCHED_GETAFFINITY)
	cpu_set_t mask;
#endif
	pid_t *pids;
	size_t i, yield_sched = SIZE_MAX;

	if (!stress_get_setting("yield-procs", &yield_procs)) {
		if (g_opt_flags & OPT_FLAGS_MAXIMIZE)
			yield_procs = MAX_YIELD_PROCS;
		if (g_opt_flags & OPT_FLAGS_MINIMIZE)
			yield_procs = MIN_YIELD_PROCS;
	}
	(void)stress_get_setting("yield-sched", &yield_sched);

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
				const int32_t residual = cpus - (int32_t)(yielders * instances);

				if (residual > 0)
					yielders += residual;
			}
		}
	} else {
		yielders = yield_procs;
	}
	max_ops_per_yielder = args->bogo.max_ops / yielders;

	pids = (pid_t *)calloc(yielders, sizeof(*pids));
	if (!pids) {
		pr_inf_skip("%s: failed to allocate %" PRIu32
			" pids%s, skipping stressor\n",
			args->name, yielders, stress_get_memfree_str());
		return EXIT_NO_RESOURCE;
	}

	metrics_size = yielders * sizeof(*metrics);
	metrics = (stress_metrics_t *)
		stress_mmap_populate(NULL, metrics_size,
			PROT_READ | PROT_WRITE,
			MAP_SHARED | MAP_ANONYMOUS, -1, 0);
	if (metrics == MAP_FAILED) {
		pr_err("%s: failed to mmap %zu bytes%s, errno=%d (%s)\n",
			args->name, metrics_size,
			stress_get_memfree_str(), errno, strerror(errno));
		free(pids);
		return EXIT_NO_RESOURCE;
	}
	stress_set_vma_anon_name(metrics, metrics_size, "metrics");
	stress_zero_metrics(metrics, yielders);

	stress_set_proc_state(args->name, STRESS_STATE_SYNC_WAIT);
	stress_sync_start_wait(args);
	stress_set_proc_state(args->name, STRESS_STATE_RUN);

	for (i = 0; LIKELY(stress_continue_flag() && (i < yielders)); i++) {
		pids[i] = fork();
		if (pids[i] < 0) {
			pr_dbg("%s: fork failed (instance %" PRIu32
				", yielder %zd), errno=%d (%s)\n",
				args->name, args->instance, i, errno, strerror(errno));
		} else if (pids[i] == 0) {
			stress_set_proc_state(args->name, STRESS_STATE_RUN);
			stress_parent_died_alarm();
			(void)sched_settings_apply(true);
			stress_yield_sched_policy(args, yield_sched);

			do {
				int ret;
				double t = stress_time_now();

				ret = shim_sched_yield();
				if (LIKELY(ret == 0)) {
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
		(void)shim_sched_yield();
		stress_bogo_inc(args);
#else
		(void)shim_usleep(100000);
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
		ns, STRESS_METRIC_HARMONIC_MEAN);

	(void)munmap((void *)metrics, metrics_size);
	free(pids);

	return EXIT_SUCCESS;
}

const stressor_info_t stress_yield_info = {
	.stressor = stress_yield,
	.classifier = CLASS_SCHEDULER | CLASS_OS,
	.opts = opts,
	.verify = VERIFY_OPTIONAL,
	.help = help
};
#else
const stressor_info_t stress_yield_info = {
	.stressor = stress_unimplemented,
	.classifier = CLASS_SCHEDULER | CLASS_OS,
	.opts = opts,
	.verify = VERIFY_OPTIONAL,
	.help = help,
	.unimplemented_reason = "built without scheduling support"
};
#endif
