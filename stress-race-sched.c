/*
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
#include "core-affinity.h"
#include "core-builtin.h"
#include "core-out-of-memory.h"

#include <math.h>
#include <sched.h>

#if defined(HAVE_SCHED_SETAFFINITY) &&					     \
    (defined(_POSIX_PRIORITY_SCHEDULING) || defined(__linux__)) &&	     \
    (defined(SCHED_OTHER) || defined(SCHED_BATCH) || defined(SCHED_IDLE)) && \
    !defined(__OpenBSD__) &&						     \
    !defined(__minix__) &&						     \
    !defined(__APPLE__)
#define HAVE_SCHEDULING
#endif

#define DEFAULT_CHILDREN		(8)

#define RACE_SCHED_METHOD_ALL		(0)
#define RACE_SCHED_METHOD_NEXT		(1)
#define RACE_SCHED_METHOD_PREV		(2)
#define RACE_SCHED_METHOD_RAND		(3)
#define RACE_SCHED_METHOD_RANDINC	(4)
#define RACE_SCHED_METHOD_SYNCNEXT	(5)
#define RACE_SCHED_METHOD_SYNCPREV	(6)

typedef struct {
	const char *name;
	const int   method;
} stress_race_sched_method_t;

typedef struct stress_race_sched {
	struct stress_race_sched *next;
	pid_t	pid;
	uint32_t cpu_idx;
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
	{ NULL, "race-sched-method M",	"method M: all, rand, next, prev, yoyo, randinc" },
	{ NULL,	NULL,			NULL }
};

static const stress_race_sched_method_t stress_race_sched_methods[] = {
	{ "all",	RACE_SCHED_METHOD_ALL },
	{ "next",	RACE_SCHED_METHOD_NEXT },
	{ "prev",	RACE_SCHED_METHOD_PREV },
	{ "rand",	RACE_SCHED_METHOD_RAND },
	{ "randinc",	RACE_SCHED_METHOD_RANDINC },
	{ "syncnext",	RACE_SCHED_METHOD_SYNCNEXT },
	{ "syncprev",	RACE_SCHED_METHOD_SYNCPREV },
};

static const char *stress_race_sched_method(const size_t i)
{
	return (i < SIZEOF_ARRAY(stress_race_sched_methods)) ? stress_race_sched_methods[i].name : NULL;
}

static const stress_opt_t opts[] = {
	{ OPT_race_sched_method, "race-sched-method", TYPE_ID_SIZE_T_METHOD, 0, 0, stress_race_sched_method },
	END_OPT,
};

#if defined(HAVE_SCHEDULING) &&		\
    defined(HAVE_SCHED_SETSCHEDULER)
static stress_race_sched_list_t children;

static uint32_t n_cpus;
static uint32_t *cpus;

/*
 *  "Normal" non-realtime scheduling policies
 */
static const int normal_policies[] = {
#if defined(SCHED_OTHER)
		SCHED_OTHER,
#endif
#if defined(SCHED_BATCH)
		SCHED_BATCH,
#endif
#if defined(SCHED_EXT)
		SCHED_EXT,
#endif
#if defined(SCHED_IDLE)
		SCHED_IDLE,
#endif
};

static uint32_t stress_call_race_sched_method_idx(const uint32_t cpu_idx, size_t method_index)
{
	static size_t method_all_index = 1;
	uint32_t new_cpu_idx = cpu_idx;

again:
	switch (stress_race_sched_methods[method_index].method) {
	default:
	case RACE_SCHED_METHOD_ALL:
		method_index = method_all_index;
		method_all_index++;
		if (UNLIKELY(method_all_index >= SIZEOF_ARRAY(stress_race_sched_methods)))
			method_all_index = 1;
		goto again;
	case RACE_SCHED_METHOD_NEXT:
		if (n_cpus > 0) {
			new_cpu_idx++;
			if (UNLIKELY(new_cpu_idx >= n_cpus))
				new_cpu_idx = 0;
		}
		break;
	case RACE_SCHED_METHOD_PREV:
		if (n_cpus > 0) {
			if (cpu_idx == 0)
				new_cpu_idx = n_cpus - 1;
			else
				new_cpu_idx = cpu_idx - 1;
		}
		break;
	case RACE_SCHED_METHOD_RAND:
		if (n_cpus > 0)
			new_cpu_idx = (int)stress_mwc32modn((uint32_t)n_cpus);
		break;
	case RACE_SCHED_METHOD_RANDINC:
		if (n_cpus > 0) {
			new_cpu_idx += (int)(stress_mwc8modn((uint32_t)n_cpus) & 0x3) + 1;
			new_cpu_idx %= n_cpus;
		}
		break;
	case RACE_SCHED_METHOD_SYNCNEXT:
		if (n_cpus > 0) {
			/* Move every second */
			new_cpu_idx = (uint32_t)rint(stress_time_now()) % n_cpus;
		}
		break;
	case RACE_SCHED_METHOD_SYNCPREV:
		if (n_cpus > 0) {
			/* Move every second */
			new_cpu_idx = (~(uint32_t)rint(stress_time_now())) % n_cpus;
		}
		break;
	}
	return new_cpu_idx;
}

static int stress_race_sched_setaffinity(
	stress_args_t *args,
	const pid_t pid,
	const int cpu_idx)
{
	cpu_set_t cpu_set;
	int ret;

	if (LIKELY(n_cpus > 0)) {
		CPU_ZERO(&cpu_set);
		CPU_SET(cpus[cpu_idx], &cpu_set);
		ret = sched_setaffinity(pid, sizeof(cpu_set), &cpu_set);
		if (LIKELY(ret == 0)) {
			CPU_ZERO(&cpu_set);
			ret = sched_getaffinity(pid, sizeof(cpu_set), &cpu_set);
			if (UNLIKELY((ret < 0) && (errno != ESRCH))) {
				pr_fail("%s: sched_getaffinity failed on PID %" PRIdMAX ", errno=%d (%s)\n",
					args->name, (intmax_t)pid, errno, strerror(errno));
				return ret;
			}
		}
	}
	return 0;
}

static int stress_race_sched_setscheduler(
	stress_args_t *args,
	const pid_t pid)
{
	struct sched_param param;
	const uint32_t i = stress_mwc8modn((uint8_t)SIZEOF_ARRAY(normal_policies));
	int ret;

	(void)shim_memset(&param, 0, sizeof(param));
	param.sched_priority = 0;
	ret = sched_setscheduler(pid, normal_policies[i], &param);
	if (LIKELY(ret == 0)) {
		ret = sched_getscheduler(pid);
		if (UNLIKELY((ret < 0) && (errno != ESRCH))) {
			pr_fail("%s: sched_getscheduler failed on PID %" PRIdMAX ", errno=%d (%s)\n",
				args->name, (intmax_t)pid, errno, strerror(errno));
			return ret;
		}
	}
	return 0;
}

static int stress_race_sched_exercise(
	stress_args_t *args,
	const size_t method_index)
{
	stress_race_sched_child_t *child;
	int i, rc = 0;

	for (i = 0; LIKELY(stress_continue_flag() && (i < 20)); i++)  {
		for (child = children.head; child; child = child->next) {
			if (stress_mwc1()) {
				const uint32_t cpu_idx = stress_call_race_sched_method_idx(child->cpu_idx, method_index);

				child->cpu_idx = cpu_idx;
				if (UNLIKELY(stress_race_sched_setaffinity(args, child->pid, cpu_idx) < 0))
					rc = -1;
				if (UNLIKELY(stress_race_sched_setscheduler(args, child->pid) < 0))
					rc = -1;
			}
		}
	}
	return rc;
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
		new = (stress_race_sched_child_t *)calloc(1, sizeof(*new));
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
		pid_t ret;
		int status;
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

static int stress_race_sched_child(stress_args_t *args, void *context)
{
	/* Child */
	uint32_t max_forks = 0;
	uint32_t children_max = DEFAULT_CHILDREN;
	int rc = EXIT_SUCCESS;
	uint32_t cpu_idx = 0;
	size_t method_index = 0;
	const pid_t mypid = getpid();
	n_cpus = stress_get_usable_cpus(&cpus, true);

	(void)stress_get_setting("race-sched-method", &method_index);

	(void)context;

	do {
		const bool low_mem_reap = ((g_opt_flags & OPT_FLAGS_OOM_AVOID) &&
					   stress_low_memory((size_t)(1 * MB)));
		const uint8_t rnd = stress_mwc8();

		cpu_idx = stress_call_race_sched_method_idx(cpu_idx, method_index);
		if (UNLIKELY(stress_race_sched_setaffinity(args, mypid, cpu_idx) < 0)) {
			rc = EXIT_FAILURE;
			break;
		}

		if (!low_mem_reap && (children.length < children_max)) {
			stress_race_sched_child_t *child_info;

			child_info = stress_race_sched_new();
			if (!child_info)
				break;

			child_info->cpu_idx = cpu_idx;
			child_info->pid = fork();
			if (child_info->pid < 0) {
				/*
				 * Reached max forks or error
				 * (e.g. EPERM)? .. then reap
				 */
				if (UNLIKELY(stress_race_sched_exercise(args, method_index) < 0)) {
					rc = EXIT_FAILURE;
					break;
				}
				stress_race_sched_head_remove(WNOHANG);
				continue;
			} else if (child_info->pid == 0) {
				/* child */
				const pid_t child_pid = getpid();

				stress_set_proc_state(args->name, STRESS_STATE_RUN);

				if (rnd & 0x01)
					(void)shim_sched_yield();
				if (rnd & 0x02) {
					if (stress_race_sched_setaffinity(args, child_pid, cpu_idx) < 0) {
						rc = EXIT_FAILURE;
						break;
					}
				}
				if (rnd & 0x04) {
					if (stress_race_sched_setscheduler(args, child_pid) < 0) {
						rc = EXIT_FAILURE;
						break;
					}
				}
				if (rnd & 0x08) {
					if (stress_race_sched_exercise(args, method_index) < 0) {
						rc = EXIT_FAILURE;
						break;
					}
				}
				if (rnd & 0x10)
					(void)shim_sched_yield();
				_exit(0);
			} else {
				/* parent */
				if (rnd & 0x20)
					(void)shim_sched_yield();
				if (rnd & 0x40) {
					if (stress_race_sched_exercise(args, method_index) < 0) {
						rc = EXIT_FAILURE;
						break;
					}
				}
				if (rnd & 0x80)
					(void)shim_sched_yield();
			}

			if (max_forks < children.length)
				max_forks = children.length;
			stress_bogo_inc(args);
		} else {
			if (rnd & 0x01) {
				if (stress_race_sched_exercise(args, method_index) < 0) {
					rc = EXIT_FAILURE;
					break;
				}
			}
			stress_race_sched_head_remove(WNOHANG);
			if (rnd & 0x02) {
				if (stress_race_sched_exercise(args, method_index) < 0) {
					rc = EXIT_FAILURE;
					break;
				}
			}
		}
	} while (stress_continue(args));

	/* And reap */
	while (children.head) {
		stress_race_sched_head_remove(0);
	}
	/* And free */
	stress_race_sched_free();

	return rc;
}

/*
 *  stress_race_sched()
 *	stress by cloning and exiting
 */
static int stress_race_sched(stress_args_t *args)
{
	int rc;

	stress_set_oom_adjustment(args, false);

	stress_set_proc_state(args->name, STRESS_STATE_SYNC_WAIT);
	stress_sync_start_wait(args);
	stress_set_proc_state(args->name, STRESS_STATE_RUN);

	rc = stress_oomable_child(args, NULL, stress_race_sched_child, STRESS_OOMABLE_DROP_CAP);
	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);

	return rc;
}

const stressor_info_t stress_race_sched_info = {
	.stressor = stress_race_sched,
	.classifier = CLASS_SCHEDULER | CLASS_OS,
	.opts = opts,
	.verify = VERIFY_ALWAYS,
	.help = help
};

#else

const stressor_info_t stress_race_sched_info = {
        .stressor = stress_unimplemented,
	.classifier = CLASS_SCHEDULER | CLASS_OS,
	.opts = opts,
	.help = help,
	.verify = VERIFY_ALWAYS,
	.unimplemented_reason = "built without Linux scheduling or sched_setscheduler() system call"
};
#endif
