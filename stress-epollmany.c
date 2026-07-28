/*
 * Copyright (C) 2026      Colin Ian King.
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
#include "core-builtin.h"
#include "core-filesystem.h"
#include "core-killpid.h"
#include "core-net.h"
#include "core-pragma.h"

#define MAX_PIPE_FDS		(16)		/* max pipe file descriptors to use */

#define MIN_EPOLL_FDS		(1)		/* min epoll file descriptors */
#define MAX_EPOLL_FDS		(1000000)	/* max epoll file descriptors */
#define DEFAULT_EPOLL_FDS	(512)		/* default epoll file descriptors */

#include <time.h>

static const stress_help_t help[] = {
	{ NULL,	"epollmany N",      "start N workers doing epoll handled socket activity" },
	{ NULL, "epollmany-delete", "delete fds from epoll descriptors before close" },
	{ NULL, "epollmany-fds N",  "specify number of epoll_create fds to exercise" },
	{ NULL,	"epollmany-ops N",  "stop after N epoll bogo operations" },
	{ NULL,	NULL,               NULL },
};

static const stress_opt_t opts[] = {
	{ OPT_epollmany_delete, "epollmany-delete", TYPE_ID_BOOL, 0, 1, NULL },
	{ OPT_epollmany_fds,    "epollmany-fds",    TYPE_ID_INT, MIN_EPOLL_FDS, MAX_EPOLL_FDS, NULL },
	END_OPT,
};

#if defined(HAVE_SYS_EPOLL_H) &&	\
    defined(HAVE_EPOLL_CREATE) &&	\
    NEED_GLIBC(2,3,2)

#include <sys/epoll.h>

/*
 *  stress_epollmany
 *	stress epoll_create and epoll_ctl using pipe file
 *	descriptors and looped/stacked epoll file descriptors
 */
static int OPTIMIZE3 stress_epollmany(stress_args_t *args)
{
	struct epoll_event event;
	struct epoll_event events[MAX_EPOLL_FDS];
	int epollmany_fds = DEFAULT_EPOLL_FDS;
	int pipe_fds;
	int max_add[MAX_EPOLL_FDS];
	int efds[MAX_EPOLL_FDS];
	int pfds[MAX_PIPE_FDS][2];
	int rc = EXIT_SUCCESS;
	int i;
	static char data[1] = { 0xff };
	uint64_t epoll_create_count = 0;
	uint64_t epoll_ctl_add_count = 0;
	uint64_t epoll_event_count = 0;
	double duration;
	double rate;
	double t;
	bool epollmany_fds_specified = false;
	bool epollmany_delete = false;

	(void)stress_setting_get("epollmany-delete", &epollmany_delete);
	if (stress_setting_get("epollmany-fds", &epollmany_fds))
		epollmany_fds_specified = true;

	for (pipe_fds = 0; pipe_fds < MAX_PIPE_FDS; pipe_fds++) {
		if (UNLIKELY(pipe(pfds[pipe_fds]) < 0))
			break;
	}

	if (pipe_fds == 0) {
		pr_inf_skip("%s: failed to create a pipe, errno=%d (%s), skipping stressor\n",
			args->name, errno, strerror(errno));
		return EXIT_NO_RESOURCE;
	}

	(void)shim_memset(&events, 0, sizeof(events));
	(void)shim_memset(&event, 0, sizeof(event));

	for (i = 0; i < MAX_EPOLL_FDS; i++)
		efds[i] = -1;

	stress_proc_state_set(args->name, STRESS_STATE_SYNC_WAIT);
	stress_sync_start_wait(args);
	stress_proc_state_set(args->name, STRESS_STATE_RUN);

	t = stress_time_now();
	do {
		int n;
		int j;
		double t_end;

		t_end = stress_time_now() + 1.0;
		for (n = 0; stress_continue(args) && (n < epollmany_fds); n++) {
			efds[n] = epoll_create1(0);
			if (UNLIKELY(efds[n] < 0))
				break;
			max_add[n] = 0;

			/* Phase 1, add pipe fds */
			for (j = 0; j < pipe_fds; j++) {
				event.data.fd = pfds[j][0];
				event.events = EPOLLIN | EPOLLOUT;

				if (LIKELY(epoll_ctl(efds[n], EPOLL_CTL_ADD, pfds[j][0], &event) == 0))
					max_add[n] = j;
				else
					break;
			}
			epoll_ctl_add_count += j;

			/* Phase 2, add up to n existing efds for loop/stacked usage */
			for (j = 0; j < n; j++) {
				event.data.fd = efds[j];
				event.events = EPOLLIN | EPOLLOUT;

				/* add existing epoll fds, ignore EINVAL errors */
				if (LIKELY(epoll_ctl(efds[n], EPOLL_CTL_ADD, efds[j], &event) == 0))
					max_add[n] = j;
				else if (errno == ELOOP)
					break;
			}
			epoll_ctl_add_count += j;
			stress_bogo_inc(args);

			/*
			 *  user has not specified an upper limit, so have we hit a time
			 *  out in creating epoll efds and adding fds to it?
			 */
			if ((!epollmany_fds_specified) && (stress_time_now() > t_end))
				break;
		}
		epoll_create_count += n;

		for (i = 0; i < pipe_fds; i++) {
			if (UNLIKELY(write(pfds[i][1], data, sizeof(data)) < 0)) {
				pr_inf("%s: write to pipe failed, errno=%d (%s)\n", args->name, errno, strerror(errno));
				rc = EXIT_FAILURE;
				goto epollmany_end;
			}
		}

		for (i = 0; i < n; i++) {
			register int ret;

			ret = epoll_wait(efds[i], events, SIZEOF_ARRAY(events), 0);
			if (LIKELY(ret > 0))
				epoll_event_count += ret;
		}

		for (i = 0; i < pipe_fds; i++)
			VOID_RET(ssize_t, read(pfds[i][0], data, sizeof(data)));

		/*
		 *  If user requested to delete the file descriptors then
		 *  do so. Normally these are auto-reaped when the efds are closed
		 */
		if (epollmany_delete) {
			for (i = 0; i < n; i++) {
				for (j = 0; j < pipe_fds; j++) {
					event.data.fd = pfds[j][0];
					event.events = EPOLLIN | EPOLLOUT;
					(void)epoll_ctl(efds[n], EPOLL_CTL_DEL, pfds[j][0], &event);

				}
				for (j = 0; j < max_add[n] ; j++) {
					event.data.fd = efds[j];
					event.events = EPOLLIN | EPOLLOUT;
					(void)epoll_ctl(efds[n], EPOLL_CTL_DEL, efds[j], &event);
				}
			}
		}
		stress_fs_close_fds(efds, n);
	} while (stress_continue(args));

epollmany_end:
	duration = stress_time_now() - t;

	stress_proc_state_set(args->name, STRESS_STATE_DEINIT);

	for (i = 0; i < pipe_fds; i++) {
		(void)close(pfds[i][0]);
		(void)close(pfds[i][1]);
	}

	rate = (duration > 0.0) ? (double)epoll_create_count / duration : 0.0;
	stress_metrics_set(args, "epoll_create1() calls per sec", rate, STRESS_METRIC_HARMONIC_MEAN);
	rate = (duration > 0.0) ? (double)epoll_ctl_add_count / duration : 0.0;
	stress_metrics_set(args, "epoll_ctl EPOLL_CTL_ADD calls per sec", rate, STRESS_METRIC_HARMONIC_MEAN);
	rate = (duration > 0.0) ? (double)epoll_event_count / duration : 0.0;
	stress_metrics_set(args, "epoll_wait events per sec", rate, STRESS_METRIC_HARMONIC_MEAN);

	return rc;
}

static const stress_exercises_t exercises[] = {
	STRESS_EX_FEATURE("lock-contention"),

	STRESS_EX_SYSCALL("epoll_create1"),
	STRESS_EX_SYSCALL("epoll_ctl"),

	STRESS_EX_END,
};

const stressor_info_t stress_epollmany_info = {
	.stressor = stress_epollmany,
	.classifier = CLASS_NETWORK | CLASS_OS,
	.verify = VERIFY_ALWAYS,
	.help = help,
	.opts = opts,
	.exercises = exercises,
};

#else
const stressor_info_t stress_epollmany_info = {
	.stressor = stress_unimplemented,
	.classifier = CLASS_NETWORK | CLASS_OS,
	.verify = VERIFY_ALWAYS,
	.help = help,
	.opts = opts,
	.unimplemented_reason = "built without sys/epoll.h or librt or timer support"
};
#endif
