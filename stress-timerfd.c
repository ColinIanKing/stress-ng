// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2013-2021 Canonical, Ltd.
 * Copyright (C) 2022-2023 Colin Ian King.
 *
 */
#include "stress-ng.h"
#include "core-capabilities.h"

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
	{ NULL, "timerfd-fds N", "number of timerfd file descriptors to open" },
	{ NULL,	"timerfd-freq F", "run timer(s) at F Hz, range 1 to 1000000000" },
	{ NULL,	"timerfd-ops N",  "stop after N timerfd bogo events" },
	{ NULL,	"timerfd-rand",	  "enable random timerfd frequency" },
	{ NULL,	NULL,		  NULL }
};

#define COUNT_MAX		(256)
#if defined(HAVE_POLL_H)
#define TIMER_FDS_MAX		(INT_MAX)
#define USE_POLL		(1)
#elif defined(HAVE_SELECT)
#define USE_SELECT		(1)
#define TIMER_FDS_MAX		(FD_SETSIZE)
#else
#define TIMER_FDS_MAX		(INT_MAX)
#endif
#define TIMER_FDS_DEFAULT	STRESS_MINIMUM(1024, TIMER_FDS_MAX)

/*
 *  stress_set_timerfd_fds()
 *	set maximum number of timerfd file descriptors to use
 */
static int stress_set_timerfd_fds(const char *opt)
{
	int timerfd_fds;

	timerfd_fds = (int)stress_get_uint32(opt);
	stress_check_range("timerfd-fds", (uint64_t)timerfd_fds, 1, TIMER_FDS_MAX);
	return stress_set_setting("timerfd-fds", TYPE_ID_INT, &timerfd_fds);
}

/*
 *  stress_set_timerfd_freq()
 *	set timer frequency from given option
 */
static int stress_set_timerfd_freq(const char *opt)
{
	uint64_t timerfd_freq;

	timerfd_freq = stress_get_uint64(opt);
	stress_check_range("timerfd-freq", timerfd_freq,
		MIN_TIMERFD_FREQ, MAX_TIMERFD_FREQ);
	return stress_set_setting("timerfd-freq", TYPE_ID_UINT64, &timerfd_freq);
}

static int stress_set_timerfd_rand(const char *opt)
{
	return stress_set_setting_true("timerfd-rand", opt);
}

static const stress_opt_set_func_t opt_set_funcs[] = {
	{ OPT_timerfd_fds,	stress_set_timerfd_fds },
	{ OPT_timerfd_freq,	stress_set_timerfd_freq },
	{ OPT_timerfd_rand,	stress_set_timerfd_rand },
	{ 0,			NULL }
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
static int stress_timerfd(const stress_args_t *args)
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

	timerfds = calloc((size_t)timerfd_fds, sizeof(*timerfds));
	if (!timerfds) {
		pr_inf_skip("%s: cannot allocate %" PRIu32 " timerfd file descriptors, "
			"skipping stressor\n", args->name, timerfd_fds);
		return EXIT_NO_RESOURCE;
	}
	for (i = 0; i < timerfd_fds; i++)
		timerfds[i] = -1;

#if defined(USE_POLL)
	pollfds = calloc((size_t)timerfd_fds, sizeof(*pollfds));
	if (!pollfds) {
		pr_inf_skip("%s: cannot allocate %" PRIu32 " timerfd file descriptors, "
			"skipping stressor\n", args->name, timerfd_fds);
		rc = EXIT_NO_RESOURCE;
		goto free_fds;
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

#if defined(CLOCK_REALTIME)
	/* Exercise timerfd_create with invalid flags */
	ret = timerfd_create(CLOCK_REALTIME, ~0);
	if (ret >= 0)
		(void)close(ret);
#endif

	if (count == 0) {
		pr_fail("%s: timerfd_create failed, no timers created\n", args->name);
		rc = EXIT_FAILURE;
		goto close_file_fd;
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
			goto close_file_fd;
		}
	}

	stress_set_proc_state(args->name, STRESS_STATE_RUN);

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
			if (timerfds[i] >= 0)
				FD_SET(timerfds[i], &rdfs);
		}
		timeout.tv_sec = 0;
		timeout.tv_usec = 500000;
#endif
#if defined(USE_POLL)
		for (i = 0, j = 0; i < timerfd_fds; i++) {
			if (timerfds[i] >= 0) {
				pollfds[j].fd = timerfds[i];
				pollfds[j].events = POLLIN;
				pollfds[j].revents = 0;
				j++;
			}
		}
#endif

		if (!stress_continue_flag())
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
			unsigned long arg = 1ULL;

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

close_file_fd:
	if (file_fd >= 0)
		(void)close(file_fd);
dir_rm:
	(void)stress_temp_dir_rm_args(args);

close_timer_fds:
	for (i = 0; i < timerfd_fds; i++) {
		if (timerfds[i] >= 0)
			(void)close(timerfds[i]);
	}

#if defined(USE_POLL)
free_fds:
	free(pollfds);
#endif
	free(timerfds);

	return rc;
}

stressor_info_t stress_timerfd_info = {
	.stressor = stress_timerfd,
	.class = CLASS_INTERRUPT | CLASS_OS,
	.verify = VERIFY_ALWAYS,
	.opt_set_funcs = opt_set_funcs,
	.help = help
};
#else
stressor_info_t stress_timerfd_info = {
	.stressor = stress_unimplemented,
	.class = CLASS_INTERRUPT | CLASS_OS,
	.opt_set_funcs = opt_set_funcs,
	.verify = VERIFY_ALWAYS,
	.help = help,
	.unimplemented_reason = "built without sys/timerfd.h, timerfd_create(), timerfd_settime(), timerfd_setime, select() or poll()"
};
#endif
