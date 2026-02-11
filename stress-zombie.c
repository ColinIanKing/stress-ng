/*
 * Copyright (C) 2013-2021 Canonical, Ltd.
 * Copyright (C) 2022-2026 Colin Ian King.
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
#include "core-capabilities.h"
#include "core-killpid.h"

#if defined(HAVE_SYS_MOUNT_H)
#include <sys/mount.h>
#endif

#include <arpa/inet.h>

#define MIN_ZOMBIES		(1)
#define MAX_ZOMBIES		(1000000)
#define DEFAULT_ZOMBIES		(8192)
#define ZOMBIE_STACK_SIZE	(1024)

#if defined(__linux__) &&	\
    defined(HAVE_CLONE) &&	\
    defined(CLONE_NEWCGROUP) &&	\
    defined(CLONE_NEWNS) &&	\
    defined(CLONE_NEWNET) &&	\
    defined(CLONE_NEWPID) &&	\
    defined(CLONE_NEWIPC) &&	\
    defined(SIGCHLD)
#define HAVE_LINUX_CLONE
#endif

#if defined(CLONE_NEWCGROUP)
#define SHIM_CLONE_NEWCGROUP	CLONE_NEWCGROUP
#else
#define SHIM_CLONE_NEWCGROUP	(0)
#endif

#if defined(CLONE_NEWNS)
#define SHIM_CLONE_NEWNS	CLONE_NEWNS
#else
#define SHIM_CLONE_NEWNS	(0)
#endif

#if defined(CLONE_NEWNET)
#define SHIM_CLONE_NEWNET	CLONE_NEWNET
#else
#define SHIM_CLONE_NEWNET	(0)
#endif

#if defined(CLONE_NEWPID)
#define SHIM_CLONE_NEWPID	CLONE_NEWPID
#else
#define SHIM_CLONE_NEWPID	(0)
#endif

#if defined(CLONE_NEWIPC)
#define SHIM_CLONE_NEWIPC	CLONE_NEWIPC
#else
#define SHIM_CLONE_NEWIPC	(0)
#endif

#define SHIM_CLONE_FLAGS		\
	(SHIM_CLONE_NEWCGROUP	|	\
	 SHIM_CLONE_NEWNS	|	\
	 SHIM_CLONE_NEWNET	|	\
	 SHIM_CLONE_NEWPID	|	\
	 SHIM_CLONE_NEWIPC)

typedef struct stress_zombie {
	struct stress_zombie *next;
	pid_t	pid;
#if defined(HAVE_LINUX_CLONE)
	char stack[ZOMBIE_STACK_SIZE];
#endif
} stress_zombie_t;

typedef struct {
	stress_zombie_t *head;	/* Head of zombie procs list */
	stress_zombie_t *tail;	/* Tail of zombie procs list */
	stress_zombie_t *free;	/* List of free'd zombies */
	uint32_t length;	/* Length of list */
} stress_zombie_list_t;

typedef struct {
	stress_args_t *args;
	char path[PATH_MAX];
} stress_zombie_context_t;

static stress_zombie_list_t zombies;
static bool zombie_clone = false;

static const stress_help_t help[] = {
	{ NULL,	"zombie N",         "start N workers that rapidly create and reap zombies" },
	{ NULL, "zombie-clone",	    "using clone instead of fork, exercise clone and network cleanup" },
	{ NULL,	"zombie-max N",     "set upper limit of N zombies per worker" },
	{ NULL,	"zombie-ops N",     "stop after N bogo zombie fork operations" },
	{ NULL,	NULL,                NULL }
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
	stress_zombie_t *new_item;

	if (zombies.free) {
		/* Pop an old one off the free list */
		new_item = zombies.free;
		zombies.free = new_item->next;
		new_item->next = NULL;
	} else {
		new_item = (stress_zombie_t *)calloc(1, sizeof(*new_item));
		if (!new_item)
			return NULL;
	}

	if (zombies.head)
		zombies.tail->next = new_item;
	else
		zombies.head = new_item;

	zombies.tail = new_item;
	zombies.length++;

	return new_item;
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

static inline ALWAYS_INLINE int stress_zombie_child(stress_args_t *args)
{
	stress_set_make_it_fail();
	stress_set_proc_state(args->name, STRESS_STATE_ZOMBIE);

	return 0;
}

#if defined(HAVE_LINUX_CLONE)
static int stress_zombie_clone(void *voidctxt)
{
	stress_zombie_context_t *context = (stress_zombie_context_t *)voidctxt;
	stress_zombie_child(context->args);

	return 0;
}

static int stress_zombie_clone_cap_sys_admin(void *voidctxt)
{
	stress_zombie_context_t *context = (stress_zombie_context_t *)voidctxt;
	stress_zombie_child(context->args);

#if defined(CLONE_NEWNET) &&	\
    defined(AF_INET) &&		\
    defined(SOCK_STREAM)
	/* intentional socket leak, should be cleaned up on exit */
	VOID_RET(int, socket(AF_INET, SOCK_STREAM, 0));
#endif
	return 0;
}
#endif

/*
 *  stress_zombie()
 *	stress by zombieing and exiting
 */
static int stress_zombie(stress_args_t *args)
{
	uint32_t max_zombies = 0;
	uint32_t zombie_max = DEFAULT_ZOMBIES;
#if defined(HAVE_LINUX_CLONE)
	stress_zombie_context_t context;
	int ret;
	int clone_flags = SHIM_CLONE_NEWIPC;
	int (*clone_func)(void *) = stress_zombie_clone_cap_sys_admin;
#endif
	if (!stress_get_setting("zombie-max", &zombie_max)) {
		if (g_opt_flags & OPT_FLAGS_MAXIMIZE)
			zombie_max = MAX_ZOMBIES;
		if (g_opt_flags & OPT_FLAGS_MINIMIZE)
			zombie_max = MIN_ZOMBIES;
	}
	(void)stress_get_setting("zombie-clone", &zombie_clone);
	if (zombie_clone) {
#if !defined(HAVE_LINUX_CLONE)
		if (stress_instance_zero(args)) {
			pr_inf("%s: --zombie-clone selected but clone is "
				"not available, disabling option\n", args->name);
			zombie_clone = false;
		}
#else
		if (!stress_capabilities_check(SHIM_CAP_SYS_ADMIN)) {
			pr_inf("%s: --zombie-clone selected without CAP_SYS_ADMIN "
				"rights, minimal clone flags being used\n", args->name);
			clone_flags = 0;
			clone_func = stress_zombie_clone;
		}
#endif
	}

#if defined(HAVE_LINUX_CLONE)
	context.args = args;
	(void)stress_temp_dir(context.path, sizeof(context.path), args->name, getpid(), args->instance);
        ret = mkdir(context.path, S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP);
        if (ret < 0) {
                (void)shim_rmdir(context.path);
		*context.path = '\0';
        }
#endif

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

#if defined(HAVE_LINUX_CLONE)
			if (zombie_clone) {
				void *stack =  stress_stack_top(zombie->stack, ZOMBIE_STACK_SIZE);

				zombie->pid = clone(clone_func, stack, clone_flags, (void *)&context);
				if (zombie->pid < 0) {
					if (errno == EPERM) {
						pr_inf("%s: need CAP_SYS_ADMIN, aborting\n", args->name);
						break;
					}
				}
			}
#else
			zombie->pid = fork();
			if (zombie->pid == 0) {
				stress_zombie_child(args);
				_exit(0);
			}
#endif

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

#if defined(HAVE_LINUX_CLONE)
	if (*context.path)
                (void)shim_rmdir(context.path);
#endif

	return EXIT_SUCCESS;
}

static const stress_opt_t opts[] = {
	{ OPT_zombie_max,   "zombie-max",   TYPE_ID_INT32, MIN_ZOMBIES, MAX_ZOMBIES, NULL },
	{ OPT_zombie_clone, "zombie-clone", TYPE_ID_BOOL, 0, 1, NULL },
	END_OPT,
};

const stressor_info_t stress_zombie_info = {
	.stressor = stress_zombie,
	.classifier = CLASS_SCHEDULER | CLASS_OS,
	.opts = opts,
	.verify = VERIFY_OPTIONAL,
	.help = help
};
