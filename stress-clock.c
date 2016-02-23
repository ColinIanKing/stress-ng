/*
 * Copyright (C) 2013-2016 Canonical, Ltd.
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
#define _GNU_SOURCE

#include "stress-ng.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <signal.h>
#include <errno.h>

#if defined(STRESS_CLOCK)

typedef struct {
	int	id;		/* Clock ID */
	char 	*name;		/* Clock name */
} clock_info_t;

#define CLOCK_INFO(x)	{ x, #x }

static const clock_info_t clocks[] = {
#ifdef CLOCK_REALTIME
	CLOCK_INFO(CLOCK_REALTIME),
#endif
#ifdef CLOCK_REALTIME_COARSE
	CLOCK_INFO(CLOCK_REALTIME_COARSE),
#endif
#ifdef CLOCK_MONOTONIC
	CLOCK_INFO(CLOCK_MONOTONIC),
#endif
#ifdef CLOCK_MONOTONIC_RAW
	CLOCK_INFO(CLOCK_MONOTONIC_RAW),
#endif
#ifdef CLOCK_BOOTTIME
	CLOCK_INFO(CLOCK_BOOTTIME),
#endif
#ifdef CLOCK_PROCESS_CPUTIME_ID
	CLOCK_INFO(CLOCK_PROCESS_CPUTIME_ID),
#endif
#ifdef CLOCK_THREAD_CPUTIME_ID
	CLOCK_INFO(CLOCK_THREAD_CPUTIME_ID)
#endif
};

#if (_XOPEN_SOURCE >= 600 || _POSIX_C_SOURCE >= 200112L)
static const int clocks_nanosleep[] = {
#ifdef CLOCK_REALTIME
	CLOCK_REALTIME,
#endif
#ifdef CLOCK_MONOTONIC
	CLOCK_MONOTONIC
#endif
};
#endif

#if _POSIX_C_SOURCE >= 199309L && defined(__linux__)
static const int timers[] = {
#ifdef CLOCK_REALTIME
	CLOCK_REALTIME,
#endif
#ifdef CLOCK_MONOTONIC
	CLOCK_MONOTONIC
#endif
};
#endif

/*
 *  stress_clock_name()
 *	clock id to name
 */
static char *stress_clock_name(int id)
{
	size_t i;

	for (i = 0; i < SIZEOF_ARRAY(clocks); i++) {
		if (clocks[i].id == id)
			return clocks[i].name;
	}
	return "(unknown clock)";
}

/*
 *  stress_clock()
 *	stress system by rapid clocking system calls
 */
int stress_clock(
	uint64_t *const counter,
	const uint32_t instance,
	const uint64_t max_ops,
	const char *name)
{
	(void)instance;

	do {
		size_t i;
		struct timespec t;
		int ret;

		/*
		 *  Exercise clock_getres and clock_gettime for each clock
		 */
		for (i = 0; i < SIZEOF_ARRAY(clocks); i++) {
			ret = clock_getres(clocks[i].id, &t);
			if ((ret < 0) && (opt_flags & OPT_FLAGS_VERIFY))
				pr_fail(stderr, "%s: clock_getres failed for "
				"timer '%s', errno=%d (%s)\n",
				name, clocks[i].name, errno, strerror(errno));
			ret = clock_gettime(clocks[i].id, &t);
			if ((ret < 0) && (opt_flags & OPT_FLAGS_VERIFY))
				pr_fail(stderr, "%s: clock_gettime failed for "
				"timer '%s', errno=%d (%s)\n",
				name, clocks[i].name, errno, strerror(errno));
		}

#if (_XOPEN_SOURCE >= 600 || _POSIX_C_SOURCE >= 200112L)
		/*
		 *  Exercise clock_nanosleep for each clock
		 */
		for (i = 0; i < SIZEOF_ARRAY(clocks_nanosleep); i++) {
			t.tv_sec = 0;
			t.tv_nsec = 0;
			/*
			 *  Calling with TIMER_ABSTIME will force
			 *  clock_nanosleep() to return immediately
			 */
			ret = clock_nanosleep(clocks_nanosleep[i], TIMER_ABSTIME, &t, NULL);
			if ((ret < 0) && (opt_flags & OPT_FLAGS_VERIFY))
				pr_fail(stderr, "%s: clock_nanosleep failed for timer '%s', "
					"errno=%d (%s)\n", name,
					stress_clock_name(clocks_nanosleep[i]),
					errno, strerror(errno));
		}
#endif

#if _POSIX_C_SOURCE >= 199309L && defined(__linux__)
		/*
		 *  Stress the timers
		 */
		for (i = 0; i < SIZEOF_ARRAY(timers); i++) {
			timer_t timer_id;
			struct itimerspec its;
			struct sigevent sevp;
			int64_t loops = 1000000;

			memset(&sevp, 0, sizeof(sevp));
			sevp.sigev_notify = SIGEV_NONE;
			ret = timer_create(timers[i], &sevp, &timer_id);
			if ((ret < 0) && (opt_flags & OPT_FLAGS_VERIFY)) {
				pr_fail(stderr, "%s: timer_create failed for timer '%s', "
					"errno=%d (%s)\n", name,
					stress_clock_name(timers[i]),
					errno, strerror(errno));
				continue;
			}

			/* One shot mode, for 50000 ns */
			its.it_value.tv_sec = 0;
			its.it_value.tv_nsec = 50000;
			its.it_interval.tv_sec = 0;
			its.it_interval.tv_nsec = 0;

			ret = timer_settime(timer_id, 0, &its, NULL);
			if ((ret < 0) && (opt_flags & OPT_FLAGS_VERIFY)) {
				pr_fail(stderr, "%s: timer_settime failed for timer '%s', "
					"errno=%d (%s)\n", name,
					stress_clock_name(timers[i]),
					errno, strerror(errno));
				goto timer_delete;
			}

			do {
				ret = timer_gettime(timer_id, &its);
				if ((ret < 0) && (opt_flags & OPT_FLAGS_VERIFY)) {
					pr_fail(stderr, "%s: timer_gettime failed for timer '%s', "
						"errno=%d (%s)\n", name,
						stress_clock_name(timers[i]),
						errno, strerror(errno));
					goto timer_delete;
				}
				loops--;
			} while ((loops > 0) && opt_do_run && (its.it_value.tv_nsec != 0));

timer_delete:
			ret = timer_delete(timer_id);
			if ((ret < 0) && (opt_flags & OPT_FLAGS_VERIFY)) {
				pr_fail(stderr, "%s: timer_delete failed for timer '%s', "
					"errno=%d (%s)\n", name,
					stress_clock_name(timers[i]),
					errno, strerror(errno));
				break;
			}
		}
#endif
		(*counter)++;
	} while (opt_do_run && (!max_ops || *counter < max_ops));

	return EXIT_SUCCESS;
}

#endif
