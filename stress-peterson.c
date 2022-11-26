/*
 * Copyright (C)      2022 Colin Ian King.
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
#include "core-cache.h"

static const stress_help_t help[] = {
	{ NULL,	"peterson N",		"start N workers that exercise Peterson's algorithm" },
	{ NULL,	"peterson-ops N",	"stop after N peterson mutex bogo operations" },
	{ NULL,	NULL,		NULL }
};

#if defined(HAVE_SHIM_MFENCE)

typedef struct peterson {
	volatile int	turn;
	volatile int check;
	volatile bool	flag[2];
	double p0_duration;
	double p0_count;
	double p1_duration;
	double p1_count;
} peterson_t;

static peterson_t *peterson;

static void stress_peterson_p0(const stress_args_t *args)
{
	int check0, check1;
	double t;

	t = stress_time_now();
	peterson->flag[0] = true;
	peterson->turn = 1;
	shim_mfence();
	while (peterson->flag[1] && peterson->turn == 1) {
	}

	/* Critical section */
	check0 = peterson->check;
	peterson->check++;
	check1 = peterson->check;

	peterson->flag[0] = false;
	shim_mfence();
	peterson->p0_duration += stress_time_now() - t;
	peterson->p0_count += 1.0;

	if (check0 + 1 != check1) {
		pr_fail("%s p0: peterson mutex check failed %d vs %d\n",
			args->name, check0 + 1, check1);
	}
}

static void stress_peterson_p1(const stress_args_t *args)
{
	int check0, check1;
	double t;

	t = stress_time_now();
	peterson->flag[1] = true;
	peterson->turn = 0;
	shim_mfence();
	while (peterson->flag[0] && peterson->turn == 0) {
	}

	/* Critical section */
	check0 = peterson->check;
	peterson->check--;
	check1 = peterson->check;
	inc_counter(args);

	peterson->flag[1] = false;
	shim_mfence();
	peterson->p1_duration += stress_time_now() - t;
	peterson->p1_count += 1.0;

	if (check0 - 1 != check1) {
		pr_fail("%s p1: peterson mutex check failed %d vs %d\n",
			args->name, check0 - 1, check1);
	}
}

/*
 *  stress_peterson()
 *	stress peterson algorithm
 */
static int stress_peterson(const stress_args_t *args)
{
	const size_t sz = STRESS_MAXIMUM(args->page_size, sizeof(*peterson));
	pid_t pid;
	double duration, count, rate;

	peterson = (peterson_t *)mmap(NULL, sz, PROT_READ | PROT_WRITE,
			MAP_ANONYMOUS | MAP_SHARED, -1, 0);
	if (peterson == MAP_FAILED) {
		pr_inf_skip("%s: cannot mmap %zd bytes for bekker shared struct, skipping stressor\n",
			args->name, sz);
		return EXIT_NO_RESOURCE;
	}
	stress_set_proc_state(args->name, STRESS_STATE_RUN);

	peterson->flag[0] = false;
	peterson->flag[1] = false;

	pid = fork();
	if (pid < 0) {
		pr_inf_skip("%s: cannot create child process, skipping stressor\n", args->name);
		return EXIT_NO_RESOURCE;
	} else if (pid == 0) {
		/* Child */
		while (keep_stressing(args))
			stress_peterson_p0(args);
		_exit(0);
	} else {
		int status;

		/* Parent */
		while (keep_stressing(args))
			stress_peterson_p1(args);
		(void)kill(pid, SIGKILL);
		(void)waitpid(pid, &status, 0);
	}

	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);

	duration = peterson->p0_duration + peterson->p1_duration;
	count = peterson->p0_count + peterson->p1_count;
	rate = (count > 0.0) ? (duration / count) : 0.0;
	stress_misc_stats_set(args->misc_stats, 0, "nanosecs per mutex", rate * 1000000000.0);

	(void)munmap((void *)peterson, sz);

	return EXIT_SUCCESS;
}

stressor_info_t stress_peterson_info = {
	.stressor = stress_peterson,
	.class = CLASS_CPU_CACHE,
	.verify = VERIFY_ALWAYS,
	.help = help
};

#else

stressor_info_t stress_peterson_info = {
	.stressor = stress_unimplemented,
	.class = CLASS_CPU,
	.verify = VERIFY_ALWAYS,
	.help = help,
	.unimplemented_reason = "built without user space memory fencing"
};

#endif
