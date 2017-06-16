/*
 * Copyright (C) 2013-2017 Canonical, Ltd.
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

#if defined(HAVE_LIB_PTHREAD) && (HAVE_SEM_POSIX)
#include <semaphore.h>
#endif

#include <math.h>

#define DEFAULT_DELAY_NS	(100000)
#define MAX_SAMPLES		(10000)
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

static const policy_t policies[] = {
#if defined(SCHED_DEADLINE)
	{ SCHED_DEADLINE,   "SCHED_DEADLINBE", "deadline" },
#endif
#if defined(SCHED_FIFO)
	{ SCHED_FIFO, "SCHED_FIFO", "fifo" },
#endif
#if defined(SCHED_RR)
	{ SCHED_RR,   "SCHED_RR", "rr" },
#endif
};

static const size_t num_policies = SIZEOF_ARRAY(policies);


void stress_set_cyclic_sleep(const char *opt)
{
	uint64_t cyclic_sleep;

	cyclic_sleep = get_uint64(opt);
        check_range_bytes("cyclic-sleep", cyclic_sleep,
                1, NANOSECS);
        set_setting("cyclic-sleep", TYPE_ID_UINT64, &cyclic_sleep);
}

int stress_set_cyclic_policy(const char *opt)
{
	size_t policy;

	for (policy = 0; policy < num_policies; policy++) {
		if (!strcmp(opt, policies[policy].opt_name)) {
			set_setting("cyclic-policy", TYPE_ID_SIZE_T, &policy);
			return 0;
		}
	}
	fprintf(stderr, "invalid cyclic-policy '%s', policies allowed are:", opt);
	for (policy = 0; policy < num_policies; policy++) {
		fprintf(stderr, " %s", policies[policy].opt_name);
	}
	fprintf(stderr, "\n");
	return -1;
}

void stress_set_cyclic_prio(const char *opt)
{
	int32_t cyclic_prio;

	cyclic_prio = get_int32(opt);
        check_range_bytes("cyclic-prio", cyclic_prio, 1, 100);
        set_setting("cyclic-prio", TYPE_ID_INT32, &cyclic_prio);
}

/*
 *  stress_cyclic_supported()
 *      check if we can run this as root
 */
int stress_cyclic_supported(void)
{
        if (geteuid() != 0) {
		pr_inf("stress-cyclic stressor needs to be run as root to "
			"set SCHED_RR or SCHED_FIFO priorities, "
			"skipping this stressor\n");
                return -1;
        }
        return 0;
}

#if defined(__linux__)

static sigjmp_buf jmp_env;

/*
 *  stress_rlimit_handler()
 *      rlimit generic handler
 */
static void MLOCKED stress_rlimit_handler(int dummy)
{
	(void)dummy;

	g_keep_stressing_flag = 1;
	siglongjmp(jmp_env, 1);
}

/*
 *  stress_cyclic_cmp()
 *	sort latencies into order, least first
 */
int stress_cyclic_cmp(const void *p1, const void *p2)
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
void stress_rt_stats(rt_stats_t *rt_stats)
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

int stress_cyclic(const args_t *args)
{
	const uint32_t num_instances = args->num_instances;
	struct sigaction old_action_xcpu;
	struct sched_param param = { 0 };
	struct rlimit rlim;
	pid_t pid;
	NOCLOBBER uint64_t timeout;
	uint64_t cyclic_sleep = DEFAULT_DELAY_NS;
	int32_t cyclic_prio = INT32_MAX;
	int policy;
	size_t cyclic_policy = 0;
	const double start = time_now();
	rt_stats_t *rt_stats;
	const size_t page_size = args->page_size;
	const size_t size = (sizeof(rt_stats_t) + page_size - 1) & (~(page_size - 1));

	timeout  = g_opt_timeout;
        (void)get_setting("cyclic-sleep", &cyclic_sleep);
        (void)get_setting("cyclic-prio", &cyclic_prio);
	(void)get_setting("cyclic-policy", &cyclic_policy);

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

	rt_stats = mmap(NULL, size, PROT_READ | PROT_WRITE,
			MAP_SHARED | MAP_ANONYMOUS, -1, 0);
	if (rt_stats == MAP_FAILED) {
		pr_inf("%s: mmap of shared policy data failed: %d (%s)\n",
			args->name, errno, strerror(errno));
		return EXIT_NO_RESOURCE;
	}
	rt_stats->min_ns = INT64_MAX;
	rt_stats->max_ns = INT64_MIN;
	rt_stats->ns = 0.0;
	rt_stats->min_prio = sched_get_priority_min(policy);
	rt_stats->max_prio = sched_get_priority_max(policy);
	/* If user has set max priority.. */
	if (cyclic_prio != INT32_MAX) {
		if (rt_stats->max_prio > cyclic_prio) {
			rt_stats->max_prio = cyclic_prio;
		}
	}

	pid = fork();
	if (pid < 0) {
		pr_inf("%s: cannot fork, errno=%d (%s)\n",
			args->name, errno, strerror(errno));
		return EXIT_NO_RESOURCE;
	} else if (pid == 0) {
		const pid_t mypid = getpid();
		uint32_t count;
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
			usleep(50000);
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

		rlim.rlim_cur = 1000000 * timeout;
		rlim.rlim_max = 1000000 * timeout;
		(void)setrlimit(RLIMIT_RTTIME, &rlim);

		if (stress_sighandler(args->name, SIGXCPU, stress_rlimit_handler, &old_action_xcpu) < 0)
			goto tidy;

		ret = sigsetjmp(jmp_env, 1);
		if (ret)
			goto tidy_ok;

		param.sched_priority = rt_stats->max_prio;
		ret = sched_setscheduler(mypid, policy, &param);
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

		do {
			struct timespec t1, t2, t, trem;
			double ns = 0.0;

			t.tv_sec = cyclic_sleep / NANOSECS;
			t.tv_nsec = cyclic_sleep % NANOSECS;
			clock_gettime(CLOCK_REALTIME, &t1);
			ret = clock_nanosleep(CLOCK_REALTIME, 0, &t, &trem);
			clock_gettime(CLOCK_REALTIME, &t2);
			if (ret == 0) {
				int64_t delta_ns;

				delta_ns = ((t2.tv_sec - t1.tv_sec) * NANOSECS) + (t2.tv_nsec - t1.tv_nsec);
				delta_ns -= cyclic_sleep;

				ns += delta_ns;

				if (rt_stats->index < MAX_SAMPLES)
					rt_stats->latencies[rt_stats->index++] = delta_ns;

				rt_stats->ns += ns;
			}
			inc_counter(args);

			/* Ensure we NEVER spin forever */
			if ((time_now() - start) > (double)timeout)
				break;
		} while (keep_stressing());

tidy_ok:
		rc = EXIT_SUCCESS;
tidy:
		fflush(stdout);
		_exit(rc);
	} else {
		int status;

		param.sched_priority = rt_stats->max_prio;
		(void)sched_setscheduler(args->pid, policy, &param);

		pause();
		kill(pid, SIGKILL);
#if defined(HAVE_ATOMIC)
		__sync_fetch_and_sub(&g_shared->softlockup_count, 1);
#endif

		(void)waitpid(pid, &status, 0);
	}

	stress_rt_stats(rt_stats);

	if (rt_stats->index && (args->instance == 0)) {
		size_t i;

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

		pr_inf("%s: sched %s: %" PRIu64 " ns delay, %zd samples\n",
			args->name,
			policies[cyclic_policy].name,
			cyclic_sleep,
			rt_stats->index);
		pr_inf("%s:   mean: %.2f ns, mode: %" PRId64 " ns\n",
			args->name,
			rt_stats->latency_mean,
			rt_stats->latency_mode);
		pr_inf("%s:   min: %" PRId64 " ns, max: %" PRId64 " ns, std.dev. %.2f\n",
			args->name,
			rt_stats->min_ns,
			rt_stats->max_ns,
			rt_stats->std_dev);

		pr_inf("%s: latencies:\n", args->name);
		for (i = 0; i < sizeof(percentiles) / sizeof(percentiles[0]); i++) {
			size_t j = (size_t)(((double)rt_stats->index * percentiles[i]) / 100.0);
			pr_inf("%s:   %5.2f%%: %10" PRId64 " us\n",
				args->name,
				percentiles[i],
				rt_stats->latencies[j]);
		}
	} else {
		pr_inf("%s: %10s: no latency information available\n",
			args->name,
			policies[policy].name);
	}

	(void)munmap(rt_stats, size);

	return EXIT_SUCCESS;
}
#else
int stress_cyclic(const args_t *args)
{
	return stress_not_implemented(args);
}
#endif

