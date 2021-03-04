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

typedef struct stress_zombie {
	struct stress_zombie *next;
	pid_t	pid;
} stress_zombie_t;

typedef struct {
	stress_zombie_t *head;	/* Head of zombie procs list */
	stress_zombie_t *tail;	/* Tail of zombie procs list */
	stress_zombie_t *free;	/* List of free'd zombies */
	uint32_t length;	/* Length of list */
} stress_zombie_list_t;

static stress_zombie_list_t zombies;

static const stress_help_t help[] = {
	{ NULL,	"zombie N",	"start N workers that rapidly create and reap zombies" },
	{ NULL,	"zombie-ops N",	"stop after N bogo zombie fork operations" },
	{ NULL,	"zombie-max N",	"set upper limit of N zombies per worker" },
	{ NULL,	NULL,		NULL }
};

/*
 *  stress_zombie_new()
 *	allocate a new zombie, add to end of list
 */
static stress_zombie_t *stress_zombie_new(void)
{
	stress_zombie_t *new;

	if (zombies.free) {
		/* Pop an old one off the free list */
		new = zombies.free;
		zombies.free = new->next;
		new->next = NULL;
	} else {
		new = calloc(1, sizeof(*new));
		if (!new)
			return NULL;
	}

	if (zombies.head)
		zombies.tail->next = new;
	else
		zombies.head = new;

	zombies.tail = new;
	zombies.length++;

	return new;
}

/*
 *  stress_zombie_head_remove
 *	reap a zombie and remove a zombie from head of list, put it onto
 *	the free zombie list
 */
static void stress_zombie_head_remove(void)
{
	if (zombies.head) {
		int status;
		stress_zombie_t *head;

		(void)shim_waitpid(zombies.head->pid, &status, 0);

		head = zombies.head;
		if (zombies.tail == zombies.head) {
			zombies.tail = NULL;
			zombies.head = NULL;
		} else {
			zombies.head = head->next;
		}

		/* Shove it on the free list */
		head->next = zombies.free;
		zombies.free = head;

		zombies.length--;
	}
}

/*
 *  stress_zombie_free()
 *	free the zombies off the zombie free lists
 */
static void stress_zombie_free(void)
{
	while (zombies.head) {
		stress_zombie_t *next = zombies.head->next;

		free(zombies.head);
		zombies.head = next;
	}
	while (zombies.free) {
		stress_zombie_t *next = zombies.free->next;

		free(zombies.free);
		zombies.free = next;
	}
}

/*
 *  stress_set_zombie_max()
 *	set maximum number of zombies allowed
 */
static int stress_set_zombie_max(const char *opt)
{
	uint32_t zombie_max;

	zombie_max = stress_get_uint32(opt);
	stress_check_range("zombie-max", zombie_max,
		MIN_ZOMBIES, MAX_ZOMBIES);
	return stress_set_setting("zombie-max", TYPE_ID_INT32, &zombie_max);
}

/*
 *  stress_zombie()
 *	stress by zombieing and exiting
 */
static int stress_zombie(const stress_args_t *args)
{
	uint32_t max_zombies = 0;
	uint32_t zombie_max = DEFAULT_ZOMBIES;

	if (!stress_get_setting("zombie-max", &zombie_max)) {
		if (g_opt_flags & OPT_FLAGS_MAXIMIZE)
			zombie_max = MAX_ZOMBIES;
		if (g_opt_flags & OPT_FLAGS_MINIMIZE)
			zombie_max = MIN_ZOMBIES;
	}

	stress_set_proc_state(args->name, STRESS_STATE_RUN);

	do {
		if (zombies.length < zombie_max) {
			stress_zombie_t *zombie;

			zombie = stress_zombie_new();
			if (!zombie) {
				stress_zombie_head_remove();
				continue;
			}

			zombie->pid = fork();
			if (zombie->pid == 0) {
				/*
				 * No need to free, we're going to die
				 *
				stress_zombie_free();
				 */
				_exit(0);
			}
			if (zombie->pid == -1) {
				/* Reached max forks? .. then reap */
				stress_zombie_head_remove();
				continue;
			}
			(void)setpgid(zombie->pid, g_pgrp);

			if (max_zombies < zombies.length)
				max_zombies = zombies.length;
			inc_counter(args);
		} else {
			stress_zombie_head_remove();
		}
	} while (keep_stressing(args));

	pr_inf("%s: created a maximum of %" PRIu32 " zombies\n",
		args->name, max_zombies);

	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);

	/* And reap */
	while (zombies.head) {
		stress_zombie_head_remove();
	}
	/* And free */
	stress_zombie_free();

	return EXIT_SUCCESS;
}

static const stress_opt_set_func_t opt_set_funcs[] = {
	{ OPT_zombie_max,	stress_set_zombie_max },
	{ 0,			NULL }
};

stressor_info_t stress_zombie_info = {
	.stressor = stress_zombie,
	.class = CLASS_SCHEDULER | CLASS_OS,
	.opt_set_funcs = opt_set_funcs,
	.help = help
};
