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

static const stress_help_t help[] = {
	{ NULL,	"sigvtalrm N",		"start N workers exercising SIGVTALRM signals" },
	{ NULL,	"sigvtalrm-ops N",	"stop after N SIGVTALRM signals" },
	{ NULL, NULL,		NULL }
};

#if defined(HAVE_GETITIMER) &&	\
    defined(HAVE_SETITIMER) &&	\
    defined(ITIMER_VIRTUAL) &&	\
    defined(SIGVTALRM)

static stress_args_t *s_args;

/*
 *  stress_sigvtalrm_set()
 *	set timer, ensure it is never zero
 */
static void stress_sigvtalrm_set(struct itimerval *timer)
{
	timer->it_value.tv_sec = 0;
	timer->it_value.tv_usec = 1;
	timer->it_interval.tv_sec = 0;
	timer->it_interval.tv_usec = 1;
}

/*
 *  stress_sigvtalrm_handler()
 *	catch itimer signal and cancel if no more runs flagged
 */
static void MLOCKED_TEXT OPTIMIZE3 stress_sigvtalrm_handler(int sig)
{
	(void)sig;

	stress_bogo_inc(s_args);
	if (UNLIKELY(!stress_continue(s_args))) {
		struct itimerval timer;

		/* Cancel timer if we detect no more runs */
		(void)shim_memset(&timer, 0, sizeof(timer));
		(void)setitimer(ITIMER_VIRTUAL, &timer, NULL);
	}
}

/*
 *  stress_sigvtalrm
 *	stress itimer
 */
static int stress_sigvtalrm(stress_args_t *args)
{
	struct itimerval timer;

	s_args = args;

	if (stress_sighandler(args->name, SIGVTALRM, stress_sigvtalrm_handler, NULL) < 0)
		return EXIT_FAILURE;

	stress_set_proc_state(args->name, STRESS_STATE_SYNC_WAIT);
	stress_sync_start_wait(args);
	stress_set_proc_state(args->name, STRESS_STATE_RUN);

	stress_sigvtalrm_set(&timer);
	if (setitimer(ITIMER_VIRTUAL, &timer, NULL) < 0) {
		if (errno == EINVAL) {
			if (stress_instance_zero(args))
				pr_inf_skip("%s: skipping stressor, setitimer with "
					"ITIMER_VIRTUAL is not implemented\n",
					args->name);
			return EXIT_NOT_IMPLEMENTED;
		}
		pr_fail("%s: setitimer failed, errno=%d (%s)\n",
			args->name, errno, strerror(errno));
		return EXIT_FAILURE;
	}

	/*
	 *  Consume CPU cycles, the more we consume the more
	 *  SIGVTALRM timer signals we generate
	 */
	do {
		struct itimerval t;

		(void)getitimer(ITIMER_VIRTUAL, &t);
	} while (stress_continue(args));

#if defined(HAVE_GETRUSAGE) &&	\
    defined(RUSAGE_SELF)
	{
		struct rusage usage;

		if (shim_getrusage(RUSAGE_SELF, &usage) == 0) {
			const double duration = (double)usage.ru_utime.tv_sec +
						((double)usage.ru_utime.tv_usec) / STRESS_DBL_MICROSECOND;

			if ((duration > 1.0) && (stress_bogo_get(args) == 0))
				pr_fail("%s: did not handle any itimer SIGVTALRM signals\n", args->name);
		}
	}
#endif
	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);
	(void)shim_memset(&timer, 0, sizeof(timer));
	(void)setitimer(ITIMER_VIRTUAL, &timer, NULL);
	return EXIT_SUCCESS;
}

const stressor_info_t stress_sigvtalrm_info = {
	.stressor = stress_sigvtalrm,
	.classifier = CLASS_SIGNAL | CLASS_OS,
	.verify = VERIFY_ALWAYS,
	.help = help
};

#else

const stressor_info_t stress_sigvtalrm_info = {
	.stressor = stress_unimplemented,
	.classifier = CLASS_SIGNAL | CLASS_OS,
	.verify = VERIFY_ALWAYS,
	.help = help,
	.unimplemented_reason = "built without SIGVTALRM, getitimer() or setitimer() support"
};
#endif
