// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2013-2021 Canonical, Ltd.
 * Copyright (C) 2022-2023 Colin Ian King.
 *
 */
#include "stress-ng.h"
#include "core-builtin.h"

/*
 *  Herd of pipe processes, simulates how GNU make passes tokens
 *  when building with -j option, but without the timely building.
 *
 *  Inspired by Linux commit:
 *     0ddad21d3e99c743a3aa473121dc5561679e26bb
 *     ("pipe: use exclusive waits when reading or writing")
 *
 */
#define PIPE_HERD_MAX	(100)

typedef struct {
	uint64_t	counter;
	uint32_t	check;
} stress_pipeherd_data_t;

static const stress_help_t help[] = {
	{ "p N", "pipeherd N",		"start N multi-process workers exercising pipes I/O" },
	{ NULL,	"pipeherd-ops N",	"stop after N pipeherd I/O bogo operations" },
	{ NULL,	"pipeherd-yield",	"force processes to yield after each write" },
	{ NULL,	NULL,			NULL }
};

static int stress_set_pipeherd_yield(const char *opt)
{
	return stress_set_setting_true("pipeherd-yield", opt);
}

static int stress_pipeherd_read_write(const stress_args_t *args, const int fd[2], const bool pipeherd_yield)
{
	while (stress_continue(args)) {
		stress_pipeherd_data_t data;
		ssize_t sz;

		sz = read(fd[0], &data, sizeof(data));
		if (UNLIKELY(sz < 0)) {
			if ((errno == EINTR) || (errno == EPIPE))
				break;
			return EXIT_FAILURE;
		}
		data.counter++;
		sz = write(fd[1], &data, sizeof(data));
		if (UNLIKELY(sz < 0)) {
			if ((errno == EINTR) || (errno == EPIPE))
				break;
			return EXIT_FAILURE;
		}
		if (pipeherd_yield)
			(void)shim_sched_yield();
	}
	return EXIT_SUCCESS;
}

/*
 *  stress_pipeherd
 *	stress by heavy pipe I/O
 */
static int stress_pipeherd(const stress_args_t *args)
{
	int fd[2];
	stress_pipeherd_data_t data;
	uint32_t check = stress_mwc32();
	pid_t pids[PIPE_HERD_MAX];
	int i, rc;
	ssize_t sz;
	bool pipeherd_yield = false;
#if defined(HAVE_GETRUSAGE) &&	\
    defined(RUSAGE_CHILDREN) &&	\
    defined(RUSAGE_SELF) &&	\
    defined(HAVE_RUSAGE_RU_NVCSW)
	struct rusage usage;
	double t1, t2;
#endif

	(void)stress_get_setting("pipeherd-yield", &pipeherd_yield);

	if (pipe(fd) < 0) {
		pr_fail("%s: pipe failed: %d (%s)\n",
			args->name, errno, strerror(errno));
		return EXIT_FAILURE;
	}

#if defined(F_GETFL) &&	\
    defined(F_SETFL) && \
    defined(O_DIRECT)
	{
		int flag;

		/* Enable pipe "packet mode" if possible */
		flag = fcntl(fd[1], F_GETFL);
		if (flag != -1) {
			flag |= O_DIRECT;
			VOID_RET(int, fcntl(fd[1], F_SETFL, flag));
		}
	}
#endif

	data.counter = 0;
	data.check = check;
	sz = write(fd[1], &data, sizeof(data));
	if (sz < 0) {
		pr_fail("%s: write to pipe failed: %d (%s)\n",
			args->name, errno, strerror(errno));
		(void)close(fd[0]);
		(void)close(fd[1]);
		return EXIT_FAILURE;
	}

	for (i = 0; i < PIPE_HERD_MAX; i++)
		pids[i] = -1;

	stress_set_proc_state(args->name, STRESS_STATE_RUN);

#if defined(HAVE_GETRUSAGE) &&	\
    defined(RUSAGE_CHILDREN) &&	\
    defined(RUSAGE_SELF) &&	\
    defined(HAVE_RUSAGE_RU_NVCSW)
	t1 = stress_time_now();
#endif
	for (i = 0; stress_continue(args) && (i < PIPE_HERD_MAX); i++) {
		pid_t pid;

		pid = fork();
		if (pid == 0) {
			stress_parent_died_alarm();
			(void)sched_settings_apply(true);
			rc = stress_pipeherd_read_write(args, fd, pipeherd_yield);
			(void)close(fd[0]);
			(void)close(fd[1]);
			_exit(rc);
		} else if (pid < 0) {
			pids[i] = -1;
		} else {
			pids[i] = pid;
		}
	}

	VOID_RET(int, stress_pipeherd_read_write(args, fd, pipeherd_yield));
	sz = read(fd[0], &data, sizeof(data));
	if (sz > 0)
		stress_bogo_set(args, data.counter);

#if defined(HAVE_GETRUSAGE) &&	\
    defined(RUSAGE_CHILDREN) &&	\
    defined(RUSAGE_SELF) &&	\
    defined(HAVE_RUSAGE_RU_NVCSW)
	t2 = stress_time_now();
#endif

	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);

	for (i = 0; i < PIPE_HERD_MAX; i++) {
		if (pids[i] >= 0) {
			int status;

			(void)shim_kill(pids[i], SIGKILL);
			(void)shim_waitpid(pids[i], &status, 0);
		}
	}

	(void)close(fd[0]);
	(void)close(fd[1]);

#if defined(HAVE_GETRUSAGE) &&	\
    defined(RUSAGE_CHILDREN) &&	\
    defined(RUSAGE_SELF) &&	\
    defined(HAVE_RUSAGE_RU_NVCSW)
	(void)shim_memset(&usage, 0, sizeof(usage));
	if (shim_getrusage(RUSAGE_CHILDREN, &usage) == 0) {
		long total = usage.ru_nvcsw + usage.ru_nivcsw;

		(void)shim_memset(&usage, 0, sizeof(usage));
		if (getrusage(RUSAGE_SELF, &usage) == 0) {
			const uint64_t count = stress_bogo_get(args);
			const double dt = t2 - t1;

			total += usage.ru_nvcsw + usage.ru_nivcsw;
			if (total) {
				stress_metrics_set(args, 0, "context switches per bogo op",
					(count > 0) ? ((double)total / (double)count) : 0.0);
				stress_metrics_set(args, 1, "context switches per sec",
					(dt > 0.0) ? ((double)total / dt) : 0.0);
			}
		}
	}
#endif
	if (data.check != check) {
		pr_fail("%s: verification check failed, got 0x%" PRIx32 ", "
			"expected 0x%" PRIx32 "\n",
			args->name, data.check, check);
		return EXIT_FAILURE;
	}
	return EXIT_SUCCESS;
}

static const stress_opt_set_func_t opt_set_funcs[] = {
	{ OPT_pipeherd_yield,	stress_set_pipeherd_yield },
	{ 0,                    NULL }
};

stressor_info_t stress_pipeherd_info = {
	.stressor = stress_pipeherd,
	.class = CLASS_PIPE_IO | CLASS_MEMORY | CLASS_OS,
	.opt_set_funcs = opt_set_funcs,
	.verify = VERIFY_ALWAYS,
	.help = help
};
