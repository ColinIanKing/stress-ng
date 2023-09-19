// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2014-2021 Canonical, Ltd.
 * Copyright (C) 2022-2023 Colin Ian King
 *
 */
#include "stress-ng.h"
#include "core-limit.h"

static const shim_rlimit_resource_t limits[] = {
#if defined(RLIMIT_AS)
	RLIMIT_AS,
#endif
#if defined(RLIMIT_CPU)
	RLIMIT_CPU,
#endif
#if defined(RLIMIT_DATA)
	RLIMIT_DATA,
#endif
#if defined(RLIMIT_FSIZE)
	RLIMIT_FSIZE,
#endif
#if defined(RLIMIT_LOCKS)
	RLIMIT_LOCKS,
#endif
#if defined(RLIMIT_MEMLOCK)
	RLIMIT_MEMLOCK,
#endif
#if defined(RLIMIT_MSGQUEUE)
	RLIMIT_MSGQUEUE,
#endif
#if defined(RLIMIT_NICE)
	RLIMIT_NICE,
#endif
#if defined(RLIMIT_NOFILE)
	RLIMIT_NOFILE,
#endif
#if defined(RLIMIT_NPROC)
	RLIMIT_NPROC,
#endif
#if defined(RLIMIT_RSS)
	RLIMIT_RSS,
#endif
#if defined(RLIMIT_RTPRIO)
	RLIMIT_RTPRIO,
#endif
#if defined(RLIMIT_RTTIME)
	RLIMIT_RTTIME,
#endif
#if defined(RLIMIT_SIGPENDING)
	RLIMIT_SIGPENDING,
#endif
#if defined(RLIMIT_STACK)
	RLIMIT_STACK
#endif
};

/*
 *  stress_set_max_limits()
 *	push rlimits to maximum values allowed
 *	so we can stress a system to the maximum,
 *	we ignore any rlimit errors.
 */
void stress_set_max_limits(void)
{
	size_t i;
	struct rlimit rlim;

	for (i = 0; i < SIZEOF_ARRAY(limits); i++) {
		if (getrlimit(limits[i], &rlim) < 0)
			continue;
		rlim.rlim_cur = rlim.rlim_max;
		(void)setrlimit(limits[i], &rlim);
	}

#if defined(RLIMIT_NOFILE)
	{
		uint64_t max_fd = 0;

		(void)stress_get_setting("max-fd", &max_fd);
		if (max_fd != 0) {
			rlim.rlim_cur = (rlim_t)max_fd + 1;
			rlim.rlim_max = (rlim_t)max_fd + 1;
			(void)setrlimit(RLIMIT_NOFILE, &rlim);
		}
	}
#endif
}

