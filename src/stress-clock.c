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
	{ NULL,	"clock N",	"start N workers thrashing clocks and POSIX timers" },
	{ NULL,	"clock-ops N",	"stop clock workers after N bogo operations" },
	{ NULL,	NULL,		NULL }
};

#if defined(HAVE_LIB_RT) &&		\
    defined(HAVE_CLOCK_GETTIME) &&	\
    defined(HAVE_CLOCK_SETTIME)

typedef struct {
	const int 	id;		/* Clock ID */
	const char 	*name;		/* Clock name */
} stress_clock_info_t;

#define CLOCK_INFO(x)	{ x, #x }

static const stress_clock_info_t clocks[] = {
#if defined(CLOCK_REALTIME)
	CLOCK_INFO(CLOCK_REALTIME),
#endif
#if defined(CLOCK_REALTIME_COARSE)
	CLOCK_INFO(CLOCK_REALTIME_COARSE),
#endif
#if defined(CLOCK_MONOTONIC)
	CLOCK_INFO(CLOCK_MONOTONIC),
#endif
#if defined(CLOCK_MONOTONIC_RAW)
	CLOCK_INFO(CLOCK_MONOTONIC_RAW),
#endif
#if defined(CLOCK_MONOTONIC_ACTIVE)
	CLOCK_INFO(CLOCK_MONOTONIC_ACTIVE),
#endif
#if defined(CLOCK_BOOTTIME)
	CLOCK_INFO(CLOCK_BOOTTIME),
#endif
#if defined(CLOCK_PROCESS_CPUTIME_ID)
	CLOCK_INFO(CLOCK_PROCESS_CPUTIME_ID),
#endif
#if defined(CLOCK_THREAD_CPUTIME_ID)
	CLOCK_INFO(CLOCK_THREAD_CPUTIME_ID),
#endif
#if defined(CLOCK_MONOTONIC_ACTIVE)
	CLOCK_INFO(CLOCK_MONOTONIC_ACTIVE),
#endif
#if defined(CLOCK_TAI)
	CLOCK_INFO(CLOCK_TAI)
#endif
};

#if defined(HAVE_CLOCK_NANOSLEEP)
static const int clocks_nanosleep[] = {
#if defined(CLOCK_REALTIME)
	CLOCK_REALTIME,
#endif
#if defined(CLOCK_MONOTONIC)
	CLOCK_MONOTONIC,
#endif
#if defined(CLOCK_THREAD_CPUTIME_ID)
	CLOCK_THREAD_CPUTIME_ID
#endif
};
#endif

#if defined(HAVE_TIMER_CREATE) &&	\
    defined(HAVE_TIMER_DELETE) &&	\
    defined(HAVE_TIMER_GETTIME) &&	\
    defined(HAVE_TIMER_GETOVERRUN) &&	\
    defined(HAVE_TIMER_SETTIME)
static const int timers[] = {
#if defined(CLOCK_REALTIME)
	CLOCK_REALTIME,
#endif
#if defined(CLOCK_MONOTONIC)
	CLOCK_MONOTONIC,
#endif
#if defined(CLOCK_THREAD_CPUTIME_ID)
	CLOCK_THREAD_CPUTIME_ID
#endif
};
#endif

#if defined(HAVE_CLOCK_NANOSLEEP) || 	\
    (defined(HAVE_TIMER_CREATE) &&	\
    defined(HAVE_TIMER_DELETE) &&	\
    defined(HAVE_TIMER_GETTIME) &&	\
    defined(HAVE_TIMER_GETOVERRUN) &&	\
    defined(HAVE_TIMER_SETTIME))
/*
 *  stress_clock_name()
 *	clock id to name
 */
static const char *stress_clock_name(int id)
{
	size_t i;

	for (i = 0; i < SIZEOF_ARRAY(clocks); i++) {
		if (clocks[i].id == id)
			return clocks[i].name;
	}
	return "(unknown clock)";
}
#endif

#if (defined(__NR_clock_adjtime) &&	\
     defined(HAVE_SYS_TIMEX_H) &&	\
     defined(ADJ_SETOFFSET)) ||		\
    (defined(HAVE_CLOCK_NANOSLEEP) &&	\
    defined(TIMER_ABSTIME))
#define CHECK_INVALID_CLOCK_ID		(1)
#endif

#if defined(CHECK_INVALID_CLOCK_ID)
/*
 * check_invalid_clock_id()
 * function to check if given clock_id is valid
 */
static inline bool check_invalid_clock_id(const clockid_t id) {
        struct timespec tp;

        (void)memset(&tp, 0, sizeof(tp));
        return (clock_gettime(id, &tp) != 0);
}
#endif

#define FD_TO_CLOCKID(fd)	((~(clockid_t)(fd) << 3) | 3)

/*
 *  stress_clock()
 *	stress system by rapid clocking system calls
 */
static int stress_clock(const stress_args_t *args)
{
	/*
	 * Use same random number seed for each
	 * test run to ensure predictable repeatable
	 * 'random' sleep duration timings
	 */
	stress_mwc_seed(0xf238, 0x1872);
	bool test_invalid_timespec = true;
	const bool is_root = stress_check_capability(SHIM_CAP_IS_ROOT);

#if defined(CHECK_INVALID_CLOCK_ID)
	const bool invalid_clock_id = check_invalid_clock_id(INT_MAX);
#endif

	stress_set_proc_state(args->name, STRESS_STATE_RUN);

	do {
#if defined(CLOCK_THREAD_CPUTIME_ID) && \
    defined(HAVE_CLOCK_GETTIME) &&	\
    defined(HAVE_CLOCK_SETTIME)
		{
			int ret;
			struct timespec t;

			/*
			 *  Exercise setting local thread CPU timer
			 */
			ret = shim_clock_gettime(CLOCK_THREAD_CPUTIME_ID, &t);
			if ((ret < 0) && (g_opt_flags & OPT_FLAGS_VERIFY) &&
			    (errno != EINVAL) && (errno != ENOSYS))
				pr_fail("%s: clock_gettime failed for timer 'CLOCK_THREAD_CPUTIME_ID', errno=%d (%s)\n",
					args->name, errno, strerror(errno));

			/* Exercise clock_settime with illegal clockid */
			(void)shim_clock_settime((clockid_t)-1, &t);

			/*
			 *  According to clock_settime(2), setting the timer
			 *  CLOCK_THREAD_CPUTIME_ID is not possible on Linux.
			 *  Try to set it and ignore the result because it will
			 *  currently return -EINVAL, but it may not do so in the
			 *  future.
			 */
			ret = clock_settime(CLOCK_THREAD_CPUTIME_ID, &t);
			(void)ret;
		}
#endif

#if defined(HAVE_CLOCK_GETRES)
		{
			size_t i;
			struct timespec t;

			/* Exercise clock_getres with illegal clockid */
			(void)shim_clock_getres((clockid_t)-1, &t);

			/* Exercise clock_gettime with illegal clockid */
			(void)shim_clock_gettime((clockid_t)-1, &t);

			/*
			 *  Exercise clock_getres and clock_gettime for each clock
			 */
			for (i = 0; i < SIZEOF_ARRAY(clocks); i++) {
				int ret;

				ret = shim_clock_getres(clocks[i].id, &t);
				if ((ret < 0) && (g_opt_flags & OPT_FLAGS_VERIFY) &&
			            (errno != EINVAL) && (errno != ENOSYS))
					pr_fail("%s: clock_getres failed for timer '%s', errno=%d (%s)\n",
							args->name, clocks[i].name, errno, strerror(errno));
				ret = shim_clock_gettime(clocks[i].id, &t);
				if ((ret < 0) && (g_opt_flags & OPT_FLAGS_VERIFY) &&
			            (errno != EINVAL) && (errno != ENOSYS))
					pr_fail("%s: clock_gettime failed for timer '%s', errno=%d (%s)\n",
						args->name, clocks[i].name, errno, strerror(errno));
			}
		}
#endif

#if defined(HAVE_CLOCK_GETTIME) &&	\
    defined(HAVE_CLOCK_SETTIME)
		if (test_invalid_timespec) {
			size_t i;

			for (i = 0; i < SIZEOF_ARRAY(clocks); i++) {
				struct timespec t, t1;
				int ret;

				/* Save current time to reset later if required */
				ret = shim_clock_gettime(clocks[i].id, &t1);
				if ((ret < 0) && (g_opt_flags & OPT_FLAGS_VERIFY) &&
				    (errno != EINVAL) && (errno != ENOSYS)) {
					pr_fail("%s: clock_getres failed for timer '%s', errno=%d (%s)\n",
					args->name, clocks[i].name, errno, strerror(errno));
				}
				if (ret < 0)
					continue;

				/* Ensuring clock_settime cannot succeed without privilege */
				if (!is_root) {
					ret = shim_clock_settime(clocks[i].id, &t);
					if (ret != -EPERM) {
						/* This is an error, report it! */
						pr_fail("%s: clock_settime failed, did not have privilege to "
							"set time, expected -EPERM, instead got errno=%d (%s)\n",
							args->name, errno, strerror(errno));
					}
				}

				/*
				 * Exercise clock_settime with illegal tv sec
				 * and nsec values
				 */
				t.tv_sec = -1;
				t.tv_nsec = -1;

				/*
				 * Test only if time fields are invalid
				 * negative values (some systems may
				 * represent tv_* files as unsigned hence
				 * this sanity check)
				 */
				if ((t.tv_sec < 0) && (t.tv_nsec < 0)) {
					ret = shim_clock_settime(clocks[i].id, &t);
					if (ret < 0)
						continue;

					/* Expected a failure, but it succeeded(!) */
					pr_fail("%s: clock_settime was able to set an "
						"invalid negative time for timer '%s'\n",
						args->name, clocks[i].name);

					/* Restore the correct time */
					ret = shim_clock_settime(clocks[i].id, &t1);
					if ((ret < 0) && (errno != EINVAL) && (errno != ENOSYS)) {
						pr_fail("%s: clock_gettime failed for timer '%s', errno=%d (%s)\n",
							args->name, clocks[i].name, errno, strerror(errno));
					}
					/*
					 * Ensuring invalid clock_settime runs
					 * only single time to minimize time lag
					 */
					test_invalid_timespec = false;
				}
			}
		}
#endif

#if defined(HAVE_CLOCK_NANOSLEEP) &&	\
    defined(TIMER_ABSTIME)
		{
			size_t i;
			struct timespec t;
			static int n = 0;

			if (n++ >= 1024) {
				n = 0;
				int ret_st;

				/* Exercise clock_nanosleep on invalid clock id */
				if (invalid_clock_id) {
					(void)memset(&t, 0, sizeof(t));
					ret_st = clock_nanosleep(INT_MAX, TIMER_ABSTIME, &t, NULL);
					(void)ret_st;
				}

				/* Exercise clock_adjtime on non-permitted timespec object values */
				(void)memset(&t, 0, sizeof(t));
				t.tv_sec = -1;
				ret_st = clock_nanosleep(clocks_nanosleep[0], TIMER_ABSTIME, &t, NULL);
				(void)ret_st;

				(void)memset(&t, 0, sizeof(t));
				t.tv_nsec = STRESS_NANOSECOND;
				ret_st = clock_nanosleep(clocks_nanosleep[0], TIMER_ABSTIME, &t, NULL);
				(void)ret_st;
			}

			/*
			 *  Exercise clock_nanosleep for each clock
			 */
			for (i = 0; i < SIZEOF_ARRAY(clocks_nanosleep); i++) {
				int ret;

				t.tv_sec = 0;
				t.tv_nsec = (stress_mwc32() % 2500) + 1;
				/*
				 *  Calling with TIMER_ABSTIME will force
				 *  clock_nanosleep() to return immediately
				 */
				ret = clock_nanosleep(clocks_nanosleep[i], TIMER_ABSTIME, &t, NULL);
				if ((ret < 0) && (g_opt_flags & OPT_FLAGS_VERIFY))
					pr_fail("%s: clock_nanosleep failed for timer '%s', errno=%d (%s)\n",
						args->name,
						stress_clock_name(clocks_nanosleep[i]),
						errno, strerror(errno));
			}
		}
#endif

#if defined(__NR_clock_adjtime) &&	\
    defined(HAVE_SYS_TIMEX_H) &&	\
    defined(ADJ_SETOFFSET)
		{
			size_t i;
			struct shim_timex tx;

			/* Exercise clock_adjtime on invalid clock id */
			if (invalid_clock_id) {
				int ret_st;

				(void)memset(&tx, 0, sizeof(tx));
				ret_st = shim_clock_adjtime(INT_MAX, &tx);
				(void)ret_st;
			}

			/*
			 *  Exercise clock_adjtime
			 */
			for (i = 0; i < SIZEOF_ARRAY(clocks); i++) {
				int ret;

				(void)memset(&tx, 0, sizeof(tx));

				tx.modes = ADJ_SETOFFSET;
				tx.time.tv_sec = 0;
				tx.time.tv_usec = 0;

				ret = shim_clock_adjtime(clocks[i].id, &tx);
				if ((ret < 0) && (g_opt_flags & OPT_FLAGS_VERIFY) &&
			            (errno != EINVAL) && (errno != ENOSYS) &&
				    (errno != EPERM) && (errno != EOPNOTSUPP)) {
					pr_fail("%s: clock_adjtime failed for timer '%s', errno=%d (%s)\n",
						args->name, clocks[i].name, errno, strerror(errno));
				}
			}
		}
#endif


#if defined(HAVE_TIMER_CREATE) &&	\
    defined(HAVE_TIMER_DELETE) &&	\
    defined(HAVE_TIMER_GETTIME) &&	\
    defined(HAVE_TIMER_GETOVERRUN) &&	\
    defined(HAVE_TIMER_SETTIME)
		{
			size_t i;
			const size_t n = SIZEOF_ARRAY(timers);
			bool timer_fail[n];
			timer_t timer_id[n];
			struct itimerspec its;
			int ret;

			/*
			 *  Stress the timers
			 */
			for (i = 0; i < n; i++) {
				struct sigevent sevp;

				timer_fail[i] = false;
				timer_id[i] = (timer_t)-1;

				(void)memset(&sevp, 0, sizeof(sevp));
				sevp.sigev_notify = SIGEV_NONE;
				ret = timer_create(timers[i], &sevp, &timer_id[i]);
				if (ret < 0) {
					timer_fail[i] = true;
					if (g_opt_flags & OPT_FLAGS_VERIFY) {
						if ((errno == EINVAL) || (errno == EPERM))
							continue;
						pr_fail("%s: timer_create failed for timer '%s', errno=%d (%s)\n",
							args->name,
							stress_clock_name(timers[i]),
							errno, strerror(errno));
					}
					continue;
				}

				/* One shot mode, for random time 0..5000 ns */
				its.it_value.tv_sec = 0;
				its.it_value.tv_nsec = (stress_mwc32() % 5000) + 1;
				its.it_interval.tv_sec = 0;
				its.it_interval.tv_nsec = 0;

				ret = timer_settime(timer_id[i], 0, &its, NULL);
				if ((ret < 0) && (g_opt_flags & OPT_FLAGS_VERIFY)) {
					pr_fail("%s: timer_settime failed for timer '%s', errno=%d (%s)\n",
						args->name,
						stress_clock_name(timers[i]),
						errno, strerror(errno));
				}
			}

			for (i = 0; i < n; i++) {
				if (timer_fail[i] || timer_id[i] == (timer_t)-1)
					continue;

				ret = timer_gettime(timer_id[i], &its);
				if ((ret < 0) && (g_opt_flags & OPT_FLAGS_VERIFY)) {
					pr_fail("%s: timer_gettime failed for timer '%s', errno=%d (%s)\n",
						args->name,
						stress_clock_name(timers[i]),
						errno, strerror(errno));
					break;
				}
				ret = timer_getoverrun(timer_id[i]);
				(void)ret;
			}

			for (i = 0; i < n; i++) {
				if (timer_fail[i] || timer_id[i] == (timer_t)-1)
					continue;
				ret = timer_delete(timer_id[i]);
				if ((ret < 0) && (g_opt_flags & OPT_FLAGS_VERIFY)) {
					pr_fail("%s: timer_delete failed for timer '%s', errno=%d (%s)\n",
						args->name,
						stress_clock_name(timers[i]),
						errno, strerror(errno));
					break;
				}
			}
		}
#endif

#if defined(__linux__)
		{
			int fd;

			fd = open("/dev/ptp0", O_RDWR);
			if (fd >= 0) {
				struct timespec t;
				int ret, clkid = FD_TO_CLOCKID(fd);
#if defined(HAVE_POLL_H)
				struct pollfd pollfds[1];

				pollfds[0].fd = fd;
				pollfds[0].events = POLLIN;
				pollfds[0].revents = 0;

				ret = poll(pollfds, 1, 0);
				(void)ret;
#endif
				ret = clock_gettime(clkid, &t);
				if ((ret < 0) && (g_opt_flags & OPT_FLAGS_VERIFY) &&
				    (errno != EINVAL) && (errno != ENOSYS)) {
					pr_fail("%s: clock_gettime failed for /dev/ptp0, errno=%d (%s)",
					args->name, errno, strerror(errno));
				}
#if defined(HAVE_CLOCK_GETRES)
				ret = shim_clock_getres(clkid, &t);
				if ((ret < 0) && (g_opt_flags & OPT_FLAGS_VERIFY) &&
				    (errno != EINVAL) && (errno != ENOSYS)) {
					pr_fail("%s: clock_getres failed for /dev/ptp0, errno=%d (%s)",
					args->name, errno, strerror(errno));
				}
#endif
				(void)close(fd);
			}
		}
#endif
		inc_counter(args);
	} while (keep_stressing(args));

	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);

	return EXIT_SUCCESS;
}

stressor_info_t stress_clock_info = {
	.stressor = stress_clock,
	.class = CLASS_INTERRUPT | CLASS_OS,
	.help = help
};
#else
stressor_info_t stress_clock_info = {
	.stressor = stress_not_implemented,
	.class = CLASS_INTERRUPT | CLASS_OS,
	.help = help
};
#endif
