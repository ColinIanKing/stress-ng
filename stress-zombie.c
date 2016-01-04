/*
 * Copyright (C) 2013-2016 Canonical, Ltd.
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
#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>

#include "stress-ng.h"

static uint64_t opt_zombie_max = DEFAULT_ZOMBIES;
static bool set_zombie_max = false;

typedef struct zombie {
	pid_t	pid;
	struct zombie *next;
} zombie_t;

typedef struct {
	zombie_t *head;		/* Head of zombie procs list */
	zombie_t *tail;		/* Tail of zombie procs list */
	zombie_t *free;		/* List of free'd zombies */
	uint64_t length;	/* Length of list */
} zombie_list_t;

static zombie_list_t zombies;

/*
 *  stress_zombie_new()
 *	allocate a new zombie, add to end of list
 */
static zombie_t *stress_zombie_new(void)
{
	zombie_t *new;

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
void stress_zombie_head_remove(void)
{
	if (zombies.head) {
		int status;

		(void)waitpid(zombies.head->pid, &status, 0);

		zombie_t *head = zombies.head;

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
void stress_zombie_free(void)
{
	while (zombies.head) {
		zombie_t *next = zombies.head->next;

		free(zombies.head);
		zombies.head = next;
	}
	while (zombies.free) {
		zombie_t *next = zombies.free->next;

		free(zombies.free);
		zombies.free = next;
	}
}

/*
 *  stress_set_zombie_max()
 *	set maximum number of zombies allowed
 */
void stress_set_zombie_max(const char *optarg)
{
	set_zombie_max = true;
	opt_zombie_max = get_uint64_byte(optarg);
	check_range("zombie-max", opt_zombie_max,
		MIN_ZOMBIES, MAX_ZOMBIES);
}

/*
 *  stress_zombie()
 *	stress by zombieing and exiting
 */
int stress_zombie(
	uint64_t *const counter,
	const uint32_t instance,
	const uint64_t max_ops,
	const char *name)
{
	uint64_t max_zombies = 0;

	(void)instance;

	if (!set_zombie_max) {
		if (opt_flags & OPT_FLAGS_MAXIMIZE)
			opt_zombie_max = MAX_ZOMBIES;
		if (opt_flags & OPT_FLAGS_MINIMIZE)
			opt_zombie_max = MIN_ZOMBIES;
	}

	do {
		if (zombies.length < opt_zombie_max) {
			zombie_t *zombie;

			zombie = stress_zombie_new();
			if (!zombie)
				break;

			zombie->pid = fork();
			if (zombie->pid == 0) {
				setpgid(0, pgrp);
				stress_parent_died_alarm();

				stress_zombie_free();
				_exit(0);
			}
			if (zombie->pid == -1) {
				/* Reached max forks? .. then reap */
				stress_zombie_head_remove();
				continue;
			}
			setpgid(zombie->pid, pgrp);

			if (max_zombies < zombies.length)
				max_zombies = zombies.length;
			(*counter)++;
		} else {
			stress_zombie_head_remove();
		}
	} while (opt_do_run && (!max_ops || *counter < max_ops));

	pr_inf(stderr, "%s: created a maximum of %" PRIu64 " zombies\n",
		name, max_zombies);

	/* And reap */
	while (zombies.head) {
		stress_zombie_head_remove();
	}
	/* And free */
	stress_zombie_free();

	return EXIT_SUCCESS;
}
