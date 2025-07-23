/*
 * Copyright (C) 2024-2025Tejun Heo
 * Copyright (C) 2024-2025Colin Ian King
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
 * Based on flipflop source kindly provided by Tejun Heo
 *
 */
#include "stress-ng.h"
#include "core-affinity.h"
#include "core-attribute.h"
#include "core-builtin.h"
#include "core-pthread.h"
#include "core-time.h"

#include <sched.h>

#define BOGO_SCALE		(100000)
#define MIN_FLIPFLOP_BITS	(1)
#define MAX_FLIPFLOP_BITS	(65536)

static const stress_opt_t opts[] = {
	{ OPT_flipflop_taskset1, "flipflop-taskset1", TYPE_ID_STR, 0, 0, NULL },
	{ OPT_flipflop_taskset2, "flipflop-taskset2", TYPE_ID_STR, 0, 0, NULL },
	{ OPT_flipflop_bits,     "flipflop-bits",     TYPE_ID_UINT32, MIN_FLIPFLOP_BITS, MAX_FLIPFLOP_BITS, NULL },
	END_OPT,
};

static const stress_help_t help[] = {
	{ NULL,	"flipflop N",           "start N workers exercising flipflop" },
	{ NULL,	"flipflop-bits N",      "number of bits to be exercised by 2 x N x pthreads" },
	{ NULL, "flipflop-taskset1 S1", "list of CPUs to pin N ping threads to" },
	{ NULL, "flipflop-taskset2 S2", "list of CPUs to pin N pong threads to" },
	{ NULL,	"flipflop-ops N",       "stop after N flipflop bogo operations" },
	{ NULL,	NULL,		        NULL }
};

#if defined(HAVE_LIB_PTHREAD) && 	\
    defined(HAVE_CPU_SET_T) &&		\
    defined(HAVE_SCHED_SETAFFINITY) &&	\
    defined(HAVE_SYNC_VAL_COMPARE_AND_SWAP)

struct stress_flipflop_worker {
	uint64_t *word;			/* word being twiddled */
	uint64_t and_mask;		/* mask, and ops */
	uint64_t or_mask;		/* mask, or ops */

	uint64_t nr_max_loops;		/* max nr_loops, 0 for no limit */
	uint64_t nr_loops;		/* number of loops */
	uint64_t nr_tries;		/* number of tries */
	uint64_t nr_successes;		/* number of successes */

	cpu_set_t *cpus;		/* cpu set bitmask */
	pthread_t thread;		/* pthread handle */
	int thread_ret;			/* return from pthread create */
	pid_t ppid;			/* controlling parent pid */
	bool *worker_hold;		/* hold flag */
	bool *worker_exit;		/* exit flag */
} ALIGNED64;

typedef struct stress_flipflop_worker stress_flipflop_worker_t;

/*
 *  stress_flipflop_sigusr1_handler()
 *	handler to interrupt parent pause() waits
 */
static void stress_flipflop_sigusr1_handler(int signum)
{
	(void)signum;
}

static void *stress_flipflop_worker(void *arg)
{
	stress_flipflop_worker_t *w = arg;
	const bool check_max_loops = (w->nr_max_loops > 0);

	(void)sched_setaffinity(0, sizeof(*(w->cpus)), w->cpus);

	/* wait on hold or until finished flag */
	while (*(volatile bool *)w->worker_hold) {
		if (UNLIKELY(!stress_continue_flag()))
			return &g_nowt;
	}

	while (!*(volatile bool *)w->worker_exit) {
		uint64_t old = *((volatile uint64_t *)w->word);
		uint64_t new = (old & w->and_mask) | w->or_mask;
		uint64_t ret;

		w->nr_loops++;
		if ((UNLIKELY(check_max_loops) && (w->nr_loops >= w->nr_max_loops)))
			break;

		if (old == new)
			continue;

		ret = __sync_val_compare_and_swap(w->word, old, new);
		w->nr_tries++;
		if (ret == old)
			w->nr_successes++;
		if (UNLIKELY(!stress_continue_flag()))
			break;
	}

	/* Interrupt parent in sleep */
	(void)kill(w->ppid, SIGUSR1);
	return &g_nowt;
}

/*
 *  stress_flipflop_create_workers()
 *	create worker pthreads
 */
static int stress_flipflop_create_workers(
	const uint64_t max_ops,
	stress_flipflop_worker_t *workers,
	uint64_t *bits,
	const uint32_t flipflop_bits,
	const bool set_or_clear,
	cpu_set_t *cpus,
	bool *worker_hold,
	bool *worker_exit)
{
	uint32_t i;

	for (i = 0; i < flipflop_bits; i++) {
		stress_flipflop_worker_t *w = &workers[i];
		const uint32_t idx = i / 64;
		const uint32_t bit = i % 64;

		w->thread_ret = -1;
		w->nr_max_loops = max_ops;
		w->ppid = getpid();
		w->cpus = cpus;
		w->worker_hold = worker_hold;
		w->worker_exit = worker_exit;
		w->word = &bits[idx];
		if (set_or_clear) {
			w->and_mask = -1LLU;
			w->or_mask = 1LLU << bit;
		} else {
			w->and_mask = ~(1LLU << bit);
			w->or_mask = 0;
		}
	}

	for (i = 0; i < flipflop_bits; i++) {
		stress_flipflop_worker_t *w = &workers[i];

		w->thread_ret = pthread_create(&w->thread, NULL, stress_flipflop_worker, w);
		if (w->thread_ret != 0)
			return -1;
	}
	return 0;
}

/*
 *  stress_flipflop_uint64_cmp()
 *	qsort uint64 compare
 */
static int stress_flipflop_uint64_cmp(const void *a, const void *b)
{
	return (int64_t)(*(const uint64_t *)a - *(const uint64_t *)b);
}

/*
 *  stress_flipflop_set_cpuset()
 *	enable all bits in cpu_set_t set
 */
static void stress_flipflop_set_cpuset(cpu_set_t *set, const int num_cpus)
{
	int i;

	for (i = 0; i < num_cpus; i++)
		CPU_SET(i, set);
}

/*
 *  stress_flipflop
 *	stress flipflop scheduling
 */
static int stress_flipflop(stress_args_t *args)
{
	const int num_cpus = stress_get_processors_configured();
	double t_begin, duration;
	stress_flipflop_worker_t *workers;
	uint64_t bogo_ops, *bits, *dist;
	bool worker_hold = true, worker_exit = false, all_done;
	uint32_t flipflop_bits = (uint32_t)num_cpus, i;
	cpu_set_t cpus_a, cpus_b;
	uint64_t nr_loops = 0, nr_tries = 0, nr_successes = 0, max_ops;
	size_t bits_size;
	int rc = EXIT_SUCCESS, setbits;
	char *flipflop_taskset1 = NULL, *flipflop_taskset2 = NULL;
	const bool loop_until_max_ops = (args->bogo.max_ops > 0);

	if (!stress_get_setting("flipflop-bits", &flipflop_bits)) {
		if (g_opt_flags & OPT_FLAGS_MAXIMIZE)
			flipflop_bits = MAX_FLIPFLOP_BITS;
		if (g_opt_flags & OPT_FLAGS_MINIMIZE)
			flipflop_bits = MIN_FLIPFLOP_BITS;
	}
	(void)stress_get_setting("flipflop-taskset1", &flipflop_taskset1);
	(void)stress_get_setting("flipflop-taskset2", &flipflop_taskset2);

	/* Should never happen, keeps static analyzer happy */
	if (flipflop_bits < 1) {
		pr_inf("%s: flipflop-bits less than one, aborting\n", args->name);
		return EXIT_FAILURE;
	}

	if (stress_sighandler(args->name, SIGUSR1, stress_flipflop_sigusr1_handler, NULL))
		return EXIT_NO_RESOURCE;

	dist = calloc(2 * flipflop_bits, sizeof(uint64_t));
	if (!dist) {
		pr_inf_skip("%s: failed to allocate dist array%s, skipping stressor\n",
			args->name, stress_get_memfree_str());
		return EXIT_NO_RESOURCE;
	}

	setbits = 0;
	CPU_ZERO(&cpus_a);
	if (flipflop_taskset1)
		(void)stress_parse_cpu_affinity(flipflop_taskset1, &cpus_a, &setbits);
	if (!setbits)
		stress_flipflop_set_cpuset(&cpus_a, num_cpus);

	setbits = 0;
	CPU_ZERO(&cpus_b);
	if (flipflop_taskset2)
		(void)stress_parse_cpu_affinity(flipflop_taskset2, &cpus_b, &setbits);
	if (!setbits)
		stress_flipflop_set_cpuset(&cpus_b, num_cpus);

	pr_dbg("%s: flipflop_bits=%u, taskset1=%u taskset=%u\n", args->name,
		flipflop_bits, CPU_COUNT(&cpus_a), CPU_COUNT(&cpus_b));

	bits_size = ((flipflop_bits + 63) / 64) * 8;
	bits = calloc(bits_size, 1);
	if (!bits) {
		pr_inf_skip("%s: failed to allocate %zu bytes%s, skipping stressor\n",
			args->name, bits_size, stress_get_memfree_str());
		rc = EXIT_NO_RESOURCE;
		goto free_dist;
	}

	max_ops = (args->bogo.max_ops * BOGO_SCALE) / (2 * flipflop_bits);
	workers = (stress_flipflop_worker_t *)calloc(2 * flipflop_bits, sizeof(stress_flipflop_worker_t));
	if (!workers) {
		pr_inf_skip("%s: failed to allocate workers array%s, skipping stressor\n",
			args->name, stress_get_memfree_str());
		rc = EXIT_NO_RESOURCE;
		goto free_bits;
	}

	stress_set_proc_state(args->name, STRESS_STATE_SYNC_WAIT);
	stress_sync_start_wait(args);
	stress_set_proc_state(args->name, STRESS_STATE_RUN);

	if (stress_flipflop_create_workers(max_ops, workers,
			bits, flipflop_bits, false, &cpus_a, &worker_hold,
			&worker_exit) < 0)
		goto free_workers;
	if (stress_flipflop_create_workers(max_ops, workers + flipflop_bits,
			bits, flipflop_bits, true, &cpus_b, &worker_hold,
			&worker_exit) < 0)
		goto free_workers;

	t_begin = stress_time_now();
	*(volatile bool *)&worker_hold = false;

	do {
		all_done = true;
		bogo_ops = 0;

		/* wait for SIGALRM or SIGUSR1 */
		shim_pause();

		for (i = 0; i < 2 * flipflop_bits; i++) {
			bogo_ops += workers[i].nr_loops;
			if (loop_until_max_ops && (workers[i].nr_loops < max_ops))
				all_done = false;
		}

		if (loop_until_max_ops && all_done) {
			bogo_ops = args->bogo.max_ops * BOGO_SCALE;
			break;
		}
	} while (stress_continue(args));

	stress_bogo_set(args, bogo_ops / BOGO_SCALE);

	*(volatile bool *)&worker_exit = true;
	duration = stress_time_now() - t_begin;

	for (i = 0; i < 2 * flipflop_bits; i++) {
		stress_flipflop_worker_t *w = &workers[i];

		if (!w->thread_ret)
			(void)pthread_join(w->thread, NULL);

		w->thread_ret = -1;
		nr_loops += w->nr_loops;
		nr_tries += w->nr_tries;
		nr_successes += w->nr_successes;

		dist[i] = w->nr_successes;
	}

	if (stress_instance_zero(args)) {
		qsort(dist, 2 * flipflop_bits, sizeof(uint64_t), stress_flipflop_uint64_cmp);

		pr_inf("%s: ran for %.2lfs loops/tries/successes = %" PRIu64 " / %" PRIu64
			" (%2.02lf%%) / %" PRIu64 " (%2.02lf%%)\n",
			args->name, duration, nr_loops, nr_tries,
			100.0 * (double)nr_tries / (double)nr_loops,
			nr_successes,
			100.0 * (double)nr_successes / (double)nr_tries);
		pr_inf("%s: QPS loops/tries/successes = %.02lf / %.02lf / %.02lf\n",
			args->name, nr_loops / duration, nr_tries / duration, nr_successes / duration);
		pr_inf("%s: QPS min/p25/p50/p75/max = %.02lf / %.02lf / %.02lf / %.02lf / %.02lf\n",
			args->name,
			(double)dist[0] / duration,
			(double)dist[flipflop_bits / 2] / duration,
			(double)dist[flipflop_bits - 1] / duration,
			(double)dist[flipflop_bits + flipflop_bits / 2] / duration,
			(double)dist[2 * flipflop_bits - 1] / duration);
	}

	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);

free_workers:
	for (i = 0; i < 2 * flipflop_bits; i++) {
		stress_flipflop_worker_t *w = &workers[i];

		if (!w->thread_ret)
			(void)pthread_join(w->thread, NULL);
	}
	free(workers);
free_bits:
	free(bits);
free_dist:
	free(dist);

	return rc;
}

const stressor_info_t stress_flipflop_info = {
	.stressor = stress_flipflop,
	.classifier = CLASS_SCHEDULER | CLASS_OS,
	.verify = VERIFY_NONE,
	.opts = opts,
	.help = help
};

#else

const stressor_info_t stress_flipflop_info = {
	.stressor = stress_unimplemented,
	.classifier = CLASS_SCHEDULER | CLASS_OS,
	.opts = opts,
	.verify = VERIFY_NONE,
	.help = help,
	.unimplemented_reason = "built without pthread support, __sync_val_compare_and_swap(), cpu_set_t or sched_setaffinity()"
};

#endif
