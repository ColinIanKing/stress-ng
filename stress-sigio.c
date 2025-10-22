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
#include "core-affinity.h"
#include "core-builtin.h"
#include "core-killpid.h"
#include "core-mmap.h"
#include "core-out-of-memory.h"

#if defined(HAVE_SYS_SELECT_H)
#include <sys/select.h>
#else
UNEXPECTED
#endif

static const stress_help_t help[] = {
	{ NULL,	"sigio N",	"start N workers that exercise SIGIO signals" },
	{ NULL,	"sigio-ops N",	"stop after N bogo sigio signals" },
	{ NULL,	NULL,		NULL }
};

#if defined(O_ASYNC) &&	\
    defined(O_NONBLOCK) && \
    defined(F_SETOWN) && \
    defined(F_GETFL) && \
    defined(F_SETFL)

#define BUFFER_SIZE	(4096)

static volatile int got_err;
static volatile uint64_t async_sigs;
static int rd_fd;
static stress_args_t *sigio_args;
static pid_t pid;
static double time_end;
static char *rd_buffer;

/*
 *  stress_sigio_handler()
 *      SIGIO handler
 */
static void MLOCKED_TEXT stress_sigio_handler(int signum)
{
	(void)signum;

	async_sigs++;

	if (LIKELY(rd_fd > 0)) {
		/*
		 *  Data is ready, so drain as much as possible
		 */
		while (LIKELY(stress_continue_flag() && (stress_time_now() < time_end))) {
			ssize_t ret;

			got_err = 0;
			errno = 0;
			ret = read(rd_fd, rd_buffer, BUFFER_SIZE);
			if (UNLIKELY(ret < 0)) {
				if (errno != EAGAIN)
					got_err = errno;
				break;
			}
			if (sigio_args)
				stress_bogo_inc(sigio_args);
		}
	}
}

/*
 *  stress_sigio
 *	stress reading of /dev/zero using SIGIO
 */
static int stress_sigio(stress_args_t *args)
{
	int ret, rc = EXIT_SUCCESS, fds[2], flags = -1, parent_cpu;
	double t_start, t_delta, rate;
	char *buffers, *wr_buffer;

	rd_fd = -1;
	sigio_args = args;
	pid = -1;

	time_end = args->time_end;
	buffers = (char *)stress_mmap_populate(NULL, 2 * BUFFER_SIZE,
			PROT_READ | PROT_WRITE,
			MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
	if (buffers == MAP_FAILED) {
		pr_inf_skip("%s: failed to mmap %d byte I/O buffers%s, "
			"errno=%d (%s), skipping stressor\n",
			args->name, 2 * BUFFER_SIZE,
			stress_get_memfree_str(), errno, strerror(errno));
		return EXIT_NO_RESOURCE;
	}
	stress_set_vma_anon_name(buffers, 2 * BUFFER_SIZE, "io-buffers");
	rd_buffer = &buffers[BUFFER_SIZE * 0];
	wr_buffer = &buffers[BUFFER_SIZE * 1];

	if (pipe(fds) < 0) {
		pr_err("%s: pipe failed, errno=%d (%s)\n",
			args->name, errno, strerror(errno));
		(void)munmap((void *)buffers, 2 * BUFFER_SIZE);
		return EXIT_NO_RESOURCE;
	}
	rd_fd = fds[0];

#if defined(F_SETPIPE_SZ)
	(void)fcntl(fds[0], F_SETPIPE_SZ, BUFFER_SIZE);
	(void)fcntl(fds[1], F_SETPIPE_SZ, BUFFER_SIZE);
#endif

#if !defined(__minix__)
	ret = fcntl(fds[0], F_SETOWN, getpid());
	if (ret < 0) {
		if (errno != EINVAL) {
			pr_err("%s: fcntl F_SETOWN failed, errno=%d (%s)\n",
				args->name, errno, strerror(errno));
			goto err;
		}
	}
#endif
	flags = fcntl(fds[0], F_GETFL);
	if (flags < 0) {
		pr_err("%s: fcntl F_GETFL failed, errno=%d (%s)\n",
			args->name, errno, strerror(errno));
		goto err;
	}

	async_sigs = 0;
again:
	parent_cpu = stress_get_cpu();
	pid = fork();
	if (pid < 0) {
		if (stress_redo_fork(args, errno))
			goto again;
		if (UNLIKELY(!stress_continue(args)))
			goto finish;
		pr_err("%s: fork failed, errno=%d (%s)\n",
			args->name, errno, strerror(errno));
		goto err;
	} else if (pid == 0) {
		/* Child */

		stress_set_proc_state(args->name, STRESS_STATE_RUN);
		(void)stress_change_cpu(args, parent_cpu);
		stress_parent_died_alarm();
		(void)sched_settings_apply(true);

		/* Make sure this is killable by OOM killer */
		stress_set_oom_adjustment(args, true);

		(void)close(fds[0]);
		(void)shim_memset(wr_buffer, 0, BUFFER_SIZE);

		stress_set_proc_state(args->name, STRESS_STATE_RUN);

		while (stress_continue(args)) {
			ssize_t n;

			n = write(fds[1], wr_buffer, BUFFER_SIZE);
			if (UNLIKELY(n < 0))
				break;
		}
		(void)close(fds[1]);
		_exit(1);
	}
	/* Parent */
	(void)close(fds[1]);
	fds[1] = -1;

	stress_set_proc_state(args->name, STRESS_STATE_SYNC_WAIT);
	stress_sync_start_wait(args);
	stress_set_proc_state(args->name, STRESS_STATE_RUN);

	if (stress_sighandler(args->name, SIGIO, stress_sigio_handler, NULL) < 0)
		goto reap;

	ret = fcntl(fds[0], F_SETFL, flags | O_ASYNC | O_NONBLOCK);
	if (ret < 0) {
		pr_err("%s: fcntl F_SETFL failed, errno=%d (%s)\n",
			args->name, errno, strerror(errno));
		goto reap;
	}
	t_start = stress_time_now();
	do {
		struct timeval timeout;

		timeout.tv_sec = 1;
		timeout.tv_usec = 0;

		(void)select(0, NULL, NULL, NULL, &timeout);
		if (got_err) {
			if (got_err != EINTR) {
				pr_fail("%s: read error, errno=%d (%s)\n",
					args->name, got_err, strerror(got_err));
				rc = EXIT_FAILURE;
			}
			break;
		}
	} while (stress_continue(args));

	t_delta = stress_time_now() - t_start;
	rate = (t_delta > 0.0) ? (double)async_sigs / t_delta : 0.0;
	stress_metrics_set(args, 0, "SIGIO signals per sec",
		rate, STRESS_METRIC_HARMONIC_MEAN);


finish:
	/*  And ignore IO signals from now on */
	VOID_RET(int, stress_sighandler(args->name, SIGIO, SIG_IGN, NULL));

reap:
	if (pid > 0)
		(void)stress_kill_pid_wait(pid, NULL);

err:
	if (flags != -1) {
		VOID_RET(int, fcntl(fds[0], F_SETFL, flags & ~(O_ASYNC | O_NONBLOCK)));
	}

	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);

	if (fds[0] != -1)
		(void)close(fds[0]);
	if (fds[1] != -1)
		(void)close(fds[1]);

	(void)munmap((void *)buffers, 2 * BUFFER_SIZE);

	return rc;
}

const stressor_info_t stress_sigio_info = {
	.stressor = stress_sigio,
	.classifier = CLASS_SIGNAL | CLASS_OS,
	.verify = VERIFY_ALWAYS,
	.help = help
};
#else
const stressor_info_t stress_sigio_info = {
	.stressor = stress_unimplemented,
	.classifier = CLASS_INTERRUPT | CLASS_OS,
	.verify = VERIFY_ALWAYS,
	.help = help,
	.unimplemented_reason = "built without fcntl() commands O_ASYNC, O_NONBLOCK, F_SETOWN, F_GETFL or F_SETFL"
};
#endif
