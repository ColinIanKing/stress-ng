/*
 * Copyright (C) 2013-2021 Canonical, Ltd.
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

#if defined(HAVE_SPAWN_H)
#include <spawn.h>
#else
UNEXPECTED
#endif

#define MIN_EXECS		(1)
#define MAX_EXECS		(16000)
#define DEFAULT_EXECS		(4096)
#define HASH_EXECS		(32003)	/* a prime larger than 2 x MAX_EXECS */

#define EXEC_METHOD_ALL		(0x00)
#define EXEC_METHOD_EXECVE	(0x01)
#define EXEC_METHOD_EXECVEAT	(0x02)

#if defined(HAVE_CLONE)
#define EXEC_FORK_METHOD_CLONE	(0x10)
#endif
#define EXEC_FORK_METHOD_FORK	(0x11)
#if defined(HAVE_VFORK)
#define EXEC_FORK_METHOD_VFORK	(0x12)
#endif
#if defined(HAVE_SPAWN_H) &&	\
    defined(HAVE_POSIX_SPAWN)
#define EXEC_FORK_METHOD_SPAWN	(0x13)
#endif

#define MAX_ARG_PAGES		(32)

#define CLONE_STACK_SIZE	(8 * 1024)

/*
 *   exec* family of args to pass
 */
typedef struct {
	const stress_args_t *args;	/* stress-ng args */
	const char *exec_prog;		/* path to program to execute */
	const char *garbage_prog;	/* path of garbage program */
	char *str;			/* huge argv and env string */
	char *argv[4];			/* executable argv[] */
	char *env[2];			/* executable env[] */
#if defined(HAVE_EXECVEAT) &&	\
    defined(O_PATH)
	int fdexec;			/* fd of program to exec */
#endif
	int exec_method;		/* exec method */
	uint8_t rnd8;			/* random value */
	bool no_pthread;		/* do not use pthread */
} stress_exec_context_t;

typedef struct stress_pid_hash {
	struct stress_pid_hash *next;	/* next entry */
	pid_t	pid;			/* child pid */
	stress_exec_context_t arg;	/* exec info context */
#if defined(HAVE_CLONE)
	void	*stack;			/* stack for clone */
#endif
} stress_pid_hash_t;

static size_t stress_pid_cache_index = 0;
static size_t stress_pid_cache_items = 0;
static stress_pid_hash_t *stress_pid_cache;
static stress_pid_hash_t *stress_pid_hash_table[HASH_EXECS];
static stress_pid_hash_t *free_list;

typedef struct {
	const char *name;
	const int method;
} stress_exec_method_t;

static const stress_exec_method_t stress_exec_methods[] = {
	{ "all",	EXEC_METHOD_ALL },
	{ "execve",	EXEC_METHOD_EXECVE },
	{ "execveat",	EXEC_METHOD_EXECVEAT },
	{ NULL,		-1 },
};

static const stress_exec_method_t stress_exec_fork_methods[] = {
#if defined(HAVE_CLONE)
	{ "clone",	EXEC_FORK_METHOD_CLONE },
#endif
	{ "fork",	EXEC_FORK_METHOD_FORK },
#if defined(HAVE_SPAWN_H) &&	\
    defined(HAVE_POSIX_SPAWN)
	{ "spawn",	EXEC_FORK_METHOD_SPAWN },
#endif
#if defined(HAVE_VFORK)
	{ "vfork",	EXEC_FORK_METHOD_VFORK },
#endif
	{ NULL,		-1 },
};

static const stress_help_t help[] = {
	{ NULL,	"exec N",		"start N workers spinning on fork() and exec()" },
	{ NULL,	"exec-fork-method M",	"select exec fork method:"
#if defined(HAVE_CLONE)
					" clone"
#endif
					" fork"
#if defined(HAVE_SPAWN_H) &&	\
    defined(HAVE_POSIX_SPAWN)
					" spawn"
#endif
#if defined(HAVE_VFORK)
					" vfork"
#endif
					"" },
	{ NULL,	"exec-max P",		"create P workers per iteration, default is 4096" },
	{ NULL,	"exec-method M",	"select exec method: all, execve, execveat" },
	{ NULL,	"exec-no-pthread",	"do not use pthread_create" },
	{ NULL,	"exec-ops N",		"stop after N exec bogo operations" },
	{ NULL,	NULL,			NULL }
};

/*
 *  stress_set_exec_max()
 *	set maximum number of forks allowed
 */
static int stress_set_exec_max(const char *opt)
{
	uint32_t exec_max;

	exec_max = stress_get_uint32(opt);
	stress_check_range("exec-max", (uint64_t)exec_max, MIN_EXECS, MAX_EXECS);
	return stress_set_setting("exec-max", TYPE_ID_INT32, &exec_max);
}

/*
 * stress_search_exec_method
 * 	search the given option in the given array and if found set the
 * 	corresponding option.
 */
static int stress_search_exec_method(
	const char *name,
	const stress_exec_method_t *methods,
	const char *opt)
{
	size_t i;

	for (i = 0; methods[i].name; i++) {
		if (!strcmp(opt, methods[i].name))
			return stress_set_setting(name, TYPE_ID_INT, &methods[i].method);
	}

	(void)fprintf(stderr, "%s must be one of:", name);
	for (i = 0; methods[i].name; i++) {
		(void)fprintf(stderr, " %s", methods[i].name);
	}
	(void)fprintf(stderr, "\n");
	return -1;
}

/*
 *  stress_set_exec_method()
 *	set exec call method
 */
static int stress_set_exec_method(const char *opt)
{
	return stress_search_exec_method("exec-method", stress_exec_methods, opt);
}

/*
 *  stress_set_exec_method()
 *	set exec call method
 */
static int stress_set_exec_fork_method(const char *opt)
{
	return stress_search_exec_method("exec-fork-method", stress_exec_fork_methods, opt);
}

/*
 *  stress_set_exec_no_pthread()
 *	set no pthread flag
 */
static int stress_set_exec_no_pthread(const char *opt)
{
	return stress_set_setting_true("exec-no-pthread", opt);
}

static const stress_opt_set_func_t opt_set_funcs[] = {
	{ OPT_exec_max,		stress_set_exec_max },
	{ OPT_exec_method,	stress_set_exec_method },
	{ OPT_exec_fork_method,	stress_set_exec_fork_method },
	{ OPT_exec_no_pthread,	stress_set_exec_no_pthread },
	{ 0,			NULL }
};

/*
 *  stress_exec_free_list_add()
 *	add sph to the free list
 */
static inline void stress_exec_free_list_add(stress_pid_hash_t *sph)
{
	sph->next = free_list;
	free_list = sph;
}

/*
 *  stress_exec_free_pid_list()
 *	unmap any allocated stacks
 */
static void stress_exec_free_pid_list(stress_pid_hash_t *sph)
{
#if defined(HAVE_CLONE)
	while (sph) {
		if (sph->stack)
			(void)munmap(sph->stack, CLONE_STACK_SIZE);
		sph = sph->next;
	}
#else
	(void)sph;
#endif
}

/*
 *  stress_exec_alloc_pid()
 *	allocate a pid hash item, pull it from the free list first
 *	and if there are none available pull it from the new item
 *	cache. Note that the new item hash cache should never run
 *	out it is sized to the maximum number of pids allowed to
 *	be running at any time.
 */
static stress_pid_hash_t *stress_exec_alloc_pid(const bool alloc_stack)
{
	NOCLOBBER stress_pid_hash_t *sph;

	/* Any on the free list, re-use these */
	if (free_list) {
		sph = free_list;
		free_list = free_list->next;

		sph->next = NULL;
	} else {
		if (stress_pid_cache_index < stress_pid_cache_items) {
			sph = &stress_pid_cache[stress_pid_cache_index];
			stress_pid_cache_index++;
		} else {
			/* Should never occur */
			return NULL;
		}
	}

#if defined(HAVE_CLONE)
	if (sph && alloc_stack && !sph->stack) {
		sph->stack = mmap(NULL, CLONE_STACK_SIZE, PROT_READ | PROT_WRITE,
				MAP_PRIVATE | MAP_ANONYMOUS | MAP_STACK, -1, 0);
		if (sph->stack == MAP_FAILED) {
			sph->stack = NULL;
			stress_exec_free_list_add(sph);
			return NULL;
		}
	}
#else
	(void)alloc_stack;
#endif
	return sph;
}

/*
 *  stress_exec_add_pid()
 *	add a sph and pid to the pid hash table for fast
 *	pid -> sph lookups later on
 */
static void stress_exec_add_pid(stress_pid_hash_t *sph, const pid_t pid)
{
	const size_t hash = (size_t)pid % HASH_EXECS;

	/*
	 *  Since pids are unique we never have
	 *  a pid clash so we can just add
	 *  the new pid hash to the table - this
	 *  reall simplifies life
	 */
	sph->pid = pid;
	sph->next = stress_pid_hash_table[hash];
	stress_pid_hash_table[hash] = sph;
}

/*
 *  stress_exec_free_pid()
 *	free up all pid items in hash table and free list
 */
static void stress_exec_free_pid(void)
{
	size_t i;

	/* Free hash table */
	for (i = 0; i < HASH_EXECS; i++)
		stress_exec_free_pid_list(stress_pid_hash_table[i]);

	/* Free free list */
	stress_exec_free_pid_list(free_list);
}

/*
 *  stress_exec_remove_pid()
 *	remove pid from the hash table and recycle it
 *	back onto the free list
 */
static void stress_exec_remove_pid(const pid_t pid)
{
	const size_t hash = (size_t)pid % HASH_EXECS;
	stress_pid_hash_t *sph = stress_pid_hash_table[hash];
	stress_pid_hash_t *prev = NULL;

	while (sph) {
		if (sph->pid == pid) {
			if (prev) {
				prev->next = sph->next;
			} else {
				stress_pid_hash_table[hash] = sph->next;
			}
			stress_exec_free_list_add(sph);
			return;
		}
		prev = sph;
		sph = sph->next;
	}
}

/*
 *  stress_exec_supported()
 *      check that we don't run this as root
 */
static int stress_exec_supported(const char *name)
{
	/*
	 *  Don't want to run this when running as root as
	 *  this could allow somebody to try and run another
	 *  executable as root.
	 */
	if (geteuid() == 0) {
		pr_inf_skip("%s stressor must not run as root, skipping the stressor\n", name);
		return -1;
	}
	return 0;
}

/*
 *  stress_exec_method()
 *	perform one of the various execs depending on how
 *	ea->exec_method is set.
 */
static int stress_exec_method(const stress_exec_context_t *context)
{
	int ret;

	switch (context->exec_method) {
	case EXEC_METHOD_EXECVE:
		ret = execve(context->exec_prog, context->argv, context->env);
		break;
#if defined(HAVE_EXECVEAT) &&	\
    defined(O_PATH)
	case EXEC_METHOD_EXECVEAT:
		if (stress_mwc1())
			ret = shim_execveat(0, context->exec_prog, context->argv, context->env, 0);
		else
			ret = shim_execveat(context->fdexec, "", context->argv, context->env, AT_EMPTY_PATH);
		break;
#endif
	default:
		ret = execve(context->exec_prog, context->argv, context->env);
		break;
	}
	return ret;
}

#if defined(HAVE_LIB_PTHREAD)
/*
 *  stress_exec_from_pthread()
 *	perform exec calls from inside a pthread. This should cause
 * 	the kernel to also kill and reap other associated pthreads
 *	automatically such as the dummy pthead
 */
static void *stress_exec_from_pthread(void *arg)
{
	const stress_exec_context_t *context = (const stress_exec_context_t *)arg;
	static int ret;
	char buffer[128];

	(void)snprintf(buffer, sizeof(buffer), "%s-pthread-exec", context->args->name);
	stress_set_proc_name(buffer);
	ret = stress_exec_method(context);
	pthread_exit((void *)&ret);

	return NULL;
}

/*
 *  stress_exec_dummy_pthread()
 *	dummy pthread that just sleeps and *should* be killed by the
 *	exec'ing of code from the other pthread
 */
static void *stress_exec_dummy_pthread(void *arg)
{
	const stress_exec_context_t *context = (const stress_exec_context_t *)arg;
	static int ret = 0;
	char buffer[128];

	(void)snprintf(buffer, sizeof(buffer), "%s-pthread-sleep", context->args->name);
	stress_set_proc_name(buffer);
	(void)sleep(1);

	pthread_exit((void *)&ret);

	return NULL;
}
#endif

/*
 *  stress_do_exec()
 * 	perform an exec. If we have pthread support then
 *	exercise exec from inside a pthread 25% of the time
 *	to add extra work on the kernel to make it reap
 *	other pthreads.
 */
static inline int stress_do_exec(stress_exec_context_t *context)
{
#if defined(HAVE_LIB_PTHREAD)
	int ret;
	int ret_dummy = EINVAL;
	pthread_t pthread_exec, pthread_dummy = 0;

	if (!context->no_pthread && (stress_mwc8() & 3) == 0) {
		ret_dummy = pthread_create(&pthread_dummy, NULL, stress_exec_dummy_pthread, (void *)context);

		ret = pthread_create(&pthread_exec, NULL, stress_exec_from_pthread, (void*)context);
		if (ret == 0) {
			int *exec_ret;

			ret = pthread_join(pthread_exec, (void *)&exec_ret);
			if (ret == 0) {
				if (ret_dummy)
					(void)pthread_kill(pthread_dummy, SIGKILL);
				return *exec_ret;
			}
		}
	}

	/*
	 *  pthread failure or 75% of the execs just fall back to
	 *  the normal non-pthread exec
	 */
	ret = stress_exec_method(context);
	/*
	 *  If exec fails, we end up here, so kill dummy pthread
	 */
	if (!context->no_pthread && (ret_dummy == 0))
		(void)pthread_kill(pthread_dummy, SIGKILL);
	return ret;
#else
	/*
	 *  non-pthread enable systems just do normal exec
	 */
	return stress_exec_method(context);
#endif
}


static int stress_exec_child(void *arg)
{
	stress_exec_context_t *argp = (stress_exec_context_t *)arg;
	int rc, ret, fd_out, fd_in, fd = -1;
	stress_exec_context_t context;
	int method = argp->exec_method;
	const bool big_env = ((argp->rnd8 >= 128 + 64) && (argp->rnd8 < 128 + 80));
	const bool big_arg = ((argp->rnd8 >= 128 + 80) && (argp->rnd8 < 128 + 96));
#if defined(HAVE_EXECVEAT) &&	\
    defined(O_PATH)
	bool exec_garbage = ((argp->rnd8 >= 128) && (argp->rnd8 < 128 + 64));
#else
	bool exec_garbage = false;
#endif
	if (method == EXEC_METHOD_ALL)
		method = stress_mwc1() ? EXEC_METHOD_EXECVE : EXEC_METHOD_EXECVEAT;

	stress_parent_died_alarm();
	(void)sched_settings_apply(true);

	if ((fd_out = open("/dev/null", O_WRONLY)) < 0) {
		pr_fail("%s: child open on /dev/null failed\n",
						argp->args->name);
		_exit(EXIT_FAILURE);
	}
	if ((fd_in = open("/dev/zero", O_RDONLY)) < 0) {
		pr_fail("%s: child open on /dev/zero failed\n",
						argp->args->name);
		(void)close(fd_out);
		_exit(EXIT_FAILURE);
	}
	(void)dup2(fd_out, STDOUT_FILENO);
	(void)dup2(fd_out, STDERR_FILENO);
	(void)dup2(fd_in, STDIN_FILENO);
	(void)close(fd_out);
	(void)close(fd_in);
	VOID_RET(int, stress_drop_capabilities(argp->args->name));

	/*
	 *  Create a garbage executable
	 */
#if defined(HAVE_EXECVEAT) &&	\
    defined(O_PATH)
	if (exec_garbage) {
		char buffer[1024];
		ssize_t n;

		fd = open(argp->garbage_prog, O_CREAT | O_RDWR | O_TRUNC, S_IRUSR | S_IWUSR | S_IXUSR);
		if (fd < 0) {
			exec_garbage = false;
			goto do_exec;
		}

		stress_rndbuf(buffer, sizeof(buffer));
		if (stress_mwc1()) {
			buffer[0] = '#';
			buffer[1] = '!';
			buffer[2] = '/';
		}

		n = write(fd, buffer, sizeof(buffer));
		if (n < (ssize_t)sizeof(buffer)) {
			exec_garbage = false;
			(void)close(fd);
			fd = -1;
			goto do_exec;
		}

		(void)close(fd);
		fd = open(argp->garbage_prog, O_PATH);
		if (fd < 0) {
			exec_garbage = false;
			goto do_exec;
		}
	}
do_exec:
#endif
	context.exec_prog = exec_garbage ? argp->garbage_prog : argp->exec_prog;
	context.args = argp->args;
	context.exec_method = method;
	context.no_pthread = argp->no_pthread;
	(void)memcpy(&context.argv, argp->argv, sizeof(context.argv));
	(void)memcpy(&context.env, argp->env, sizeof(context.env));
	if (big_env)
		argp->env[0] = argp->str;
	if (big_arg)
		argp->argv[2] = argp->str;
#if defined(HAVE_EXECVEAT) &&	\
    defined(O_PATH)
	context.fdexec = exec_garbage ? fd : argp->fdexec;
#endif
	ret = stress_do_exec(&context);

	rc = EXIT_SUCCESS;
	if (ret < 0) {
		switch (errno) {
		case 0:
			/* Should not happen? */
			rc = EXIT_SUCCESS;
			break;
#if defined(ENOEXEC)
		case ENOEXEC:
			/* we expect this error if exec'ing garbage */
			if (exec_garbage)
				rc = EXIT_SUCCESS;
			break;
#endif
#if defined(ENOMEM)
		case ENOMEM:
			rc = EXIT_NO_RESOURCE;
			break;
#endif
#if defined(EMFILE)
		case EMFILE:
			rc = EXIT_NO_RESOURCE;
			break;
#endif
#if defined(EAGAIN)
		case EAGAIN:
			/* Ignore as an error */
			rc = EXIT_SUCCESS;
			break;
#endif
#if defined(E2BIG)
		case E2BIG:
			/* E2BIG only happens on large args or env */
			if (!big_arg && !big_env)
				rc = EXIT_FAILURE;
			break;
#endif
		default:
			rc = EXIT_FAILURE;
			break;
		}
	}
	if (exec_garbage) {
		if (fd != -1)
			(void)close(fd);
		(void)shim_unlink(argp->garbage_prog);
	}

	return rc;
}

/*
 *  stress_exec()
 *	stress by forking and exec'ing
 */
static int stress_exec(const stress_args_t *args)
{
	char *exec_prog;
	char garbage_prog[PATH_MAX];
	int ret, rc = EXIT_FAILURE;
#if defined(HAVE_EXECVEAT) &&	\
    defined(O_PATH)
	int fdexec;
#endif
	uint64_t volatile exec_fails = 0, exec_calls = 0;
	uint32_t exec_max = DEFAULT_EXECS;
	int exec_method = EXEC_METHOD_ALL;
	int exec_fork_method = EXEC_FORK_METHOD_FORK;
	bool exec_no_pthread = false;
	size_t arg_max, cache_max;
	char *str;

	(void)stress_get_setting("exec-max", &exec_max);
	(void)stress_get_setting("exec-method", &exec_method);
	(void)stress_get_setting("exec-fork-method", &exec_fork_method);
	(void)stress_get_setting("exec-no-pthread", &exec_no_pthread);

	/*
	 *  Determine our own self as the executable, e.g. run stress-ng
	 */
	exec_prog = stress_proc_self_exe();
	if (!exec_prog) {
		if (args->instance == 0)
			pr_inf_skip("%s: skipping stressor, can't determine stress-ng "
				"executable name\n", args->name);
		return EXIT_NOT_IMPLEMENTED;
	}

#if defined(HAVE_VFORK)
	/* Remind folk that vfork can only do execve in this stressor */
	if ((exec_fork_method == EXEC_FORK_METHOD_VFORK) &&
	    (exec_method != EXEC_METHOD_EXECVE) &&
	    (args->instance == 0)) {
		pr_inf("%s: limiting vfork to only use execve()\n", args->name);
	}
#endif

	cache_max = sizeof(*stress_pid_cache) * exec_max;
	stress_pid_cache = (stress_pid_hash_t*)
		mmap(NULL, cache_max, PROT_READ | PROT_WRITE,
			MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
	if (stress_pid_cache == MAP_FAILED) {
		pr_inf_skip("%s: failed to allocate pid hash cache, skipping stressor\n", args->name);
		return EXIT_NO_RESOURCE;
	}
	stress_pid_cache_index = 0;
	stress_pid_cache_items = (size_t)exec_max;

	arg_max = (MAX_ARG_PAGES + 1) * args->page_size;
	str = mmap(NULL, arg_max, PROT_READ | PROT_WRITE,
			MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
	if (str == MAP_FAILED)
		str = NULL;
	else
		(void)memset(str, 'X', arg_max - 1);

#if !defined(HAVE_EXECVEAT)
	if (args->instance == 0 &&
	    ((exec_method == EXEC_METHOD_ALL) ||
	     (exec_method == EXEC_METHOD_EXECVEAT))) {
		pr_inf("%s: execveat not available, just using execve\n", args->name);
		exec_method = EXEC_METHOD_EXECVE;
	}
#endif
	ret = stress_temp_dir_mk_args(args);
	if (ret < 0)
		return stress_exit_status(-ret);
	(void)stress_temp_filename_args(args,
		garbage_prog, sizeof(garbage_prog), stress_mwc32());

#if defined(HAVE_EXECVEAT) &&	\
    defined(O_PATH)
	fdexec = open(exec_prog, O_PATH);
	if (fdexec < 0) {
		pr_fail("%s: open O_PATH on /proc/self/exe failed, errno=%d (%s)\n",
			args->name, errno, strerror(errno));
		goto err;
	}
#endif
	stress_set_proc_state(args->name, STRESS_STATE_RUN);

	do {
		NOCLOBBER uint32_t i;

		for (i = 0; i < exec_max; i++) {
			int status;
			pid_t pid;
#if defined(HAVE_CLONE)
			char *stack_top;
			const bool alloc_stack = (exec_fork_method == EXEC_FORK_METHOD_CLONE);
#else
			const bool alloc_stack = false;
#endif
			NOCLOBBER stress_pid_hash_t *sph;

			if (!keep_stressing_flag())
				break;

			sph = stress_exec_alloc_pid(alloc_stack);
			if (!sph)
				continue;

			sph->arg.garbage_prog = garbage_prog;
			sph->arg.exec_prog = exec_prog;
			sph->arg.rnd8 = stress_mwc8();
			sph->arg.exec_method = exec_method;
			sph->arg.no_pthread = exec_no_pthread;
			sph->arg.str = str;
			sph->arg.args = args;
#if defined(HAVE_EXECVEAT) &&	\
    defined(O_PATH)
			sph->arg.fdexec = fdexec;
#endif
			sph->arg.argv[0] = exec_prog;
			sph->arg.argv[1] = "--exec-exit";
			sph->arg.argv[2] = NULL;
			sph->arg.argv[3] = NULL;
			sph->arg.env[0] = NULL;
			sph->arg.env[1] = NULL;

			switch (exec_fork_method) {
			default:
			case EXEC_FORK_METHOD_FORK:
				pid = fork();
				if (pid == 0) {
					_exit(stress_exec_child(&sph->arg));
				}
				break;
#if defined(HAVE_VFORK)
			case EXEC_FORK_METHOD_VFORK:
				pid = shim_vfork();
				if (pid == 0) {
					/*
					 *  vfork has to be super simple to avoid clobbering
					 *  the parent stack, so just do vanilla execve
					 */
					_exit(execve(exec_prog, sph->arg.argv, sph->arg.env));
				}
				break;
#endif
#if defined(HAVE_CLONE)
			case EXEC_FORK_METHOD_CLONE:
				stack_top = (char *)stress_get_stack_top(sph->stack, CLONE_STACK_SIZE);
				stack_top = stress_align_stack(stack_top);
				pid = clone(stress_exec_child, stack_top, CLONE_VM | SIGCHLD, &sph->arg);
				break;
#endif
#if defined(HAVE_SPAWN_H) &&	\
    defined(HAVE_POSIX_SPAWN)
			case EXEC_FORK_METHOD_SPAWN:
				if (posix_spawn(&pid, exec_prog, NULL, NULL, sph->arg.argv, sph->arg.env) != 0)
					pid = -1;
				break;
#endif
			}

			stress_exec_add_pid(sph, pid);

			/* Check if we can reap children */
			ret = waitpid(-1, &status, WNOHANG);
			if ((ret > 0) && WIFEXITED(status)) {
				stress_exec_remove_pid((pid_t)ret);
				exec_calls++;
				inc_counter(args);
			}
		}

		/* Parent, wait for children */
		for (i = 0; i < HASH_EXECS; i++) {
			stress_pid_hash_t *sph = stress_pid_hash_table[i];

			while (sph) {
				stress_pid_hash_t *next = sph->next;

				if (LIKELY(sph->pid > 0)) {
					int status;

					(void)shim_waitpid(sph->pid, &status, 0);
					stress_exec_remove_pid(sph->pid);
					exec_calls++;
					inc_counter(args);
					if (WEXITSTATUS(status) != EXIT_SUCCESS)
						exec_fails++;
				}
				sph = next;
			}
		}
	} while (keep_stressing(args));

	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);


#if defined(HAVE_EXECVEAT) &&	\
    defined(O_PATH)
	(void)close(fdexec);
#endif
	if ((exec_fails > 0) && (g_opt_flags & OPT_FLAGS_VERIFY)) {
		pr_fail("%s: %" PRIu64 " execs failed (%.2f%%)\n",
			args->name, exec_fails,
			(double)exec_fails * 100.0 / (double)(exec_calls));
	}

	rc = EXIT_SUCCESS;
#if defined(HAVE_EXECVEAT) &&	\
    defined(O_PATH)
err:
#endif
	stress_exec_free_pid();

	if (str)
		(void)munmap((void *)str, arg_max);
	(void)shim_unlink(garbage_prog);
	(void)stress_temp_dir_rm_args(args);

	return rc;
}

stressor_info_t stress_exec_info = {
	.stressor = stress_exec,
	.supported = stress_exec_supported,
	.class = CLASS_SCHEDULER | CLASS_OS,
	.opt_set_funcs = opt_set_funcs,
	.verify = VERIFY_OPTIONAL,
	.help = help
};
