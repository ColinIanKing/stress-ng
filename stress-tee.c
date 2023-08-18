// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2013-2021 Canonical, Ltd.
 * Copyright (C) 2022-2023 Colin Ian King.
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

typedef struct {
	uint64_t	length;
	uint64_t	counter;
} stress_tee_t;

static void stress_sigpipe_handler(int signum)
{
	(void)signum;

	stress_continue_set_flag(false);
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
		if (stress_redo_fork(args, errno))
			goto again;
		(void)close(fds[0]);
		(void)close(fds[1]);
		if (!stress_continue(args))
			return -1;
		pr_err("%s: fork failed: %d (%s)\n",
			args->name, errno, strerror(errno));
		return -1;
	}
	if (pid == 0) {
		stress_parent_died_alarm();
		(void)sched_settings_apply(true);

		func(args, fds);
		_exit(EXIT_SUCCESS);
	}
	return pid;
}

/*
 *  stress_tee_pipe_write()
 *	write data down a pipe
 */
static void stress_tee_pipe_write(const stress_args_t *args, int fds[2])
{
	static stress_tee_t ALIGN64 data;

	data.length = sizeof(data);
	data.counter = 0;

	(void)args;
	(void)close(fds[0]);

	while (stress_continue_flag()) {
		ssize_t ret;

		ret = write(fds[1], &data, sizeof(data));
		if (UNLIKELY(ret < 0)) {
			switch (errno) {
			case EPIPE:
				break;
			case EINTR:
			case EAGAIN:
				continue;
			default:
				pr_fail("%s: unexpected write error, errno=%d (%s)\n",
					args->name, errno, strerror(errno));
				break;
			}
		}
		data.counter++;
	}
	(void)close(fds[1]);
}

/*
 *  stress_tee_pipe_read()
 *	read data from a pipe
 */
static void stress_tee_pipe_read(const stress_args_t *args, int fds[2])
{
	static stress_tee_t ALIGN64 data;
	register uint64_t counter = 0;

	data.length = sizeof(data);

	(void)close(fds[1]);

	while (stress_continue_flag()) {
		register size_t n = 0;
		ssize_t ret;

		while (n < sizeof(data)) {
			ret = read(fds[0], &data, sizeof(data));
			if (UNLIKELY(ret < 0)) {
				switch (errno) {
				case EPIPE:
					goto finish;
				case EAGAIN:
				case EINTR:
					continue;
				default:
					pr_fail("%s: unexpected read error, errno=%d (%s)\n",
						args->name, errno, strerror(errno));
					goto finish;
				}
			} else {
				n += (size_t)ret;
			}
		}
		if (UNLIKELY(data.length != sizeof(data))) {
			pr_fail("%s: pipe read of %zu bytes, wrong size detected, got %" PRIu64
				", expected %zu\n", args->name,
				n, data.length, sizeof(data));
		}
		if (UNLIKELY(data.counter != counter)) {
			pr_fail("%s: pipe read, wrong check value detected\n", args->name);
		}
		counter++;
	}
finish:
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
		if (UNLIKELY(ret >= 0)) {
			pr_fail("%s: tee with illegal flags "
				"unexpectedly succeeded\n",
				args->name);
			return -1;
		}
	}

	/* Exercise on same pipe */
	ret = tee(fd_in, fd_in, INT_MAX, 0);
	if (UNLIKELY(ret >= 0)) {
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
	if (UNLIKELY(ret < 0)) {
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
	int metrics_count = 0;
	double duration = 0.0, bytes = 0.0, rate;

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
		if (LIKELY(metrics_count++ < 1000)) {
			len = tee(pipe_in[0], pipe_out[1], INT_MAX, 0);
			if (LIKELY(len > 0))
				goto do_splice;

		} else {
			double t;

			metrics_count = 0;
			t = stress_time_now();
			len = tee(pipe_in[0], pipe_out[1], INT_MAX, 0);
			if (LIKELY(len > 0)) {
				duration += stress_time_now() - t;
				bytes += (double)len;
				goto do_splice;
			}
		}
		if (len == 0)
			break;

		if (UNLIKELY(len < 0)) {
			switch (errno) {
			case EPIPE:
			case EAGAIN:
				continue;
			case EINTR:
				ret = EXIT_SUCCESS;
				goto tidy_child2;
				break;
			case ENOMEM:
				pr_inf_skip("%s: skipping stressor, out of memory\n",
					args->name);
				ret = EXIT_NO_RESOURCE;
				goto tidy_child2;
			}
			pr_fail("%s: tee failed: errno=%d (%s)\n",
				args->name, errno, strerror(errno));
			goto tidy_child2;
		}

do_splice:
		while (len > 0) {
			slen = splice(pipe_in[0], NULL, fd, NULL,
				(size_t)len, SPLICE_F_MOVE);
			if (UNLIKELY(errno == EINTR))
				break;
			if (UNLIKELY(slen < 0)) {
				pr_err("%s: splice failed: errno=%d (%s)\n",
					args->name, errno, strerror(errno));
				goto tidy_child2;
			}
			len -= slen;
		}

		if (exercise_tee(args, release, pipe_in[0], pipe_out[1]) < 0)
			goto tidy_child2;

		stress_bogo_inc(args);
	} while (stress_continue(args));

	ret = EXIT_SUCCESS;

	rate = (duration > 0.0) ? bytes / duration : 0.0;
	stress_metrics_set(args, 0, "MB per sec tee rate", rate / (double)MB);

tidy_child2:
	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);
	(void)close(pipe_out[1]);
	(void)shim_kill(pids[1], SIGKILL);
	(void)shim_waitpid(pids[1], &status, 0);

tidy_child1:
	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);
	(void)close(pipe_in[0]);
	(void)shim_kill(pids[0], SIGKILL);
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
	.stressor = stress_unimplemented,
	.class = CLASS_PIPE_IO | CLASS_OS | CLASS_SCHEDULER,
	.verify = VERIFY_ALWAYS,
	.help = help,
	.unimplemented_reason = "built without tee() system call or undefined SPLICE_F_NONBLOCK"
};
#endif
