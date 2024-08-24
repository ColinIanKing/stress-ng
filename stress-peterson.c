/*
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
#include "core-affinity.h"
#include "core-arch.h"
#include "core-cpu-cache.h"
#include "core-killpid.h"

static const stress_help_t help[] = {
	{ NULL,	"peterson N",		"start N workers that exercise Peterson's algorithm" },
	{ NULL,	"peterson-ops N",	"stop after N peterson mutex bogo operations" },
	{ NULL,	NULL,			NULL }
};

#if defined(HAVE_SHIM_MFENCE)

typedef struct {
	volatile int	turn;
	volatile int 	check;
	volatile bool	flag[2];
} peterson_mutex_t;

/*
 *  peterson_t is mmap'd shared and m, p0, p1 are 64 byte cache aligned
 *  to reduce cache contention when updating metrics on p0 and p1
 */
typedef struct peterson {
	peterson_mutex_t	m;
	char 			pad1[64 - sizeof(peterson_mutex_t)];
	stress_metrics_t	p0;
	char 			pad2[64 - sizeof(stress_metrics_t)];
	stress_metrics_t	p1;
} peterson_t;

static peterson_t *peterson;

static int stress_peterson_p0(stress_args_t *args)
{
	int check0, check1;
	double t;

	t = stress_time_now();
	peterson->m.flag[0] = true;
	peterson->m.turn = 1;
	shim_mfence();
	while (peterson->m.flag[1] && (peterson->m.turn == 1)) {
#if defined(STRESS_ARCH_RISCV)
		shim_sched_yield();
#endif
	}

	/* Critical section */
	check0 = peterson->m.check;
	peterson->m.check++;
	check1 = peterson->m.check;
#if defined(STRESS_ARCH_ARM)
	shim_mfence();
#endif

	peterson->m.flag[0] = false;
	shim_mfence();
	peterson->p0.duration += stress_time_now() - t;
	peterson->p0.count += 1.0;

	if (check0 + 1 != check1) {
		pr_fail("%s p0: peterson mutex check failed %d vs %d\n",
			args->name, check0 + 1, check1);
		return EXIT_FAILURE;
	}
	return EXIT_SUCCESS;
}

static int stress_peterson_p1(stress_args_t *args)
{
	int check0, check1;
	double t;

	t = stress_time_now();
	peterson->m.flag[1] = true;
	peterson->m.turn = 0;
	shim_mfence();
	while (peterson->m.flag[0] && (peterson->m.turn == 0)) {
#if defined(STRESS_ARCH_RISCV)
		shim_sched_yield();
#endif
	}

	/* Critical section */
	check0 = peterson->m.check;
	peterson->m.check--;
	check1 = peterson->m.check;
#if defined(STRESS_ARCH_ARM)
	shim_mfence();
#endif
	stress_bogo_inc(args);

	peterson->m.flag[1] = false;
	shim_mfence();
	peterson->p1.duration += stress_time_now() - t;
	peterson->p1.count += 1.0;

	if (check0 - 1 != check1) {
		pr_fail("%s p1: peterson mutex check failed %d vs %d\n",
			args->name, check0 - 1, check1);
		return EXIT_FAILURE;
	}
	return EXIT_SUCCESS;
}

/*
 *  stress_peterson()
 *	stress peterson algorithm
 */
static int stress_peterson(stress_args_t *args)
{
	const size_t sz = STRESS_MAXIMUM(args->page_size, sizeof(*peterson));
	pid_t pid;
	double duration, count, rate;
	int parent_cpu, rc = EXIT_SUCCESS;

	peterson = (peterson_t *)stress_mmap_populate(NULL, sz,
			PROT_READ | PROT_WRITE,
			MAP_ANONYMOUS | MAP_SHARED, -1, 0);
	if (peterson == MAP_FAILED) {
		pr_inf_skip("%s: cannot mmap %zd bytes for bekker shared struct, skipping stressor\n",
			args->name, sz);
		return EXIT_NO_RESOURCE;
	}

	stress_set_proc_state(args->name, STRESS_STATE_SYNC_WAIT);
	stress_sync_start_wait(args);
	stress_set_proc_state(args->name, STRESS_STATE_RUN);

	peterson->m.flag[0] = false;
	peterson->m.flag[1] = false;

	parent_cpu = stress_get_cpu();
	pid = fork();
	if (pid < 0) {
		pr_inf_skip("%s: cannot create child process, skipping stressor\n", args->name);
		return EXIT_NO_RESOURCE;
	} else if (pid == 0) {
		/* Child */
		(void)stress_change_cpu(args, parent_cpu);
		while (stress_continue(args)) {
			rc = stress_peterson_p0(args);
			if (rc != EXIT_SUCCESS)
				break;
		}
		_exit(rc);
	} else {
		int status;

		/* Parent */
		while (stress_continue(args)) {
			rc = stress_peterson_p1(args);
			if (rc != EXIT_SUCCESS)
				break;
		}
		if (stress_kill_pid_wait(pid, &status) >= 0) {
			if (WIFEXITED(status))
				rc = WEXITSTATUS(status);
                }
	}

	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);

	duration = peterson->p0.duration + peterson->p1.duration;
	count = peterson->p0.count + peterson->p1.count;
	rate = (count > 0.0) ? (duration / count) : 0.0;
	stress_metrics_set(args, 0, "nanosecs per mutex",
		rate * STRESS_DBL_NANOSECOND, STRESS_METRIC_HARMONIC_MEAN);

	(void)munmap((void *)peterson, sz);

	return rc;
}

stressor_info_t stress_peterson_info = {
	.stressor = stress_peterson,
	.class = CLASS_CPU_CACHE | CLASS_IPC,
	.verify = VERIFY_ALWAYS,
	.help = help
};

#else

stressor_info_t stress_peterson_info = {
	.stressor = stress_unimplemented,
	.class = CLASS_CPU | CLASS_IPC,
	.verify = VERIFY_ALWAYS,
	.help = help,
	.unimplemented_reason = "built without user space memory fencing"
};

#endif
