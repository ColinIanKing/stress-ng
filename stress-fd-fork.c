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
#include "core-killpid.h"
#include "core-mmap.h"

#include <sched.h>

#define STRESS_FD_MIN		(1000)
#define STRESS_FD_MAX		(16000000)	/* Max fds if we can't figure it out */
#define STRESS_FD_DEFAULT	(2000000)	/* Default fds */
#define STRESS_PID_MAX		(8)

#define STRESS_FD_NULL		(0)
#define STRESS_FD_RANDOM	(1)
#define STRESS_FD_STDIN		(2)
#define STRESS_FD_STDOUT	(3)
#define STRESS_FD_ZERO		(4)


typedef struct {
	stress_metrics_t metrics;
	bool	use_close_range;
	int	fd_min_val;
	int	fd_max_val;
} stress_fd_close_info_t;

typedef struct {
	const char *name;
	const int fd_type;
} stress_fd_file_t;

static const stress_help_t help[] = {
	{ NULL,	"fd-fork N",		"start N workers exercising dup/fork/close" },
	{ NULL, "fd-fork-file file",	"select file to dup [ null, random, stdin, stdout, zero ]" },
	{ NULL,	"fd-fork-fds N",	"set maximum number of file descriptors to use" },
	{ NULL,	"fd-fork-ops N",	"stop after N dup/fork/close bogo operations" },
	{ NULL,	NULL,			NULL }
};

static const stress_fd_file_t stress_fd_files[] = {
	{ "null",	STRESS_FD_NULL },
	{ "random",	STRESS_FD_RANDOM },
	{ "stdin",	STRESS_FD_STDIN },
	{ "stdout",	STRESS_FD_STDOUT },
	{ "zero",	STRESS_FD_ZERO },
};

static const char *stress_fd_fork_file(const size_t i)
{
	return (i < SIZEOF_ARRAY(stress_fd_files)) ? stress_fd_files[i].name : NULL;
}

static const stress_opt_t opts[] = {
	{ OPT_fd_fork_fds,  "fd-fork-fds",  TYPE_ID_SIZE_T, STRESS_FD_MIN, STRESS_FD_MAX, NULL },
	{ OPT_fd_fork_file, "fd-fork-file", TYPE_ID_SIZE_T_METHOD, 0, 0, stress_fd_fork_file },
	END_OPT,
};

static void stress_fd_close(
	const int *fds,
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
	size_t i, count_fd = 1, start_fd = 1, fds_size;
	size_t max_fd = stress_get_file_limit();
	size_t fd_fork_fds = STRESS_FD_DEFAULT;
	size_t fd_fork_file = STRESS_FD_ZERO;
	stress_fd_close_info_t *info;
	double rate, t_start = -1.0, t_max = -1.0;
	char *filename;

	if (!stress_get_setting("fd-fork-fds", &fd_fork_fds)) {
		if (g_opt_flags & OPT_FLAGS_MAXIMIZE)
			fd_fork_fds = STRESS_FD_MAX;
		if (g_opt_flags & OPT_FLAGS_MINIMIZE)
			fd_fork_fds = STRESS_FD_MIN;
	}
	(void)stress_get_setting("fd-fork-file", &fd_fork_file);

	if (fd_fork_fds > max_fd) {
		if (stress_instance_zero(args))
			pr_inf("%s: limited to system maximum of %zu file descriptors\n",
				args->name, max_fd);
		fd_fork_fds = max_fd;
	}

	fds_size = sizeof(int) * fd_fork_fds;
	fds = stress_mmap_populate(NULL, fds_size,
			PROT_READ | PROT_WRITE,
			MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
	stress_set_vma_anon_name(fds, fds_size, "fds");
	if (fds == MAP_FAILED) {
		pr_inf_skip("%s: failed to mmap %d file descriptors%s, skipping stressor\n",
			args->name, STRESS_FD_MAX, stress_get_memfree_str());
		return EXIT_NO_RESOURCE;
	}

	info = stress_mmap_populate(NULL, sizeof(*info),
			PROT_READ | PROT_WRITE,
			MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
	if (info == MAP_FAILED) {
		pr_inf_skip("%s: failed to mmap %zu bytes%s, errno=%d (%s), "
			"skipping stressor\n",
			args->name, sizeof(*info), stress_get_memfree_str(),
			errno, strerror(errno));
		(void)munmap((void *)fds, fds_size);
		return EXIT_NO_RESOURCE;
	}
	stress_set_vma_anon_name(info, sizeof(*info), "state");
	stress_zero_metrics(&info->metrics, 1);
	info->use_close_range = true;

	for (i = 1; i < fd_fork_fds; i++)
		fds[i] = -1;

	switch (fd_fork_file) {
	default:
	case STRESS_FD_ZERO:
		filename = "/dev/zero";
		fds[0] = open(filename, O_RDONLY);
		break;
	case STRESS_FD_NULL:
		filename = "/dev/null";
		fds[0] = open(filename, O_WRONLY);
		break;
	case STRESS_FD_STDIN:
		filename = "stdin";
		fds[0] = dup(fileno(stdin));
		break;
	case STRESS_FD_STDOUT:
		filename = "stdout";
		fds[0] = dup(fileno(stdout));
		break;
	case STRESS_FD_RANDOM:
		filename = "/dev/random";
		fds[0] = open(filename, O_RDONLY);
		break;
	}

	info->fd_min_val = fds[0];
	info->fd_max_val = fds[0];
	if (fds[0] < 0) {
		pr_dbg("%s: open failed on /dev/zero, errno=%d (%s)\n",
			args->name, errno, strerror(errno));
		rc = EXIT_NO_RESOURCE;
		goto tidy_fds;
	}

	stress_set_proc_state(args->name, STRESS_STATE_SYNC_WAIT);
	stress_sync_start_wait(args);
	stress_set_proc_state(args->name, STRESS_STATE_RUN);

	t_start = stress_time_now();
	do {
		pid_t pids[STRESS_PID_MAX];
		size_t n = (start_fd == 1) ? 10000 : start_fd + 10000;
		size_t max_pids;
		const bool rnd = stress_mwc1();

		if (n > fd_fork_fds)
			n = fd_fork_fds;

		for (i = start_fd; i < n; i++) {
			register int fd;

			fd = dup(fds[0]);
			if (fd < 0) {
				fd_fork_fds = i - 1;
				t_max = stress_time_now();
				break;
			}
			if (fd > info->fd_max_val)
				info->fd_max_val = fd;
			if (fd < info->fd_min_val)
				info->fd_min_val = fd;
			fds[i] = fd;
			count_fd++;
		}
		start_fd = i;
		if ((count_fd >= fd_fork_fds) && (t_max < 0.0))
			t_max = stress_time_now();

		for (i = 0; i < STRESS_PID_MAX; i++)
			pids[i] = -1;

		for (max_pids = 0, i = 0; LIKELY(stress_continue(args) && (i < STRESS_PID_MAX)); i++) {
			pids[i] = fork();
			if (pids[i] < 0) {
				continue;
			} else if (pids[i] == 0) {
				stress_set_proc_state(args->name, STRESS_STATE_RUN);
				if (rnd) {
					stress_fd_close(fds, fd_fork_fds, info);
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
	stress_fd_close(fds, fd_fork_fds, info);

	if (stress_instance_zero(args)) {
		pr_inf("%s: used %s() to close ~%d file descriptors on %s\n",
			args->name,
			info->use_close_range ? "close_range" : "close",
			1 + info->fd_max_val - info->fd_min_val,
			filename);
	}

	rate = (info->metrics.count > 0.0) ? (double)info->metrics.duration / info->metrics.count : 0.0;
	stress_metrics_set(args, 0, "nanosecs per fd close",
		rate * STRESS_DBL_NANOSECOND, STRESS_METRIC_HARMONIC_MEAN);
	stress_metrics_set(args, 1, "file descriptors open at one time",
		(double)count_fd, STRESS_METRIC_GEOMETRIC_MEAN);
	if (t_max > 0.0) {
		const double duration = t_max - t_start;

		stress_metrics_set(args, 2, "seconds to open all file descriptors",
			(double)duration, STRESS_METRIC_GEOMETRIC_MEAN);
	}

	(void)munmap((void *)info, sizeof(*info));
	(void)munmap((void *)fds, fds_size);

	return rc;
}

const stressor_info_t stress_fd_fork_info = {
	.stressor = stress_fd_fork,
	.classifier = CLASS_FILESYSTEM | CLASS_OS,
	.opts = opts,
	.verify = VERIFY_ALWAYS,
	.help = help
};
