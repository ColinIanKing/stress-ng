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

#include <sys/ioctl.h>

#if defined(HAVE_SYS_TIMERFD_H)
#include <sys/timerfd.h>
#else
UNEXPECTED
#endif

#if defined(HAVE_POLL_H)
#include <poll.h>
#else
UNEXPECTED
#endif

#define MIN_TIMERFD_FREQ	(1)
#define MAX_TIMERFD_FREQ	(100000000)
#define DEFAULT_TIMERFD_FREQ	(1000000)

#if !defined(TFD_IOC_SET_TICKS) &&	\
    defined(_IOW) &&			\
    defined(__linux__)
#define TFD_IOC_SET_TICKS	_IOW('T', 0, uint64_t)
#endif

static const stress_help_t help[] = {
	{ NULL,	"timerfd N",	  "start N workers producing timerfd events" },
	{ NULL, "timerfd-fds N",  "number of timerfd file descriptors to open" },
	{ NULL,	"timerfd-freq F", "run timer(s) at F Hz, range 1 to 1000000000" },
	{ NULL,	"timerfd-ops N",  "stop after N timerfd bogo events" },
	{ NULL,	"timerfd-rand",	  "enable random timerfd frequency" },
	{ NULL,	NULL,		  NULL }
};

#define COUNT_MAX		(256)
#if defined(HAVE_POLL_H) &&	\
    defined(HAVE_POLL)
#define TIMER_FDS_MAX		(INT_MAX)
#define USE_POLL		(1)
#elif defined(HAVE_SELECT)
#define USE_SELECT		(1)
#define TIMER_FDS_MAX		(FD_SETSIZE)
#else
#define TIMER_FDS_MAX		(INT_MAX)
#endif
#define TIMER_FDS_DEFAULT	STRESS_MINIMUM(1024, TIMER_FDS_MAX)

static const stress_opt_t opts[] = {
	{ OPT_timerfd_fds,  "timerfd-fds",  TYPE_ID_INT, 1, TIMER_FDS_MAX, NULL },
	{ OPT_timerfd_freq, "timerfd-freq", TYPE_ID_UINT64, MIN_TIMERFD_FREQ, MAX_TIMERFD_FREQ, NULL },
	{ OPT_timerfd_rand, "timerfd-rand", TYPE_ID_BOOL, 0, 1, NULL },
	END_OPT,
};

#if defined(HAVE_SYS_TIMERFD_H) &&	\
    defined(HAVE_TIMERFD_CREATE) &&	\
    defined(HAVE_TIMERFD_GETTIME) &&	\
    defined(HAVE_TIMERFD_SETTIME) &&	\
    (defined(USE_SELECT) || defined(USE_POLL))

static double rate_ns;

/*
 *  stress_timerfd_set()
 *	set timerfd, ensure it is never zero
 */
static void stress_timerfd_set(
	struct itimerspec *timer,
	const bool timerfd_rand)
{
	double rate;

	if (timerfd_rand) {
		/* Mix in some random variation */
		const double r = ((double)(stress_mwc32modn(10000)) - 5000.0) / 40000.0;
		rate = rate_ns + (rate_ns * r);
	} else {
		rate = rate_ns;
	}

	timer->it_value.tv_sec = (time_t)rate / STRESS_NANOSECOND;
	timer->it_value.tv_nsec = (suseconds_t)rate % STRESS_NANOSECOND;

	if (timer->it_value.tv_sec == 0 &&
	    timer->it_value.tv_nsec < 1)
		timer->it_value.tv_nsec = 1;

	timer->it_interval.tv_sec = timer->it_value.tv_sec;
	timer->it_interval.tv_nsec = timer->it_value.tv_nsec;
}

/*
 *  stress_timerfd
 *	stress timerfd
 */
static int stress_timerfd(stress_args_t *args)
{
	struct itimerspec timer;
	uint64_t timerfd_freq = DEFAULT_TIMERFD_FREQ;
	int timerfd_fds = TIMER_FDS_DEFAULT;
	int count = 0, i, max_timerfd = -1;
	bool timerfd_rand = false;
	int file_fd;
	char file_fd_name[PATH_MAX];
#if defined(CLOCK_BOOTTIME_ALARM)
	const bool cap_wake_alarm = stress_check_capability(SHIM_CAP_WAKE_ALARM);
#endif
	const int bad_fd = stress_get_bad_fd();
	const pid_t self = getpid();
	int *timerfds;
	int ret, rc = EXIT_SUCCESS;
#if defined(USE_POLL)
	struct pollfd *pollfds;
#endif

	(void)stress_get_setting("timerfd-rand", &timerfd_rand);
	(void)stress_get_setting("timerfd-fds", &timerfd_fds);

	if (!stress_get_setting("timerfd-freq", &timerfd_freq)) {
		if (g_opt_flags & OPT_FLAGS_MAXIMIZE)
			timerfd_freq = MAX_TIMERFD_FREQ;
		if (g_opt_flags & OPT_FLAGS_MINIMIZE)
			timerfd_freq = MIN_TIMERFD_FREQ;
	}
	rate_ns = timerfd_freq ? (double)STRESS_NANOSECOND / (double)timerfd_freq :
				 (double)STRESS_NANOSECOND;

	/* Create a non valid timerfd file descriptor */
	ret = stress_temp_dir_mk_args(args);
	if (ret < 0) {
		rc = stress_exit_status(-ret);
		goto dir_rm;
	}
	(void)stress_temp_filename_args(args, file_fd_name, sizeof(file_fd_name), stress_mwc32());
	file_fd = open(file_fd_name, O_CREAT | O_RDWR, S_IRUSR | S_IWUSR);
	if (file_fd < 0) {
		pr_err("%s: cannot create %s\n", args->name, file_fd_name);
		rc = stress_exit_status(errno);
		goto close_file_fd;
	}
	(void)shim_unlink(file_fd_name);

	timerfds = (int *)calloc((size_t)timerfd_fds, sizeof(*timerfds));
	if (!timerfds) {
		pr_inf_skip("%s: failed to allocate %" PRIu32 " timerfd file descriptors%s, "
			"skipping stressor\n", args->name,
			timerfd_fds, stress_get_memfree_str());
		rc = EXIT_NO_RESOURCE;
		goto close_file_fd;
	}
	for (i = 0; i < timerfd_fds; i++)
		timerfds[i] = -1;

#if defined(USE_POLL)
	pollfds = (struct pollfd *)calloc((size_t)timerfd_fds, sizeof(*pollfds));
	if (!pollfds) {
		pr_inf_skip("%s: failed to allocate %" PRIu32 " pollfd file descriptors%s, "
			"skipping stressor\n", args->name,
			timerfd_fds, stress_get_memfree_str());
		rc = EXIT_NO_RESOURCE;
		goto free_timerfds;
	}
#endif

	for (i = 0; i < timerfd_fds; i++) {
#if defined(USE_SELECT)
		/* In select mode we must never exceed the FD_SETSIZE */
		if (max_timerfd >= FD_SETSIZE - 1)
			continue;
#endif
		timerfds[i] = timerfd_create(CLOCK_REALTIME, 0);
		if (timerfds[i] < 0) {
			if ((errno != EMFILE) &&
			    (errno != ENFILE) &&
			    (errno != ENOMEM)) {
				pr_fail("%s: timerfd_create failed, errno=%d (%s)\n",
					args->name, errno, strerror(errno));
				rc = EXIT_FAILURE;
				goto close_timer_fds;
			}
		} else {
			count++;
			if (max_timerfd < timerfds[i])
				max_timerfd = timerfds[i];
		}
	}

#if defined(CLOCK_REALTIME_ALARM)
	/* Check timerfd_create cannot succeed without capability */
	if (!cap_wake_alarm) {
		ret = timerfd_create(CLOCK_REALTIME_ALARM, 0);
		if (ret >= 0) {
#if 0
			pr_fail("%s: timerfd_create without capability CAP_WAKE_ALARM unexpectedly "
					"succeeded, errno=%d (%s)\n", args->name, errno, strerror(errno));
#endif
			(void)close(ret);
		}
	}
#endif

	stress_set_proc_state(args->name, STRESS_STATE_SYNC_WAIT);
	stress_sync_start_wait(args);
	stress_set_proc_state(args->name, STRESS_STATE_RUN);

#if defined(CLOCK_REALTIME)
	/* Exercise timerfd_create with invalid flags */
	ret = timerfd_create(CLOCK_REALTIME, ~0);
	if (ret >= 0)
		(void)close(ret);
#endif

	if (count == 0) {
		pr_fail("%s: timerfd_create failed, no timers created\n", args->name);
		rc = EXIT_FAILURE;
		goto free_pollfds;
	}
	count = 0;

	stress_timerfd_set(&timer, timerfd_rand);
	for (i = 0; i < timerfd_fds; i++) {
		if (timerfds[i] < 0)
			continue;
		if (timerfd_settime(timerfds[i], 0, &timer, NULL) < 0) {
			pr_fail("%s: timerfd_settime failed on fd %d, errno=%d (%s)\n",
				args->name, timerfds[i], errno, strerror(errno));
			rc = EXIT_FAILURE;
			goto free_pollfds;
		}
	}

	do {
		uint64_t expval;
		struct itimerspec value;
#if defined(USE_SELECT)
		struct timeval timeout;
		fd_set rdfs;
#endif
#if defined(USE_POLL)
		int j;
#endif

#if defined(USE_SELECT)
		FD_ZERO(&rdfs);
		for (i = 0; i < timerfd_fds; i++) {
			if (LIKELY(timerfds[i] >= 0))
				FD_SET(timerfds[i], &rdfs);
		}
		timeout.tv_sec = 0;
		timeout.tv_usec = 500000;
#endif
#if defined(USE_POLL)
		for (i = 0, j = 0; i < timerfd_fds; i++) {
			if (LIKELY(timerfds[i] >= 0)) {
				pollfds[j].fd = timerfds[i];
				pollfds[j].events = POLLIN;
				pollfds[j].revents = 0;
				j++;
			}
		}
#endif

		if (UNLIKELY(!stress_continue_flag()))
			break;
#if defined(USE_SELECT)
		ret = select(max_timerfd + 1, &rdfs, NULL, NULL, &timeout);
		if (UNLIKELY(ret < 0)) {
			if (errno == EINTR)
				continue;
			pr_fail("%s: select failed, errno=%d (%s)\n",
				args->name, errno, strerror(errno));
			break;
		}
#endif
#if defined(USE_POLL)
		ret = poll(pollfds, (nfds_t)j, 0);
		if (UNLIKELY(ret < 0)) {
			if (errno == EINTR)
				continue;
			pr_fail("%s: poll failed, errno=%d (%s)\n",
				args->name, errno, strerror(errno));
			break;
		}
#endif
		if (UNLIKELY(ret < 1))
			continue; /* Timeout */

		for (i = 0; i < timerfd_fds; i++) {
			ssize_t rret;

			if (timerfds[i] < 0)
				continue;
#if defined(USE_SELECT)
			if (!FD_ISSET(timerfds[i], &rdfs))
				continue;
			rret = read(timerfds[i], &expval, sizeof expval);
#endif
#if defined(USE_POLL)
			if (pollfds[i].revents != POLLIN)
				continue;
			rret = read(pollfds[i].fd, &expval, sizeof expval);
#endif

			if (UNLIKELY(rret < 0)) {
				pr_fail("%s: read of timerfd failed, errno=%d (%s)\n",
					args->name, errno, strerror(errno));
				break;
			}
			if (UNLIKELY(timerfd_gettime(timerfds[i], &value) < 0)) {
				pr_fail("%s: timerfd_gettime failed, errno=%d (%s)\n",
					args->name, errno, strerror(errno));
				break;
			}
			if (timerfd_rand) {
				stress_timerfd_set(&timer, timerfd_rand);
				if (UNLIKELY(timerfd_settime(timerfds[i], 0, &timer, NULL) < 0)) {
					pr_fail("%s: timerfd_settime failed, errno=%d (%s)\n",
						args->name, errno, strerror(errno));
					break;
				}
			}
			stress_bogo_inc(args);
		}

		/* Exercise invalid timerfd_gettime syscalls on bad fd */
		VOID_RET(int, timerfd_gettime(bad_fd, &value));

		VOID_RET(int, timerfd_gettime(file_fd, &value));

		/* Exercise invalid timerfd_settime syscalls on bad fd */
		VOID_RET(int, timerfd_settime(bad_fd, 0, &timer, NULL));

		VOID_RET(int, timerfd_settime(file_fd, 0, &timer, NULL));

		/* Exercise timerfd_settime with invalid flags */
		VOID_RET(int, timerfd_settime(bad_fd, ~0, &timer, NULL));

#if defined(HAVE_SYS_TIMERFD_H) &&	\
    defined(TFD_IOC_SET_TICKS)
		/* Exercise timer tick setting ioctl */
		{
			unsigned long int arg = 1ULL;

			VOID_RET(int, ioctl(timerfds[0], TFD_IOC_SET_TICKS, &arg));
		}
#endif

		/*
		 *  Periodically read /proc/$pid/fdinfo/timerfd[0],
		 *  we don't care about failures, we just want to
		 *  exercise this interface
		 */
		if (UNLIKELY(count++ >= COUNT_MAX)) {
			(void)stress_read_fdinfo(self, timerfds[0]);
			count = 0;
		}
	} while (stress_continue(args));

	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);


close_timer_fds:
	for (i = 0; i < timerfd_fds; i++) {
		if (timerfds[i] >= 0)
			(void)close(timerfds[i]);
	}

free_pollfds:
#if defined(USE_POLL)
	free(pollfds);
free_timerfds:
#endif
	free(timerfds);

close_file_fd:
	if (file_fd >= 0)
		(void)close(file_fd);
dir_rm:
	(void)stress_temp_dir_rm_args(args);

	return rc;
}

const stressor_info_t stress_timerfd_info = {
	.stressor = stress_timerfd,
	.classifier = CLASS_INTERRUPT | CLASS_OS,
	.verify = VERIFY_ALWAYS,
	.opts = opts,
	.help = help
};
#else
const stressor_info_t stress_timerfd_info = {
	.stressor = stress_unimplemented,
	.classifier = CLASS_INTERRUPT | CLASS_OS,
	.opts = opts,
	.verify = VERIFY_ALWAYS,
	.help = help,
	.unimplemented_reason = "built without sys/timerfd.h, timerfd_create(), timerfd_settime(), timerfd_setime, select() or poll()"
};
#endif
