/*
 * Copyright (C) 2013-2019 Canonical, Ltd.
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

static const args_t *s_args;

static const help_t help[] = {
	{ NULL,	"sigpipe N",	 "start N workers exercising SIGPIPE" },
	{ NULL,	"sigpipe-ops N", "stop after N SIGPIPE bogo operations" },
	{ NULL,	NULL,		 NULL }
};

#define CLONE_STACK_SIZE	(16 * 1024)

static void stress_sigpipe_handler(int signum)
{
	(void)signum;

	if (LIKELY(s_args != NULL))
		inc_counter(s_args);
}

static int NORETURN pipe_child(void *ptr)
{
	(void)ptr;

	_exit(EXIT_SUCCESS);
}

static inline int stress_sigpipe_write(
	const args_t *args,
	const char *buf,
	const size_t buf_len)
{
	pid_t pid;
	int pipefds[2];

	if (UNLIKELY(pipe(pipefds) < 0)) {
		pr_fail_dbg("pipe");
		return EXIT_FAILURE;
	}

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
		} else  {
			static char stack[CLONE_STACK_SIZE];
			const ssize_t stack_offset =
				stress_get_stack_direction() *
				(CLONE_STACK_SIZE - 64);
			char *stack_top = stack + stack_offset;

			pid = clone(pipe_child, align_stack(stack_top),
				CLONE_VM | CLONE_FS | CLONE_SIGHAND | SIGCHLD, NULL);
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
		if (UNLIKELY(g_keep_stressing_flag && (errno == EAGAIN)))
			goto again;
		(void)close(pipefds[0]);
		(void)close(pipefds[1]);
		pr_fail_dbg("fork");
		return EXIT_FAILURE;
	} else if (pid == 0) {
		/* Child, only for non-clone path */
		(void)pipe_child((void *)pipefds);
		_exit(EXIT_SUCCESS);
	} else {
		int status;

		/* Parent */
		(void)setpgid(pid, g_pgrp);
		(void)close(pipefds[0]);

		do {
			int ret;

			ret = write(pipefds[1], buf, buf_len);
			if (LIKELY(ret <= 0))
				break;
		} while (keep_stressing());

		(void)close(pipefds[1]);
		(void)kill(pid, SIGKILL);
		(void)shim_waitpid(pid, &status, 0);
	}
	return EXIT_SUCCESS;
}

/*
 *  stress_sigpipe
 *	stress by generating SIGPIPE signals on pipe I/O
 */
static int stress_sigpipe(const args_t *args)
{
	s_args = args;
	char buf[args->page_size * 2];

	if (stress_sighandler(args->name, SIGPIPE, stress_sigpipe_handler, NULL) < 0)
		return EXIT_FAILURE;

	(void)memset(buf, 0, sizeof buf);

	do {
		stress_sigpipe_write(args, buf, sizeof buf);
	} while (keep_stressing());
	return EXIT_SUCCESS;
}

stressor_info_t stress_sigpipe_info = {
	.stressor = stress_sigpipe,
	.class = CLASS_INTERRUPT | CLASS_OS,
	.help = help
};
