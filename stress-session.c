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
#include "core-attribute.h"
#include "core-builtin.h"

#define STRESS_SESSION_SUCCESS		(0x00)
#define STRESS_SESSION_SETSID_FAILED	(0x10)
#define STRESS_SESSION_GETSID_FAILED	(0x11)
#define STRESS_SESSION_WRONGSID_FAILED	(0x12)
#define STRESS_SESSION_FORK_FAILED	(0x13)
#define STRESS_SESSION_WAITPID_FAILED	(0x14)

static const stress_help_t session_help[] = {
	{ NULL, "session N",	 "start N workers that exercise new sessions" },
	{ NULL,	"session-ops N", "stop after N session bogo operations" },
	{ NULL,	NULL,		NULL }
};

typedef struct {
	int	status;		/* session status error */
	int	err;		/* copy of errno */
} session_error_t;

static char * CONST stress_session_error(const int err)
{
	switch (err) {
	case STRESS_SESSION_SUCCESS:
		return "success";
	case STRESS_SESSION_SETSID_FAILED:
		return "setsid() failed";
	case STRESS_SESSION_GETSID_FAILED:
		return "getsid() failed";
	case STRESS_SESSION_WRONGSID_FAILED:
		return "getsid() returned incorrect session id";
	case STRESS_SESSION_FORK_FAILED:
		return "fork() failed";
	case STRESS_SESSION_WAITPID_FAILED:
		return "waitpid() failed";
	default:
		break;
	}
	return "unknown failure";
}

static ssize_t stress_session_return_status(const int fd, const int err, const int status)
{
	session_error_t error;
	ssize_t n;
	const int saved_errno = errno;

	error.err = err;
	error.status = status;

	n = write(fd, &error, sizeof(error));

	errno = saved_errno;	/* Restore errno */

	return n;
}

/*
 *  stress_session_set_and_get()
 *	set and get session id, simple sanity check
 */
static int stress_session_set_and_get(stress_args_t *args, const int fd)
{
	pid_t sid, gsid;

	sid = setsid();
	if (UNLIKELY(sid == (pid_t)-1)) {
		stress_session_return_status(fd, errno, STRESS_SESSION_SETSID_FAILED);
		pr_inf("%s: setsid failed, errno=%d (%s)\n",
			args->name, errno, strerror(errno));
		return STRESS_SESSION_SETSID_FAILED;
	}

	gsid = getsid(getpid());
	if (UNLIKELY(gsid == (pid_t)-1)) {
		stress_session_return_status(fd, errno, STRESS_SESSION_GETSID_FAILED);
		pr_inf("%s: getsid failed, errno=%d (%s)\n",
			args->name, errno, strerror(errno));
		return STRESS_SESSION_GETSID_FAILED;
	}

	if (UNLIKELY(gsid != sid)) {
		stress_session_return_status(fd, errno, STRESS_SESSION_WRONGSID_FAILED);
		pr_inf("%s getsid failed, got session ID %d, expected %d\n",
			args->name, (int)gsid, (int)sid);
		return STRESS_SESSION_WRONGSID_FAILED;
	}
	return STRESS_SESSION_SUCCESS;
}

/*
 *  stress_session_child()
 *	make grand child processes, 25% of which are
 *	orphaned for init to reap
 */
static int stress_session_child(stress_args_t *args, const int fd)
{
	pid_t pid;
	int ret;

	ret = stress_session_set_and_get(args, fd);
	if (ret != STRESS_SESSION_SUCCESS)
		return ret;

	pid = fork();
	if (pid < 0) {
		/* Silently ignore resource limitation failures */
		if ((errno == EAGAIN) || (errno == ENOMEM)) {
			stress_session_return_status(fd, 0, STRESS_SESSION_SUCCESS);
			return STRESS_SESSION_SUCCESS;
		}
		stress_session_return_status(fd, errno, STRESS_SESSION_FORK_FAILED);
		pr_err("%s: fork failed, errno=%d (%s)\n",
			args->name, errno, strerror(errno));
		return STRESS_SESSION_FORK_FAILED;
	} else if (pid == 0) {
		stress_set_proc_state(args->name, STRESS_STATE_RUN);
		stress_session_set_and_get(args, fd);
		(void)shim_vhangup();
		stress_session_return_status(fd, 0, STRESS_SESSION_SUCCESS);
		return STRESS_SESSION_SUCCESS;
	} else {
		int status;

		/* 25% of calls will be orphans */
		stress_mwc_reseed();
		if (stress_mwc8() >= 64) {
			pid_t wret;
#if defined(HAVE_WAIT4)
			struct rusage usage;

			if (stress_mwc1())
				wret = shim_wait4(pid, &status, 0, &usage);
			else
				wret = shim_waitpid(pid, &status, 0);
#else
			wret = shim_waitpid(pid, &status, 0);
#endif
			if (wret < 0) {
				if (UNLIKELY((errno != EINTR) && (errno == ECHILD))) {
					stress_session_return_status(fd, errno, STRESS_SESSION_WAITPID_FAILED);
					return STRESS_SESSION_WAITPID_FAILED;
				}
			}
		}
	}
	stress_session_return_status(fd, 0, STRESS_SESSION_SUCCESS);

	return STRESS_SESSION_SUCCESS;
}

/*
 *  stress_session()
 *	stress by process sessions
 */
static int stress_session(stress_args_t *args)
{
	int fds[2];
	int rc = EXIT_SUCCESS;

	if (pipe(fds) < 0) {
		pr_inf("%s: pipe failed, errno=%d (%s)\n",
			args->name, errno, strerror(errno));
		return EXIT_NO_RESOURCE;
	}

	stress_set_proc_state(args->name, STRESS_STATE_SYNC_WAIT);
	stress_sync_start_wait(args);
	stress_set_proc_state(args->name, STRESS_STATE_RUN);

	while (stress_continue(args)) {
		pid_t pid;

		pid = fork();
		if (pid < 0) {
			if ((errno == EAGAIN) || (errno == ENOMEM))
				continue;
			pr_inf("%s: fork failed, errno=%d (%s)\n",
				args->name, errno, strerror(errno));
			(void)close(fds[0]);
			(void)close(fds[1]);
			return EXIT_NO_RESOURCE;
		} else if (pid == 0) {
			/* Child */
			int ret;

			stress_set_proc_state(args->name, STRESS_STATE_RUN);
			(void)close(fds[0]);
			ret = stress_session_child(args, fds[1]);
			(void)close(fds[1]);
			_exit(ret);
		} else {
			int status;
			ssize_t n;
			session_error_t error;

			(void)shim_memset(&error, 0, sizeof(error));
			n = read(fds[0], &error, sizeof(error));

			(void)shim_waitpid(pid, &status, 0);
			if (WIFEXITED(status) &&
			   (WEXITSTATUS(status) != STRESS_SESSION_SUCCESS)) {
				if ((n < (ssize_t)sizeof(error)) ||
				   ((n == (ssize_t)sizeof(error)) && (error.err == 0))) {
					pr_fail("%s: failure in child, %s\n", args->name,
						stress_session_error(WEXITSTATUS(status)));
					rc = EXIT_FAILURE;
				} else {
					pr_fail("%s: failure in child, %s, errno=%d (%s)\n",
						args->name,
						stress_session_error(error.status),
						error.err, strerror(error.err));
					rc = EXIT_FAILURE;
				}
			}
		}
		stress_bogo_inc(args);
	}
	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);
	(void)close(fds[0]);
	(void)close(fds[1]);

	return rc;
}

const stressor_info_t stress_session_info = {
	.stressor = stress_session,
	.classifier = CLASS_SCHEDULER | CLASS_OS,
	.verify = VERIFY_ALWAYS,
	.help = session_help
};
