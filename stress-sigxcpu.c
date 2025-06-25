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
	{ NULL,	"sigxcpu N",		"start N workers that exercise SIGXCPU signals" },
	{ NULL,	"sigxcpu-ops N",	"stop after N bogo SIGXCPU signals" },
	{ NULL,	NULL,			NULL }
};

#if defined(SIGXCPU) &&	\
    defined(RLIMIT_FSIZE)

stress_args_t *sigxcpu_args;

/*
 *  stress_sigxcpu_handler()
 *      SIGXCPU handler
 */
static void MLOCKED_TEXT stress_sigxcpu_handler(int signum)
{
	if (sigxcpu_args && (signum == SIGXCPU))
		stress_bogo_inc(sigxcpu_args);
}

/*
 *  stress_sigxcpu
 *	stress reading of /dev/zero using SIGXCPU
 */
static int stress_sigxcpu(stress_args_t *args)
{
	int rc = EXIT_SUCCESS;
	struct rlimit limit;

	sigxcpu_args = args;

	if (stress_sighandler(args->name, SIGXCPU, stress_sigxcpu_handler, NULL) < 0)
		return EXIT_FAILURE;

	if (getrlimit(RLIMIT_FSIZE, &limit) < 0) {
		pr_inf("%s: getrimit failed, errno=%d (%s), skipping stressor\n",
			args->name, errno, strerror(errno));
		return EXIT_NO_RESOURCE;
	}

	stress_set_proc_state(args->name, STRESS_STATE_SYNC_WAIT);
	stress_sync_start_wait(args);
	stress_set_proc_state(args->name, STRESS_STATE_RUN);

	do {
		limit.rlim_cur = 0;
		if (setrlimit(RLIMIT_CPU, &limit) < 0) {
			pr_inf("%s: setrlimit failed, errno=%d (%s)\n",
				args->name, errno, strerror(errno));
			rc = EXIT_FAILURE;
			break;
		}
		(void)shim_sched_yield();
	} while (stress_continue(args));

	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);

	VOID_RET(int, stress_sighandler(args->name, SIGXCPU, SIG_IGN, NULL));

	return rc;
}

const stressor_info_t stress_sigxcpu_info = {
	.stressor = stress_sigxcpu,
	.classifier = CLASS_SIGNAL | CLASS_OS,
	.verify = VERIFY_ALWAYS,
	.help = help
};
#else
const stressor_info_t stress_sigxcpu_info = {
	.stressor = stress_unimplemented,
	.classifier = CLASS_SIGNAL | CLASS_OS,
	.verify = VERIFY_ALWAYS,
	.help = help,
	.unimplemented_reason = "built without SIGXCPU or RLIMIT_FSIZE"
};
#endif
