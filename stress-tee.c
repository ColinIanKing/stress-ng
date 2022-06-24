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

static const stress_help_t help[] = {
	{ NULL,	"tee N",	"start N workers exercising the tee system call" },
	{ NULL,	"tee-ops N",	"stop after N tee bogo operations" },
	{ NULL,	NULL,		NULL }
};

#if defined(HAVE_TEE) &&	\
    defined(SPLICE_F_NONBLOCK)

#define TEE_IO_SIZE	(65536)

static void stress_sigpipe_handler(int signum)
{
	(void)signum;

	keep_stressing_set_flag(false);
}

/*
 *  stress_tee_spawn()
 *	spawn off tee I/O processes
 */
static pid_t stress_tee_spawn(
	const stress_args_t *args,
	void (*func)(const stress_args_t *args, int fds[2]),
	int fds[2])
{
	pid_t pid;

	if (pipe(fds) < 0) {
		pr_err("%s: pipe failed: %d (%s)\n",
			args->name, errno, strerror(errno));
		return -1;
	}

again:
	pid = fork();
	if (pid < 0) {
		if (stress_redo_fork(errno))
			goto again;
		(void)close(fds[0]);
		(void)close(fds[1]);
		if (!keep_stressing(args))
			return -1;
		pr_err("%s: fork failed: %d (%s)\n",
			args->name, errno, strerror(errno));
		return -1;
	}
	if (pid == 0) {
		(void)setpgid(0, g_pgrp);
		stress_parent_died_alarm();
		(void)sched_settings_apply(true);

		func(args, fds);
		_exit(EXIT_SUCCESS);
	}
	(void)setpgid(pid, g_pgrp);
	return pid;
}

/*
 *  stress_tee_pipe_write()
 *	write data down a pipe
 */
static void stress_tee_pipe_write(const stress_args_t *args, int fds[2])
{
	static uint64_t ALIGN64 buffer[TEE_IO_SIZE / sizeof(uint64_t)];
	uint64_t counter = 0;

	(void)args;
	(void)close(fds[0]);

	(void)memset(buffer, 0, sizeof(buffer));
	while (keep_stressing_flag()) {
		ssize_t ret;

		buffer[0] = sizeof(buffer);
		buffer[1] = counter++;

		ret = write(fds[1], buffer, sizeof(buffer));
		if (ret < 0) {
			if (errno != EAGAIN)
				break;
		}
	}
	(void)close(fds[1]);
}

/*
 *  stress_tee_pipe_read()
 *	read data from a pipe
 */
static void stress_tee_pipe_read(const stress_args_t *args, int fds[2])
{
	static uint64_t ALIGN64 buffer[TEE_IO_SIZE / sizeof(uint64_t)];
	uint64_t count = 0;

	(void)close(fds[1]);

	while (keep_stressing_flag()) {
		ssize_t ret;
		size_t n = 0;

		while (n < sizeof(buffer)) {
			ret = read(fds[0], buffer, sizeof(buffer));
			if (ret < 0) {
				if (errno != EAGAIN)
					break;
			} else {
				n += (size_t)ret;
			}
		}
		if (buffer[0] != sizeof(buffer)) {
			pr_fail("%s: pipe read, wrong size detected\n", args->name);
		}
		if (buffer[1] != count) {
			pr_fail("%s: pipe read, wrong check value detected\n", args->name);
		}
		count++;
	}
	(void)close(fds[1]);
}

/*
 *  exercise_tee()
 *	exercise the tee syscall in most possible ways
 */
static int exercise_tee(
	const stress_args_t *args,
	const int release,
	const int fd_in,
	const int fd_out)
{
	ssize_t ret;

	if ((release != -1) &&
            (release >= stress_kernel_release(4, 10, 0))) {
		/*
		 *  Linux commit 3d6ea290f337
		 *  ("splice/tee/vmsplice: validate flags")
		 *  added a check for flags against ~SPLICE_F_ALL
		 *  in Linux 4.10.  For now disable this test
		 *  as it is throwing errors for pre-4.10 kernels
		 */
		ret = tee(fd_in, fd_out, INT_MAX, ~0U);
		if (ret >= 0) {
			pr_fail("%s: tee with illegal flags "
				"unexpectedly succeeded\n",
				args->name);
			return -1;
		}
	}

	/* Exercise on same pipe */
	ret = tee(fd_in, fd_in, INT_MAX, 0);
	if (ret >= 0) {
		pr_fail("%s: tee on same fd_out and fd_in "
			"unexpectedly succeeded\n",
			args->name);
		return -1;
	}

	/*
	 * Exercise tee on with 0 len argument creating absolutely
	 * no difference other than increase in kernel test coverage
	 */
	ret = tee(fd_in, fd_out, 0, 0);
	if (ret < 0) {
		pr_fail("%s: tee with 0 len argument "
			"unexpectedly failed\n",
			args->name);
		return -1;
	}

	return 0;
}

/*
 *  stress_tee()
 *	stress the Linux tee syscall
 */
static int stress_tee(const stress_args_t *args)
{
	ssize_t len, slen;
	int fd, pipe_in[2], pipe_out[2];
	pid_t pids[2];
	int ret = EXIT_FAILURE, status;
	const int release = stress_get_kernel_release();

	if (stress_sighandler(args->name, SIGPIPE, stress_sigpipe_handler, NULL) < 0)
		return EXIT_NO_RESOURCE;

	fd = open("/dev/null", O_WRONLY);
	if (fd < 0) {
		pr_err("%s: open /dev/null failed: errno=%d (%s)\n",
			args->name, errno, strerror(errno));
		return EXIT_NO_RESOURCE;
	}

	stress_set_proc_state(args->name, STRESS_STATE_RUN);

	pids[0] = stress_tee_spawn(args, stress_tee_pipe_write, pipe_in);
	if (pids[0] < 0) {
		(void)close(fd);
		return EXIT_FAILURE;
	}
	(void)close(pipe_in[1]);

	pids[1] = stress_tee_spawn(args, stress_tee_pipe_read, pipe_out);
	if (pids[0] < 0)
		goto tidy_child1;
	(void)close(pipe_out[0]);

	do {
		len = tee(pipe_in[0], pipe_out[1],
			INT_MAX, 0 & SPLICE_F_NONBLOCK);

		if (len < 0) {
			if (errno == EAGAIN)
				continue;
			if (errno == EINTR)
				break;
			if (errno == ENOMEM) {
				pr_inf_skip("%s: skipping stressor, out of memory\n",
					args->name);
				ret = EXIT_NO_RESOURCE;
				goto tidy_child2;
			}
			pr_fail("%s: tee failed: errno=%d (%s)\n",
				args->name, errno, strerror(errno));
			goto tidy_child2;
		} else {
			if (len == 0)
				break;
		}

		while (len > 0) {
			slen = splice(pipe_in[0], NULL, fd, NULL,
				(size_t)len, SPLICE_F_MOVE);
			if (errno == EINTR)
				break;
			if (slen < 0) {
				pr_err("%s: splice failed: errno=%d (%s)\n",
					args->name, errno, strerror(errno));
				goto tidy_child2;
			}
			len -= slen;
		}

		if (exercise_tee(args, release, pipe_in[0], pipe_out[1]) < 0)
			goto tidy_child2;

		inc_counter(args);
	} while (keep_stressing(args));

	ret = EXIT_SUCCESS;

tidy_child2:
	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);
	(void)close(pipe_out[1]);
	(void)kill(pids[1], SIGKILL);
	(void)shim_waitpid(pids[1], &status, 0);

tidy_child1:
	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);
	(void)close(pipe_in[0]);
	(void)kill(pids[0], SIGKILL);
	(void)shim_waitpid(pids[0], &status, 0);

	(void)close(fd);

	return ret;
}

stressor_info_t stress_tee_info = {
	.stressor = stress_tee,
	.class = CLASS_PIPE_IO | CLASS_OS | CLASS_SCHEDULER,
	.verify = VERIFY_ALWAYS,
	.help = help
};
#else
stressor_info_t stress_tee_info = {
	.stressor = stress_not_implemented,
	.class = CLASS_PIPE_IO | CLASS_OS | CLASS_SCHEDULER,
	.help = help
};
#endif
