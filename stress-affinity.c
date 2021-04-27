/*
 * Copyright (C) 2013-2021 Canonical, Ltd.
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

#define STRESS_AFFINITY_PROCS	(16)

static const stress_help_t help[] = {
	{ NULL,	"affinity N",	 "start N workers that rapidly change CPU affinity" },
	{ NULL,	"affinity-ops N","stop after N affinity bogo operations" },
	{ NULL,	"affinity-rand", "change affinity randomly rather than sequentially" },
	{ NULL,	NULL,		 NULL }
};

static int stress_set_affinity_rand(const char *opt)
{
	bool affinity_rand = true;

	(void)opt;
	return stress_set_setting("affinity-rand", TYPE_ID_BOOL, &affinity_rand);
}

static const stress_opt_set_func_t opt_set_funcs[] = {
	{ OPT_affinity_rand,    stress_set_affinity_rand },
	{ 0,			NULL }
};

/*
 *  stress on sched_affinity()
 *	stress system by changing CPU affinity periodically
 */

#if defined(HAVE_AFFINITY) && \
    defined(HAVE_SCHED_GETAFFINITY)

/*
 *  stress_affinity_supported()
 *      check that we can set affinity
 */
static int stress_affinity_supported(const char *name)
{
	cpu_set_t mask;

	CPU_ZERO(&mask);

	if (sched_getaffinity(0, sizeof(mask), &mask) < 0) {
		pr_inf("%s stressor cannot get CPU affinity, skipping the stressor\n", name);
		return -1;
	}
	if (sched_setaffinity(0, sizeof(mask), &mask) < 0) {
		if (errno == EPERM) {
			pr_inf("%s stressor cannot set CPU affinity, "
			       "process lacks privilege, skipping the stressor\n", name);
			return -1;
		}
	}
	return 0;
}

static void stress_affinity_reap(pid_t *pids)
{
	size_t i;
	const pid_t mypid = getpid();

	/*
	 *  Kill and reap children
	 */
	for (i = 1; i < STRESS_AFFINITY_PROCS; i++) {
		if ((pids[i] > 1) && (pids[i] != mypid))
			kill(pids[i], SIGKILL);
	}
	for (i = 1; i < STRESS_AFFINITY_PROCS; i++) {
		if ((pids[i] > 1) && (pids[i] != mypid)) {
			int status;

			(void)waitpid(pids[i], &status, 0);
		}
	}
}

/*
 *  stress_affinity_racy_count()
 *	racy bogo op counter, we have a lot of contention
 *	if we lock the args->counter, so sum per-process
 *	counters in a racy way.
 */
static uint64_t stress_affinity_racy_count(uint64_t *counters)
{
	register uint64_t count = 0;
	register size_t i;

	for (i = 0; i < STRESS_AFFINITY_PROCS; i++)
		count += counters[i];

	return count;
}

/*
 *  affinity_keep_stressing(args)
 *	check if SIGALRM has triggered to the bogo ops count
 *	has been reached, counter is racy, but that's OK
 */
static bool HOT OPTIMIZE3 affinity_keep_stressing(
	const stress_args_t *args,
	uint64_t *counters)
{
	return (LIKELY(g_keep_stressing_flag) &&
		LIKELY(!args->max_ops ||
                (stress_affinity_racy_count(counters) < args->max_ops)));
}

static void stress_affinity_child(
	const stress_args_t *args,
	const bool affinity_rand,
	const uint32_t cpus,
	uint64_t *counters,
	pid_t *pids,
	size_t instance)
{
	uint32_t cpu = args->instance;
	cpu_set_t mask0;

	CPU_ZERO(&mask0);

	do {
		cpu_set_t mask;
		int ret;

		cpu = affinity_rand ? (stress_mwc32() >> 4) : cpu + 1;
		cpu %= cpus;
		CPU_ZERO(&mask);
		CPU_SET(cpu, &mask);
		if (sched_setaffinity(0, sizeof(mask), &mask) < 0) {
			if (errno == EINVAL) {
				/*
				 * We get this if CPU is offline'd,
				 * and since that can be dynamically
				 * set, we should just retry
				 */
				continue;
			}
			pr_fail("%s: failed to move to CPU %" PRIu32 ", errno=%d (%s)\n",
				args->name, cpu, errno, strerror(errno));
			(void)shim_sched_yield();
		} else {
			/* Now get and check */
			CPU_ZERO(&mask);
			CPU_SET(cpu, &mask);
			if (sched_getaffinity(0, sizeof(mask), &mask) == 0) {
				if ((g_opt_flags & OPT_FLAGS_VERIFY) &&
				    (!CPU_ISSET(cpu, &mask)))
					pr_fail("%s: failed to move " "to CPU %" PRIu32 "\n",
						args->name, cpu);
			}
		}
		/* Exercise getaffinity with invalid pid */
		ret = sched_getaffinity(-1, sizeof(mask), &mask);
		(void)ret;

		/* Exercise getaffinity with mask size */
		ret = sched_getaffinity(0, 0, &mask);
		(void)ret;

		/* Exercise setaffinity with invalid mask size */
		ret = sched_setaffinity(0, 0, &mask);
		(void)ret;

		/* Exercise setaffinity with invalid mask */
		ret = sched_setaffinity(0, sizeof(mask), &mask0);
		(void)ret;

		counters[instance]++;
	} while (affinity_keep_stressing(args, counters));

	stress_affinity_reap(pids);
}

static int stress_affinity(const stress_args_t *args)
{
	const uint32_t cpus = (uint32_t)stress_get_processors_configured();
	bool affinity_rand = false;
	pid_t pids[STRESS_AFFINITY_PROCS];
	size_t i;
	size_t counters_sz = ((sizeof(uint64_t) * (STRESS_AFFINITY_PROCS)) + args->page_size)
				& ~(args->page_size - 1);
	uint64_t *counters;

	(void)stress_get_setting("affinity-rand", &affinity_rand);

	counters = (uint64_t *)mmap(NULL, counters_sz, PROT_READ | PROT_WRITE,
			MAP_SHARED | MAP_ANONYMOUS, -1, 0);
	if (!counters) {
		pr_inf("%s: cannot mmap %zd bytes for shared counters, skipping stressor\n",
			args->name, counters_sz);
		return EXIT_NO_RESOURCE;
	}

	(void)memset(pids, 0, sizeof(pids));

	/*
	 *  process slots 1..STRESS_AFFINITY_PROCS are the children,
	 *  slot 0 is the parent.
	 */
	for (i = 1; i < STRESS_AFFINITY_PROCS; i++) {
		pids[i] = fork();

		if (pids[i] == 0) {
			stress_affinity_child(args, affinity_rand, cpus, counters, pids, i);
			_exit(EXIT_SUCCESS);
		}
	}

	stress_set_proc_state(args->name, STRESS_STATE_RUN);
	stress_affinity_child(args, affinity_rand, cpus, counters, pids, 0);
	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);

	/*
	 *  The first process to hit the bogo op limit or get a SIGALRM
	 *  will have reap'd the processes, but to be safe, reap again
	 *  to ensure all processes are really dead and reaped.
	 */
	stress_affinity_reap(pids);

	/*
	 *  Set counter, this is always going to be >= the bogo_ops
	 *  threshold because it is racy, but that is OK
	 */
	set_counter(args, stress_affinity_racy_count(counters));

	(void)munmap((void *)counters, counters_sz);

	return EXIT_SUCCESS;
}

stressor_info_t stress_affinity_info = {
	.stressor = stress_affinity,
	.class = CLASS_SCHEDULER,
	.supported = stress_affinity_supported,
	.opt_set_funcs = opt_set_funcs,
	.help = help
};
#else
stressor_info_t stress_affinity_info = {
	.stressor = stress_not_implemented,
	.class = CLASS_SCHEDULER,
	.opt_set_funcs = opt_set_funcs,
	.help = help
};
#endif
