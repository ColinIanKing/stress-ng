/*
 * Copyright (C) 2024-2025 Colin Ian King.
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
#include "core-cpuidle.h"
#include "core-mmap.h"

#include <time.h>

#define NANOSLEEP_MAX_SHIFT	(20)
#define NANOSLEEP_MAX_NS	((1U << NANOSLEEP_MAX_SHIFT) - 1)
#define NANOSLEEP_LOOPS		(16)
#define NANOSLEEP_DELAYS_MAX	(NANOSLEEP_MAX_SHIFT + 2)
#define NANOSLEEP_MAX		(0xffffffff)

/*
 *  per nsec nanosleep delay stats
 */
typedef struct {
	uint32_t nsec;		/* desired nanosleep time */
	uint32_t min_nsec;	/* minimum nanosleep time measured */
	uint32_t max_nsec;	/* maximum nanosleep time measured */
	uint32_t count;		/* count of measurements */
	uint64_t sum_nsec;	/* sum of measurements */
	double mean;		/* mean of measurements */
	bool updated;		/* true of data has been updated */
} nanosleep_delay_t;

/*
 *  per nanosleep instance measurements
 */
typedef struct {
	nanosleep_delay_t delay[NANOSLEEP_DELAYS_MAX];
	pid_t pid;		/* pid of stressor instance */
	bool started;		/* true if measuring started in instance */
	bool finished;		/* true if measuring finished in instance */
} nanosleep_delays_t;

static const stress_help_t help[] = {
	{ NULL,	"min-nanosleep N",	 "start N workers performing short sleeps" },
	{ NULL,	"min-nanosleep-ops N",	 "stop after N bogo sleep operations" },
	{ NULL,	"min-nanosleep-max N",	 "maximum nanosleep delay to be used" },
	{ NULL,	"min-nanosleep-sched P", "select scheduler policy [ batch, deadline, idle, fifo, other, rr ]" },
	{ NULL,	NULL,			 NULL }
};

static const char *stress_min_nanoseconds_sched(const size_t i)
{
	return (i < stress_sched_types_length) ? stress_sched_types[i].sched_name : NULL;
}

static const stress_opt_t opts[] = {
	{ OPT_min_nanosleep_max,   "min-nanosleep-max",   TYPE_ID_SIZE_T, 0, NANOSLEEP_MAX_NS, NULL },
	{ OPT_min_nanosleep_sched, "min-nanosleep-sched", TYPE_ID_SIZE_T_METHOD, 0, 0, stress_min_nanoseconds_sched },
	END_OPT,
};

#if defined(HAVE_CLOCK_GETTIME) &&	\
    defined(CLOCK_MONOTONIC)
static nanosleep_delays_t *delays = MAP_FAILED;
static size_t delays_size;

#if defined(HAVE_SCHED_SETAFFINITY) &&		\
    (defined(_POSIX_PRIORITY_SCHEDULING) || 	\
     defined(__linux__)) &&			\
    (defined(SCHED_BATCH) ||			\
     defined(SCHED_DEADLINE) ||			\
     defined(SCHED_EXT) ||			\
     defined(SCHED_FIFO) ||			\
     defined(SCHED_IDLE) ||			\
     defined(SCHED_OTHER) ||			\
     defined(SCHED_RR)) &&			\
    !defined(__OpenBSD__) &&			\
    !defined(__minix__) &&			\
    !defined(__APPLE__)
/*
 *  stress_min_nanosleep_sched_name()
 *	report scheduler name
 */
static const char * stress_min_nanosleep_sched_name(void)
{
	const int sched = sched_getscheduler(0);

	return stress_get_sched_name(sched);
}

/*
 *  stress_min_nanosleep_sched()
 *	attenmt to apply a scheduling policy, ignore if min_nanosleep_sched out of bounds
 *	or if policy cannot be applied (e.g. not enough privilege).
 */
static void stress_min_nanosleep_sched(stress_args_t *args, const size_t min_nanosleep_sched)
{
	struct sched_param param;
	int ret = 0;
	int max_prio, min_prio, rng_prio, policy;
	const char *policy_name;

	if (min_nanosleep_sched >= stress_sched_types_length)
		return;

	policy = stress_sched_types[min_nanosleep_sched].sched;
	policy_name = stress_sched_types[min_nanosleep_sched].sched_name;

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
		goto case_sched_other;
#endif
#if defined(SCHED_BATCH)
	case SCHED_BATCH:
		goto case_sched_other;
#endif
#if defined(SCHED_EXT)
	case SCHED_EXT:
		goto case_sched_other;
#endif
#if defined(SCHED_IDLE)
	case SCHED_IDLE:
		goto case_sched_other;
#endif
#if defined(SCHED_OTHER)
	case SCHED_OTHER:
#endif
#if (defined(SCHED_DEADLINE) &&		\
     defined(HAVE_SCHED_GETATTR) &&	\
     defined(HAVE_SCHED_SETATTR)) ||	\
     defined(SCHED_EXT) ||		\
     defined(SCHED_IDLE) ||		\
     defined(SCHED_BATCH)
case_sched_other:
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
		if ((min_prio == -1) || (max_prio == -1))
			return;

		rng_prio = max_prio - min_prio;
		if (UNLIKELY(rng_prio == 0)) {
			pr_inf("%s: invalid min/max priority "
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
	if (ret < 0) {
		/*
		 *  Some systems return EINVAL for non-POSIX
		 *  scheduling policies, silently ignore these
		 *  failures.
		 */
		if ((errno != EINVAL) &&
		    (errno != EINTR) &&
		    (errno != ENOSYS) &&
		    (errno != EBUSY)) {
			pr_inf("%s: sched_setscheduler "
				"failed, errno=%d (%s) "
				"for scheduler policy %s\n",
				args->name, errno, strerror(errno),
				policy_name);
		}
	}
}
#else
static const char * stress_min_nanosleep_sched_name(void)
{
	return "default (unknown)";
}

static void stress_min_nanosleep_sched(stress_args_t *args, const size_t min_nanosleep_sched)
{
	(void)min_nanosleep_sched;

	if (stress_instance_zero(args)) {
		pr_inf("%s: scheduler setting not available, "
			"ignoring --min-nanosleep-sched option\n", args->name);
	}
}
#endif

static inline size_t CONST stress_min_nanosleep_log2plus1(size_t n)
{
#if defined(HAVE_BUILTIN_CLZLL)
	long long int lln = (long long int)n;

	if (lln == 0)
		return 0;
	return (8 * sizeof(lln)) - __builtin_clzll(lln);
#else
	register size_t l2;

	for (l2 = 0; n > 1; l2++)
		n >>= 1;

	return l2 + 1;
#endif
}

static void stress_min_nanosleep_init(const uint32_t instances)
{
	delays_size = (size_t)instances * sizeof(*delays);

	delays = stress_mmap_populate(NULL, delays_size, PROT_READ | PROT_WRITE,
					MAP_ANONYMOUS | MAP_SHARED, -1, 0);
	if (delays != MAP_FAILED)
		stress_set_vma_anon_name(delays, delays_size, "nanosleep-timings");
}

static void stress_min_nanosleep_deinit(void)
{
	if (delays != MAP_FAILED)
		(void)munmap((void *)delays, delays_size);
}

static void stress_min_nanosleep_init_delay(nanosleep_delay_t *delay, const uint32_t nsec)
{
	delay->nsec = nsec;
	delay->min_nsec = NANOSLEEP_MAX;
	delay->max_nsec = 0;
	delay->count = 0;
	delay->sum_nsec = 0;
	delay->mean = 0.0;
	delay->updated = false;
}

/*
 *  stress_min_nanosleep()
 *	stress nanosleep by many sleeping threads
 */
static int stress_min_nanosleep(stress_args_t *args)
{
	int rc = EXIT_FAILURE;
	size_t i, j;
	uint32_t k;
	size_t min_nanosleep_max = NANOSLEEP_MAX_NS, max_delay;
	size_t min_nanosleep_sched = SIZE_MAX;
	const pid_t mypid = getpid();
	nanosleep_delay_t *delay, *delay_head;

	(void)stress_get_setting("min-nanosleep-max", &min_nanosleep_max);
	(void)stress_get_setting("min-nanosleep-sched", &min_nanosleep_sched);

	max_delay = stress_min_nanosleep_log2plus1(min_nanosleep_max);
	if (max_delay > NANOSLEEP_MAX_SHIFT)
		max_delay = NANOSLEEP_MAX_SHIFT;
	stress_min_nanosleep_sched(args, min_nanosleep_sched);

	if (delays == MAP_FAILED) {
		pr_inf("%s: failed to mmap an array of %zu bytes%s, "
			"errno=%d (%s), skipping stressor\n",
			args->name, max_delay * sizeof(*delay),
			stress_get_memfree_str(),
			errno, strerror(errno));
		return EXIT_NO_RESOURCE;
	}

	delay_head = &delays[args->instance].delay[0];
	delays[args->instance].pid = getpid();
	delays[args->instance].started = false;
	delays[args->instance].finished = false;

	delay = delay_head;
	stress_min_nanosleep_init_delay(delay, 0);
	delay++;

	for (i = 0; i <= NANOSLEEP_MAX_SHIFT; i++) {
		stress_min_nanosleep_init_delay(delay, 1U << i);
		delay++;
	}

	delays[args->instance].started = true;

	stress_set_proc_state(args->name, STRESS_STATE_SYNC_WAIT);
	stress_sync_start_wait(args);
	stress_set_proc_state(args->name, STRESS_STATE_RUN);

	do {
		for (i = 0, delay = delay_head; i <= max_delay; i++, delay++) {
			const uint32_t nsec = delay->nsec;
			struct timespec tv;
			struct timespec t1, t2;
			long long int dt_nsec;

			tv.tv_sec = 0;
			tv.tv_nsec = (long int)nsec;
			if (UNLIKELY(clock_gettime(CLOCK_MONOTONIC, &t1) < 0)) {
				pr_inf("%s: clock_gettime with CLOCK_MONOTONIC failed, errno=%d (%s)\n",
					args->name, errno, strerror(errno));
				goto err;
			}

			for (j = 0; j < NANOSLEEP_LOOPS; j++) {
				if (UNLIKELY(nanosleep(&tv, NULL) != 0)) {
					if (errno == EINTR)
						break;
					goto err;
				}
			}

			if (UNLIKELY(clock_gettime(CLOCK_MONOTONIC, &t2) < 0)) {
				pr_inf("%s: clock_gettime with CLOCK_MONOTONIC failed, errno=%d (%s)\n",
					args->name, errno, strerror(errno));
				goto err;
			}

			if (j) {
				dt_nsec = (t2.tv_sec - t1.tv_sec) * 1000000000;
				dt_nsec += t2.tv_nsec - t1.tv_nsec;

				dt_nsec /= j;

				if (delay->min_nsec > dt_nsec)
					delay->min_nsec = dt_nsec;
				if (delay->max_nsec < dt_nsec)
					delay->max_nsec = dt_nsec;
				delay->count++;
				delay->sum_nsec += dt_nsec;
				delay->updated = true;
			}
		}
		stress_bogo_inc(args);
	}  while (stress_continue(args));

	rc = EXIT_SUCCESS;
err:
	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);
	delays[args->instance].finished = true;

	if (stress_instance_zero(args)) {
		uint32_t count;
		uint32_t underflow = 0;
		uint32_t min_ns_requested = NANOSLEEP_MAX;
		uint32_t min_ns_measured = NANOSLEEP_MAX;

		do {
			count = 0;
			for (k = 0; k < args->instances; k++) {
				pid_t ret;
				int status;

				if (!delays[k].started) {
					count++;
					continue;
				}
				if (delays[k].finished) {
					count++;
					continue;
				}
				if (delays[k].pid == mypid)
					continue;
				if (delays[k].pid <= 1)
					continue;

				ret = waitpid(delays[k].pid, &status, 0);
				if (ret < 0) {
					if (errno == ECHILD)
						delays[k].finished = true;
				}
			}
		} while (count != args->instances);

		pr_block_begin();
		pr_inf("using scheduler '%s'\n", stress_min_nanosleep_sched_name());
		pr_inf("%8s %9s %9s %12s\n",
			"sleep ns", "min ns", "max ns", "mean ns");
		for (i = 0; i <= max_delay; i++) {
			nanosleep_delay_t result;
			char *notes = "";

			stress_min_nanosleep_init_delay(&result, delays[0].delay[i].nsec);

			for (k = 0; k < args->instances; k++) {
				delay = &delays[k].delay[i];

				if (delay->updated) {
					if (result.min_nsec > delay->min_nsec)
						result.min_nsec = delay->min_nsec;
					if (result.max_nsec < delay->max_nsec)
						result.max_nsec = delay->max_nsec;
					result.count += delay->count;
					result.sum_nsec += delay->sum_nsec;

					if (min_ns_measured > delay->min_nsec) {
						min_ns_measured = delay->min_nsec;
						min_ns_requested = delay->nsec;
					}
				}
			}
			if (result.min_nsec < result.nsec) {
				underflow++;
				notes = "(too short)";
			}

			pr_inf("%8" PRIu32 " %9" PRIu32 " %9" PRIu32 " %12.2f %s\n",
				result.nsec, result.min_nsec, result.max_nsec,
				(double)result.sum_nsec / (double)result.count,
				notes);
		}
		if (underflow) {
			pr_fail("%s: %" PRIu32 " nanosleeps were too short in duration\n",
				args->name, underflow);
			rc = EXIT_FAILURE;
		}
		if (min_ns_measured != NANOSLEEP_MAX)
			pr_inf("%s: minimum nanosleep of %" PRIu32 " ns using sleep of %" PRIu32 " ns\n",
				args->name, min_ns_measured, min_ns_requested);

		pr_block_end();
	}
	return rc;
}

const stressor_info_t stress_min_nanosleep_info = {
	.stressor = stress_min_nanosleep,
	.init = stress_min_nanosleep_init,
	.deinit = stress_min_nanosleep_deinit,
	.classifier = CLASS_INTERRUPT | CLASS_SCHEDULER | CLASS_OS,
	.verify = VERIFY_ALWAYS,
	.opts = opts,
	.help = help
};
#else
const stressor_info_t stress_min_nanosleep_info = {
	.stressor = stress_unimplemented,
	.classifier = CLASS_INTERRUPT | CLASS_SCHEDULER | CLASS_OS,
	.verify = VERIFY_ALWAYS,
	.help = help,
	.opts = opts,
	.unimplemented_reason = "built without clock_gettime() system call and CLOCK_MONOTONIC support"
};
#endif
