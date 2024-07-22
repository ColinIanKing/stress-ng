/*
 * Copyright (C) 2013-2021 Canonical, Ltd.
 * Copyright (C) 2022-2024 Colin Ian King.
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

#include <sched.h>

static stress_args_t *s_args;

static const stress_help_t help[] = {
	{ NULL,	"sigpipe N",	 "start N workers exercising SIGPIPE" },
	{ NULL,	"sigpipe-ops N", "stop after N SIGPIPE bogo operations" },
	{ NULL,	NULL,		 NULL }
};

#define CLONE_STACK_SIZE	(16 * 1024)

static void stress_sigpipe_handler(int signum)
{
	(void)signum;

	if (LIKELY(s_args != NULL))
		stress_bogo_inc(s_args);
}

#if defined(HAVE_CLONE)
static inline void NORETURN no_return_pipe_child(void *ptr)
{
	int *pipefds = (int *)ptr;

	(void)close(pipefds[0]);	/* causes SIGPIPE */
	(void)close(pipefds[1]);
	_exit(EXIT_SUCCESS);
}

static int pipe_child(void *ptr)
{
	no_return_pipe_child(ptr);
	return -1;
}
#endif

static inline int stress_sigpipe_write(
	stress_args_t *args,
	const char *buf,
	const size_t buf_len,
	uint64_t *pipe_count,
	uint64_t *epipe_count)
{
	pid_t pid;
	int pipefds[2];

	if (UNLIKELY(pipe(pipefds) < 0)) {
		pr_fail("%s: pipe failed, errno=%d (%s)\n",
			args->name, errno, strerror(errno));
		return EXIT_FAILURE;
	}
	(*pipe_count)++;

#if defined(F_SETPIPE_SZ)
	/*
	 *  Try to limit pipe max size to 1 page
	 *  and hence writes more than this will
	 *  block and fail with SIGPIPE when
	 *  the child exits and closes the pipe
	 */
	(void)fcntl(pipefds[1], F_SETPIPE_SZ, args->page_size);
#endif

again:
#if defined(HAVE_CLONE)
	{
		static bool clone_enosys = false;

		if (UNLIKELY(clone_enosys)) {
			pid = fork();
		} else {
			static char stack[CLONE_STACK_SIZE];
			char *stack_top = (char *)stress_get_stack_top((void *)stack, CLONE_STACK_SIZE);

			pid = clone(pipe_child, stress_align_stack(stack_top),
				CLONE_VM | CLONE_FS | CLONE_SIGHAND | SIGCHLD, pipefds);
			/*
			 *  May not have clone, so fall back to fork instead.
			 */
			if (UNLIKELY((pid < 0) && (errno == ENOSYS))) {
				clone_enosys = true;
				goto again;
			}
		}
	}
#else
	pid = fork();
#endif
	if (UNLIKELY(pid < 0)) {
		if (stress_redo_fork(args, errno))
			goto again;

		(void)close(pipefds[0]);
		(void)close(pipefds[1]);
		if (!stress_continue(args))
			goto finish;
		pr_err("%s: fork failed, errno=%d (%s)\n",
			args->name, errno, strerror(errno));
		return EXIT_FAILURE;
	} else if (pid == 0) {
		/* Child, only for non-clone path */
		(void)close(pipefds[0]);	/* causes SIGPIPE */
		(void)close(pipefds[1]);
		_exit(EXIT_SUCCESS);
	} else {
		/* Parent */
		(void)close(pipefds[0]);

		do {
			ssize_t ret;

			/* cause SIGPIPE if pipe closed */
			ret = write(pipefds[1], buf, buf_len);
			if (LIKELY(ret <= 0)) {
				if (errno == EPIPE)
					(*epipe_count)++;
				break;
			}
		} while (stress_continue(args));

		(void)close(pipefds[1]);
		(void)stress_kill_pid_wait(pid, NULL);
	}
finish:
	return EXIT_SUCCESS;
}

/*
 *  stress_sigpipe
 *	stress by generating SIGPIPE signals on pipe I/O
 */
static int stress_sigpipe(stress_args_t *args)
{
	const size_t buf_size = args->page_size * 2;
	char *buf;
	uint64_t pipe_count = 0, epipe_count = 0;
	int rc = EXIT_SUCCESS;

	buf = (char *)stress_mmap_populate(NULL, buf_size,
			PROT_READ | PROT_WRITE,
			MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
	if (buf == MAP_FAILED) {
		pr_inf_skip("%s: failed to allocate buffer of %zd bytes, skipping stressor\n",
			args->name, buf_size);
		return EXIT_NO_RESOURCE;
	}
	stress_set_vma_anon_name(buf, buf_size, "io-buffer");

	s_args = args;
	if (stress_sighandler(args->name, SIGPIPE, stress_sigpipe_handler, NULL) < 0) {
		(void)munmap((void *)buf, buf_size);
		return EXIT_FAILURE;
	}

	stress_set_proc_state(args->name, STRESS_STATE_RUN);
	stress_sync_start_wait(args);

	do {
		stress_sigpipe_write(args, buf, buf_size, &pipe_count, &epipe_count);
	} while (stress_continue(args));

	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);

	/* simple sanity check */
	if ((pipe_count + epipe_count > 0) && (stress_bogo_get(args) < 1)) {
		pr_fail("%s: %" PRIu64 " pipes closed and %" PRIu64 " EPIPE "
			"writes occurred but got 0 SIGPIPE signals\n",
			args->name, pipe_count, epipe_count);
		rc = EXIT_FAILURE;
	}

	(void)munmap((void *)buf, buf_size);

	return rc;
}

stressor_info_t stress_sigpipe_info = {
	.stressor = stress_sigpipe,
	.class = CLASS_SIGNAL | CLASS_OS,
	.verify = VERIFY_ALWAYS,
	.help = help
};
