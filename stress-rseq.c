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
 */
#include "stress-ng.h"
#include "core-arch.h"
#include "core-builtin.h"
#include "core-helper.h"
#include "core-mmap.h"
#include "core-out-of-memory.h"
#include "core-pragma.h"

#if defined(HAVE_LINUX_RSEQ_H)
#  include <linux/rseq.h>
#else /* HAVE_LINUX_RSEQ_H undefined */
#  if defined(HAVE_SYS_RSEQ_H)
#    include <sys/rseq.h>
#  endif /* HAVE_SYS_RSEQ_H undefined */
#endif /* HAVE_LINUX_RSEQ_H undefined */

static const stress_help_t help[] = {
	{ NULL,	"rseq N",	"start N workers that exercise restartable sequences" },
	{ NULL,	"rseq-ops N",	"stop after N bogo restartable sequence operations" },
	{ NULL,	NULL,		NULL }
};

#if defined(HAVE_LINUX_RSEQ_H) &&		\
    defined(HAVE_ASM_NOP) &&			\
    defined(__NR_rseq) &&			\
    defined(HAVE___RSEQ_OFFSET) &&		\
    defined(HAVE_SYSCALL) &&			\
    defined(RSEQ_SIG) &&			\
    defined(HAVE_COMPILER_GCC_OR_MUSL) &&	\
    defined(HAVE_BUILTIN_THREAD_POINTER) &&	\
    !defined(HAVE_COMPILER_CLANG) &&		\
    !defined(HAVE_COMPILER_ICC) &&		\
    !defined(HAVE_COMPILER_ICX)

#define STRESS_ACCESS_ONCE(x)     (*(__volatile__  __typeof__(x) *)&(x))

#if !defined(OPTIMIZE0)
#define OPTIMIZE0       __attribute__((optimize("-O0")))
#endif

#define STALLS() 	\
do {			\
	rseq_stalls++;	\
	rseq_stalls++;	\
	rseq_stalls++;	\
	rseq_stalls++;	\
	rseq_stalls++;	\
	rseq_stalls++;	\
	rseq_stalls++;	\
	rseq_stalls++;	\
	rseq_stalls++;	\
	rseq_stalls++;	\
	rseq_stalls++;	\
	rseq_stalls++;	\
	rseq_stalls++;	\
	rseq_stalls++;	\
	rseq_stalls++;	\
	rseq_stalls++;	\
} while (0);

typedef struct {
	uint64_t crit_count;		/* critical path entry count */
	uint64_t crit_interruptions;	/* interruptions in critical path */
	uint64_t segv_count;		/* SIGSEGV count from rseq */
} rseq_info_t;

static struct rseq *rseq_area;
static rseq_info_t *rseq_info;
static volatile uint64_t rseq_stalls;

static inline struct rseq *stress_rseq_get_area(void)
{
	return (struct rseq *)((ptrdiff_t)__builtin_thread_pointer() + __rseq_offset);
}

#define set_rseq_ptr(value)	*(uint64_t *)(uintptr_t)&rseq_area->rseq_cs = (uint64_t)(uintptr_t)value

/*
 *  rseq_test()
 *	exercise a critical section. force zero optimization to try
 *	and keep the compiler from doing any code reorganization that
 *	may affect the intentional layout of the branch sections
 */
static void OPTIMIZE0 rseq_test(const uint32_t cpu)
{
	struct rseq_cs __attribute__((aligned(64))) cs = {
		0,
		0,
		(uintptr_t)(void *)&&l1,
		((uintptr_t)&&l2 - (uintptr_t)&&l1),
		(uintptr_t)&&l3
	};
l1:
	/* Critical section starts here */
	set_rseq_ptr(&cs);
	if (cpu != rseq_area->cpu_id)
		goto l3;

	/*
	 *  Long duration in critical section will
	 *  be likely to be interrupted so rseq jumps
	 *  to label l3
	 */
	STALLS();
	STALLS();
	STALLS();
	STALLS();
	STALLS();
	STALLS();
	STALLS();
	STALLS();

	set_rseq_ptr(NULL);
	return;
	/* Critical section ends here */
l2:
	/* No-op filler, should never execute */
	set_rseq_ptr(NULL);
l3:
	/* Interrupted abort handler */
	set_rseq_ptr(NULL);
	rseq_info->crit_interruptions++;
}

/*
 *  sigsegv_handler()
 *	if rseq critical section handling is broken
 *	(e.g. the signature differs from the expected
 *	pattern) then a SIGSEGV is triggered. Hence we
 *	require a handler for this
 */
static void sigsegv_handler(int sig)
{
	(void)sig;

	rseq_info->segv_count++;
}

/*
 *  Sanity check if glibc supports rseq and area is
 *  initialized
 */
static int stress_rseq_supported(const char *name)
{
	struct rseq *rseq;

	rseq = stress_rseq_get_area();
	if (rseq == NULL) {
		pr_inf_skip("%s stressor will be skipped, libc rseq area is NULL\n", name);
		return -1;
	}
	if (!stress_addr_readable(rseq, sizeof(*rseq))) {
		pr_inf_skip("%s stressor will be skipped, libc rseq area is unreadable\n", name);
		return -1;
	}
	if ((uint32_t)rseq->cpu_id == (uint32_t)RSEQ_CPU_ID_REGISTRATION_FAILED) {
		pr_inf_skip("%s stressor will be skipped, libc rseq area is not enabled\n", name);
		return -1;
	}
	return 0;
}

/*
 *  stress_rseq()
 *	exercise restartable sequences rseq
 */
static int stress_rseq_oomable(stress_args_t *args, void *context)
{
	struct sigaction sa;
	(void)context;
	char misaligned_seq_buf[sizeof(struct rseq) + 1];
	struct rseq *misaligned_seq = (struct rseq *)&misaligned_seq_buf[1];
	struct rseq invalid_seq;

	(void)shim_memset(&sa, 0, sizeof(sa));
	sa.sa_handler = sigsegv_handler;
	if (sigaction(SIGSEGV, &sa, NULL) < 0) {
		pr_inf("%s: failed to set SIGSEGV handler\n", args->name);
		_exit(EXIT_FAILURE);
	}

	(void)shim_memcpy((void *)misaligned_seq, (void *)rseq_area, sizeof(*rseq_area));
	(void)shim_memcpy((void *)&invalid_seq, (void *)rseq_area, sizeof(*rseq_area));

	do {
		register int i;

		for (i = 0; i < 10000; i++) {
			uint32_t cpu;
			cpu = STRESS_ACCESS_ONCE(rseq_area->cpu_id_start);
			rseq_test(cpu);
		}
		rseq_info->crit_count += i;

		stress_bogo_inc(args);
	} while (stress_continue(args));

	return EXIT_SUCCESS;
}

static int stress_rseq(stress_args_t *args)
{
	int ret;
	double rate;

	/*
	 *  rseq_info is in a shared page to avoid losing the
	 *  stats when the child gets SEGV'd by rseq when we use
	 *  in invalid signature
	 */
	rseq_info = (rseq_info_t *)stress_mmap_populate(NULL, sizeof(*rseq_info),
			PROT_READ | PROT_WRITE,
			MAP_ANONYMOUS | MAP_SHARED, -1, 0);
	if (rseq_info == MAP_FAILED) {
		pr_inf_skip("%s: failed to mmap %zu byte shared page%s, "
			"errno=%d (%s), skipping stressor\n",
			args->name, sizeof(*rseq_info),
			stress_get_memfree_str(), errno, strerror(errno));
		return EXIT_NO_RESOURCE;
	}
	stress_set_vma_anon_name(rseq_info, sizeof(*rseq_info), "state");

	rseq_area = stress_rseq_get_area();
	if (stress_instance_zero(args))
		pr_dbg("libc rseq_area @ %p\n", rseq_area);

	stress_set_proc_state(args->name, STRESS_STATE_SYNC_WAIT);
	stress_sync_start_wait(args);
	stress_set_proc_state(args->name, STRESS_STATE_RUN);

	ret = stress_oomable_child(args, NULL, stress_rseq_oomable, STRESS_OOMABLE_QUIET);

	rate = (rseq_info->crit_count > 0) ?
		(double)rseq_info->crit_interruptions * 1000000000.0 / (rseq_info->crit_count) : 0.0;
	stress_metrics_set(args, 0, "critical section interruptions per billion rseq ops",
			rate, STRESS_METRIC_HARMONIC_MEAN);
	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);

	(void)munmap((void *)rseq_info, sizeof(*rseq_info));

	return ret;
}

const stressor_info_t stress_rseq_info = {
	.stressor = stress_rseq,
	.supported = stress_rseq_supported,
	.classifier = CLASS_CPU,
	.help = help
};
#else
const stressor_info_t stress_rseq_info = {
	.stressor = stress_unimplemented,
	.classifier = CLASS_CPU,
	.help = help,
	.unimplemented_reason = "built without Linux restartable sequences support"
};
#endif
