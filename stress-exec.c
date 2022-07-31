/*
 * Copyright (C) 2013-2021 Canonical, Ltd.
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

#define MIN_EXECS		(1)
#define MAX_EXECS		(16000)
#define DEFAULT_EXECS		(1)

#define EXEC_METHOD_ALL		(0)
#define EXEC_METHOD_EXECVE	(1)
#define EXEC_METHOD_EXECVEAT	(2)

#define MAX_ARG_PAGES		(32)

#if defined(__linux__)
/*
 *   exec* family of args to pass
 */
typedef struct {
	const stress_args_t *args;
	const char *path;
	char **argv_new;
	char **env_new;
#if defined(HAVE_EXECVEAT) &&	\
    defined(O_PATH)
	int fdexec;
#endif
	int exec_method;
} stress_exec_args_t;
#endif

typedef struct {
	const char *name;
	const int method;
} stress_exec_method_t;

static const stress_exec_method_t stress_exec_methods[] = {
	{ "all",	EXEC_METHOD_ALL },
	{ "execve",	EXEC_METHOD_EXECVE },
	{ "execveat",	EXEC_METHOD_EXECVEAT },
};

static const stress_help_t help[] = {
	{ NULL,	"exec N",	"start N workers spinning on fork() and exec()" },
	{ NULL,	"exec-ops N",	"stop after N exec bogo operations" },
	{ NULL,	"exec-max P",	"create P workers per iteration, default is 1" },
	{ NULL,	"exec-method M","select exec method: all, execve, execveat" },
	{ NULL,	NULL,		NULL }
};

/*
 *  stress_set_exec_max()
 *	set maximum number of forks allowed
 */
static int stress_set_exec_max(const char *opt)
{
	uint64_t exec_max;

	exec_max = stress_get_uint64(opt);
	stress_check_range("exec-max", exec_max,
		MIN_EXECS, MAX_EXECS);
	return stress_set_setting("exec-max", TYPE_ID_INT64, &exec_max);
}

/*
 *  stress_set_exec_method()
 *	set exec call method
 */
static int stress_set_exec_method(const char *opt)
{
	size_t i;

	for (i = 0; i < SIZEOF_ARRAY(stress_exec_methods); i++) {
		if (!strcmp(opt, stress_exec_methods[i].name))
			return stress_set_setting("exec-method", TYPE_ID_INT, &stress_exec_methods[i].method);
	}

	(void)fprintf(stderr, "exec-method must be one of:");
	for (i = 0; i < SIZEOF_ARRAY(stress_exec_methods); i++) {
		(void)fprintf(stderr, " %s", stress_exec_methods[i].name);
	}
	(void)fprintf(stderr, "\n");
	return -1;
}

static const stress_opt_set_func_t opt_set_funcs[] = {
	{ OPT_exec_max,		stress_set_exec_max },
	{ OPT_exec_method,	stress_set_exec_method },
	{ 0,		NULL }
};

#if defined(__linux__)

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
static int stress_exec_method(const stress_exec_args_t *ea)
{
	int ret;

	switch (ea->exec_method) {
	case EXEC_METHOD_EXECVE:
		ret = execve(ea->path, ea->argv_new, ea->env_new);
		break;
#if defined(HAVE_EXECVEAT) &&	\
    defined(O_PATH)
	case EXEC_METHOD_EXECVEAT:
		if (stress_mwc1())
			ret = shim_execveat(0, ea->path, ea->argv_new, ea->env_new, 0);
		else
			ret = shim_execveat(ea->fdexec, "", ea->argv_new, ea->env_new, AT_EMPTY_PATH);
		break;
#endif
	default:
		ret = execve(ea->path, ea->argv_new, ea->env_new);
		break;
	}
	return ret;
}

#if defined(HAVE_LIB_PTHREAD)
/*
 *  stress_exec_from_pthread()
 *	perform exec calls from inside a pthead. This should cause
 * 	the kernel to also kill and reap other associated pthreads
 *	automatically such as the dummy pthead
 */
static void *stress_exec_from_pthread(void *arg)
{
	const stress_exec_args_t *ea = (const stress_exec_args_t *)arg;
	static int ret;
	char buffer[128];

	(void)snprintf(buffer, sizeof(buffer), "%s-pthread-exec", ea->args->name);
	stress_set_proc_name(buffer);
	ret = stress_exec_method(ea);
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
	const stress_exec_args_t *ea = (const stress_exec_args_t *)arg;
	static int ret = 0;
	char buffer[128];

	(void)snprintf(buffer, sizeof(buffer), "%s-pthread-sleep", ea->args->name);
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
static inline int stress_do_exec(stress_exec_args_t *ea)
{
#if defined(HAVE_LIB_PTHREAD)
	int ret;
	int ret_dummy = EINVAL;
	pthread_t pthread_exec, pthread_dummy = 0;

	if ((stress_mwc8() & 3) == 0) {
		ret_dummy = pthread_create(&pthread_dummy, NULL, stress_exec_dummy_pthread, (void *)ea);

		ret = pthread_create(&pthread_exec, NULL, stress_exec_from_pthread, (void*)ea);
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
	ret = stress_exec_method(ea);
	/*
	 *  If exec fails, we end up here, so kill dummy pthread
	 */
	if (ret_dummy == 0)
		(void)pthread_kill(pthread_dummy, SIGKILL);
	return ret;
#else
	/*
	 *  non-pthread enable systems just do normal exec
	 */
	return stress_exec_method(ea);
#endif
}

/*
 *  stress_exec()
 *	stress by forking and exec'ing
 */
static int stress_exec(const stress_args_t *args)
{
	static pid_t pids[MAX_EXECS];
	char path[PATH_MAX + 1];
	char filename[PATH_MAX];
	ssize_t len;
	int ret, rc = EXIT_FAILURE;
#if defined(HAVE_EXECVEAT) &&	\
    defined(O_PATH)
	int fdexec;
#endif
	uint64_t exec_fails = 0, exec_calls = 0;
	uint64_t exec_max = DEFAULT_EXECS;
	int exec_method = EXEC_METHOD_ALL;
	size_t arg_max;
	char *argv_new[] = { NULL, "--exec-exit", NULL, NULL };
	char *env_new[] = { NULL, NULL };
	char *str;

	(void)stress_get_setting("exec-max", &exec_max);
	(void)stress_get_setting("exec-method", &exec_method);

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

	/*
	 *  Determine our own self as the executable, e.g. run stress-ng
	 */
	len = shim_readlink("/proc/self/exe", path, sizeof(path));
	if ((len < 0) || (len > PATH_MAX)) {
		pr_fail("%s: readlink on /proc/self/exe failed, errno=%d (%s)\n",
			args->name, errno, strerror(errno));
		return EXIT_FAILURE;
	}
	path[len] = '\0';
	argv_new[0] = path;

	ret = stress_temp_dir_mk_args(args);
	if (ret < 0)
		return exit_status(-ret);
	(void)stress_temp_filename_args(args,
		filename, sizeof(filename), stress_mwc32());

#if defined(HAVE_EXECVEAT) &&	\
    defined(O_PATH)
	fdexec = open(path, O_PATH);
	if (fdexec < 0) {
		pr_fail("%s: open O_PATH on /proc/self/exe failed, errno=%d (%s)\n",
			args->name, errno, strerror(errno));
		goto err;
	}
#endif
	stress_set_proc_state(args->name, STRESS_STATE_RUN);

	do {
		unsigned int i;

		(void)memset(pids, 0, sizeof(pids));

		for (i = 0; i < exec_max; i++) {
			const uint8_t rnd8 = stress_mwc8();
			bool exec_garbage, big_env, big_arg;

#if defined(HAVE_EXECVEAT) &&	\
    defined(O_PATH)
			exec_garbage = ((rnd8 >= 128) && (rnd8 < 128 + 64));
#else
			exec_garbage = false;
#endif
			big_env = ((rnd8 >= 128 + 64) && (rnd8 < 128 + 80));
			big_arg = ((rnd8 >= 128 + 80) && (rnd8 < 128 + 96));

			pids[i] = fork();

			if (pids[i] == 0) {
				int fd_out, fd_in, fd = -1;
				stress_exec_args_t exec_args;
				int method = exec_method;

				if (method == EXEC_METHOD_ALL)
					method = stress_mwc1() ? EXEC_METHOD_EXECVE : EXEC_METHOD_EXECVEAT;

				stress_parent_died_alarm();
				(void)sched_settings_apply(true);

				if ((fd_out = open("/dev/null", O_WRONLY)) < 0) {
					pr_fail("%s: child open on /dev/null failed\n",
						args->name);
					_exit(EXIT_FAILURE);
				}
				if ((fd_in = open("/dev/zero", O_RDONLY)) < 0) {
					pr_fail("%s: child open on /dev/zero failed\n",
						args->name);
					(void)close(fd_out);
					_exit(EXIT_FAILURE);
				}
				(void)dup2(fd_out, STDOUT_FILENO);
				(void)dup2(fd_out, STDERR_FILENO);
				(void)dup2(fd_in, STDIN_FILENO);
				(void)close(fd_out);
				(void)close(fd_in);
				ret = stress_drop_capabilities(args->name);
				(void)ret;

				/*
				 *  Create a garbage executable
				 */
#if defined(HAVE_EXECVEAT) &&	\
    defined(O_PATH)
				if (exec_garbage) {
					char buffer[1024];
					ssize_t n;

					fd = open(filename, O_CREAT | O_RDWR | O_TRUNC, S_IRUSR | S_IWUSR | S_IXUSR);
					if (fd < 0) {
						exec_garbage = 0;
						goto do_exec;
					}

					stress_strnrnd(buffer, sizeof(buffer));
					if (stress_mwc1()) {
						buffer[0] = '#';
						buffer[1] = '!';
						buffer[2] = '/';
					}

					n = write(fd, buffer, sizeof(buffer));
					if (n < (ssize_t)sizeof(buffer)) {
						exec_garbage = 0;
						goto do_exec;
					}

					(void)close(fd);
					fd = open(filename, O_PATH);
					if (fd < 0) {
						exec_garbage = 0;
						goto do_exec;
					}
				}
do_exec:
#endif
				if (big_env)
					env_new[0] = str;
				if (big_arg)
					argv_new[2] = str;
				exec_args.path = exec_garbage ? filename : path;
				exec_args.args = args;
				exec_args.exec_method = method;
				exec_args.argv_new = argv_new;
				exec_args.env_new = env_new;
#if defined(HAVE_EXECVEAT) &&	\
    defined(O_PATH)
				exec_args.fdexec = exec_garbage ? fd : fdexec;
#endif

				ret = stress_do_exec(&exec_args);

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
					(void)shim_unlink(filename);
				}

				/* Child, immediately exit */
				_exit(rc);
			}
			if (!keep_stressing_flag())
				break;
		}
		for (i = 0; i < exec_max; i++) {
			if (pids[i] > 0) {
				int status;
				/* Parent, wait for child */
				(void)shim_waitpid(pids[i], &status, 0);
				exec_calls++;
				inc_counter(args);
				if (WEXITSTATUS(status) != EXIT_SUCCESS)
					exec_fails++;
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
	if (str)
		(void)munmap((void *)str, arg_max);
	(void)shim_unlink(filename);
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
#else
stressor_info_t stress_exec_info = {
	.stressor = stress_not_implemented,
	.class = CLASS_SCHEDULER | CLASS_OS,
	.opt_set_funcs = opt_set_funcs,
	.help = help
};
#endif
