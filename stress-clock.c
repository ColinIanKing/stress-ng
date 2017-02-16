/*
 * Copyright (C) 2013-2017 Canonical, Ltd.
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

#if defined(HAVE_LIB_RT) && (_POSIX_C_SOURCE >= 199309L)

typedef struct {
	int	id;		/* Clock ID */
	char 	*name;		/* Clock name */
} clock_info_t;

#define CLOCK_INFO(x)	{ x, #x }

static const clock_info_t clocks[] = {
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
#if defined(CLOCK_BOOTTIME)
	CLOCK_INFO(CLOCK_BOOTTIME),
#endif
#if defined(CLOCK_PROCESS_CPUTIME_ID)
	CLOCK_INFO(CLOCK_PROCESS_CPUTIME_ID),
#endif
#if defined(CLOCK_THREAD_CPUTIME_ID)
	CLOCK_INFO(CLOCK_THREAD_CPUTIME_ID)
#endif
};

#if (_XOPEN_SOURCE >= 600 || _POSIX_C_SOURCE >= 200112L)
static const int clocks_nanosleep[] = {
#if defined(CLOCK_REALTIME)
	CLOCK_REALTIME,
#endif
#if defined(CLOCK_MONOTONIC)
	CLOCK_MONOTONIC
#endif
};
#endif

#if _POSIX_C_SOURCE >= 199309L && defined(__linux__)
static const int timers[] = {
#if defined(CLOCK_REALTIME)
	CLOCK_REALTIME,
#endif
#if defined(CLOCK_MONOTONIC)
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
int stress_clock(const args_t *args)
{
	do {
		size_t i;
		struct timespec t;

		/*
		 *  Exercise clock_getres and clock_gettime for each clock
		 */
		for (i = 0; i < SIZEOF_ARRAY(clocks); i++) {
			int ret = clock_getres(clocks[i].id, &t);

			if ((ret < 0) && (g_opt_flags & OPT_FLAGS_VERIFY))
				pr_fail("%s: clock_getres failed for "
				"timer '%s', errno=%d (%s)\n",
				args->name, clocks[i].name, errno, strerror(errno));
			ret = clock_gettime(clocks[i].id, &t);
			if ((ret < 0) && (g_opt_flags & OPT_FLAGS_VERIFY))
				pr_fail("%s: clock_gettime failed for "
				"timer '%s', errno=%d (%s)\n",
				args->name, clocks[i].name, errno, strerror(errno));
		}

#if (_XOPEN_SOURCE >= 600 || _POSIX_C_SOURCE >= 200112L)
		/*
		 *  Exercise clock_nanosleep for each clock
		 */
		for (i = 0; i < SIZEOF_ARRAY(clocks_nanosleep); i++) {
			int ret;
			t.tv_sec = 0;
			t.tv_nsec = 0;
			/*
			 *  Calling with TIMER_ABSTIME will force
			 *  clock_nanosleep() to return immediately
			 */
			ret = clock_nanosleep(clocks_nanosleep[i], TIMER_ABSTIME, &t, NULL);
			if ((ret < 0) && (g_opt_flags & OPT_FLAGS_VERIFY))
				pr_fail("%s: clock_nanosleep failed for timer '%s', "
					"errno=%d (%s)\n", args->name,
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
			int ret;

			memset(&sevp, 0, sizeof(sevp));
			sevp.sigev_notify = SIGEV_NONE;
			ret = timer_create(timers[i], &sevp, &timer_id);
			if ((ret < 0) && (g_opt_flags & OPT_FLAGS_VERIFY)) {
				pr_fail("%s: timer_create failed for timer '%s', "
					"errno=%d (%s)\n", args->name,
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
			if ((ret < 0) && (g_opt_flags & OPT_FLAGS_VERIFY)) {
				pr_fail("%s: timer_settime failed for timer '%s', "
					"errno=%d (%s)\n", args->name,
					stress_clock_name(timers[i]),
					errno, strerror(errno));
				goto timer_delete;
			}

			do {
				ret = timer_gettime(timer_id, &its);
				if ((ret < 0) && (g_opt_flags & OPT_FLAGS_VERIFY)) {
					pr_fail("%s: timer_gettime failed for timer '%s', "
						"errno=%d (%s)\n", args->name,
						stress_clock_name(timers[i]),
						errno, strerror(errno));
					goto timer_delete;
				}
				loops--;
			} while ((loops > 0) && g_keep_stressing_flag && (its.it_value.tv_nsec != 0));

timer_delete:
			ret = timer_delete(timer_id);
			if ((ret < 0) && (g_opt_flags & OPT_FLAGS_VERIFY)) {
				pr_fail("%s: timer_delete failed for timer '%s', "
					"errno=%d (%s)\n", args->name,
					stress_clock_name(timers[i]),
					errno, strerror(errno));
				break;
			}
		}
#endif
		inc_counter(args);
	} while (keep_stressing());

	return EXIT_SUCCESS;
}
#else
int stress_clock(const args_t *args)
{
	return stress_not_implemented(args);
}
#endif
