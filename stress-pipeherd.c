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

/*
 *  Herd of pipe processes, simulates how GNU make passes tokens
 *  when building with -j option, but without the timely building.
 *
 *  Inspired by Linux commit:
 *     0ddad21d3e99c743a3aa473121dc5561679e26bb
 *     ("pipe: use exclusive waits when reading or writing")
 *
 */
#define PIPE_HERD_MAX	(100)

static const stress_help_t help[] = {
	{ "p N", "pipeherd N",		"start N multi-process workers exercising pipes I/O" },
	{ NULL,	"pipeherd-ops N",	"stop after N pipeherd I/O bogo operations" },
	{ NULL,	"pipeherd-yield",	"force processes to yield after each write" },
	{ NULL,	NULL,			NULL }
};

static int stress_set_pipeherd_yield(const char *opt)
{
	bool pipeherd_yield  = true;
	(void)opt;

        return stress_set_setting("pipeherd-yield", TYPE_ID_BOOL, &pipeherd_yield);
}

static int stress_pipeherd_read_write(const stress_args_t *args, const int fd[2], const bool pipeherd_yield)
{
	while (keep_stressing()) {
		int64_t counter;
		ssize_t sz;

		sz = read(fd[0], &counter, sizeof(counter));
		if (sz < 0) {
			if (errno == EINTR || errno == EPIPE)
				break;
			return EXIT_FAILURE;
		}
		counter++;
		sz = write(fd[1], &counter, sizeof(counter));
		if (sz < 0) {
			if (errno == EINTR || errno == EPIPE)
				break;
			return EXIT_FAILURE;
		}
		if (pipeherd_yield)
			(void)shim_sched_yield();
	}
	return EXIT_SUCCESS;
}

/*
 *  stress_pipeherd
 *	stress by heavy pipe I/O
 */
static int stress_pipeherd(const stress_args_t *args)
{
	int fd[2];
	int64_t counter;
	pid_t pids[PIPE_HERD_MAX];
	int i, rc;
	ssize_t sz;
	bool pipeherd_yield = false;
#if defined(__linux__)
	struct rusage usage;
	double t1, t2;
#endif

	(void)stress_get_setting("pipeherd-yield", &pipeherd_yield);

	if (pipe(fd) < 0) {
		pr_fail("%s: pipe failed: %d (%s)\n",
			args->name, errno, strerror(errno));
		return EXIT_FAILURE;
	}

	counter = 0;
	sz = write(fd[1], &counter, sizeof(counter));
	if (sz < 0) {
		pr_fail("%s: write to pipe failed: %d (%s)\n",
			args->name, errno, strerror(errno));
		(void)close(fd[0]);
		(void)close(fd[1]);
		return EXIT_FAILURE;
	}

#if defined(__linux__)
	t1 = stress_time_now();
#endif
	for (i = 0; i < PIPE_HERD_MAX; i++) {
		pid_t pid;
		pid = fork();

		if (pid == 0) {
			(void)setpgid(0, g_pgrp);
			stress_parent_died_alarm();
			rc = stress_pipeherd_read_write(args, fd, pipeherd_yield);
			(void)close(fd[0]);
			(void)close(fd[1]);
			_exit(rc);
		} else if (pid < 0) {
			pids[i] = -1;
		} else {
			pids[i] = pid;
		}
	}

	rc = stress_pipeherd_read_write(args, fd, pipeherd_yield);
	(void)rc;
	sz = read(fd[0], &counter, sizeof(counter));
	if (sz > 0)
		set_counter(args, counter);

#if defined(__linux__)
	t2 = stress_time_now();
#endif

	for (i = 0; i < PIPE_HERD_MAX; i++) {
		if (pids[i] >= 0) {
			int status;

			(void)kill(pids[i], SIGKILL);
			(void)shim_waitpid(pids[i], &status, 0);
		}
	}

	(void)close(fd[0]);
	(void)close(fd[1]);

#if defined(__linux__)
	(void)memset(&usage, 0, sizeof(usage));
	if (getrusage(RUSAGE_CHILDREN, &usage) == 0) {
		long total = usage.ru_nvcsw + usage.ru_nivcsw;

		(void)memset(&usage, 0, sizeof(usage));
		if (getrusage(RUSAGE_SELF, &usage) == 0) {
			total += usage.ru_nvcsw + usage.ru_nivcsw;
			if (total) {
				pr_inf("%s: %.2f context switches per bogo operation (%.2f per second)\n",
					args->name,
					(float)total / (float)get_counter(args),
					(float)total / (t2 - t1));
			}
		}
	}
#endif

	return EXIT_SUCCESS;
}

static const stress_opt_set_func_t opt_set_funcs[] = {
        { OPT_pipeherd_yield,	stress_set_pipeherd_yield },
        { 0,                    NULL }
};

stressor_info_t stress_pipeherd_info = {
	.stressor = stress_pipeherd,
	.class = CLASS_PIPE_IO | CLASS_MEMORY | CLASS_OS,
	.opt_set_funcs = opt_set_funcs,
	.help = help
};
