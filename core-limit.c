/*
 * Copyright (C) 2014-2021 Canonical, Ltd.
 * Copyright (C) 2022-2025 Colin Ian King
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
#include "core-limit.h"

typedef struct {
	const shim_rlimit_resource_t resource;	/* RLIMIT_* resource */
	const char *opt;	/* stress-ng option to control resource */
} stress_rlimit_t;

static const stress_rlimit_t limits[] = {
#if defined(RLIMIT_AS)
	{ RLIMIT_AS,		"limit-as" },
#endif
#if defined(RLIMIT_CPU)
	{ RLIMIT_CPU,		NULL },
#endif
#if defined(RLIMIT_DATA)
	{ RLIMIT_DATA,		"limit-data" },
#endif
#if defined(RLIMIT_FSIZE)
	{ RLIMIT_FSIZE,		NULL },
#endif
#if defined(RLIMIT_LOCKS)
	{ RLIMIT_LOCKS,		NULL },
#endif
#if defined(RLIMIT_MEMLOCK)
	{ RLIMIT_MEMLOCK,	NULL },
#endif
#if defined(RLIMIT_MSGQUEUE)
	{ RLIMIT_MSGQUEUE,	NULL },
#endif
#if defined(RLIMIT_NICE)
	{ RLIMIT_NICE,		NULL },
#endif
#if defined(RLIMIT_NOFILE)
	{ RLIMIT_NOFILE,	NULL },
#endif
#if defined(RLIMIT_NPROC)
	{ RLIMIT_NPROC,		NULL },
#endif
#if defined(RLIMIT_RSS)
	{ RLIMIT_RSS,		NULL },
#endif
#if defined(RLIMIT_RTPRIO)
	{ RLIMIT_RTPRIO,	NULL },
#endif
#if defined(RLIMIT_RTTIME)
	{ RLIMIT_RTTIME,	NULL },
#endif
#if defined(RLIMIT_SIGPENDING)
	{ RLIMIT_SIGPENDING,	NULL },
#endif
#if defined(RLIMIT_STACK)
	{ RLIMIT_STACK,		"limit-stack" },
#endif
};

static void stress_set_limit(int resource, const char *opt)
{
	struct rlimit rlim;
	uint64_t val = 0;

	/* User optional override for a specific limit? */
	if (opt && stress_get_setting(opt, &val) && (val > 0)) {
		rlim.rlim_cur = (rlim_t)val;
		rlim.rlim_max = (rlim_t)val;
		(void)setrlimit(resource, &rlim);
		return;
	}

	if (UNLIKELY(getrlimit(resource, &rlim) < 0))
		return;

	rlim.rlim_cur = rlim.rlim_max;
	(void)setrlimit(resource, &rlim);
}

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

	for (i = 0; i < SIZEOF_ARRAY(limits); i++)
		stress_set_limit(limits[i].resource, limits[i].opt);

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

