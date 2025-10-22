/*
 * Copyright (C) 2024-2025 Colin Ian King.
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
#include "core-asm-arm.h"
#include "core-asm-generic.h"
#include "core-cpu-cache.h"
#include "core-killpid.h"
#include "core-mmap.h"
#include "core-numa.h"

#include <sched.h>

static const stress_help_t help[] = {
	{ NULL,	"spinmem",	    "start N workers exercising shared memory spin write/read operations" },
	{ NULL, "spinmem-affinity", "use CPU affinity (specific CPUS can be defined by --taskset option)" },
	{ NULL, "spinmem-method",   "select method of write/reads, default is 32bit" },
	{ NULL, "spinmem-numa",     "move pages to randomly chosen NUMA nodes" },
	{ NULL, "spinmem-ops",	    "stop after N bogo shared memory spin write/read operations" },
	{ NULL, "spinmem-yield",    "force scheduling yeilds after each spin write/read operation" },
	{ NULL, NULL,		    NULL }
};

#define SPINMEM_LOOPS	(1000)
#define SPINMEM_OFFSET	(1)
#define SPINMEM_SPINS	(1000000)

#if defined(HAVE_SIGLONGJMP)
static volatile bool do_jmp = true;
static sigjmp_buf jmp_env;
#endif

static inline void ALWAYS_INLINE stress_spinmem_mfence(void)
{
	shim_mfence();
}

static inline void ALWAYS_INLINE stress_spinmem_mbarrier(void)
{
#if defined(HAVE_ASM_ARM_DMB_SY)
	stress_asm_arm_dmb_sy();
#endif
}

#define SPINMEM_MB()			\
do {					\
	stress_asm_mb();		\
	stress_spinmem_mfence();	\
	stress_spinmem_mbarrier();	\
} while (0)				\

#define SPINMEM_FLUSH(ptr)		\
	stress_cpu_data_cache_flush((void *)ptr, 64)

#if defined(HAVE_SIGLONGJMP)
/*
 *  stress_spinmem_handler()
 *      SIGALRM generic handler
 */
static void MLOCKED_TEXT stress_spinmem_handler(int signum)
{
	(void)signum;

	if (do_jmp) {
		do_jmp = false;
		siglongjmp(jmp_env, 1);         /* Ugly, bounce back */
		stress_no_return();
	}
}
#endif

#define SPINMEM_READER(name, type)		\
static void OPTIMIZE3 name(uint8_t *data, const bool spinmem_yield)	\
{						\
	volatile type *uptr = (type *)data;	\
	register type val = (type)0;		\
	register int i;				\
						\
	for (i = 0; i < SPINMEM_LOOPS; i++) {	\
		register type newval;		\
		register int spins = 0;		\
						\
		do {				\
			newval = uptr[0];	\
			SPINMEM_MB();		\
			if (spins++ > SPINMEM_SPINS) \
				break;		\
		} while (newval == val);	\
						\
		uptr[SPINMEM_OFFSET] = newval;	\
		SPINMEM_FLUSH(data);		\
		SPINMEM_MB();			\
		val = newval;			\
		if (spinmem_yield)		\
			shim_sched_yield();	\
	}					\
}


#define SPINMEM_WRITER(name, type)		\
static void OPTIMIZE3 name(uint8_t *data, const bool spinmem_yield)	\
{						\
	volatile type *uptr = (type *)data;	\
	register type v = *data;		\
	register int i;				\
						\
	for (i = 0; i < SPINMEM_LOOPS; i++) {	\
		register int spins = 0;		\
						\
		v++;				\
		SPINMEM_FLUSH(data);		\
		SPINMEM_MB();			\
		uptr[0] = v;			\
		SPINMEM_FLUSH(data);		\
		SPINMEM_MB();			\
						\
		while (uptr[SPINMEM_OFFSET] != v) { \
			if (spins++ > SPINMEM_SPINS) \
				break;		\
			SPINMEM_MB();		\
		}				\
		if (spinmem_yield)		\
			shim_sched_yield();	\
	}					\
}

SPINMEM_READER(stress_spinmem_reader8, uint8_t)
SPINMEM_WRITER(stress_spinmem_writer8, uint8_t)

SPINMEM_READER(stress_spinmem_reader16, uint16_t)
SPINMEM_WRITER(stress_spinmem_writer16, uint16_t)

SPINMEM_READER(stress_spinmem_reader32, uint32_t)
SPINMEM_WRITER(stress_spinmem_writer32, uint32_t)

SPINMEM_READER(stress_spinmem_reader64, uint64_t)
SPINMEM_WRITER(stress_spinmem_writer64, uint64_t)

#if defined(HAVE_INT128_T)
SPINMEM_READER(stress_spinmem_reader128, __uint128_t)
SPINMEM_WRITER(stress_spinmem_writer128, __uint128_t)
#endif

typedef void (*spinmem_func_t)(uint8_t *data, const bool spinmem_yield);

typedef struct {
	const char *name;
	const spinmem_func_t reader;
	const spinmem_func_t writer;
} spinmem_funcs_t;

static const spinmem_funcs_t spinmem_funcs[] = {
	{ "8bit",  stress_spinmem_reader8,  stress_spinmem_writer8 },
	{ "16bit", stress_spinmem_reader16, stress_spinmem_writer16 },
	{ "32bit", stress_spinmem_reader32, stress_spinmem_writer32 },
	{ "64bit", stress_spinmem_reader64, stress_spinmem_writer64 },
#if defined(HAVE_INT128_T)
	{ "128bit", stress_spinmem_reader128, stress_spinmem_writer128 },
#endif
};

#if defined(HAVE_SCHED_SETAFFINITY)
/*
 *  stress_spinmem_change_affinity()
 *	move current process to a random CPU
 */
static inline void stress_spinmem_change_affinity(const uint32_t n_cpus, const uint32_t *cpus)
{
	if (LIKELY(n_cpus > 0)) {
		cpu_set_t mask;
		const uint32_t i = stress_mwc32modn(n_cpus);
		const uint32_t cpu = cpus[i];

		CPU_ZERO(&mask);
		CPU_SET((int)cpu, &mask);
		VOID_RET(int, sched_setaffinity(0, sizeof(mask), &mask));
	}
}
#endif

#if defined(HAVE_LINUX_MEMPOLICY_H)
static void stress_spinmem_numa(
	stress_args_t *args,
	const int max,
	uint8_t *mapping,
	const size_t mapping_size,
	const bool spinmem_numa,
        stress_numa_mask_t *numa_mask,
        stress_numa_mask_t *numa_nodes)
{
	static int numa_count = 0;

	if (spinmem_numa && numa_mask && numa_nodes) {
		numa_count++;
		if (numa_count > max) {
			stress_numa_randomize_pages(args, numa_nodes,
						numa_mask, mapping,
						mapping_size, mapping_size);
			numa_count = 0;
		}
	}
}
#endif


/*
 *  stress_spinmem()
 *      stress spin write/reads on shared memory
 */
static int stress_spinmem(stress_args_t *args)
{
	NOCLOBBER int rc = EXIT_SUCCESS;
	NOCLOBBER pid_t pid;
	NOCLOBBER double duration = 0.0, count = 0.0;
	uint8_t *mapping;
	double rate;
	size_t spinmem_method = 2; /* 32bit default */
	spinmem_func_t spinmem_reader, spinmem_writer;
	bool spinmem_affinity = false;
	bool spinmem_numa = false;
	bool spinmem_yield = false;
#if defined(HAVE_SCHED_SETAFFINITY)
	uint32_t *cpus = NULL;
	uint32_t n_cpus = stress_get_usable_cpus(&cpus, true);
#endif
#if defined(HAVE_LINUX_MEMPOLICY_H)
        stress_numa_mask_t *numa_mask = NULL;
        stress_numa_mask_t *numa_nodes = NULL;
	const size_t page_size = args->page_size;
#endif
#if defined(HAVE_SIGLONGJMP)
	struct sigaction old_action;
	int ret;
#endif

	(void)stress_get_setting("spinmem-affinity", &spinmem_affinity);
	(void)stress_get_setting("spinmem-method", &spinmem_method);
	(void)stress_get_setting("spinmem-numa", &spinmem_numa);
	(void)stress_get_setting("spinmem-yield", &spinmem_yield);

#if !defined(HAVE_SCHED_SETAFFINITY)
	if ((spinmem_affinity) && (stress_instance_zero(args))) {
		pr_inf("%s: disabling spinmem_affinity option, "
			"CPU affinity not supported\n", args->name);
		spinmem_affinity = false;
	}
#endif
	if (spinmem_numa) {
#if defined(HAVE_LINUX_MEMPOLICY_H)
		stress_numa_mask_and_node_alloc(args, &numa_nodes, &numa_mask, "--spinmem-numa", &spinmem_numa);
#else
		if (stress_instance_zero(args))
			pr_inf("%s: --spinmem-numa selected but not supported by this system, disabling option\n",
				args->name);
		spinmem_numa = false;
#endif
	}

	mapping = (uint8_t *)stress_mmap_populate(NULL,
			args->page_size, PROT_READ | PROT_WRITE,
			MAP_SHARED | MAP_ANONYMOUS, -1, 0);
	if (mapping == MAP_FAILED) {
		pr_inf_skip("%s: failed to mmap a page of "
			"%zu bytes%s, errno=%d (%s), skipping stressor\n",
			args->name, args->page_size,
			stress_get_memfree_str(), errno, strerror(errno));
		rc = EXIT_NO_RESOURCE;
		goto tidy_cpus;
	}
	stress_set_vma_anon_name(mapping, args->page_size, "spinmem-data");

#if defined(HAVE_SIGLONGJMP)
	ret = sigsetjmp(jmp_env, 1);
	if (ret) {
		/*
		 * We return here if SIGALRM jmp'd back
		 */
		 (void)stress_sigrestore(args->name, SIGALRM, &old_action);
		goto tidy;
	}
	if (stress_sighandler(args->name, SIGALRM, stress_spinmem_handler, &old_action) < 0) {
		rc = EXIT_FAILURE;
		goto tidy;
	}
#endif

	spinmem_reader = spinmem_funcs[spinmem_method].reader;
	spinmem_writer = spinmem_funcs[spinmem_method].writer;

	stress_set_proc_state(args->name, STRESS_STATE_SYNC_WAIT);
	stress_sync_start_wait(args);
	stress_set_proc_state(args->name, STRESS_STATE_RUN);

	pid = fork();
	if (pid < 0) {
		pr_inf_skip("%s: could not fork child process, errno=%d (%s), skipping stressor\n",
			args->name, errno, strerror(errno));
		rc = EXIT_NO_RESOURCE;
		goto tidy;
	} else if (pid == 0) {
		stress_set_proc_state(args->name, STRESS_STATE_RUN);

#if defined(HAVE_SCHED_SETAFFINITY)
		if (spinmem_affinity) {
			do {
				register int i;

				for (i = 0; i < 1000; i++) {
					spinmem_reader(mapping, spinmem_yield);
					stress_spinmem_change_affinity(n_cpus, cpus);

#if defined(HAVE_LINUX_MEMPOLICY_H)
					stress_spinmem_numa(args, 200, mapping, page_size,
							spinmem_numa, numa_mask, numa_nodes);
#endif
				}
			} while (stress_continue(args));
			_exit(0);
		}
#endif
		do {
			spinmem_reader(mapping, spinmem_yield);
#if defined(HAVE_LINUX_MEMPOLICY_H)
			stress_spinmem_numa(args, 2000, mapping, page_size,
				spinmem_numa, numa_mask, numa_nodes);
#endif
		} while (stress_continue(args));
		_exit(0);
	} else {
#if defined(HAVE_SCHED_SETAFFINITY)
		if (spinmem_affinity) {
			do {
				register int i;

				for (i = 0; i < 100; i++) {
					double t = stress_time_now();

					spinmem_writer(mapping, spinmem_yield);
					duration += stress_time_now() - t;
					count += SPINMEM_LOOPS;
					stress_bogo_inc(args);
				}
				stress_spinmem_change_affinity(n_cpus, cpus);
#if defined(HAVE_LINUX_MEMPOLICY_H)
				stress_spinmem_numa(args, 1, mapping, page_size,
							spinmem_numa, numa_mask, numa_nodes);
#endif
			} while (stress_continue(args));
			goto completed;
		}
#endif
		do {
			double t = stress_time_now();

			spinmem_writer(mapping, spinmem_yield);
			duration += stress_time_now() - t;
			count += SPINMEM_LOOPS;
			stress_bogo_inc(args);
#if defined(HAVE_LINUX_MEMPOLICY_H)
			stress_spinmem_numa(args, 2000, mapping, page_size,
				spinmem_numa, numa_mask, numa_nodes);
#endif
		} while (stress_continue(args));
	}

#if defined(HAVE_SCHED_SETAFFINITY)
completed:
#endif
#if defined(HAVE_SIGLONGJMP)
	do_jmp = false;
	(void)stress_sigrestore(args->name, SIGALRM, &old_action);
#endif
tidy:
	stress_kill_and_wait(args, pid, SIGKILL, false);
	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);

	rate = (count > 0.0) ? duration / count : 0.0;
	stress_metrics_set(args, 0, "nanoseconds per spin write/read",
                rate * STRESS_DBL_NANOSECOND, STRESS_METRIC_HARMONIC_MEAN);

	(void)munmap((void *)mapping, args->page_size);
tidy_cpus:
#if defined(HAVE_LINUX_MEMPOLICY_H)
	if (numa_mask)
		stress_numa_mask_free(numa_mask);
	if (numa_nodes)
		stress_numa_mask_free(numa_nodes);
#endif
#if defined(HAVE_SCHED_SETAFFINITY)
	stress_free_usable_cpus(&cpus);
#endif

	return rc;
}

static const char *stress_spinmem_method(const size_t i)
{
	return (i < SIZEOF_ARRAY(spinmem_funcs)) ? spinmem_funcs[i].name : NULL;
}

static const stress_opt_t opts[] = {
	{ OPT_spinmem_affinity, "spinmem-affinity", TYPE_ID_BOOL, 0, 1, NULL },
	{ OPT_spinmem_method,   "spinmem-method",   TYPE_ID_SIZE_T_METHOD, 0, 0, stress_spinmem_method },
	{ OPT_spinmem_numa,     "spinmem-numa",     TYPE_ID_BOOL, 0, 1, NULL },
	{ OPT_spinmem_yield,    "spinmem-yield",    TYPE_ID_BOOL, 0, 1, NULL },
	END_OPT,
};

const stressor_info_t stress_spinmem_info = {
	.stressor = stress_spinmem,
	.classifier = CLASS_CPU | CLASS_MEMORY | CLASS_CPU_CACHE,
	.verify = VERIFY_NONE,
	.opts = opts,
	.help = help
};
