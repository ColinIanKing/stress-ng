/*
 * Copyright (C) 2013-2021 Canonical, Ltd.
 * Copyright (C) 2021-2024 Colin Ian King
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

static const stress_help_t help[] = {
	{ NULL,	"lockbus N",	 	"start N workers locking a memory increment" },
	{ NULL, "lockbus-nosplit",	"disable split locks" },
	{ NULL,	"lockbus-ops N", 	"stop after N lockbus bogo operations" },
	{ NULL, NULL,			NULL }
};

static int stress_set_lockbus_nosplit(const char *opt)
{
	return stress_set_setting_true("lockbus-nosplit", opt);
}

static const stress_opt_set_func_t opt_set_funcs[] = {
	{ OPT_lockbus_nosplit,	stress_set_lockbus_nosplit },
	{ 0,			NULL },
};

#if (((defined(HAVE_COMPILER_GCC_OR_MUSL) ||		\
       defined(HAVE_COMPILER_CLANG) ||			\
       defined(HAVE_COMPILER_ICC) ||			\
       defined(HAVE_COMPILER_ICX) ||			\
       defined(HAVE_COMPILER_TCC) ||			\
       defined(HAVE_COMPILER_PCC)) &&			\
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

#define BUFFER_SIZE	(1024 * 1024 * 16)
#define CHUNK_SIZE	(64 * 4)

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

#if defined(STRESS_ARCH_X86)
static sigjmp_buf jmp_env;
static bool do_splitlock;

static void NORETURN MLOCKED_TEXT stress_sigbus_handler(int signum)
{
	(void)signum;

	do_splitlock = false;

	siglongjmp(jmp_env, 1);
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
#if defined(STRESS_ARCH_X86)
	uint32_t *splitlock_ptr1, *splitlock_ptr2;
	bool lockbus_nosplit = false;

	(void)stress_get_setting("lockbus-nosplit", &lockbus_nosplit);

	if (stress_sighandler(args->name, SIGBUS, stress_sigbus_handler, NULL) < 0)
		return EXIT_FAILURE;
#endif

	buffer = (uint32_t*)stress_mmap_populate(NULL, BUFFER_SIZE,
			PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_SHARED, -1, 0);
	if (buffer == MAP_FAILED) {
		int rc = stress_exit_status(errno);

		pr_err("%s: mmap failed\n", args->name);
		return rc;
	}

#if defined(STRESS_ARCH_X86)
	/* Split lock on a page boundary */
	splitlock_ptr1 = (uint32_t *)(uintptr_t)(((uint8_t *)buffer) + args->page_size - (sizeof(*splitlock_ptr1) >> 1));
	/* Split lock on a cache boundary */
	splitlock_ptr2 = (uint32_t *)(uintptr_t)(((uint8_t *)buffer) + 64 - (sizeof(*splitlock_ptr2) >> 1));
	do_splitlock = !lockbus_nosplit;
	if (args->instance == 0)
		pr_dbg("%s: splitlocks %s\n", args->name,
			do_splitlock ? "enabled" : "disabled");
	if (sigsetjmp(jmp_env, 1) && !stress_continue(args))
		goto done;
#endif
	stress_set_proc_state(args->name, STRESS_STATE_RUN);
	stress_sync_start_wait(args);

	duration = 0;
	count = 0;
	do {
		uint32_t *ptr0 = buffer + (stress_mwc32modn(BUFFER_SIZE - CHUNK_SIZE) >> 2);
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
		duration += (stress_time_now() - t);
#if defined(HAVE_SYNC_BOOL_COMPARE_AND_SWAP)
		count += (8.0 * 12.0) + 6.0;
#else
		count += (8.0 * 12.0);
#endif
		stress_bogo_inc(args);
	} while (stress_continue(args));

#if defined(STRESS_ARCH_X86)
done:
#endif
	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);

	rate = (count > 0.0) ? duration / count : 0.0;
	stress_metrics_set(args, 0, "nanosecs per memory lock operation",
		rate * STRESS_DBL_NANOSECOND, STRESS_METRIC_HARMONIC_MEAN);

	(void)munmap((void *)buffer, BUFFER_SIZE);

	return EXIT_SUCCESS;
}

stressor_info_t stress_lockbus_info = {
	.stressor = stress_lockbus,
	.class = CLASS_CPU_CACHE | CLASS_MEMORY,
	.opt_set_funcs = opt_set_funcs,
	.help = help
};
#else
stressor_info_t stress_lockbus_info = {
	.stressor = stress_unimplemented,
	.class = CLASS_CPU_CACHE | CLASS_MEMORY,
	.opt_set_funcs = opt_set_funcs,
	.help = help,
	.unimplemented_reason = "built without gcc __atomic* lock builtins"
};
#endif
