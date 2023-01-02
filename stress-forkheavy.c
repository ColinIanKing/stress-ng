/*
 * Copyright (C) 2022-2023 Colin Ian King.
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
#include "core-resources.h"

#define MIN_MEM_FREE		((size_t)(16 * MB))

#define DEFAULT_FORKHEAVY_PROCS		(4096)
#define MIN_FORKHEAVY_PROCS		(1)
#define MAX_FORKHEAVY_PROCS		(65536)

#define DEFAULT_FORKHEAVY_ALLOCS	(16384)
#define MIN_FORKHEAVY_ALLOCS		(1)
#define MAX_FORKHEAVY_ALLOCS		(1024 * 1024)

typedef struct {
	void *lock;			/* lock on shared metrics data */
	double duration;		/* total duration for fork calls */
	double count;			/* total number of fork calls */
	volatile double t_start;	/* start time of fork call */
} stress_forkheavy_metrics_t;

typedef struct stress_forkheavy_args {
	const stress_args_t *args;
	stress_forkheavy_metrics_t *metrics;
} stress_forkheavy_args_t;

typedef struct stress_forkheavy {
	struct stress_forkheavy *next;	/* next item in list */
	pid_t	pid;			/* pid of fork'd process */
} stress_forkheavy_t;

typedef struct {
	stress_forkheavy_t *head;	/* Head of forked procs list */
	stress_forkheavy_t *tail;	/* Tail of forked procs list */
	stress_forkheavy_t *free;	/* List of free'd forked info */
	uint32_t length;		/* Length of list */
} stress_forkheavy_list_t;

static const stress_help_t help[] = {
	{ NULL,	"forkheavy N",		"start N workers that rapidly fork and reap resource heavy processes" },
	{ NULL,	"forkheavy-allocs N",	"attempt to allocate N x resources" },
	{ NULL,	"forkheavy-ops N",	"stop after N bogo fork operations" },
	{ NULL, "forkheavy-procs N",	"attempt to fork N processes" },
	{ NULL,	NULL,			NULL }
};

/*
 *  stress_set_forkeheavy_allocs()
 *	set maximum number of resources allocated
 */
static int stress_set_forkheavy_allocs(const char *opt)
{
	uint32_t forkheavy_allocs;

	forkheavy_allocs = stress_get_uint32(opt);
	stress_check_range("forkheavy-allocs", (uint64_t)forkheavy_allocs,
		MIN_FORKHEAVY_ALLOCS, MAX_FORKHEAVY_ALLOCS);
	return stress_set_setting("forkheavy-allocs", TYPE_ID_UINT32, &forkheavy_allocs);
}

/*
 *  stress_set_forkeheavy_procs()
 *	set maximum number of processes allowed
 */
static int stress_set_forkheavy_procs(const char *opt)
{
	uint32_t forkheavy_procs;

	forkheavy_procs = stress_get_uint32(opt);
	stress_check_range("forkheavy-procs", (uint64_t)forkheavy_procs,
		MIN_FORKHEAVY_PROCS, MAX_FORKHEAVY_PROCS);
	return stress_set_setting("forkheavy-procs", TYPE_ID_UINT32, &forkheavy_procs);
}

static stress_forkheavy_list_t forkheavy_list;

/*
 *  stress_forkheavy_new()
 *	allocate a new process, add to end of list
 */
static stress_forkheavy_t *stress_forkheavy_new(void)
{
	stress_forkheavy_t *new;

	if (forkheavy_list.free) {
		/* Pop an old one off the free list */
		new = forkheavy_list.free;
		forkheavy_list.free = new->next;
		new->next = NULL;
	} else {
		new = malloc(sizeof(*new));
		if (!new)
			return NULL;
	}

	if (forkheavy_list.head)
		forkheavy_list.tail->next = new;
	else
		forkheavy_list.head = new;

	forkheavy_list.tail = new;
	forkheavy_list.length++;

	return new;
}

/*
 *  stress_forkheavy_head_remove
 *	reap a clone and remove a clone from head of list, put it onto
 *	the free clone list
 */
static void stress_forkheavy_head_remove(void)
{
	if (forkheavy_list.head) {
		int status;
		stress_forkheavy_t *head = forkheavy_list.head;

		(void)waitpid(forkheavy_list.head->pid, &status, 0);

		if (forkheavy_list.tail == forkheavy_list.head) {
			forkheavy_list.tail = NULL;
			forkheavy_list.head = NULL;
		} else {
			forkheavy_list.head = head->next;
		}

		/* Shove it on the free list */
		head->next = forkheavy_list.free;
		forkheavy_list.free = head;

		forkheavy_list.length--;
	}
}

/*
 *  stress_forkheavy_free()
 *	free the forkheavy_list off the free lists
 */
static void stress_forkheavy_free(void)
{
	while (forkheavy_list.head) {
		stress_forkheavy_t *next = forkheavy_list.head->next;

		free(forkheavy_list.head);
		forkheavy_list.head = next;
	}
	while (forkheavy_list.free) {
		stress_forkheavy_t *next = forkheavy_list.free->next;

		free(forkheavy_list.free);
		forkheavy_list.free = next;
	}
}

static int stress_forkheavy_child(const stress_args_t *args, void *context)
{
	const stress_forkheavy_args_t *forkheavy_args = (stress_forkheavy_args_t *)context;
	stress_forkheavy_metrics_t *metrics = forkheavy_args->metrics;
	uint32_t forkheavy_allocs = DEFAULT_FORKHEAVY_ALLOCS;
	uint32_t forkheavy_procs = DEFAULT_FORKHEAVY_PROCS;

	(void)stress_get_setting("forkheavy-allocs", &forkheavy_allocs);
	(void)stress_get_setting("forkheavy-procs", &forkheavy_procs);

	do {
		const bool low_mem_reap = stress_low_memory(MIN_MEM_FREE);

		if (!low_mem_reap && (forkheavy_list.length < forkheavy_procs)) {
			stress_forkheavy_t *forkheavy;
			bool update_metrics;

			forkheavy = stress_forkheavy_new();
			if (!forkheavy)
				break;

			if (metrics->lock && (stress_lock_acquire(metrics->lock) == 0)) {
				update_metrics = true;
				metrics->t_start = stress_time_now();
				stress_lock_release(metrics->lock);
			} else {
				update_metrics = false;
			}
			forkheavy->pid = fork();
			if (forkheavy->pid == 0) {
				const double duration = stress_time_now() - metrics->t_start;

				if (update_metrics && (duration > 0.0) &&
				    (stress_lock_acquire(metrics->lock) == 0)) {
					metrics->duration += duration;
					metrics->count += 1.0;
					stress_lock_release(metrics->lock);
				}
				_exit(0);
			} else if (forkheavy->pid == -1) {
				/*
				 * Reached max forks or error
				 * (e.g. EPERM)? .. then reap
				 */
				sleep(200);
				stress_forkheavy_head_remove();
				continue;
			} else {
				inc_counter(args);
			}
		} else {
			stress_forkheavy_head_remove();
		}
	} while (keep_stressing(args));

	/* And reap */
	while (forkheavy_list.head) {
		stress_forkheavy_head_remove();
	}
	/* And free */
	stress_forkheavy_free();

	return EXIT_SUCCESS;
}

/*
 *  stress_forkheavy()
 *	stress by forking with many resources allocated and exiting
 */
static int stress_forkheavy(const stress_args_t *args)
{
	int rc;
	double average;
	stress_resources_t *resources;
	size_t num_resources = DEFAULT_FORKHEAVY_ALLOCS;
	const size_t pipe_size = stress_probe_max_pipe_size();
	stress_forkheavy_metrics_t *metrics;
	stress_forkheavy_args_t forkheavy_args;
	size_t shmall, freemem, totalmem, freeswap, totalswap, min_mem_free;

	resources = malloc(num_resources * sizeof(*resources));
	if (!resources) {
		pr_inf_skip("%s: cannot allocate %zd resource structures, skipping stressor\n",
			args->name, num_resources);
		return EXIT_NO_RESOURCE;
	}

	metrics = mmap(NULL, sizeof(*metrics), PROT_READ | PROT_WRITE,
			MAP_SHARED | MAP_ANONYMOUS, -1, 0);
	if (metrics == MAP_FAILED) {
		pr_inf_skip("%s: failed to memory map %zd bytes, skipping stressor\n",
			args->name, sizeof(*metrics));
		free(resources);
		return EXIT_NO_RESOURCE;
	}
	metrics->lock = stress_lock_create();
	metrics->duration = 0.0;
	metrics->count = 0.0;
	metrics->t_start = 0.0;

	stress_get_memlimits(&shmall, &freemem, &totalmem, &freeswap, &totalswap);
	min_mem_free = (freemem / 100) * 2;
	if (min_mem_free < MIN_MEM_FREE)
		min_mem_free = MIN_MEM_FREE;

	num_resources = stress_resources_allocate(args, resources, num_resources, pipe_size, min_mem_free, false);
	if (num_resources < 1) {
		pr_inf("%s: failed to allocate resources, skipping stressor\n", args->name);
		(void)munmap((void *)metrics, sizeof(*metrics));
		free(resources);
		return EXIT_NO_RESOURCE;
	}
	forkheavy_args.metrics = metrics;

	stress_set_oom_adjustment(args->name, false);

	stress_set_proc_state(args->name, STRESS_STATE_RUN);
	rc = stress_oomable_child(args, &forkheavy_args, stress_forkheavy_child, STRESS_OOMABLE_DROP_CAP);
	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);

	average = (metrics->count > 0.0) ? metrics->duration / metrics->count : 0.0;
	stress_metrics_set(args, 0, "microsecs per fork" , average * 1000000);

	(void)munmap((void *)metrics, sizeof(*metrics));
	stress_resources_free(args, resources, num_resources);
	free(resources);

	return rc;
}

static const stress_opt_set_func_t forkheavy_opt_set_funcs[] = {
	{ OPT_forkheavy_allocs,	stress_set_forkheavy_allocs },
	{ OPT_forkheavy_procs,	stress_set_forkheavy_procs },
	{ 0,			NULL }
};

stressor_info_t stress_forkheavy_info = {
	.stressor = stress_forkheavy,
	.class = CLASS_SCHEDULER | CLASS_OS,
	.opt_set_funcs = forkheavy_opt_set_funcs,
	.help = help
};
