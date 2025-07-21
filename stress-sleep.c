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
#include "core-asm-x86.h"
#include "core-builtin.h"
#include "core-cpu.h"
#include "core-cpuidle.h"
#include "core-pthread.h"

#if defined(HAVE_SYS_SELECT_H)
#include <sys/select.h>
#else
UNEXPECTED
#endif

#define MIN_SLEEP		(1)
#define MAX_SLEEP		(30000)
#define DEFAULT_SLEEP		(256)

#if defined(HAVE_LIB_PTHREAD)

typedef struct {
	stress_args_t *args;
	uint64_t sleep_max;
	pthread_t pthread;
	uint64_t underruns;
} stress_ctxt_t;

typedef struct {
	double monotonic;
	double time_now;
} stress_sleep_times_t;

static void *stress_sleep_counter_lock;
static volatile bool thread_terminate;
static sigset_t set;
#endif

static const stress_help_t help[] = {
	{ NULL,	"sleep N",	"start N workers performing various duration sleeps" },
	{ NULL,	"sleep-max P",	"create P threads at a time by each worker" },
	{ NULL,	"sleep-ops N",	"stop after N bogo sleep operations" },
	{ NULL,	NULL,		NULL }
};

static const stress_opt_t opts[] = {
	{ OPT_sleep_max, "sleep-max", TYPE_ID_UINT64, MIN_SLEEP, MAX_SLEEP, NULL },
	END_OPT,
};

#if defined(HAVE_LIB_PTHREAD)

static void MLOCKED_TEXT stress_sigalrm_handler(int signum)
{
	(void)signum;

	thread_terminate = true;
}

/*
 *  stress_sleep_time_now()
 *	Linux uses CLOCK_MONOTONIC for nanosecond
 *	sleeps with nanosleep(), however, other systems
 *	may use a different wall clock time. Get both
 *	and later use both to find the largest delta
 *	between measured times to overcome any warpages
 */
static void stress_sleep_time_now(stress_sleep_times_t *t)
{
	t->time_now = stress_time_now();
#if defined(HAVE_CLOCK_GETTIME) &&      \
    defined(CLOCK_MONOTONIC)
	struct timespec ts;

	if (clock_gettime(CLOCK_MONOTONIC, &ts) < 0) {
		/* fall back to re-using stress_time_now() time */
		t->monotonic = t->time_now;
	} else {
		t->monotonic = ts.tv_sec + (ts.tv_nsec * ONE_BILLIONTH);
	}
#endif
}

/*
 *  stress_time_delta()
 *	find largetst delta between t1 and t2 in terms
 *	of monotonic time and time_now call
 */
static double stress_time_delta(
	const stress_sleep_times_t *t1,
	const stress_sleep_times_t *t2)
{
	const double d1 = t2->monotonic - t1->monotonic;
	const double d2 = t2->time_now - t1->time_now;

	return (d1 > d2) ? d1 : d2;
}

/*
 *  stress_pthread_func()
 *	pthread that performs different ranges of sleeps
 */
static void *stress_pthread_func(void *c)
{
	stress_ctxt_t *ctxt = (stress_ctxt_t *)c;
	stress_args_t *args = ctxt->args;
#if defined(HAVE_ASM_X86_TPAUSE) &&	\
    !defined(HAVE_COMPILER_PCC)
	const bool x86_has_waitpkg = stress_cpu_x86_has_waitpkg();
#endif
	cpu_cstate_t *cstate_list = stress_cpuidle_cstate_list_head();

	while (stress_continue(args) && !thread_terminate) {
		bool eintr;
		cpu_cstate_t *cc;
		struct timespec tv;
		double delta, expected;
		stress_sleep_times_t t1, t2;
#if defined(HAVE_SYS_SELECT_H) &&	\
    defined(HAVE_SELECT)
		struct timeval timeout;
#endif

		if (UNLIKELY(!stress_continue_flag()))
			break;
		/*
		 *  exercise C state residency duration sleeps
		 *  to try and get CPU into deeper C states
		 */
		expected = 0.0;
		eintr = false;
		stress_sleep_time_now(&t1);
		for (cc = cstate_list; cc; cc = cc->next) {
			if (cc->residency > 0) {
				const unsigned long int residency_ns = (unsigned long int)(cc->residency + 1) * 1000UL;

				expected += cc->residency;
				tv.tv_nsec = residency_ns % 1000000000;
				tv.tv_sec = residency_ns / 1000000000;
				if (UNLIKELY(nanosleep(&tv, NULL) < 0)) {
					eintr |= (errno == EINTR);
					break;
				}
			}
		}
		stress_sleep_time_now(&t2);
		delta = stress_time_delta(&t1, &t2);
		/* don't check for clock warping */
		if (!eintr && (expected > 0.0) && (delta > 0.0)) {
			if (UNLIKELY(delta < expected / STRESS_DBL_NANOSECOND)) {
				pr_fail("%s: C state nanosleeps for %.f nanosecs took less than %.2f nanosecs to complete\n",
					args->name, expected, delta * STRESS_DBL_NANOSECOND);
				ctxt->underruns++;
			}
		}

		eintr = false;
		stress_sleep_time_now(&t1);
		if (UNLIKELY(!stress_continue_flag()))
			break;
		tv.tv_sec = 0;
		tv.tv_nsec = 1;
		if (UNLIKELY(nanosleep(&tv, NULL) < 0)) {
			eintr |= (errno == EINTR);
			break;
		}

		if (UNLIKELY(!stress_continue_flag()))
			break;
		tv.tv_sec = 0;
		tv.tv_nsec = 10;
		if (UNLIKELY(nanosleep(&tv, NULL) < 0)) {
			eintr |= (errno == EINTR);
			break;
		}

		if (UNLIKELY(!stress_continue_flag()))
			break;
		tv.tv_sec = 0;
		tv.tv_nsec = 100;
		if (UNLIKELY(nanosleep(&tv, NULL) < 0)) {
			eintr |= (errno == EINTR);
			break;
		}

		if (UNLIKELY(!stress_continue_flag()))
			break;
		tv.tv_sec = 0;
		tv.tv_nsec = 1000;
		if (UNLIKELY(nanosleep(&tv, NULL) < 0)) {
			eintr |= (errno == EINTR);
			break;
		}

		if (UNLIKELY(!stress_continue_flag()))
			break;
		tv.tv_sec = 0;
		tv.tv_nsec = 10000;
		if (UNLIKELY(nanosleep(&tv, NULL) < 0)) {
			eintr |= (errno == EINTR);
			break;
		}

		stress_sleep_time_now(&t2);
		delta = stress_time_delta(&t1, &t2);
		/* don't check for clock warping */
		if (!eintr && (delta > 0.0)) {
			expected = (1.0 + 10.0 + 100.0 + 1000.0 + 10000.0);
			if (UNLIKELY(delta < expected / STRESS_DBL_NANOSECOND)) {
				pr_fail("%s: nanosleeps for %.f nanosecs took less than %.2f nanosecs to complete\n",
					args->name, expected, delta * STRESS_DBL_NANOSECOND);
				ctxt->underruns++;
			}
		}

		eintr = false;
		stress_sleep_time_now(&t1);
		if (UNLIKELY(!stress_continue_flag()))
			break;
		if (UNLIKELY(shim_usleep(1) < 0)) {
			eintr |= (errno == EINTR);
			break;
		}

		if (UNLIKELY(!stress_continue_flag()))
			break;
		if (UNLIKELY(shim_usleep(10) < 0)) {
			eintr |= (errno == EINTR);
			break;
		}

		if (UNLIKELY(!stress_continue_flag()))
			break;
		if (UNLIKELY(shim_usleep(100) < 0)) {
			eintr |= (errno == EINTR);
			break;
		}

		if (UNLIKELY(!stress_continue_flag()))
			break;
		if (UNLIKELY(shim_usleep(1000) < 0)) {
			eintr |= (errno == EINTR);
			break;
		}

		if (UNLIKELY(!stress_continue_flag()))
			break;
		if (UNLIKELY(shim_usleep(10000) < 0)) {
			eintr |= (errno == EINTR);
			break;
		}

		stress_sleep_time_now(&t2);
		delta = stress_time_delta(&t1, &t2);
		expected = (1.0 + 10.0 + 100.0 + 1000.0 + 10000.0);
		if (!eintr && UNLIKELY(delta < expected / STRESS_DBL_MICROSECOND)) {
			pr_fail("%s: nanosleeps for %.f microsecs took less than %.2f microsecs to complete\n",
				args->name, expected, delta * STRESS_DBL_MICROSECOND);
			ctxt->underruns++;
		}

#if defined(HAVE_PSELECT)
		stress_sleep_time_now(&t1);
		if (UNLIKELY(!stress_continue_flag()))
			break;
		tv.tv_sec = 0;
		tv.tv_nsec = 1;
		if (UNLIKELY(pselect(0, NULL, NULL, NULL, &tv, NULL) < 0))
			goto skip_pselect;

		if (UNLIKELY(!stress_continue_flag()))
			break;
		tv.tv_sec = 0;
		tv.tv_nsec = 10;

		if (UNLIKELY(pselect(0, NULL, NULL, NULL, &tv, NULL) < 0))
			goto skip_pselect;

		if (UNLIKELY(!stress_continue_flag()))
			break;
		tv.tv_sec = 0;
		tv.tv_nsec = 100;
		if (UNLIKELY(pselect(0, NULL, NULL, NULL, &tv, NULL) < 0))
			goto skip_pselect;

		if (UNLIKELY(!stress_continue_flag()))
			break;
		tv.tv_sec = 0;
		tv.tv_nsec = 1000;
		if (UNLIKELY(pselect(0, NULL, NULL, NULL, &tv, NULL) < 0))
			goto skip_pselect;

		if (UNLIKELY(!stress_continue_flag()))
			break;
		tv.tv_sec = 0;
		tv.tv_nsec = 10000;
		if (UNLIKELY(pselect(0, NULL, NULL, NULL, &tv, NULL) < 0))
			goto skip_pselect;

		stress_sleep_time_now(&t2);
		delta = stress_time_delta(&t1, &t2);
		expected = (1.0 + 10.0 + 100.0 + 1000.0 + 10000.0);
		if (UNLIKELY(delta < expected / STRESS_DBL_NANOSECOND)) {
			pr_fail("%s: pselects for %.f nanosecs took less than %.2f nanosecs to complete\n",
				args->name, expected, delta * STRESS_DBL_NANOSECOND);
			ctxt->underruns++;
		}

skip_pselect:
#endif

#if defined(HAVE_SYS_SELECT_H) &&	\
    defined(HAVE_SELECT)
		eintr = false;
		stress_sleep_time_now(&t1);
		if (UNLIKELY(!stress_continue_flag()))
			break;
		timeout.tv_sec = 0;
		timeout.tv_usec = 10;
		if (UNLIKELY(select(0, NULL, NULL, NULL, &timeout) < 0)) {
			eintr |= (errno == EINTR);
			break;
		}

		if (UNLIKELY(!stress_continue_flag()))
			break;
		timeout.tv_sec = 0;
		timeout.tv_usec = 100;
		if (UNLIKELY(select(0, NULL, NULL, NULL, &timeout) < 0)) {
			eintr |= (errno == EINTR);
			break;
		}

		if (UNLIKELY(!stress_continue_flag()))
			break;
		timeout.tv_sec = 0;
		timeout.tv_usec = 1000;
		if (UNLIKELY(select(0, NULL, NULL, NULL, &timeout) < 0)) {
			eintr |= (errno == EINTR);
			break;
		}

		if (UNLIKELY(!stress_continue_flag()))
			break;
		timeout.tv_sec = 0;
		timeout.tv_usec = 10000;
		if (UNLIKELY(select(0, NULL, NULL, NULL, &timeout) < 0)) {
			eintr |= (errno == EINTR);
			break;
		}

		stress_sleep_time_now(&t2);
		delta = stress_time_delta(&t1, &t2);
		expected = (1.0 + 10.0 + 100.0 + 1000.0 + 10000.0);
		if (!eintr && UNLIKELY(delta < expected / STRESS_DBL_MICROSECOND)) {
			pr_fail("%s: selects for %.f microsecs took less than %.2f microsecs to complete\n",
				args->name, expected, delta * STRESS_DBL_MICROSECOND);
			ctxt->underruns++;
		}

#endif
#if defined(HAVE_ASM_X86_TPAUSE) &&	\
    !defined(HAVE_COMPILER_PCC)
		if (UNLIKELY(!stress_continue_flag()))
			break;
		if (x86_has_waitpkg) {
			int i;

			for (i = 1; LIKELY(stress_continue_flag() && (i < 1024)); i <<= 1)
				stress_asm_x86_tpause(0, i);
		}
#endif
		stress_bogo_inc_lock(args, stress_sleep_counter_lock, true);
	}
	return &g_nowt;
}

/*
 *  stress_sleep()
 *	stress by many sleeping threads
 */
static int stress_sleep(stress_args_t *args)
{
	uint64_t i, n, limited = 0;
	uint64_t sleep_max = DEFAULT_SLEEP;
	uint64_t underruns = 0;
	static stress_ctxt_t ctxts[MAX_SLEEP];
	int ret = EXIT_SUCCESS;

	if (!stress_get_setting("sleep-max", &sleep_max)) {
		if (g_opt_flags & OPT_FLAGS_MAXIMIZE)
			sleep_max = MAX_SLEEP;
		if (g_opt_flags & OPT_FLAGS_MINIMIZE)
			sleep_max = MIN_SLEEP;
	}

	stress_sleep_counter_lock = stress_lock_create("counter");
	if (!stress_sleep_counter_lock) {
		pr_inf("%s: cannot create counter lock, skipping stressor\n", args->name);
		return EXIT_NO_RESOURCE;
	}

	if (stress_sighandler(args->name, SIGALRM, stress_sigalrm_handler, NULL) < 0)
		return EXIT_FAILURE;

	(void)shim_memset(ctxts, 0, sizeof(ctxts));
	(void)sigfillset(&set);

	stress_set_proc_state(args->name, STRESS_STATE_SYNC_WAIT);
	stress_sync_start_wait(args);
	stress_set_proc_state(args->name, STRESS_STATE_RUN);

	for (n = 0; n < sleep_max; n++) {
		ctxts[n].args = args;
		ctxts[n].sleep_max = sleep_max;
		ctxts[n].underruns = 0;
		ret = pthread_create(&ctxts[n].pthread, NULL,
			stress_pthread_func, &ctxts[n]);
		if (ret) {
			/* Out of resources, don't try any more */
			if (ret == EAGAIN) {
				limited++;
				break;
			}
			/* Something really unexpected */
			pr_fail("%s: pthread create failed, errno=%d (%s)\n",
				args->name, ret, strerror(ret));
			ret = EXIT_NO_RESOURCE;
			goto tidy;
		}
		/* Timed out? abort! */
		if (UNLIKELY(!stress_continue_flag()))
			goto tidy;
	}

	do {
		(void)shim_usleep_interruptible(10000);
	}  while (!thread_terminate && stress_continue(args));

	ret = EXIT_SUCCESS;
tidy:
	(void)alarm(0);
	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);

	thread_terminate = true;
	for (i = 0; i < n; i++) {
		VOID_RET(int, pthread_join(ctxts[i].pthread, NULL));
		underruns += ctxts[i].underruns;
	}

	if (underruns) {
		pr_fail("%s: detected %" PRIu64 " sleep underruns\n",
			args->name, underruns);
		ret = EXIT_FAILURE;
	}

	if (limited) {
		pr_inf("%s: %.2f%% of iterations could not reach "
			"requested %" PRIu64 " threads (instance %"
			PRIu32 ")\n",
			args->name,
			100.0 * (double)limited / (double)sleep_max,
			sleep_max, args->instance);
	}

	stress_lock_destroy(stress_sleep_counter_lock);

	return ret;
}

const stressor_info_t stress_sleep_info = {
	.stressor = stress_sleep,
	.classifier = CLASS_INTERRUPT | CLASS_SCHEDULER | CLASS_OS,
	.opts = opts,
	.verify = VERIFY_ALWAYS,
	.help = help
};
#else
const stressor_info_t stress_sleep_info = {
	.stressor = stress_unimplemented,
	.classifier = CLASS_INTERRUPT | CLASS_SCHEDULER | CLASS_OS,
	.opts = opts,
	.verify = VERIFY_ALWAYS,
	.help = help,
	.unimplemented_reason = "built without pthread support"
};
#endif
