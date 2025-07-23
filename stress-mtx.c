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
#include "core-builtin.h"
#include "core-pthread.h"

#if defined(HAVE_THREADS_H)
#include <threads.h>
#endif

#define MIN_MTX_PROCS		(2)
#define MAX_MTX_PROCS		(64)
#define DEFAULT_MTX_PROCS	(2)

static const stress_help_t help[] = {
	{ NULL,	"mtx N",	"start N workers exercising ISO C mutex operations" },
	{ NULL,	"mtx-ops N",	"stop after N ISO C mutex bogo operations" },
	{ NULL, "mtx-procs N",	"select the number of concurrent processes" },
	{ NULL,	NULL,		NULL }
};

static const stress_opt_t opts[] = {
	{ OPT_mtx_procs, "mtx-procs", TYPE_ID_UINT64, MIN_MTX_PROCS, MAX_MTX_PROCS, NULL },
	END_OPT,
};

#if defined(HAVE_LIB_PTHREAD) &&	\
    defined(HAVE_THREADS_H) &&		\
    defined(HAVE_MTX_T) &&		\
    defined(HAVE_MTX_DESTROY) &&	\
    defined(HAVE_MTX_INIT)

static mtx_t ALIGN64 mtx;

typedef struct {
	stress_args_t *args;
	pthread_t pthread;
	int ret;
	double lock_duration;
	double lock_count;
} pthread_info_t;

/*
 *  mtx_exercise()
 *	exercise the mtx
 */
static void OPTIMIZE3 *mtx_exercise(void *arg)
{
	pthread_info_t *pthread_info = (pthread_info_t *)arg;
	stress_args_t *args = pthread_info->args;
	int metrics_count = 0;

	stress_mwc_reseed();
	stress_random_small_sleep();

	do {
		if (LIKELY(metrics_count > 0)) {
			/* fast non-metrics lock path */
			if (UNLIKELY(mtx_lock(&mtx) != thrd_success)) {
				pr_fail("%s: mtx_lock failed\n", args->name);
				break;
			}
			pthread_info->lock_count += 1.0;
		} else {
			/* slow metrics lock path */
			double t;

			t = stress_time_now();
			if (UNLIKELY(mtx_lock(&mtx) != thrd_success)) {
				pr_fail("%s: mtx_lock failed\n", args->name);
				break;
			}
			pthread_info->lock_duration += stress_time_now() - t;
			pthread_info->lock_count += 1.0;
		}
		metrics_count = (UNLIKELY(metrics_count >= 1000)) ? 0 : metrics_count + 1;

		stress_bogo_inc(args);

		if (UNLIKELY(mtx_unlock(&mtx) != thrd_success)) {
			pr_fail("%s: pthread_mtx_unlock failed\n", args->name);
			break;
		}
	} while (stress_continue(args));

	return &g_nowt;
}

/*
 *  stress_mtx()
 *	stress system with priority changing mtx lock/unlocks
 */
static int stress_mtx(stress_args_t *args)
{
	size_t i;
	bool created = false;
	pthread_info_t pthread_info[MAX_MTX_PROCS];
	uint64_t mtx_procs = DEFAULT_MTX_PROCS;
	double duration = 0.0, count = 0.0, rate;

	if (!stress_get_setting("mtx-procs", &mtx_procs)) {
		if (g_opt_flags & OPT_FLAGS_MAXIMIZE)
			mtx_procs = MAX_MTX_PROCS;
		if (g_opt_flags & OPT_FLAGS_MINIMIZE)
			mtx_procs = MIN_MTX_PROCS;
	}

	(void)shim_memset(&pthread_info, 0, sizeof(pthread_info));

	if (mtx_init(&mtx, mtx_plain) != thrd_success) {
		pr_fail("%s: pthread_mtx_init failed\n", args->name);
		return EXIT_FAILURE;
	}

	stress_set_proc_state(args->name, STRESS_STATE_SYNC_WAIT);
	stress_sync_start_wait(args);
	stress_set_proc_state(args->name, STRESS_STATE_RUN);

	for (i = 0; i < mtx_procs; i++) {
		pthread_info[i].args = args;
		pthread_info[i].lock_duration = 0.0;
		pthread_info[i].lock_count = 0.0;
		pthread_info[i].ret = pthread_create(&pthread_info[i].pthread, NULL,
                                mtx_exercise, (void *)&pthread_info[i]);
		if ((pthread_info[i].ret) && (pthread_info[i].ret != EAGAIN)) {
			pr_fail("%s: pthread create failed, errno=%d (%s)\n",
				args->name, pthread_info[i].ret, strerror(pthread_info[i].ret));
			break;
		}
		if (UNLIKELY(!stress_continue_flag()))
			break;
		created = true;
	}

	if (!created) {
		pr_inf("%s: could not create any pthreads\n", args->name);
		return EXIT_NO_RESOURCE;
	}

	/* Wait for termination */
	while (stress_continue(args))
		(void)shim_pause();

	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);

	for (i = 0; i < mtx_procs; i++) {
		if (pthread_info[i].ret)
			continue;

		VOID_RET(int, pthread_join(pthread_info[i].pthread, NULL));
		duration += pthread_info[i].lock_duration;
		count += pthread_info[i].lock_count;
	}
	mtx_destroy(&mtx);

	rate = (count > 0.0) ? (duration / count) : 0.0;
	stress_metrics_set(args, 0, "nanosecs per mtx",
		rate * STRESS_DBL_NANOSECOND, STRESS_METRIC_HARMONIC_MEAN);

	return EXIT_SUCCESS;
}

const stressor_info_t stress_mtx_info = {
	.stressor = stress_mtx,
	.classifier = CLASS_OS | CLASS_SCHEDULER,
	.verify = VERIFY_ALWAYS,
	.opts = opts,
	.help = help
};
#else
const stressor_info_t stress_mtx_info = {
	.stressor = stress_unimplemented,
	.classifier = CLASS_OS | CLASS_SCHEDULER,
	.opts = opts,
	.verify = VERIFY_ALWAYS,
	.help = help,
	.unimplemented_reason = "built without librt, threads.h, pthread_np.h or pthread support"
};
#endif
