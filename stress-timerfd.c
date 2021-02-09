/*
 * Copyright (C) 2013-2021 Canonical, Ltd.
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

static const stress_help_t help[] = {
	{ NULL,	"timerfd N",	  "start N workers producing timerfd events" },
	{ NULL,	"timerfd-ops N",  "stop after N timerfd bogo events" },
	{ NULL,	"timerfd-freq F", "run timer(s) at F Hz, range 1 to 1000000000" },
	{ NULL,	"timerfd-rand",	  "enable random timerfd frequency" },
	{ NULL,	NULL,		  NULL }
};

#define COUNT_MAX		(256)
#define TIMERFD_MAX		(256)

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
	bool timerfd_rand = true;

	(void)opt;
	return stress_set_setting("timerfd-rand", TYPE_ID_BOOL, &timerfd_rand);
}

static const stress_opt_set_func_t opt_set_funcs[] = {
	{ OPT_timerfd_freq,	stress_set_timerfd_freq },
	{ OPT_timerfd_rand,	stress_set_timerfd_rand },
	{ 0,			NULL }
};

#if defined(HAVE_SYS_TIMERFD_H)

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
		double r = ((double)(stress_mwc32() % 10000) - 5000.0) / 40000.0;
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
	int timerfd[TIMERFD_MAX], count = 0, i, max_timerfd = -1;
	bool timerfd_rand = false;
	int file_fd = -1;
	char file_fd_name[PATH_MAX];
#if defined(CLOCK_BOOTTIME_ALARM)
	const bool cap_wake_alarm = stress_check_capability(SHIM_CAP_WAKE_ALARM);
#endif
	const int bad_fd = stress_get_bad_fd();
	const pid_t self = getpid();
	int ret;

	(void)stress_get_setting("timerfd-rand", &timerfd_rand);

	if (!stress_get_setting("timerfd-freq", &timerfd_freq)) {
		if (g_opt_flags & OPT_FLAGS_MAXIMIZE)
			timerfd_freq = MAX_TIMERFD_FREQ;
		if (g_opt_flags & OPT_FLAGS_MINIMIZE)
			timerfd_freq = MIN_TIMERFD_FREQ;
	}
	rate_ns = timerfd_freq ? (double)STRESS_NANOSECOND / timerfd_freq :
				 (double)STRESS_NANOSECOND;

	for (i = 0; i < TIMERFD_MAX; i++) {
		timerfd[i] = timerfd_create(CLOCK_REALTIME, 0);
		if (timerfd[i] < 0) {
			if ((errno != EMFILE) &&
			    (errno != ENFILE) &&
			    (errno != ENOMEM)) {
				pr_fail("%s: timerfd_create failed, errno=%d (%s)\n",
					args->name, errno, strerror(errno));
				return EXIT_FAILURE;
			}
		}
		count++;
		if (max_timerfd < timerfd[i])
			max_timerfd = timerfd[i];
	}

	/* Create a non valid timerfd file descriptor */
	ret = stress_temp_dir_mk_args(args);
	if (ret < 0)
		return exit_status(-ret);
	(void)stress_temp_filename_args(args, file_fd_name, sizeof(file_fd_name), stress_mwc32());
	file_fd = open(file_fd_name, O_CREAT | O_RDWR, S_IRUSR | S_IWUSR);
	if (file_fd < 0) {
		pr_err("%s: cannot create %s\n", args->name, file_fd_name);
		return exit_status(errno);
	}
	(void)unlink(file_fd_name);

#if defined(CLOCK_REALTIME_ALARM)
	/* Check timerfd_create cannot succeed without capability */
	if (!cap_wake_alarm) {
		ret = timerfd_create(CLOCK_REALTIME_ALARM, 0);
		if (ret >= 0) {
			pr_fail("%s: timerfd_create without capability CAP_WAKE_ALARM unexpectedly"
					"succeeded, errno=%d (%s)\n", args->name, errno, strerror(errno));
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
		return EXIT_FAILURE;
	}
	count = 0;

	stress_timerfd_set(&timer, timerfd_rand);
	for (i = 0; i < TIMERFD_MAX; i++) {
		if (timerfd[i] < 0)
			continue;
		if (timerfd_settime(timerfd[i], 0, &timer, NULL) < 0) {
			pr_fail("%s: timerfd_settime failed on fd %d, errno=%d (%s)\n",
				args->name, timerfd[i], errno, strerror(errno));
			(void)close(timerfd[i]);
			return EXIT_FAILURE;
		}
	}

	stress_set_proc_state(args->name, STRESS_STATE_RUN);

	do {
		uint64_t expval;
		struct itimerspec value;
		struct timeval timeout;
		fd_set rdfs;

		FD_ZERO(&rdfs);
		for (i = 0; i < TIMERFD_MAX; i++)
			FD_SET(timerfd[i], &rdfs);

		timeout.tv_sec = 0;
		timeout.tv_usec = 500000;

		if (!keep_stressing_flag())
			break;
		ret = select(max_timerfd + 1, &rdfs, NULL, NULL, &timeout);
		if (ret < 0) {
			if (errno == EINTR)
				continue;
			pr_fail("%s: select failed, errno=%d (%s)\n",
				args->name, errno, strerror(errno));
			break;
		}
		if (ret < 1)
			continue; /* Timeout */

		for (i = 0; i < TIMERFD_MAX; i++) {
			if (timerfd[i] < 0)
				continue;
			if (!FD_ISSET(timerfd[i], &rdfs))
				continue;

			ret = read(timerfd[i], &expval, sizeof expval);
			if (ret < 0) {
				pr_fail("%s: read of timerfd failed, errno=%d (%s)\n",
					args->name, errno, strerror(errno));
				break;
			}
			if (timerfd_gettime(timerfd[i], &value) < 0) {
				pr_fail("%s: timerfd_gettime failed, errno=%d (%s)\n",
					args->name, errno, strerror(errno));
				break;
			}
			if (timerfd_rand) {
				stress_timerfd_set(&timer, timerfd_rand);
				if (timerfd_settime(timerfd[i], 0, &timer, NULL) < 0) {
					pr_fail("%s: timerfd_settime failed, errno=%d (%s)\n",
						args->name, errno, strerror(errno));
					break;
				}
			}
			inc_counter(args);
		}

		/* Exercise invalid timerfd_gettime syscalls on bad fd */
		ret = timerfd_gettime(bad_fd, &value);
		(void)ret;

		ret = timerfd_gettime(file_fd, &value);
		(void)ret;

		/* Exercise invalid timerfd_settime syscalls on bad fd */
		ret = timerfd_settime(bad_fd, 0, &timer, NULL);
		(void)ret;

		ret = timerfd_settime(file_fd, 0, &timer, NULL);
		(void)ret;

		/* Exercise timerfd_settime with invalid flags */
		ret = timerfd_settime(bad_fd, ~0, &timer, NULL);;
		(void)ret;

		/*
		 *  Periodically read /proc/$pid/fdinfo/timerfd[0],
		 *  we don't care about failures, we just want to
		 *  exercise this interface
		 */
		if (UNLIKELY(count++ >= COUNT_MAX)) {
			(void)stress_read_fdinfo(self, timerfd[0]);
			count = 0;
		}
	} while (keep_stressing(args));

	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);

	for (i = 0; i < TIMERFD_MAX; i++) {
		if (timerfd[i] > 0)
			(void)close(timerfd[i]);
	}
	(void)close(file_fd);
	(void)stress_temp_dir_rm_args(args);

	return EXIT_SUCCESS;
}

stressor_info_t stress_timerfd_info = {
	.stressor = stress_timerfd,
	.class = CLASS_INTERRUPT | CLASS_OS,
	.opt_set_funcs = opt_set_funcs,
	.help = help
};
#else
stressor_info_t stress_timerfd_info = {
	.stressor = stress_not_implemented,
	.class = CLASS_INTERRUPT | CLASS_OS,
	.opt_set_funcs = opt_set_funcs,
	.help = help
};
#endif
