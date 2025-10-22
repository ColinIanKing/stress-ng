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
#include "core-capabilities.h"

#if defined(NSIG)
#define MAX_SIGNUM      NSIG
#elif defined(_NSIG)
#define MAX_SIGNUM      _NSIG
#else
#define MAX_SIGNUM      (256)
#endif

#define MAX_BACKOFF	(10000)

static const stress_help_t help[] = {
	{ NULL,	"daemon N",	"start N workers creating multiple daemons" },
	{ NULL,	"daemon-ops N",	"stop when N daemons have been created" },
	{ NULL, "daemon-wait",	"stressor wait for daemon to exit and not init" },
	{ NULL,	NULL,		NULL }
};

/*
 *  daemon_wait_pid()
 *	waits for child if daemon_wait is set, otherwise let init do it
 */
static void daemon_wait_pid(const pid_t pid, const bool daemon_wait)
{
	if (daemon_wait) {
		int status;

		VOID_RET(pid_t, waitpid(pid, &status, 0));
	}
}

/*
 *  stress_make_daemon()
 *	fork off a child and let the parent die
 */
static int stress_make_daemon(
	stress_args_t *args,
	const int fd,
	const bool daemon_wait)
{
	int fds[3];
	int i;
	sigset_t set;
	uint64_t backoff = 100;
	ssize_t sz;
	int rc = EXIT_SUCCESS;

	if (stress_sig_stop_stressing(args->name, SIGALRM) < 0)
		goto err_close_fd;
	if (setsid() < 0) {
		if (errno != ENOSYS) {
			pr_fail("%s: setsid failed, errno=%d (%s)\n", args->name,
				errno, strerror(errno));
			rc = EXIT_FAILURE;

			sz = write(fd, &rc, sizeof(rc));
			if (sz != sizeof(rc))
				goto err_close_fd;
		}
		goto err_close_fd;
	}

	(void)close(0);
	(void)close(1);
	(void)close(2);

	for (i = 0; i < MAX_SIGNUM; i++)
		(void)signal(i, SIG_DFL);

	(void)sigemptyset(&set);
	(void)sigprocmask(SIG_SETMASK, &set, NULL);
#if defined(HAVE_CLEARENV)
	(void)clearenv();
#endif

	/*
	 *  The following calls may fail if we are low on
	 *  file descriptors or memory, silently ignore
	 *  these so we can re-try. We can't report them
	 *  as stdout/stderr are now closed
	 */
	if ((fds[0] = open("/dev/null", O_RDWR)) < 0) {
		goto err_close_fd;
	}
	if ((fds[1] = dup(0)) < 0)
		goto err_close_fds0;
	if ((fds[2] = dup(0)) < 0)
		goto err_close_fds1;

	while (stress_continue_flag()) {
		pid_t pid;

		pid = fork();
		if (pid < 0) {
			/* A slow init? no pids or memory, retry */
			if ((errno == EAGAIN) || (errno == ENOMEM)) {
				/* Minor backoff before retrying */
				(void)shim_usleep_interruptible(backoff);
				backoff += 100;
				if (backoff > MAX_BACKOFF)
					backoff = MAX_BACKOFF;
				continue;
			}
			goto err_close_fds2;
		} else if (pid == 0) {
			/* Child */
			if (chdir("/") < 0)
				goto err_close_fds2;
			(void)umask(0);
			VOID_RET(int, stress_drop_capabilities(args->name));
			stress_set_proc_state(args->name, STRESS_STATE_RUN);

			sz = write(fd, &rc, sizeof(rc));
			if (sz != sizeof(rc))
				goto err_close_fds2;
		} else {
			/* Parent, will be reaped by init unless daemon_wait is true */
			daemon_wait_pid(pid, daemon_wait);
			break;
		}
	}

err_close_fds2:
	(void)close(fds[2]);
err_close_fds1:
	(void)close(fds[1]);
err_close_fds0:
	(void)close(fds[0]);
err_close_fd:
	(void)close(fd);

	return rc;
}

/*
 *  stress_daemon()
 *	stress by multiple daemonizing forks
 */
static int stress_daemon(stress_args_t *args)
{
	int fds[2], rc = EXIT_SUCCESS;
	pid_t pid;
	bool daemon_wait = false;

	(void)stress_get_setting("daemon-wait", &daemon_wait);

	if (stress_sig_stop_stressing(args->name, SIGALRM) < 0)
		return EXIT_FAILURE;

	if (pipe(fds) < 0) {
		pr_fail("%s: pipe failed, errno=%d (%s)\n",
			args->name, errno, strerror(errno));
		return EXIT_FAILURE;
	}

	stress_set_proc_state(args->name, STRESS_STATE_SYNC_WAIT);
	stress_sync_start_wait(args);
	stress_set_proc_state(args->name, STRESS_STATE_RUN);
again:
	pid = fork();
	if (pid < 0) {
		if (stress_redo_fork(args, errno))
			goto again;
		if (UNLIKELY(!stress_continue(args))) {
			(void)close(fds[0]);
			(void)close(fds[1]);
			goto finish;
		}
		pr_fail("%s: fork failed, errno=%d (%s)\n",
			args->name, errno, strerror(errno));
		(void)close(fds[0]);
		(void)close(fds[1]);
		return EXIT_FAILURE;
	} else if (pid == 0) {
		/* Children */
		(void)close(fds[0]);
		rc = stress_make_daemon(args, fds[1], daemon_wait);
		shim_exit_group(rc);
	} else {
		/* Parent */
		(void)close(fds[1]);
		do {
			ssize_t n;

			n = read(fds[0], &rc, sizeof(rc));
			if (n < (ssize_t)sizeof(rc)) {
				if (errno != EINTR) {
					pr_dbg("%s: read failed, "
						"errno=%d (%s)\n",
						args->name,
						errno, strerror(errno));
				}
				break;
			} else {
				if (rc != EXIT_SUCCESS)
					break;
			}
			stress_bogo_inc(args);
		} while (stress_continue(args));
		(void)close(fds[0]);

		daemon_wait_pid(pid, daemon_wait);
	}

finish:
	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);

	return rc;
}

static const stress_opt_t opts[] = {
	{ OPT_daemon_wait, "daemon-wait", TYPE_ID_BOOL, 0, 1, NULL },
	END_OPT,
};

const stressor_info_t stress_daemon_info = {
	.stressor = stress_daemon,
	.classifier = CLASS_SCHEDULER | CLASS_OS,
	.opts = opts,
	.verify = VERIFY_ALWAYS,
	.help = help
};
