/*
 * Copyright (C) 2024      Colin Ian King.
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
	{ NULL,	"itimer N",	"start N workers exercising interval timers" },
	{ NULL,	"itimer-ops N",	"stop after N interval timer bogo operations" },
	{ NULL, NULL,		NULL }
};

#if defined(HAVE_GETITIMER) &&	\
    defined(HAVE_SETITIMER) &&	\
    defined(ITIMER_VIRTUAL) &&	\
    defined(SIGVTALRM)

static volatile uint64_t itimer_counter = 0;
static uint64_t max_ops;

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
 *  stress_sigvtalrm_stress_continue(args)
 *      returns true if we can keep on running a stressor
 */
static inline ALWAYS_INLINE bool OPTIMIZE3 stress_sigvtalrm_stress_continue(void)
{
	return (LIKELY(stress_continue_flag()) &&
		LIKELY(!max_ops || (itimer_counter < max_ops)));
}

/*
 *  stress_sigvtalrm_handler()
 *	catch itimer signal and cancel if no more runs flagged
 */
static void stress_sigvtalrm_handler(int sig)
{
	(void)sig;

	itimer_counter++;

	if (!stress_sigvtalrm_stress_continue()) {
		struct itimerval timer;

		stress_continue_set_flag(false);
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
	double t_start, duration;

	max_ops = args->max_ops;

	if (stress_sighandler(args->name, SIGVTALRM, stress_sigvtalrm_handler, NULL) < 0)
		return EXIT_FAILURE;

	stress_set_proc_state(args->name, STRESS_STATE_SYNC_WAIT);
	stress_sync_start_wait(args);
	stress_set_proc_state(args->name, STRESS_STATE_RUN);

	stress_sigvtalrm_set(&timer);
	if (setitimer(ITIMER_VIRTUAL, &timer, NULL) < 0) {
		if (errno == EINVAL) {
			if (args->instance == 0)
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
	t_start = stress_time_now();
	do {
		struct itimerval t;

		(void)getitimer(ITIMER_VIRTUAL, &t);
	} while (stress_sigvtalrm_stress_continue());
	duration = stress_time_now() - t_start;

	stress_bogo_set(args, itimer_counter);
	if ((duration > 1.0) && (itimer_counter == 0))
		pr_fail("%s: did not handle any itimer SIGVTALRM signals\n", args->name);

	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);
	(void)shim_memset(&timer, 0, sizeof(timer));
	(void)setitimer(ITIMER_VIRTUAL, &timer, NULL);
	return EXIT_SUCCESS;
}

stressor_info_t stress_sigvtalrm_info = {
	.stressor = stress_sigvtalrm,
	.class = CLASS_INTERRUPT | CLASS_OS,
	.verify = VERIFY_ALWAYS,
	.help = help
};

#else

stressor_info_t stress_sigvtalrm_info = {
	.stressor = stress_unimplemented,
	.class = CLASS_INTERRUPT | CLASS_OS,
	.verify = VERIFY_ALWAYS,
	.help = help,
	.unimplemented_reason = "built without SIGVTALRM, getitimer() or setitimer() support"
};
#endif
