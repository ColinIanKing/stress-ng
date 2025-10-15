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

#include <sched.h>

#define STRESS_AFFINITY_PROCS	(16)

typedef struct {
	volatile uint32_t cpu;		/* Pinned CPU to use, in pin mode */
	uint32_t cpus;			/* Number of CPUs available */
	uint64_t affinity_delay;	/* Affinity nanosecond delay, 0 default */
	uint64_t affinity_sleep;	/* Affinity nanosecond delay, 0 default */
	bool	 affinity_rand;		/* True if --affinity-rand set */
	bool	 affinity_pin;		/* True if --affinity-pin set */
} stress_affinity_info_t;

static const stress_help_t help[] = {
	{ NULL,	"affinity N",	  	"start N workers that rapidly change CPU affinity" },
	{ NULL, "affinity-delay D",	"delay in nanoseconds between affinity changes" },
	{ NULL,	"affinity-ops N",	"stop after N affinity bogo operations" },
	{ NULL, "affinity-pin",		"keep per stressor threads pinned to same CPU" },
	{ NULL,	"affinity-rand",	"change affinity randomly rather than sequentially" },
	{ NULL,	"affinity-sleep N",	"sleep in nanoseconds between affinity changes" },
	{ NULL,	NULL,			NULL }
};

static const stress_opt_t opts[] = {
	{ OPT_affinity_delay, "affinity-delay", TYPE_ID_UINT64, 0, STRESS_NANOSECOND, NULL },
	{ OPT_affinity_pin,   "affinity-pin",	TYPE_ID_BOOL,	0, 1,                 NULL },
	{ OPT_affinity_rand,  "affinity-rand",  TYPE_ID_BOOL,	0, 1,                 NULL },
	{ OPT_affinity_sleep, "affinity-sleep", TYPE_ID_UINT64, 0, STRESS_NANOSECOND, NULL },
	END_OPT,
};

/*
 *  stress on sched_affinity()
 *	stress system by changing CPU affinity periodically
 */
#if defined(HAVE_SCHED_GETAFFINITY) &&	\
    defined(HAVE_SCHED_SETAFFINITY)

static void *counter_lock;	/* Counter lock */

/*
 *  stress_affinity_supported()
 *      check that we can set affinity
 */
static int stress_affinity_supported(const char *name)
{
	cpu_set_t mask;

	CPU_ZERO(&mask);

	if (sched_getaffinity(0, sizeof(mask), &mask) < 0) {
		pr_inf_skip("%s stressor cannot get CPU affinity, skipping the stressor\n", name);
		return -1;
	}
	if (sched_setaffinity(0, sizeof(mask), &mask) < 0) {
		if (errno == EPERM) {
			pr_inf_skip("%s stressor cannot set CPU affinity, "
			       "process lacks privilege, skipping the stressor\n", name);
			return -1;
		}
	}
	return 0;
}

/*
 *  stress_affinity_reap()
 *	kill and wait on child processes
 */
static void stress_affinity_reap(stress_args_t *args, const stress_pid_t *s_pids)
{
	stress_kill_and_wait_many(args, s_pids, STRESS_AFFINITY_PROCS, SIGALRM, true);
}

/*
 *  stress_affinity_spin_delay()
 *	delay by delay nanoseconds, spinning on rescheduling
 *	eat cpu cycles.
 */
static inline void stress_affinity_spin_delay(
	const uint64_t delay,
	const stress_affinity_info_t *info)
{
	const uint32_t cpu = info->cpu;
	const double end = stress_time_now() +
		((double)delay / (double)STRESS_NANOSECOND);

	while ((stress_time_now() < end) && (cpu == info->cpu))
		(void)shim_sched_yield();
}

/*
 *  stress_affinity_child()
 *	affinity stressor child process
 */
static void stress_affinity_child(
	stress_args_t *args,
	stress_affinity_info_t *info,
	const bool pin_controller)
{
	uint32_t cpu = args->instance, last_cpu = cpu;
	cpu_set_t mask0;
	bool stress_continue_affinity = true;
	const bool taskset_random = g_opt_flags & OPT_FLAGS_TASKSET_RANDOM;

	CPU_ZERO(&mask0);

	do {
		cpu_set_t mask;

		if (info->affinity_rand) {
			cpu = stress_mwc32modn(info->cpus);
			/* More than 2 cpus and same as last, move to next cpu */
			if ((cpu == last_cpu) && (info->cpus > 2))
				cpu = (cpu + 1) % info->cpus;
			last_cpu = cpu;
		} else {
			cpu = (cpu + 1) % info->cpus;
		}

		/*
		 *  In pin mode stressor instance 0 controls the CPU
		 *  to use, other instances use that CPU too
		 */
		if (info->affinity_pin) {
			if (pin_controller) {
				info->cpu = cpu;
				stress_asm_mb();
			} else {
				stress_asm_mb();
				cpu = info->cpu;
			}
		}
		CPU_ZERO(&mask);
		CPU_SET(cpu, &mask);
		if (UNLIKELY(sched_setaffinity(0, sizeof(mask), &mask) < 0)) {
			if (errno == EINVAL) {
				/*
				 * We get this if CPU is offline'd,
				 * and since that can be dynamically
				 * set, we should just retry
				 */
				goto affinity_continue;
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
				    (!taskset_random) &&
				    (!CPU_ISSET(cpu, &mask)))
					pr_fail("%s: failed to move " "to CPU %" PRIu32 "\n",
						args->name, cpu);
			}
		}
		if (g_opt_flags & OPT_FLAGS_AGGRESSIVE) {
			uint32_t next_cpu = (cpu + 1) % info->cpus;
			uint32_t prev_cpu = (cpu + info->cpus - 1) % info->cpus;

			CPU_ZERO(&mask);
			CPU_SET(next_cpu, &mask);
			(void)sched_setaffinity(0, sizeof(mask), &mask);
			(void)shim_sched_yield();

			CPU_ZERO(&mask);
			CPU_SET(stress_mwc32modn(info->cpus), &mask);
			(void)sched_setaffinity(0, sizeof(mask), &mask);
			(void)shim_sched_yield();

			CPU_ZERO(&mask);
			CPU_SET(next_cpu, &mask);
			(void)sched_setaffinity(0, sizeof(mask), &mask);
			(void)shim_sched_yield();

			CPU_ZERO(&mask);
			CPU_SET(prev_cpu, &mask);
			(void)sched_setaffinity(0, sizeof(mask), &mask);
			(void)shim_sched_yield();
		}

		/* Exercise getaffinity with invalid pid */
		VOID_RET(int, sched_getaffinity(-1, sizeof(mask), &mask));

		/* Exercise getaffinity with mask size */
		VOID_RET(int, sched_getaffinity(0, 0, &mask));

		/* Exercise setaffinity with invalid mask size */
		VOID_RET(int, sched_setaffinity(0, 0, &mask));

		/* Exercise setaffinity with invalid mask */
		VOID_RET(int, sched_setaffinity(0, sizeof(mask), &mask0));

affinity_continue:
		stress_continue_affinity = stress_bogo_inc_lock(args, counter_lock, true);
		if (!stress_continue_affinity)
			break;

		if (info->affinity_delay > 0)
			stress_affinity_spin_delay(info->affinity_delay, info);
		if (info->affinity_sleep > 0)
			(void)shim_nanosleep_uint64(info->affinity_sleep);
	} while (stress_continue(args));
}

static int stress_affinity(stress_args_t *args)
{
	stress_pid_t *s_pids, *s_pids_head = NULL;

	size_t i;
	stress_affinity_info_t *info;
	const size_t info_sz = (sizeof(*info) + args->page_size) & ~(args->page_size - 1);

	s_pids = stress_sync_s_pids_mmap(STRESS_AFFINITY_PROCS);
	if (s_pids == MAP_FAILED) {
		pr_inf_skip("%s: failed to mmap %d PIDs%s, skipping stressor\n",
			args->name, STRESS_AFFINITY_PROCS, stress_get_memfree_str());
		return EXIT_NO_RESOURCE;
	}

	counter_lock = stress_lock_create("counter");
	if (!counter_lock) {
		pr_inf_skip("%s: failed to create counter lock. skipping stressor\n", args->name);
		(void)stress_sync_s_pids_munmap(s_pids, STRESS_AFFINITY_PROCS);
		return EXIT_NO_RESOURCE;
	}

	info = (stress_affinity_info_t *)stress_mmap_populate(NULL,
			info_sz, PROT_READ | PROT_WRITE,
			MAP_SHARED | MAP_ANONYMOUS, -1, 0);
	if (info == MAP_FAILED) {
		pr_inf_skip("%s: cannot mmap %zu bytes for shared counters%s, skipping stressor\n",
			args->name, info_sz, stress_get_memfree_str());
		(void)stress_lock_destroy(counter_lock);
		(void)stress_sync_s_pids_munmap(s_pids, STRESS_AFFINITY_PROCS);
		return EXIT_NO_RESOURCE;
	}
	stress_set_vma_anon_name(info, info_sz, "counters");

	info->affinity_delay = 0;
	info->affinity_pin = false;
	info->affinity_rand = false;
	info->affinity_sleep = 0;
	info->cpus = (uint32_t)stress_get_processors_configured();

	(void)stress_get_setting("affinity-delay", &info->affinity_delay);
	(void)stress_get_setting("affinity-pin", &info->affinity_pin);
	(void)stress_get_setting("affinity-rand", &info->affinity_rand);
	(void)stress_get_setting("affinity-sleep", &info->affinity_sleep);

	/*
	 *  process slots 1..STRESS_AFFINITY_PROCS are the children,
	 *  slot 0 is the parent.
	 */
	for (i = 1; i < STRESS_AFFINITY_PROCS; i++) {
		stress_sync_start_init(&s_pids[i]);
		s_pids[i].pid = fork();

		if (s_pids[i].pid == 0) {
			s_pids[i].pid = getpid();

			stress_set_proc_state(args->name, STRESS_STATE_SYNC_WAIT);
			stress_sync_start_wait_s_pid(&s_pids[i]);
			stress_set_proc_state(args->name, STRESS_STATE_RUN);
			stress_affinity_child(args, info, false);
			_exit(EXIT_SUCCESS);
		} else if (s_pids[i].pid > 0) {
			stress_sync_start_s_pid_list_add(&s_pids_head, &s_pids[i]);
		}
	}

	stress_set_proc_state(args->name, STRESS_STATE_SYNC_WAIT);
	stress_sync_start_wait(args);
	stress_sync_start_cont_list(s_pids_head);
	stress_set_proc_state(args->name, STRESS_STATE_RUN);

	stress_affinity_child(args, info, true);
	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);

	/*
	 *  The first process to hit the bogo op limit or get a SIGALRM
	 *  will have reap'd the processes, but to be safe, reap again
	 *  to ensure all processes are really dead and reaped.
	 */
	stress_affinity_reap(args, s_pids);

	(void)munmap((void *)info, info_sz);
	(void)stress_lock_destroy(counter_lock);
	(void)stress_sync_s_pids_munmap(s_pids, STRESS_AFFINITY_PROCS);

	return EXIT_SUCCESS;
}

const stressor_info_t stress_affinity_info = {
	.stressor = stress_affinity,
	.classifier = CLASS_SCHEDULER,
	.supported = stress_affinity_supported,
	.opts = opts,
	.verify = VERIFY_OPTIONAL,
	.help = help,
};
#else
const stressor_info_t stress_affinity_info = {
	.stressor = stress_unimplemented,
	.classifier = CLASS_SCHEDULER,
	.opts = opts,
	.verify = VERIFY_OPTIONAL,
	.help = help,
	.unimplemented_reason = "built without sched_getaffinity() or sched_setaffinity()"
};
#endif
