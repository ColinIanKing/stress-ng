/*
 * Copyright (C) 2013-2021 Canonical, Ltd.
 * Copyright (C) 2022-2024 Colin Ian King.
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

#include <sched.h>

uint64_t sigpipe_max_count;
volatile uint64_t sigpipe_count;

static const stress_help_t help[] = {
	{ NULL,	"sigpipe N",	 "start N workers exercising SIGPIPE" },
	{ NULL,	"sigpipe-ops N", "stop after N SIGPIPE bogo operations" },
	{ NULL,	NULL,		 NULL }
};

static void stress_sigpipe_handler(int signum)
{
	if (LIKELY(signum == SIGPIPE))
		sigpipe_count++;
}

static void stress_sigpipe_handler_count_check(int signum)
{
	if (LIKELY(signum == SIGPIPE))
		sigpipe_count++;
	if (sigpipe_count >= sigpipe_max_count)
		stress_continue_set_flag(false);
}

/*
 *  stress_sigpipe
 *	stress by generating SIGPIPE signals on pipe I/O
 */
static int stress_sigpipe(stress_args_t *args)
{
	char data = 0;
	uint64_t epipe_count = 0;
	int rc = EXIT_SUCCESS;
	int pipefds[2];
	sigpipe_count = 0;
	sigpipe_max_count = args->max_ops;

	if (stress_sighandler(args->name, SIGPIPE,
		(args->max_ops == 0) ? stress_sigpipe_handler :
				       stress_sigpipe_handler_count_check, NULL) < 0)
		return EXIT_FAILURE;

	if (UNLIKELY(pipe(pipefds) < 0)) {
		pr_inf_skip("%s: pipe failed, errno=%d (%s), skipping stressor\n",
			args->name, errno, strerror(errno));
		return EXIT_NO_RESOURCE;
	}
	(void)close(pipefds[0]);

	stress_set_proc_state(args->name, STRESS_STATE_SYNC_WAIT);
	stress_sync_start_wait(args);
	stress_set_proc_state(args->name, STRESS_STATE_RUN);

	do {
		register int ret;

		/* cause SIGPIPE if pipe closed */
		ret = write(pipefds[1], &data, sizeof(data));
		if (LIKELY(ret <= 0)) {
			if (errno == EPIPE)
				epipe_count++;
		}
	} while (stress_continue_flag());

	stress_bogo_set(args, sigpipe_count);
	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);

	/* simple sanity check */
	if ((epipe_count > 0) && (sigpipe_count < 1)) {
		pr_fail("%s: %" PRIu64 " writes occurred but got 0 SIGPIPE signals\n",
			args->name, epipe_count);
		rc = EXIT_FAILURE;
	}

	(void)close(pipefds[1]);

	return rc;
}

const stressor_info_t stress_sigpipe_info = {
	.stressor = stress_sigpipe,
	.class = CLASS_SIGNAL | CLASS_OS,
	.verify = VERIFY_ALWAYS,
	.help = help
};
