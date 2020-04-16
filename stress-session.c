/*
 * Copyright (C) 2013-2020 Canonical, Ltd.
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

#define STRESS_SESSION_SUCCESS		(0x00)
#define STRESS_SESSION_SETSID_FAILED	(0x10)
#define STRESS_SESSION_GETSID_FAILED	(0x11)
#define STRESS_SESSION_FORK_FAILED	(0x12)
#define STRESS_SESSION_WAITPID_FAILED	(0x13)

static const stress_help_t session_help[] = {
	{ "f N","session N",	 "start N workers that exercise new sessions" },
	{ NULL,	"session-ops N", "stop after N session bogo operations" },
	{ NULL,	NULL,		NULL }
};

static char *stress_session_error(int err)
{
	switch (err) {
	case STRESS_SESSION_SUCCESS:
		return "success";
	case STRESS_SESSION_SETSID_FAILED:
		return "setsid() failed";
	case STRESS_SESSION_GETSID_FAILED:
		return "getsid() failed";
	case STRESS_SESSION_FORK_FAILED:
		return "fork() failed";
	case STRESS_SESSION_WAITPID_FAILED:
		return "waitpid() failed";
	default:
		break;
	}
	return "unknown failure";
}

/*
 *  stress_session_set_and_get()
 *	set and get session id, simple sanity check
 */
static void stress_session_set_and_get(const stress_args_t *args)
{
	pid_t sid, gsid;

	sid = setsid();
	if (sid < 0) {
		pr_inf("%s: setsid failed: %d (%s)\n",
			args->name, errno, strerror(errno));
		_exit(STRESS_SESSION_SETSID_FAILED);
	}
	gsid = getsid(getpid());
	if (gsid < 0) {
		pr_inf("%s: getsid failed: %d (%s)\n",
			args->name, errno, strerror(errno));
		_exit(STRESS_SESSION_GETSID_FAILED);
	}
	if (gsid != sid) {
		pr_inf("%s getsid failed, got %d, expected %d\n",
			args->name, (int)gsid, (int)sid);
		_exit(STRESS_SESSION_GETSID_FAILED);
	}
}

/*
 *  stress_session_child()
 *	make grand child processes, 25% of which are
 *	orphaned for init to reap
 */
static int stress_session_child(const stress_args_t *args)
{
	pid_t pid;

	stress_session_set_and_get(args);

	pid = fork();
	if (pid < 0) {
		/* Silently ignore resource limitation failures */
		if ((errno == EAGAIN) || (errno == ENOMEM))
			_exit(STRESS_SESSION_SUCCESS);
		pr_err("%s: fork failed: errno=%d (%s)\n",
			args->name, errno, strerror(errno));
		_exit(STRESS_SESSION_FORK_FAILED);
	} else if (pid == 0) {
		stress_session_set_and_get(args);
		_exit(STRESS_SESSION_SUCCESS);
	} else {
		int status;

		/* 25% of calls will be ophans */
		stress_mwc_reseed();
		if (stress_mwc8() >= 64) {
			int ret;
#if defined(HAVE_WAIT4)
			struct rusage usage;

			if (stress_mwc1())
				ret = shim_wait4(pid, &status, 0, &usage);
			else
				ret = shim_waitpid(pid, &status, 0);
#else
			ret = shim_waitpid(pid, &status, 0);
#endif
			if (ret < 0)
				_exit(STRESS_SESSION_WAITPID_FAILED);
		}
	}
	_exit(STRESS_SESSION_SUCCESS);
}

/*
 *  stress_session()
 *	stress by process sessions
 */
static int stress_session(const stress_args_t *args)
{
	pid_t pid;

	while (keep_stressing()) {
		pid = fork();
		if (pid < 0) {
			if ((errno == EAGAIN) || (errno == ENOMEM))
				continue;
			pr_err("%s: fork failed: errno=%d (%s)\n",
				args->name, errno, strerror(errno));
			return EXIT_FAILURE;
		} else if (pid == 0) {
			/* Child */
			_exit(stress_session_child(args));
		} else {
			int status;

			(void)shim_waitpid(pid, &status, 0);
			if (WIFEXITED(status) &&
			   (WEXITSTATUS(status) != STRESS_SESSION_SUCCESS)) {
				pr_fail("%s: failure in child: %s\n", args->name,
					stress_session_error(WEXITSTATUS(status)));
			}
		}
		inc_counter(args);
	}
	return EXIT_SUCCESS;
}

stressor_info_t stress_session_info = {
	.stressor = stress_session,
	.class = CLASS_SCHEDULER | CLASS_OS,
	.help = session_help
};
