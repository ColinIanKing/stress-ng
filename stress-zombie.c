/*
 * Copyright (C) 2013-2021 Canonical, Ltd.
 * Copyright (C) 2022-2025 Colin Ian King.
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
#include "core-killpid.h"

#define MIN_ZOMBIES		(1)
#define MAX_ZOMBIES		(1000000)
#define DEFAULT_ZOMBIES		(8192)

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
	{ NULL,	"zombie-max N",	"set upper limit of N zombies per worker" },
	{ NULL,	"zombie-ops N",	"stop after N bogo zombie fork operations" },
	{ NULL,	NULL,		NULL }
};

/*
 *  stress_pid_a_zombie()
 *	return false if we are 100% sure the process not a zombie
 */
static bool stress_pid_a_zombie(const pid_t pid)
{
#if defined(__linux__)
	char path[PATH_MAX];
	char buf[4096], *ptr = buf;
	int fd;
	ssize_t n;

	(void)snprintf(path, sizeof(path), "/proc/%" PRIdMAX "/stat", (intmax_t)pid);
	fd = open(path, O_RDONLY);
	if (fd < 0)
		return true;	/* Unknown */
	n = read(fd, buf, sizeof(buf));
	(void)close(fd);
	if (n < 0)
		return true;	/* Unknown */
	while (*ptr) {
		if (*ptr == ')')
			break;
		ptr++;
	}
	if (!*ptr)
		return true;	/* Unknown */
	ptr++;
	while (*ptr) {
		if (*ptr != ' ')
			break;
		ptr++;
	}
	if (!*ptr)
		return true;	/* Unknown */

	/* Process should not be in runnable state */
	return (*ptr == 'Z');
#else
	(void)pid;

	return true; 	/* No idea */
#endif
}

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
		new = (stress_zombie_t *)calloc(1, sizeof(*new));
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
static void stress_zombie_head_remove(stress_args_t *args, const bool check)
{
	const bool verify = !!(g_opt_flags & OPT_FLAGS_VERIFY);

	if (zombies.head) {
		int status;
		stress_zombie_t *head;
		const pid_t pid = zombies.head->pid;

		if (verify && check) {
			if (pid > 1) {
				uint64_t usec = 1;
				bool zombie = false;

				while (usec <= 262144) {
					(void)stress_kill_pid(pid);
					if (stress_pid_a_zombie(pid)) {
						zombie = true;
						break;
					}
					(void)shim_usleep(usec);
					usec <<= 1ULL;
				}
				if (!zombie)
					pr_fail("%s: PID %" PRIdMAX " is not in the expected zombie state\n",
						args->name, (intmax_t)pid);
			}
		}

		(void)shim_waitpid(pid, &status, 0);

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
 *  stress_zombie()
 *	stress by zombieing and exiting
 */
static int stress_zombie(stress_args_t *args)
{
	uint32_t max_zombies = 0;
	uint32_t zombie_max = DEFAULT_ZOMBIES;

	if (!stress_get_setting("zombie-max", &zombie_max)) {
		if (g_opt_flags & OPT_FLAGS_MAXIMIZE)
			zombie_max = MAX_ZOMBIES;
		if (g_opt_flags & OPT_FLAGS_MINIMIZE)
			zombie_max = MIN_ZOMBIES;
	}

	stress_set_proc_state(args->name, STRESS_STATE_SYNC_WAIT);
	stress_sync_start_wait(args);
	stress_set_proc_state(args->name, STRESS_STATE_RUN);

	do {
		if (zombies.length < zombie_max) {
			stress_zombie_t *zombie;

			zombie = stress_zombie_new();
			if (!zombie) {
				stress_zombie_head_remove(args, false);
				continue;
			}

			zombie->pid = fork();
			if (zombie->pid == 0) {
				/*
				 * No need to free, we're going to die
				 *
				stress_zombie_free();
				 */
				stress_set_proc_state(args->name, STRESS_STATE_ZOMBIE);
				_exit(0);
			}
			if (zombie->pid == -1) {
				/* Reached max forks? .. then reap */
				stress_zombie_head_remove(args, false);
				continue;
			}
#if defined(HAVE_GETPGID)
			(void)setpgid(zombie->pid, getpgid(zombie->pid));
#else
			(void)setpgid(zombie->pid, 0);
#endif

			if (max_zombies < zombies.length)
				max_zombies = zombies.length;
			stress_bogo_inc(args);
		} else {
			stress_zombie_head_remove(args, true);
		}
	} while (stress_continue(args));

	stress_metrics_set(args, 0, "created zombies per stressor",
		(double)max_zombies, STRESS_METRIC_HARMONIC_MEAN);

	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);

	/* And reap */
	while (zombies.head) {
		stress_zombie_head_remove(args, false);
	}
	/* And free */
	stress_zombie_free();

	return EXIT_SUCCESS;
}

static const stress_opt_t opts[] = {
	{ OPT_zombie_max, "zombie-max", TYPE_ID_INT32, MIN_ZOMBIES, MAX_ZOMBIES, NULL },
	END_OPT,
};

const stressor_info_t stress_zombie_info = {
	.stressor = stress_zombie,
	.classifier = CLASS_SCHEDULER | CLASS_OS,
	.opts = opts,
	.verify = VERIFY_OPTIONAL,
	.help = help
};
