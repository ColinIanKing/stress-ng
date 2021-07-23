/*
 * Copyright (C) 2013-2021 Canonical, Ltd.
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
	{ NULL,	"dirmany N",	"start N directory file populating stressors" },
	{ NULL,	"dirmany-ops N","stop after N directory file bogo operations" },
	{ NULL,	NULL,		NULL }
};

static uint64_t stress_dirmany_create(
	const stress_args_t *args,
	const char *path,
	const double t_start,
	const double i_start,
	double *create_time)
{
	const double t_now = stress_time_now();
	const double t_left = (t_start + (double)g_opt_timeout) - t_now;
	/* Assume create takes 60%, remove takes 40% of run time */
	const double t_end = t_now + (t_left * 0.60);
	uint64_t i_end = i_start;

	while (keep_stressing(args) && (stress_time_now() <= t_end)) {
		char filename[PATH_MAX + 20];
		int fd;

		(void)snprintf(filename, sizeof(filename), "%s/f%16.16" PRIx64, path, i_end++);
		fd = open(filename, O_CREAT | O_TRUNC | O_RDWR, S_IRUSR | S_IWUSR);
		if (fd < 0)
			break;
		inc_counter(args);
		(void)close(fd);
	}

	*create_time += (stress_time_now() - t_now);

	return i_end;
}

static void stress_dirmany_remove(
	const char *path,
	const uint64_t i_start,
	uint64_t i_end,
	double *remove_time)
{
	uint64_t i;
	const double t_now = stress_time_now();

	for (i = i_start; i < i_end; i++) {
		char filename[PATH_MAX + 20];

		(void)snprintf(filename, sizeof(filename), "%s/f%16.16" PRIx64, path, i);
		(void)unlink(filename);
	}
	*remove_time += (stress_time_now() - t_now);
}

/*
 *  stress_dirmany
 *	stress directory with many empty files
 */
static int stress_dirmany(const stress_args_t *args)
{
	int ret;
	uint64_t i_start = 0;
	char pathname[PATH_MAX];
	const double t_start = stress_time_now();
	double create_time = 0.0, remove_time = 0.0, total_time = 0.0;

	stress_temp_dir(pathname, sizeof(pathname), args->name, args->pid, args->instance);

	ret = stress_temp_dir_mk_args(args);
	if (ret < 0)
		return exit_status(-ret);

	stress_set_proc_state(args->name, STRESS_STATE_RUN);

	do {
		uint64_t i_end;

		i_end = stress_dirmany_create(args, pathname, t_start, i_start, &create_time);
		stress_dirmany_remove(pathname, i_start, i_end, &remove_time);
		i_start = i_end;

		/* Avoid i_start wraparound */
		if (i_start > 1000000000)
			i_start = 0;
	} while (keep_stressing(args));

	total_time = create_time + remove_time;
	if (total_time > 0.0) {
		pr_inf("%s: %.2f%% create time, %.2f%% remove time\n",
			args->name,
			create_time / total_time * 100.0,
			remove_time / total_time * 100.0);
	}

	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);

	(void)stress_temp_dir_rm_args(args);

	return ret;
}

stressor_info_t stress_dirmany_info = {
	.stressor = stress_dirmany,
	.class = CLASS_FILESYSTEM | CLASS_OS,
	.help = help
};
