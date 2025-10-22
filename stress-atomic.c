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
#include "core-killpid.h"
#include "core-mmap.h"

/* Current disable 128 bit support, some toolchains don't support it */
#undef HAVE_INT128_T

#if ULONG_MAX == 0xffffffffffffffff
#define STRESS_ATOMIC_64BIT		(1)
#endif

#define STRESS_ATOMIC_STRINGIZE(x)	#x

#define STRESS_ATOMIC_MAX_PROCS		(3)
#define STRESS_ATOMIC_MAX_FUNCS		(SIZEOF_ARRAY(atomic_func_info))

typedef int (*atomic_func_t)(stress_args_t *args, double *duration, double *count);

#define DO_NOTHING()	do { } while (0)

#if defined(HAVE_ATOMIC_ADD_FETCH)
#define HAVE_ATOMIC_OPS
#define	SHIM_ATOMIC_ADD_FETCH(ptr, val, memorder)	\
	do { __atomic_add_fetch(ptr, val, memorder); } while (0)
#else
#define SHIM_ATOMIC_ADD_FETCH(ptr, val, memorder)	DO_NOTHING()
#endif

#if defined(HAVE_ATOMIC_AND_FETCH)
#define HAVE_ATOMIC_OPS
#define	SHIM_ATOMIC_AND_FETCH(ptr, val, memorder)	\
	do { __atomic_and_fetch(ptr, val, memorder); } while (0)
#else
#define SHIM_ATOMIC_AND_FETCH(ptr, val, memorder)	DO_NOTHING()
#endif

#if defined(HAVE_ATOMIC_CLEAR)
#define HAVE_ATOMIC_OPS
#define	SHIM_ATOMIC_CLEAR(ptr, memorder)		\
	do { __atomic_clear(ptr, memorder); } while (0)
#else
#define SHIM_ATOMIC_CLEAR(ptr, memorder)		DO_NOTHING()
#endif

#if defined(HAVE_ATOMIC_FETCH_ADD)
#define HAVE_ATOMIC_OPS
#define	SHIM_ATOMIC_FETCH_ADD(ptr, val, memorder)	\
	do { __atomic_fetch_add(ptr, val, memorder); } while (0)
#else
#define SHIM_ATOMIC_FETCH_ADD(ptr, val, memorder)	DO_NOTHING()
#endif

#if defined(HAVE_ATOMIC_FETCH_AND)
#define HAVE_ATOMIC_OPS
#define	SHIM_ATOMIC_FETCH_AND(ptr, val, memorder)	\
	do { __atomic_fetch_and(ptr, val, memorder); } while (0)
#else
#define SHIM_ATOMIC_FETCH_AND(ptr, val, memorder)	DO_NOTHING()
#endif

#if defined(HAVE_ATOMIC_FETCH_NAND)
#define HAVE_ATOMIC_OPS
#if defined(HAVE_COMPILER_GCC_OR_MUSL) && __GNUC__ != 11
#define	SHIM_ATOMIC_FETCH_NAND(ptr, val, memorder)	\
	do { __atomic_fetch_nand(ptr, val, memorder); } while (0)
#else
/*
 *  gcc 11.x has a buggy fetch nand that can lock indefinitely, so
 *  workaround this
 */
#define	SHIM_ATOMIC_FETCH_NAND(ptr, val, memorder)	\
	do { __atomic_fetch_and(ptr, val, memorder); 	\
	     __atomic_fetch_xor(ptr, ~0, memorder); } while (0)
#endif
#else
#define SHIM_ATOMIC_FETCH_NAND(ptr, val, memorder)	DO_NOTHING()
#endif

#if defined(HAVE_ATOMIC_FETCH_OR)
#define HAVE_ATOMIC_OPS
#define	SHIM_ATOMIC_FETCH_OR(ptr, val, memorder)	\
	do { __atomic_fetch_or(ptr, val, memorder); } while (0)
#else
#define SHIM_ATOMIC_FETCH_OR(ptr, val, memorder)	DO_NOTHING()
#endif

#if defined(HAVE_ATOMIC_FETCH_SUB)
#define HAVE_ATOMIC_OPS
#define	SHIM_ATOMIC_FETCH_SUB(ptr, val, memorder)	\
	do { __atomic_fetch_sub(ptr, val, memorder); } while (0)
#else
#define SHIM_ATOMIC_FETCH_SUB(ptr, val, memorder)	DO_NOTHING()
#endif

#if defined(HAVE_ATOMIC_FETCH_XOR)
#define HAVE_ATOMIC_OPS
#define	SHIM_ATOMIC_FETCH_XOR(ptr, val, memorder)	\
	do { __atomic_fetch_xor(ptr, val, memorder); } while (0)
#else
#define SHIM_ATOMIC_FETCH_XOR(ptr, val, memorder)	DO_NOTHING()
#endif

#if defined(HAVE_ATOMIC_LOAD)
#define HAVE_ATOMIC_OPS
#define	SHIM_ATOMIC_LOAD(ptr, val, memorder)		\
	do { __atomic_load(ptr, val, memorder); } while (0)
#else
#define SHIM_ATOMIC_LOAD(ptr, val, memorder)		DO_NOTHING()
#endif

#if defined(HAVE_ATOMIC_NAND_FETCH)
#define HAVE_ATOMIC_OPS
#if defined(HAVE_COMPILER_GCC_OR_MUSL) && __GNUC__ != 11
#define	SHIM_ATOMIC_NAND_FETCH(ptr, val, memorder)	\
	do { __atomic_nand_fetch(ptr, val, memorder); } while (0)
#else
/*
 *  gcc 11.x has a buggy fetch nand that can lock indefinitely, so
 *  workaround this
 */
#define	SHIM_ATOMIC_NAND_FETCH(ptr, val, memorder)	\
	do { __atomic_and_fetch(ptr, val, memorder);	\
	     __atomic_xor_fetch(ptr, ~0, memorder); } while (0)
#endif
#else
#define SHIM_ATOMIC_NAND_FETCH(ptr, val, memorder)	DO_NOTHING()
#endif

#if defined(HAVE_ATOMIC_OR_FETCH)
#define HAVE_ATOMIC_OPS
#define	SHIM_ATOMIC_OR_FETCH(ptr, val, memorder)	\
	do { __atomic_or_fetch(ptr, val, memorder); } while (0)
#else
#define SHIM_ATOMIC_OR_FETCH(ptr, val, memorder)	DO_NOTHING()
#endif

#if defined(HAVE_ATOMIC_STORE)
#define HAVE_ATOMIC_OPS
#define	SHIM_ATOMIC_STORE(ptr, val, memorder)		\
	do { __atomic_store(ptr, val, memorder); } while (0)
#else
#define SHIM_ATOMIC_STORE(ptr, val, memorder)		DO_NOTHING()
#endif

#if defined(HAVE_ATOMIC_SUB_FETCH)
#define HAVE_ATOMIC_OPS
#define	SHIM_ATOMIC_SUB_FETCH(ptr, val, memorder)	\
	do { __atomic_sub_fetch(ptr, val, memorder); } while (0)
#else
#define SHIM_ATOMIC_SUB_FETCH(ptr, val, memorder)	DO_NOTHING()
#endif

#if defined(HAVE_ATOMIC_XOR_FETCH)
#define HAVE_ATOMIC_OPS
#define	SHIM_ATOMIC_XOR_FETCH(ptr, val, memorder)	\
	do { __atomic_xor_fetch(ptr, val, memorder); } while (0)
#else
#define SHIM_ATOMIC_XOR_FETCH(ptr, val, memorder)	DO_NOTHING()
#endif

/* 60 atomic operations */

#define STRESS_ATOMIC_OPS_COUNT		(60)

#define DO_ATOMIC_OPS(args, type, var, duration, count, rc)		\
do {									\
	double t;							\
	type tmp = (type)stress_mwc64();				\
	type unshared, check1 = tmp, check2 = ~tmp;			\
									\
	t = stress_time_now();						\
	SHIM_ATOMIC_STORE(&unshared, &check1, __ATOMIC_RELAXED);	\
	SHIM_ATOMIC_ADD_FETCH(&unshared, (type)2, __ATOMIC_RELAXED);	\
	SHIM_ATOMIC_SUB_FETCH(&unshared, (type)1, __ATOMIC_RELAXED);	\
	SHIM_ATOMIC_LOAD(&unshared, &check2, __ATOMIC_RELAXED);		\
									\
	SHIM_ATOMIC_STORE(var, &tmp, __ATOMIC_RELAXED); 		\
	SHIM_ATOMIC_LOAD(var, &tmp, __ATOMIC_RELAXED);			\
	SHIM_ATOMIC_LOAD(var, &tmp, __ATOMIC_ACQUIRE);			\
	SHIM_ATOMIC_ADD_FETCH(var, (type)1, __ATOMIC_RELAXED);		\
	SHIM_ATOMIC_ADD_FETCH(var, (type)2, __ATOMIC_ACQUIRE);		\
	SHIM_ATOMIC_SUB_FETCH(var, (type)3, __ATOMIC_RELAXED);		\
	SHIM_ATOMIC_SUB_FETCH(var, (type)4, __ATOMIC_ACQUIRE);		\
	SHIM_ATOMIC_AND_FETCH(var, (type)~1, __ATOMIC_RELAXED);		\
	SHIM_ATOMIC_AND_FETCH(var, (type)~2, __ATOMIC_ACQUIRE);		\
	SHIM_ATOMIC_XOR_FETCH(var, (type)~4, __ATOMIC_RELAXED);		\
	SHIM_ATOMIC_XOR_FETCH(var, (type)~8, __ATOMIC_ACQUIRE);		\
	SHIM_ATOMIC_OR_FETCH(var, (type)16, __ATOMIC_RELAXED);		\
	SHIM_ATOMIC_OR_FETCH(var, (type)32, __ATOMIC_ACQUIRE);		\
	SHIM_ATOMIC_NAND_FETCH(var, (type)64, __ATOMIC_RELAXED);	\
	SHIM_ATOMIC_NAND_FETCH(var, (type)128, __ATOMIC_ACQUIRE);	\
	SHIM_ATOMIC_CLEAR(var, __ATOMIC_RELAXED);			\
									\
	SHIM_ATOMIC_STORE(var, &tmp, __ATOMIC_RELAXED); 		\
	SHIM_ATOMIC_FETCH_ADD(var, (type)1, __ATOMIC_RELAXED);		\
	SHIM_ATOMIC_FETCH_ADD(var, (type)2, __ATOMIC_ACQUIRE);		\
	SHIM_ATOMIC_FETCH_SUB(var, (type)3, __ATOMIC_RELAXED);		\
	SHIM_ATOMIC_FETCH_SUB(var, (type)4, __ATOMIC_ACQUIRE);		\
	SHIM_ATOMIC_FETCH_AND(var, (type)~1, __ATOMIC_RELAXED);		\
	SHIM_ATOMIC_FETCH_AND(var, (type)~2, __ATOMIC_ACQUIRE);		\
	SHIM_ATOMIC_FETCH_XOR(var, (type)~4, __ATOMIC_RELAXED);		\
	SHIM_ATOMIC_FETCH_XOR(var, (type)~8, __ATOMIC_ACQUIRE);		\
	SHIM_ATOMIC_FETCH_OR(var, (type)16, __ATOMIC_RELAXED);		\
	SHIM_ATOMIC_FETCH_OR(var, (type)32, __ATOMIC_ACQUIRE);		\
	SHIM_ATOMIC_FETCH_NAND(var, (type)64, __ATOMIC_RELAXED);	\
	SHIM_ATOMIC_FETCH_NAND(var, (type)128, __ATOMIC_ACQUIRE);	\
	SHIM_ATOMIC_CLEAR(var, __ATOMIC_RELAXED);			\
									\
	SHIM_ATOMIC_STORE(var, &tmp, __ATOMIC_RELAXED); 		\
	SHIM_ATOMIC_LOAD(var, &tmp, __ATOMIC_RELAXED);			\
	SHIM_ATOMIC_ADD_FETCH(var, (type)1, __ATOMIC_RELAXED);		\
	SHIM_ATOMIC_SUB_FETCH(var, (type)3, __ATOMIC_RELAXED);		\
	SHIM_ATOMIC_AND_FETCH(var, (type)~1, __ATOMIC_RELAXED);		\
	SHIM_ATOMIC_XOR_FETCH(var, (type)~4, __ATOMIC_RELAXED);		\
	SHIM_ATOMIC_OR_FETCH(var, (type)16, __ATOMIC_RELAXED);		\
	SHIM_ATOMIC_NAND_FETCH(var, (type)64, __ATOMIC_RELAXED);	\
	SHIM_ATOMIC_LOAD(var, &tmp, __ATOMIC_ACQUIRE);			\
	SHIM_ATOMIC_ADD_FETCH(var, (type)2, __ATOMIC_ACQUIRE);		\
	SHIM_ATOMIC_SUB_FETCH(var, (type)4, __ATOMIC_ACQUIRE);		\
	SHIM_ATOMIC_AND_FETCH(var, (type)~2, __ATOMIC_ACQUIRE);		\
	SHIM_ATOMIC_XOR_FETCH(var, (type)~8, __ATOMIC_ACQUIRE);		\
	SHIM_ATOMIC_OR_FETCH(var, (type)32, __ATOMIC_ACQUIRE);		\
	SHIM_ATOMIC_NAND_FETCH(var, (type)128, __ATOMIC_ACQUIRE);	\
	SHIM_ATOMIC_CLEAR(var, __ATOMIC_RELAXED);			\
									\
	SHIM_ATOMIC_STORE(var, &tmp, __ATOMIC_RELAXED); 		\
	SHIM_ATOMIC_FETCH_ADD(var, (type)1, __ATOMIC_RELAXED);		\
	SHIM_ATOMIC_FETCH_SUB(var, (type)3, __ATOMIC_RELAXED);		\
	SHIM_ATOMIC_FETCH_AND(var, (type)~1, __ATOMIC_RELAXED);		\
	SHIM_ATOMIC_FETCH_XOR(var, (type)~4, __ATOMIC_RELAXED);		\
	SHIM_ATOMIC_FETCH_OR(var, (type)16, __ATOMIC_RELAXED);		\
	SHIM_ATOMIC_FETCH_NAND(var, (type)64, __ATOMIC_RELAXED);	\
	SHIM_ATOMIC_FETCH_ADD(var, (type)2, __ATOMIC_ACQUIRE);		\
	SHIM_ATOMIC_FETCH_SUB(var, (type)4, __ATOMIC_ACQUIRE);		\
	SHIM_ATOMIC_FETCH_AND(var, (type)~2, __ATOMIC_ACQUIRE);		\
	SHIM_ATOMIC_FETCH_XOR(var, (type)~8, __ATOMIC_ACQUIRE);		\
	SHIM_ATOMIC_FETCH_OR(var, (type)32, __ATOMIC_ACQUIRE);		\
	SHIM_ATOMIC_FETCH_NAND(var, (type)128, __ATOMIC_ACQUIRE);	\
	SHIM_ATOMIC_CLEAR(var, __ATOMIC_RELAXED);			\
	(*duration) += stress_time_now() - t;				\
	(*count) += 64.0;						\
									\
	(void)tmp;							\
	check2--;							\
	if (UNLIKELY(check2 != check1)) {				\
		pr_fail("%s atomic store/inc/dec/load on " 		\
			STRESS_ATOMIC_STRINGIZE(type)			\
			" failed, got 0x%" PRIx64 			\
			", expecting 0x%" PRIx64 "\n",			\
			args->name, (uint64_t)check2, (uint64_t)check1);\
		rc = -1;						\
		break;							\
	}								\
} while (0)

static const stress_help_t help[] = {
	{ NULL,	"atomic",	"start N workers exercising GCC atomic operations" },
	{ NULL, "atomic-ops",	"stop after N bogo atomic bogo operations" },
	{ NULL, NULL,		NULL }
};

#if defined(HAVE_ATOMIC_OPS)

#if defined(__sh__)
/*
 *  sh gcc can break by running out of spill registers, so
 *  crank down the optimization for this on sh until this
 *  gcc bug is resolved.
 */
#define ATOMIC_OPTIMIZE OPTIMIZE0
#else
#define ATOMIC_OPTIMIZE
#endif

#if defined(STRESS_ATOMIC_64BIT) &&	\
    defined(HAVE_INT128_T)
static int ATOMIC_OPTIMIZE stress_atomic_uint128(
	stress_args_t *args,
	double *duration,
	double *count)
{
	static int idx = 0;
	int rc = 0;

	DO_ATOMIC_OPS(args, __uint128_t, &g_shared->atomic.val128[idx], duration, count, rc);
	idx++;
	idx &= (SIZEOF_ARRAY(g_shared->atomic.val128) - 1);

	return rc;
}
#endif

#if defined(STRESS_ATOMIC_64BIT)
static int ATOMIC_OPTIMIZE stress_atomic_uint64(
	stress_args_t *args,
	double *duration,
	double *count)
{
	static int idx = 0;
	int rc = 0;

	DO_ATOMIC_OPS(args, uint64_t, &g_shared->atomic.val64[idx], duration, count, rc);
	idx++;
	idx &= (SIZEOF_ARRAY(g_shared->atomic.val64) - 1);

	return rc;
}
#endif

static int ATOMIC_OPTIMIZE stress_atomic_uint32(
	stress_args_t *args,
	double *duration,
	double *count)
{
	static int idx = 0;
	int rc = 0;

	DO_ATOMIC_OPS(args, uint32_t, &g_shared->atomic.val32[idx], duration, count, rc);
	idx += 3;
	idx &= (SIZEOF_ARRAY(g_shared->atomic.val32) - 1);

	return rc;
}

static int ATOMIC_OPTIMIZE stress_atomic_uint16(
	stress_args_t *args,
	double *duration,
	double *count)
{
	static int idx = 0;
	int rc = 0;

	DO_ATOMIC_OPS(args, uint16_t, &g_shared->atomic.val16[idx], duration, count, rc);
	idx += 5;
	idx &= (SIZEOF_ARRAY(g_shared->atomic.val16) - 1);

	return rc;
}

static int ATOMIC_OPTIMIZE stress_atomic_uint8(
	stress_args_t *args,
	double *duration,
	double *count)
{
	static int idx = 0;
	int rc = 0;

	DO_ATOMIC_OPS(args, uint8_t, &g_shared->atomic.val8[idx], duration, count, rc);
	idx += 9;
	idx &= (SIZEOF_ARRAY(g_shared->atomic.val8) - 1);

	return rc;
}

typedef struct {
	const atomic_func_t func;
	const char *name;
	const int arch_bits;
} atomic_func_info_t;

static atomic_func_info_t atomic_func_info[] = {
#if defined(STRESS_ATOMIC_64BIT) &&	\
    defined(HAVE_INT128_T)
	{ stress_atomic_uint128, "uint128", 64 },
#endif
#if defined(STRESS_ATOMIC_64BIT)
	{ stress_atomic_uint64,	 "uint64",  64 },
#endif
	{ stress_atomic_uint32,	 "uint32",  32 },
	{ stress_atomic_uint16,	 "uint16",  32 },
	{ stress_atomic_uint8,	 "uint8",   32 },
};

typedef struct {
	stress_metrics_t metrics[STRESS_ATOMIC_MAX_FUNCS];
	stress_pid_t s_pid;
} stress_atomic_info_t;

static int stress_atomic_exercise(
	stress_args_t *args,
	stress_atomic_info_t *atomic_info,
	const int arch_bits)
{
	const int rounds = 1000;

	do {
		register size_t i;

		for (i = 0; i < STRESS_ATOMIC_MAX_FUNCS; i++) {
			if (arch_bits >= atomic_func_info[i].arch_bits) {
				register int j;

				const atomic_func_t func = atomic_func_info[i].func;

				for (j = 0; j < rounds; j++) {
					if (UNLIKELY(func(args, &atomic_info->metrics[i].duration,
							  &atomic_info->metrics[i].count) < 0))
						return -1;
				}
			}
		}
		stress_bogo_inc(args);
	} while (stress_continue(args));

	return 0;
}

/*
 *  stress_atomic()
 *      stress gcc atomic memory ops
 */
static int stress_atomic(stress_args_t *args)
{
	size_t i, j, atomic_info_sz;
	stress_atomic_info_t *atomic_info;
	stress_pid_t *s_pid_head = NULL;
	const size_t n_atomic_procs = STRESS_ATOMIC_MAX_PROCS + 1;
	const int arch_bits = (sizeof(long int) == sizeof(uint64_t)) ? 64 : 32;
	int rc = EXIT_SUCCESS;

	atomic_info_sz = sizeof(*atomic_info) * n_atomic_procs;
	atomic_info = (stress_atomic_info_t *)stress_mmap_populate(NULL,
			atomic_info_sz, PROT_READ | PROT_WRITE,
			MAP_SHARED | MAP_ANONYMOUS, -1, 0);
	if (atomic_info == MAP_FAILED) {
		pr_inf_skip("%s: could not mmap share metrics of "
			"%zu bytes%s, skipping stressor\n",
			args->name, atomic_info_sz, stress_get_memfree_str());
		return EXIT_NO_RESOURCE;
	}
	stress_set_vma_anon_name(atomic_info, atomic_info_sz, "atomic-data");

	for (i = 0; i < n_atomic_procs; i++) {
		stress_sync_start_init(&atomic_info[i].s_pid);
		stress_zero_metrics(atomic_info[i].metrics, STRESS_ATOMIC_MAX_FUNCS);
	}

	stress_set_proc_state(args->name, STRESS_STATE_SYNC_WAIT);

	for (i = 0; i < STRESS_ATOMIC_MAX_PROCS; i++) {
		pid_t pid;

		pid = fork();
		if (pid == 0) {
			stress_set_proc_state(args->name, STRESS_STATE_SYNC_WAIT);
			stress_sync_start_wait_s_pid(&atomic_info[i].s_pid);
			stress_set_proc_state(args->name, STRESS_STATE_RUN);
			if (stress_atomic_exercise(args, &atomic_info[i], arch_bits) < 0)
				_exit(EXIT_FAILURE);
			_exit(EXIT_SUCCESS);
		}
		atomic_info[i].s_pid.pid = pid;
		if (pid > 0)
			stress_sync_start_s_pid_list_add(&s_pid_head, &atomic_info[i].s_pid);
	}

	stress_sync_start_wait(args);
	stress_sync_start_cont_list(s_pid_head);
	stress_set_proc_state(args->name, STRESS_STATE_RUN);

	if (stress_atomic_exercise(args, &atomic_info[n_atomic_procs - 1], arch_bits) < 0)
		rc = EXIT_FAILURE;

	for (i = 0; i < STRESS_ATOMIC_MAX_PROCS; i++) {
		if (atomic_info[i].s_pid.pid > 0) {
			int status;

			if (waitpid(atomic_info[i].s_pid.pid, &status, WNOHANG) == atomic_info[i].s_pid.pid) {
				if (WIFEXITED(status)) {
					if (WEXITSTATUS(status) == EXIT_FAILURE)
						rc = EXIT_FAILURE;
					continue;
				}
			}

			if (shim_kill(atomic_info[i].s_pid.pid, 0) == 0) {
				stress_force_killed_bogo(args);
				(void)stress_kill_pid(atomic_info[i].s_pid.pid);
			}
			(void)waitpid(atomic_info[i].s_pid.pid, &status, 0);
		}
	}

	for (j = 0; j < STRESS_ATOMIC_MAX_FUNCS; j++) {
		if (arch_bits >= atomic_func_info[j].arch_bits) {
			double duration = 0.0, count = 0.0, rate;
			char str[60];

			for (i = 0; i < n_atomic_procs; i++) {
				duration += atomic_info[i].metrics[j].duration;
				count += atomic_info[i].metrics[j].count;
			}
			rate = (duration > 0.0) ? count / duration : 0.0;
			(void)snprintf(str, sizeof(str), "%s atomic ops per sec", atomic_func_info[j].name);
			stress_metrics_set(args, j, str, rate, STRESS_METRIC_HARMONIC_MEAN);
		}
	}

	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);

	(void)munmap((void *)atomic_info, atomic_info_sz);

	return rc;
}

const stressor_info_t stress_atomic_info = {
	.stressor = stress_atomic,
	.classifier = CLASS_CPU | CLASS_MEMORY,
	.verify = VERIFY_ALWAYS,
	.help = help
};

#else
const stressor_info_t stress_atomic_info = {
	.stressor = stress_unimplemented,
	.classifier = CLASS_CPU | CLASS_MEMORY,
	.verify = VERIFY_ALWAYS,
	.help = help,
	.unimplemented_reason = "built without gcc __atomic builtin functions"
};
#endif
