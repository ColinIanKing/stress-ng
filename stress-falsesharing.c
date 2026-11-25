/*
 * Copyright (C) 2026      Colin Ian King.
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
#include "core-asm-generic.h"
#include "core-builtin.h"
#include "core-cpu-cache.h"
#include "core-mmap.h"
#include "core-pthread.h"

#define MAX_THREADS	(1024)
#define MAX_LOOPS	(10000)

#define MIN_MS		(100)
#define MAX_MS		(1000)
#define DEFAULT_MS	(100)

#define MIN_STRIDE	(0)
#define MAX_STRIDE	(256)
#define DEFAULT_STRIDE	(1)

static const stress_help_t help[] = {
	{ NULL,	"falsesharing N",        "start N workers exercising sharing of memory with an atomic add" },
	{ NULL, "falsesharing-ms N",     "loop for minimim of N milliseconds per million atomic add ops" },
	{ NULL,	"falsesharing-ops N",    "stop after N operations" },
	{ NULL, "falsesharing-stride N", "stride size for atomic add memory locations in bytes" },
	{ NULL,	NULL,                     NULL },
};

static const stress_opt_t opts[] = {
	{ OPT_falsesharing_ms,     "falsesharing-ms",     TYPE_ID_INT, MIN_MS, MAX_MS, NULL },
	{ OPT_falsesharing_stride, "falsesharing-stride", TYPE_ID_SIZE_T, MIN_STRIDE, MAX_STRIDE, NULL },
	END_OPT,
};

#if defined(HAVE_LIB_PTHREAD) &&	\
    defined(HAVE_ATOMIC_FETCH_ADD)

static int falsesharing_ms;
static size_t falsesharing_stride;

typedef struct stress_pthread_info {
	stress_args_t *args;
	uint8_t *atomic;
	pthread_t pthread;	/* The pthread */
	double duration;
	double count;
	int ret;		/* pthread create return */
	uint8_t	val_got;
	uint8_t val_expected;
} stress_pthread_info_t;

static pthread_barrier_t barrier;

static void OPTIMIZE3 *stress_falsesharing_pthread(void *arg)
{
	stress_pthread_info_t *info = (stress_pthread_info_t *)arg;
	int ret;
	double t_start;
	double t_now;
	double t_end;

	(void)arg;
	stress_args_t *args = info->args;
	register uint8_t *atomic = info->atomic;
	uint8_t val_start = stress_mwc8();

	ret = pthread_barrier_wait(&barrier);
	if ((ret != 0) && (ret != PTHREAD_BARRIER_SERIAL_THREAD)) {
		pr_inf("%s: pthread_barrier_wait failed, errno=%d (%s)\n",
			args->name, ret, strerror(ret));
		pthread_exit(0);
	}

	*atomic = val_start;

	t_start = stress_time_now();
	t_now = t_start;
	t_end = t_start + ((double)falsesharing_ms / 1000.0);
	do {
		const double t = t_now;
		register int i;

		for (i = 0; i < MAX_LOOPS; i++) {
			__atomic_fetch_add(atomic, (uint8_t)1, __ATOMIC_ACQUIRE);
			stress_asm_mb();
		}
		t_now = stress_time_now();
		info->duration += t_now - t;
		info->count += 1.0;
		val_start += MAX_LOOPS;
	} while (t_now < t_end);

	info->val_got = *atomic;
	info->val_expected = val_start;

	return &g_nowt;
}

static int stress_falsesharing_exercise(
	stress_args_t *args,
	stress_pthread_info_t *pthreads,
	const size_t n_pthreads,
	size_t *max_pthreads)
{
	int ret;
	int rc = EXIT_SUCCESS;
	size_t i;
	size_t n;

	ret = pthread_barrier_init(&barrier, NULL, (unsigned int)n_pthreads);
	if (ret != 0) {
		pr_inf_skip("%s: pthread_barrier_init failed, errno=%d (%s), skipping stressor\n",
			args->name, ret, strerror(ret));
		return EXIT_NO_RESOURCE;
	}
	for (i = 0; i < n_pthreads; i++)
		pthreads[i].ret = -1;

	for (n = 0; n < n_pthreads; n++) {
		pthreads[n].args = args;
		pthreads[n].ret = pthread_create(&pthreads[n].pthread, NULL,
				stress_falsesharing_pthread, &pthreads[n]);
		if (UNLIKELY(pthreads[n].ret)) {
			if (pthreads[n].ret == EAGAIN)
				break;
			pr_inf_skip("%s: pthread_create failed, errno=%d (%s), skipping stessor\n",
				args->name, pthreads[n].ret, strerror(pthreads[n].ret));
			(void)pthread_barrier_destroy(&barrier);
			rc = EXIT_NO_RESOURCE;
			goto reap;
		}
		if (UNLIKELY(pthreads[n].val_got != pthreads[n].val_expected)) {
			pr_fail("%s: pthread %zu detected unexepcted memory value at offset 0x%zx, "
				"got 0x%2.2x, expected 0x%2.2x\n",
				args->name, n, n * falsesharing_stride,
				pthreads[n].val_got, pthreads[n].val_expected);
			rc = EXIT_FAILURE;
		}
	}

reap:
	for (i = 0; i < n; i++) {
		if (pthreads[i].ret == 0) {
			pthread_join(pthreads[i].pthread, NULL);
		}
	}
	if (*max_pthreads < n)
		*max_pthreads = n;
	(void)pthread_barrier_destroy(&barrier);

	return rc;
}

/*
 *  stress_falsesharing
 *	stress cache with atomic false sharing operations
 */
static int stress_falsesharing(stress_args_t *args)
{
	stress_pthread_info_t pthreads[MAX_THREADS];
	size_t max_cpus = (size_t)stress_cpus_online_get();
	size_t max_pthreads = 0;
	size_t n_pthreads;
	size_t i;
	size_t atomics_size;
	int rc = EXIT_SUCCESS;
	bool got_results = false;

	uint8_t *atomics;

	falsesharing_ms = DEFAULT_MS;
	falsesharing_stride = DEFAULT_STRIDE;

	(void)stress_setting_get("falsesharing-ms", &falsesharing_ms);
	(void)stress_setting_get("falsesharing-stride", &falsesharing_stride);
	if (falsesharing_stride == 0)
		falsesharing_stride = (size_t)stress_cpu_cache_line_size_get();

	if (max_cpus > MAX_THREADS)
		max_cpus = MAX_THREADS;

	if (stress_instance_zero(args)) {
		pr_inf("%s: exercising a maximum of %zu pthreads, stride %zu bytes\n",
			args->name, max_cpus, falsesharing_stride);
	}

	atomics_size = max_cpus * falsesharing_stride;
	atomics = (uint8_t *)stress_mmap_populate(NULL, atomics_size,
				PROT_READ | PROT_WRITE,
				MAP_SHARED | MAP_ANONYMOUS, -1, 0);
	if (atomics == MAP_FAILED) {
		pr_inf_skip("%s: failed to mmap %zu bytes%s, errno=%d (%s), skipping stressor\n",
			args->name, atomics_size, stress_memory_free_get(), errno, strerror(errno));
		return EXIT_NO_RESOURCE;
	}

	(void)shim_memset(&pthreads, 0, sizeof(pthreads));

	for (i = 0; i < max_cpus; i++) {
		pthreads[i].count = 0.0;
		pthreads[i].duration = 0.0;
		pthreads[i].atomic = atomics + (i * falsesharing_stride);
		pthreads[i].val_got = 0;
		pthreads[i].val_expected = 0;
	}

	stress_proc_state_set(args->name, STRESS_STATE_SYNC_WAIT);
	stress_sync_start_wait(args);
	stress_proc_state_set(args->name, STRESS_STATE_RUN);

	do {
		for (n_pthreads = 1; n_pthreads <= max_cpus; n_pthreads++) {
			if (!stress_continue(args))
				break;
			rc = stress_falsesharing_exercise(args, pthreads, n_pthreads, &max_pthreads);
			if (rc != EXIT_SUCCESS)
				break;
			stress_bogo_inc(args);
		}
	} while (stress_continue(args));
	stress_proc_state_set(args->name, STRESS_STATE_DEINIT);

	for (i = 0; i < max_pthreads; i++) {
		if (pthreads[i].count > 0) {
			got_results = true;
			break;
		}
	}

	if (got_results) {
		for (i = 0; i < max_pthreads; i++) {
			if (pthreads[i].duration > 0.0) {
				char msg[64];
				double rate = (double)MAX_LOOPS * pthreads[i].count / pthreads[i].duration;

				(void)snprintf(msg, sizeof(msg), "atomic ops per sec with %zu pthreads", i + 1);
				stress_metrics_set(args, msg, rate, STRESS_METRIC_HARMONIC_MEAN);
			}
		}
	}

	(void)munmap((void *)atomics, atomics_size);

	return rc;
}

static const stress_exercises_t exercises[] = {
	STRESS_EX_FEATURE("atomic"),
	STRESS_EX_FEATURE("d-cache"),
	STRESS_EX_FEATURE("d-cache-read-miss"),

#if defined(__linux__)
	STRESS_EX_SYSCALL("clone"),
	STRESS_EX_SYSCALL("futex"),
#endif

	STRESS_EX_END,
};

const stressor_info_t stress_falsesharing_info = {
	.stressor = stress_falsesharing,
	.classifier = CLASS_CPU_CACHE,
	.verify = VERIFY_ALWAYS,
	.help = help,
	.opts = opts,
	.max_metrics_items = 1024,
	.exercises = exercises,
};

#else

const stressor_info_t stress_pthread_info = {
	.stressor = stress_unimplemented,
	.classifier = CLASS_CPU_CACHE,
	.verify = VERIFY_ALWAYS,
	.help = help,
	.opts = opts,
	.unimplemented_reason = "built without pthread support or atomic fetch add support"
};
#endif
