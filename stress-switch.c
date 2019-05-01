/*
 * Copyright (C) 2013-2019 Canonical, Ltd.
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

static const help_t help[] = {
	{ "s N","switch N",	 "start N workers doing rapid context switches" },
	{ NULL,	"switch-ops N",	 "stop after N context switch bogo operations" },
	{ NULL, "switch-freq N", "set frequency of context switches" },
	{ NULL, NULL, 		 NULL }
};

#define SWITCH_STOP	'X'
#define THRESH_FREQ	(100)		/* Delay adjustment rate in HZ */
#define NANO_SECS	(1000000000)

/*
 *  stress_set_switch_freq()
 *	set context switch freq in Hz from given option
 */
static int stress_set_switch_freq(const char *opt)
{
	uint64_t switch_freq;

	switch_freq = get_uint64(opt);
	check_range("switch-freq", switch_freq, 0, NANO_SECS);
	return set_setting("switch-freq", TYPE_ID_UINT64, &switch_freq);
}

/*
 *  stress_switch
 *	stress by heavy context switching
 */
static int stress_switch(const args_t *args)
{
	pid_t pid;
	int pipefds[2];
	size_t buf_size;
	uint64_t switch_freq = 0;

	(void)get_setting("switch-freq", &switch_freq);

#if defined(HAVE_PIPE2) &&	\
    defined(O_DIRECT)
	if (pipe2(pipefds, O_DIRECT) < 0) {
		/*
		 *  Fallback to pipe if pipe2 fails
		 */
		if (pipe(pipefds) < 0) {
			pr_fail_dbg("pipe");
			return EXIT_FAILURE;
		}
	}
	buf_size = 1;
#else
	if (pipe(pipefds) < 0) {
		pr_fail_dbg("pipe");
		return EXIT_FAILURE;
	}
	buf_size = args->page_size;
#endif

#if defined(F_SETPIPE_SZ)
	if (fcntl(pipefds[0], F_SETPIPE_SZ, buf_size) < 0) {
		pr_dbg("%s: could not force pipe size to 1 page, "
			"errno = %d (%s)\n",
			args->name, errno, strerror(errno));
	}
	if (fcntl(pipefds[1], F_SETPIPE_SZ, buf_size) < 0) {
		pr_dbg("%s: could not force pipe size to 1 page, "
			"errno = %d (%s)\n",
			args->name, errno, strerror(errno));
	}
#endif

again:
	pid = fork();
	if (pid < 0) {
		if (g_keep_stressing_flag && (errno == EAGAIN))
			goto again;
		(void)close(pipefds[0]);
		(void)close(pipefds[1]);
		pr_fail_dbg("fork");
		return EXIT_FAILURE;
	} else if (pid == 0) {
		char buf[buf_size];

		(void)setpgid(0, g_pgrp);
		stress_parent_died_alarm();

		(void)close(pipefds[1]);

		while (g_keep_stressing_flag) {
			ssize_t ret;

			ret = read(pipefds[0], buf, sizeof(buf));
			if (ret < 0) {
				if ((errno == EAGAIN) || (errno == EINTR))
					continue;
				pr_fail_dbg("read");
				break;
			}
			if (ret == 0)
				break;
			if (*buf == SWITCH_STOP)
				break;
		}
		(void)close(pipefds[0]);
		_exit(EXIT_SUCCESS);
	} else {
		char buf[buf_size];
		int status;
		double t1, t2, t;
		uint64_t delay, switch_delay = (switch_freq == 0) ? 0 : NANO_SECS / switch_freq;
		uint64_t i = 0, threshold = switch_freq / THRESH_FREQ;

		/* Parent */
		(void)setpgid(pid, g_pgrp);
		(void)close(pipefds[0]);
		(void)memset(buf, '_', buf_size);

		delay = switch_delay;

		t1 = time_now();
		do {
			ssize_t ret;

			inc_counter(args);

			ret = write(pipefds[1], buf, sizeof(buf));
			if (ret <= 0) {
				if ((errno == EAGAIN) || (errno == EINTR))
					continue;
				if (errno) {
					pr_fail_dbg("write");
					break;
				}
				continue;
			}

			if (switch_freq) {
				/*
				 *  Small delays take a while, so skip these
				 */
				if (delay > 1000)
					shim_nanosleep_uint64(delay);

				/*
				 *  This is expensive, so only update the
				 *  delay infrequently (at THRESH_FREQ HZ)
				 */
				if (++i >= threshold) {
					double overrun, overrun_by;

					i = 0;
					t = t1 + ((((double)get_counter(args)) * switch_delay) / NANO_SECS);
					overrun = (time_now() - t) * (double)NANO_SECS;
					overrun_by = (double)switch_delay - overrun;

					if (overrun_by < 0.0) {
						/* Massive overrun, skip a delay */
						delay = 0;
					} else {
						/* Overrun or underrun? */
						delay = (double)overrun_by;
						if (delay > switch_delay) {
							/* Don't delay more than the switch delay */
							delay = switch_delay;
						}
					}
				}
			}
		} while (keep_stressing());

		t2 = time_now();
		pr_inf("%s: %.2f nanoseconds per context switch (based on parent run time)\n",
			args->name,
			((t2 - t1) * NANO_SECS) / (double)get_counter(args));

		(void)memset(buf, SWITCH_STOP, sizeof(buf));
		if (write(pipefds[1], buf, sizeof(buf)) <= 0)
			pr_fail_dbg("termination write");
		(void)kill(pid, SIGKILL);
		(void)shim_waitpid(pid, &status, 0);
	}

	return EXIT_SUCCESS;
}

static const opt_set_func_t opt_set_funcs[] = {
	{ OPT_switch_freq,	stress_set_switch_freq },
	{ 0,			NULL }
};

stressor_info_t stress_switch_info = {
	.stressor = stress_switch,
	.class = CLASS_SCHEDULER | CLASS_OS,
	.opt_set_funcs = opt_set_funcs,
	.help = help
};
