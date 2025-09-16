/*
 * Copyright (C) 2013-2021 Canonical, Ltd.
 * Copyright (C) 2021-2025 Colin Ian King
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
#include "core-arch.h"
#include "core-mmap.h"
#include "core-numa.h"

#if defined(HAVE_LINUX_MEMPOLICY_H) &&  \
    defined(__NR_mbind)
#include <linux/mempolicy.h>
#define HAVE_NUMA_LOCKBUS	(1)
#endif

#if defined(HAVE_LIB_RT) &&		\
    defined(HAVE_TIMER_CREATE) &&	\
    defined(HAVE_TIMER_DELETE) &&	\
    defined(HAVE_TIMER_GETOVERRUN) &&	\
    defined(HAVE_TIMER_SETTIME)
#include <time.h>
#define HAVE_TIMER_FUNCS
#endif

static const stress_help_t help[] = {
	{ NULL,	"lockbus N",	 	"start N workers locking a memory increment" },
	{ NULL, "lockbus-nosplit",	"disable split locks" },
	{ NULL,	"lockbus-ops N", 	"stop after N lockbus bogo operations" },
	{ NULL, NULL,			NULL }
};

static const stress_opt_t opts[] = {
	{ OPT_lockbus_nosplit, "lockbus-nosplit", TYPE_ID_BOOL, 0, 1, NULL },
	END_OPT,
};

#if (defined(HAVE_SIGLONGJMP) &&			\
     ((defined(HAVE_COMPILER_GCC_OR_MUSL) ||		\
       defined(HAVE_COMPILER_CLANG) ||			\
       defined(HAVE_COMPILER_ICC) ||			\
       defined(HAVE_COMPILER_ICX) ||			\
       defined(HAVE_COMPILER_TCC)) &&			\
       defined(STRESS_ARCH_X86)) ||			\
     (defined(HAVE_COMPILER_GCC_OR_MUSL) && 		\
      (defined(HAVE_ATOMIC_ADD_FETCH) ||		\
       defined(HAVE_ATOMIC_FETCH_ADD)) &&		\
      defined(__ATOMIC_SEQ_CST) &&			\
      NEED_GNUC(4,7,0) && 				\
      (defined(STRESS_ARCH_ALPHA) ||			\
       defined(STRESS_ARCH_ARM) ||			\
       defined(STRESS_ARCH_HPPA) ||			\
       defined(STRESS_ARCH_M68K) ||			\
       defined(STRESS_ARCH_MIPS) ||			\
       defined(STRESS_ARCH_PPC64) ||			\
       defined(STRESS_ARCH_PPC) ||			\
       defined(STRESS_ARCH_RISCV) ||			\
       defined(STRESS_ARCH_S390) ||			\
       defined(STRESS_ARCH_SH4) ||			\
       defined(STRESS_ARCH_SPARC)) ))

#if defined(HAVE_ATOMIC_ADD_FETCH)
#define MEM_LOCK(ptr, inc)				\
do {							\
	 __atomic_add_fetch(ptr, inc, __ATOMIC_SEQ_CST);\
} while (0)
#elif defined(HAVE_ATOMIC_FETCH_ADD)
#define MEM_LOCK(ptr, inc)				\
do {							\
	 __atomic_fetch_add(ptr, inc, __ATOMIC_SEQ_CST);\
} while (0)
#else
#define MEM_LOCK(ptr, inc)				\
do {							\
	__asm__ __volatile__("lock addl %1,%0" :	\
			     "+m" (*ptr) :		\
			     "ir" (inc));		\
} while (0)
#endif

#define BUFFER_SIZE		(1024 * 1024 * 8)
#define SHARED_BUFFER_SIZE	(1024 * 1024 * 8)
#define CHUNK_SIZE		(64 * 4)

#if defined(HAVE_SYNC_BOOL_COMPARE_AND_SWAP)
/* basically locked cmpxchg */
#define SYNC_BOOL_COMPARE_AND_SWAP(ptr, old_val, new_val) 	\
do {								\
	__sync_bool_compare_and_swap(ptr, old_val, new_val);	\
} while (0)
#else
/* no-op */
#define SYNC_BOOL_COMPARE_AND_SWAP(ptr, old_val, new_val)	\
do {								\
	(void)ptr;						\
	(void)old_val;						\
	(void)new_val;						\
}
#endif

#define MEM_LOCK_AND_INC(ptr, inc)		\
do {						\
	MEM_LOCK(ptr, inc);			\
	ptr++;					\
} while (0)

#define MEM_LOCK_AND_INCx8(ptr, inc)		\
do {						\
	MEM_LOCK_AND_INC(ptr, inc);		\
	MEM_LOCK_AND_INC(ptr, inc);		\
	MEM_LOCK_AND_INC(ptr, inc);		\
	MEM_LOCK_AND_INC(ptr, inc);		\
	MEM_LOCK_AND_INC(ptr, inc);		\
	MEM_LOCK_AND_INC(ptr, inc);		\
	MEM_LOCK_AND_INC(ptr, inc);		\
	MEM_LOCK_AND_INC(ptr, inc);		\
} while (0)

#define MEM_LOCKx8(ptr)				\
do {						\
	MEM_LOCK(ptr, 0);			\
	MEM_LOCK(ptr, 0);			\
	MEM_LOCK(ptr, 0);			\
	MEM_LOCK(ptr, 0);			\
	MEM_LOCK(ptr, 0);			\
	MEM_LOCK(ptr, 0);			\
	MEM_LOCK(ptr, 0);			\
	MEM_LOCK(ptr, 0);			\
} while (0)

static sigjmp_buf jmp_env;
static bool do_misaligned;
#if defined(STRESS_ARCH_X86)
static bool do_splitlock;
#endif
static bool do_sigill;
static uint32_t *shared_buffer = MAP_FAILED;

static void stress_lockbus_init(const uint32_t instances)
{
	(void)instances;

	shared_buffer = (uint32_t *)stress_mmap_populate(NULL, SHARED_BUFFER_SIZE,
		PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_SHARED, -1, 0);
	if (shared_buffer != MAP_FAILED)
		stress_set_vma_anon_name(shared_buffer, BUFFER_SIZE, "lockbus-shared-data");
}

static void stress_lockbus_deinit(void)
{
	if (shared_buffer != MAP_FAILED) {
		(void)munmap((void *)shared_buffer, SHARED_BUFFER_SIZE);
		shared_buffer = MAP_FAILED;
	}
}

static void NORETURN MLOCKED_TEXT stress_sigill_handler(int signum)
{
	if (signum == SIGILL) {
#if defined(STRESS_ARCH_S390)
		do_misaligned = false;
#else
		do_sigill = true;
#endif
	}

	siglongjmp(jmp_env, 1);
	stress_no_return();
}

static void NORETURN MLOCKED_TEXT stress_sigbus_misaligned_handler(int signum)
{
	(void)signum;

	do_misaligned = false;

	siglongjmp(jmp_env, 1);
	stress_no_return();
}

#if defined(STRESS_ARCH_X86)
static void NORETURN MLOCKED_TEXT stress_sigbus_splitlock_handler(int signum)
{
	(void)signum;

	do_splitlock = false;

	siglongjmp(jmp_env, 1);
	stress_no_return();
}
#endif

/*
 *  stress_lockbus()
 *      stress memory with lock and increment
 */
static int stress_lockbus(stress_args_t *args)
{
	uint32_t *buffer;
	double t, rate;
	NOCLOBBER double duration, count;
	NOCLOBBER int rc = EXIT_SUCCESS;
	uint32_t *misaligned_ptr1, *misaligned_ptr2;
#if defined(HAVE_TIMER_FUNCS)
	timer_t timerid;
	NOCLOBBER int timer_ret = -1;
#endif
#if defined(STRESS_ARCH_X86)
	uint32_t *splitlock_ptr1, *splitlock_ptr2;
	bool lockbus_nosplit = false;

	do_sigill = false;
	(void)stress_get_setting("lockbus-nosplit", &lockbus_nosplit);

	if (stress_sighandler(args->name, SIGBUS, stress_sigbus_splitlock_handler, NULL) < 0)
		return EXIT_FAILURE;
#endif
#if defined(HAVE_NUMA_LOCKBUS)
	NOCLOBBER stress_numa_mask_t *numa_mask;
#endif

	if (shared_buffer == MAP_FAILED) {
		pr_inf_skip("%s: failed to mmap %zu shared bytes%s, errno=%d (%s), skipping stressor\n",
			args->name, (size_t)SHARED_BUFFER_SIZE,
			stress_get_memfree_str(), errno, strerror(errno));
		return EXIT_NO_RESOURCE;
	}

	buffer = (uint32_t *)stress_mmap_populate(NULL, BUFFER_SIZE,
			PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_SHARED, -1, 0);
	if (buffer == MAP_FAILED) {
		pr_inf_skip("%s: failed to mmap %zu bytes%s, errno=%d (%s), skipping stressor\n",
			args->name, (size_t)BUFFER_SIZE,
			stress_get_memfree_str(), errno, strerror(errno));
		return EXIT_NO_RESOURCE;
	}
	stress_set_vma_anon_name(buffer, BUFFER_SIZE, "lockbus-data");

	do_misaligned = true;
	misaligned_ptr1 = (uint32_t *)(uintptr_t)((uint8_t *)buffer + 1);
	misaligned_ptr2 = (uint32_t *)(uintptr_t)((uint8_t *)buffer + 10);

	if (stress_sighandler(args->name, SIGBUS, stress_sigbus_misaligned_handler, NULL) < 0)
		return EXIT_FAILURE;
#if defined(HAVE_TIMER_FUNCS)
	if (stress_sighandler(args->name, SIGRTMIN, stress_sigbus_misaligned_handler, NULL) < 0)
		return EXIT_FAILURE;
#endif
	if (stress_sighandler(args->name, SIGILL, stress_sigill_handler, NULL) < 0)
		return EXIT_FAILURE;
	if (sigsetjmp(jmp_env, 1))
		goto misaligned_done;

#if defined(HAVE_TIMER_FUNCS)
	/*
	 *  Use a 1 second timer to jmp out of a hung
	 *  misaligned splitlock.
	 */
	{
		struct sigevent sev;
		struct itimerspec timer;

		sev.sigev_notify = SIGEV_SIGNAL;
		sev.sigev_signo = SIGRTMIN;
		sev.sigev_value.sival_ptr = &timerid;

#if defined(CLOCK_PROCESS_CPUTIME_ID)
		timer_ret = timer_create(CLOCK_PROCESS_CPUTIME_ID, &sev, &timerid);
#else
		timer_ret = timer_create(CLOCK_REALTIME, &sev, &timerid);
#endif
		if (timer_ret == 0) {
			timer.it_value.tv_sec = 1;
			timer.it_value.tv_nsec = 0;
			timer.it_interval.tv_sec = 1;
			timer.it_interval.tv_nsec = 0;
			if (timer_settime(timerid, 0, &timer, NULL) < 0) {
				(void)timer_delete(timerid);
				timer_ret = -1;
			}
		}
	}
#endif
	/* These can hang on old ppc64 linux kernels */
	MEM_LOCK_AND_INC(misaligned_ptr1, 1);
	MEM_LOCK_AND_INC(misaligned_ptr2, 1);

misaligned_done:
#if defined(HAVE_TIMER_FUNCS)
	if (timer_ret == 0) {
		(void)timer_delete(timerid);
		timer_ret = -1;
	}
#endif
	if (do_sigill) {
		pr_inf_skip("%s: SIGILL occurred on atomic lock operations "
			"(possibly unsupported opcodes), skipping stressor\n",
			args->name);
		rc = EXIT_NO_RESOURCE;
		goto done;
	}
	if (stress_instance_zero(args))
		pr_dbg("%s: misaligned splitlocks %s\n", args->name,
			do_misaligned ? "enabled" : "disabled");

#if defined(STRESS_ARCH_X86)
	if (stress_sighandler(args->name, SIGBUS, stress_sigbus_splitlock_handler, NULL) < 0)
		return EXIT_FAILURE;
	/* Split lock on a page boundary */
	splitlock_ptr1 = (uint32_t *)(uintptr_t)(((uint8_t *)buffer) + args->page_size - (sizeof(*splitlock_ptr1) >> 1));
	/* Split lock on a cache boundary */
	splitlock_ptr2 = (uint32_t *)(uintptr_t)(((uint8_t *)buffer) + 64 - (sizeof(*splitlock_ptr2) >> 1));
	do_splitlock = !lockbus_nosplit;
	if (stress_instance_zero(args))
		pr_dbg("%s: splitlocks %s\n", args->name,
			do_splitlock ? "enabled" : "disabled");
	if (sigsetjmp(jmp_env, 1) && !stress_continue(args))
		goto done;
#endif

#if defined(HAVE_NUMA_LOCKBUS)
	numa_mask = stress_numa_mask_alloc();
	if (numa_mask) {
		stress_numa_mask_t *numa_nodes;

		numa_nodes = stress_numa_mask_alloc();
		if (numa_nodes) {
			if (stress_numa_mask_nodes_get(numa_nodes) > 0) {
				stress_numa_randomize_pages(args, numa_nodes, numa_mask, buffer, BUFFER_SIZE, args->page_size);
				stress_numa_randomize_pages(args, numa_nodes, numa_mask, shared_buffer, SHARED_BUFFER_SIZE, args->page_size);
			}
			stress_numa_mask_free(numa_nodes);
		}
		stress_numa_mask_free(numa_mask);
	}
#endif

	stress_set_proc_state(args->name, STRESS_STATE_SYNC_WAIT);
	stress_sync_start_wait(args);
	stress_set_proc_state(args->name, STRESS_STATE_RUN);

	duration = 0;
	count = 0;
	do {
		uint32_t *ptr0 = stress_mwc1() ?
			buffer + (stress_mwc32modn(BUFFER_SIZE - CHUNK_SIZE) >> 2) :
			shared_buffer + (stress_mwc32modn(SHARED_BUFFER_SIZE - CHUNK_SIZE) >> 2);
#if defined(STRESS_ARCH_X86)
		uint32_t *ptr1 = do_splitlock ? splitlock_ptr1 : ptr0;
		uint32_t *ptr2 = do_splitlock ? splitlock_ptr2 : ptr0;
#else
		uint32_t *ptr1 = ptr0;
		uint32_t *ptr2 = ptr0;
#endif
		const uint32_t inc = 1;

		t = stress_time_now();
		MEM_LOCK_AND_INCx8(ptr0, inc);
		MEM_LOCKx8(ptr1);
		MEM_LOCKx8(ptr2);
		MEM_LOCK_AND_INCx8(ptr0, inc);
		MEM_LOCKx8(ptr1);
		MEM_LOCKx8(ptr2);
		MEM_LOCK_AND_INCx8(ptr0, inc);
		MEM_LOCKx8(ptr1);
		MEM_LOCKx8(ptr2);
		MEM_LOCK_AND_INCx8(ptr0, inc);
		MEM_LOCKx8(ptr1);
		MEM_LOCKx8(ptr2);

#if defined(HAVE_SYNC_BOOL_COMPARE_AND_SWAP)
		{
			register uint32_t val;
			register const uint32_t zero = 0;

			val = *ptr0;
			SYNC_BOOL_COMPARE_AND_SWAP(ptr0, val, zero);
			SYNC_BOOL_COMPARE_AND_SWAP(ptr0, zero, val);

			val = *ptr1;
			SYNC_BOOL_COMPARE_AND_SWAP(ptr1, val, zero);
			SYNC_BOOL_COMPARE_AND_SWAP(ptr1, zero, val);

			val = *ptr2;
			SYNC_BOOL_COMPARE_AND_SWAP(ptr2, val, zero);
			SYNC_BOOL_COMPARE_AND_SWAP(ptr2, zero, val);
		}
#endif
		if (do_misaligned) {
			MEM_LOCK_AND_INCx8(misaligned_ptr1, inc);
			MEM_LOCK_AND_INCx8(misaligned_ptr2, inc);
			count += (8.0 * 2.0);
		}

		duration += (stress_time_now() - t);
#if defined(HAVE_SYNC_BOOL_COMPARE_AND_SWAP)
		count += (8.0 * 12.0) + 6.0;
#else
		count += (8.0 * 12.0);
#endif
		stress_bogo_inc(args);
	} while (stress_continue(args));

done:
	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);

	rate = (count > 0.0) ? duration / count : 0.0;
	stress_metrics_set(args, 0, "nanosecs per memory lock operation",
		rate * STRESS_DBL_NANOSECOND, STRESS_METRIC_HARMONIC_MEAN);

	(void)munmap((void *)buffer, BUFFER_SIZE);

	return rc;
}

const stressor_info_t stress_lockbus_info = {
	.stressor = stress_lockbus,
	.classifier = CLASS_CPU_CACHE | CLASS_MEMORY,
	.opts = opts,
	.help = help,
	.init = stress_lockbus_init,
	.deinit = stress_lockbus_deinit
};
#else
const stressor_info_t stress_lockbus_info = {
	.stressor = stress_unimplemented,
	.classifier = CLASS_CPU_CACHE | CLASS_MEMORY,
	.opts = opts,
	.help = help,
	.unimplemented_reason = "built without gcc __atomic* lock builtins or siglongjmp support"
};
#endif
