/*
 * Copyright (C) 2013-2019 Canonical, Ltd.
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

static const help_t help[] = {
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
	CLOCK_INFO(CLOCK_THREAD_CPUTIME_ID)
#endif
#if defined(CLOCK_MONOTONIC_ACTIVE)
	CLOCK_INFO(CLOCK_MONOTONIC_ACTIVE)
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

/*
 *  stress_clock()
 *	stress system by rapid clocking system calls
 */
static int stress_clock(const args_t *args)
{
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
			ret = clock_gettime(CLOCK_THREAD_CPUTIME_ID, &t);
			if ((ret < 0) && (g_opt_flags & OPT_FLAGS_VERIFY) &&
			    (errno != EINVAL) && (errno != ENOSYS))
				pr_fail("%s: clock_gettime failed for "
					"timer 'CLOCK_THREAD_CPUTIME_ID', errno=%d (%s)\n",
					args->name, errno, strerror(errno));

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

			/*
			 *  Exercise clock_getres and clock_gettime for each clock
			 */
			for (i = 0; i < SIZEOF_ARRAY(clocks); i++) {
				int ret;

				ret = clock_getres(clocks[i].id, &t);
				if ((ret < 0) && (g_opt_flags & OPT_FLAGS_VERIFY) &&
			            (errno != EINVAL) && (errno != ENOSYS))
					pr_fail("%s: clock_getres failed for "
						"timer '%s', errno=%d (%s)\n",
							args->name, clocks[i].name, errno, strerror(errno));
				ret = clock_gettime(clocks[i].id, &t);
				if ((ret < 0) && (g_opt_flags & OPT_FLAGS_VERIFY) &&
			            (errno != EINVAL) && (errno != ENOSYS))
					pr_fail("%s: clock_gettime failed for "
						"timer '%s', errno=%d (%s)\n",
						args->name, clocks[i].name, errno, strerror(errno));
			}
		}
#endif

#if defined(HAVE_CLOCK_NANOSLEEP)
		{
			size_t i;
			struct timespec t;

			/*
			 *  Exercise clock_nanosleep for each clock
			 */
			for (i = 0; i < SIZEOF_ARRAY(clocks_nanosleep); i++) {
				int ret;

				t.tv_sec = 0;
				t.tv_nsec = 25000;
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
		}
#endif

#if defined(HAVE_TIMER_CREATE) &&	\
    defined(HAVE_TIMER_DELETE) &&	\
    defined(HAVE_TIMER_GETTIME) &&	\
    defined(HAVE_TIMER_GETOVERRUN) &&	\
    defined(HAVE_TIMER_SETTIME) 
		{
			size_t i;

			/*
			 *  Stress the timers
			 */
			for (i = 0; i < SIZEOF_ARRAY(timers); i++) {
				timer_t timer_id;
				struct itimerspec its;
				struct sigevent sevp;
				int64_t loops = 1000000;
				int ret;

				(void)memset(&sevp, 0, sizeof(sevp));
				sevp.sigev_notify = SIGEV_NONE;
				ret = timer_create(timers[i], &sevp, &timer_id);
				if ((ret < 0) && (g_opt_flags & OPT_FLAGS_VERIFY)) {
					if ((errno == EINVAL) || (errno == EPERM))
						continue;
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
					ret = timer_getoverrun(timer_id);
					(void)ret;

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
		}
#endif
		inc_counter(args);
	} while (keep_stressing());

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
