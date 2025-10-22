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
#include "core-attribute.h"
#include "core-builtin.h"
#include "core-lock.h"
#include "core-mincore.h"
#include "core-mmap.h"
#include "core-out-of-memory.h"

#include <sched.h>

#if defined(HAVE_MODIFY_LDT)
#include <asm/ldt.h>
#endif

#define MIN_CLONES		(1)
#define MAX_CLONES		(1000000)
#define DEFAULT_CLONES		(8192)

#define CLONE_STACK_SIZE	(8*1024)

#if defined(HAVE_CLONE)

typedef struct {
	stress_metrics_t metrics;
	volatile bool clone_invoked_ok;		/* racy bool */
	volatile bool clone_waited_ok;		/* racy bool */
} stress_clone_shared_t;

typedef struct stress_clone_args {
	stress_args_t *args;
	stress_clone_shared_t *shared;
} stress_clone_args_t;

typedef struct clone {
	struct clone *next;
	pid_t	pid;
	uint64_t stack[CLONE_STACK_SIZE / sizeof(uint64_t)];
} stress_clone_t;

typedef struct {
	stress_clone_t *head;	/* Head of clone procs list */
	stress_clone_t *tail;	/* Tail of clone procs list */
	stress_clone_t *free;	/* List of free'd clones */
	uint32_t length;	/* Length of list */
} stress_clone_list_t;

#endif

static const stress_help_t help[] = {
	{ NULL,	"clone N",	"start N workers that rapidly create and reap clones" },
	{ NULL,	"clone-max N",	"set upper limit of N clones per worker" },
	{ NULL,	"clone-ops N",	"stop after N bogo clone operations" },
	{ NULL,	NULL,		NULL }
};

#if defined(HAVE_CLONE)

static stress_clone_list_t clones;

static size_t flag_count;
static int *flag_perms;
static const unsigned int all_flags =
#if defined(CLONE_FS)
	CLONE_FS |
#endif
#if defined(CLONE_PIDFD)
	CLONE_PIDFD |
#endif
#if defined(CLONE_PTRACE)
	CLONE_PTRACE |
#endif
#if defined(CLONE_VFORK)
	CLONE_VFORK |
#endif
#if defined(CLONE_PARENT)
	CLONE_PARENT |
#endif
#if defined(CLONE_SYSVSEM)
	CLONE_SYSVSEM |
#endif
#if defined(CLONE_DETACHED)
	CLONE_DETACHED |
#endif
#if defined(CLONE_UNTRACED)
	CLONE_UNTRACED |
#endif
#if defined(CLONE_IO)
	CLONE_IO |
#endif
#if defined(CLONE_FILES)
	CLONE_FILES |
#endif
#if defined(CLONE_SYSVSEM)
	CLONE_SYSVSEM |
#endif
	0;


/*
 *  A random selection of clone flags that are worth exercising
 */
static const uint64_t flags[] = {
	0,
/*
 * Avoid CLONE_VM for now as child may memory clobber parent
#if defined(CLONE_SIGHAND) && 	\
    defined(CLONE_VM)
	CLONE_SIGHAND | CLONE_VM,
#endif
 */
#if defined(CLONE_FS)
	CLONE_FS,
#endif
#if defined(CLONE_FILES)
	CLONE_FILES,
#endif
#if defined(CLONE_SIGHAND)
	CLONE_SIGHAND,
#endif
#if defined(CLONE_PIDFD)
	CLONE_PIDFD,
#endif
/*
#if defined(CLONE_PTRACE)
	CLONE_PTRACE,
#endif
*/
/*
#if defined(CLONE_VFORK)
	CLONE_VFORK,
#endif
*/
#if defined(CLONE_PARENT)
	CLONE_PARENT,
#endif
#if defined(CLONE_THREAD)
	CLONE_THREAD,
#endif
#if defined(CLONE_NEWNS)
	CLONE_NEWNS,
#endif
#if defined(CLONE_SYSVSEM)
	CLONE_SYSVSEM,
#endif
/*
#if defined(CLONE_SETTLS)
	CLONE_SETTLS,
#endif
*/
#if defined(CLONE_PARENT_SETTID)
	CLONE_PARENT_SETTID,
#endif
#if defined(CLONE_CHILD_CLEARTID)
	CLONE_CHILD_CLEARTID,
#endif
#if defined(CLONE_DETACHED)
	CLONE_DETACHED,
#endif
#if defined(CLONE_UNTRACED)
	CLONE_UNTRACED,
#endif
#if defined(CLONE_CHILD_SETTID)
	CLONE_CHILD_SETTID,
#endif
#if defined(CLONE_NEWCGROUP)
	CLONE_NEWCGROUP,
#endif
#if defined(CLONE_NEWUTS)
	CLONE_NEWUTS,
#endif
#if defined(CLONE_NEWIPC)
	CLONE_NEWIPC,
#endif
#if defined(CLONE_NEWUSER)
	CLONE_NEWUSER,
#endif
#if defined(CLONE_NEWPID)
	CLONE_NEWPID,
#endif
#if defined(CLONE_NEWNET)
	CLONE_NEWNET,
#endif
#if defined(CLONE_IO)
	CLONE_IO,
#endif
#if defined(CLONE_CLEAR_SIGHAND)
	CLONE_CLEAR_SIGHAND,
#endif
#if defined(CLONE_INTO_CGROUP)
	CLONE_INTO_CGROUP,
#endif
#if defined(CLONE_NEWTIME)
	CLONE_NEWTIME,
#endif
};

static const uint64_t unshare_flags[] = {
#if defined(CLONE_FILES)
	CLONE_FILES,
#endif
#if defined(CLONE_FS)
	CLONE_FS,
#endif
#if defined(CLONE_NEWIPC)
	CLONE_NEWIPC,
#endif
#if defined(CLONE_NEWNET)
	CLONE_NEWNET,
#endif
#if defined(CLONE_NEWNS)
	CLONE_NEWNS,
#endif
#if defined(CLONE_NEWUTS)
	CLONE_NEWUTS,
#endif
#if defined(CLONE_SYSVSEM)
	CLONE_SYSVSEM,
#endif
#if defined(CLONE_NEWCGROUP)
	CLONE_NEWCGROUP,
#endif
};
#endif

static const stress_opt_t opts[] = {
	{ OPT_clone_max, "clone-max", TYPE_ID_UINT32, MIN_CLONES, MAX_CLONES, NULL },
        END_OPT,
};

#if defined(HAVE_CLONE)

/*
 *  stress_clone_shim_exit()
 *	perform _exit(), try and use syscall first to
 *	avoid any shared library late binding of _exit(),
 *	if the direct syscall fails do _exit() call.
 */
static inline ALWAYS_INLINE NORETURN void stress_clone_shim_exit(int status)
{
#if defined(__NR_exit) && \
    defined(HAVE_SYSCALL)
        (void)syscall(__NR_exit, status);
	/* in case __NR_exit fails, do _exit anyhow */
#endif
	_exit(status);
}

/*
 *  stress_clone_force_bind()
 *	the child process performs various system calls via the libc
 *	shared library and this involves doing late binding on these
 *	libc functions. Since the child process has to do this many
 *	times it's useful to avoid the late binding overhead by forcing
 *	binding by calling the functions before the child uses them.
 *
 *	This could be avoided by compiling with late binding disabled
 *	via LD_FLAGS -znow however this can break on some distros due
 *	to symbol resolving ordering, so we do it using this ugly way.
 */
static void clone_stress_force_bind(void)
{
#if defined(HAVE_SETNS)
	(void)setns(-1, 0);
#endif
	(void)shim_unshare(0);
}

static inline CONST uint64_t uint64_ptr(const void *ptr)
{
	return (uint64_t)(uintptr_t)ptr;
}

/*
 *  stress_clone_new()
 *	allocate a new clone, add to end of list
 */
static stress_clone_t *stress_clone_new(void)
{
	stress_clone_t *new;

	if (clones.free) {
		/* Pop an old one off the free list */
		new = clones.free;
		clones.free = new->next;
		new->next = NULL;
	} else {
		new = stress_mmap_populate(NULL,
			sizeof(*new), PROT_READ | PROT_WRITE,
			MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
		if (new == MAP_FAILED)
			return NULL;
		stress_set_vma_anon_name(new, sizeof(*new), "clone-descriptor");
	}

	if (clones.head)
		clones.tail->next = new;
	else
		clones.head = new;

	clones.tail = new;
	clones.length++;

	return new;
}

/*
 *  stress_clone_head_remove
 *	reap a clone and remove a clone from head of list, put it onto
 *	the free clone list
 */
static void stress_clone_head_remove(stress_clone_shared_t *shared)
{
	if (clones.head) {
		int status;
		stress_clone_t *head = clones.head;

		if (clones.head->pid != -1) {
			if (waitpid(clones.head->pid, &status, (int)__WCLONE) > 0) {
				shared->clone_waited_ok = true;
			}
		}
		if (clones.tail == clones.head) {
			clones.tail = NULL;
			clones.head = NULL;
		} else {
			clones.head = head->next;
		}

		/* Shove it on the free list */
		head->next = clones.free;
		clones.free = head;

		clones.length--;
	}
}

/*
 *  stress_clone_free()
 *	free the clones off the clone free lists
 */
static void stress_clone_free(void)
{
	while (clones.head) {
		stress_clone_t *next = clones.head->next;

		(void)munmap((void *)clones.head, sizeof(*(clones.head)));
		clones.head = next;
	}
	while (clones.free) {
		stress_clone_t *next = clones.free->next;

		(void)munmap((void *)clones.free, sizeof(*(clones.free)));
		clones.free = next;
	}
}

/*
 *  clone_func()
 *	clone thread just returns immediately
 */
static int clone_func(void *arg)
{
	size_t i;
	stress_clone_args_t *clone_arg = arg;
	stress_clone_shared_t *shared = clone_arg->shared;
	stress_metrics_t *metrics = &shared->metrics;

	shared->clone_invoked_ok = true;	/* Racy, but setting to true is OK */

	if (metrics->lock && (stress_lock_acquire(metrics->lock) == 0)) {
		double duration = stress_time_now() - metrics->t_start;
		/* On WSL we get can get -ve durations, so check for this! */
		if (duration >= 0.0) {
			metrics->duration += stress_time_now() - metrics->t_start;
			metrics->count += 1.0;
		}
		stress_lock_release(metrics->lock);
	}

	if ((g_opt_flags & OPT_FLAGS_OOM_AVOID) && stress_low_memory((size_t)(1 * MB))) {
		return 0;
	}

	stress_set_oom_adjustment(clone_arg->args, true);
#if defined(HAVE_SETNS)
	{
		int fd;

		fd = open("/proc/self/ns/uts", O_RDONLY);
		if (fd >= 0) {
			/* Exercise invalid setns nstype, EINVAL */
			(void)setns(fd, ~0);

			/* Exercise invalid setns fd, EBADF */
			(void)setns(~0, 0);

			/*
			 *  Capabilities have been dropped
			 *  so this will always fail, but
			 *  lets exercise it anyhow.
			 */
			(void)setns(fd, 0);
			(void)close(fd);
		}
	}
#else
	UNEXPECTED
#endif

#if defined(HAVE_MODIFY_LDT) &&	\
    defined(__NR_modify_ldt)
	{
		struct user_desc ud;

		(void)shim_memset(&ud, 0, sizeof(ud));
		if (shim_modify_ldt(0, &ud, sizeof(ud)) == 0) {
			(void)shim_modify_ldt(1, &ud, sizeof(ud));
			/* Exercise invalid size */
			(void)shim_modify_ldt(1, &ud, 1);
			/* Exercise invalid entries */
			ud.entry_number = ~0U;
			(void)shim_modify_ldt(1, &ud, sizeof(ud));
		}

		(void)shim_memset(&ud, 0, sizeof(ud));
		if (shim_modify_ldt(0, &ud, sizeof(ud)) == 0) {
			/* Old mode style */
			(void)shim_modify_ldt(0x11, &ud, sizeof(ud));
		}
		(void)shim_memset(&ud, 0, sizeof(ud));
		(void)shim_modify_ldt(2, &ud, sizeof(ud));

		/* Exercise invalid command */
		(void)shim_modify_ldt(0xff, &ud, sizeof(ud));

		/* Exercise invalid ldt size */
		(void)shim_memset(&ud, 0, sizeof(ud));
		(void)shim_modify_ldt(0, &ud, 0);
	}
#endif
	for (i = 0; i < SIZEOF_ARRAY(unshare_flags); i++) {
		(void)shim_unshare((int)unshare_flags[i]);
	}

	return 0;
}

static int stress_clone_child(stress_args_t *args, void *context)
{
	/* Child */
	uint32_t max_clones = 0;
	uint32_t clone_max = DEFAULT_CLONES;
	bool use_clone3 = true;
	const size_t mmap_size = args->page_size * 32768;
	void *ptr;
	stress_clone_shared_t *shared = (stress_clone_shared_t *)context;

	if (!stress_get_setting("clone-max", &clone_max)) {
		if (g_opt_flags & OPT_FLAGS_MAXIMIZE)
			clone_max = MAX_CLONES;
		if (g_opt_flags & OPT_FLAGS_MINIMIZE)
			clone_max = MIN_CLONES;
	}

	/*
	 * Make child larger than parent to make it more of
	 * a candidate for a OOMable process
	 */
	ptr = stress_mmap_populate(NULL, mmap_size, PROT_READ | PROT_WRITE,
			MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
	if (ptr != MAP_FAILED) {
		stress_set_vma_anon_name(ptr, mmap_size, "oom-allocation");
		(void)stress_mincore_touch_pages(ptr, mmap_size);
	}

	clone_stress_force_bind();

	do {
		const bool low_mem_reap = ((g_opt_flags & OPT_FLAGS_OOM_AVOID) &&
					   stress_low_memory((size_t)(1 * MB)));

		if (!low_mem_reap && (clones.length < clone_max)) {
			static size_t idx;
			stress_clone_t *clone_info;
			stress_clone_args_t clone_arg = { args, shared };
			const uint32_t rnd = stress_mwc32();
			uint64_t flag;
			const bool try_clone3 = rnd >> 31;
			pid_t child_tid = -1, parent_tid = -1;
			clone_info = stress_clone_new();
			if (!clone_info)
				break;

			if ((rnd & 0x80000000UL) || (flag_count == 0) || (!flag_perms)) {
				flag = flags[rnd % SIZEOF_ARRAY(flags)];	/* cppcheck-suppress moduloofone */
			} else {
				flag = (unsigned int)flag_perms[idx];
				idx++;
				if (idx >= flag_count)
					idx = 0;
			}

			if (use_clone3 && try_clone3) {
				struct shim_clone_args cl_args;
				int pidfd = -1;

				(void)shim_memset(&cl_args, 0, sizeof(cl_args));

				cl_args.flags = flag;
				cl_args.pidfd = uint64_ptr(&pidfd);
				cl_args.child_tid = uint64_ptr(&child_tid);
				cl_args.parent_tid = uint64_ptr(&parent_tid);
				cl_args.exit_signal = SIGCHLD;
				cl_args.stack = uint64_ptr(NULL);
				cl_args.stack_size = 0;
				cl_args.tls = uint64_ptr(NULL);

				shared->metrics.t_start = stress_time_now();
				clone_info->pid = shim_clone3(&cl_args, sizeof(cl_args));
				if (clone_info->pid < 0) {
					/* Not available, don't use it again */
					if (errno == ENOSYS)
						use_clone3 = false;
				} else if (clone_info->pid == 0) {
					/* child */
					stress_clone_shim_exit(clone_func(&clone_arg));
				}
			} else {
				char *stack_top = (char *)stress_get_stack_top((char *)clone_info->stack, CLONE_STACK_SIZE);
#if defined(__FreeBSD_kernel__) || 	\
    defined(__NetBSD__)
				shared->metrics.t_start = stress_time_now();
				clone_info->pid = clone(clone_func,
					stress_align_stack(stack_top), (int)flag, &clone_arg);
#else
				shared->metrics.t_start = stress_time_now();
				clone_info->pid = clone(clone_func,
					stress_align_stack(stack_top), (int)flag, &clone_arg, &parent_tid,
					NULL, &child_tid);
#endif
			}
			if (clone_info->pid == -1) {
				/*
				 * Reached max forks or error
				 * (e.g. EPERM)? .. then reap
				 */
				stress_clone_head_remove(shared);
				continue;
			}
			if (max_clones < clones.length)
				max_clones = clones.length;
			stress_bogo_inc(args);
		} else {
			stress_clone_head_remove(shared);
		}
	} while (stress_continue(args));

	if (ptr != MAP_FAILED)
		(void)munmap(ptr, mmap_size);
	/* And reap */
	while (clones.head) {
		stress_clone_head_remove(shared);
	}
	/* And free */
	stress_clone_free();

	return EXIT_SUCCESS;
}

/*
 *  stress_clone()
 *	stress by cloning and exiting
 */
static int stress_clone(stress_args_t *args)
{
	int rc;
	stress_clone_shared_t *shared;
	double average;

	shared = mmap(NULL, sizeof(*shared), PROT_READ | PROT_WRITE,
			MAP_SHARED | MAP_ANONYMOUS, -1, 0);
	if (shared == MAP_FAILED) {
		pr_inf_skip("%s: failed to memory map %zu bytes%s, skipping stressor\n",
			args->name, sizeof(*shared), stress_get_memfree_str());
		return EXIT_NO_RESOURCE;
	}
	stress_set_vma_anon_name(shared, sizeof(*shared), "clone-state");
	stress_zero_metrics(&shared->metrics, 1);
	shared->metrics.lock = stress_lock_create("metrics");

	flag_count = stress_flag_permutation((int)all_flags, &flag_perms);

	stress_set_oom_adjustment(args, false);

	stress_set_proc_state(args->name, STRESS_STATE_SYNC_WAIT);
	stress_sync_start_wait(args);
	stress_set_proc_state(args->name, STRESS_STATE_RUN);

	rc = stress_oomable_child(args, &shared->metrics, stress_clone_child, STRESS_OOMABLE_DROP_CAP);
	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);

	if (flag_perms)
		free(flag_perms);

	/*
	 *  Check if we got a clone termination (via a wait) but they did not
	 *  successfully get to the invocation stage
	 */
	if ((shared->clone_waited_ok) && (!shared->clone_invoked_ok)) {
		pr_fail("%s: no clone processes got fully invoked correctly "
			"before they terminated\n", args->name);
		rc = EXIT_FAILURE;
	}

	average = (shared->metrics.count > 0.0) ? shared->metrics.duration / shared->metrics.count : 0.0;
	stress_metrics_set(args, 0, "microsecs per clone",
		average * 1000000, STRESS_METRIC_HARMONIC_MEAN);

	(void)munmap((void *)shared, sizeof(*shared));

	return rc;
}

const stressor_info_t stress_clone_info = {
	.stressor = stress_clone,
	.classifier = CLASS_SCHEDULER | CLASS_OS,
	.opts = opts,
	.verify = VERIFY_ALWAYS,
	.help = help
};
#else
const stressor_info_t stress_clone_info = {
	.stressor = stress_unimplemented,
	.classifier = CLASS_SCHEDULER | CLASS_OS,
	.opts = opts,
	.verify = VERIFY_ALWAYS,
	.help = help,
	.unimplemented_reason = "built without clone() system call"
};
#endif
