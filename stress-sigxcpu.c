/*
 * Copyright (C) 2024-2026 Colin Ian King.
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

static const stress_help_t help[] = {
	{ NULL,	"sigxcpu N",		"start N workers that exercise SIGXCPU signals" },
	{ NULL,	"sigxcpu-ops N",	"stop after N bogo SIGXCPU signals" },
	{ NULL,	NULL,			NULL }
};

#if defined(SIGXCPU) &&		\
    (defined(RLIMIT_CPU) ||	\
     defined(RLIMIT_RTTIME))

static stress_args_t *sigxcpu_args;

/*
 *  stress_sigxcpu_handler()
 *      SIGXCPU handler
 */
static void MLOCKED_TEXT stress_sigxcpu_handler(int signum)
{
	if (sigxcpu_args && (signum == SIGXCPU))
		stress_bogo_inc(sigxcpu_args);
}

static double stress_sigxcpu_cpu_usage(void)
{
#if defined(RUSAGE_SELF)
	struct rusage usage;

	if (shim_getrusage(RUSAGE_SELF, &usage) == 0) {
		return (double)usage.ru_utime.tv_sec +
		       (((double)usage.ru_utime.tv_usec) / STRESS_DBL_MICROSECOND) +
		       (double)usage.ru_stime.tv_sec +
	               (((double)usage.ru_stime.tv_usec) / STRESS_DBL_MICROSECOND);
	}
#endif
	return 0.0;
}

/*
 *  stress_sigxcpu
 *	stress SIGXCPU signal generation
 */
static int stress_sigxcpu(stress_args_t *args)
{
	int rc = EXIT_SUCCESS;
#if defined(RLIMIT_CPU)
	struct rlimit limit_cpu;
#endif
#if defined(RLIMIT_RTTIME)
	struct rlimit limit_rttime;
#endif
#if defined(RUSAGE_SELF)
	struct rusage usage;
	const bool verify = !!(g_opt_flags & OPT_FLAGS_VERIFY);
#endif
	double cpu_used;

	sigxcpu_args = args;

	if (stress_signal_handler(args->name, SIGXCPU, stress_sigxcpu_handler, NULL) < 0)
		return EXIT_FAILURE;

#if defined(RLIMIT_CPU)
	if (getrlimit(RLIMIT_CPU, &limit_cpu) < 0) {
		pr_inf("%s: getrlimit RLIMIT_CPU failed, errno=%d (%s), skipping stressor\n",
			args->name, errno, strerror(errno));
		return EXIT_NO_RESOURCE;
	}
#endif
#if defined(RLIMIT_RTTIME)
	if (getrlimit(RLIMIT_CPU, &limit_rttime) < 0) {
		pr_inf("%s: getrlimit RLIMIT_RTTIME failed, errno=%d (%s), skipping stressor\n",
			args->name, errno, strerror(errno));
		return EXIT_NO_RESOURCE;
	}
#endif

	stress_proc_state_set(args->name, STRESS_STATE_SYNC_WAIT);
	stress_sync_start_wait(args);
	stress_proc_state_set(args->name, STRESS_STATE_RUN);

	cpu_used = stress_sigxcpu_cpu_usage();

	do {
#if defined(RLIMIT_CPU)
		limit_cpu.rlim_cur = 0;
		if (setrlimit(RLIMIT_CPU, &limit_cpu) < 0) {
			pr_inf("%s: setrlimit RLIMIT_CPU failed, errno=%d (%s)\n",
				args->name, errno, strerror(errno));
			rc = EXIT_FAILURE;
			break;
		}
		(void)shim_sched_yield();
#endif
#if defined(RLIMIT_RTTIME)
		limit_rttime.rlim_cur = 0;
		if (setrlimit(RLIMIT_RTTIME, &limit_rttime) < 0) {
			pr_inf("%s: setrlimit RLIMIT_RTTIME failed, errno=%d (%s)\n",
				args->name, errno, strerror(errno));
			rc = EXIT_FAILURE;
			break;
		}
#endif
		(void)shim_sched_yield();
	} while (stress_continue(args));

	stress_proc_state_set(args->name, STRESS_STATE_DEINIT);

	VOID_RET(int, stress_signal_handler(args->name, SIGXCPU, SIG_IGN, NULL));

#if defined(RUSAGE_SELF)
	if (verify && (shim_getrusage(RUSAGE_SELF, &usage) == 0)) {
		const double runtime = stress_sigxcpu_cpu_usage() - cpu_used;

		/* Allow stressor to run for ~10 second before checking */
		if ((runtime > 10.0) && (stress_bogo_get(args) == 0)) {
			pr_fail("%s: no SIGXCPU signals occurred in %.2f seconds of runtime\n",
				args->name, runtime);
			rc = EXIT_FAILURE;
		}
	}
#endif

	return rc;
}

const stressor_info_t stress_sigxcpu_info = {
	.stressor = stress_sigxcpu,
	.classifier = CLASS_SIGNAL | CLASS_OS,
	.verify = VERIFY_OPTIONAL,
	.help = help
};
#else
const stressor_info_t stress_sigxcpu_info = {
	.stressor = stress_unimplemented,
	.classifier = CLASS_SIGNAL | CLASS_OS,
	.verify = VERIFY_OPTIONAL,
	.help = help,
	.unimplemented_reason = "built without SIGXCPU or RLIMIT_FSIZE"
};
#endif
