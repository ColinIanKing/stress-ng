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
#include "core-killpid.h"

#include <sched.h>

#define STRESS_FD_MAX	(2 * 1024 * 1024)	/* Max fds if we can't figure it out */
#define STRESS_PID_MAX	(8)

static const stress_help_t help[] = {
	{ NULL,	"fd-fork N",		"start N workers exercising dup/fork/close" },
	{ NULL,	"fd-fork-ops N",	"stop after N dup/fork/close bogo operations" },
	{ NULL,	NULL,			NULL }
};

typedef struct {
	stress_metrics_t metrics;
	bool	use_close_range;
	int	fd_min_val;
	int	fd_max_val;
} stress_fd_close_info_t;

static void stress_fd_close(
	int *fds,
	const size_t n_fds,
	stress_fd_close_info_t *info)
{
	register size_t i, closed = 0;
	double t;

	if (info->use_close_range) {
		t = stress_time_now();
		if (shim_close_range(info->fd_min_val, info->fd_max_val, 0) == 0) {
			info->metrics.duration += stress_time_now() - t;
			info->metrics.count += (double)((info->fd_max_val - info->fd_min_val) + 1);
			return;
		}
		info->use_close_range = false;
	}

	t = stress_time_now();
	for (i = 0; i < n_fds; i++) {
		if (fds[i] != -1) {
			if (close(fds[i]) == 0)
				closed++;
		}
	}
	info->metrics.duration += stress_time_now() - t;
	info->metrics.count += (double)closed;
}

/*
 *  stress_fd_fork()
 *	stress system by rapid dup/fork/close calls
 */
static int stress_fd_fork(stress_args_t *args)
{
	int *fds, rc = EXIT_SUCCESS;
	size_t i, count_fd, start_fd, fds_size, max_fd = stress_get_file_limit();
	stress_fd_close_info_t *info;
	double rate;

	if (max_fd > STRESS_FD_MAX) {
		if (args->instance == 0)
			pr_inf("%s: limited to %d file descriptors (system maximum %zu)\n",
				args->name, STRESS_FD_MAX, max_fd);
		max_fd = STRESS_FD_MAX;
	} else {
		if (args->instance == 0)
			pr_inf("%s: limited to system maximum of %zu file descriptors\n",
				args->name, max_fd);
	}
	fds_size = sizeof(int) * max_fd;

	fds = stress_mmap_populate(NULL, fds_size,
			PROT_READ | PROT_WRITE,
			MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
	if (fds == MAP_FAILED) {
		pr_inf_skip("%s: failed to mmap %d file descriptors, skipping stressor\n",
			args->name, STRESS_FD_MAX);
		return EXIT_NO_RESOURCE;
	}

	info = stress_mmap_populate(NULL, sizeof(*info),
			PROT_READ | PROT_WRITE,
			MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
	if (info == MAP_FAILED) {
		pr_inf_skip("%s: failed to mmap %zu bytes, errno=%d (%s), "
			"skipping stressor\n",
			args->name, sizeof(*info), errno, strerror(errno));
		(void)munmap((void *)fds, fds_size);
		return EXIT_NO_RESOURCE;
	}

	info->metrics.count = 0.0;
	info->metrics.duration = 0.0;
	info->use_close_range = true;

	for (i = 1; i < max_fd; i++)
		fds[i] = -1;

	fds[0] = open("/dev/zero", O_RDONLY);
	info->fd_min_val = fds[0];
	info->fd_max_val = fds[0];

	if (fds[0] < 0) {
		pr_dbg("%s: open failed on /dev/zero, errno=%d (%s)\n",
			args->name, errno, strerror(errno));
		rc = EXIT_NO_RESOURCE;
		goto tidy_fds;
	}

	stress_set_proc_state(args->name, STRESS_STATE_RUN);

	start_fd = 1;
	do {
		pid_t pids[STRESS_PID_MAX];
		size_t n = start_fd + 1000;
		size_t max_pids;
		const bool rnd = stress_mwc1();

		if (n > max_fd)
			n = max_fd;

		for (i = start_fd; i < n; i++) {
			register int fd;

			fd = dup(fds[0]);
			if (fd < 0) {
				max_fd = i - 1;
				break;
			}
			if (fd > info->fd_max_val)
				info->fd_max_val = fd;
			if (fd < info->fd_min_val)
				info->fd_min_val = fd;
			fds[i] = fd;
		}
		start_fd = i;

		for (i = 0; i < STRESS_PID_MAX; i++)
			pids[i] = -1;

		for (max_pids = 0, i = 0; stress_continue(args) && (i < STRESS_PID_MAX); i++) {
			pids[i] = fork();
			if (pids[i] < 0) {
				continue;
			} else if (pids[i] == 0) {
				if (rnd) {
					stress_fd_close(fds, max_fd, info);
				}
				_exit(0);
			} else {
				stress_bogo_inc(args);
				max_pids++;
			}
		}
		for (i = 0; i < STRESS_PID_MAX; i++) {
			if (pids[i] > 1)  {
				int status;

				(void)shim_waitpid(pids[i], &status, 0);
			}
		}
		if (max_pids == 0) {
			pr_inf("%s: could not fork child processes, exiting early\n",
				args->name);
		}
	} while (stress_continue(args));

tidy_fds:
	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);
	stress_fd_close(fds, max_fd, info);

	for (count_fd = 0, i = 0; i < max_fd; i++) {
		if (fds[i] != -1)
			count_fd++;
	}

	if (args->instance == 0) {
		pr_inf("%s: used %s() to close file descriptors\n",
			args->name, info->use_close_range ? "close_range" : "close");
	}

	rate = (info->metrics.count > 0.0) ? (double)info->metrics.duration / info->metrics.count : 0.0;
	stress_metrics_set(args, 0, "nanosecs per fd close",
		rate * STRESS_DBL_NANOSECOND, STRESS_HARMONIC_MEAN);
	stress_metrics_set(args, 1, "file descriptors open at one time",
		(double)count_fd, STRESS_GEOMETRIC_MEAN);

	(void)munmap((void *)info, sizeof(*info));
	(void)munmap((void *)fds, fds_size);

	return rc;
}

stressor_info_t stress_fd_fork_info = {
	.stressor = stress_fd_fork,
	.class = CLASS_FILESYSTEM | CLASS_OS,
	.verify = VERIFY_ALWAYS,
	.help = help
};
