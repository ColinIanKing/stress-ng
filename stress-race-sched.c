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

#define DEFAULT_CHILDREN	(8)

typedef struct stress_race_sched {
	struct stress_race_sched *next;
	pid_t	pid;
} stress_race_sched_child_t;

typedef struct {
	stress_race_sched_child_t *head;	/* Head of process list */
	stress_race_sched_child_t *tail;	/* Tail of process list */
	stress_race_sched_child_t *free;	/* List of free'd processes */
	uint32_t length;			/* Length of list */
} stress_race_sched_list_t;

static const stress_help_t help[] = {
	{ NULL,	"race-sched N",		"start N workers that race cpu affinity" },
	{ NULL,	"race-sched-ops N",	"stop after N bogo race operations" },
	{ NULL,	NULL,		NULL }
};

static stress_race_sched_list_t children;

static void stress_race_sched_exercise(const int cpus)
{
	cpu_set_t cpu_set;
	stress_race_sched_child_t *child;
	int i;

	for (i = 0; keep_stressing_flag() && (i < 20); i++)  {
		for (child = children.head; child; child = child->next) {
			int ret;

			CPU_ZERO(&cpu_set);
			ret = sched_getaffinity(child->pid, sizeof(cpu_set), &cpu_set);
			if (ret == 0) {
				const int new_cpu = (int)(stress_mwc32() % (uint32_t)cpus);

				CPU_ZERO(&cpu_set);
				CPU_SET(new_cpu, &cpu_set);
				VOID_RET(int, sched_setaffinity(child->pid, sizeof(cpu_set), &cpu_set));
			}
		}
	}
}

/*
 *  stress_race_sched_new()
 *	allocate a new child, add to end of list
 */
static stress_race_sched_child_t *stress_race_sched_new(void)
{
	stress_race_sched_child_t *new;

	if (children.free) {
		/* Pop an old one off the free list */
		new = children.free;
		children.free = new->next;
		new->next = NULL;
	} else {
		new = calloc(1, sizeof(*new));
		if (!new)
			return NULL;
	}

	if (children.head)
		children.tail->next = new;
	else
		children.head = new;

	children.tail = new;
	children.length++;

	return new;
}

/*
 *  stress_race_sched_head_remove
 *	reap a child and remove a child from head of list, put it onto
 *	the free child list
 */
static void stress_race_sched_head_remove(const int options)
{
	if (children.head) {
		int status, ret;
		stress_race_sched_child_t *head = children.head;

		ret = waitpid(children.head->pid, &status, options);
		if (ret >= 0) {
			if (children.tail == children.head) {
				children.tail = NULL;
				children.head = NULL;
			} else {
				children.head = head->next;
			}
			/* Shove it on the free list */
			head->next = children.free;
			children.free = head;
			children.length--;
		}
	}
}

/*
 *  stress_race_sched_free()
 *	free the children off the child free lists
 */
static void stress_race_sched_free(void)
{
	while (children.head) {
		stress_race_sched_child_t *next = children.head->next;

		free(children.head);
		children.head = next;
	}
	while (children.free) {
		stress_race_sched_child_t *next = children.free->next;

		free(children.free);
		children.free = next;
	}
}

static int stress_race_sched_child(const stress_args_t *args, void *context)
{
	/* Child */
	uint32_t max_forks = 0;
	uint32_t children_max = DEFAULT_CHILDREN;
	int cpu = 0, cpus = (int)stress_get_processors_configured();

	(void)context;

	if (cpus < 1)
		cpus = 1;

	do {
		const bool low_mem_reap = ((g_opt_flags & OPT_FLAGS_OOM_AVOID) &&
					   stress_low_memory((size_t)(1 * MB)));

		if (!low_mem_reap && (children.length < children_max)) {
			stress_race_sched_child_t *child_info;

			child_info = stress_race_sched_new();
			if (!child_info)
				break;

			child_info->pid = fork();
			if (child_info->pid < 0) {
				/*
				 * Reached max forks or error
				 * (e.g. EPERM)? .. then reap
				 */
				stress_race_sched_exercise(cpus);
				stress_race_sched_head_remove(WNOHANG);
				continue;
			} else if (child_info->pid == 0) {
				/* child */
				cpu_set_t cpu_set;
				int inc = (int)(stress_mwc8() % 20);

				VOID_RET(int, shim_nice(inc));

				/* Move process to next cpu */
				cpu = (cpu + 1) % cpus;
				CPU_ZERO(&cpu_set);
				CPU_SET(cpu, &cpu_set);
				VOID_RET(int, sched_setaffinity(0, sizeof(cpu_set), &cpu_set));

				stress_race_sched_exercise(cpus);
				/* child */
				_exit(0);
			} else {
				/* parent */
				stress_race_sched_exercise(cpus);
			}

			if (max_forks < children.length)
				max_forks = children.length;
			inc_counter(args);
			cpu = (cpu + 1) % cpus;
		} else {
			stress_race_sched_exercise(cpus);
			stress_race_sched_head_remove(WNOHANG);
			stress_race_sched_exercise(cpus);
		}
	} while (keep_stressing(args));

	/* And reap */
	while (children.head) {
		stress_race_sched_head_remove(0);
	}
	/* And free */
	stress_race_sched_free();

	return EXIT_SUCCESS;
}

/*
 *  stress_race_sched()
 *	stress by cloning and exiting
 */
static int stress_race_sched(const stress_args_t *args)
{
	int rc;

	stress_set_oom_adjustment(args->name, false);

	stress_set_proc_state(args->name, STRESS_STATE_RUN);
	rc = stress_oomable_child(args, NULL, stress_race_sched_child, STRESS_OOMABLE_DROP_CAP);
	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);

	return rc;
}

stressor_info_t stress_race_sched_info = {
	.stressor = stress_race_sched,
	.class = CLASS_SCHEDULER | CLASS_OS,
	.help = help
};
