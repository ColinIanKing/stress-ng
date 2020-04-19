/*
 * Copyright (C) 2013-2020 Canonical, Ltd.
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
 * This code is a complete clean re-write of the stress tool by
 * Colin Ian King <colin.king@canonical.com> and attempts to be
 * backwardly compatible with the stress tool by Amos Waterland
 * <apw@rossby.metr.ou.edu> but has more stress tests and more
 * functionality.
 *
 */
#include "stress-ng.h"

static const stress_help_t help[] = {
	{ NULL,	"sigabrt N",	 "start N workers generating segmentation faults" },
	{ NULL,	"sigabrt-ops N", "stop after N bogo segmentation faults" },
	{ NULL,	NULL,		 NULL }
};

static void MLOCKED_TEXT stress_sigabrt_handler(int num)
{
	(void)num;
}

/*
 *  stress_sigabrt
 *	stress by generating segmentation faults by
 *	writing to a read only page
 */
static int stress_sigabrt(const stress_args_t *args)
{
	if (stress_sighandler(args->name, SIGABRT, stress_sigabrt_handler, NULL) < 0)
		return EXIT_NO_RESOURCE;

	do {
		pid_t pid;

		(void)stress_mwc32();

		pid = fork();
		if (pid < 0) {
			if ((errno == EAGAIN) || (errno == ENOMEM))
				continue;
			pr_fail("%s: fork failed: %d (%s)\n",
				args->name, errno, strerror(errno));
			return EXIT_FAILURE;
		} else if (pid == 0) {
			int ret;

			ret = stress_sighandler(args->name, SIGABRT, SIG_DFL, NULL);
			(void)ret;

			/* Randomly select death by abort or SIGABRT */
			if (stress_mwc1()) {
				abort();
			} else {
				raise(SIGABRT);
			}
			/* Should never get here */
			_exit(EXIT_FAILURE);
		} else {
			int ret, status;

rewait:
			ret = shim_waitpid(pid, &status, 0);
			if (ret < 0) {
				if (errno == EINTR) {
					goto rewait;
				}
				pr_fail("%s: waitpid failed: %d (%s)\n",
					args->name, errno, strerror(errno));
				continue;
			} else {
				if (WIFSIGNALED(status) &&
				    (WTERMSIG(status) == SIGABRT)) {
					inc_counter(args);
				} else if (WIFEXITED(status)) {
					pr_fail("%s: child did not abort as expected\n",
						args->name);
				}
			}
		}
	} while (keep_stressing());

	return EXIT_SUCCESS;
}

stressor_info_t stress_sigabrt_info = {
	.stressor = stress_sigabrt,
	.class = CLASS_INTERRUPT | CLASS_OS,
	.help = help
};
