/*
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
#include "core-asm-arm.h"
#include "core-affinity.h"
#include "core-builtin.h"
#include "core-cpu-cache.h"
#include "core-killpid.h"
#include "core-mmap.h"

static const stress_help_t help[] = {
	{ NULL,	"dekker N",		"start N workers that exercise the Dekker algorithm" },
	{ NULL,	"dekker-ops N",		"stop after N dekker mutex bogo operations" },
	{ NULL,	NULL,			NULL }
};

#if defined(HAVE_SHIM_MFENCE) &&	\
    defined(HAVE_SIGLONGJMP)

typedef struct {
	volatile bool	wants_to_enter[2];
	volatile int	turn;
	volatile int	check;
} dekker_mutex_t;

/*
 *  dekker_t is mmap'd shared and m, p0, p1 are 64 byte cache aligned
 *  to reduce cache contention when updating metrics on p0 and p1
 */
typedef struct dekker {
	dekker_mutex_t	m;
	char 		pad1[64 - sizeof(dekker_mutex_t)];
	stress_metrics_t p0;
	char		pad2[64 - sizeof(stress_metrics_t)];
	stress_metrics_t p1;
} dekker_t;

static dekker_t *dekker;
static sigjmp_buf jmp_env;

static inline void ALWAYS_INLINE dekker_mfence(void)
{
	shim_mfence();
}

static inline void ALWAYS_INLINE dekker_mbarrier(void)
{
#if defined(HAVE_ASM_ARM_DMB_SY)
	stress_asm_arm_dmb_sy();
#endif
}

static void stress_decker_sigill_handler(int signum)
{
	(void)signum;

	siglongjmp(jmp_env, 1);
	stress_no_return();
}

static int stress_dekker_supported(const char *name)
{
	static struct sigaction act, oldact;
	int ret;

	(void)shim_memset(&act, 0, sizeof(act));
	(void)shim_memset(&oldact, 0, sizeof(oldact));

	ret = sigsetjmp(jmp_env, 1);
	if (ret == 1) {
		pr_inf_skip("%s: memory barrier not functional, skipping stressor\n", name);
		(void)sigaction(SIGILL, &oldact, &act);
		return -1;
	}

	act.sa_handler = stress_decker_sigill_handler;
        (void)sigemptyset(&act.sa_mask);
        act.sa_flags = SA_NOCLDSTOP;
	if (sigaction(SIGILL, &act, &oldact) < 0) {
		pr_inf_skip("%s: sigaction for SIGILL failed, skipping stressor\n",
			name);
		return -1;
	}

	dekker_mbarrier();

	if (sigaction(SIGILL, &oldact, NULL) < 0) {
		pr_inf_skip("%s: sigaction for SIGILL failed, skipping stressor\n",
			name);
		return -1;
	}
	return 0;
}

static int stress_dekker_p0(stress_args_t *args)
{
	int check0, check1;
	double t;

	t = stress_time_now();
	dekker->m.wants_to_enter[0] = true;
	dekker_mfence();
	dekker_mbarrier();
	while (LIKELY(dekker->m.wants_to_enter[1])) {
		if (dekker->m.turn != 0) {
			dekker->m.wants_to_enter[0] = false;
			dekker_mfence();
			dekker_mbarrier();
			while (dekker->m.turn != 0) {
			}
			dekker->m.wants_to_enter[0] = true;
			dekker_mfence();
			dekker_mbarrier();
		}
	}

	/* Critical section */
	check0 = dekker->m.check;
	dekker->m.check++;
	check1 = dekker->m.check;
	dekker_mfence();
	dekker_mbarrier();

	dekker->m.turn = 1;
	dekker->m.wants_to_enter[0] = false;
	dekker_mfence();
	dekker_mbarrier();
	dekker->p0.duration += stress_time_now() - t;
	dekker->p0.count += 1.0;

	if (check0 + 1 != check1) {
		pr_fail("%s p0: dekker mutex check failed %d vs %d\n",
			args->name, check0 + 1, check1);
		return EXIT_FAILURE;
	}
	return EXIT_SUCCESS;
}

static int stress_dekker_p1(stress_args_t *args)
{
	int check0, check1;
	double t;

	t = stress_time_now();

	dekker->m.wants_to_enter[1] = true;
	dekker_mfence();
	dekker_mbarrier();
	while (LIKELY(dekker->m.wants_to_enter[0])) {
		if (dekker->m.turn != 1) {
			dekker->m.wants_to_enter[1] = false;
			dekker_mfence();
			dekker_mbarrier();
			while (dekker->m.turn != 1) {
			}
			dekker->m.wants_to_enter[1] = true;
			dekker_mfence();
			dekker_mbarrier();
		}
	}

	/* Critical section */
	check0 = dekker->m.check;
	dekker->m.check--;
	check1 = dekker->m.check;
	dekker_mfence();
	dekker_mbarrier();
	stress_bogo_inc(args);

	dekker->m.turn = 0;
	dekker->m.wants_to_enter[1] = false;
	dekker_mfence();
	dekker_mbarrier();
	dekker->p1.duration += stress_time_now() - t;
	dekker->p1.count += 1.0;

	if (check0 - 1 != check1) {
		pr_fail("%s p1: dekker mutex check failed %d vs %d\n",
			args->name, check0 - 1, check1);
		return EXIT_FAILURE;
	}
	return EXIT_SUCCESS;
}

/*
 *  stress_dekker()
 *	stress dekker algorithm
 */
static int stress_dekker(stress_args_t *args)
{
	const size_t sz = STRESS_MAXIMUM(args->page_size, sizeof(*dekker));
	pid_t pid;
	double rate, duration, count;
	int parent_cpu, rc = EXIT_SUCCESS;

	dekker = (dekker_t *)stress_mmap_populate(NULL, sz,
			PROT_READ | PROT_WRITE,
			MAP_ANONYMOUS | MAP_SHARED, -1, 0);
	if (dekker == MAP_FAILED) {
		pr_inf_skip("%s: cannot mmap %zu bytes for bekker shared struct%s, skipping stressor\n",
			args->name, sz, stress_get_memfree_str());
		return EXIT_NO_RESOURCE;
	}

	stress_set_vma_anon_name(dekker, sz, "dekker-mutex");
	stress_zero_metrics(&dekker->p0, 1);
	stress_zero_metrics(&dekker->p1, 1);

	stress_set_proc_state(args->name, STRESS_STATE_SYNC_WAIT);
	stress_sync_start_wait(args);
	stress_set_proc_state(args->name, STRESS_STATE_RUN);

	parent_cpu = stress_get_cpu();
	pid = fork();
	if (pid < 0) {
		pr_inf_skip("%s: cannot create child process, skipping stressor\n", args->name);
		return EXIT_NO_RESOURCE;
	} else if (pid == 0) {
		/* Child */
		stress_set_proc_state(args->name, STRESS_STATE_RUN);
		(void)stress_change_cpu(args, parent_cpu);

		while (stress_continue(args)) {
			rc = stress_dekker_p0(args);
			if (rc != EXIT_SUCCESS)
				break;
		}
		_exit(rc);
	} else {
		int status;

		/* Parent */
		while (stress_continue(args)) {
			rc = stress_dekker_p1(args);
			if (rc != EXIT_SUCCESS)
				break;
		}
		if (stress_kill_pid_wait(pid, &status) >= 0) {
			if (WIFEXITED(status))
				rc = WEXITSTATUS(status);
		}
	}

	duration = dekker->p0.duration + dekker->p1.duration;
	count = dekker->p0.count + dekker->p1.count;
	rate = (count > 0.0) ? (duration / count) : 0.0;
	stress_metrics_set(args, 0, "nanosecs per mutex",
		rate * STRESS_DBL_NANOSECOND, STRESS_METRIC_HARMONIC_MEAN);

	(void)munmap((void *)dekker, 4096);

	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);
	(void)munmap((void *)dekker, sz);

	return rc;
}

const stressor_info_t stress_dekker_info = {
	.stressor = stress_dekker,
	.classifier = CLASS_CPU_CACHE | CLASS_IPC,
	.verify = VERIFY_ALWAYS,
	.supported = stress_dekker_supported,
	.help = help
};

#else

const stressor_info_t stress_dekker_info = {
	.stressor = stress_unimplemented,
	.classifier = CLASS_CPU | CLASS_IPC,
	.verify = VERIFY_ALWAYS,
	.help = help,
	.unimplemented_reason = "built without user space memory fencing or siglongjmp support"
};

#endif
