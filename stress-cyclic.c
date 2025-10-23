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
#include "core-lock.h"
#include "core-mmap.h"

#include <math.h>
#include <sched.h>
#include <time.h>

#undef SCHED_EXT

#if defined(HAVE_SYS_SELECT_H)
#include <sys/select.h>
#endif

#define DEFAULT_DELAY_NS	(100000)
#define MAX_SAMPLES		(100000000)
#define DEFAULT_SAMPLES		(10000)
#define MAX_BUCKETS		(250)

typedef struct {
	void *lock;			/* lock protecting count */
	uint32_t count;			/* count of error messages emitted */
} stress_cyclic_state_t;

typedef struct {
	const int	policy;		/* scheduler policy */
	const char	*name;		/* name of scheduler policy */
	const char	*opt_name;	/* option name */
	const bool	cap_sys_nice;	/* need cap sys nice to run? */
} stress_policy_t;

typedef struct {
	int64_t		min_ns;		/* min latency */
	int64_t		max_ns;		/* max latency */
	int64_t		*latencies;	/* latency samples */
	size_t		latencies_size;	/* size of latencies allocation */
	size_t		cyclic_samples;	/* number of latency samples */
	size_t		index;		/* index into latencies */
	size_t		index_reqd;	/* theoretic size of index required for the run */
	int32_t		min_prio;	/* min priority allowed */
	int32_t		max_prio;	/* max priority allowed */
	double		ns;		/* total nanosecond latency */
	double		latency_mean;	/* average latency */
	int64_t		latency_mode;	/* first mode */
	double		std_dev;	/* standard deviation */
} stress_rt_stats_t;

typedef int (*stress_cyclic_func)(stress_args_t *args, stress_rt_stats_t *rt_stats, uint64_t cyclic_sleep);

typedef struct {
	const char 		 *name;
	const stress_cyclic_func func;
} stress_cyclic_method_info_t;

static const stress_help_t help[] = {
	{ NULL,	"cyclic N",		"start N cyclic real time benchmark stressors" },
	{ NULL,	"cyclic-dist N",	"calculate distribution of interval N nanosecs" },
	{ NULL,	"cyclic-method M",	"specify cyclic method M, default is clock_ns" },
	{ NULL,	"cyclic-ops N",		"stop after N cyclic timing cycles" },
	{ NULL,	"cyclic-policy P",	"used rr or fifo scheduling policy" },
	{ NULL,	"cyclic-prio N",	"real time scheduling priority 1..100" },
	{ NULL, "cyclic-samples N",	"number of latency samples to take" },
	{ NULL,	"cyclic-sleep N",	"sleep time of real time timer in nanosecs" },
	{ NULL,	NULL,			NULL }
};

static stress_cyclic_state_t *stress_cyclic_state = MAP_FAILED;

static const stress_policy_t cyclic_policies[] = {
#if defined(SCHED_BATCH)
	{ SCHED_BATCH,    "SCHED_BATCH",     "batch",    false },
#endif
#if defined(SCHED_DEADLINE)
	{ SCHED_DEADLINE, "SCHED_DEADLINE",  "deadline", true },
#endif
#if defined(SCHED_EXT)
	{ SCHED_EXT,      "SCHED_EXT",       "ext",      false },
#endif
#if defined(SCHED_FIFO)
	{ SCHED_FIFO,     "SCHED_FIFO",      "fifo",     true },
#endif
#if defined(SCHED_IDLE)
	{ SCHED_IDLE,     "SCHED_IDLE",      "idle",     false },
#endif
#if defined(SCHED_OTHER)
	{ SCHED_OTHER,    "SCHED_OTHER",     "other",    false },
#endif
#if defined(SCHED_RR)
	{ SCHED_RR,       "SCHED_RR",        "rr",       true },
#endif
};

#define NUM_CYCLIC_POLICIES	(SIZEOF_ARRAY(cyclic_policies))

/*
 *  stress_cyclic_find_policy()
 *	try to find given policy, if it does not exist, return
 *	default first policy
 */
static inline size_t stress_cyclic_find_policy(const int policy)
{
	size_t i;

	for (i = 0; i < SIZEOF_ARRAY(cyclic_policies); i++)
		if (cyclic_policies[i].policy == policy)
			return i;

	return 0;
}

static void stress_cyclic_init(const uint32_t instances)
{
	(void)instances;

	stress_cyclic_state = (stress_cyclic_state_t *)
		stress_mmap_populate(NULL, sizeof(*stress_cyclic_state),
				PROT_READ | PROT_WRITE,
				MAP_ANONYMOUS | MAP_SHARED, -1, 0);
	if (stress_cyclic_state == MAP_FAILED)
		return;

	stress_set_vma_anon_name(stress_cyclic_state, sizeof(*stress_cyclic_state), "cyclic-state");
	stress_cyclic_state->lock = stress_lock_create("cyclic-state");
}

static void stress_cyclic_deinit(void)
{
	if (stress_cyclic_state != MAP_FAILED) {
		if (stress_cyclic_state->lock)
			stress_lock_destroy(stress_cyclic_state->lock);
		(void)munmap((void *)stress_cyclic_state, sizeof(*stress_cyclic_state));
	}
}

#if (defined(HAVE_CLOCK_GETTIME) && defined(HAVE_CLOCK_NANOSLEEP)) ||	\
    (defined(HAVE_CLOCK_GETTIME) && defined(HAVE_NANOSLEEP)) ||		\
    (defined(HAVE_CLOCK_GETTIME) && defined(HAVE_PSELECT)) ||		\
    (defined(HAVE_CLOCK_GETTIME))
static void stress_cyclic_stats(
	stress_rt_stats_t *rt_stats,
	const uint64_t cyclic_sleep,
	const struct timespec *t1,
	const struct timespec *t2)
{
	int64_t delta_ns;

	delta_ns = ((int64_t)(t2->tv_sec - t1->tv_sec) * STRESS_NANOSECOND) +
		   (t2->tv_nsec - t1->tv_nsec);
	delta_ns -= cyclic_sleep;

	if (rt_stats->index < rt_stats->cyclic_samples)
		rt_stats->latencies[rt_stats->index++] = delta_ns;
	rt_stats->index_reqd++;

	rt_stats->ns += (double)delta_ns;
}
#else
	UNEXPECTED
#endif

#if defined(HAVE_CLOCK_GETTIME) &&	\
    defined(HAVE_CLOCK_NANOSLEEP)
/*
 *  stress_cyclic_clock_nanosleep()
 *	measure latencies with clock_nanosleep
 */
static int stress_cyclic_clock_nanosleep(
	stress_args_t *args,
	stress_rt_stats_t *rt_stats,
	const uint64_t cyclic_sleep)
{
	struct timespec t1, t2, t, trem;
	int ret;

	(void)args;

	t.tv_sec = cyclic_sleep / STRESS_NANOSECOND;
	t.tv_nsec = cyclic_sleep % STRESS_NANOSECOND;
	(void)clock_gettime(CLOCK_REALTIME, &t1);
	ret = clock_nanosleep(CLOCK_REALTIME, 0, &t, &trem);
	(void)clock_gettime(CLOCK_REALTIME, &t2);
	if (LIKELY(ret == 0))
		stress_cyclic_stats(rt_stats, cyclic_sleep, &t1, &t2);
	return 0;
}
#else
	UNEXPECTED
#endif

#if defined(HAVE_CLOCK_GETTIME)	&&	\
    defined(HAVE_NANOSLEEP)
/*
 *  stress_cyclic_posix_nanosleep()
 *	measure latencies with posix nanosleep
 */
static int stress_cyclic_posix_nanosleep(
	stress_args_t *args,
	stress_rt_stats_t *rt_stats,
	const uint64_t cyclic_sleep)
{
	struct timespec t1, t2, t, trem;
	int ret;

	(void)args;

	t.tv_sec = cyclic_sleep / STRESS_NANOSECOND;
	t.tv_nsec = cyclic_sleep % STRESS_NANOSECOND;
	(void)clock_gettime(CLOCK_REALTIME, &t1);
	ret = nanosleep(&t, &trem);
	(void)clock_gettime(CLOCK_REALTIME, &t2);
	if (LIKELY(ret == 0))
		stress_cyclic_stats(rt_stats, cyclic_sleep, &t1, &t2);
	return 0;
}
#else
	UNEXPECTED
#endif

#if defined(HAVE_CLOCK_GETTIME)
/*
 *  stress_cyclic_poll()
 *	measure latencies of heavy polling the clock
 */
static int stress_cyclic_poll(
	stress_args_t *args,
	stress_rt_stats_t *rt_stats,
	const uint64_t cyclic_sleep)
{
	struct timespec t1, t2;

	(void)args;

	/* find nearest point to clock roll over */
	(void)clock_gettime(CLOCK_REALTIME, &t1);
	for (;;) {
		(void)clock_gettime(CLOCK_REALTIME, &t2);
		if ((t1.tv_sec != t2.tv_sec) || (t1.tv_nsec != t2.tv_nsec))
			break;
	}
	t1 = t2;

	for (;;) {
		int64_t delta_ns;

		(void)clock_gettime(CLOCK_REALTIME, &t2);

		delta_ns = ((int64_t)(t2.tv_sec - t1.tv_sec) * STRESS_NANOSECOND) +
			   (t2.tv_nsec - t1.tv_nsec);
		if (delta_ns >= (int64_t)cyclic_sleep) {
			delta_ns -= cyclic_sleep;

			if (rt_stats->index < rt_stats->cyclic_samples)
				rt_stats->latencies[rt_stats->index++] = delta_ns;
			rt_stats->index_reqd++;

			rt_stats->ns += (double)delta_ns;
			break;
		}
	}
	return 0;
}
#else
	UNEXPECTED
#endif

#if defined(HAVE_PSELECT) &&		\
    defined(HAVE_CLOCK_GETTIME)
/*
 *  stress_cyclic_pselect()
 *	measure latencies with pselect sleep
 */
static int stress_cyclic_pselect(
	stress_args_t *args,
	stress_rt_stats_t *rt_stats,
	const uint64_t cyclic_sleep)
{
	struct timespec t1, t2, t;
	int ret;

	(void)args;

	t.tv_sec = cyclic_sleep / STRESS_NANOSECOND;
	t.tv_nsec = cyclic_sleep % STRESS_NANOSECOND;
	(void)clock_gettime(CLOCK_REALTIME, &t1);
	ret = pselect(0, NULL, NULL,NULL, &t, NULL);
	(void)clock_gettime(CLOCK_REALTIME, &t2);
	if (LIKELY(ret == 0))
		stress_cyclic_stats(rt_stats, cyclic_sleep, &t1, &t2);
	return 0;
}
#else
	UNEXPECTED
#endif

#if defined(HAVE_CLOCK_GETTIME) &&	\
    defined(HAVE_TIMER_CREATE) &&	\
    defined(HAVE_TIMER_DELETE) &&	\
    defined(HAVE_TIMER_SETTIME)
static struct timespec itimer_time;

static void MLOCKED_TEXT stress_cyclic_itimer_handler(int sig)
{
	(void)sig;

	(void)clock_gettime(CLOCK_REALTIME, &itimer_time);
}

/*
 *  stress_cyclic_itimer()
 *	measure latencies with itimers
 */
static int stress_cyclic_itimer(
	stress_args_t *args,
	stress_rt_stats_t *rt_stats,
	const uint64_t cyclic_sleep)
{
	struct itimerspec timer;
	struct timespec t1;
	int64_t delta_ns;
	struct sigaction old_action;
	struct sigevent sev;
	timer_t timerid;
	int ret = -1;

	timer.it_interval.tv_sec = timer.it_value.tv_sec = cyclic_sleep / STRESS_NANOSECOND;
	timer.it_interval.tv_nsec = timer.it_value.tv_nsec = cyclic_sleep % STRESS_NANOSECOND;

	if (stress_sighandler(args->name, SIGRTMIN, stress_cyclic_itimer_handler, &old_action) < 0)
		return ret;

	(void)shim_memset(&sev, 0, sizeof(sev));
	sev.sigev_notify = SIGEV_SIGNAL;
	sev.sigev_signo = SIGRTMIN;
	sev.sigev_value.sival_ptr = &timerid;
	if (timer_create(CLOCK_REALTIME, &sev, &timerid) < 0)
		goto restore;

	(void)shim_memset(&itimer_time, 0, sizeof(itimer_time));
	(void)clock_gettime(CLOCK_REALTIME, &t1);
	if (timer_settime(timerid, 0, &timer, NULL) < 0)
		goto restore;

	(void)shim_pause();
	if ((itimer_time.tv_sec == 0) &&
	    (itimer_time.tv_nsec == 0))
		goto tidy;

	delta_ns = ((int64_t)(itimer_time.tv_sec - t1.tv_sec) * STRESS_NANOSECOND) +
		(itimer_time.tv_nsec - t1.tv_nsec);
	delta_ns -= cyclic_sleep;

	if (rt_stats->index < rt_stats->cyclic_samples)
		rt_stats->latencies[rt_stats->index++] = delta_ns;
	rt_stats->index_reqd++;

	rt_stats->ns += (double)delta_ns;

	(void)timer_delete(timerid);

	ret = 0;
tidy:
	/* And cancel timer */
	(void)shim_memset(&timer, 0, sizeof(timer));
	(void)timer_settime(timerid, 0, &timer, NULL);
restore:
	stress_sigrestore(args->name, SIGRTMIN, &old_action);
	return ret;
}
#else
	UNEXPECTED
#endif

#if defined(HAVE_CLOCK_GETTIME)
/*
 *  stress_cyclic_usleep()
 *	measure latencies with usleep
 */
static int stress_cyclic_usleep(
	stress_args_t *args,
	stress_rt_stats_t *rt_stats,
	const uint64_t cyclic_sleep)
{
	struct timespec t1, t2;
	const useconds_t usecs = (useconds_t)cyclic_sleep / 1000;
	int ret;

	(void)args;

	(void)clock_gettime(CLOCK_REALTIME, &t1);
	ret = usleep(usecs);
	(void)clock_gettime(CLOCK_REALTIME, &t2);
	if (LIKELY(ret == 0))
		stress_cyclic_stats(rt_stats, cyclic_sleep, &t1, &t2);
	return 0;
}
#else
	UNEXPECTED
#endif

#if defined(HAVE_SIGLONGJMP)
static sigjmp_buf jmp_env;

/*
 *  stress_rlimit_handler()
 *      rlimit generic handler
 */
static void NORETURN MLOCKED_TEXT stress_rlimit_handler(int signum)
{
	(void)signum;

	stress_continue_set_flag(false);
	siglongjmp(jmp_env, 1);
	stress_no_return();
}
#endif

/*
 *  stress_cyclic_cmp()
 *	sort latencies into order, least first
 */
static int stress_cyclic_cmp(const void *p1, const void *p2)
{
	const int64_t *i1 = (const int64_t *)p1;
	const int64_t *i2 = (const int64_t *)p2;

	if (*i1 > *i2)
		return 1;
	else if (*i1 < *i2)
		return -1;
	return 0;
}

/*
 *  stress_rt_stats()
 *	compute statistics on gathered latencies
 */
static void stress_rt_stats(stress_rt_stats_t *rt_stats)
{
	size_t i;
	size_t n = 0, best_n = 0;
	int64_t current;
	double variance = 0.0;

	rt_stats->latency_mean = 0.0;
	rt_stats->latency_mode = 0;

	for (i = 0; i < rt_stats->index; i++) {
		const int64_t ns = rt_stats->latencies[i];

		if (ns > rt_stats->max_ns)
			rt_stats->max_ns = ns;
		if (ns < rt_stats->min_ns)
			rt_stats->min_ns = ns;

		rt_stats->latency_mean += (double)ns;
	}
	if (rt_stats->index)
		rt_stats->latency_mean /= (double)rt_stats->index;

	qsort(rt_stats->latencies, rt_stats->index, sizeof(*(rt_stats->latencies)), stress_cyclic_cmp);

	current = rt_stats->latency_mode = rt_stats->latencies[0];

	for (i = 0; i < rt_stats->index; i++) {
		const int64_t ns = rt_stats->latencies[i];
		double diff;

		if (ns == current) {
			n++;
			if (n > best_n) {
				rt_stats->latency_mode = current;
				best_n = n;
			}
		} else {
			current = ns;
			n = 0;
		}
		diff = ((double)ns - rt_stats->latency_mean);
		variance += (diff * diff);
	}
	if (rt_stats->index) {
		variance /= (double)rt_stats->index;
		rt_stats->std_dev = sqrt(variance);
	}
}

/*
 *  cyclic methods
 */
static const stress_cyclic_method_info_t cyclic_methods[] = {
#if defined(HAVE_CLOCK_GETTIME) &&	\
    defined(HAVE_CLOCK_NANOSLEEP)
	{ "clock_ns",	stress_cyclic_clock_nanosleep },
#endif

#if defined(HAVE_CLOCK_GETTIME) &&	\
    defined(HAVE_TIMER_CREATE) &&	\
    defined(HAVE_TIMER_DELETE) &&	\
    defined(HAVE_TIMER_SETTIME)
	{ "itimer",	stress_cyclic_itimer },
#endif

#if defined(HAVE_CLOCK_GETTIME)
	{ "poll",	stress_cyclic_poll },
#endif

#if defined(HAVE_CLOCK_GETTIME)	&&	\
    defined(HAVE_NANOSLEEP)
	{ "posix_ns",	stress_cyclic_posix_nanosleep },
#endif

#if defined(HAVE_PSELECT) &&		\
    defined(HAVE_CLOCK_GETTIME)
	{ "pselect",	stress_cyclic_pselect },
#endif

#if defined(HAVE_CLOCK_GETTIME)
	{ "usleep",	stress_cyclic_usleep },
#endif
};

#define NUM_CYCLIC_METHODS	(SIZEOF_ARRAY(cyclic_methods))

/*
 *  stress_rt_dist()
 *	show real time distribution
 */
static void stress_rt_dist(
	const char *name,
	stress_rt_stats_t *rt_stats,
	const int64_t cyclic_dist)
{
	const ssize_t dist_max_size = (cyclic_dist > 0) ?
		((ssize_t)rt_stats->max_ns / (ssize_t)cyclic_dist) + 1 : 1;
	const ssize_t dist_size = STRESS_MINIMUM(MAX_BUCKETS, dist_max_size);
	const ssize_t dist_min = STRESS_MINIMUM(5, dist_max_size);
	ssize_t i, n;
	int64_t *dist;

	if (!cyclic_dist)
		return;

	dist = (int64_t *)calloc(dist_size, sizeof(*dist));
	if (!dist) {
		pr_inf("%s: cannot allocate distribution stats buffer, cannot log distribution\n", name);
		return;
	}

	for (i = 0; i < (ssize_t)rt_stats->index; i++) {
		const int64_t lat = rt_stats->latencies[i] / cyclic_dist;

		if (lat < (int64_t)dist_size)
			dist[lat]++;
	}

	for (n = dist_size; n >= 1; n--) {
		if (dist[n - 1])
			break;
	}
	if (n < dist_min)
		n = dist_min;
	if (n >= dist_size - 3)
		n = dist_size;

	pr_inf("%s: latency distribution (%" PRIu64 " ns intervals):\n", name, cyclic_dist);
	pr_inf("%s: (for the first %zd buckets of %zd)\n", name, dist_size, dist_max_size);
	pr_inf("%s: %12s %10s\n", name, "latency (ns)", "frequency");
	for (i = 0; i < n; i++) {
		pr_inf("%s: %12" PRIu64 " %10" PRId64 "\n",
			name, cyclic_dist * i, dist[i]);
	}

	/*
	 *  This caters for the case where there are lots of zeros at
	 *  the end of the distribution
	 */
	if (n < dist_size) {
		pr_inf("%s: %12s %10s (all zeros hereafter)\n", name, "..", "..");
		pr_inf("%s: %12s %10s\n", name, "..", "..");
		for (i = STRESS_MAXIMUM(dist_size - 3, n); i < dist_size; i++) {
			pr_inf("%s: %12" PRIu64 " %10" PRId64 "\n",
				name, cyclic_dist * i, (int64_t)0);
		}
	}
	free(dist);
}

static int stress_cyclic(stress_args_t *args)
{
	const uint32_t instances = args->instances;
#if defined(HAVE_SIGLONGJMP)
	struct sigaction old_action_xcpu;
#endif
	struct rlimit rlim;
	pid_t pid;
	NOCLOBBER uint64_t timeout;
	uint64_t cyclic_sleep = DEFAULT_DELAY_NS;
	uint64_t cyclic_dist = 0;
	int32_t cyclic_prio = INT32_MAX;
	size_t cyclic_samples = DEFAULT_SAMPLES;
	NOCLOBBER int policy;
	int rc = EXIT_SUCCESS;
#if defined(SCHED_FIFO)
	size_t cyclic_policy = stress_cyclic_find_policy(SCHED_FIFO);
#else
	size_t cyclic_policy = 0;
#endif
	size_t cyclic_method = 0;
	const double start = stress_time_now();
	stress_rt_stats_t *rt_stats;
	const size_t page_size = args->page_size;
	const size_t size = (sizeof(*rt_stats) + page_size - 1) & (~(page_size - 1));
	stress_cyclic_func func;
	char sched_ext_op[128];

	timeout  = g_opt_timeout;
	(void)stress_get_setting("cyclic-dist", &cyclic_dist);
	(void)stress_get_setting("cyclic-method", &cyclic_method);
	(void)stress_get_setting("cyclic-policy", &cyclic_policy);
	(void)stress_get_setting("cyclic-prio", &cyclic_prio);
	(void)stress_get_setting("cyclic-samples", &cyclic_samples);
	(void)stress_get_setting("cyclic-sleep", &cyclic_sleep);

	if (NUM_CYCLIC_POLICIES == 0) {
		if (!args->instance) {
			pr_inf_skip("%s: no scheduling policies "
				"available, skipping stressor\n",
				args->name);
		}
		return EXIT_NOT_IMPLEMENTED;
	}
	if ((ssize_t)cyclic_policy >= (ssize_t)NUM_CYCLIC_POLICIES) {
		if (!args->instance) {
			pr_err("%s: cyclic-policy %zu is out of range\n",
				args->name, cyclic_policy);
		}
		return EXIT_FAILURE;
	}
	if (NUM_CYCLIC_METHODS == 0) {
		if (!args->instance) {
			pr_inf_skip("%s: no cyclic methods"
				"available, skipping stressor\n",
				args->name);
		}
		return EXIT_NOT_IMPLEMENTED;
	}
	if ((ssize_t)cyclic_method >= (ssize_t)NUM_CYCLIC_METHODS) {
		if (!args->instance) {
			pr_err("%s: cyclic-method %zu is out of range\n",
				args->name, cyclic_method);
		}
		return EXIT_FAILURE;
	}

	func = cyclic_methods[cyclic_method].func;
	policy = cyclic_policies[cyclic_policy].policy;

	if (cyclic_policies[cyclic_policy].cap_sys_nice &&
	    !stress_check_capability(SHIM_CAP_SYS_NICE)) {
		pr_inf_skip("%s stressor needs to be run with CAP_SYS_NICE "
			"set for %s policy, skipping stressor\n",
			args->name, cyclic_policies[cyclic_policy].name);
		return EXIT_NO_RESOURCE;
	}

	*sched_ext_op = '\0';
#if defined(SCHED_EXT)
	if (policy == SCHED_EXT)
		(void)sched_get_sched_ext_ops(sched_ext_op, sizeof(sched_ext_op));
#endif

	if (g_opt_timeout == TIMEOUT_NOT_SET) {
		timeout = 60;
		pr_inf("%s: timeout has not been set, forcing timeout to "
			"be %" PRIu64 " seconds\n", args->name, timeout);
	}

	if ((instances > 1) && (stress_instance_zero(args))) {
		pr_inf("%s: for best results, run just 1 instance of "
			"this stressor\n", args->name);
	}

	rt_stats = (stress_rt_stats_t *)stress_mmap_populate(NULL, size,
						PROT_READ | PROT_WRITE,
						MAP_SHARED | MAP_ANONYMOUS, -1, 0);
	if (rt_stats == MAP_FAILED) {
		pr_inf_skip("%s: mmap of shared statistics data failed%s, errno=%d (%s)\n",
			args->name, stress_get_memfree_str(),
			errno, strerror(errno));
		return EXIT_NO_RESOURCE;
	}
	stress_set_vma_anon_name(rt_stats, size, "rt-statistics");
	rt_stats->cyclic_samples = cyclic_samples;
	rt_stats->latencies_size = cyclic_samples * sizeof(*rt_stats->latencies);
	rt_stats->latencies = (int64_t *)stress_mmap_populate(NULL,
						rt_stats->latencies_size,
						PROT_READ | PROT_WRITE,
						MAP_SHARED | MAP_ANONYMOUS, -1, 0);
	if (rt_stats->latencies == MAP_FAILED) {
		pr_inf_skip("%s: mmap of %zu samples failed%s, errno=%d (%s)\n",
			args->name, cyclic_samples, stress_get_memfree_str(),
			errno, strerror(errno));
		(void)munmap((void *)rt_stats, size);
		return EXIT_NO_RESOURCE;
	}
	stress_set_vma_anon_name(rt_stats->latencies, rt_stats->latencies_size, "latencies");
	rt_stats->min_ns = INT64_MAX;
	rt_stats->max_ns = INT64_MIN;
	rt_stats->ns = 0.0;
#if defined(HAVE_SCHED_GET_PRIORITY_MIN)
	rt_stats->min_prio = sched_get_priority_min(policy);
#else
	rt_stats->min_prio = 0;
#endif

#if defined(HAVE_SCHED_GET_PRIORITY_MAX)
	rt_stats->max_prio = sched_get_priority_max(policy);
#else
	rt_stats->max_prio = 0;
#endif
	/* If user has set max priority.. */
	if (cyclic_prio != INT32_MAX) {
		if (rt_stats->max_prio > cyclic_prio) {
			rt_stats->max_prio = cyclic_prio;
		}
	}

	if (stress_instance_zero(args))
		pr_dbg("%s: using method '%s'\n", args->name, cyclic_methods[cyclic_method].name);

	stress_set_proc_state(args->name, STRESS_STATE_SYNC_WAIT);
	stress_sync_start_wait(args);
	stress_set_proc_state(args->name, STRESS_STATE_RUN);

again:
	pid = fork();
	if (pid < 0) {
		if (stress_redo_fork(args, errno))
			goto again;
		if (UNLIKELY(!stress_continue(args)))
			goto finish;
		pr_inf("%s: cannot fork, errno=%d (%s)\n",
			args->name, errno, strerror(errno));
		(void)munmap((void *)rt_stats->latencies, rt_stats->latencies_size);
		(void)munmap((void *)rt_stats, size);
		return EXIT_NO_RESOURCE;
	} else if (pid == 0) {
#if defined(HAVE_SCHED_GET_PRIORITY_MIN) &&	\
    defined(HAVE_SCHED_GET_PRIORITY_MAX)
		const pid_t mypid = getpid();
#endif
		int ret;
		NOCLOBBER int ncrc = EXIT_FAILURE;

		stress_set_proc_state(args->name, STRESS_STATE_RUN);

		/*
		 * We run the stressor as a child so that
		 * if we the hard time timits the child is
		 * terminated with a SIGKILL and we can
		 * catch that with the parent
		 */
		rlim.rlim_cur = timeout;
		rlim.rlim_max = timeout;
		(void)setrlimit(RLIMIT_CPU, &rlim);

#if defined(RLIMIT_RTTIME)
		rlim.rlim_cur = 1000000 * timeout;
		rlim.rlim_max = 1000000 * timeout;
		(void)setrlimit(RLIMIT_RTTIME, &rlim);
#endif

#if defined(HAVE_SIGLONGJMP)
		ret = sigsetjmp(jmp_env, 1);
		if (ret)
			goto tidy_ok;
		if (stress_sighandler(args->name, SIGXCPU, stress_rlimit_handler, &old_action_xcpu) < 0)
			goto tidy;
#else
		(void)ret;
#endif

#if defined(HAVE_SCHED_GET_PRIORITY_MIN) &&	\
    defined(HAVE_SCHED_GET_PRIORITY_MAX)
#if defined(SCHED_DEADLINE)
redo_policy:
#endif
		ret = stress_set_sched(mypid, policy, rt_stats->max_prio, true);
		if (ret < 0) {
			const int saved_errno = errno;

#if defined(SCHED_DEADLINE)
			/*
			 *  The following occurs if we use an older kernel
			 *  that does not support the larger newer attr structure
			 *  but userspace does. This currently only occurs with
			 *  SCHED_DEADLINE; fall back to the next scheduling policy
			 *  which users the older and smaller attr structure.
			 */
			if ((saved_errno == E2BIG) &&
			    (cyclic_policies[cyclic_policy].policy == SCHED_DEADLINE)) {
				cyclic_policy = 1;
				if ((ssize_t)cyclic_policy >= (ssize_t)NUM_CYCLIC_POLICIES) {
					pr_inf("%s: DEADLINE not supported by kernel, no other policies "
						"available. skipping stressor\n", args->name);
					ncrc = EXIT_NO_RESOURCE;
					goto finish;
				}
				policy = cyclic_policies[cyclic_policy].policy;
#if defined(HAVE_SCHED_GET_PRIORITY_MAX)
				rt_stats->max_prio = sched_get_priority_max(policy);
#else
				rt_stats->max_prio = 0;
#endif
				pr_inf("%s: DEADLINE not supported by kernel, defaulting to %s\n",
					args->name, cyclic_policies[cyclic_policy].name);
				goto redo_policy;
			}
#endif
			if (saved_errno != EPERM) {
				uint32_t count = 0;

				const char *msg = (saved_errno == EBUSY) ?
					", (recommend setting --sched-runtime to less than 90000 or run one instance of cyclic stressor)" : "";

				if (stress_cyclic_state != MAP_FAILED) {
					(void)stress_lock_acquire(stress_cyclic_state->lock);
					count = stress_cyclic_state->count;
					stress_cyclic_state->count++;
					(void)stress_lock_release(stress_cyclic_state->lock);
				}

				if (count == 0) {
					pr_fail("%s: sched_setscheduler "
						"failed, errno=%d (%s) "
						"for scheduler policy %s%s\n",
						args->name, saved_errno, strerror(saved_errno),
						cyclic_policies[cyclic_policy].name,
						msg);
					if (saved_errno == EINVAL) {
						kill(getppid(), SIGALRM);
						goto tidy;
					}
				}
			}
			goto tidy;
		}
#endif
		do {
			func(args, rt_stats, cyclic_sleep);
			stress_bogo_inc(args);

			/* Ensure we NEVER spin forever */
			if (UNLIKELY((stress_time_now() - start) > (double)timeout))
				break;
		} while (stress_continue(args));

#if defined(HAVE_SIGLONGJMP)
tidy_ok:
#endif
		ncrc = EXIT_SUCCESS;
#if defined(HAVE_SIGLONGJMP) ||	\
    (defined(HAVE_SCHED_GET_PRIORITY_MIN) &&	\
     defined(HAVE_SCHED_GET_PRIORITY_MAX))
tidy:
#endif
		(void)fflush(stdout);
		(void)munmap((void *)rt_stats->latencies, rt_stats->latencies_size);
		(void)munmap((void *)rt_stats, size);
		_exit(ncrc);
	} else {
		VOID_RET(int, stress_set_sched(args->pid, policy, rt_stats->max_prio, true));

		(void)shim_pause();
		stress_force_killed_bogo(args);
		(void)stress_kill_pid_wait(pid, NULL);
	}

	stress_rt_stats(rt_stats);

	if (stress_instance_zero(args)) {
		if (rt_stats->index) {
			size_t i;

			static const double percentiles[] = {
				25.0,
				50.0,
				75.0,
				90.0,
				95.40,
				99.0,
				99.5,
				99.9,
				99.99,
			};

			pr_block_begin();
			pr_inf("%s: sched %s%s%s%s: %" PRIu64 " ns delay, %zd samples\n",
				args->name,
				cyclic_policies[cyclic_policy].name,
				(*sched_ext_op) ? " (" : "",
				sched_ext_op,
				(*sched_ext_op) ? ")" : "",
				cyclic_sleep,
				rt_stats->index);
			pr_inf( "%s:   mean: %.2f ns, mode: %" PRId64 " ns\n",
				args->name,
				rt_stats->latency_mean,
				rt_stats->latency_mode);
			pr_inf("%s:   min: %" PRId64 " ns, max: %" PRId64 " ns, std.dev. %.2f\n",
				args->name,
				rt_stats->min_ns,
				rt_stats->max_ns,
				rt_stats->std_dev);

			pr_inf("%s: latency percentiles:\n", args->name);
			for (i = 0; i < sizeof(percentiles) / sizeof(percentiles[0]); i++) {
				size_t j = (size_t)(((double)rt_stats->index * percentiles[i]) / 100.0);
				pr_inf("%s:   %5.2f%%: %10" PRId64 " ns\n",
					args->name,
					percentiles[i],
					rt_stats->latencies[j]);
			}
			stress_rt_dist(args->name, rt_stats, (int64_t)cyclic_dist);

			if (rt_stats->index < rt_stats->index_reqd)
				pr_inf("%s: Note: --cyclic-samples needed to be %zd to capture all the data for this run\n",
					args->name, rt_stats->index_reqd);
			pr_block_end();
		} else {
			pr_inf("%s: %s: no latency information available\n",
				args->name,
				cyclic_policies[cyclic_policy].name);
		}
	}

finish:
	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);

	(void)munmap((void *)rt_stats->latencies, rt_stats->latencies_size);
	(void)munmap((void *)rt_stats, size);

	return rc;
}

static const char *stress_cyclic_methods(const size_t i)
{
	return (NUM_CYCLIC_METHODS == 0) ? NULL :
		(((ssize_t)i < (ssize_t)NUM_CYCLIC_METHODS) ? cyclic_methods[i].name : NULL);
}

static const char *stress_cyclic_policies(const size_t i)
{
	return (NUM_CYCLIC_POLICIES == 0) ? NULL :
		(((ssize_t)i < (ssize_t)NUM_CYCLIC_POLICIES) ? cyclic_policies[i].opt_name : NULL);
};

static const stress_opt_t opts[] = {
	{ OPT_cyclic_dist,    "cyclic-dist",    TYPE_ID_UINT64, 1, 10000000, NULL },
	{ OPT_cyclic_method,  "cyclic-method",  TYPE_ID_SIZE_T_METHOD, 0, 0, stress_cyclic_methods },
	{ OPT_cyclic_policy,  "cyclic-policy",  TYPE_ID_SIZE_T_METHOD, 0, 0, stress_cyclic_policies },
	{ OPT_cyclic_prio,    "cyclic-prio",    TYPE_ID_INT32, 1, 100, NULL },
	{ OPT_cyclic_sleep,   "cyclic-sleep",   TYPE_ID_UINT64, 1, STRESS_NANOSECOND, NULL },
	{ OPT_cyclic_samples, "cyclic-samples", TYPE_ID_SIZE_T, 1, MAX_SAMPLES, NULL },
	END_OPT,
};

const stressor_info_t stress_cyclic_info = {
	.stressor = stress_cyclic,
	.classifier = CLASS_SCHEDULER | CLASS_OS,
	.opts = opts,
	.init = stress_cyclic_init,
	.deinit = stress_cyclic_deinit,
	.help = help
};
