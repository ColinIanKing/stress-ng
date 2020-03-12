/*
 * Copyright (C) 2013-2020 Canonical, Ltd.
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

#define CLONE_STACK_SIZE	(16*1024)

typedef struct stress_clone_args {
	const stress_args_t *args;
} stress_clone_args_t;

typedef struct clone {
	struct clone *next;
	pid_t	pid;
	char stack[CLONE_STACK_SIZE];
} stress_clone_t;

typedef struct {
	stress_clone_t *head;	/* Head of clone procs list */
	stress_clone_t *tail;	/* Tail of clone procs list */
	stress_clone_t *free;	/* List of free'd clones */
	uint32_t length;	/* Length of list */
} stress_clone_list_t;

static const stress_help_t help[] = {
	{ NULL,	"clone N",	"start N workers that rapidly create and reap clones" },
	{ NULL,	"clone-ops N",	"stop after N bogo clone operations" },
	{ NULL,	"clone-max N",	"set upper limit of N clones per worker" },
	{ NULL,	NULL,		NULL }
};

#if defined(HAVE_CLONE)

static stress_clone_list_t clones;

/*
 *  A random selection of clone flags that are worth exercising
 */
static const int flags[] = {
	0,
#if defined(CLONE_FILES)
	CLONE_FILES,
#endif
#if defined(CLONE_FS)
	CLONE_FS,
#endif
#if defined(CLONE_IO)
	CLONE_IO,
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
#if defined(CLONE_NEWUSER)
	CLONE_NEWUSER,
#endif
#if defined(CLONE_NEWUTS)
	CLONE_NEWUTS,
#endif
#if defined(CLONE_SIGHAND)
	CLONE_SIGHAND,
#endif
#if defined(CLONE_SYSVSEM)
	CLONE_SYSVSEM,
#endif
#if defined(CLONE_UNTRACED)
	CLONE_UNTRACED,
#endif
#if defined(CLONE_PIDFD)
	CLONE_PIDFD,
#endif
#if defined(CLONE_NEWCGROUP)
	CLONE_NEWCGROUP,
#endif
#if defined(CLONE_CLEAR_SIGHAND)
	CLONE_CLEAR_SIGHAND,
#endif
};

static const int unshare_flags[] = {
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

/*
 *  stress_set_clone_max()
 *	set maximum number of clones allowed
 */
static int stress_set_clone_max(const char *opt)
{
	uint32_t clone_max;

	clone_max = stress_get_uint32(opt);
	stress_check_range("clone-max", clone_max,
		MIN_ZOMBIES, MAX_ZOMBIES);
	return stress_set_setting("clone-max", TYPE_ID_UINT32, &clone_max);
}

static const stress_opt_set_func_t opt_set_funcs[] = {
	{ OPT_clone_max,	stress_set_clone_max },
	{ 0,			NULL }
};

#if defined(HAVE_CLONE)

static inline uint64_t uint64_ptr(const void *ptr)
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
		new = calloc(1, sizeof(*new));
		if (!new)
			return NULL;
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
static void stress_clone_head_remove(void)
{
	if (clones.head) {
		int status;
		stress_clone_t *head = clones.head;

		(void)shim_waitpid(clones.head->pid, &status, __WCLONE);

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

		free(clones.head);
		clones.head = next;
	}
	while (clones.free) {
		stress_clone_t *next = clones.free->next;

		free(clones.free);
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

	(void)arg;

	stress_set_oom_adjustment(clone_arg->args->name, true);
#if defined(HAVE_SETNS)
	{
		int fd;

		fd = open("/proc/self/ns/uts", O_RDONLY);
		if (fd >= 0) {
			/*
			 *  Capabilities have been dropped
			 *  so this will always fail, but
			 *  lets exercise it anyhow.
			 */
			(void)setns(fd, 0);
			(void)close(fd);
		}
	}
#endif

#if defined(HAVE_MODIFY_LDT)
	{
		struct user_desc ud;
		int ret;

		(void)memset(&ud, 0, sizeof(ud));
		ret = syscall(__NR_modify_ldt, 0, &ud, sizeof(ud));
		if (ret == 0) {
			ret = syscall(__NR_modify_ldt, 1, &ud, sizeof(ud));
			(void)ret;
		}
	}
#endif
	for (i = 0; i < SIZEOF_ARRAY(unshare_flags); i++) {
		(void)shim_unshare(unshare_flags[i]);
	}

	return 0;
}

static int stress_clone_child(const stress_args_t *args, void *context)
{
	/* Child */
	uint32_t max_clones = 0;
	uint32_t clone_max = DEFAULT_ZOMBIES;
	bool use_clone3 = true;
	const size_t mmap_size = args->page_size * 32768;
	void *ptr;
	const ssize_t stack_offset =
		stress_get_stack_direction() *
		(CLONE_STACK_SIZE - 64);
#if defined(MAP_POPULATE)
	const int mflags = MAP_ANONYMOUS | MAP_PRIVATE | MAP_POPULATE;
#else
	const int mflags = MAP_ANONYMOUS | MAP_PRIVATE;
#endif

	(void)context;

	if (!stress_get_setting("clone-max", &clone_max)) {
		if (g_opt_flags & OPT_FLAGS_MAXIMIZE)
			clone_max = MAX_ZOMBIES;
		if (g_opt_flags & OPT_FLAGS_MINIMIZE)
			clone_max = MIN_ZOMBIES;
	}

	/*
	 * Make child larger than parent to make it more of
	 * a candidate for a OOMable process
	 */
	ptr = mmap(NULL, mmap_size, PROT_READ | PROT_WRITE, mflags, -1, 0);
	if (ptr != MAP_FAILED)
		(void)stress_mincore_touch_pages(ptr, mmap_size);

	do {
		if (clones.length < clone_max) {
			stress_clone_t *clone_info;
			stress_clone_args_t clone_arg = { args };
			const uint32_t rnd = stress_mwc32();
			const int flag = flags[rnd % SIZEOF_ARRAY(flags)];
			const bool try_clone3 = rnd >> 31;

			clone_info = stress_clone_new();
			if (!clone_info)
				break;

			if (use_clone3 && try_clone3) {
				struct shim_clone_args cl_args;
				int pidfd = -1;
				pid_t child_tid = -1, parent_tid = -1;

				memset(&cl_args, 0, sizeof(cl_args));

				cl_args.flags = flag;
				cl_args.pidfd = uint64_ptr(&pidfd);
				cl_args.child_tid = uint64_ptr(&child_tid);
				cl_args.parent_tid = uint64_ptr(&parent_tid);
				cl_args.exit_signal = SIGCHLD;
				cl_args.stack = uint64_ptr(NULL);
				cl_args.stack_size = 0;
				cl_args.tls = uint64_ptr(NULL);
				clone_info->pid = sys_clone3(&cl_args, sizeof(cl_args));
				if (clone_info->pid < 0) {
					/* Not available, don't use it again */
					if (errno == ENOSYS)
						use_clone3 = false;
				} else if (clone_info->pid == 0) {
					/* child */
					_exit(clone_func(&clone_arg));
				}
			} else {
				char *stack_top = clone_info->stack + stack_offset;

				clone_info->pid = clone(clone_func,
					stress_align_stack(stack_top), flag, &clone_arg);
			}
			if (clone_info->pid == -1) {
				/*
				 * Reached max forks or error
				 * (e.g. EPERM)? .. then reap
				 */
				stress_clone_head_remove();
				continue;
			}
			if (max_clones < clones.length)
				max_clones = clones.length;
			inc_counter(args);
		} else {
			stress_clone_head_remove();
		}
	} while (keep_stressing());

	pr_inf("%s: created a maximum of %" PRIu32 " clones\n",
		args->name, max_clones);

	if (ptr != MAP_FAILED)
		(void)munmap(ptr, mmap_size);
	/* And reap */
	while (clones.head) {
		stress_clone_head_remove();
	}
	/* And free */
	stress_clone_free();

	return EXIT_SUCCESS;
}

/*
 *  stress_clone()
 *	stress by cloning and exiting
 */
static int stress_clone(const stress_args_t *args)
{
	stress_set_oom_adjustment(args->name, false);

	return stress_oomable_child(args, NULL, stress_clone_child, STRESS_OOMABLE_DROP_CAP);
}

stressor_info_t stress_clone_info = {
	.stressor = stress_clone,
	.class = CLASS_SCHEDULER | CLASS_OS,
	.opt_set_funcs = opt_set_funcs,
	.help = help
};
#else
stressor_info_t stress_clone_info = {
	.stressor = stress_not_implemented,
	.class = CLASS_SCHEDULER | CLASS_OS,
	.opt_set_funcs = opt_set_funcs,
	.help = help
};
#endif
