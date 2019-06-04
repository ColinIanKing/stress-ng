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

#define DEFAULT_DELAY_NS	(100000)
#define MAX_SAMPLES		(10000)
#define MAX_BUCKETS		(250)
#define NANOSECS		(1000000000)

typedef struct {
	const int	policy;		/* scheduler policy */
	const char	*name;		/* name of scheduler policy */
	const char	*opt_name;	/* option name */
} policy_t;

typedef struct {
	int64_t		min_ns;		/* min latency */
	int64_t		max_ns;		/* max latency */
	int64_t		latencies[MAX_SAMPLES];
	size_t		index;		/* index into latencies */
	int32_t		min_prio;	/* min priority allowed */
	int32_t		max_prio;	/* max priority allowed */
	double		ns;		/* total nanosecond latency */
	double		latency_mean;	/* average latency */
	int64_t		latency_mode;	/* first mode */
	double		std_dev;	/* standard deviation */
} rt_stats_t;

typedef int (*cyclic_func)(const args_t *args, rt_stats_t *rt_stats, uint64_t cyclic_sleep);

typedef struct {
	const char 		*name;
	const cyclic_func	func;
} stress_cyclic_method_info_t;

static const help_t help[] = {
	{ NULL,	"cyclic N",		"start N cyclic real time benchmark stressors" },
	{ NULL,	"cyclic-ops N",		"stop after N cyclic timing cycles" },
	{ NULL,	"cyclic-method M",	"specify cyclic method M, default is clock_ns" },
	{ NULL,	"cyclic-dist N",	"calculate distribution of interval N nanosecs" },
	{ NULL,	"cyclic-policy P",	"used rr or fifo scheduling policy" },
	{ NULL,	"cyclic-prio N",	"real time scheduling priority 1..100" },
	{ NULL,	"cyclic-sleep N",	"sleep time of real time timer in nanosecs" },
	{ NULL,	NULL,			NULL }
};

static const policy_t policies[] = {
#if defined(SCHED_DEADLINE)
	{ SCHED_DEADLINE, "SCHED_DEADLINE",  "deadline" },
#endif
#if defined(SCHED_FIFO)
	{ SCHED_FIFO,     "SCHED_FIFO",      "fifo" },
#endif
#if defined(SCHED_RR)
	{ SCHED_RR,       "SCHED_RR",        "rr" },
#endif
};

static const size_t num_policies = SIZEOF_ARRAY(policies);

static int stress_set_cyclic_sleep(const char *opt)
{
	uint64_t cyclic_sleep;

	cyclic_sleep = get_uint64(opt);
        check_range("cyclic-sleep", cyclic_sleep,
                1, NANOSECS);
        return set_setting("cyclic-sleep", TYPE_ID_UINT64, &cyclic_sleep);
}

static int stress_set_cyclic_policy(const char *opt)
{
	size_t policy;

	for (policy = 0; policy < num_policies; policy++) {
		if (!strcmp(opt, policies[policy].opt_name)) {
			set_setting("cyclic-policy", TYPE_ID_SIZE_T, &policy);
			return 0;
		}
	}
	(void)fprintf(stderr, "invalid cyclic-policy '%s', policies allowed are:", opt);
	for (policy = 0; policy < num_policies; policy++) {
		(void)fprintf(stderr, " %s", policies[policy].opt_name);
	}
	(void)fprintf(stderr, "\n");
	return -1;
}

static int stress_set_cyclic_prio(const char *opt)
{
	int32_t cyclic_prio;

	cyclic_prio = get_int32(opt);
        check_range("cyclic-prio", cyclic_prio, 1, 100);
        return set_setting("cyclic-prio", TYPE_ID_INT32, &cyclic_prio);
}

static int stress_set_cyclic_dist(const char *opt)
{
	uint64_t cyclic_dist;

	cyclic_dist = get_uint64(opt);
        check_range("cyclic-dist", cyclic_dist, 1, 10000000);
        return set_setting("cyclic-dist", TYPE_ID_UINT64, &cyclic_dist);
}

static void stress_cyclic_stats(
	rt_stats_t *rt_stats,
	const uint64_t cyclic_sleep,
	const struct timespec *t1,
	const struct timespec *t2)
{
	int64_t delta_ns;

	delta_ns = ((int64_t)(t2->tv_sec - t1->tv_sec) * NANOSECS) +
		   (t2->tv_nsec - t1->tv_nsec);
	delta_ns -= cyclic_sleep;

	if (rt_stats->index < MAX_SAMPLES)
		rt_stats->latencies[rt_stats->index++] = delta_ns;

	rt_stats->ns += (double)delta_ns;
}

#if defined(HAVE_CLOCK_GETTIME) &&	\
    defined(HAVE_CLOCK_NANOSLEEP) 
/*
 *  stress_cyclic_clock_nanosleep()
 *	measure latencies with clock_nanosleep
 */
static int stress_cyclic_clock_nanosleep(
	const args_t *args,
	rt_stats_t *rt_stats,
	const uint64_t cyclic_sleep)
{
	struct timespec t1, t2, t, trem;
	int ret;

	(void)args;

	t.tv_sec = cyclic_sleep / NANOSECS;
	t.tv_nsec = cyclic_sleep % NANOSECS;
	(void)clock_gettime(CLOCK_REALTIME, &t1);
	ret = clock_nanosleep(CLOCK_REALTIME, 0, &t, &trem);
	(void)clock_gettime(CLOCK_REALTIME, &t2);
	if (ret == 0)
		stress_cyclic_stats(rt_stats, cyclic_sleep, &t1, &t2);
	return 0;
}
#endif

#if defined(HAVE_CLOCK_GETTIME)	&&	\
    defined(HAVE_NANOSLEEP)
/*
 *  stress_cyclic_posix_nanosleep()
 *	measure latencies with posix nanosleep
 */
static int stress_cyclic_posix_nanosleep(
	const args_t *args,
	rt_stats_t *rt_stats,
	const uint64_t cyclic_sleep)
{
	struct timespec t1, t2, t, trem;
	int ret;

	(void)args;

	t.tv_sec = cyclic_sleep / NANOSECS;
	t.tv_nsec = cyclic_sleep % NANOSECS;
	(void)clock_gettime(CLOCK_REALTIME, &t1);
	ret = nanosleep(&t, &trem);
	(void)clock_gettime(CLOCK_REALTIME, &t2);
	if (ret == 0)
		stress_cyclic_stats(rt_stats, cyclic_sleep, &t1, &t2);
	return 0;
}
#endif

#if defined(HAVE_CLOCK_GETTIME)
/*
 *  stress_cyclic_poll()
 *	measure latencies of heavy polling the clock
 */
static int stress_cyclic_poll(
	const args_t *args,
	rt_stats_t *rt_stats,
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

		delta_ns = ((int64_t)(t2.tv_sec - t1.tv_sec) * NANOSECS) +
			   (t2.tv_nsec - t1.tv_nsec);
		if (delta_ns >= (int64_t)cyclic_sleep) {
			delta_ns -= cyclic_sleep;

			if (rt_stats->index < MAX_SAMPLES)
				rt_stats->latencies[rt_stats->index++] = delta_ns;

			rt_stats->ns += (double)delta_ns;
			break;
		}
	}
	return 0;
}
#endif

#if defined(HAVE_PSELECT) &&		\
    defined(HAVE_CLOCK_GETTIME)
/*
 *  stress_cyclic_pselect()
 *	measure latencies with pselect sleep
 */
static int stress_cyclic_pselect(
	const args_t *args,
	rt_stats_t *rt_stats,
	const uint64_t cyclic_sleep)
{
	struct timespec t1, t2, t;
	int ret;

	(void)args;

	t.tv_sec = cyclic_sleep / NANOSECS;
	t.tv_nsec = cyclic_sleep % NANOSECS;
	(void)clock_gettime(CLOCK_REALTIME, &t1);
	ret = pselect(0, NULL, NULL,NULL, &t, NULL);
	(void)clock_gettime(CLOCK_REALTIME, &t2);
	if (ret == 0)
		stress_cyclic_stats(rt_stats, cyclic_sleep, &t1, &t2);
	return 0;
}
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
	const args_t *args,
	rt_stats_t *rt_stats,
	const uint64_t cyclic_sleep)
{
	struct itimerspec timer;
	struct timespec t1;
	int64_t delta_ns;
	struct sigaction old_action;
	struct sigevent sev;
	timer_t timerid;
	int ret = -1;

	timer.it_interval.tv_sec = timer.it_value.tv_sec = cyclic_sleep / NANOSECS;
	timer.it_interval.tv_nsec = timer.it_value.tv_nsec = cyclic_sleep % NANOSECS;

	if (stress_sighandler(args->name, SIGRTMIN, stress_cyclic_itimer_handler, &old_action) < 0)
		return ret;

	sev.sigev_notify = SIGEV_SIGNAL;
	sev.sigev_signo = SIGRTMIN;
	sev.sigev_value.sival_ptr = &timerid;
	if (timer_create(CLOCK_REALTIME, &sev, &timerid) < 0)
		goto restore;

	(void)memset(&itimer_time, 0, sizeof(itimer_time));
	(void)clock_gettime(CLOCK_REALTIME, &t1);
	if (timer_settime(timerid, 0, &timer, NULL) < 0)
		goto restore;

	(void)pause();
	if ((itimer_time.tv_sec == 0) &&
            (itimer_time.tv_nsec == 0))
		goto tidy;

	delta_ns = ((int64_t)(itimer_time.tv_sec - t1.tv_sec) * NANOSECS) + (itimer_time.tv_nsec - t1.tv_nsec);
	delta_ns -= cyclic_sleep;

	if (rt_stats->index < MAX_SAMPLES)
		rt_stats->latencies[rt_stats->index++] = delta_ns;

	rt_stats->ns += (double)delta_ns;

	(void)timer_delete(timerid);

	ret = 0;
tidy:
	/* And cancel timer */
	(void)memset(&timer, 0, sizeof(timer));
	(void)timer_settime(timerid, 0, &timer, NULL);
restore:
	stress_sigrestore(args->name, SIGRTMIN, &old_action);
	return ret;
}
#endif

/*
 *  stress_cyclic_usleep()
 *	measure latencies with usleep
 */
static int stress_cyclic_usleep(
	const args_t *args,
	rt_stats_t *rt_stats,
	const uint64_t cyclic_sleep)
{
	struct timespec t1, t2;
	const useconds_t usecs = cyclic_sleep / 1000;
	int ret;

	(void)args;

	(void)clock_gettime(CLOCK_REALTIME, &t1);
	ret = usleep(usecs);
	(void)clock_gettime(CLOCK_REALTIME, &t2);
	if (ret == 0)
		stress_cyclic_stats(rt_stats, cyclic_sleep, &t1, &t2);
	return 0;
}

static sigjmp_buf jmp_env;

/*
 *  stress_rlimit_handler()
 *      rlimit generic handler
 */
static void MLOCKED_TEXT stress_rlimit_handler(int signum)
{
	(void)signum;

	g_keep_stressing_flag = 1;
	siglongjmp(jmp_env, 1);
}

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
static void stress_rt_stats(rt_stats_t *rt_stats)
{
	size_t i;
	size_t n = 0, best_n = 0;
	int64_t current;
	double variance = 0.0;

	rt_stats->latency_mean = 0.0;
	rt_stats->latency_mode = 0;

	for (i = 0; i < rt_stats->index; i++) {
		int64_t ns = rt_stats->latencies[i];

		if (ns > rt_stats->max_ns)
			rt_stats->max_ns = ns;
		if (ns < rt_stats->min_ns)
			rt_stats->min_ns = ns;

		rt_stats->latency_mean += (double)ns;
	}
	if (rt_stats->index)
		rt_stats->latency_mean /= (double)rt_stats->index;

	qsort(rt_stats->latencies, rt_stats->index, sizeof(int64_t), stress_cyclic_cmp);

	current = rt_stats->latency_mode = rt_stats->latencies[0];

	for (i = 0; i < rt_stats->index; i++) {
		int64_t ns = rt_stats->latencies[i];
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
		variance /= rt_stats->index;
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

	{ "usleep",	stress_cyclic_usleep },

	{ NULL,		NULL }
};

/*
 *  stress_set_cyclic_method()
 *	set the default cyclic method
 */
static int stress_set_cyclic_method(const char *name)
{
	stress_cyclic_method_info_t const *info;

	for (info = cyclic_methods; info->func; info++) {
		if (!strcmp(info->name, name)) {
			set_setting("cyclic-method", TYPE_ID_UINTPTR_T, &info);
			return 0;
		}
	}

	(void)fprintf(stderr, "cyclic-method must be one of:");
	for (info = cyclic_methods; info->func; info++) {
		(void)fprintf(stderr, " %s", info->name);
	}
	(void)fprintf(stderr, "\n");

	return -1;
}

/*
 *  stress_rt_dist()
 *	show real time distribution
 */
static void stress_rt_dist(
	const char *name,
	bool *lock,
	rt_stats_t *rt_stats,
	const uint64_t cyclic_dist)
{
	ssize_t dist_max_size = (cyclic_dist > 0) ? (rt_stats->max_ns / cyclic_dist) + 1 : 1;
	ssize_t dist_size = STRESS_MINIMUM(MAX_BUCKETS, dist_max_size);
	const ssize_t dist_min = STRESS_MINIMUM(5, dist_max_size);
	ssize_t i, n;
	int64_t dist[dist_size];

	if (!cyclic_dist)
		return;

	(void)memset(dist, 0, sizeof(dist));

	for (i = 0; i < (ssize_t)rt_stats->index; i++) {
		int64_t lat = rt_stats->latencies[i] / cyclic_dist;

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

	pr_inf_lock(lock, "%s: latency distribution (%" PRIu64 " ns intervals):\n", name, cyclic_dist);
	pr_inf_lock(lock, "%s: (for the first %zd buckets of %zd)\n", name, dist_size, dist_max_size);
	pr_inf_lock(lock, "%s: %12s %10s\n", name, "latency (ns)", "frequency");
	for (i = 0; i < n; i++) {
		pr_inf_lock(lock, "%s: %12" PRIu64 " %10" PRId64 "\n",
			name, cyclic_dist * i, dist[i]);
	}

	/*
	 *  This caters for the case where there are lots of zeros at
	 *  the end of the distribution
	 */
	if (n < dist_size) {
		pr_inf_lock(lock, "%s: %12s %10s (all zeros hereafter)\n", name, "..", "..");
		pr_inf_lock(lock, "%s: %12s %10s\n", name, "..", "..");
		for (i = STRESS_MAXIMUM(dist_size - 3, n); i < dist_size; i++) {
			pr_inf_lock(lock, "%s: %12" PRIu64 " %10" PRId64 "\n",
				name, cyclic_dist * i, (int64_t)0);
		}
	}
}

/*
 *  stress_cyclic_supported()
 *      check if we can run this as root
 */
static int stress_cyclic_supported(void)
{
	if (!stress_check_capability(SHIM_CAP_SYS_NICE)) {
		pr_inf("stress-cyclic stressor needs to be run with CAP_SYS_NICE "
			"set SCHED_RR, SCHED_FIFO or SCHED_DEADLINE priorities, "
			"skipping this stressor\n");
                return -1;
        }
        return 0;
}

static int stress_cyclic(const args_t *args)
{
	const stress_cyclic_method_info_t *cyclic_method = &cyclic_methods[0];
	const uint32_t num_instances = args->num_instances;
	struct sigaction old_action_xcpu;
	struct rlimit rlim;
	pid_t pid;
	NOCLOBBER uint64_t timeout;
	uint64_t cyclic_sleep = DEFAULT_DELAY_NS;
	uint64_t cyclic_dist = 0;
	int32_t cyclic_prio = INT32_MAX;
	int policy;
	size_t cyclic_policy = 0;
	const double start = time_now();
	rt_stats_t *rt_stats;
	const size_t page_size = args->page_size;
	const size_t size = (sizeof(rt_stats_t) + page_size - 1) & (~(page_size - 1));
	cyclic_func func;

	timeout  = g_opt_timeout;
	(void)get_setting("cyclic-sleep", &cyclic_sleep);
	(void)get_setting("cyclic-prio", &cyclic_prio);
	(void)get_setting("cyclic-policy", &cyclic_policy);
	(void)get_setting("cyclic-dist", &cyclic_dist);
	(void)get_setting("cyclic-method", &cyclic_method);

	func = cyclic_method->func;
	policy = policies[cyclic_policy].policy;

	if (!args->instance) {
		if (num_policies == 0) {
			pr_inf("%s: no scheduling policies "
				"available, skipping test\n",
				args->name);
			return EXIT_NOT_IMPLEMENTED;
		}
	}

	if (g_opt_timeout == TIMEOUT_NOT_SET) {
		timeout = 60;
		pr_inf("%s: timeout has not been set, forcing timeout to "
			"be %" PRIu64 " seconds\n", args->name, timeout);
	}

	if ((num_instances > 1) && (args->instance == 0)) {
		pr_inf("%s: for best results, run just 1 instance of "
			"this stressor\n", args->name);
	}

	rt_stats = (rt_stats_t *)mmap(NULL, size, PROT_READ | PROT_WRITE,
			MAP_SHARED | MAP_ANONYMOUS, -1, 0);
	if (rt_stats == MAP_FAILED) {
		pr_inf("%s: mmap of shared policy data failed: %d (%s)\n",
			args->name, errno, strerror(errno));
		return EXIT_NO_RESOURCE;
	}
	rt_stats->min_ns = INT64_MAX;
	rt_stats->max_ns = INT64_MIN;
	rt_stats->ns = 0.0;
#if defined(HAVE_SCHED_GET_PRIORITY_MIN)
	rt_stats->min_prio = sched_get_priority_min(policy);
#else
	rt_stats->min_prio = 0;
#endif

#if defined(HAVE_SCHED_GET_PRIORITY_MIN)
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

	if (args->instance == 0)
		pr_dbg("%s: using method '%s'\n", args->name, cyclic_method->name);

	pid = fork();
	if (pid < 0) {
		pr_inf("%s: cannot fork, errno=%d (%s)\n",
			args->name, errno, strerror(errno));
		return EXIT_NO_RESOURCE;
	} else if (pid == 0) {
#if defined(HAVE_SCHED_GET_PRIORITY_MIN) &&	\
    defined(HAVE_SCHED_GET_PRIORITY_MAX)
		const pid_t mypid = getpid();
#endif
#if defined(HAVE_ATOMIC)
		uint32_t count;
#endif
		int ret;
		NOCLOBBER int rc = EXIT_FAILURE;

#if defined(HAVE_ATOMIC)
		__sync_fetch_and_add(&g_shared->softlockup_count, 1);

		/*
		 * Wait until all instances have reached this point
		 */
		do {
			if ((time_now() - start) > (double)timeout)
				goto tidy_ok;
			(void)usleep(50000);
			__atomic_load(&g_shared->softlockup_count, &count, __ATOMIC_RELAXED);
		} while (keep_stressing() && count < num_instances);
#endif

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

		if (stress_sighandler(args->name, SIGXCPU, stress_rlimit_handler, &old_action_xcpu) < 0)
			goto tidy;

		ret = sigsetjmp(jmp_env, 1);
		if (ret)
			goto tidy_ok;

#if defined(HAVE_SCHED_GET_PRIORITY_MIN) &&	\
    defined(HAVE_SCHED_GET_PRIORITY_MAX)
		ret = stress_set_sched(mypid, policy, rt_stats->max_prio, args->instance != 0);
		if (ret < 0) {
			if (errno != EPERM) {
				pr_fail("%s: sched_setscheduler "
					"failed: errno=%d (%s) "
					"for scheduler policy %s\n",
					args->name, errno, strerror(errno),
					policies[cyclic_policy].name);
			}
			goto tidy;
		}
#endif

		do {
			func(args, rt_stats, cyclic_sleep);
			inc_counter(args);

			/* Ensure we NEVER spin forever */
			if ((time_now() - start) > (double)timeout)
				break;
		} while (keep_stressing());

tidy_ok:
		rc = EXIT_SUCCESS;
tidy:
		(void)fflush(stdout);
		_exit(rc);
	} else {
		int status, ret;

		ret = stress_set_sched(args->pid, policy, rt_stats->max_prio, true);
		(void)ret;

		(void)pause();
		(void)kill(pid, SIGKILL);
#if defined(HAVE_ATOMIC)
		__sync_fetch_and_sub(&g_shared->softlockup_count, 1);
#endif

		(void)shim_waitpid(pid, &status, 0);
	}

	stress_rt_stats(rt_stats);

	if (args->instance  == 0) {
		if (rt_stats->index) {
			size_t i;
			bool lock = false;

			static const float percentiles[] = {
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

			pr_lock(&lock);
			pr_inf_lock(&lock, "%s: sched %s: %" PRIu64 " ns delay, %zd samples\n",
				args->name,
				policies[cyclic_policy].name,
				cyclic_sleep,
				rt_stats->index);
			pr_inf_lock(&lock, "%s:   mean: %.2f ns, mode: %" PRId64 " ns\n",
				args->name,
				rt_stats->latency_mean,
				rt_stats->latency_mode);
			pr_inf_lock(&lock, "%s:   min: %" PRId64 " ns, max: %" PRId64 " ns, std.dev. %.2f\n",
				args->name,
				rt_stats->min_ns,
				rt_stats->max_ns,
				rt_stats->std_dev);

			pr_inf_lock(&lock, "%s: latency percentiles:\n", args->name);
			for (i = 0; i < sizeof(percentiles) / sizeof(percentiles[0]); i++) {
				size_t j = (size_t)(((double)rt_stats->index * percentiles[i]) / 100.0);
				pr_inf_lock(&lock, "%s:   %5.2f%%: %10" PRId64 " ns\n",
					args->name,
					percentiles[i],
					rt_stats->latencies[j]);
			}
			stress_rt_dist(args->name, &lock, rt_stats, cyclic_dist);
			pr_unlock(&lock);
		} else {
			pr_inf("%s: %10s: no latency information available\n",
				args->name,
				policies[policy].name);
		}
	}

	(void)munmap((void *)rt_stats, size);

	return EXIT_SUCCESS;
}

static const opt_set_func_t opt_set_funcs[] = {
	{ OPT_cyclic_dist,	stress_set_cyclic_dist },
	{ OPT_cyclic_method,	stress_set_cyclic_method },
	{ OPT_cyclic_policy,	stress_set_cyclic_policy },
	{ OPT_cyclic_prio, 	stress_set_cyclic_prio },
	{ OPT_cyclic_sleep,	stress_set_cyclic_sleep },
	{ 0,			NULL }
};

stressor_info_t stress_cyclic_info = {
	.stressor = stress_cyclic,
	.supported = stress_cyclic_supported,
	.class = CLASS_SCHEDULER | CLASS_OS,
	.opt_set_funcs = opt_set_funcs,
	.help = help
};
