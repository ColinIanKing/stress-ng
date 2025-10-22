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
#include "core-arch.h"
#include "core-builtin.h"
#include "core-cpu-cache.h"
#include "core-killpid.h"
#include "core-mmap.h"

static const stress_help_t help[] = {
	{ NULL,	"peterson N",		"start N workers that exercise Peterson's algorithm" },
	{ NULL,	"peterson-ops N",	"stop after N peterson mutex bogo operations" },
	{ NULL,	NULL,			NULL }
};

#if defined(HAVE_SHIM_MFENCE) &&	\
    defined(HAVE_SIGLONGJMP)

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
static sigjmp_buf jmp_env;

static inline void ALWAYS_INLINE peterson_mfence(void)
{
	shim_mfence();
}

static inline void ALWAYS_INLINE peterson_mbarrier(void)
{
#if defined(HAVE_ASM_ARM_DMB_SY)
	stress_asm_arm_dmb_sy();
#endif
}

static void stress_peterson_sigill_handler(int signum)
{
	(void)signum;

	siglongjmp(jmp_env, 1);
	stress_no_return();
}

static int stress_peterson_supported(const char *name)
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

	act.sa_handler = stress_peterson_sigill_handler;
        (void)sigemptyset(&act.sa_mask);
        act.sa_flags = SA_NOCLDSTOP;
	if (sigaction(SIGILL, &act, &oldact) < 0) {
		pr_inf_skip("%s: sigaction for SIGILL failed, skipping stressor\n",
			name);
		return -1;
	}

	peterson_mbarrier();

	if (sigaction(SIGILL, &oldact, NULL) < 0) {
		pr_inf_skip("%s: sigaction for SIGILL failed, skipping stressor\n",
			name);
		return -1;
	}
	return 0;
}

static int stress_peterson_p0(stress_args_t *args)
{
	int check0, check1;
	double t;

	t = stress_time_now();
	peterson->m.flag[0] = true;
	peterson->m.turn = 1;
	peterson_mfence();
	peterson_mbarrier();
	while (peterson->m.flag[1] && (peterson->m.turn == 1)) {
#if defined(STRESS_ARCH_RISCV)
		(void)shim_sched_yield();
#endif
	}

	/* Critical section */
	check0 = peterson->m.check;
	peterson->m.check++;
	check1 = peterson->m.check;
#if defined(STRESS_ARCH_ARM)
	peterson_mfence();
	peterson_mbarrier();
#endif

	peterson->m.flag[0] = false;
	peterson_mfence();
	peterson_mbarrier();
	peterson->p0.duration += stress_time_now() - t;
	peterson->p0.count += 1.0;

	if (UNLIKELY(check0 + 1 != check1)) {
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
	peterson_mfence();
	peterson_mbarrier();
	while (peterson->m.flag[0] && (peterson->m.turn == 0)) {
#if defined(STRESS_ARCH_RISCV)
		(void)shim_sched_yield();
#endif
	}

	/* Critical section */
	check0 = peterson->m.check;
	peterson->m.check--;
	check1 = peterson->m.check;
#if defined(STRESS_ARCH_ARM)
	peterson_mfence();
	peterson_mbarrier();
#endif
	stress_bogo_inc(args);

	peterson->m.flag[1] = false;
	peterson_mfence();
	peterson_mbarrier();
	peterson->p1.duration += stress_time_now() - t;
	peterson->p1.count += 1.0;

	if (UNLIKELY(check0 - 1 != check1)) {
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
		pr_inf_skip("%s: cannot mmap %zu bytes for peterson shared struct%s, "
			"errno=%d (%s), skipping stressor\n",
			args->name, sz, stress_get_memfree_str(),
			errno, strerror(errno));
		return EXIT_NO_RESOURCE;
	}
	stress_set_vma_anon_name(peterson, sz, "peterson-lock");

	stress_zero_metrics(&peterson->p0, 1);
	stress_zero_metrics(&peterson->p1, 1);

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
		stress_set_proc_state(args->name, STRESS_STATE_RUN);
		(void)stress_change_cpu(args, parent_cpu);
		while (stress_continue(args)) {
			rc = stress_peterson_p0(args);
			if (UNLIKELY(rc != EXIT_SUCCESS))
				break;
		}
		_exit(rc);
	} else {
		int status;

		/* Parent */
		while (stress_continue(args)) {
			rc = stress_peterson_p1(args);
			if (UNLIKELY(rc != EXIT_SUCCESS))
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

const stressor_info_t stress_peterson_info = {
	.stressor = stress_peterson,
	.classifier = CLASS_CPU_CACHE | CLASS_IPC,
	.verify = VERIFY_ALWAYS,
	.supported = stress_peterson_supported,
	.help = help
};

#else

const stressor_info_t stress_peterson_info = {
	.stressor = stress_unimplemented,
	.classifier = CLASS_CPU | CLASS_IPC,
	.verify = VERIFY_ALWAYS,
	.help = help,
	.unimplemented_reason = "built without user space memory fencing or support for siglongjmp"
};

#endif
