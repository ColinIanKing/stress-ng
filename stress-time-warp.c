/*
 * Copyright (C) 2024-2025 Colin Ian King.
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
#include "core-capabilities.h"

#include <time.h>

static const stress_help_t help[] = {
	{ NULL,	"time-warp N",		"start N workers checking for timer/clock warping" },
	{ NULL,	"time-warp-ops N",	"stop workers after N bogo timer/clock reads" },
	{ NULL,	NULL,			NULL }
};

#undef HAVE_LIB_RT

#if (defined(HAVE_LIB_RT) && defined(HAVE_CLOCK_GETTIME)) ||	\
     defined(HAVE_GETTIMEOFDAY) ||				\
     defined(HAVE_TIME) ||					\
     defined(HAVE_GETRUSAGE)

typedef struct {
	int (*gettime)(clockid_t clockid, struct timespec *tp);
	const int 	id;		/* Clock ID */
	const char 	*name;		/* Clock name */
	const bool	monotonic;	/* Clock always increases monotonically */
} stress_time_warp_info_t;

typedef struct {
	struct timespec ts_init;	/* Initial clock time at start */
	struct timespec	ts_prev;	/* Previous clock time */
	uint64_t	warped;		/* Count of clock warping */
	bool		failed;		/* Failed to get time */
} stress_time_t;

#define TIME_CLOCK(f, x, m)	{ f, x, "clock_gettime(" #x ")", m }
#define TIME_MISC(f, x, m)	{ f, 0, #x "()", m }

#if defined(HAVE_GETTIMEOFDAY)
/*
 *  stress_time_warp_gettimeofday()
 *	use gettimeofday to get timespec ts much like a clock_gettime() call
 */
static int stress_time_warp_gettimeofday(clockid_t clockid, struct timespec *ts)
{
	int ret;
	struct timeval tv;

	(void)clockid;
	ret = gettimeofday(&tv, NULL);
	if (LIKELY(ret == 0)) {
		ts->tv_sec = tv.tv_sec;
		ts->tv_nsec = tv.tv_usec * 1000;
	}
	return ret;
}
#endif

#if defined(HAVE_TIME)
/*
 *  stress_time_warp_time()
 *	use time to get timespec ts much like a clock_gettime() call
 */
static int stress_time_warp_time(clockid_t clockid, struct timespec *ts)
{
	(void)clockid;
	if (LIKELY(time(&ts->tv_sec) != (time_t)-1)) {
		ts->tv_nsec = 0;
		return 0;
	}
	return -1;
}
#endif

#if defined(HAVE_GETRUSAGE)
/*
 *  stress_time_warp_rusage()
 *	use rusage to get timespec ts much like a clock_gettime() call but
 *	in terms of cpu usage in user and system time
 */
static int stress_time_warp_rusage(clockid_t clockid, struct timespec *ts)
{
	int ret;
	struct rusage usage;

	(void)clockid;
	ret = getrusage(RUSAGE_SELF, &usage);
	if (LIKELY(ret == 0)) {
		ts->tv_sec = usage.ru_utime.tv_sec + usage.ru_stime.tv_sec;
		ts->tv_nsec = (usage.ru_utime.tv_usec + usage.ru_stime.tv_usec) * 1000;
	}
	return ret;
}
#endif

static const stress_time_warp_info_t clocks[] = {
#if (defined(HAVE_LIB_RT) && defined(HAVE_CLOCK_GETTIME))
#if defined(CLOCK_REALTIME)
	TIME_CLOCK(shim_clock_gettime, CLOCK_REALTIME, false),
#endif
#if defined(CLOCK_REALTIME_COARSE)
	TIME_CLOCK(shim_clock_gettime, CLOCK_REALTIME_COARSE, false),
#endif
#if defined(CLOCK_MONOTONIC)
	TIME_CLOCK(shim_clock_gettime, CLOCK_MONOTONIC, true),
#endif
#if defined(CLOCK_MONOTONIC_RAW)
	TIME_CLOCK(shim_clock_gettime, CLOCK_MONOTONIC_RAW, true),
#endif
#if defined(CLOCK_MONOTONIC_ACTIVE)
	TIME_CLOCK(shim_clock_gettime, CLOCK_MONOTONIC_ACTIVE, true),
#endif
#if defined(CLOCK_BOOTTIME)
	TIME_CLOCK(shim_clock_gettime, CLOCK_BOOTTIME, false),
#endif
#if defined(CLOCK_PROCESS_CPUTIME_ID)
	TIME_CLOCK(shim_clock_gettime, CLOCK_PROCESS_CPUTIME_ID, false),
#endif
#if defined(CLOCK_THREAD_CPUTIME_ID)
	TIME_CLOCK(shim_clock_gettime, CLOCK_THREAD_CPUTIME_ID, false),
#endif
#if defined(CLOCK_MONOTONIC_ACTIVE)
	TIME_CLOCK(shim_clock_gettime, CLOCK_MONOTONIC_ACTIVE, true),
#endif
#if defined(CLOCK_TAI)
	TIME_CLOCK(shim_clock_gettime, CLOCK_TAI, false),
#endif
#if defined(CLOCK_AUX)
	TIME_CLOCK(shim_clock_gettime, CLOCK_AUX, false),
#endif
#endif
#if defined(HAVE_GETTIMEOFDAY)
	TIME_MISC(stress_time_warp_gettimeofday, getitimeofday, false),
#endif
#if defined(HAVE_TIME)
	TIME_MISC(stress_time_warp_time, time, false),
#endif
	TIME_MISC(stress_time_warp_rusage, time, false),
};

/*
 *  stress_time_warp_timespec_fix()
 *	fixup timespec time ts so that tv_nsec is in range 0..NANOSEC-1
 */
static void stress_time_warp_timespec_fix(struct timespec *ts)
{
	if (ts->tv_nsec >= STRESS_NANOSECOND) {
		const time_t sec = ts->tv_nsec / STRESS_NANOSECOND;

		ts->tv_sec += sec;
		ts->tv_nsec -= (sec * STRESS_NANOSECOND);
	} else if (ts->tv_nsec <= -STRESS_NANOSECOND) {
		const time_t sec = -ts->tv_nsec / STRESS_NANOSECOND;

		ts->tv_sec -= sec;
		ts->tv_nsec += (sec * STRESS_NANOSECOND);
	}
}

/*
 *  stress_time_warp_lt()
 *	check if timespec t1 <= timespec t2
 */
static inline int stress_time_warp_lt(struct timespec *ts1, struct timespec *ts2)
{
	stress_time_warp_timespec_fix(ts1);
	stress_time_warp_timespec_fix(ts2);

	if (ts1->tv_sec < ts2->tv_sec) {
		return 1;
	} else if ((ts1->tv_sec == ts2->tv_sec) && (ts1->tv_nsec < ts2->tv_nsec)) {
		return 1;
	}
	return 0;
}

/*
 *  stress_time_warp()
 *	stress system by rapid clocking system calls
 */
static int stress_time_warp(stress_args_t *args)
{
	stress_time_t stress_times[SIZEOF_ARRAY(clocks)];
	size_t i;
	int ret, rc = EXIT_SUCCESS;

	(void)shim_memset(&stress_times, 0, sizeof(stress_times));

	stress_set_proc_state(args->name, STRESS_STATE_SYNC_WAIT);
	stress_sync_start_wait(args);
	stress_set_proc_state(args->name, STRESS_STATE_RUN);

	/* Get initial times */
	for (i = 0; i < SIZEOF_ARRAY(clocks); i++) {
		ret = clocks[i].gettime(clocks[i].id, &stress_times[i].ts_init);
		if (ret == 0) {
			stress_times[i].ts_prev = stress_times[i].ts_init;
		} else {
			pr_fail("%s: %s failed, errno=%d (%s)\n",
				args->name, clocks[i].name, errno, strerror(errno));
			stress_times[i].failed = true;
		}
	}

	do {
		/*
		 *  Exercise clock_getres and clock_gettime for each clock
		 */
		for (i = 0; i < SIZEOF_ARRAY(clocks); i++) {
			struct timespec ts;

			if (stress_times[i].failed)
				continue;

			ret = clocks[i].gettime(clocks[i].id, &ts);
			if (LIKELY(ret == 0)) {
				stress_times[i].warped +=
					stress_time_warp_lt(&ts, &stress_times[i].ts_prev);
				stress_times[i].ts_prev = ts;
			} else if (UNLIKELY((errno != EINVAL) && (errno != ENOSYS))) {
				pr_fail("%s: %s failed, errno=%d (%s)\n",
					args->name, clocks[i].name, errno, strerror(errno));
				stress_times[i].failed = true;
				rc = EXIT_FAILURE;
			}
		}
		stress_bogo_inc(args);
	} while (stress_continue(args));

	/*
	 *  Verify current clocks/times are not less than starting times
	 *  and hence potentially wrapped around
	 */
	for (i = 0; i < SIZEOF_ARRAY(clocks); i++) {
		if (stress_time_warp_lt(&stress_times[i].ts_prev, &stress_times[i].ts_init)) {
			pr_fail("%s: failed, %30.30s, detected %" PRIu64 " time wrap-around\n",
				args->name, clocks[i].name, stress_times[i].warped);
			rc = EXIT_FAILURE;
		}
	}

	/*
	 *  Verify monotonic clocks/times have not jumped back in time
	 *  and broken the monotone restriction
	 */
	for (i = 0; i < SIZEOF_ARRAY(clocks); i++) {
		if (clocks[i].monotonic && stress_times[i].warped) {
			pr_fail("%s: failed, %30.30s, detected %" PRIu64 " time warps\n",
				args->name, clocks[i].name, stress_times[i].warped);
			rc = EXIT_FAILURE;
		}
	}
	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);

	return rc;
}

const stressor_info_t stress_time_warp_info = {
	.stressor = stress_time_warp,
	.classifier = CLASS_OS,
	.verify = VERIFY_ALWAYS,
	.help = help
};
#else
const stressor_info_t stress_time_warp_info = {
	.stressor = stress_unimplemented,
	.classifier = CLASS_OS,
	.verify = VERIFY_ALWAYS,
	.help = help,
	.unimplemented_reason = "built without librt or clock_gettime(), gettimeofday(), getrusage() or time() support"
};
#endif
